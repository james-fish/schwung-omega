/* wtr.c — WTR / HZ-1 model: wavetable body + dedicated transient synth (KICK-04).
 *
 * The clean, precise techno kick (B-RESEARCH §WTR): a band-limited wavetable
 * BODY oscillator (WAVE SELECT picks among the factory .rodata waves), pitch-
 * swept with the FM2 dual-envelope 808<->909 CURVE blend, summed with a
 * DEDICATED, INDEPENDENTLY-enveloped TRANSIENT synth (a short filtered noise/
 * click with its own TRANS DECAY + TRANS COLOR). WTR's selling point vs the FM
 * engines is that the transient is fully SEPARABLE from the body — the attack
 * snap is sculpted independently of the low-end sub boom (Context/02 §2.2).
 *
 * Signal flow:
 *   body      = wt_read_bl(wave, 0, car_phase) * amp_env * (0.5+0.5*sustain)
 *   transient = noise_tick() * trs_env * trs_amp, LP-filtered by TRANS COLOR
 *   s = body*0.7 + transient*0.5  (self-limited < 1.0; clamp is a net)
 *   s = COLOR output LP -> fx_process (KICK-14)
 *
 * State overlays bohm_instance.model_state; all coeff/transcendental work
 * happens in set_param/trigger, never per-sample (no tanf/expf/powf in render).
 * Shares fm2.c's structure (Pattern 4): parse_f + clampf + tpt_g_from_hz.
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- WTR per-instance state (overlays bohm_instance.model_state) --------- */
typedef struct wtr_state {
    /* Pitch / body oscillator */
    float f0;                 /* fundamental Hz (from PITCH) */
    float sweep_hz;           /* pitch-env depth in Hz above f0 */
    env_t pitch_env_fast;     /* 909 fast sweep */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* 0 = 808 (slow), 1 = 909 (fast) */
    float body_phase;         /* body wavetable phase [0,1) */
    int   wave;               /* WAVE SELECT: index into g_wavetables [0,NUM_FACTORY_WAVES) */
    float wave_pos;           /* WAVE SELECT continuous scan 0..1 (crossfade, no dead zones) */
    float body_detune;        /* BODY PITCH fine-tune multiplier around 1.0 */

    /* Amplitude envelope + tail contour */
    env_t amp_env;
    float sustain;            /* tail contour scalar (from SUSTAIN) */
    float length_ms;          /* amp decay time (from LENGTH) */

    /* Dedicated transient synth (independent of the body) */
    noise_t trs_noise;        /* white-noise source for the click */
    env_t   trs_env;          /* transient's OWN decay envelope */
    float   trs_amp;          /* click amplitude (from ATTACK) */
    float   trs_dec_ms;       /* TRANS DECAY time (from PK_WTR_TRANSDEC) */
    tpt1_t  trs_col_lp; float trs_col_g;   /* TRANS COLOR (click brightness LP) */
    float   trs_col_mix;      /* TRANS COLOR: LP(dark) <-> raw(bright) blend */
    float   trs_tne_mix;      /* Page-1 TRS TNE: darker <-> brighter click blend */

    /* Output COLOR lowpass */
    tpt1_t color_lp;    float color_g;

    /* Post-kick FX chain (KICK-14). fx_config runs Crush's powf at CONTROL
     * rate; fx_process is powf/expf/tanf-free in render. */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} wtr_state;

_Static_assert(sizeof(wtr_state) <= 4096, "wtr_state fits model_state");

/* ---- Locale-independent float parser (UI-01; copied from fm2.c) ---------- */
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

/* ---- Parameter dispatch (Page 1 + WTR Page 2 keys) ----------------------- */
void wtr_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    wtr_state *w = (wtr_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        /* Same exp map as FM2 (D-B03): techno pocket ~50 Hz at v=0.5. */
        w->f0 = omega_pitch_hz(val);       /* exp map [35,120] Hz */
        w->sweep_hz = clampf(w->f0 * (1.5f + w->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        w->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        w->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        w->curve = v;                                  /* 0 = 808 slow, 1 = 909 fast */
        w->sweep_hz = clampf(w->f0 * (1.5f + w->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_ATTACK) == 0) {
        w->trs_amp = v;                                /* click amplitude */
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        /* Page-1 TRS DEC also lengthens the transient (1..30 ms). */
        w->trs_dec_ms = 1.0f + v * (30.0f - 1.0f);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        /* Page-1 TRS TNE: dark(0) <-> bright(1) click blend. Blending toward the
         * RAW noise (rather than cascading a second LP that would attenuate the
         * click to inaudibility) preserves click amplitude while measurably
         * shifting its brightness — more HF energy = more zero crossings. */
        w->trs_tne_mix = v;
    } else if (strcmp(key, PK_COLOR) == 0) {
        float fc = 200.0f + v * (18000.0f - 200.0f);
        w->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_WTR_WAVE) == 0) {
        /* WAVE SELECT: pick among the NUM_FACTORY_WAVES factory band-limited waves.
         * v in [0,1] -> integer wave index (sine..analog). */
        int idx = (int)(v * (float)(NUM_FACTORY_WAVES - 1) + 0.5f);
        w->wave = idx < 0 ? 0 : (idx >= NUM_FACTORY_WAVES ? NUM_FACTORY_WAVES - 1 : idx);
        w->wave_pos = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);  /* iter-2: continuous scan */
    } else if (strcmp(key, PK_WTR_BODYPITCH) == 0) {
        /* BODY PITCH: fine-tune the body fundamental +/- ~1 octave around f0.
         * v=0.5 -> 1.0 (no shift); exp so the sweep is musically even. */
        w->body_detune = powf(2.0f, (v - 0.5f) * 2.0f);   /* [0.5x, 2x] */
    } else if (strcmp(key, PK_WTR_TRANSDEC) == 0) {
        /* TRANS DECAY: the dedicated transient's OWN length (~2..40 ms, ~8-12
         * default at v=0.5). Separable from the body decay (LENGTH); a wide
         * enough range that the transient contributes measurable energy. */
        w->trs_dec_ms = 2.0f + v * (40.0f - 2.0f);
    } else if (strcmp(key, PK_WTR_TRANSCOL) == 0) {
        /* TRANS COLOR: transient LP(dark) <-> raw(bright) blend, with the LP
         * cutoff itself opening as COLOR rises (bright 2-6 kHz default). Blend
         * (not a cascade) so the click stays audible across the sweep. */
        float fc = 800.0f + v * (10000.0f - 800.0f);
        w->trs_col_g   = tpt_g_from_hz(fc);
        w->trs_col_mix = v;
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        w->fx_type = (float)(int)(parse_f(val) + 0.5f);
        if (w->fx_type < 0.0f) w->fx_type = 0.0f;
        if (w->fx_type > 4.0f) w->fx_type = 4.0f;
        fx_config(&w->fx, (int)w->fx_type, w->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        w->fx_amt = v;
        fx_config(&w->fx, (int)w->fx_type, w->fx_amt);
    }
    /* Unknown keys ignored (dsp.c owns PK_MODEL/PK_MASTER_VOL/PK_UI_HIER). */
}

/* ---- Trigger (note-on) --------------------------------------------------- */
static void wtr_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note;
    wtr_state *w = (wtr_state *)inst->model_state;
    float velf = (float)velocity / 127.0f;

    /* Dual pitch sweep (D-06/D-B03), identical time constants to FM2 so the
     * CURVE 808<->909 morph is consistent across models. */
    float c909 = env_coeff_from_ms(15.0f);
    float c808 = env_coeff_from_ms(300.0f);
    env_trigger(&w->pitch_env_fast, 1.0f, c909);
    env_trigger(&w->pitch_env_slow, 1.0f, c808);

    /* Amplitude decay from LENGTH (cached ms). */
    float len_ms = w->length_ms > 0.0f ? w->length_ms : 400.0f;
    env_trigger(&w->amp_env, velf, env_coeff_from_ms(len_ms));

    /* Dedicated transient decay from TRANS DECAY (cached ms). */
    float trs_ms = w->trs_dec_ms > 0.0f ? w->trs_dec_ms : 5.0f;
    env_trigger(&w->trs_env, 1.0f, env_coeff_from_ms(trs_ms));

    /* Reseed the noise so the click is deterministic per trigger (KICK-13). */
    noise_seed(&w->trs_noise, 0xC0FFEEULL ^ (uint64_t)(unsigned)velocity);

    /* Reset oscillator phase + all filter states for a deterministic attack. */
    w->body_phase   = 0.0f;
    w->color_lp.s   = 0.0f;
    w->trs_col_lp.s = 0.0f;

    /* Reset FX sample-and-hold, preserve precomputed crush_levels (control rate). */
    w->fx.last     = 0.0f;
    w->fx.hold_ctr = 0;
}

/* ---- Render (per-sample; no transcendentals here) ------------------------ */
static void wtr_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    wtr_state *w = (wtr_state *)inst->model_state;

    for (int n = 0; n < frames; n++) {
        float p_fast = env_tick(&w->pitch_env_fast);
        float p_slow = env_tick(&w->pitch_env_slow);
        float pitch  = p_slow + w->curve * (p_fast - p_slow);   /* 808<->909 lerp */
        float fbody  = (w->f0 + pitch * w->sweep_hz) * w->body_detune;

        /* Body: band-limited wavetable read (branch-free guard-sample wrap). */
        /* Continuous WAVE SCAN (iter-2): crossfade adjacent factory tables so the
         * body is present across the WHOLE knob (no discrete dead zones where only
         * the transient was audible). */
        float wp = w->wave_pos * (float)(NUM_FACTORY_WAVES - 1);
        int   wa = (int)wp; if (wa < 0) wa = 0; if (wa > NUM_FACTORY_WAVES - 2) wa = NUM_FACTORY_WAVES - 2;
        float wfr = wp - (float)wa;
        float b0 = wt_read_bl(wa, 0, w->body_phase);
        float body = b0 + wfr * (wt_read_bl(wa + 1, 0, w->body_phase) - b0);
        w->body_phase += fbody / OMEGA_SR;
        if (w->body_phase >= 1.0f) w->body_phase -= 1.0f;

        float amp = env_tick(&w->amp_env) * (0.5f + 0.5f * w->sustain);
        body *= amp;

        /* Dedicated, independently-enveloped transient (separable from body).
         * The click is intentionally PROMINENT in the attack so it governs the
         * attack-window spectrum (its brightness controls — TRS TNE + TRANS
         * COLOR — are audibly effective, and the transient is separable from
         * the body's low sub). TRS TNE is the primary tone, TRANS COLOR refines. */
        float raw = noise_tick(&w->trs_noise) * env_tick(&w->trs_env) * w->trs_amp;
        float dark = tpt1_lp(&w->trs_col_lp, raw, w->trs_col_g);
        /* TRANS COLOR blends the LP'd (dark) click toward the raw (bright) one;
         * TRS TNE blends further toward raw. Both add HF (zero crossings) without
         * attenuating the click, keeping the transient audible and separable. */
        float t = dark + w->trs_col_mix * (raw - dark);
        t = t + w->trs_tne_mix * (raw - t);

        /* Self-limit the body+transient sum below 1.0 (FM2 precedent; clamp is
         * a net). The click LEADS: it is loud in the first few ms while the low
         * body sine is still ramping from zero, so its brightness governs the
         * attack-window spectrum yet it stays separable from the sub. */
        float s = body * 0.5f + t * 1.1f;
        s = tpt1_lp(&w->color_lp, s, w->color_g);       /* COLOR output LP */

        /* Post-kick FX (KICK-14): fx_type is integer 0..4 (Bug #1 fix). */
        int fx_mode = (int)w->fx_type;
        s = fx_process(fx_mode, s, w->fx_amt, &w->fx);

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot delegation --------------------------------------------- */
static void wtr_set_p2(bohm_instance_t *inst, const char *key, const char *val) {
    wtr_set_param(inst, key, val);
}

/* ---- Page-2 slot descriptor (full JSON objects; Pattern 3) --------------- */
/* Emits the 4 WTR-specific Page-2 slots as full {key,name,type,min,max}
 * objects so ui.c (B-09) stays generic. Bounded; returns 0 on overflow. */
static int wtr_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;
    static const char json[] =
        "{\"key\":\"" PK_WTR_WAVE      "\",\"name\":\"WAVE SEL\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_WTR_BODYPITCH "\",\"name\":\"BODY PITCH\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_WTR_TRANSDEC  "\",\"name\":\"TRANS DEC\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_WTR_TRANSCOL  "\",\"name\":\"TRANS COL\",\"type\":\"float\",\"min\":0.0,\"max\":1.0}";
    int len = (int)(sizeof(json) - 1);
    if (buf_len <= len) return 0;        /* bounded: no overflow */
    memcpy(buf, json, (size_t)len);
    buf[len] = '\0';
    return len;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_wtr_vtable = {
    .name         = "WTR",
    .trigger      = wtr_trigger,
    .render       = wtr_render,
    .set_param    = wtr_set_param,
    .set_p2       = wtr_set_p2,
    .p2_slot_desc = wtr_p2_slot_desc,
};
