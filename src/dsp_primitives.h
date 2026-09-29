/* dsp_primitives.h — Shared DSP building blocks for all Omega models.
 *
 * All primitives are `static inline` so both the native test harness and the
 * cross-compiled dsp.so get them without a separate link unit (A-RESEARCH).
 * The one shared .rodata table (g_sine_table) is declared extern here and
 * DEFINED once in dsp_primitives.c (via the generated sine_table.h).
 *
 * Contracts (locked, D-04/D-05/D-07):
 *   env_t   — shared one-pole exponential decay envelope (pitch/index/amp)
 *   tpt1_t  — TPT 1-pole lowpass state (COLOR; Phase D upgrades to full SVF)
 *   wt_read — branch-free linear-interp wavetable read (relies on guard sample)
 */
#ifndef DSP_PRIMITIVES_H
#define DSP_PRIMITIVES_H

#include "omega.h"
#include <math.h>

/* Shared single-cycle sine table (2048 + 1 guard). Defined in dsp_primitives.c
 * via the generated sine_table.h; every model reads this same table (D-04). */
extern const float g_sine_table[2049];

/* --- Envelope: one-pole exponential decay (D-05) ----------------------- */
typedef struct { float value; float coeff; } env_t;

static inline void env_trigger(env_t *e, float start, float coeff) {
    e->value = start;
    e->coeff = coeff;
}

static inline float env_tick(env_t *e) {
    float v = e->value;
    e->value *= e->coeff;
    return v;
}

/* time_ms -> per-sample decay coefficient (discretion D; time-constant form
 * maps LENGTH/ATTACK/TRS DEC cleanly). VERBATIM A-RESEARCH §Pattern 3. */
static inline float env_coeff_from_ms(float time_ms) {
    return expf(-1.0f / (time_ms * 0.001f * OMEGA_SR));
}

/* --- Wavetable read: linear interp, branch-free wrap (D-04) ------------ */
/* phase01 in [0,1); relies on t[2048]==t[0] so the +1 read needs no mask. */
static inline float wt_read(const float *t, float phase01) {
    float fp = phase01 * 2048.0f;
    int   i  = (int)fp;
    float fr = fp - (float)i;
    return t[i] + fr * (t[i + 1] - t[i]);
}

/* --- TPT 1-pole lowpass (D-07, COLOR) ---------------------------------- */
typedef struct { float s; } tpt1_t;

/* g = tanf(pi*fc/SR) precomputed per set_param, not per sample. VERBATIM A-RESEARCH. */
static inline float tpt1_lp(tpt1_t *f, float x, float g) {
    float v = (x - f->s) * (g / (1.0f + g));
    float y = v + f->s;
    f->s = y + v;
    return y;
}

/* --- Output stage: the single int16 boundary (FNDTN-07) ---------------- */
/* Shared by the test harness (Wave 0) and dsp.c render_block (Plan A-02).
 * Clamp + isfinite + lrintf: never wrap on overflow, never emit NaN/Inf. */
static inline int16_t omega_to_i16(float x) {
    if (!isfinite(x)) x = 0.0f;
    if (x >  1.0f) x =  1.0f;
    if (x < -1.0f) x = -1.0f;
    return (int16_t)lrintf(x * 32767.0f);
}

/* --- Post-kick FX chain (KICK-14) -------------------------------------- */
/* Five bounded modes selected by FX TYPE (0..4), scaled by FX AMT [0,1]:
 *   0 Diode, 1 Clip, 2 SAT, 3 Fold, 4 Crush.
 * All outputs are bounded to [-1,1] by construction (Pitfall 5 — engines
 * self-limit; the int16 clamp is a net, not the plan). RT-safety: the
 * fx_process render path contains NO powf/sinf/expf/tanf — every
 * transcendental is precomputed at CONTROL rate in fx_config (STATE.md
 * bug #2: the unbounded reference fast_tanh x/(1-x) is FORBIDDEN). */
enum { FX_DIODE = 0, FX_CLIP = 1, FX_SAT = 2, FX_FOLD = 3, FX_CRUSH = 4 };

/* Crush is the only stateful mode: it holds the last quantized sample and a
 * sample-and-hold counter, plus the PRECOMPUTED bit-reduction level count
 * (crush_levels), so fx_process never calls powf. */
typedef struct { float last; int hold_ctr; float crush_levels; } fx_state_t;

/* fx_config — CONTROL-RATE configurator. Call from each model's set_param /
 * FX-param path (NOT per sample). Precomputes any per-amt transcendental into
 * fx_state_t so fx_process stays transcendental-free. */
void fx_config(fx_state_t *st, int mode, float amt);

/* fx_process — per-sample render stage. Reads only PRECOMPUTED state
 * (st->crush_levels/last/hold_ctr). NO powf/sinf/expf/tanf here. */
float fx_process(int mode, float x, float amt, fx_state_t *st);

/* crush — shared bit-reducer reused by HRD/DIG and the FX Crush mode. The
 * caller must precompute `levels` at control rate (map bits->levels in
 * set_param), keeping powf out of the render loop. Bounded: rounding a
 * bounded input stays bounded. */
static inline float crush(float x, float levels) {
    float l = (levels > 0.0f) ? levels : 1.0f;
    return roundf(x * l) / l;
}

/* Runtime KICK-15 guard-sample self-check (defined in dsp_primitives.c). */
void omega_primitives_selfcheck(void);

#endif /* DSP_PRIMITIVES_H */
