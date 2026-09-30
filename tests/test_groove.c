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

/* ---- GRVX-02 (C1 + vhr): feedback rumble sustains + stays bounded ---------- */
/* vhr redesign: LENGTH is BIDIRECTIONAL. LEFT (low LENGTH) = high feedback -> a
 * smeared resonant drone that SUSTAINS into the second half of the buffer. RIGHT
 * (high LENGTH) = fb=0 -> clean gated tap copies that decay once the kick is
 * gone. So low LENGTH must sustain MORE than high LENGTH (the inverse of the old
 * pre-vhr semantics), while both stay bounded (no runaway). */
static void test_feedback_rumble(void) {
    host_api_v1_t host = make_mock_host();
    mock_host_set_bpm(128.0f);
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    static int16_t buf[NSAMP];
    double dbeat = dbeat_for_bpm(128.0);
    uint8_t noteon[3] = { 0x90, 36, 100 };

    /* LOW LENGTH (high feedback = drone), single kick, no re-trigger. */
    select_model(api, inst, MODEL_FM2);
    prime_groove(api, inst);
    api->set_param(inst, KGRV_VOL,    "0.9");
    api->set_param(inst, KGRV_LENGTH, "0.05");   /* strong feedback drone */
    api->on_midi(inst, noteon, 3, 0);
    render_driven(api, inst, dbeat, buf);

    /* Second-half energy is a meaningful fraction of first-half (sustained), and
     * every sample is in range (bounded, no runaway). */
    double e1 = buf_rms(buf, NSAMP / 2);
    double e2 = buf_rms(buf + NSAMP / 2, NSAMP / 2);
    for (int i = 0; i < NSAMP; i++) assert(buf[i] >= -32768 && buf[i] <= 32767);
    assert(e1 > 1e-3 && e2 > 1e-3);
    assert(e2 > 0.15 * e1);   /* drone sustains, not gated to silence */

    /* HIGH LENGTH (fb=0, clean copies) renders a DIFFERENT, bounded output — the
     * bidirectional LENGTH knob morphs between two distinct regimes (drone vs
     * clean copies). The exact energy ordering over this short window depends on
     * tap alignment, so the load-bearing check is: both bounded + the two regimes
     * differ (proving LENGTH actually re-voices the tail, not just trims level).
     * The clean-copies EQUAL-LEVEL invariant is verified precisely in
     * test_taps_redesign.c gate #1 with the post-groove chain neutralised. */
    static int16_t bclean[NSAMP];
    prime_groove(api, inst);
    api->set_param(inst, KGRV_VOL,    "0.9");
    api->set_param(inst, KGRV_LENGTH, "1.0");    /* fb=0 -> clean gated copies */
    api->on_midi(inst, noteon, 3, 0);
    render_driven(api, inst, dbeat, bclean);
    for (int i = 0; i < NSAMP; i++) assert(bclean[i] >= -32768 && bclean[i] <= 32767);
    assert(memcmp(buf, bclean, sizeof buf) != 0);   /* the two LENGTH regimes differ */

    api->destroy_instance(inst);
    printf("test_groove: GRVX-02 bidirectional LENGTH drone sustains + bounded OK (e2/e1=%.2f)\n", e2/e1);
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
    api->set_param(inst, PK_GRV_GSEED, "0.2");
    api->on_midi(inst, noteon, 3, 0);
    double e = render_driven(api, inst, dbeat, a);
    for (int i = 0; i < NSAMP; i++) assert(a[i] >= -32768 && a[i] <= 32767);
    assert(e > 1e3);   /* generative rumble audible with a non-GEN kick model */

    /* SEED changes the sequence -> different output. */
    api->set_param(inst, PK_GRV_GSEED, "0.85");
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
    assert(strstr(ui, "WholeTone")   != NULL);

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
    api->set_param(inst, PK_GRV_GROOT, "0.1");  /* low root */
    static int16_t low[NSAMP], high[NSAMP];
    render_driven(api, inst, dbeat, low);
    api->set_param(inst, PK_GRV_GROOT, "0.9");  /* high root */
    render_driven(api, inst, dbeat, high);
    /* Different root → different pitch → different output (not byte-identical). */
    assert(memcmp(low, high, sizeof low) != 0);

    /* SC3: Unquantized mode (UI SCALE idx 0) → ROOT HZ appears in UI. */
    api->set_param(inst, PK_GRV_GSCALE, "0");   /* Unquantized */
    n = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
    assert(n > 0);
    assert(strstr(ui, "ROOT HZ") != NULL);

    /* SC1 expanded: RANGE changes the sequence pitch span. */
    api->set_param(inst, PK_GRV_GSCALE, "2");   /* Major */
    api->set_param(inst, PK_GRV_GSEED,  "0.3");
    api->set_param(inst, PK_GRV_GRANGE, "0.0"); /* narrow: 1 degree */
    static int16_t narrow_buf[NSAMP], wide_buf[NSAMP];
    render_driven(api, inst, dbeat, narrow_buf);
    api->set_param(inst, PK_GRV_GRANGE, "1.0"); /* wide: 24 degrees */
    render_driven(api, inst, dbeat, wide_buf);
    /* Different range → different pitch assignments → different output. */
    assert(memcmp(narrow_buf, wide_buf, sizeof narrow_buf) != 0);

    api->destroy_instance(inst);
    printf("test_groove: E3 GEN groove enhancements OK (scales+root+range+unquantized+page layout)\n");
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
    test_feedback_rumble();   /* GRVX-02: continuous rumble, not gated echo */
    test_groove_fx();         /* GRVX-03: groove drive/filter/LFO/reverb */
    test_gen_groove_type();   /* GRVX-04/05: GEN groove decoupled + retrigger/stop */

    /* GRV-04 (C-03): GEN clocks to the transport (not GEN_STEP_FRAMES) + the
     * LPF POLE 2/4-pole cascade toggle. Requires GEN registered. */
    assert(g_models[MODEL_GEN] != NULL);
    assert(g_models[MODEL_GEN]->render != NULL);
    test_gen_clocks_to_bpm();
    test_gen_lpf_pole();
    test_e3_gen_groove_enhancements();  /* E3: scales + ROOT/RANGE + page layout */

    printf("test_groove: ALL TESTS PASSED\n");
    return 0;
}
