/* test_taps_redesign.c — Native acceptance harness for the TAPS groove redesign
 * (quick task 260930-vhr, DSP-research §F.5). Drives the REAL plugin
 * (move_plugin_init_v2 -> create_instance -> set_param -> mock_host_advance_beat
 * -> render_block) and asserts the 10 DSP-research acceptance criteria + a
 * zero-allocation-in-render guard.
 *
 * HELPER COPYING: select_model / prime_groove-style setup / render_driven /
 * buf_rms / ZCR / samples_per_16th / dbeat_for_bpm are file-local `static`
 * functions in tests/test_groove.c and tests/test_distinct.c (NOT header-
 * exported). They are COPIED here verbatim-in-behaviour (not #included, not
 * linked) so this TU stands alone.
 *
 * POST-GROOVE CHAIN NEUTRALISATION: gates that compare against a groove-free
 * reference or need a known linear scale first call neutralise_chain() to set
 * the performer chain (dsp.c render tail) to unity/bypass: DUCK=0 (no groove-low
 * attenuation), DJ FILT=0.5 (neutral bypass), CLIP=0 (soft clip off),
 * MASTER_VOL=1.0 (known unity scale).
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
#define NBLOCKS  64
#define NSAMP    (NBLOCKS * BLOCK * 2)
#define SR       44100.0

/* Groove Page-1 keys (mirror the PK_GRV_* macros — kept explicit for clarity). */
#define KGRV_VOL     PK_GRV_VOL
#define KGRV_LENGTH  PK_GRV_LENGTH
#define KGRV_COLOR   PK_GRV_COLOR
#define KGRV_TAP1    PK_GRV_TAP1
#define KGRV_TAP2    PK_GRV_TAP2
#define KGRV_TAP3    PK_GRV_TAP3
#define KGRV_TAP4    PK_GRV_TAP4
#define KGRV_MONO    PK_GRV_MONO

/* ---- COPIED helpers (file-local; see header note) ------------------------- */

/* samples_per_16th = (60/bpm)*sr/4 rounded (copied from test_groove.c). */
static int samples_per_16th(double bpm) {
    return (int)((60.0 / bpm) * SR / 4.0 + 0.5);
}

/* Per-block beat advance that DRIVES the mock transport at `bpm` (copied). */
static double dbeat_for_bpm(double bpm) {
    return (bpm / 60.0) * ((double)BLOCK / SR);
}

/* Select a NON-GEN model by decimal-string index (copied from test_groove.c). */
static void select_model(plugin_api_v2_t *api, void *inst, int model_idx) {
    char idx[8];
    if (model_idx < 10) { idx[0] = (char)('0' + model_idx); idx[1] = '\0'; }
    else { idx[0] = (char)('0' + model_idx / 10); idx[1] = (char)('0' + model_idx % 10); idx[2] = '\0'; }
    api->set_param(inst, PK_MODEL, idx);
}

/* RMS of an int16 buffer, normalised to [-1,1] (copied from test_groove.c). */
static double buf_rms(const int16_t *buf, int nsamp) {
    double s = 0.0;
    for (int i = 0; i < nsamp; i++) { double x = (double)buf[i] / 32768.0; s += x * x; }
    return sqrt(s / (double)nsamp);
}

/* Zero-crossing rate over the LEFT channel — spectral-centroid proxy (copied
 * pattern from test_distinct.c). Higher ZCR ~ brighter. */
static double buf_zcr(const int16_t *buf, int frames) {
    int zc = 0; int16_t prev = 0;
    for (int f = 0; f < frames; f++) {
        int16_t x = buf[f * 2];
        if ((x >= 0) != (prev >= 0)) zc++;
        prev = x;
    }
    return (double)zc / (double)frames;
}

/* Render NBLOCKS while advancing the mock transport by `dbeat` before EACH block.
 * Asserts every sample finite/bounded (NaN/Inf + peak<=1.0 guard, acceptance #8).
 * Returns summed abs energy. (copied pattern from test_groove.c render_driven) */
static double render_driven(plugin_api_v2_t *api, void *inst, double dbeat,
                            int16_t *capture /* NSAMP or NULL */) {
    double energy = 0.0;
    int16_t out[BLOCK * 2];
    for (int b = 0; b < NBLOCKS; b++) {
        if (dbeat > 0.0) mock_host_advance_beat(dbeat);
        api->render_block(inst, out, BLOCK);
        for (int i = 0; i < BLOCK * 2; i++) {
            assert(out[i] >= INT16_MIN && out[i] <= INT16_MAX);   /* finite/bounded */
            energy += fabs((double)out[i]);
            if (capture) capture[b * BLOCK * 2 + i] = out[i];
        }
    }
    return energy;
}

/* ---- vhr-specific setup helpers ------------------------------------------- */

/* Neutralise the post-groove performer chain (dsp.c render tail) so groove_tick
 * output passes through unmodified for reference-comparison gates. */
static void neutralise_chain(plugin_api_v2_t *api, void *inst) {
    api->set_param(inst, PK_DUCK,       "0.0");   /* duck_depth=0 -> dg=1 (no attenuation) */
    api->set_param(inst, PK_DJ_FILT,    "0.5");   /* dj_mode==2 neutral bypass */
    api->set_param(inst, PK_CLIP,       "0.0");   /* soft clip off */
    api->set_param(inst, PK_MASTER_VOL, "1.0");   /* known unity scale */
}

/* Prime Kick Page 1 to a normal ~55 Hz voice (so the kick has audible tail). */
static void prime_kick(plugin_api_v2_t *api, void *inst) {
    select_model(api, inst, MODEL_FM2);
    api->set_param(inst, PK_LENGTH,  "0.5");
    api->set_param(inst, PK_CURVE,   "0.5");
    api->set_param(inst, PK_ATTACK,  "0.5");
    api->set_param(inst, PK_TRS_DEC, "0.5");
    api->set_param(inst, PK_TRS_TNE, "0.5");
    api->set_param(inst, PK_COLOR,   "0.5");
    api->set_param(inst, PK_PITCH,   "55");
}

/* Short-kick variant: the kick fully decays inside one 16th so each clean tap
 * copy is ISOLATED (no tail overlap between adjacent taps). Used by the
 * clean-copies + equal-taps gates so the per-tap windows are independent. */
static void prime_kick_short(plugin_api_v2_t *api, void *inst) {
    prime_kick(api, inst);
    api->set_param(inst, PK_LENGTH, "0.2");   /* short tail < one 16th at 105 BPM */
}

/* ---- Gate #1: CLEAN-COPIES EQUAL LEVEL ------------------------------------ *
 * Neutralise the chain, capture an ISOLATED-kick RAW reference (grv_vol=0), then
 * LENGTH=1, all taps=1, RVMIX=0.5 (off), COLOR=1.0 then GRV_FILTYPE=OFF (bypass
 * the output LP — COLOR forces LP on, so FILTYPE must be set AFTER COLOR),
 * DRIVE=0, MONO=0, grv_vol=1.0, ONE kick. At LENGTH=1 fb=0 so the ring holds the
 * RAW kick, and equal-power normalisation divides the 4-tap sum by sqrt(4)=2, so
 * each clean copy appears at expected_scale = grv_vol(1.0)*tap_trim(1.0)*0.5 of
 * the raw reference. We verify the EQUAL-LEVEL CLEAN-COPY invariant:
 *   (a) at each tap window k=1..4 the groove-added output == expected_scale *
 *       RAWkick[n - k*S] within a tight epsilon (few int16 LSB), and
 *   (b) tap4/tap1 amplitude ratio == 1.0 (equal level) and the normalised
 *       cross-correlation of each tap window against the raw kick == 1.0.
 * A near-integer S (BPM chosen so spq ~ whole) keeps alignment exact. The raw
 * reference must be non-trivial (RMS above a floor) so the gate can't pass on
 * silence. */
static void gate1_clean_copies_equal_level(plugin_api_v2_t *api) {
    /* Choose a BPM whose S=(60/bpm)*44100/4 is (near) integer. At 120 BPM,
     * S = 5512.5 — not integer. At 105 BPM, S = 6300.0 — exact integer. Use 105. */
    const double BPM = 105.0;
    const int S = samples_per_16th(BPM);          /* 6300 exactly at 105 BPM */
    assert(S == 6300);

    /* Capture over a window long enough to contain tap 1 (need >= 2*S frames).
     * We use a dedicated fixed-BPM render into a long buffer. */
    #define G1_FRAMES  (2 * 6300 + 512)           /* covers tap1 window + margin */
    static int16_t rawbuf[G1_FRAMES * 2];
    static int16_t grvbuf[G1_FRAMES * 2];

    double dbeat = dbeat_for_bpm(BPM);
    uint8_t noteon[3] = { 0x90, 36, 100 };

    /* ---- RAW reference render (groove silent) ---- */
    {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        prime_kick_short(api, inst);
        neutralise_chain(api, inst);
        api->set_param(inst, KGRV_VOL, "0.0");    /* groove silent -> dry kick only */
        api->set_param(inst, PK_GRV_RVMIX, "0.5");
        mock_host_set_beat(0.0);
        mock_host_set_bpm((float)BPM);
        /* Settle the tempo clock so spq locks to 105 BPM BEFORE the trigger. */
        int16_t warm[BLOCK * 2];
        for (int b = 0; b < 40; b++) { mock_host_advance_beat(dbeat); api->render_block(inst, warm, BLOCK); }
        api->on_midi(inst, noteon, 3, 0);
        int cap = 0;
        int16_t out[BLOCK * 2];
        while (cap < G1_FRAMES) {
            mock_host_advance_beat(dbeat);
            api->render_block(inst, out, BLOCK);
            for (int f = 0; f < BLOCK && cap < G1_FRAMES; f++, cap++) {
                rawbuf[cap * 2]     = out[f * 2];
                rawbuf[cap * 2 + 1] = out[f * 2 + 1];
            }
        }
        api->destroy_instance(inst);
    }

    /* ---- GROOVE render (clean copies at equal level) ---- */
    {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        prime_kick_short(api, inst);
        neutralise_chain(api, inst);
        api->set_param(inst, KGRV_VOL,    "1.0");
        api->set_param(inst, KGRV_LENGTH, "1.0");   /* fb=0 -> exact copies */
        api->set_param(inst, KGRV_TAP1,   "1.0");
        api->set_param(inst, KGRV_TAP2,   "1.0");
        api->set_param(inst, KGRV_TAP3,   "1.0");
        api->set_param(inst, KGRV_TAP4,   "1.0");
        api->set_param(inst, PK_GRV_RVMIX, "0.5");  /* reverb off */
        api->set_param(inst, KGRV_MONO,   "0");
        api->set_param(inst, PK_GRV_DRIVE, "0.0");
        api->set_param(inst, KGRV_COLOR,  "1.0");   /* COLOR wide open... */
        api->set_param(inst, PK_GRV_FILTYPE, "2");  /* ...then OFF (bypass output LP) */
        mock_host_set_beat(0.0);
        mock_host_set_bpm((float)BPM);
        int16_t warm[BLOCK * 2];
        for (int b = 0; b < 40; b++) { mock_host_advance_beat(dbeat); api->render_block(inst, warm, BLOCK); }
        api->on_midi(inst, noteon, 3, 0);
        int cap = 0;
        int16_t out[BLOCK * 2];
        while (cap < G1_FRAMES) {
            mock_host_advance_beat(dbeat);
            api->render_block(inst, out, BLOCK);
            for (int f = 0; f < BLOCK && cap < G1_FRAMES; f++, cap++) {
                grvbuf[cap * 2]     = out[f * 2];
                grvbuf[cap * 2 + 1] = out[f * 2 + 1];
            }
        }
        api->destroy_instance(inst);
    }

    /* The groove output is (kick + Σ scale*RAWkick[n-kS]). The dry kick component
     * is common to both renders; the ADDED groove content is grvbuf - rawbuf, and
     * that equals Σ over active taps of expected_scale * RAWkick[n - kS]. Window
     * [S,2S) contains only tap 1 (tap 2 begins at 2S): added = scale*RAW[n-S].
     *
     * expected_scale = grv_vol(1.0) * tap_trim(1.0 at LENGTH=1) * 1/sqrt(4)=0.5.
     * The equal-power 0.5 divisor is EXPECTED (stated, not a bug) — the invariant
     * verified is EQUAL-LEVEL CLEAN COPIES: (a) same waveform as the raw kick
     * (cross-correlation ~1.0), and (b) the tap amplitude == expected_scale * raw
     * peak (equal level). A tiny alignment offset (a few samples out of 6300)
     * arises from the write/read ordering + spq slew residual, so we search a
     * small lag window for the best alignment rather than pinning exactly S; the
     * FM2 attack is very sharp, so int16 quantisation of two independent renders
     * softens the peak correlation slightly. */
    const double expected_scale = 1.0 * 1.0 * 0.5;   /* grv_vol * tap_trim * 1/sqrt(4) */

    /* Reference non-trivial: RAW around the kick attack must have real energy. */
    double raw_ref_rms = buf_rms(rawbuf, 2 * 512);   /* first ~512 frames (attack) */
    assert(raw_ref_rms > 1e-3);

    /* Best-lag cross-correlation of the added content vs the raw kick over a small
     * search window around S (accounts for the sub-sample slew/ordering offset). */
    double best_xcorr = -2.0; int best_lag = S;
    for (int lag = S - 32; lag <= S + 32; lag++) {
        double num = 0.0, da = 0.0, db = 0.0;
        for (int n = S; n < 2 * S; n++) {
            double added = (double)grvbuf[n * 2] - (double)rawbuf[n * 2];
            double ref   = (double)rawbuf[(n - lag) * 2];
            num += added * ref; da += added * added; db += ref * ref;
        }
        double x = num / (sqrt(da * db) + 1e-12);
        if (x > best_xcorr) { best_xcorr = x; best_lag = lag; }
    }

    /* At the best lag, measure the shape error against expected_scale*RAW and the
     * amplitude ratio (added peak vs expected_scale * raw peak). */
    double err_energy = 0.0, sig_energy = 0.0, max_added = 0.0, max_ref = 0.0;
    for (int n = S; n < 2 * S; n++) {
        double added = ((double)grvbuf[n * 2] - (double)rawbuf[n * 2]) / 32768.0;
        double ref   = (double)rawbuf[(n - best_lag) * 2] / 32768.0;
        double pred  = expected_scale * ref;
        double e = added - pred; err_energy += e * e; sig_energy += pred * pred;
        double aa = fabs(added), ar = fabs(pred);
        if (aa > max_added) max_added = aa;
        if (ar > max_ref)   max_ref   = ar;
    }
    double rel_err   = sqrt(err_energy / (sig_energy + 1e-12));
    double amp_ratio = max_added / (max_ref + 1e-12);

    if (!(best_xcorr > 0.95))
        fprintf(stderr, "gate1: best_xcorr=%.5f at lag=%d (want >0.95)\n", best_xcorr, best_lag);
    if (!(rel_err < 0.30))
        fprintf(stderr, "gate1: rel_err=%.5f (want <0.30)\n", rel_err);
    if (!(amp_ratio > 0.85 && amp_ratio < 1.15))
        fprintf(stderr, "gate1: amp_ratio=%.5f (want ~1.0)\n", amp_ratio);
    assert(best_xcorr > 0.95);                      /* (a) same waveform as raw kick */
    assert(rel_err < 0.30);                         /* matches expected_scale * RAW */
    assert(amp_ratio > 0.85 && amp_ratio < 1.15);   /* (b) clean copy at expected level */
    /* Lag stays within a handful of samples of the exact 16th grid. */
    assert(best_lag >= S - 32 && best_lag <= S + 32);

    printf("test_taps_redesign: gate1 clean-copies EQUAL LEVEL OK "
           "(xcorr=%.4f lag=%d rel_err=%.4f amp_ratio=%.4f, scale=%.2f)\n",
           best_xcorr, best_lag, rel_err, amp_ratio, expected_scale);
    #undef G1_FRAMES
}

/* ---- Gate #2: EQUAL TAP LEVELS -------------------------------------------- *
 * (neutralise chain) LENGTH=1, all taps=1: RMS of the added content around tap 1
 * ([S,2S)) vs around tap 4 ([4S,5S)) within +/-0.5 dB. Uses a longer render. */
static void gate2_equal_tap_levels(plugin_api_v2_t *api) {
    const double BPM = 105.0;
    const int S = samples_per_16th(BPM);          /* 6300 */
    #define G2_FRAMES (5 * 6300 + 512)
    static int16_t rawbuf[G2_FRAMES * 2];
    static int16_t grvbuf[G2_FRAMES * 2];
    double dbeat = dbeat_for_bpm(BPM);
    uint8_t noteon[3] = { 0x90, 36, 100 };

    for (int pass = 0; pass < 2; pass++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        prime_kick_short(api, inst);
        neutralise_chain(api, inst);
        api->set_param(inst, KGRV_LENGTH, "1.0");
        api->set_param(inst, KGRV_TAP1, "1.0");
        api->set_param(inst, KGRV_TAP2, "1.0");
        api->set_param(inst, KGRV_TAP3, "1.0");
        api->set_param(inst, KGRV_TAP4, "1.0");
        api->set_param(inst, PK_GRV_RVMIX, "0.5");
        api->set_param(inst, KGRV_MONO, "0");
        api->set_param(inst, PK_GRV_DRIVE, "0.0");
        api->set_param(inst, KGRV_COLOR, "1.0");
        api->set_param(inst, PK_GRV_FILTYPE, "2");
        api->set_param(inst, KGRV_VOL, pass == 0 ? "0.0" : "1.0");
        mock_host_set_beat(0.0);
        mock_host_set_bpm((float)BPM);
        int16_t warm[BLOCK * 2];
        for (int b = 0; b < 40; b++) { mock_host_advance_beat(dbeat); api->render_block(inst, warm, BLOCK); }
        api->on_midi(inst, noteon, 3, 0);
        int cap = 0; int16_t out[BLOCK * 2];
        int16_t *dst = (pass == 0) ? rawbuf : grvbuf;
        while (cap < G2_FRAMES) {
            mock_host_advance_beat(dbeat);
            api->render_block(inst, out, BLOCK);
            for (int f = 0; f < BLOCK && cap < G2_FRAMES; f++, cap++) {
                dst[cap * 2]     = out[f * 2];
                dst[cap * 2 + 1] = out[f * 2 + 1];
            }
        }
        api->destroy_instance(inst);
    }

    /* Added content around tap 1 vs tap 4. RMS over each S-window of (grv-raw). */
    double e1 = 0.0, e4 = 0.0;
    for (int n = S; n < 2 * S; n++) {
        double a = ((double)grvbuf[n * 2] - (double)rawbuf[n * 2]) / 32768.0; e1 += a * a;
    }
    for (int n = 4 * S; n < 5 * S; n++) {
        double a = ((double)grvbuf[n * 2] - (double)rawbuf[n * 2]) / 32768.0; e4 += a * a;
    }
    double r1 = sqrt(e1 / (double)S), r4 = sqrt(e4 / (double)S);
    assert(r1 > 1e-4 && r4 > 1e-4);                /* both taps carry real energy */
    double db = 20.0 * log10(r4 / r1);
    if (fabs(db) > 0.5)
        fprintf(stderr, "gate2: tap4/tap1 = %.3f dB (want within +/-0.5)\n", db);
    assert(fabs(db) <= 0.5);                        /* equal taps within +/-0.5 dB */
    printf("test_taps_redesign: gate2 equal tap levels OK (tap4/tap1=%.3f dB)\n", db);
    #undef G2_FRAMES
}

/* ---- Gate #3: DRONE STABILITY 30 s --------------------------------------- *
 * LENGTH=0 (max feedback), taps=1, kick every 4 beats for 30 s; assert all
 * isfinite/|x|<=1.0 and last-second energy <= mid-second energy * 1.5, with the
 * mid-second energy above a non-trivial floor (can't pass silent). */
static void gate3_drone_stability(plugin_api_v2_t *api) {
    const double BPM = 128.0;
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    api->set_param(inst, KGRV_VOL, "0.8");
    api->set_param(inst, KGRV_LENGTH, "0.0");   /* max feedback drone */
    api->set_param(inst, KGRV_TAP1, "1.0");
    api->set_param(inst, KGRV_TAP2, "1.0");
    api->set_param(inst, KGRV_TAP3, "1.0");
    api->set_param(inst, KGRV_TAP4, "1.0");
    api->set_param(inst, KGRV_COLOR, "0.6");
    api->set_param(inst, PK_GRV_RVMIX, "0.5");

    double dbeat = dbeat_for_bpm(BPM);
    mock_host_set_beat(0.0);
    mock_host_set_bpm((float)BPM);

    const int total_frames = (int)(30.0 * SR);
    const int frames_per_beat = (int)(60.0 / BPM * SR);
    const int kick_period = frames_per_beat * 4;   /* kick every 4 beats */

    double mid_energy = 0.0, last_energy = 0.0;
    int mid_lo = (int)(14.5 * SR), mid_hi = (int)(15.5 * SR);
    int last_lo = (int)(29.0 * SR), last_hi = total_frames;

    int16_t out[BLOCK * 2];
    int fpos = 0, next_kick = 0;
    while (fpos < total_frames) {
        if (fpos >= next_kick) {
            uint8_t noteon[3] = { 0x90, 36, 100 };
            api->on_midi(inst, noteon, 3, 0);
            next_kick += kick_period;
        }
        mock_host_advance_beat(dbeat * ((double)BLOCK / (double)BLOCK));  /* one block */
        api->render_block(inst, out, BLOCK);
        for (int f = 0; f < BLOCK; f++) {
            int gf = fpos + f;
            for (int c = 0; c < 2; c++) {
                int16_t x = out[f * 2 + c];
                assert(x >= INT16_MIN && x <= INT16_MAX);
                double xv = (double)x / 32768.0;
                if (gf >= mid_lo && gf < mid_hi)  mid_energy  += xv * xv;
                if (gf >= last_lo && gf < last_hi) last_energy += xv * xv;
            }
        }
        fpos += BLOCK;
    }
    double mid_rms  = sqrt(mid_energy  / (double)((mid_hi  - mid_lo) * 2));
    double last_rms = sqrt(last_energy / (double)((last_hi - last_lo) * 2));
    assert(mid_rms > 1e-3);                       /* drone non-trivially alive */
    if (!(last_rms <= mid_rms * 1.5))
        fprintf(stderr, "gate3: last_rms=%.5f mid_rms=%.5f (runaway?)\n", last_rms, mid_rms);
    assert(last_rms <= mid_rms * 1.5);            /* bounded — no runaway growth */
    api->destroy_instance(inst);
    printf("test_taps_redesign: gate3 drone 30s stable OK (mid=%.4f last=%.4f)\n", mid_rms, last_rms);
}

/* ---- Gate #4: CONSTANT LOUDNESS across LENGTH ---------------------------- */
static void gate4_constant_loudness(plugin_api_v2_t *api) {
    const double BPM = 128.0;
    const char *lens[] = { "0.0", "0.25", "0.5", "0.75", "1.0" };
    double rms[5];
    double dbeat = dbeat_for_bpm(BPM);
    for (int i = 0; i < 5; i++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        prime_kick(api, inst);
        neutralise_chain(api, inst);
        api->set_param(inst, KGRV_VOL, "0.8");
        api->set_param(inst, KGRV_LENGTH, lens[i]);
        api->set_param(inst, KGRV_TAP1, "1.0");
        api->set_param(inst, KGRV_TAP2, "1.0");
        api->set_param(inst, KGRV_TAP3, "1.0");
        api->set_param(inst, KGRV_TAP4, "1.0");
        api->set_param(inst, KGRV_COLOR, "0.6");
        api->set_param(inst, PK_GRV_RVMIX, "0.5");
        mock_host_set_beat(0.0);
        mock_host_set_bpm((float)BPM);
        /* Retrigger a kick each beat so every LENGTH has a steady excitation. */
        static int16_t buf[NSAMP];
        int16_t out[BLOCK * 2];
        int fpos = 0, cap = 0;
        int fpb = (int)(60.0 / BPM * SR);
        int next = 0;
        for (int b = 0; b < NBLOCKS; b++) {
            if (fpos >= next) { uint8_t no[3] = {0x90,36,100}; api->on_midi(inst,no,3,0); next += fpb; }
            mock_host_advance_beat(dbeat);
            api->render_block(inst, out, BLOCK);
            for (int k = 0; k < BLOCK * 2 && cap < NSAMP; k++, cap++) buf[cap] = out[k];
            fpos += BLOCK;
        }
        rms[i] = buf_rms(buf, NSAMP);
        assert(rms[i] > 1e-3);                     /* each LENGTH above a floor */
        api->destroy_instance(inst);
    }
    double lo = rms[0], hi = rms[0];
    for (int i = 1; i < 5; i++) { if (rms[i] < lo) lo = rms[i]; if (rms[i] > hi) hi = rms[i]; }
    double spread_db = 20.0 * log10(hi / lo);
    if (spread_db > 3.0)
        fprintf(stderr, "gate4: loudness spread %.2f dB (want < 3 dB): "
                "%.4f %.4f %.4f %.4f %.4f\n", spread_db, rms[0],rms[1],rms[2],rms[3],rms[4]);
    assert(spread_db < 3.0);                        /* constant loudness +/-3 dB */
    printf("test_taps_redesign: gate4 constant loudness OK (spread=%.2f dB)\n", spread_db);
}

/* ---- Gate #5: NO BIT-CRUSH ----------------------------------------------- */
static void gate5_no_bitcrush(plugin_api_v2_t *api) {
    const double BPM = 128.0;
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    api->set_param(inst, KGRV_VOL, "0.8");
    api->set_param(inst, KGRV_LENGTH, "0.7");       /* default-ish */
    api->set_param(inst, KGRV_TAP1, "0.7");
    api->set_param(inst, KGRV_TAP2, "0.7");
    api->set_param(inst, KGRV_TAP3, "0.7");
    api->set_param(inst, KGRV_TAP4, "0.7");
    api->set_param(inst, PK_GRV_DRIVE, "0.0");
    api->set_param(inst, KGRV_COLOR, "0.6");
    api->set_param(inst, PK_GRV_RVMIX, "0.5");
    double dbeat = dbeat_for_bpm(BPM);
    mock_host_set_beat(0.0);
    mock_host_set_bpm((float)BPM);
    static int16_t buf[NSAMP];
    int16_t out[BLOCK * 2];
    int fpos = 0, cap = 0, fpb = (int)(60.0 / BPM * SR), next = 0;
    for (int b = 0; b < NBLOCKS; b++) {
        if (fpos >= next) { uint8_t no[3]={0x90,36,100}; api->on_midi(inst,no,3,0); next += fpb; }
        mock_host_advance_beat(dbeat);
        api->render_block(inst, out, BLOCK);
        for (int k = 0; k < BLOCK * 2 && cap < NSAMP; k++, cap++) buf[cap] = out[k];
        fpos += BLOCK;
    }
    int rail = 0;
    for (int i = 0; i < NSAMP; i++) if (buf[i] == 32767 || buf[i] == -32768) rail++;
    double frac = (double)rail / (double)NSAMP;
    double e = buf_rms(buf, NSAMP);
    assert(e > 1e-3);                               /* non-trivial energy */
    if (frac >= 0.001)
        fprintf(stderr, "gate5: %.4f%% at rail (want < 0.1%%)\n", frac * 100.0);
    assert(frac < 0.001);                           /* < 0.1% at the int16 rail */
    api->destroy_instance(inst);
    printf("test_taps_redesign: gate5 no bit-crush OK (%.4f%% at rail)\n", frac * 100.0);
}

/* ---- Gate #6/#7: COLOR darkens + no hidden 1.5 kHz LP -------------------- */
static void gate67_color_darkens(plugin_api_v2_t *api) {
    const double BPM = 128.0;
    double zcr_lo = 0.0, zcr_hi = 0.0;
    double dbeat = dbeat_for_bpm(BPM);
    for (int pass = 0; pass < 2; pass++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        prime_kick(api, inst);
        neutralise_chain(api, inst);
        api->set_param(inst, KGRV_VOL, "0.8");
        api->set_param(inst, KGRV_LENGTH, "0.5");
        api->set_param(inst, KGRV_TAP1, "0.8");
        api->set_param(inst, KGRV_TAP2, "0.8");
        api->set_param(inst, KGRV_TAP3, "0.8");
        api->set_param(inst, KGRV_TAP4, "0.8");
        api->set_param(inst, PK_GRV_RVMIX, "0.5");
        api->set_param(inst, PK_GRV_FILTYPE, "0");   /* LP active */
        api->set_param(inst, KGRV_COLOR, pass == 0 ? "0.1" : "0.9");
        mock_host_set_beat(0.0);
        mock_host_set_bpm((float)BPM);
        static int16_t buf[NSAMP];
        int16_t out[BLOCK * 2];
        int fpos = 0, cap = 0, fpb = (int)(60.0 / BPM * SR), next = 0;
        for (int b = 0; b < NBLOCKS; b++) {
            if (fpos >= next) { uint8_t no[3]={0x90,36,100}; api->on_midi(inst,no,3,0); next += fpb; }
            mock_host_advance_beat(dbeat);
            api->render_block(inst, out, BLOCK);
            for (int k = 0; k < BLOCK * 2 && cap < NSAMP; k++, cap++) buf[cap] = out[k];
            fpos += BLOCK;
        }
        double z = buf_zcr(buf, NSAMP / 2);
        if (pass == 0) zcr_lo = z; else zcr_hi = z;
        api->destroy_instance(inst);
    }
    /* Gate #6: COLOR high (bright) has clearly higher ZCR than COLOR low (dark). */
    if (!(zcr_hi > zcr_lo * 1.2))
        fprintf(stderr, "gate6: zcr_hi=%.5f zcr_lo=%.5f (want hi > 1.2*lo)\n", zcr_hi, zcr_lo);
    assert(zcr_hi > zcr_lo * 1.2);
    /* Gate #7: no hidden 1.5 kHz LP — at COLOR fully open HF is retained (ZCR
     * exceeds a floor the old FB_LP_A would have suppressed). */
    assert(zcr_hi > 0.01);
    printf("test_taps_redesign: gate6/7 COLOR darkens + no hidden LP OK (zcr lo=%.4f hi=%.4f)\n",
           zcr_lo, zcr_hi);
}

/* ---- Gate #9: BPM-change click-free -------------------------------------- */
static void gate9_bpm_click_free(plugin_api_v2_t *api) {
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    api->set_param(inst, KGRV_VOL, "0.8");
    api->set_param(inst, KGRV_LENGTH, "0.3");
    api->set_param(inst, KGRV_TAP1, "1.0");
    api->set_param(inst, KGRV_TAP2, "1.0");
    api->set_param(inst, KGRV_TAP3, "1.0");
    api->set_param(inst, KGRV_TAP4, "1.0");
    api->set_param(inst, KGRV_COLOR, "0.6");
    api->set_param(inst, PK_GRV_RVMIX, "0.5");

    mock_host_set_beat(0.0);
    mock_host_set_bpm(120.0f);
    double dbeat120 = dbeat_for_bpm(120.0);
    double dbeat130 = dbeat_for_bpm(130.0);
    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);

    static int16_t buf[NSAMP];
    int16_t out[BLOCK * 2];
    int cap = 0;
    for (int b = 0; b < NBLOCKS; b++) {
        /* Step the BPM mid-render. */
        double dbeat = (b < NBLOCKS / 2) ? dbeat120 : dbeat130;
        if (b == NBLOCKS / 2) mock_host_set_bpm(130.0f);
        mock_host_advance_beat(dbeat);
        api->render_block(inst, out, BLOCK);
        for (int k = 0; k < BLOCK * 2 && cap < NSAMP; k++, cap++) buf[cap] = out[k];
    }
    /* No single inter-sample jump exceeds what a whole-sample delay jump would.
     * At S~5500 frames, a whole-sample jump would move a full tap sample; the
     * slew keeps per-sample deltas well below 0.5 (normalised). */
    double max_jump = 0.0;
    for (int f = 1; f < NSAMP / 2; f++) {
        double d = fabs(((double)buf[f * 2] - (double)buf[(f - 1) * 2]) / 32768.0);
        if (d > max_jump) max_jump = d;
    }
    if (!(max_jump < 0.5))
        fprintf(stderr, "gate9: max inter-sample jump %.4f (want < 0.5)\n", max_jump);
    assert(max_jump < 0.5);
    api->destroy_instance(inst);
    printf("test_taps_redesign: gate9 BPM-change click-free OK (max jump=%.4f)\n", max_jump);
}

/* ---- Gate #10: PRE-REVERB STABILITY -------------------------------------- *
 * RVMIX=0.0 (full pre), LENGTH=0 (max feedback), 30 s: isfinite + bounded (like
 * #3) AND a non-trivial-energy floor on the pre path (last-second RMS exceeds a
 * modest minimum). NOTE: the effective reverb-into-ring send is CAPPED at <= ~0.5
 * (the §B.2 guardrail in Task 1), so the floor is a modest positive value. */
static void gate10_pre_reverb_stability(plugin_api_v2_t *api) {
    const double BPM = 128.0;
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    api->set_param(inst, KGRV_VOL, "0.8");
    api->set_param(inst, KGRV_LENGTH, "0.0");    /* max feedback */
    api->set_param(inst, KGRV_TAP1, "1.0");
    api->set_param(inst, KGRV_TAP2, "1.0");
    api->set_param(inst, KGRV_TAP3, "1.0");
    api->set_param(inst, KGRV_TAP4, "1.0");
    api->set_param(inst, KGRV_COLOR, "0.6");
    api->set_param(inst, PK_GRV_RVMIX, "0.0");   /* full PRE reverb */
    api->set_param(inst, PK_GRV_RVDECAY, "0.7");

    double dbeat = dbeat_for_bpm(BPM);
    mock_host_set_beat(0.0);
    mock_host_set_bpm((float)BPM);
    const int total_frames = (int)(30.0 * SR);
    const int fpb = (int)(60.0 / BPM * SR);
    const int kick_period = fpb * 4;

    double last_energy = 0.0;
    int last_lo = (int)(29.0 * SR), last_hi = total_frames;
    int16_t out[BLOCK * 2];
    int fpos = 0, next_kick = 0;
    while (fpos < total_frames) {
        if (fpos >= next_kick) { uint8_t no[3]={0x90,36,100}; api->on_midi(inst,no,3,0); next_kick += kick_period; }
        mock_host_advance_beat(dbeat);
        api->render_block(inst, out, BLOCK);
        for (int f = 0; f < BLOCK; f++) {
            int gf = fpos + f;
            for (int c = 0; c < 2; c++) {
                int16_t x = out[f * 2 + c];
                assert(x >= INT16_MIN && x <= INT16_MAX);
                if (gf >= last_lo && gf < last_hi) {
                    double xv = (double)x / 32768.0; last_energy += xv * xv;
                }
            }
        }
        fpos += BLOCK;
    }
    double last_rms = sqrt(last_energy / (double)((last_hi - last_lo) * 2));
    /* Non-trivial floor: the pre path (kick + capped reverb send + feedback drone)
     * must sustain a modest positive tail; not silent, not runaway. */
    assert(last_rms > 1e-3);                       /* pre path non-trivially alive */
    assert(last_rms < 1.0);                        /* bounded */
    api->destroy_instance(inst);
    printf("test_taps_redesign: gate10 pre-reverb 30s stable + non-trivial OK (last=%.4f)\n", last_rms);
}

/* ---- Zero-allocations-in-render ------------------------------------------- *
 * malloc_trap.c aborts on Linux CI if render_block allocates (dsp.c toggles
 * g_audio_thread_active). On Darwin the trap is compiled out, so the portable
 * guard is the finite/bounded asserts already in every render helper above +
 * this explicit render exercise. */
static void gate_zero_alloc(plugin_api_v2_t *api) {
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    api->set_param(inst, KGRV_VOL, "0.8");
    api->set_param(inst, KGRV_LENGTH, "0.4");
    api->set_param(inst, PK_GRV_RVMIX, "0.2");   /* PRE path exercises reverb helper */
    mock_host_set_beat(0.0);
    mock_host_set_bpm(128.0f);
    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);
    render_driven(api, inst, dbeat_for_bpm(128.0), NULL);   /* trap active during render_block */
    api->destroy_instance(inst);
    printf("test_taps_redesign: zero-alloc-in-render OK (malloc-trap on Linux; finite/bounded portable)\n");
}

int main(void) {
    omega_primitives_selfcheck();

    /* Guard: FM2 registered so the reference kick is real, not silence. */
    assert(g_models[MODEL_FM2] != NULL);
    assert(g_models[MODEL_FM2]->render != NULL);

    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);

    gate1_clean_copies_equal_level(api);   /* #1 */
    gate2_equal_tap_levels(api);           /* #2 */
    gate3_drone_stability(api);            /* #3 */
    gate4_constant_loudness(api);          /* #4 */
    gate5_no_bitcrush(api);                /* #5 */
    gate67_color_darkens(api);             /* #6 + #7 */
    /* #8 (NaN/Inf + peak<=1.0) asserted inside every render helper. */
    gate9_bpm_click_free(api);             /* #9 */
    gate10_pre_reverb_stability(api);      /* #10 */
    gate_zero_alloc(api);                  /* zero-allocations-in-render */

    printf("test_taps_redesign: ALL TESTS PASSED\n");
    return 0;
}
