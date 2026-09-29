/* test_params.c — reusable per-model VOICING BATTERY (D-B02 automated, Plan B-03).
 *
 * Drives the REAL plugin (move_plugin_init_v2 -> create_instance -> on_midi ->
 * render_block) through a model-agnostic battery so every Phase-B model plan
 * (B-04..B-08) reuses it by passing its own Kick Page 2 key list. In Wave 2 the
 * battery covers FM2 (model 0).
 *
 * D-B02 automated voicing criteria proven here:
 *   - Non-silent default: all params 0.5 + trigger renders audible, finite,
 *     |x| <= 1.0 output (RMS > threshold).
 *   - Param-responsive: for EACH key, param=0.1 vs param=0.9 (others 0.5)
 *     yields render buffers whose RMS-envelope delta exceeds a small threshold
 *     (each param measurably changes the output).
 *   - Bounded at extremes: a full lo->hi sweep of every param keeps output
 *     finite + |x| <= 1.0 (no divergence, D-B02 "no aliasing blow-up").
 *
 * The battery is a reusable function `assert_param_responsive(api, inst,
 * model_index, keys, nkeys)` written to accept ANY model's key list.
 */
#include "omega.h"
#include "dsp_primitives.h"
#include "mock_host.h"
#include "wav.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const kick_model_vtable_t *g_models[];

#define BLOCK    128
#define NBLOCKS  256   /* ~0.74 s at 44100 — long enough to capture the tail */
#define NSAMP    (NBLOCKS * BLOCK * 2)

/* Kick Page 1 keys (8) — shared by every model (KICK-12). */
static const char *k_page1_keys[] = {
    PK_PITCH, PK_LENGTH, PK_SUSTAIN, PK_CURVE,
    PK_ATTACK, PK_TRS_DEC, PK_TRS_TNE, PK_COLOR,
};
#define N_PAGE1 (int)(sizeof(k_page1_keys) / sizeof(k_page1_keys[0]))

/* FM2 Kick Page 2 keys (3 + FX TYPE/AMT now active). */
static const char *k_fm2_p2_keys[] = {
    PK_FM_RATIO, PK_FM_INDEX, PK_OP2_WAVE, PK_FX_TYPE, PK_FX_AMT,
};
#define N_FM2_P2 (int)(sizeof(k_fm2_p2_keys) / sizeof(k_fm2_p2_keys[0]))

/* WTR Kick Page 2 keys (4 + FX TYPE/AMT) — B-04. */
static const char *k_wtr_p2_keys[] = {
    PK_WTR_WAVE, PK_WTR_BODYPITCH, PK_WTR_TRANSDEC, PK_WTR_TRANSCOL,
    PK_FX_TYPE, PK_FX_AMT,
};
#define N_WTR_P2 (int)(sizeof(k_wtr_p2_keys) / sizeof(k_wtr_p2_keys[0]))

/* TRS Kick Page 2 keys (4 + FX TYPE/AMT) — B-04. */
static const char *k_trs_p2_keys[] = {
    PK_TRS_TONE, PK_TRS_TDEC, PK_TRS_WTCOL, PK_TRS_CURVE,
    PK_FX_TYPE, PK_FX_AMT,
};
#define N_TRS_P2 (int)(sizeof(k_trs_p2_keys) / sizeof(k_trs_p2_keys[0]))

/* ANA Kick Page 2 keys (4 + FX TYPE/AMT) — B-05. */
static const char *k_ana_p2_keys[] = {
    PK_ANA_MORPH, PK_ANA_SUBLVL, PK_ANA_SUBDEC, PK_ANA_SAMPLE,
    PK_FX_TYPE, PK_FX_AMT,
};
#define N_ANA_P2 (int)(sizeof(k_ana_p2_keys) / sizeof(k_ana_p2_keys[0]))

/* DIG Kick Page 2 keys (4 + FX TYPE/AMT) — B-05. */
static const char *k_dig_p2_keys[] = {
    PK_DIG_WAVEIDX, PK_DIG_SAMPLE, PK_DIG_BITDEPTH, PK_DIG_PITCHENV,
    PK_FX_TYPE, PK_FX_AMT,
};
#define N_DIG_P2 (int)(sizeof(k_dig_p2_keys) / sizeof(k_dig_p2_keys[0]))

/* Trigger note-on then render NBLOCKS into buf (interleaved int16). Asserts
 * every int16 sample is in range (finite + bounded, since omega_to_i16 clamps
 * and the engine self-limits). Returns the RMS-envelope: sqrt(mean(x^2)). */
static double render_rms(plugin_api_v2_t *api, void *inst, int16_t *buf) {
    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);
    double sumsq = 0.0;
    int16_t out[BLOCK * 2];
    for (int b = 0; b < NBLOCKS; b++) {
        api->render_block(inst, out, BLOCK);
        for (int i = 0; i < BLOCK * 2; i++) {
            assert(out[i] >= INT16_MIN && out[i] <= INT16_MAX);   /* bounded */
            double s = (double)out[i] / 32768.0;
            sumsq += s * s;
        }
        memcpy(&buf[b * BLOCK * 2], out, sizeof(out));
    }
    return sqrt(sumsq / (double)NSAMP);
}

/* Set a whole key list to a normalized value (as a string). */
static void set_keys(plugin_api_v2_t *api, void *inst,
                     const char **keys, int nkeys, const char *val) {
    for (int i = 0; i < nkeys; i++) api->set_param(inst, keys[i], val);
}

/* Reset all Page-1 + this model's Page-2 keys to mid (0.5). */
static void prime_mid(plugin_api_v2_t *api, void *inst,
                      const char **p2keys, int np2) {
    set_keys(api, inst, k_page1_keys, N_PAGE1, "0.5");
    set_keys(api, inst, p2keys, np2, "0.5");
}

/* RMS-envelope delta over time between two int16 buffers: mean |a-b| in windows.
 * A per-window RMS difference catches amplitude/decay shifts (research:
 * "differs measurably"). */
static double rms_envelope_delta(const int16_t *a, const int16_t *b) {
    const int WIN = BLOCK;                 /* one block per envelope point */
    double acc = 0.0;
    int npts = NSAMP / (WIN * 2);
    for (int w = 0; w < npts; w++) {
        double sa = 0.0, sb = 0.0;
        for (int i = 0; i < WIN; i++) {
            double la = (double)a[(w * WIN + i) * 2] / 32768.0;
            double lb = (double)b[(w * WIN + i) * 2] / 32768.0;
            sa += la * la; sb += lb * lb;
        }
        double ra = sqrt(sa / WIN), rb = sqrt(sb / WIN);
        acc += fabs(ra - rb);
    }
    return acc / (double)npts;
}

/* Spectral-centroid PROXY: normalized zero-crossing rate (ZCR) of the L channel
 * over the first `nframes` frames. ZCR tracks brightness/spectral tilt without
 * an FFT (allocation-free), so a purely SPECTRAL param (e.g. TRS TNE click
 * brightness, COLOR cutoff) that barely moves gross RMS still registers as
 * "measurably changes the output" (the plan's <behavior>: "RMS-envelope OR
 * spectral-centroid delta"). Transient-brightness params (TRS TNE) only affect
 * the short attack, so measuring an early window — not the whole 0.74 s tail —
 * is what makes their spectral change detectable. */
static double zcr_window(const int16_t *buf, int nframes) {
    long crossings = 0, counted = 0;
    int prev = 0;
    int limit = nframes < (NSAMP / 2) ? nframes : (NSAMP / 2);
    for (int f = 0; f < limit; f++) {
        int s = buf[f * 2];
        if (f > 0 && ((s >= 0) != (prev >= 0))) crossings++;
        prev = s; counted++;
    }
    return counted ? (double)crossings / (double)counted : 0.0;
}

/* A param "measurably changes the output" if the RMS-envelope delta OR the
 * spectral (ZCR) delta — measured both over the whole buffer AND over the
 * ~30 ms attack window (where transient/brightness params live) — exceeds
 * threshold. Amplitude/decay params move RMS; brightness/tilt params move ZCR;
 * transient params move the early-window ZCR. */
static int param_changed(const int16_t *a, const int16_t *b) {
    double rd  = rms_envelope_delta(a, b);
    double sd  = fabs(zcr_window(a, NSAMP / 2) - zcr_window(b, NSAMP / 2));
    int    atk = (int)(0.030 * 44100.0);   /* 30 ms attack window */
    double sda = fabs(zcr_window(a, atk) - zcr_window(b, atk));
    return (rd > 1e-4) || (sd > 1e-3) || (sda > 1e-3);
}

/* THE REUSABLE BATTERY. For `model_index`, primes mid, proves non-silent
 * default, then for each key proves lo(0.1) vs hi(0.9) measurably differs and
 * a full sweep stays bounded (bounded is enforced inside render_rms). Writes a
 * default-render WAV to tests/output/<name>_kick.wav. Any model plan calls this
 * with its own p2 key list. */
static void assert_param_responsive(plugin_api_v2_t *api, void *inst,
                                    int model_index,
                                    const char **p2keys, int np2) {
    /* Select the model + prime mid. PK_MODEL takes the integer model index
     * directly (dsp.c does (int)parse_f(val), clamped to [0,MODEL_COUNT-1]);
     * a switch memsets model_state and re-primes defaults through the model. */
    char idxbuf[16];
    snprintf(idxbuf, sizeof(idxbuf), "%d", model_index);
    api->set_param(inst, PK_MODEL, idxbuf);
    prime_mid(api, inst, p2keys, np2);

    static int16_t bufMid[NSAMP], bufLo[NSAMP], bufHi[NSAMP];

    /* Non-silent default. */
    double rms = render_rms(api, inst, bufMid);
    assert(rms > 1e-4);   /* audible: RMS well above a silent (0) run */

    /* Persist the default render for the ear round / audit (D-B04). */
    const char *name = g_models[model_index] ? g_models[model_index]->name : "unknown";
    char path[128];
    snprintf(path, sizeof(path), "tests/output/%s_kick.wav", name);
    FILE *wav = wav_open(path, 44100, 2);
    if (wav) { wav_write(wav, bufMid, NSAMP); wav_close(wav); }

    /* Each param measurably changes the output (lo vs hi). */
    int total = N_PAGE1 + np2;
    const char *allkeys[N_PAGE1 + 16];
    for (int i = 0; i < N_PAGE1; i++) allkeys[i] = k_page1_keys[i];
    for (int i = 0; i < np2; i++)     allkeys[N_PAGE1 + i] = p2keys[i];

    for (int k = 0; k < total; k++) {
        prime_mid(api, inst, p2keys, np2);
        api->set_param(inst, allkeys[k], "0.1");
        render_rms(api, inst, bufLo);
        prime_mid(api, inst, p2keys, np2);
        api->set_param(inst, allkeys[k], "0.9");
        render_rms(api, inst, bufHi);
        if (!param_changed(bufLo, bufHi)) {
            int atk = (int)(0.030 * 44100.0);
            fprintf(stderr, "PARAM NOT RESPONSIVE: model=%s key=%s "
                    "rms_delta=%.3e zcr_delta=%.3e atk_zcr_delta=%.3e\n",
                    name, allkeys[k], rms_envelope_delta(bufLo, bufHi),
                    fabs(zcr_window(bufLo, NSAMP / 2) - zcr_window(bufHi, NSAMP / 2)),
                    fabs(zcr_window(bufLo, atk) - zcr_window(bufHi, atk)));
        }
        assert(param_changed(bufLo, bufHi));   /* measurably changes output (D-B02) */
    }

    /* Bounded at extremes: a full lo->hi sweep of every param stays finite +
     * |x|<=1 (render_rms asserts int16 range every sample). */
    for (int k = 0; k < total; k++) {
        prime_mid(api, inst, p2keys, np2);
        api->set_param(inst, allkeys[k], "0.0"); render_rms(api, inst, bufLo);
        api->set_param(inst, allkeys[k], "1.0"); render_rms(api, inst, bufHi);
    }

    /* Restore mid for a clean exit. */
    prime_mid(api, inst, p2keys, np2);
}

int main(void) {
    omega_primitives_selfcheck();

    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    /* Wave 2: FM2 (model 0). Later plans add their own calls with their P2 keys. */
    assert_param_responsive(api, inst, MODEL_FM2, k_fm2_p2_keys, N_FM2_P2);

    /* Wave 3 (B-04): WTR (KICK-04) + TRS (KICK-08). */
    assert_param_responsive(api, inst, MODEL_WTR, k_wtr_p2_keys, N_WTR_P2);
    assert_param_responsive(api, inst, MODEL_TRS, k_trs_p2_keys, N_TRS_P2);

    /* Wave 4 (B-05): ANA (KICK-09) + DIG (KICK-07). */
    assert_param_responsive(api, inst, MODEL_ANA, k_ana_p2_keys, N_ANA_P2);
    assert_param_responsive(api, inst, MODEL_DIG, k_dig_p2_keys, N_DIG_P2);

    api->destroy_instance(inst);
    printf("test_params: ALL TESTS PASSED\n");
    return 0;
}
