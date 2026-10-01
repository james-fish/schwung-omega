/* test_taps_redesign.c — Native acceptance harness for the Phase-1 feedback-free
 * FIR rumble redesign. Drives the REAL plugin (move_plugin_init_v2 ->
 * create_instance -> set_param -> mock_host_advance_beat -> render_block).
 *
 * The pre-Phase-1 gates that encoded the OLD feedback design's invariants
 * (clean-copies-equal-level, equal-tap-levels within 0.5 dB, constant-loudness
 * across LENGTH, PRE/POST reverb) are REMOVED — that architecture is gone. What
 * remains are the design-agnostic acceptance gates that the FIR redesign must
 * still satisfy: stability/no-runaway (incl. full reverb), no bit-crush at the
 * rail, COLOR darkens, BPM-change click-free, LENGTH morphs distinct↔smear, and
 * zero-allocation-in-render. The comprehensive deliverable coverage (route order,
 * audible-at-default, GEN filter/pitch) lives in tests/test_groove.c.
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

#define KGRV_VOL     PK_GRV_VOL
#define KGRV_LENGTH  PK_GRV_LENGTH
#define KGRV_COLOR   PK_GRV_COLOR
#define KGRV_TAP1    PK_GRV_TAP1
#define KGRV_TAP2    PK_GRV_TAP2
#define KGRV_TAP3    PK_GRV_TAP3
#define KGRV_TAP4    PK_GRV_TAP4
#define KGRV_MONO    PK_GRV_MONO

static double dbeat_for_bpm(double bpm) {
    return (bpm / 60.0) * ((double)BLOCK / SR);
}
static void select_model(plugin_api_v2_t *api, void *inst, int model_idx) {
    char idx[8];
    if (model_idx < 10) { idx[0] = (char)('0' + model_idx); idx[1] = '\0'; }
    else { idx[0] = (char)('0' + model_idx / 10); idx[1] = (char)('0' + model_idx % 10); idx[2] = '\0'; }
    api->set_param(inst, PK_MODEL, idx);
}
static double buf_rms(const int16_t *buf, int nsamp) {
    double s = 0.0;
    for (int i = 0; i < nsamp; i++) { double x = (double)buf[i] / 32768.0; s += x * x; }
    return sqrt(s / (double)nsamp);
}
static double buf_zcr(const int16_t *buf, int frames) {
    int zc = 0; int16_t prev = 0;
    for (int f = 0; f < frames; f++) {
        int16_t x = buf[f * 2];
        if ((x >= 0) != (prev >= 0)) zc++;
        prev = x;
    }
    return (double)zc / (double)frames;
}
static void neutralise_chain(plugin_api_v2_t *api, void *inst) {
    api->set_param(inst, PK_DUCK,       "0.0");
    api->set_param(inst, PK_DJ_FILT,    "0.5");
    api->set_param(inst, PK_CLIP,       "0.0");
    api->set_param(inst, PK_MASTER_VOL, "1.0");
}
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

/* Prime the groove TAPS voice to a musical open state. */
static void prime_taps(plugin_api_v2_t *api, void *inst) {
    api->set_param(inst, KGRV_VOL,  "0.8");
    api->set_param(inst, KGRV_TAP1, "1.0");
    api->set_param(inst, KGRV_TAP2, "1.0");
    api->set_param(inst, KGRV_TAP3, "1.0");
    api->set_param(inst, KGRV_TAP4, "1.0");
    api->set_param(inst, KGRV_COLOR, "0.6");
    api->set_param(inst, PK_GRV_RVMIX, "0");   /* reverb off (plain MIX) */
    api->set_param(inst, PK_GRV_DRIVE, "0");
}

/* ---- Gate: SMEAR STABILITY (no runaway) ----------------------------------- *
 * LENGTH=0 (max smear, long decay) + a kick every 4 beats for 15 s; assert every
 * sample finite/|x|<=1.0 and last-second RMS <= mid-second RMS * 1.5, mid above a
 * floor. The FIR rumble has no recirculation, so this can never grow. */
static void gate_smear_stability(plugin_api_v2_t *api) {
    const double BPM = 128.0;
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    prime_taps(api, inst);
    api->set_param(inst, KGRV_LENGTH, "1.0");   /* max smear (iter-3: len=1 = longest decay) */

    double dbeat = dbeat_for_bpm(BPM);
    mock_host_set_beat(0.0);
    mock_host_set_bpm((float)BPM);

    const int total_frames = (int)(15.0 * SR);
    const int fpb = (int)(60.0 / BPM * SR);
    const int kick_period = fpb * 4;
    double mid_energy = 0.0, last_energy = 0.0;
    int mid_lo = (int)(7.0 * SR), mid_hi = (int)(8.0 * SR);
    int last_lo = (int)(14.0 * SR), last_hi = total_frames;
    int16_t out[BLOCK * 2];
    int fpos = 0, next_kick = 0;
    while (fpos < total_frames) {
        if (fpos >= next_kick) { uint8_t no[3] = {0x90,36,100}; api->on_midi(inst,no,3,0); next_kick += kick_period; }
        mock_host_advance_beat(dbeat);
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
    assert(mid_rms > 1e-3);
    if (!(last_rms <= mid_rms * 1.5))
        fprintf(stderr, "smear_stability: last=%.5f mid=%.5f (runaway?)\n", last_rms, mid_rms);
    assert(last_rms <= mid_rms * 1.5);
    api->destroy_instance(inst);
    printf("test_taps_redesign: smear stability 15s bounded OK (mid=%.4f last=%.4f)\n", mid_rms, last_rms);
}

/* ---- Gate: REVERB BOUNDED ------------------------------------------------- *
 * RVMIX=1 (full wet) + RVDECAY=1 (max comb feedback, still <1) + max smear + kick
 * every 4 beats for 15 s: finite + bounded (the reverb is in-line, never fed into
 * the ring, so its comb fb<1 decays). */
static void gate_reverb_bounded(plugin_api_v2_t *api) {
    const double BPM = 128.0;
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    prime_taps(api, inst);
    api->set_param(inst, KGRV_LENGTH,   "1.0");
    api->set_param(inst, PK_GRV_RVMIX,  "1");
    api->set_param(inst, PK_GRV_RVDECAY,"1");
    double dbeat = dbeat_for_bpm(BPM);
    mock_host_set_beat(0.0);
    mock_host_set_bpm((float)BPM);
    const int total_frames = (int)(15.0 * SR);
    const int fpb = (int)(60.0 / BPM * SR);
    const int kick_period = fpb * 4;
    double mid_energy = 0.0, last_energy = 0.0;
    int mid_lo = (int)(7.0 * SR), mid_hi = (int)(8.0 * SR);
    int last_lo = (int)(14.0 * SR), last_hi = total_frames;
    int16_t out[BLOCK * 2];
    int fpos = 0, next_kick = 0;
    while (fpos < total_frames) {
        if (fpos >= next_kick) { uint8_t no[3] = {0x90,36,100}; api->on_midi(inst,no,3,0); next_kick += kick_period; }
        mock_host_advance_beat(dbeat);
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
    if (!(last_rms <= mid_rms * 1.5 + 1e-3))
        fprintf(stderr, "reverb_bounded: last=%.5f mid=%.5f (runaway?)\n", last_rms, mid_rms);
    assert(last_rms <= mid_rms * 1.5 + 1e-3);
    assert(last_rms < 1.0);
    api->destroy_instance(inst);
    printf("test_taps_redesign: full-reverb bounded 15s OK (mid=%.4f last=%.4f)\n", mid_rms, last_rms);
}

/* ---- Gate: NO BIT-CRUSH --------------------------------------------------- */
static void gate_no_bitcrush(plugin_api_v2_t *api) {
    const double BPM = 128.0;
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    prime_taps(api, inst);
    api->set_param(inst, KGRV_LENGTH, "0.5");
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
    assert(buf_rms(buf, NSAMP) > 1e-3);
    if (frac >= 0.001)
        fprintf(stderr, "no_bitcrush: %.4f%% at rail (want < 0.1%%)\n", frac * 100.0);
    assert(frac < 0.001);
    api->destroy_instance(inst);
    printf("test_taps_redesign: no bit-crush OK (%.4f%% at rail)\n", frac * 100.0);
}

/* ---- Gate: COLOR darkens (ZCR falls as COLOR closes) ---------------------- */
static void gate_color_darkens(plugin_api_v2_t *api) {
    const double BPM = 128.0;
    double zcr_lo = 0.0, zcr_hi = 0.0;
    double dbeat = dbeat_for_bpm(BPM);
    for (int pass = 0; pass < 2; pass++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        prime_kick(api, inst);
        neutralise_chain(api, inst);
        prime_taps(api, inst);
        api->set_param(inst, KGRV_LENGTH, "0.5");
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
    if (!(zcr_hi > zcr_lo * 1.2))
        fprintf(stderr, "color_darkens: zcr_hi=%.5f zcr_lo=%.5f (want hi>1.2*lo)\n", zcr_hi, zcr_lo);
    assert(zcr_hi > zcr_lo * 1.2);
    assert(zcr_hi > 0.01);
    printf("test_taps_redesign: COLOR darkens OK (zcr lo=%.4f hi=%.4f)\n", zcr_lo, zcr_hi);
}

/* ---- Gate: BPM-change click-free ------------------------------------------ */
static void gate_bpm_click_free(plugin_api_v2_t *api) {
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    prime_taps(api, inst);
    api->set_param(inst, KGRV_LENGTH, "0.3");
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
        double dbeat = (b < NBLOCKS / 2) ? dbeat120 : dbeat130;
        if (b == NBLOCKS / 2) mock_host_set_bpm(130.0f);
        mock_host_advance_beat(dbeat);
        api->render_block(inst, out, BLOCK);
        for (int k = 0; k < BLOCK * 2 && cap < NSAMP; k++, cap++) buf[cap] = out[k];
    }
    double max_jump = 0.0;
    for (int f = 1; f < NSAMP / 2; f++) {
        double d = fabs(((double)buf[f * 2] - (double)buf[(f - 1) * 2]) / 32768.0);
        if (d > max_jump) max_jump = d;
    }
    if (!(max_jump < 0.5))
        fprintf(stderr, "bpm_click_free: max jump %.4f (want < 0.5)\n", max_jump);
    assert(max_jump < 0.5);
    api->destroy_instance(inst);
    printf("test_taps_redesign: BPM-change click-free OK (max jump=%.4f)\n", max_jump);
}

/* ---- Gate: LENGTH morphs distinct <-> smear ------------------------------- *
 * At 174 BPM (short 16th) tap1 lands ~86 ms, tap2 ~172 ms inside the window. At
 * LENGTH=1 (short tau) tap2+ are near-silent → the tail (last 20%) is quiet; at
 * LENGTH=0 (long tau) later taps stay loud → the tail carries more energy. So the
 * smear pass must have a higher tail RMS than the distinct pass. */
static void gate_length_morph(plugin_api_v2_t *api) {
    const double BPM = 174.0;
    double dbeat = dbeat_for_bpm(BPM);
    double tail_distinct = 0.0, tail_smear = 0.0;
    for (int pass = 0; pass < 2; pass++) {
        void *inst = api->create_instance("/tmp/omega", "{}");
        assert(inst);
        prime_kick(api, inst);
        api->set_param(inst, PK_LENGTH, "0.2");   /* short kick so taps are isolated */
        neutralise_chain(api, inst);
        prime_taps(api, inst);
        api->set_param(inst, KGRV_COLOR, "1.0");  /* wide open so tail HF isn't removed */
        api->set_param(inst, KGRV_LENGTH, pass == 0 ? "0.0" : "1.0"); /* distinct(0) vs smear(1) */
        mock_host_set_beat(0.0);
        mock_host_set_bpm((float)BPM);
        int16_t warm[BLOCK * 2];
        for (int b = 0; b < 40; b++) { mock_host_advance_beat(dbeat); api->render_block(inst, warm, BLOCK); }
        uint8_t noteon[3] = { 0x90, 36, 100 };
        api->on_midi(inst, noteon, 3, 0);
        static int16_t buf[NSAMP];
        int16_t out[BLOCK * 2];
        int cap = 0;
        for (int b = 0; b < NBLOCKS; b++) {
            mock_host_advance_beat(dbeat);
            api->render_block(inst, out, BLOCK);
            for (int k = 0; k < BLOCK * 2 && cap < NSAMP; k++, cap++) buf[cap] = out[k];
        }
        int tail = (NSAMP * 4) / 5;                /* last 20% */
        double t = buf_rms(buf + tail, NSAMP - tail);
        if (pass == 0) tail_distinct = t; else tail_smear = t;
        api->destroy_instance(inst);
    }
    if (!(tail_smear > tail_distinct * 1.3))
        fprintf(stderr, "length_morph: smear tail=%.5f distinct tail=%.5f (want smear>1.3*distinct)\n",
                tail_smear, tail_distinct);
    assert(tail_smear > tail_distinct * 1.3);
    printf("test_taps_redesign: LENGTH morph distinct<->smear OK (distinct=%.4f smear=%.4f)\n",
           tail_distinct, tail_smear);
}

/* ---- Zero-allocations-in-render ------------------------------------------- */
static void gate_zero_alloc(plugin_api_v2_t *api) {
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    prime_kick(api, inst);
    neutralise_chain(api, inst);
    prime_taps(api, inst);
    api->set_param(inst, KGRV_LENGTH, "0.4");
    api->set_param(inst, PK_GRV_RVMIX, "0.5");   /* in-line reverb exercises the helper */
    mock_host_set_beat(0.0);
    mock_host_set_bpm(128.0f);
    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);
    int16_t out[BLOCK * 2];
    double dbeat = dbeat_for_bpm(128.0);
    for (int b = 0; b < NBLOCKS; b++) { mock_host_advance_beat(dbeat); api->render_block(inst, out, BLOCK); }
    api->destroy_instance(inst);
    printf("test_taps_redesign: zero-alloc-in-render OK (malloc-trap on Linux; finite/bounded portable)\n");
}

int main(void) {
    omega_primitives_selfcheck();
    assert(g_models[MODEL_FM2] != NULL);
    assert(g_models[MODEL_FM2]->render != NULL);

    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);

    gate_smear_stability(api);
    gate_reverb_bounded(api);
    gate_no_bitcrush(api);
    gate_color_darkens(api);
    gate_bpm_click_free(api);
    gate_length_morph(api);
    gate_zero_alloc(api);

    printf("test_taps_redesign: ALL TESTS PASSED\n");
    return 0;
}
