/* ana.c — ANA / WT-4 model: analog wavetable morph + sub-osc + sample (KICK-09).
 *
 * The warmest, fattest kick — the 808 sub-boom king (B-RESEARCH §ANA): a
 * band-limited wavetable BODY that MORPHS across two warm "analog" factory
 * waves (WAVE MORPH crossfades wt_read_bl(analog) <-> wt_read_bl(sine/tri)),
 * pitch-swept with the FM2 dual-envelope 808<->909 CURVE blend (biased 808 by
 * default), summed with a DEDICATED SUB-OSCILLATOR (a pure low sine at the
 * fundamental with its OWN long-decay envelope — SUB LEVEL + SUB DECAY). This
 * dedicated sub-osc + long decay is ANA's distinctness lever: no other model
 * has a separate, independently-enveloped 808 boom under the body. A SAMPLE
 * layer (a short synthesized attack thump, factory .rodata-style) adds body.
 *
 * Signal flow:
 *   body = (1-morph)*wt_read_bl(waveA,0,ph) + morph*wt_read_bl(waveB,0,ph)
 *   body *= amp_env * (0.5+0.5*sustain)
 *   sub  = wt_read(g_sine_table, sub_phase) * sub_env * sub_level   (own decay)
 *   samp = synthesized attack thump * amp_env * sample_mix
 *   s = body*0.5 + sub*0.7 + samp*0.4  (self-limited < 1.0; clamp is a net)
 *   s = COLOR output LP -> fx_process (KICK-14)
 *
 * State overlays bohm_instance.model_state; all coeff/transcendental work
 * happens in set_param/trigger, never per-sample (no tanf/expf/powf in render).
 * Shares fm2.c/wtr.c structure (Pattern 4): parse_f + clampf + tpt_g_from_hz.
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Warm "analog" body morph endpoints: wave 0 (sine, warm sub) <-> wave 5
 * (analog, saturated warmth). WAVE MORPH crossfades between them. */
#define ANA_WAVE_A 0   /* sine — pure warm fundamental */
#define ANA_WAVE_B 5   /* analog — the warm saturated factory wave */

/* ---- ANA per-instance state (overlays bohm_instance.model_state) --------- */
typedef struct ana_state {
    /* Pitch / body oscillator */
    float f0;                 /* fundamental Hz (from PITCH); default ~45 Hz */
    float sweep_hz;           /* pitch-env depth in Hz above f0 */
    env_t pitch_env_fast;     /* 909 fast sweep */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* 0 = 808 (slow), 1 = 909 (fast); biased 808 */
    float body_phase;         /* body wavetable phase [0,1) */
    float morph;              /* WAVE MORPH: waveA <-> waveB crossfade [0,1] */

    /* Amplitude envelope + tail contour */
    env_t amp_env;
    float sustain;            /* tail contour scalar (from SUSTAIN) */
    float length_ms;          /* amp decay time (from LENGTH) */

    /* Dedicated SUB-OSCILLATOR (the 808 boom — independent of body) */
    float sub_phase;          /* pure sine phase [0,1) */
    env_t sub_env;            /* the sub's OWN (long) decay envelope */
    float sub_level;          /* SUB LEVEL (from PK_ANA_SUBLVL) */
    float sub_dec_ms;         /* SUB DECAY (from PK_ANA_SUBDEC); long 400-800 ms */

    /* Sample layer (synthesized attack thump; USR handles real user WAVs) */
    float samp_mix;           /* SAMPLE (from PK_ANA_SAMPLE) */
    env_t samp_env;           /* short thump env */
    float samp_phase;         /* thump body phase */
    float samp_dec_ms;        /* thump length (Page-1 TRS DEC) */

    /* Click amplitude (Page-1 ATTACK) */
    float attack;

    /* Page-1 TRS TNE: extra body-brightness (morph toward analog) bias. */
    float tne;

    /* Output COLOR lowpass */
    tpt1_t color_lp;    float color_g;

    /* Post-kick FX chain (KICK-14). fx_config runs Crush's powf at CONTROL
     * rate; fx_process is powf/expf/tanf-free in render. */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} ana_state;

_Static_assert(sizeof(ana_state) <= 4096, "ana_state fits model_state");

/* ---- Locale-independent float parser (UI-01; copied from wtr.c) ---------- */
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

/* ---- Parameter dispatch (Page 1 + ANA Page 2 keys) ----------------------- */
void ana_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    ana_state *a = (ana_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        /* ANA sits LOWER than the FM2 pocket — 808 sub territory. Exp map
         * [30,110] Hz, ~45 Hz default at v=0.5 (B-RESEARCH §ANA). */
        a->f0 = omega_pitch_hz(val);
        a->sweep_hz = clampf(a->f0 * (1.5f + a->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        a->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        a->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        a->curve = v;                                  /* 0 = 808 slow, 1 = 909 fast */
        a->sweep_hz = clampf(a->f0 * (1.5f + a->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_ATTACK) == 0) {
        a->attack = v;                                 /* sample-thump amplitude */
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        /* Page-1 TRS DEC lengthens the sample-layer thump (5..120 ms exp). */
        a->samp_dec_ms = 5.0f * powf(120.0f / 5.0f, v);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        /* Page-1 TRS TNE biases the body brighter (blends toward the analog
         * wave B on top of WAVE MORPH), so the Page-1 knob is responsive. */
        a->tne = v;
    } else if (strcmp(key, PK_COLOR) == 0) {
        /* Warm by default: COLOR opens a gentle LP (~50 Hz .. 8 kHz). */
        float fc = 50.0f + v * (8000.0f - 50.0f);
        a->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_ANA_MORPH) == 0) {
        /* WAVE MORPH: crossfade warm sine (0) <-> analog saturated (1). */
        a->morph = v;
    } else if (strcmp(key, PK_ANA_SUBLVL) == 0) {
        /* SUB LEVEL: the 808 boom mix. Default ~0.5 prominent. */
        a->sub_level = v;
    } else if (strcmp(key, PK_ANA_SUBDEC) == 0) {
        /* SUB DECAY: the boom length. Long — 100..900 ms exp map, ~400 ms
         * default at v=0.5 for the 808 boom (B-RESEARCH §ANA). */
        a->sub_dec_ms = 100.0f * powf(900.0f / 100.0f, v);
    } else if (strcmp(key, PK_ANA_SAMPLE) == 0) {
        /* SAMPLE: the attack-thump layer mix. */
        a->samp_mix = v;
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        a->fx_type = (float)(int)(parse_f(val) + 0.5f);
        if (a->fx_type < 0.0f) a->fx_type = 0.0f;
        if (a->fx_type > 4.0f) a->fx_type = 4.0f;
        fx_config(&a->fx, (int)a->fx_type, a->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        a->fx_amt = v;
        fx_config(&a->fx, (int)a->fx_type, a->fx_amt);
    }
    /* Unknown keys ignored (dsp.c owns PK_MODEL/PK_MASTER_VOL/PK_UI_HIER). */
}

/* ---- Trigger (note-on) --------------------------------------------------- */
static void ana_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note;
    ana_state *a = (ana_state *)inst->model_state;
    float velf = (float)velocity / 127.0f;

    /* Dual pitch sweep (D-06/D-B03) — same time constants as FM2/WTR so the
     * CURVE 808<->909 morph is consistent across models. */
    float c909 = env_coeff_from_ms(15.0f);
    float c808 = env_coeff_from_ms(300.0f);
    env_trigger(&a->pitch_env_fast, 1.0f, c909);
    env_trigger(&a->pitch_env_slow, 1.0f, c808);

    /* Amplitude decay from LENGTH (cached ms). */
    float len_ms = a->length_ms > 0.0f ? a->length_ms : 400.0f;
    env_trigger(&a->amp_env, velf, env_coeff_from_ms(len_ms));

    /* Dedicated SUB-OSC decay — the 808 boom, its own LONG envelope. */
    float sub_ms = a->sub_dec_ms > 0.0f ? a->sub_dec_ms : 400.0f;
    env_trigger(&a->sub_env, velf, env_coeff_from_ms(sub_ms));

    /* Sample-layer thump: a short punchy attack (Page-1 TRS DEC length). */
    float samp_ms = a->samp_dec_ms > 0.0f ? a->samp_dec_ms : 25.0f;
    env_trigger(&a->samp_env, velf, env_coeff_from_ms(samp_ms));

    /* Reset oscillator phases + all filter states for a deterministic attack. */
    a->body_phase = 0.0f;
    a->sub_phase  = 0.0f;
    a->samp_phase = 0.0f;
    a->color_lp.s = 0.0f;

    /* Reset FX sample-and-hold, preserve precomputed crush_levels (control rate). */
    a->fx.last     = 0.0f;
    a->fx.hold_ctr = 0;
}

/* ---- Render (per-sample; no transcendentals here) ------------------------ */
static void ana_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    ana_state *a = (ana_state *)inst->model_state;

    for (int n = 0; n < frames; n++) {
        float p_fast = env_tick(&a->pitch_env_fast);
        float p_slow = env_tick(&a->pitch_env_slow);
        float pitch  = p_slow + a->curve * (p_fast - p_slow);   /* 808<->909 lerp */
        float fbody  = a->f0 + pitch * a->sweep_hz;

        /* Body: MORPH between two warm analog band-limited waves (2-table
         * crossfade). No branch/transcendental — two guard-sample reads. */
        float ba = wt_read_bl(ANA_WAVE_A, 0, a->body_phase);
        float bb = wt_read_bl(ANA_WAVE_B, 0, a->body_phase);
        /* Effective morph = WAVE MORPH plus a Page-1 TRS TNE brightness bias
         * toward the analog wave B (both push toward the brighter body). */
        float m = clampf(a->morph + a->tne * (1.0f - a->morph), 0.0f, 1.0f);
        float body = ba + m * (bb - ba);
        a->body_phase += fbody / OMEGA_SR;
        if (a->body_phase >= 1.0f) a->body_phase -= 1.0f;

        float amp = env_tick(&a->amp_env) * (0.5f + 0.5f * a->sustain);
        body *= amp;

        /* Dedicated SUB-OSCILLATOR: a pure low sine at the fundamental with its
         * OWN long-decay envelope. This is the 808 boom — separable from the
         * body, adding prominent low-frequency energy below ~80 Hz. */
        float sub = wt_read(g_sine_table, a->sub_phase);
        a->sub_phase += a->f0 / OMEGA_SR;      /* sub tracks the un-swept f0 */
        if (a->sub_phase >= 1.0f) a->sub_phase -= 1.0f;
        sub *= env_tick(&a->sub_env) * a->sub_level;

        /* Sample layer: a short synthesized attack thump (a swept sine burst,
         * factory-style) — adds punch/body. ATTACK scales its amplitude. */
        float samp = wt_read(g_sine_table, a->samp_phase);
        a->samp_phase += (fbody * 1.5f) / OMEGA_SR;   /* slightly higher = thump */
        if (a->samp_phase >= 1.0f) a->samp_phase -= 1.0f;
        samp *= env_tick(&a->samp_env) * a->samp_mix * (0.4f + 0.6f * a->attack);

        /* Sum: body + the prominent sub boom + the sample thump. Self-limited
         * below 1.0 (FM2 precedent; clamp is a net). The sub is weighted heavy
         * so ANA's low-end boom dominates (its 808 distinctness). */
        float s = body * 0.45f + sub * 0.95f + samp * 0.4f;   /* sub dominant (VOICE-06) */
        s = tpt1_lp(&a->color_lp, s, a->color_g);       /* COLOR output LP */

        /* Post-kick FX (KICK-14): fx_type is integer 0..4 (Bug #1 fix). */
        int fx_mode = (int)a->fx_type;
        s = fx_process(fx_mode, s, a->fx_amt, &a->fx);

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot delegation --------------------------------------------- */
static void ana_set_p2(bohm_instance_t *inst, const char *key, const char *val) {
    ana_set_param(inst, key, val);
}

/* ---- Page-2 slot descriptor (full JSON objects; Pattern 3) --------------- */
/* Emits the 4 ANA-specific Page-2 slots as full {key,name,type,min,max}
 * objects so ui.c (B-09) stays generic. Bounded; returns 0 on overflow. */
static int ana_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;
    static const char json[] =
        "{\"key\":\"" PK_ANA_MORPH  "\",\"name\":\"Wave Morph\",\"short_name\":\"MORPH\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"},"
        "{\"key\":\"" PK_ANA_SUBLVL "\",\"name\":\"Sub Level\",\"short_name\":\"SUBLVL\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"},"
        "{\"key\":\"" PK_ANA_SUBDEC "\",\"name\":\"Sub Decay\",\"short_name\":\"SUBDEC\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"},"
        "{\"key\":\"" PK_ANA_SAMPLE "\",\"name\":\"Sample\",\"short_name\":\"SAMPLE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"}";
    int len = (int)(sizeof(json) - 1);
    if (buf_len <= len) return 0;        /* bounded: no overflow */
    memcpy(buf, json, (size_t)len);
    buf[len] = '\0';
    return len;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_ana_vtable = {
    .name         = "ANA",
    .trigger      = ana_trigger,
    .render       = ana_render,
    .set_param    = ana_set_param,
    .set_p2       = ana_set_p2,
    .p2_slot_desc = ana_p2_slot_desc,
};
