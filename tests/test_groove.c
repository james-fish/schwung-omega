/* test_groove.c — Groove rumble engine harness (GRV-01/02/03/05, Plan C-01).
 *
 * This is the Phase-C tempo-lock RED harness. It drives the REAL plugin
 * (move_plugin_init_v2 -> create_instance -> set_param -> on_midi ->
 * render_block) exactly like test_switch.c, through the tempo-DRIVABLE mock
 * host (mock_host_set_beat / mock_host_advance_beat / make_mock_host_null_
 * transport), and encodes every offline GRV assertion.
 *
 * RED STATE (expected): the groove engine (circular delay, tempo clock,
 * Page-1 controls, MONO sum) does not exist until Plan C-02. Until then the
 * groove-specific assertions (delayed tap energy, per-key responsiveness, MONO
 * channel equality) are EXPECTED to fail — that is the intended enabling state
 * this plan lands. The harness goes GREEN when C-02 wires the groove voice.
 *
 * Groove param keys: the PK_GRV_* macros are defined in C-02's omega.h. To let
 * THIS file compile independently (C-01's `make test-groove` must not error on
 * undefined macros), the keys are referenced as string literals that MIRROR the
 * C-02 PK_GRV_* naming exactly:
 *     PK_GRV_VOL    -> "grv_vol"
 *     PK_GRV_LENGTH -> "grv_length"
 *     PK_GRV_COLOR  -> "grv_color"
 *     PK_GRV_TAP1.. -> "grv_tap1".."grv_tap4"
 *     PK_GRV_MONO   -> "grv_mono"
 * (set_param ignores unknown keys today, so the file compiles + runs; the
 *  assertions bite once C-02 makes these keys live.)
 */
#include "omega.h"
#include "dsp_primitives.h"
#include "mock_host.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern const kick_model_vtable_t *g_models[];

#define BLOCK    128
#define NBLOCKS  64                    /* ~0.19 s — enough for the tempo EMA to settle */
#define NSAMP    (NBLOCKS * BLOCK * 2)

#define SR       44100.0

/* Groove Page-1 keys (mirror C-02's PK_GRV_* macros — see file header). */
#define KGRV_VOL    "grv_vol"
#define KGRV_LENGTH "grv_length"
#define KGRV_COLOR  "grv_color"
#define KGRV_TAP1   "grv_tap1"
#define KGRV_TAP2   "grv_tap2"
#define KGRV_TAP3   "grv_tap3"
#define KGRV_TAP4   "grv_tap4"
#define KGRV_MONO   "grv_mono"

/* Kick Page 1 keys (8) — model-agnostic defaults every model understands. */
static const char *k_page1_keys[] = {
    PK_PITCH, PK_LENGTH, PK_SUSTAIN, PK_CURVE,
    PK_ATTACK, PK_TRS_DEC, PK_TRS_TNE, PK_COLOR,
};
#define N_PAGE1 (int)(sizeof(k_page1_keys) / sizeof(k_page1_keys[0]))

/* Groove Page-1 keys under test for GRV-03 responsiveness. */
static const char *k_grv_keys[] = {
    KGRV_VOL, KGRV_LENGTH, KGRV_COLOR,
    KGRV_TAP1, KGRV_TAP2, KGRV_TAP3, KGRV_TAP4,
};
#define N_GRV (int)(sizeof(k_grv_keys) / sizeof(k_grv_keys[0]))

/* The DC-02 tempo formula the harness asserts against: integer samples per 16th
 * note = (60/bpm) * sr / 4, rounded. Distinct across BPMs => tap offsets track. */
static int samples_per_16th(double bpm) {
    return (int)((60.0 / bpm) * SR / 4.0 + 0.5);
}

/* Per-block beat advance that DRIVES the mock transport at `bpm`. This is the
 * inverse of C-02's bpm = dbeat*60*sr/frames: one 128-frame block advances the
 * beat by (bpm/60) quarter-notes-per-second * (frames/sr) seconds. */
static double dbeat_for_bpm(double bpm) {
    return (bpm / 60.0) * ((double)BLOCK / SR);
}

/* Select a NON-GEN model (index as decimal string, matches dsp.c's parse). */
static void select_model(plugin_api_v2_t *api, void *inst, int model_idx) {
    char idx[8];
    if (model_idx < 10) { idx[0] = (char)('0' + model_idx); idx[1] = '\0'; }
    else { idx[0] = (char)('0' + model_idx / 10); idx[1] = (char)('0' + model_idx % 10); idx[2] = '\0'; }
    api->set_param(inst, PK_MODEL, idx);
}

/* Prime Kick Page 1 to mid and open the groove voice (VOL up, all taps up,
 * moderate LENGTH/COLOR) so the rumble is audible when C-02 lands. */
static void prime_groove(plugin_api_v2_t *api, void *inst) {
    for (int i = 0; i < N_PAGE1; i++) api->set_param(inst, k_page1_keys[i], "0.5");
    /* PITCH is a Hz domain now (B2, VOICE-01); "0.5" clamps to the 30 Hz floor
     * and shortens the kick tail. Voice it at a normal ~55 Hz so the later taps
     * still capture tail energy. */
    api->set_param(inst, PK_PITCH, "55");
    api->set_param(inst, KGRV_VOL,    "0.8");
    api->set_param(inst, KGRV_LENGTH, "0.7");
    api->set_param(inst, KGRV_COLOR,  "0.6");
    api->set_param(inst, KGRV_TAP1,   "0.9");
    api->set_param(inst, KGRV_TAP2,   "0.7");
    api->set_param(inst, KGRV_TAP3,   "0.5");
    api->set_param(inst, KGRV_TAP4,   "0.3");
    api->set_param(inst, KGRV_MONO,   "0");
}

/* Render NBLOCKS while advancing the mock transport by `dbeat` before EACH block
 * (drives the tempo clock). Asserts every int16 sample is finite + in range.
 * Returns summed abs energy (int16 domain) for non-silence checks. If dbeat==0
 * the transport is left wherever the caller set it (for the fallback cases). */
static double render_driven(plugin_api_v2_t *api, void *inst, double dbeat,
                            int16_t *capture /* NSAMP or NULL */) {
    double energy = 0.0;
    int16_t out[BLOCK * 2];
    for (int b = 0; b < NBLOCKS; b++) {
        if (dbeat > 0.0) mock_host_advance_beat(dbeat);   /* drive transport */
        api->render_block(inst, out, BLOCK);
        for (int i = 0; i < BLOCK * 2; i++) {
            assert(out[i] >= INT16_MIN && out[i] <= INT16_MAX);   /* finite/bounded */
            energy += fabs((double)out[i]);
            if (capture) capture[b * BLOCK * 2 + i] = out[i];
        }
    }
    return energy;
}

/* RMS-envelope energy of an int16 buffer (for GRV-03 responsiveness deltas). */
static double buf_rms(const int16_t *buf, int nsamp) {
    double s = 0.0;
    for (int i = 0; i < nsamp; i++) { double x = (double)buf[i] / 32768.0; s += x * x; }
    return sqrt(s / (double)nsamp);
}

/* ---- GRV-02: parametric BPM sweep — the tempo-lock crux -------------------- */
static void test_bpm_sweep(void) {
    static const double bpms[] = { 120.0, 128.0, 174.0 };
    const int NB = (int)(sizeof(bpms) / sizeof(bpms[0]));

    /* The computed 16th-note intervals MUST be distinct across BPMs — this is
     * what "tap positions track BPM, never hardcoded 120" means (GRV-02 SC1).
     * 120 -> ~5513, 128 -> ~5168, 174 -> ~3802 frames. */
    int spq[3];
    for (int i = 0; i < NB; i++) spq[i] = samples_per_16th(bpms[i]);
    assert(spq[0] == 5513);            /* 120 BPM 16th */
    assert(spq[1] == 5168);            /* 128 BPM 16th */
    assert(spq[2] == 3802);            /* 174 BPM 16th */
    for (int i = 0; i < NB; i++)
        for (int j = i + 1; j < NB; j++)
            assert(spq[i] != spq[j]);  /* distinct => the interval is tempo-driven */

    /* For each BPM: reset the mock transport, init/create/select a NON-GEN model
     * (FM2 = index 0), prime the groove, trigger, then render while advancing the
     * beat at that BPM so the C-02 EMA settles. Output must be non-silent + finite
     * + bounded at every tempo (the tempo-driven rumble is alive). */
    for (int i = 0; i < NB; i++) {
        host_api_v1_t host = make_mock_host();     /* resets beat=0, bpm=120 */
        mock_host_set_bpm((float)bpms[i]);         /* fallback source also matches */
        plugin_api_v2_t *api2 = move_plugin_init_v2(&host);
        assert(api2 && api2->api_version == 2);
        void *inst = api2->create_instance("/tmp/omega", "{}");
        assert(inst);

        select_model(api2, inst, MODEL_FM2);
        prime_groove(api2, inst);
        uint8_t noteon[3] = { 0x90, 36, 100 };
        api2->on_midi(inst, noteon, 3, 0);

        double e = render_driven(api2, inst, dbeat_for_bpm(bpms[i]), NULL);
        assert(e > 1000.0);                        /* non-silent tempo-driven output */

        api2->destroy_instance(inst);
    }

    printf("test_groove: GRV-02 BPM sweep OK (spq 120=%d 128=%d 174=%d distinct)\n",
           spq[0], spq[1], spq[2]);
}

/* GRV-02 fallback chain: (a) NULL transport (both callbacks NULL) -> last-resort
 * 120 constant; (b) negative beat position -> get_bpm fallback. Both must render
 * finite/bounded/non-silent. */
static void test_fallback_chain(void) {
    /* (a) both callbacks NULL -> the 120 constant path. */
    {
        host_api_v1_t host = make_mock_host_null_transport();
        plugin_api_v2_t *api = move_plugin_init_v2(&host);
        assert(api && api->api_version == 2);
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        select_model(api, inst, MODEL_FM2);
        prime_groove(api, inst);
        uint8_t noteon[3] = { 0x90, 36, 100 };
        api->on_midi(inst, noteon, 3, 0);
        double e = render_driven(api, inst, 0.0, NULL);  /* no transport to drive */
        assert(e > 1000.0);                              /* still alive on 120 constant */
        api->destroy_instance(inst);
    }

    /* (b) beat position pinned negative (transport stopped) -> get_bpm fallback. */
    {
        host_api_v1_t host = make_mock_host();
        mock_host_set_bpm(128.0f);
        mock_host_set_beat(-1.0);                        /* stopped transport */
        plugin_api_v2_t *api = move_plugin_init_v2(&host);
        assert(api && api->api_version == 2);
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        select_model(api, inst, MODEL_FM2);
        prime_groove(api, inst);
        uint8_t noteon[3] = { 0x90, 36, 100 };
        api->on_midi(inst, noteon, 3, 0);
        double e = render_driven(api, inst, 0.0, NULL);  /* beat stays negative */
        assert(e > 1000.0);                              /* get_bpm path keeps it alive */
        api->destroy_instance(inst);
    }

    printf("test_groove: GRV-02 fallback chain OK (NULL-transport + negative-beat)\n");
}

/* ---- GRV-01: 4-tap delay presence ----------------------------------------- */
/* Trigger a kick, drive at 174 BPM (short 16th so all four taps land inside the
 * render window), and assert energy exists AFTER the initial attack window — the
 * delayed taps produce later energy that a pure single kick would not. Every
 * sample finite + bounded (int16 range). */
static void test_tap_delay_presence(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(174.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    select_model(api, inst, MODEL_FM2);
    prime_groove(api, inst);
    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);

    static int16_t buf[NSAMP];
    render_driven(api, inst, dbeat_for_bpm(174.0), buf);

    /* Late-window energy: sum |x| in the last third of the render (well past the
     * kick attack). With the groove taps active this MUST carry energy; a bare
     * decayed kick would be near-silent here. */
    int frames = NSAMP / 2;
    int late_start = (frames * 2) / 3;
    double late = 0.0;
    for (int f = late_start; f < frames; f++)
        late += fabs((double)buf[f * 2]) + fabs((double)buf[f * 2 + 1]);
    assert(late > 100.0);                 /* delayed-tap energy present (RED until C-02) */

    api->destroy_instance(inst);
    printf("test_groove: GRV-01 4-tap delayed energy present (late=%.0f)\n", late);
}

/* ---- GRV-03: Page-1 responsiveness ---------------------------------------- */
/* For each groove key, render with the key at "0.0" vs "1.0" (others primed) and
 * assert the RMS energy DIFFERS measurably; both extremes finite + bounded. */
static void test_page1_responsive(void) {
    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    static int16_t lo[NSAMP], hi[NSAMP];
    double dbeat = dbeat_for_bpm(128.0);
    mock_host_set_bpm(128.0f);

    int responsive = 0;
    for (int k = 0; k < N_GRV; k++) {
        /* lo extreme */
        select_model(api, inst, MODEL_FM2);
        prime_groove(api, inst);
        api->set_param(inst, k_grv_keys[k], "0.0");
        uint8_t noteon[3] = { 0x90, 36, 100 };
        api->on_midi(inst, noteon, 3, 0);
        mock_host_set_beat(0.0);
        render_driven(api, inst, dbeat, lo);

        /* hi extreme */
        prime_groove(api, inst);
        api->set_param(inst, k_grv_keys[k], "1.0");
        api->on_midi(inst, noteon, 3, 0);
        mock_host_set_beat(0.0);
        render_driven(api, inst, dbeat, hi);

        double rlo = buf_rms(lo, NSAMP), rhi = buf_rms(hi, NSAMP);
        if (fabs(rhi - rlo) > 1e-4) responsive++;   /* measurable change */
    }
    /* Every groove key must move the output (RED until C-02 makes them live). */
    assert(responsive == N_GRV);

    api->destroy_instance(inst);
    printf("test_groove: GRV-03 Page-1 responsiveness OK (%d/%d keys)\n", responsive, N_GRV);
}

/* ---- GRV-05: MONO force-sum ------------------------------------------------ */
/* With MONO on, the groove voice sums L+R so the rendered L channel == R channel
 * sample-for-sample. This is the load-bearing GRV-05 check (holds regardless of
 * whether the base kick is symmetric). Asymmetric taps are set so a non-mono
 * groove could differ L/R. */
static void test_mono_sum(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(128.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    static int16_t buf[NSAMP];
    double dbeat = dbeat_for_bpm(128.0);

    /* MONO on + asymmetric taps -> L must equal R everywhere. */
    select_model(api, inst, MODEL_FM2);
    prime_groove(api, inst);
    api->set_param(inst, KGRV_TAP1, "1.0");
    api->set_param(inst, KGRV_TAP3, "0.0");
    api->set_param(inst, KGRV_MONO, "1");
    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);
    render_driven(api, inst, dbeat, buf);

    int frames = NSAMP / 2;
    for (int f = 0; f < frames; f++)
        assert(buf[f * 2] == buf[f * 2 + 1]);       /* MONO forces L == R (GRV-05) */

    api->destroy_instance(inst);
    printf("test_groove: GRV-05 MONO force-sum OK (L == R under MONO)\n");
}

/* ---- GRV-04 (C-03): GEN clocks to the transport, not GEN_STEP_FRAMES ------- */
/* Select GEN, prime it, then render the SAME seeded sequence at 120 vs 174 BPM
 * while driving the mock transport. A faster tempo fires MORE steps over the
 * fixed window, so the two rendered buffers must DIFFER — proving gen.c reads
 * inst->groove.samples_per_16th and not the ~130-BPM hardcode. A min-energy
 * guard on both buffers makes the "differ" assertion non-trivial (neither can
 * be silent). */
static void gen_prime_groove2(plugin_api_v2_t *api, void *inst) {
    select_model(api, inst, MODEL_GEN);
    for (int i = 0; i < N_PAGE1; i++) api->set_param(inst, k_page1_keys[i], "0.5");
    api->set_param(inst, PK_GEN_SCALE,   "0.5");
    api->set_param(inst, PK_GEN_SEED,    "0.3");
    api->set_param(inst, PK_GEN_DENSITY, "0.7");
    api->set_param(inst, PK_GEN_SEQLEN,  "1.0");   /* full 16-step pattern */
    api->set_param(inst, PK_GEN_LPFFREQ, "0.8");   /* wide open so body passes */
    api->set_param(inst, PK_GEN_LPFPOLE, "0");
}

/* Settle the groove tempo clock at `bpm` on a FRESH instance, then trigger so
 * gen_trigger latches samples_per_16th at the fully-locked interval (the EMA
 * needs several blocks to converge + re-lock past its 0.5-BPM threshold), and
 * render the seeded GEN sequence. Returns via `buf`; sets *energy. */
static void gen_render_at_bpm(plugin_api_v2_t *api, double bpm,
                              int16_t *buf, double *energy) {
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    gen_prime_groove2(api, inst);

    /* Settle: drive the transport at `bpm` for enough blocks that the EMA locks
     * samples_per_16th to this tempo BEFORE we trigger (so step 0 uses the true
     * interval, 5513 @120 vs 3802 @174, not the 120-init value). */
    mock_host_set_beat(0.0);
    mock_host_set_bpm((float)bpm);
    double dbeat = dbeat_for_bpm(bpm);
    int16_t warm[BLOCK * 2];
    for (int b = 0; b < 40; b++) {
        mock_host_advance_beat(dbeat);
        api->render_block(inst, warm, BLOCK);   /* no trigger yet: just settle clock */
    }

    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);           /* latches locked samples_per_16th */
    *energy = render_driven(api, inst, dbeat, buf);
    api->destroy_instance(inst);
}

static void test_gen_clocks_to_bpm(void) {
    static int16_t b120[NSAMP], b174[NSAMP];

    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);

    double e120 = 0.0, e174 = 0.0;
    gen_render_at_bpm(api, 120.0, b120, &e120);
    gen_render_at_bpm(api, 174.0, b174, &e174);   /* same seed, faster tempo */

    /* Min-energy guard: both tempos must be audibly non-silent so the "differ"
     * assertion below cannot pass trivially with both buffers silent. */
    assert(e120 > 1000.0 && e174 > 1000.0);
    assert(buf_rms(b120, NSAMP) > 1e-4 && buf_rms(b174, NSAMP) > 1e-4);

    /* The transport clock changes the step rate -> the buffers differ. If gen.c
     * still used the fixed GEN_STEP_FRAMES, these would be byte-identical. */
    assert(memcmp(b120, b174, sizeof b120) != 0);

    printf("test_groove: GRV-04 GEN clocks to BPM OK (120 vs 174 differ, both live)\n");
}

/* ---- GRV-04 (C-03): LPF POLE 2-pole vs 4-pole differ ---------------------- */
/* With LPF FREQ low, a 4-pole (2 stages) cascade attenuates more than a 2-pole
 * (1 stage), so the same seeded GEN sequence renders with LESS RMS energy at
 * 4-pole. Both non-silent (min-energy guard). */
static void test_gen_lpf_pole(void) {
    static int16_t p2[NSAMP], p4[NSAMP];

    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    double dbeat = dbeat_for_bpm(128.0);

    /* 2-pole (1 stage), low cutoff. */
    gen_prime_groove2(api, inst);
    api->set_param(inst, PK_GEN_LPFFREQ, "0.1");   /* low cutoff -> pole count bites */
    api->set_param(inst, PK_GEN_LPFPOLE, "0");     /* 2-pole */
    mock_host_set_beat(0.0);
    mock_host_set_bpm(128.0f);
    { uint8_t noteon[3] = { 0x90, 36, 100 }; api->on_midi(inst, noteon, 3, 0); }
    render_driven(api, inst, dbeat, p2);

    /* 4-pole (2 stages), same cutoff + seed. */
    gen_prime_groove2(api, inst);
    api->set_param(inst, PK_GEN_LPFFREQ, "0.1");
    api->set_param(inst, PK_GEN_LPFPOLE, "1");     /* 4-pole */
    mock_host_set_beat(0.0);
    mock_host_set_bpm(128.0f);
    { uint8_t noteon[3] = { 0x90, 36, 100 }; api->on_midi(inst, noteon, 3, 0); }
    render_driven(api, inst, dbeat, p4);

    double r2 = buf_rms(p2, NSAMP), r4 = buf_rms(p4, NSAMP);
    assert(r2 > 1e-4);                             /* 2-pole path stays audible */
    /* The pole toggle measurably changes the sound (4-pole attenuates more at a
     * low cutoff; at minimum the two buffers differ). */
    if (!(fabs(r2 - r4) > 1e-5 || memcmp(p2, p4, sizeof p2) != 0))
        fprintf(stderr, "LPF POLE: r2=%.6f r4=%.6f (expected measurable diff)\n", r2, r4);
    assert(fabs(r2 - r4) > 1e-5 || memcmp(p2, p4, sizeof p2) != 0);
    assert(r4 <= r2 + 1e-3);                       /* 4-pole never LOUDER than 2-pole */

    api->destroy_instance(inst);
    printf("test_groove: GRV-04 LPF POLE OK (2-pole r=%.4f vs 4-pole r=%.4f)\n", r2, r4);
}

/* ---- Phase 1: zero-crossing fundamental estimator (GEN-PITCH) ------------- */
/* Downmix to mono and estimate the fundamental from the hysteresis zero-crossing
 * rate: f = crossings * SR / (2 * frames). A small threshold (relative to the
 * peak) ignores the quantised near-silent decay tail so only the loud fundamental
 * is counted. Robust for a near-sine GEN burst. Returns 0 if near-silent. */
static float autocorr_pitch_hz(const int16_t *buf, int nsamp) {
    int frames = nsamp / 2;
    float peak = 0.0f;
    for (int f = 0; f < frames; f++) {
        float v = 0.5f * ((float)buf[f * 2] + (float)buf[f * 2 + 1]);
        float a = v < 0 ? -v : v;
        if (a > peak) peak = a;
    }
    if (peak < 8.0f) return 0.0f;                    /* silence guard (int16 scale) */
    float thr = peak * 0.15f;                        /* hysteresis band */
    int crossings = 0, sign = 0;                     /* 0 = unknown, +1/-1 committed */
    for (int f = 0; f < frames; f++) {
        float v = 0.5f * ((float)buf[f * 2] + (float)buf[f * 2 + 1]);
        if (v > thr) { if (sign < 0) crossings++; sign = 1; }
        else if (v < -thr) { if (sign > 0) crossings++; sign = -1; }
        /* within +/-thr: hold the last committed sign (ignore tiny noise) */
    }
    return (float)crossings * (float)SR / (2.0f * (float)frames);
}

/* ---- Phase 1 RUMBLE-CORE: no runaway across all routes (1-01-01/1-02-02) --- */
/* Worst case (max LENGTH smear + max DRIVE + full REVERB + max DECAY), a kick
 * every beat (steady excitation) for each of the 4 ROUTE orders, ~5.4 s. Compare
 * a MID window (after the reverb has rung up) to a LATE window: a BIBO-stable
 * system settles, so late <= mid*1.3; a runaway would keep growing. Also every
 * sample finite/bounded and the late window not pinned at the rail. */
static void test_no_runaway(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(128.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    double dbeat = dbeat_for_bpm(128.0);
    const int NB = 1900;                 /* ~5.5 s at 128 frames/block */
    const int fpb = (int)(60.0 / 128.0 * SR);
    for (int route = 0; route < 4; route++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        select_model(api, inst, MODEL_FM2);
        prime_groove(api, inst);
        api->set_param(inst, KGRV_VOL,    "1.0");
        api->set_param(inst, KGRV_LENGTH, "1.0");   /* max smear (iter-3: len=1 = longest decay) */
        api->set_param(inst, PK_GRV_DRIVE, "1.0");
        api->set_param(inst, PK_GRV_RVMIX, "1.0");
        api->set_param(inst, PK_GRV_RVDECAY, "1.0");
        char rbuf[4] = { (char)('0' + route), 0, 0, 0 };
        api->set_param(inst, PK_GRV_ROUTE, rbuf);
        mock_host_set_beat(0.0);
        double mid = 0.0, late = 0.0; int nm = 0, nl = 0;
        int16_t out[BLOCK * 2];
        int fpos = 0, next = 0;
        int mid_lo = 700 * BLOCK, mid_hi = 900 * BLOCK;      /* settled window */
        int late_lo = 1700 * BLOCK, late_hi = 1900 * BLOCK;  /* much later */
        for (int b = 0; b < NB; b++) {
            if (fpos >= next) { uint8_t no[3]={0x90,36,100}; api->on_midi(inst,no,3,0); next += fpb; }
            mock_host_advance_beat(dbeat);
            api->render_block(inst, out, BLOCK);
            for (int i = 0; i < BLOCK * 2; i++) {
                assert(out[i] >= INT16_MIN && out[i] <= INT16_MAX);  /* finite/bounded */
                double xv = (double)out[i] / 32768.0;
                int gf = fpos + i / 2;
                if (gf >= mid_lo && gf < mid_hi)  { mid  += xv * xv; nm++; }
                if (gf >= late_lo && gf < late_hi){ late += xv * xv; nl++; }
            }
            fpos += BLOCK;
        }
        double mr = sqrt(mid / nm), lr = sqrt(late / nl);
        if (!(lr <= mr * 1.3 + 1e-4))
            fprintf(stderr, "no_runaway route %d: mid=%.5f late=%.5f (growth!)\n", route, mr, lr);
        assert(mr > 1e-3);               /* genuinely excited */
        assert(lr <= mr * 1.3 + 1e-4);   /* settled, not growing -> cannot run away */
        assert(lr < 0.95);               /* not pinned at the rail */
        api->destroy_instance(inst);
    }
    printf("test_groove: P1 no-runaway across 4 routes OK (bounded, settled tail)\n");
}

/* ---- Phase 1 RUMBLE-CORE: audible at default VOL (1-01-02, ONDEVICE #3) ---- */
/* The groove rumble must be clearly audible, not near-silent. Compare the late
 * window (after the kick attack) with groove OFF vs ON — ON must add real tail
 * energy and clear an absolute floor. 174 BPM so >=2 ghost taps land in-window. */
static void test_rumble_audible(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(174.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    double dbeat = dbeat_for_bpm(174.0);
    static int16_t off[NSAMP], on[NSAMP];
    uint8_t noteon[3] = { 0x90, 36, 100 };
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    select_model(api, inst, MODEL_FM2);
    prime_groove(api, inst);
    api->set_param(inst, KGRV_VOL, "0.0");       /* groove OFF reference */
    api->set_param(inst, KGRV_LENGTH, "0.5");
    api->on_midi(inst, noteon, 3, 0);
    render_driven(api, inst, dbeat, off);

    prime_groove(api, inst);
    api->set_param(inst, KGRV_VOL, "1.0");       /* groove ON at default-ish */
    api->set_param(inst, KGRV_LENGTH, "0.5");
    api->on_midi(inst, noteon, 3, 0);
    render_driven(api, inst, dbeat, on);

    int tail = NSAMP / 2;                          /* last half = after attack */
    double roff = buf_rms(off + tail, NSAMP - tail);
    double ron  = buf_rms(on  + tail, NSAMP - tail);
    if (!(ron > 0.003))
        fprintf(stderr, "rumble_audible: tail rms on=%.5f (want >0.003)\n", ron);
    assert(ron > 0.003);            /* absolute audibility floor (not near-silent) */
    assert(ron > roff * 2.0);       /* groove clearly adds a rumble tail */
    api->destroy_instance(inst);
    printf("test_groove: P1 rumble audible at default OK (tail off=%.4f on=%.4f)\n", roff, ron);
}

/* ---- Phase 1 RUMBLE-CORE: LENGTH morphs distinct<->smear (1-01-03) --------- */
static void test_length_morph(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(174.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    double dbeat = dbeat_for_bpm(174.0);
    static int16_t dist[NSAMP], smear[NSAMP];
    uint8_t noteon[3] = { 0x90, 36, 100 };
    for (int pass = 0; pass < 2; pass++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        select_model(api, inst, MODEL_FM2);
        prime_groove(api, inst);
        api->set_param(inst, PK_LENGTH, "0.2");       /* short kick -> isolated taps */
        api->set_param(inst, KGRV_VOL, "1.0");
        api->set_param(inst, KGRV_COLOR, "1.0");      /* wide open */
        api->set_param(inst, KGRV_LENGTH, pass == 0 ? "0.0" : "1.0"); /* distinct(0) vs smear(1) */
        api->on_midi(inst, noteon, 3, 0);
        render_driven(api, inst, dbeat, pass == 0 ? dist : smear);
    }
    int tail = (NSAMP * 4) / 5;                        /* last 20% */
    double td = buf_rms(dist + tail, NSAMP - tail);
    double ts = buf_rms(smear + tail, NSAMP - tail);
    if (!(ts > td * 1.3))
        fprintf(stderr, "length_morph: distinct tail=%.5f smear tail=%.5f (want smear>1.3*distinct)\n", td, ts);
    assert(ts > td * 1.3);     /* long decay fills the tail -> smear */
    printf("test_groove: P1 LENGTH morph OK (distinct tail=%.4f smear tail=%.4f)\n", td, ts);
}

/* ---- Phase 1 FX-ROUTE: routing order changes output (1-02-01) ------------- */
static void test_route_changes_output(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(128.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    double dbeat = dbeat_for_bpm(128.0);
    static int16_t r0[NSAMP], r2[NSAMP];
    uint8_t noteon[3] = { 0x90, 36, 100 };
    const char *routes[2] = { "0", "2" };   /* Rumble>Drive>Reverb vs Reverb first */
    for (int i = 0; i < 2; i++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        select_model(api, inst, MODEL_FM2);
        prime_groove(api, inst);
        api->set_param(inst, KGRV_VOL, "1.0");
        api->set_param(inst, PK_GRV_DRIVE, "0.7");
        api->set_param(inst, PK_GRV_RVMIX, "0.7");
        api->set_param(inst, PK_GRV_RVDECAY, "0.7");
        api->set_param(inst, PK_GRV_ROUTE, routes[i]);
        mock_host_set_beat(0.0);
        api->on_midi(inst, noteon, 3, 0);
        render_driven(api, inst, dbeat, i == 0 ? r0 : r2);
        api->destroy_instance(inst);
    }
    int diff = 0;
    for (int i = 0; i < NSAMP; i++) { int d = r0[i] - r2[i]; if (d < 0) d = -d; if (d > 2) diff++; }
    if (!(diff > NSAMP / 100))
        fprintf(stderr, "route_changes: only %d/%d samples differ\n", diff, NSAMP);
    assert(diff > NSAMP / 100);   /* order clearly changes the output */
    printf("test_groove: P1 route order changes output OK (%d samples differ)\n", diff);
}

/* ---- Phase 1 GEN-FILTER: GEN LP sweep attenuates highs (1-03-01) ---------- */
static void test_gen_filter_sweep(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(128.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    double dbeat = dbeat_for_bpm(128.0);
    static int16_t open_b[NSAMP], closed_b[NSAMP];
    uint8_t noteon[3] = { 0x90, 36, 100 };
    for (int pass = 0; pass < 2; pass++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        select_model(api, inst, MODEL_FM2);
        api->set_param(inst, PK_GRV_TYPE, "1");       /* GEN groove */
        api->set_param(inst, PK_GRV_VOL,  "1.0");
        api->set_param(inst, PK_GRV_GSCALE, "0");     /* Unquantized */
        api->set_param(inst, PK_GRV_GWAVE,  "2");     /* saw -> HF content */
        api->set_param(inst, PK_GRV_GDENSITY, "1.0");
        api->set_param(inst, PK_GRV_GFOLD, "0.5");
        api->set_param(inst, PK_GRV_COLOR, pass == 0 ? "1.0" : "0.0");  /* open vs closed */
        mock_host_set_beat(0.0);
        /* No kick note-on: the GEN groove runs off the transport, so the output is
         * the PURE (filtered) groove — the unfiltered kick would mask the sweep. */
        (void)noteon;
        render_driven(api, inst, dbeat, pass == 0 ? open_b : closed_b);
        api->destroy_instance(inst);
    }
    /* Zero-crossing rate: closing the LP removes highs -> lower ZCR. */
    int zo = 0, zc = 0; int16_t po = 0, pc = 0; int frames = NSAMP / 2;
    for (int f = 0; f < frames; f++) {
        if ((open_b[f*2] >= 0) != (po >= 0)) zo++; po = open_b[f*2];
        if ((closed_b[f*2] >= 0) != (pc >= 0)) zc++; pc = closed_b[f*2];
    }
    if (!(zc < zo * 0.9))
        fprintf(stderr, "gen_filter_sweep: zcr open=%d closed=%d (want closed<0.9*open)\n", zo, zc);
    assert(zc < (int)(zo * 0.9));   /* closed LP measurably darker */
    printf("test_groove: P1 GEN filter sweep OK (zcr open=%d closed=%d)\n", zo, zc);
}

/* ---- Phase 1 GEN-PITCH: unquantized ROOT tracks + reaches sub-bass (1-04) -- */
static void test_gen_root_pitch(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(128.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    double dbeat = dbeat_for_bpm(128.0);
    static int16_t lo[NSAMP], hi[NSAMP];
    uint8_t noteon[3] = { 0x90, 36, 100 };
    float plo = 0.0f, phi = 0.0f;
    for (int pass = 0; pass < 2; pass++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        select_model(api, inst, MODEL_FM2);
        api->set_param(inst, PK_GRV_TYPE, "1");       /* GEN groove */
        api->set_param(inst, PK_GRV_VOL,  "1.0");
        api->set_param(inst, PK_GRV_GSCALE, "0");     /* Unquantized */
        api->set_param(inst, PK_GRV_GWAVE,  "0");     /* sine -> clean pitch */
        api->set_param(inst, PK_GRV_GDENSITY, "1.0"); /* fire every step */
        api->set_param(inst, PK_GRV_GRANGE, "1");     /* narrow span (1 degree) -> stable pitch */
        api->set_param(inst, PK_GRV_COLOR, "1.0");
        api->set_param(inst, PK_GRV_GROOT, pass == 0 ? "20" : "2000");  /* low vs high root, Hz */
        mock_host_set_beat(0.0);
        /* No kick note-on: measure the PURE GEN groove pitch (the kick's ~50 Hz
         * fundamental would corrupt the autocorrelation estimate). */
        (void)noteon;
        render_driven(api, inst, dbeat, pass == 0 ? lo : hi);
        api->destroy_instance(inst);
    }
    plo = autocorr_pitch_hz(lo, NSAMP);
    phi = autocorr_pitch_hz(hi, NSAMP);
    if (!(plo > 15.0f && plo < 80.0f))
        fprintf(stderr, "gen_root_pitch: low root pitch=%.1f Hz (want 15..80 sub-bass)\n", plo);
    assert(plo > 15.0f && plo < 80.0f);   /* lowest reaches the sub-bass register (#21) */
    if (!(phi > plo * 1.5f))
        fprintf(stderr, "gen_root_pitch: low=%.1f high=%.1f (want high>1.5*low)\n", plo, phi);
    assert(phi > plo * 1.5f);             /* ROOT HZ tracks upward */
    printf("test_groove: P1 GEN unquantized root pitch OK (low=%.1f Hz high=%.1f Hz)\n", plo, phi);
}

/* ---- GRVX-03 (C1-02): groove FX (drive/filter/LFO/reverb) move + bound ----- */
static void test_groove_fx(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(128.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    static int16_t base[NSAMP], fx[NSAMP];
    double dbeat = dbeat_for_bpm(128.0);
    uint8_t noteon[3] = { 0x90, 36, 100 };

    const char *keys[] = { PK_GRV_DRIVE, PK_GRV_RVMIX, PK_GRV_LFOAMT, PK_GRV_FILTYPE };
    for (int k = 0; k < 4; k++) {
        select_model(api, inst, MODEL_FM2);
        prime_groove(api, inst);
        api->set_param(inst, KGRV_VOL, "0.9");
        api->set_param(inst, keys[k], "0.0");
        api->on_midi(inst, noteon, 3, 0);
        render_driven(api, inst, dbeat, base);
        prime_groove(api, inst);
        api->set_param(inst, KGRV_VOL, "0.9");
        api->set_param(inst, keys[k], k==3 ? "1" : "0.9");   /* filter: HP index */
        api->on_midi(inst, noteon, 3, 0);
        render_driven(api, inst, dbeat, fx);
        for (int i = 0; i < NSAMP; i++) assert(fx[i] >= -32768 && fx[i] <= 32767);
        assert(memcmp(base, fx, sizeof base) != 0);   /* FX audibly changes output */
    }
    api->destroy_instance(inst);
    printf("test_groove: GRVX-03 groove FX (drive/reverb/LFO/filter) responsive + bounded OK\n");
}

/* ---- GRVX-04/05 (C1-03): GEN groove type decoupled from the kick model ----- */
static void test_gen_groove_type(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(128.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    static int16_t a[NSAMP], b[NSAMP];
    double dbeat = dbeat_for_bpm(128.0);
    uint8_t noteon[3] = { 0x90, 36, 100 };

    /* FM2 kick model + GEN groove TYPE (decoupled — GRVX-01/04). */
    select_model(api, inst, MODEL_FM2);
    api->set_param(inst, PK_GRV_TYPE, "1");     /* GEN groove */
    api->set_param(inst, PK_GRV_VOL,  "0.9");
    api->set_param(inst, PK_GRV_GSEED, "25");
    api->on_midi(inst, noteon, 3, 0);
    double e = render_driven(api, inst, dbeat, a);
    for (int i = 0; i < NSAMP; i++) assert(a[i] >= -32768 && a[i] <= 32767);
    assert(e > 1e3);   /* generative rumble audible with a non-GEN kick model */

    /* SEED changes the sequence -> different output. */
    api->set_param(inst, PK_GRV_GSEED, "108");
    api->on_midi(inst, noteon, 3, 0);
    render_driven(api, inst, dbeat, b);
    assert(memcmp(a, b, sizeof a) != 0);

    /* UI: GEN groove type exposes the Gen Seq + Gen Tone pages (GRVX-04). */
    char ui[8192];
    int n = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
    assert(n > 0);
    assert(strstr(ui, PK_GRV_GSCALE) != NULL);
    assert(strstr(ui, PK_GRV_GRETRIG) != NULL);
    assert(strstr(ui, "\"groove3\"") != NULL);   /* two GEN pages */

    /* Transport STOP: with no beat advance the sequencer stops and the voice
     * decays (GRVX-05). Render several blocks with a static beat -> near silence. */
    mock_host_set_beat(4.0);   /* set but do NOT advance */
    static int16_t q[NSAMP];
    for (int blk = 0; blk < (NSAMP/(BLOCK*2)); blk++)
        api->render_block(inst, q + blk*BLOCK*2, BLOCK);   /* no advance_beat */
    /* Render again (still no advance): the tail must have decayed low. */
    for (int blk = 0; blk < (NSAMP/(BLOCK*2)); blk++)
        api->render_block(inst, q + blk*BLOCK*2, BLOCK);
    double eq = buf_rms(q, NSAMP);
    assert(eq < 0.05);   /* stopped transport -> sequencer silent */

    api->destroy_instance(inst);
    printf("test_groove: GRVX-04/05 GEN groove type decoupled + stop-on-stop OK (e=%.0f)\n", e);
}

/* ---- E3: GEN groove scale expansion + ROOT/RANGE (SC1-5) -------------------- */
static void test_e3_gen_groove_enhancements(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(128.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    api->set_param(inst, PK_GRV_TYPE, "1");   /* GEN groove */
    api->set_param(inst, PK_GRV_VOL,  "0.9");

    /* SC1: OPT_SCALE must include >=13 options; spot-check exotic names. */
    char ui[8192];
    int n = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
    assert(n > 0);
    assert(strstr(ui, "Hirajoshi")   != NULL);
    assert(strstr(ui, "Diminished")  != NULL);
    assert(strstr(ui, "Unquantized") != NULL);
    assert(strstr(ui, "Whole Tone")  != NULL);

    /* SC4: GEN groove1 must show ROOT and RANGE, not TAP controls. */
    assert(strstr(ui, PK_GRV_GROOT)  != NULL);
    assert(strstr(ui, PK_GRV_GRANGE) != NULL);
    assert(strstr(ui, "Gen Groove")  != NULL);
    /* TAP controls must NOT appear in the GEN layout. */
    assert(strstr(ui, "\"" PK_GRV_TAP1 "\"") == NULL);
    assert(strstr(ui, "\"" PK_GRV_TAP2 "\"") == NULL);

    /* SC5: Both effect pages named "Groove Effects". */
    assert(strstr(ui, "Groove Effects") != NULL);

    /* SC2/SC3: GROOT moves the root pitch — render enough blocks for steps to fire. */
    api->set_param(inst, PK_GRV_GSCALE, "1");   /* Chromatic (UI idx 1) */
    double dbeat = dbeat_for_bpm(128.0);
    api->set_param(inst, PK_GRV_GROOT, "32");    /* low root, Hz */
    static int16_t low[NSAMP], high[NSAMP];
    render_driven(api, inst, dbeat, low);
    api->set_param(inst, PK_GRV_GROOT, "1262");  /* high root, Hz */
    render_driven(api, inst, dbeat, high);
    /* Different root → different pitch → different output (not byte-identical). */
    assert(memcmp(low, high, sizeof low) != 0);

    /* SC3: ROOT is declared in Hz (20..2000) in BOTH modes, because groove.c
     * maps it to the same Hz whatever the scale and the host writes the value
     * in the declared range. Quantized used to declare 0..1. */
    static const char root_hz[] =
        "\"key\":\"" PK_GRV_GROOT "\",\"name\":\"Root\",\"short_name\":\"ROOT\","
        "\"type\":\"float\",\"min\":20,\"max\":2000";
    for (int scale = 0; scale <= 1; scale++) {          /* 0 = Unquantized, 1 = Chromatic */
        api->set_param(inst, PK_GRV_GSCALE, scale ? "1" : "0");
        n = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
        assert(n > 0);
        assert(strstr(ui, root_hz) != NULL);
    }
    api->set_param(inst, PK_GRV_GSCALE, "0");   /* Unquantized, as before */

    /* SC1 expanded: RANGE changes the sequence pitch span. */
    api->set_param(inst, PK_GRV_GSCALE, "2");   /* Major */
    api->set_param(inst, PK_GRV_GSEED,  "38");
    api->set_param(inst, PK_GRV_GRANGE, "1");   /* narrow: 1 degree */
    static int16_t narrow_buf[NSAMP], wide_buf[NSAMP];
    render_driven(api, inst, dbeat, narrow_buf);
    api->set_param(inst, PK_GRV_GRANGE, "24");  /* wide: 24 degrees */
    render_driven(api, inst, dbeat, wide_buf);
    /* Different range → different pitch assignments → different output. */
    assert(memcmp(narrow_buf, wide_buf, sizeof narrow_buf) != 0);

    api->destroy_instance(inst);
    printf("test_groove: E3 GEN groove enhancements OK (scales+root+range+unquantized+page layout)\n");
}

/* The host writes a value in the range ui_hierarchy DECLARES, never 0..1. These
 * five keys declare steps / Hz / degrees, and used to go through the 0..1 clamp,
 * so every value the knob grid could send above 1 pinned at the maximum (Seq
 * Len 32 played 64, Root 440 Hz played 2 kHz, Rotate 0 played full left). */
static void test_gen_declared_units(void) {
    static groove_state_t g;
    groove_init(&g);
    groove_set_param(&g, PK_GRV_GSEQLEN, "32");  assert(g.gen_seqlen == 32);
    groove_set_param(&g, PK_GRV_GSEQLEN, "1");   assert(g.gen_seqlen == 1);
    groove_set_param(&g, PK_GRV_GSEQLEN, "99");  assert(g.gen_seqlen == 64);
    groove_set_param(&g, PK_GRV_GSEED, "64");    assert(g.gen_seed_raw == 64);
    groove_set_param(&g, PK_GRV_GSEED, "127");   assert(g.gen_seed_raw == 127);
    groove_set_param(&g, PK_GRV_GRANGE, "12");   assert(g.gen_range == 12);
    groove_set_param(&g, PK_GRV_GROTATE, "0");   assert(g.gen_rotate == 0.0f);
    groove_set_param(&g, PK_GRV_GROTATE, "-32"); assert(g.gen_rotate == -1.0f);
    groove_set_param(&g, PK_GRV_GROTATE, "16");  assert(g.gen_rotate == 0.5f);
    groove_set_param(&g, PK_GRV_GROOT, "440");   assert(fabsf(g.gen_base_hz - 440.0f) < 0.01f);
    /* A scale change recomputes the base from gen_root_param: must keep 440. */
    groove_set_param(&g, PK_GRV_GSCALE, "3");    assert(fabsf(g.gen_base_hz - 440.0f) < 0.5f);
    groove_set_param(&g, PK_GRV_GROOT, "5");     assert(g.gen_base_hz == 20.0f);
    printf("test_groove: gen params take declared units OK\n");
}

int main(void) {
    omega_primitives_selfcheck();

    /* Guard: the harness must be exercising a registered non-GEN model so the
     * eventual GREEN assertions are non-trivial (groove fed by a live kick, not
     * silence-in silence-out). If FM2 were unregistered the "non-silent" checks
     * would pass on garbage / never bite. */
    assert(g_models[MODEL_FM2] != NULL);
    assert(g_models[MODEL_FM2]->render != NULL);

    /* GRV-02 (the crux): tap positions track driven BPM + guarded fallback. */
    test_bpm_sweep();
    test_fallback_chain();

    /* GRV-01 / GRV-03 / GRV-05. */
    test_tap_delay_presence();
    test_page1_responsive();
    test_mono_sum();
    test_groove_fx();         /* GRVX-03: groove drive/filter/LFO/reverb */
    test_gen_groove_type();   /* GRVX-04/05: GEN groove decoupled + retrigger/stop */

    /* Phase 1: feedback-free FIR rumble + FX routing + GEN filter/pitch. */
    test_no_runaway();            /* RUMBLE-CORE 1-01-01 + FX-ROUTE 1-02-02 */
    test_rumble_audible();        /* RUMBLE-CORE 1-01-02 (ONDEVICE #3) */
    test_length_morph();          /* RUMBLE-CORE 1-01-03 */
    test_route_changes_output();  /* FX-ROUTE 1-02-01 */
    test_gen_filter_sweep();      /* GEN-FILTER 1-03-01 */
    test_gen_root_pitch();        /* GEN-PITCH 1-04-01 */

    /* GRV-04 (C-03): GEN clocks to the transport (not GEN_STEP_FRAMES) + the
     * LPF POLE 2/4-pole cascade toggle. Requires GEN registered. */
    assert(g_models[MODEL_GEN] != NULL);
    assert(g_models[MODEL_GEN]->render != NULL);
    test_gen_clocks_to_bpm();
    test_gen_lpf_pole();
    test_e3_gen_groove_enhancements();  /* E3: scales + ROOT/RANGE + page layout */
    test_gen_declared_units();

    printf("test_groove: ALL TESTS PASSED\n");
    return 0;
}
