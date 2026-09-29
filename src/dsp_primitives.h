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
#include <stdint.h>

/* --- Shared table / scale dimensions (Task 2/3 contract) --------------- */
/* NUM_WAVES/BANDS MUST match tools/gen_wavetables.c + src/wavetables.h.
 * BANDS=1 to start: kicks live at 40-200 Hz where aliasing is negligible;
 * add band-limited variants only if the voicing harness detects aliasing on
 * a hi-pitch sweep (B-RESEARCH §New shared primitives / CLAUDE.md). */
#define NUM_WAVES   6   /* sine, triangle, saw-ish, square, digital, analog */
#define BANDS       1
#define WT_LEN      2048            /* single-cycle length */
#define WT_GUARD    (WT_LEN + 1)    /* 2049: guard sample t[2048]==t[0] */
#define NUM_SCALES  4   /* chromatic, major, minor, minor-pentatonic */

/* Shared single-cycle sine table (2048 + 1 guard). Defined in dsp_primitives.c
 * via the generated sine_table.h; every model reads this same table (D-04). */
extern const float g_sine_table[2049];

/* Band-limited factory wavetables. DEFINED in the generated wavetables.h
 * (Task 3), included once from dsp_primitives.c; shared .rodata across all
 * instances (KICK-15). Guard sample per wave so wt_read_bl needs no wrap mask. */
extern const float g_wavetables[NUM_WAVES][BANDS][WT_GUARD];

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

/* --- Band-limited wavetable read (WTR/DIG/ANA/HRD/USR) ----------------- */
/* Same linear-interp + guard-sample math as wt_read, indexed into one of the
 * factory band-limited waves. `wave` in [0,NUM_WAVES), `band` in [0,BANDS).
 * Relies on g_wavetables[wave][band][WT_LEN]==[0] so the +1 read is unmasked. */
static inline float wt_read_bl(int wave, int band, float phase01) {
    if (wave < 0) wave = 0; else if (wave >= NUM_WAVES) wave = NUM_WAVES - 1;
    if (band < 0) band = 0; else if (band >= BANDS) band = BANDS - 1;
    const float *t = g_wavetables[wave][band];
    float fp = phase01 * (float)WT_LEN;
    int   i  = (int)fp;
    float fr = fp - (float)i;
    return t[i] + fr * (t[i + 1] - t[i]);
}

/* --- Modal resonator: complex-rotation form (PHY, KICK-05) ------------- */
/* Per-sample: z *= e^{jw} * e^{-decay}. One complex multiply; the real part is
 * a decaying sinusoid. cos_w/sin_w/decay are precomputed in modal_excite
 * (trigger/set_param), NEVER per sample (Pitfall 3). */
typedef struct { float re, im, cos_w, sin_w, decay; } modal_t;

/* Excite (impulse) a mode. freq_hz clamped to [20, 0.45*SR] and
 * decay_per_sample clamped to (0,1) to prevent NaN/blowup (Pitfall 5). */
static inline void modal_excite(modal_t *m, float freq_hz,
                                float decay_per_sample, float amp) {
    if (freq_hz < 20.0f) freq_hz = 20.0f;
    float fmax = 0.45f * OMEGA_SR;
    if (freq_hz > fmax) freq_hz = fmax;
    if (decay_per_sample <= 0.0f) decay_per_sample = 0.0001f;
    if (decay_per_sample >= 1.0f) decay_per_sample = 0.9999f;
    float w = 2.0f * (float)M_PI * freq_hz / OMEGA_SR;  /* transcendental at */
    m->cos_w = cosf(w); m->sin_w = sinf(w);             /* excite only        */
    m->decay = decay_per_sample;
    m->re = amp; m->im = 0.0f;
}

/* One sample of the decaying sinusoid (no per-sample transcendentals). */
static inline float modal_tick(modal_t *m) {
    float re = m->re * m->cos_w - m->im * m->sin_w;
    float im = m->re * m->sin_w + m->im * m->cos_w;
    m->re = re * m->decay;
    m->im = im * m->decay;
    return m->re;
}

/* --- PRNG: xorshift64 (GEN, KICK-11) — deterministic, libc-independent - */
/* Deterministic: same seed -> same sequence (reproducible musical randomness). */
typedef struct { uint64_t s; } prng_t;

static inline void prng_seed(prng_t *p, uint64_t seed) {
    p->s = seed ? seed : 0x9E3779B97F4A7C15ULL;   /* avoid the zero fixed-point */
}

static inline uint64_t prng_next_u64(prng_t *p) {
    uint64_t x = p->s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    p->s = x;
    return x;
}

/* Uniform float in [0,1). */
static inline float prng_next_f(prng_t *p) {
    /* top 24 bits -> [0,1) with 2^-24 spacing */
    return (float)(prng_next_u64(p) >> 40) * (1.0f / 16777216.0f);
}

/* --- Noise burst (TRS/WTR/PHY excite/HRD) ------------------------------ */
/* White noise in [-1,1]; color through tpt1 for pink-ish clicks. */
typedef struct { prng_t rng; } noise_t;

static inline void noise_seed(noise_t *n, uint64_t seed) { prng_seed(&n->rng, seed); }

static inline float noise_tick(noise_t *n) {
    return 2.0f * prng_next_f(&n->rng) - 1.0f;
}

/* --- Scale quantize (GEN, KICK-11) ------------------------------------- */
/* Returns a semitone offset for `degree` within `scale`; free-freq mode
 * bypasses at the caller. Defined in dsp_primitives.c (g_scales in .rodata). */
int scale_quantize(int scale, int degree);

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
typedef struct { float last; int hold_ctr; float crush_levels; float out_gain; } fx_state_t;

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

/* ---- Shared PITCH voicing (B2, VOICE-01/02) ------------------------------
 * PITCH is now a DIRECT Hz control (not a normalized 0..1 exp map) with a lower
 * floor/ceiling so a musical kick fundamental is reachable without maxing CURVE.
 * Every model maps PK_PITCH through omega_pitch_hz(val) and derives its downward
 * pitch-sweep depth through omega_sweep_hz(f0, curve) — a STRONGER curve than the
 * old f0*(2+curve*4): the sweep now reaches ~8.5x f0 at full CURVE for a much
 * more pronounced 909 drop. Both run at control rate (set_param), never render. */
#define OMEGA_PITCH_MIN 30.0f
#define OMEGA_PITCH_MAX 200.0f
float omega_pitch_hz(const char *val);          /* parse Hz, clamp [MIN,MAX] */
static inline float omega_sweep_hz(float f0, float curve) {
    float d = f0 * (1.5f + curve * 7.0f);
    return d < 0.0f ? 0.0f : (d > 1000.0f ? 1000.0f : d);
}

#endif /* DSP_PRIMITIVES_H */
