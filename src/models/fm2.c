/* fm2.c — FM2 model: 2-op wavetable FM techno kick (KICK-02, KICK-12, KICK-14).
 *
 * The reference-bar voicing (D-B03): re-voiced param ranges + tuned CURVE
 * 808<->909 blend, plus the shared 5-mode post-kick FX chain (KICK-14) wired
 * into render. FX modes are configured at control rate via fx_config (Crush's
 * powf lives there) and applied per-sample via fx_process (powf/expf/tanf-free).
 *
 * Signal flow (A-RESEARCH §"FM2 DSP Recipe"):
 *   - Dual pitch envelope: a fast 909-style sweep and a slow 808-style sweep,
 *     lerped by CURVE (D-06). Drives the carrier frequency downward toward f0.
 *   - Modulator (freq = ratio*carrier) phase-modulates the carrier; the FM
 *     depth has its OWN decay envelope (index_env) so the attack is bright and
 *     the tail is pure (KICK-02).
 *   - Both operators read the ONE shared .rodata sine table (D-04); no sinf().
 *   - A transient/click layer (ATTACK amplitude, TRS DEC decay, TRS TNE
 *     brightness) is summed in.
 *   - COLOR is a TPT 1-pole lowpass on the summed output (D-07).
 *
 * State overlays bohm_instance.model_state; all coeff/transcendental work
 * happens in set_param/trigger, never per-sample (no tanf/expf in render).
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- FM2 per-instance state (overlays bohm_instance.model_state) --------- */
typedef struct fm2_state {
    /* Pitch / carrier */
    float f0;                 /* fundamental Hz (from PITCH) */
    float sweep_hz;           /* pitch-env depth in Hz above f0 */
    env_t pitch_env_fast;     /* 909 fast sweep */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* 0 = 808 (slow), 1 = 909 (fast) */

    /* Amplitude / index / transient envelopes */
    env_t amp_env;
    env_t index_env;          /* FM index decay (KICK-02) */
    env_t trs_env;

    /* Oscillators */
    float car_phase, mod_phase;
    float ratio;              /* FM RATIO in [0.5, 8.0] */
    float fm_index;           /* FM INDEX in [0, 12] */
    float op2_wave;           /* OP2 WAVE blend factor in [0, 1] */

    /* Contour / transient scalars */
    float sustain;            /* tail contour scalar (from SUSTAIN) */
    float trs_amp;            /* click amplitude (from ATTACK) */

    /* Filters */
    tpt1_t color_lp;    float color_g;      /* COLOR (output LP) */
    tpt1_t trs_tone_lp; float trs_tone_g;   /* TRS TNE (click brightness LP) */

    /* Cached raw ms values so trigger recomputes coeffs from current params */
    float length_ms;          /* amp decay time (from LENGTH) */
    float trs_dec_ms;         /* transient decay time (from TRS DEC) */

    /* Post-kick FX chain (KICK-14). fx_type 0..1 maps to mode 0..4
     * (Diode/Clip/SAT/Fold/Crush); fx_amt 0..1 is intensity. fx holds Crush's
     * sample-and-hold + PRECOMPUTED crush_levels — configured at CONTROL rate
     * via fx_config (that is where the only powf runs; render is powf-free). */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} fm2_state;

_Static_assert(sizeof(fm2_state) <= 4096, "fm2_state fits model_state");

/* ---- Locale-independent float parser (UI-01: no libc string->float) ------ */
/* Parses an optional sign, integer part, and fractional part. Ignores an
 * exponent (params are plain 0..1 normalized decimals). Locale-safe: '.' only. */
static float parse_f(const char *s) {
    if (!s) return 0.0f;
    while (*s == ' ' || *s == '\t') s++;
    float sign = 1.0f;
    if (*s == '+') { s++; }
    else if (*s == '-') { sign = -1.0f; s++; }
    float ip = 0.0f;
    while (*s >= '0' && *s <= '9') { ip = ip * 10.0f + (float)(*s - '0'); s++; }
    float fp = 0.0f, scale = 0.1f;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { fp += (float)(*s - '0') * scale; scale *= 0.1f; s++; }
    }
    return sign * (ip + fp);
}

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

/* g = tanf(pi*fc/SR); precomputed per set_param, never per sample. */
static inline float tpt_g_from_hz(float fc) {
    return tanf((float)M_PI * fc / OMEGA_SR);
}

/* ---- Parameter dispatch (Page 1 + Page 2 kick keys) ---------------------- */
void fm2_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    fm2_state *fm = (fm2_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        /* Techno kicks sit ~45-55 Hz; bias the default (v=0.5) into that pocket
         * with an exponential map over [35,120] Hz for an even musical sweep
         * (D-B03). At v=0.5 -> ~64.8 Hz center of the exp range; the fundamental
         * is pulled to ~50 Hz by the downward pitch sweep settling toward f0. */
        fm->f0 = 35.0f * powf(120.0f / 35.0f, v);   /* exp map [35,120] Hz */
        /* Sweep depth is recomputed in trigger from f0 AND curve (decoupled from
         * a pure f0*4): sweep_hz = clamp(f0*(2..6), <=480 Hz) so 909 sweeps
         * deeper. Store a curve-free baseline; trigger overrides with curve. */
        fm->sweep_hz = clampf(fm->f0 * (2.0f + fm->curve * 4.0f), 0.0f, 480.0f);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        /* Exp map over [50,1500] ms so mid-knob is musically centered
         * (v=0.5 -> ~274 ms), avoiding all-the-action-in-last-5% (D-B02). */
        fm->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        fm->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        fm->curve = v;                            /* 0 = 808 slow, 1 = 909 fast */
        /* Curve changes the sweep depth (909-side sweeps deeper); recompute. */
        fm->sweep_hz = clampf(fm->f0 * (2.0f + fm->curve * 4.0f), 0.0f, 480.0f);
    } else if (strcmp(key, PK_ATTACK) == 0) {
        fm->trs_amp = v;                          /* click amplitude */
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        fm->trs_dec_ms = 1.0f + v * (30.0f - 1.0f);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        float fc = 500.0f + v * (16000.0f - 500.0f);
        fm->trs_tone_g = tpt_g_from_hz(fc);       /* precompute — not per sample */
    } else if (strcmp(key, PK_COLOR) == 0) {
        float fc = 200.0f + v * (18000.0f - 200.0f);
        fm->color_g = tpt_g_from_hz(fc);          /* precompute — not per sample */
    } else if (strcmp(key, PK_FM_RATIO) == 0) {
        fm->ratio = 0.5f + v * (8.0f - 0.5f);
    } else if (strcmp(key, PK_FM_INDEX) == 0) {
        /* Narrowed to 0-8 (was 0-12): the tail gets buzzy past ~8. Default
         * v=0.5 -> index 4, bright attack, clean tail (KICK-02, D-B03). */
        fm->fm_index = v * 8.0f;
    } else if (strcmp(key, PK_OP2_WAVE) == 0) {
        fm->op2_wave = v;
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        fm->fx_type = v;   /* 0..1 -> mode 0..4 (Diode/Clip/SAT/Fold/Crush) */
        /* Reconfigure FX at CONTROL rate: Crush's powf runs here, never in
         * render (KICK-14 / CLAUDE.md control-rate/render-rate split). */
        fx_config(&fm->fx, (int)(fm->fx_type * 4.0f + 0.5f), fm->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        fm->fx_amt = v;
        fx_config(&fm->fx, (int)(fm->fx_type * 4.0f + 0.5f), fm->fx_amt);
    }
    /* Unknown keys are ignored (dsp.c handles PK_MODEL/PK_MASTER_VOL/PK_UI_HIER). */
}

/* ---- Trigger (note-on) --------------------------------------------------- */
static void fm2_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note;
    fm2_state *fm = (fm2_state *)inst->model_state;
    float velf = (float)velocity / 127.0f;

    /* Dual pitch sweep coefficients (D-06 / D-B03): both start at 1.0 and decay
     * to 0, so pitch = f0 + pitch_amount*sweep_hz sweeps down toward f0. The two
     * time constants are the tuned voicing targets — 909 fast ~15 ms (Context/03
     * tau~=15 ms), 808 slow ~300 ms (150-400 ms boom). CURVE lerps their OUTPUTS
     * (not the coeffs) for a smooth 808<->909 morph; default CURVE 0.5 balanced. */
    float c909 = env_coeff_from_ms(15.0f);    /* fast, steep (909 character) */
    float c808 = env_coeff_from_ms(300.0f);   /* slow boom (808 character) */
    env_trigger(&fm->pitch_env_fast, 1.0f, c909);
    env_trigger(&fm->pitch_env_slow, 1.0f, c808);

    /* Amplitude decay from LENGTH (cached ms). Guard against a zero default. */
    float len_ms = fm->length_ms > 0.0f ? fm->length_ms : 400.0f;
    env_trigger(&fm->amp_env, velf, env_coeff_from_ms(len_ms));

    /* FM index has its own decay so the attack is bright, the tail pure. */
    env_trigger(&fm->index_env, 1.0f, env_coeff_from_ms(40.0f));

    /* Transient/click decay from TRS DEC (cached ms). */
    float trs_ms = fm->trs_dec_ms > 0.0f ? fm->trs_dec_ms : 8.0f;
    env_trigger(&fm->trs_env, 1.0f, env_coeff_from_ms(trs_ms));

    /* Reset oscillator phases and filter states for a deterministic attack. */
    fm->car_phase = 0.0f;
    fm->mod_phase = 0.0f;
    fm->color_lp.s = 0.0f;
    fm->trs_tone_lp.s = 0.0f;

    /* Reset the FX sample-and-hold state for a deterministic attack, but PRESERVE
     * the precomputed crush_levels (control-rate; recomputing would need powf). */
    fm->fx.last = 0.0f;
    fm->fx.hold_ctr = 0;
}

/* ---- Render (per-sample loop; no transcendentals here) ------------------- */
static void fm2_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    fm2_state *fm = (fm2_state *)inst->model_state;

    for (int n = 0; n < frames; n++) {
        float p_fast = env_tick(&fm->pitch_env_fast);
        float p_slow = env_tick(&fm->pitch_env_slow);
        float pitch  = p_slow + fm->curve * (p_fast - p_slow);   /* 808<->909 lerp */
        float fcar   = fm->f0 + pitch * fm->sweep_hz;
        float fmod   = fm->ratio * fcar;
        float idx    = env_tick(&fm->index_env) * fm->fm_index;  /* own index env */

        fm->mod_phase += fmod / OMEGA_SR;
        if (fm->mod_phase >= 1.0f) fm->mod_phase -= 1.0f;
        float mod_out = wt_read(g_sine_table, fm->mod_phase);
        /* OP2 WAVE: blend sine toward a folded/rectified variant for a harder
         * modulator character as op2_wave rises (0 = pure sine). */
        float folded = fabsf(mod_out) * 2.0f - 1.0f;
        mod_out = mod_out + fm->op2_wave * (folded - mod_out);

        float car_ph = fm->car_phase + idx * mod_out;
        car_ph -= floorf(car_ph);                                /* wrap into [0,1) */
        float car_out = wt_read(g_sine_table, car_ph);

        fm->car_phase += fcar / OMEGA_SR;
        if (fm->car_phase >= 1.0f) fm->car_phase -= 1.0f;

        float amp   = env_tick(&fm->amp_env) * (0.5f + 0.5f * fm->sustain);
        float click = fm->trs_amp * env_tick(&fm->trs_env);
        click = tpt1_lp(&fm->trs_tone_lp, click, fm->trs_tone_g);  /* TRS TNE */

        /* Body (carrier*amp, peak ~1.0) + click (peak ~trs_amp) can sum above
         * 1.0. Scale so the natural, unclipped engine output stays within
         * [-1,1] before the int16 boundary (FNDTN-07 / D-12). The 0.6 body /
         * 0.4 click split keeps the transient present without hard clipping. */
        float s = car_out * amp * 0.6f + click * 0.4f;
        s = tpt1_lp(&fm->color_lp, s, fm->color_g);               /* COLOR */

        /* Post-kick FX (KICK-14): map fx_type 0..1 -> mode 0..4, apply the
         * selected bounded mode scaled by fx_amt. fx_process reads only the
         * precomputed fx_state_t (crush_levels + LUT) — no powf/sinf/expf/tanf
         * in this render loop. amt=0 is transparent; output stays in [-1,1]. */
        int fx_mode = (int)(fm->fx_type * 4.0f + 0.5f);
        s = fx_process(fx_mode, s, fm->fx_amt, &fm->fx);

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot delegation --------------------------------------------- */
static void fm2_set_p2(bohm_instance_t *inst, const char *key, const char *val) {
    /* Page 2 keys route through the same param dispatch as Page 1. */
    fm2_set_param(inst, key, val);
}

/* ---- Page-2 slot descriptor (JSON fragment consumed by A-03 ui.c) -------- */
/* Emits a bounded JSON array of the 3 FM2-specific Page-2 slots. Format is a
 * simple list of {key,label} objects; ui.c wraps this into the full hierarchy.
 * Returns bytes written (excluding the null terminator), 0 on overflow. */
static int fm2_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;
    static const char json[] =
        "[{\"key\":\"" PK_FM_RATIO "\",\"label\":\"FM RATIO\"},"
        "{\"key\":\"" PK_FM_INDEX "\",\"label\":\"FM INDEX\"},"
        "{\"key\":\"" PK_OP2_WAVE "\",\"label\":\"OP2 WAVE\"}]";
    int len = (int)(sizeof(json) - 1);   /* exclude null terminator */
    if (buf_len <= len) return 0;        /* bounded: no overflow */
    memcpy(buf, json, (size_t)len);
    buf[len] = '\0';
    return len;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_fm2_vtable = {
    .name         = "FM2",
    .trigger      = fm2_trigger,
    .render       = fm2_render,
    .set_param    = fm2_set_param,   /* Page-1 + FM2 Page-2 keys (Pitfall 2) */
    .set_p2       = fm2_set_p2,
    .p2_slot_desc = fm2_p2_slot_desc,
};
