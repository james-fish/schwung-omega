/* test_fx.c — TDD unit tests for the post-kick FX chain (Plan B-02, Task 1).
 *
 * Exercises fx_config/fx_process directly (no model / dsp.c lifecycle) across
 * all 5 modes (Diode/Clip/SAT/Fold/Crush). Proves KICK-14 behaviors:
 *   T1  bounded-at-max     — amt=1.0, high-pitch input -> every sample finite, |y|<=1
 *   T2  transparent-at-zero — amt=0.0 -> RMS(out) ~ RMS(in), no NaN
 *   T3  audible-at-max     — amt=1.0 -> RMS/spectral delta vs dry above a threshold
 *   T4  Crush statefulness — sample-and-hold produces a stepped/held waveform
 *   T5  safe uninitialized  — fx_process with crush_levels==0 -> no div-by-zero/NaN
 *
 * RT-safety (asserted by grep gates in the plan, not here): fx_process contains
 * no powf/sinf/expf/tanf; powf lives only in fx_config.
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SR    44100.0f
#define NSAMP 4096

/* Fill `buf` with a `freq` Hz sine at amplitude 0.9 (leaves headroom so the
 * dry signal itself is already bounded). Uses sinf at TEST time only. */
static void make_tone(float *buf, int n, float freq) {
    for (int i = 0; i < n; i++)
        buf[i] = 0.9f * sinf(2.0f * (float)M_PI * freq * (float)i / SR);
}

static double rms(const float *buf, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; i++) acc += (double)buf[i] * (double)buf[i];
    return sqrt(acc / (double)n);
}

/* Render a whole tone buffer through one FX mode at a given amt into `out`,
 * asserting finite + bounded on every sample (T1). Returns RMS(out). */
static double run_mode(int mode, const float *in, float *out, int n, float amt) {
    fx_state_t st;
    memset(&st, 0, sizeof st);
    fx_config(&st, mode, amt);
    for (int i = 0; i < n; i++) {
        float y = fx_process(mode, in[i], amt, &st);
        assert(isfinite(y));                 /* T1: no NaN/Inf */
        assert(fabsf(y) <= 1.0001f);         /* T1: bounded (tiny fp slack) */
        out[i] = y;
    }
    return rms(out, n);
}

int main(void) {
    static float dry_lo[NSAMP], dry_hi[NSAMP], out[NSAMP];
    make_tone(dry_lo, NSAMP, 60.0f);    /* body-range tone */
    make_tone(dry_hi, NSAMP, 400.0f);   /* high-pitch tone — aliasing stress */

    double dry_rms_lo = rms(dry_lo, NSAMP);

    for (int mode = 0; mode <= 4; mode++) {
        /* T1: bounded at max amt on BOTH a low and a high tone (aliasing). */
        run_mode(mode, dry_lo, out, NSAMP, 1.0f);
        run_mode(mode, dry_hi, out, NSAMP, 1.0f);

        /* T2: approximately transparent at amt=0.0 (RMS within tolerance). */
        double zero_rms = run_mode(mode, dry_lo, out, NSAMP, 0.0f);
        assert(fabs(zero_rms - dry_rms_lo) < 0.05 * dry_rms_lo + 1e-4);

        /* T3: measurably alters the tone at amt=1.0 (RMS delta OR waveform
         *     delta above threshold — Crush at a pure-tone RMS can be close,
         *     so we also compare sample-wise energy of the difference). */
        double max_rms = run_mode(mode, dry_lo, out, NSAMP, 1.0f);
        double diff = 0.0;
        for (int i = 0; i < NSAMP; i++) {
            double d = (double)out[i] - (double)dry_lo[i];
            diff += d * d;
        }
        double diff_rms = sqrt(diff / (double)NSAMP);
        assert(fabs(max_rms - dry_rms_lo) > 0.01 * dry_rms_lo || diff_rms > 0.01);
    }

    /* T4: Crush is stateful — sample-and-hold at high amt holds values across
     *     successive calls, producing runs of identical output samples that a
     *     stateless per-sample identity would not. */
    {
        fx_state_t st;
        memset(&st, 0, sizeof st);
        fx_config(&st, FX_CRUSH, 1.0f);
        int held_runs = 0;
        float prev = fx_process(FX_CRUSH, dry_lo[0], 1.0f, &st);
        for (int i = 1; i < 256; i++) {
            float y = fx_process(FX_CRUSH, dry_lo[i], 1.0f, &st);
            if (y == prev) held_runs++;    /* a held (sample-and-hold) sample */
            prev = y;
        }
        assert(held_runs > 0);   /* SR reduction actually holds samples */
    }

    /* T5: safe when crush_levels was never precomputed (levels==0) — no
     *     div-by-zero / NaN. Call fx_process WITHOUT fx_config. */
    {
        fx_state_t st;
        memset(&st, 0, sizeof st);   /* crush_levels = 0 */
        for (int i = 0; i < NSAMP; i++) {
            float y = fx_process(FX_CRUSH, dry_lo[i], 1.0f, &st);
            assert(isfinite(y) && fabsf(y) <= 1.0001f);
        }
    }

    /* T6: FX type dispatch integration — verify modes 1-4 produce DIFFERENT
     * output from Diode (mode 0). After Bug #1 fix, each mode must be distinct.
     * Uses direct fx_config/fx_process (FM2 model lifecycle unnecessary here —
     * the unit test confirms the DSP path; test_params confirms per-model wiring).
     * mode dispatch */
    {
        /* Verify calling fx_config(0..4) and fx_process(0..4) produces 5 distinct
         * waveform outputs on the same input. If the model dispatch sends everything
         * to mode 4 (Crush), diff_rms between adjacent modes would be ~0. */
        fx_state_t sts[5];
        for (int m = 0; m < 5; m++) {
            memset(&sts[m], 0, sizeof sts[m]);
            fx_config(&sts[m], m, 1.0f);
        }

        /* Each pair of modes must differ by at least 1% RMS waveform energy. */
        for (int a = 0; a < 5; a++) {
            for (int b = a + 1; b < 5; b++) {
                /* Compute waveform difference energy between mode a and mode b */
                fx_state_t sa, sb;
                memset(&sa, 0, sizeof sa); memset(&sb, 0, sizeof sb);
                fx_config(&sa, a, 1.0f); fx_config(&sb, b, 1.0f);
                double diff_e = 0.0;
                for (int i = 0; i < NSAMP; i++) {
                    float ya = fx_process(a, dry_lo[i], 1.0f, &sa);
                    float yb = fx_process(b, dry_lo[i], 1.0f, &sb);
                    double d = (double)ya - (double)yb;
                    diff_e += d * d;
                }
                double diff_rms = sqrt(diff_e / NSAMP);
                /* Modes must not be identical — diff_rms > 0.01 threshold */
                assert(diff_rms > 0.01 && "FX modes are not distinct — dispatch broken");
            }
        }
    }

    printf("test_fx: ALL TESTS PASSED\n");
    return 0;
}
