/* dsp_primitives.c — owns the single .rodata sine table definition and the
 * runtime guard-sample self-check.
 *
 * The DSP primitives themselves (env_t, wt_read, tpt1_lp, omega_to_i16) are
 * `static inline` in dsp_primitives.h so every TU gets them directly. This .c
 * exists to (1) own the ONE external definition of g_sine_table (pulled in via
 * the generated sine_table.h, which emits it non-static) and (2) provide
 * omega_primitives_selfcheck for the harness to assert KICK-15 at runtime.
 */
#include "dsp_primitives.h"
#include "sine_table.h"   /* defines: const float _Alignas(16) g_sine_table[2049] */
#include "wavetables.h"   /* defines: const float _Alignas(16) g_wavetables[...] */
#include <assert.h>

/* Shared PITCH parse -> Hz, clamped to [OMEGA_PITCH_MIN, OMEGA_PITCH_MAX] (B2,
 * VOICE-01). Locale-independent integer/decimal parse (models pass the raw val
 * string; PITCH is a Hz value now, NOT the 0..1 the model's local parse_f would
 * clamp). Control rate only. */
float omega_pitch_hz(const char *val) {
    if (!val) return OMEGA_PITCH_MIN;
    const char *s = val;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;
    float ip = 0.0f;
    while (*s >= '0' && *s <= '9') { ip = ip * 10.0f + (float)(*s - '0'); s++; }
    float fp = 0.0f, sc = 0.1f;
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { fp += (float)(*s - '0') * sc; sc *= 0.1f; s++; } }
    float hz = ip + fp;
    if (hz < OMEGA_PITCH_MIN) hz = OMEGA_PITCH_MIN;
    if (hz > OMEGA_PITCH_MAX) hz = OMEGA_PITCH_MAX;
    return hz;
}

/* --- Scale-quantize tables (GEN, KICK-11) ------------------------------ */
/* Semitone offsets per scale degree, in .rodata (branch-light lookup, no
 * per-sample interval math). Rows: chromatic, major, minor, minor-pentatonic.
 * Unused pentatonic slots repeat the octave root so any degree stays musical. */
static const int8_t g_scales[NUM_SCALES][12] = {
    /* chromatic          */ { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
    /* major (Ionian)     */ { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19 },
    /* natural minor      */ { 0, 2, 3, 5, 7, 8, 10, 12, 14, 15, 17, 19 },
    /* minor pentatonic   */ { 0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24, 27 },
};

/* Map (scale, degree) -> semitone offset. degree wraps mod 12 with octave
 * transposition so a long generative sequence keeps climbing musically. */
int scale_quantize(int scale, int degree) {
    if (scale < 0) scale = 0; else if (scale >= NUM_SCALES) scale = NUM_SCALES - 1;
    int oct = degree / 12;
    int idx = degree % 12;
    if (idx < 0) { idx += 12; oct -= 1; }
    return (int)g_scales[scale][idx] + 12 * oct;
}

/* KICK-15: the guard sample must duplicate index 0 so wt_read's t[i+1] at the
 * wrap point is branch-free and correct. Assert it at runtime from the harness. */
void omega_primitives_selfcheck(void) {
    assert(g_sine_table[2048] == g_sine_table[0]);
    /* Every factory wavetable must carry the same guard sample so wt_read_bl's
     * t[i+1] at the wrap point is branch-free and correct (KICK-15). */
    for (int w = 0; w < NUM_WAVES; w++)
        for (int b = 0; b < BANDS; b++)
            assert(g_wavetables[w][b][WT_LEN] == g_wavetables[w][b][0]);
}

/* --- FX chain (KICK-14): 5 bounded modes ------------------------------- */
/* Intensity constants (map FX AMT [0,1] onto each mode's useful range). */
#define FX_DIODE_K   6.0f     /* Diode knee sharpness           */
#define FX_CLIP_G    4.0f     /* Clip pre-gain                  */
#define FX_FOLD_F    4.0f     /* Fold pre-gain (more folds)     */
#define FX_CRUSH_H  15.0f     /* Crush max sample-and-hold span */

/* Diode shaping LUT: g_diode_lut[i] = 1 - exp(-z), z = i/(N-1)*ZMAX, in [0,1).
 * Read at CONTROL cost only via a table lookup in fx_process (NO per-sample
 * expf — Pitfall 3). Asymptotes to 1 so the diode output is always bounded. */
#define FX_DIODE_LUT_N   256
#define FX_DIODE_LUT_ZMAX 8.0f
static float g_diode_lut[FX_DIODE_LUT_N + 1];   /* +1 guard for interp */
static int   g_diode_lut_ready = 0;

/* diode_shape(z>=0) -> 1 - exp(-z), clamped, via linear-interp LUT. */
static inline float diode_shape(float z) {
    if (z <= 0.0f) return 0.0f;
    if (z >= FX_DIODE_LUT_ZMAX) return 1.0f;
    float fp = z * ((float)FX_DIODE_LUT_N / FX_DIODE_LUT_ZMAX);
    int   i  = (int)fp;
    float fr = fp - (float)i;
    return g_diode_lut[i] + fr * (g_diode_lut[i + 1] - g_diode_lut[i]);
}

/* fx_config — CONTROL RATE. All powf/expf live HERE, never in fx_process. */
void fx_config(fx_state_t *st, int mode, float amt) {
    if (!g_diode_lut_ready) {
        for (int i = 0; i <= FX_DIODE_LUT_N; i++) {
            float z = (float)i * (FX_DIODE_LUT_ZMAX / (float)FX_DIODE_LUT_N);
            g_diode_lut[i] = 1.0f - expf(-z);   /* control-rate expf */
        }
        g_diode_lut_ready = 1;
    }
    /* Crush: precompute the bit-reduction level count ONCE (powf here only). */
    if (amt < 0.0f) amt = 0.0f;
    if (amt > 1.0f) amt = 1.0f;
    st->crush_levels = powf(2.0f, 16.0f - amt * 12.0f);   /* 16 bits -> 4 bits */
    st->last = 0.0f;
    st->hold_ctr = 0;

    /* Output makeup gain (UIX-06): the drive-like modes raise pre-gain with amt,
     * which just makes the signal louder. Precompute a compensating attenuation
     * (control rate) so turning up drive changes CHARACTER, not level. out_gain
     * is 1.0 at amt=0 (transparent preserved) and drops as amt rises; the per-
     * mode factor tracks how much energy that mode's pre-gain adds. Crush is
     * level-neutral (bit reduction) so it stays at 1.0. */
    float comp = 0.0f;
    switch (mode) {
        case FX_CLIP:  comp = 0.6f; break;
        case FX_FOLD:  comp = 0.8f; break;
        case FX_DIODE: comp = 0.4f; break;
        case FX_SAT:   comp = 0.2f; break;
        default:       comp = 0.0f; break;   /* Crush: no makeup */
    }
    st->out_gain = 1.0f / (1.0f + amt * comp);
}

/* fx_process — per-sample render stage. Reads only PRECOMPUTED state.
 * Uses ONLY algebraic ops + fabsf/floorf/roundf/copysignf + the diode LUT;
 * contains no transcendental calls (those live in fx_config, control rate).
 * Each mode dry/wet-blends by amt so amt=0 is transparent and amt=1 is the
 * full effect, and every branch is bounded to [-1,1]. */
float fx_process(int mode, float x, float amt, fx_state_t *st) {
    if (amt < 0.0f) amt = 0.0f;
    if (amt > 1.0f) amt = 1.0f;
    /* Makeup gain (UIX-06), precomputed at control rate. Guard an unconfigured
     * state (out_gain==0 from calloc before any fx_config) as transparent. */
    float og = (st->out_gain > 0.0f) ? st->out_gain : 1.0f;
    switch (mode) {
        case FX_DIODE: {  /* back-to-back diode rounding; asymptotes to +/-1 */
            float k = 1.0f + amt * FX_DIODE_K;
            float wet = copysignf(diode_shape(fabsf(x) * k), x);  /* LUT lookup */
            return og * ((1.0f - amt) * x + amt * wet);   /* dry at amt=0 */
        }
        case FX_CLIP: {   /* symmetric bounded soft clip gx/(1+|gx|) */
            float gx = (1.0f + amt * FX_CLIP_G) * x;
            float wet = gx / (1.0f + fabsf(gx));
            return og * ((1.0f - amt) * x + amt * wet);   /* dry at amt=0 */
        }
        case FX_SAT: {    /* warm parallel saturation (bounded by construction) */
            float sat = x / (1.0f + fabsf(x));
            return og * ((1.0f - amt) * x + amt * sat);   /* dry at amt=0 */
        }
        case FX_FOLD: {   /* triangle wavefolder -> [-1,1] */
            float g = 1.0f + amt * FX_FOLD_F;
            float v = g * x + 1.0f;
            v = v - 4.0f * floorf(v * 0.25f);      /* mod 4 into [0,4) */
            float wet = fabsf(v - 2.0f) - 1.0f;
            return og * ((1.0f - amt) * x + amt * wet);   /* dry at amt=0 */
        }
        case FX_CRUSH: {  /* bit + sample-rate reduction (PRECOMPUTED levels) */
            float levels = (st->crush_levels > 0.0f) ? st->crush_levels : 1.0f;
            int hold = 1 + (int)(amt * FX_CRUSH_H);
            if (st->hold_ctr <= 0) {
                st->last = roundf(x * levels) / levels;   /* bit-reduce, bounded */
                st->hold_ctr = hold;
            }
            st->hold_ctr--;
            return st->last;   /* at amt=0: levels=2^16, hold=1 -> transparent */
        }
        default:
            return x;
    }
}
