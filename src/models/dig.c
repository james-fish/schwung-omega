/* dig.c — DIG / SP-6 model: digital wavetable + bit-depth + pitch env (KICK-07).
 *
 * The digital/retro kick (B-RESEARCH §DIG): a body built from digital-character
 * band-limited factory waves (square/digital tables via wt_read_bl), selected by
 * WAVE IDX, given crisp upper-harmonic content, then bit-reduced by BIT DEPTH as
 * a TIMBRAL control (retro digital crunch — always somewhat on, unlike HRD's
 * aggressive CRUSH). PITCH ENV is a dedicated pitch-sweep-depth control (chip
 * "pew"), and a SAMPLE LAYER adds an attack thump. DIG is crisper / brighter than
 * ANA (digital character) — its distinctness is bit-reduction + chip waveforms.
 *
 * Signal flow:
 *   body = wt_read_bl(waveidx, 0, ph) * amp_env * (0.5+0.5*sustain)
 *   body = crush(body, bit_levels)              (bit-depth as timbre, control-rate levels)
 *   samp = synth attack thump * samp_env * sample_mix
 *   s = body*0.7 + samp*0.4  (self-limited < 1.0; clamp is a net)
 *   s = COLOR output LP -> fx_process (KICK-14)
 *
 * State overlays bohm_instance.model_state; all coeff/transcendental work
 * happens in set_param/trigger, never per-sample. The ONLY per-sample rounding
 * is the shared crush() (a bounded round — bit-level count precomputed at
 * control rate in set_param, so no per-sample powf). Shares fm2.c/wtr.c/ana.c
 * structure (Pattern 4): parse_f + clampf + tpt_g_from_hz.
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Digital-character waves span the crisp factory tables. WAVE IDX selects
 * among square(3)/digital(4)/analog(5)/saw(2) — the bright, harmonically rich
 * end of the factory set (contrast ANA's warm sine/analog end). */
#define DIG_WAVE_MIN 2   /* saw-ish */
#define DIG_WAVE_MAX 4   /* digital chip */

/* ---- DIG per-instance state (overlays bohm_instance.model_state) --------- */
typedef struct dig_state {
    /* Pitch / body oscillator */
    float f0;                 /* fundamental Hz (from PITCH) */
    float sweep_hz;           /* base pitch-env depth (Hz) from CURVE coupling */
    float pitchenv;           /* PITCH ENV: extra sweep-depth scalar [0,1] */
    env_t pitch_env_fast;     /* 909 fast sweep */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* 0 = 808 (slow), 1 = 909 (fast) */
    float body_phase;         /* body wavetable phase [0,1) */
    int   waveidx;            /* WAVE IDX: index into digital-character waves */

    /* Amplitude envelope + tail contour */
    env_t amp_env;
    float sustain;            /* tail contour scalar (from SUSTAIN) */
    float length_ms;          /* amp decay time (from LENGTH) */

    /* BIT DEPTH: retro digital crunch. bit_levels precomputed at CONTROL rate
     * (crush() takes a bounded round per sample — no per-sample powf). */
    float bit_levels;         /* 2^bits, bits ~ 6..14 (default ~11) */

    /* Sample layer (synthesized attack thump) */
    float samp_mix;           /* SAMPLE LAYER (from PK_DIG_SAMPLE) */
    env_t samp_env;           /* short thump env */
    float samp_phase;         /* thump phase */
    float samp_dec_ms;        /* thump length (Page-1 TRS DEC) */

    /* Click amplitude (Page-1 ATTACK) */
    float attack;

    /* Page-1 TRS TNE: extra body-crunch bias (fewer bits = brighter/harsher). */
    float tne;

    /* Output COLOR lowpass */
    tpt1_t color_lp;    float color_g;

    /* Post-kick FX chain (KICK-14). */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} dig_state;

_Static_assert(sizeof(dig_state) <= 4096, "dig_state fits model_state");

/* ---- Locale-independent float parser (UI-01; copied from ana.c) ---------- */
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

/* bits -> level count (2^bits), computed at CONTROL rate so crush() per sample
 * is just a bounded round (no per-sample powf). */
static inline float bits_to_levels(float bits) {
    return powf(2.0f, bits);
}

/* ---- Parameter dispatch (Page 1 + DIG Page 2 keys) ----------------------- */
void dig_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    dig_state *d = (dig_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        /* Same exp map as FM2/WTR (D-B03): techno pocket ~50 Hz at v=0.5. */
        d->f0 = omega_pitch_hz(val);
        d->sweep_hz = clampf(d->f0 * (1.5f + d->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        d->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        d->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        d->curve = v;                                  /* 0 = 808 slow, 1 = 909 fast */
        d->sweep_hz = clampf(d->f0 * (1.5f + d->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_ATTACK) == 0) {
        d->attack = v;                                 /* sample-thump amplitude */
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        /* Page-1 TRS DEC lengthens the sample-layer thump (5..120 ms exp). */
        d->samp_dec_ms = 5.0f * powf(120.0f / 5.0f, v);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        /* Page-1 TRS TNE = body brightness: blends the selected body wave
         * toward the brightest digital chip wave (adds upper harmonics = more
         * zero crossings) AND biases the crunch harder. A wave-morph (not just
         * a crush nudge) is needed so the knob measurably moves the spectrum. */
        d->tne = v;
    } else if (strcmp(key, PK_COLOR) == 0) {
        /* DIG stays crisp: COLOR opens a wide LP (400 Hz .. 18 kHz). */
        float fc = 400.0f + v * (18000.0f - 400.0f);
        d->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_DIG_WAVEIDX) == 0) {
        /* WAVE IDX: enum 0=Saw,1=Square,2=Digital → wave indices 2/3/4. */
        int idx = (int)(parse_f(val) + 0.5f);
        if (idx < 0) idx = 0; if (idx > 2) idx = 2;
        d->waveidx = DIG_WAVE_MIN + idx;
    } else if (strcmp(key, PK_DIG_SAMPLE) == 0) {
        /* SAMPLE LAYER: the attack-thump mix. */
        d->samp_mix = v;
    } else if (strcmp(key, PK_DIG_BITDEPTH) == 0) {
        /* BIT DEPTH as TIMBRE: map v -> bits inverted so turning up = MORE crush.
         * Higher v = FEWER bits = crunchier (v=0: 14-bit clean, v=1: 6-bit crush).
         * Precompute the level count so crush() per sample is a bounded round. */
        float bits = 14.0f - v * 8.0f;   /* v=0 -> 14-bit clean, v=1 -> 6-bit maximum crush */
        d->bit_levels = bits_to_levels(bits);
    } else if (strcmp(key, PK_DIG_PITCHENV) == 0) {
        /* PITCH ENV: dedicated pitch-sweep-depth scalar (chip "pew"). */
        d->pitchenv = v;
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        d->fx_type = (float)(int)(parse_f(val) + 0.5f);
        if (d->fx_type < 0.0f) d->fx_type = 0.0f;
        if (d->fx_type > 4.0f) d->fx_type = 4.0f;
        fx_config(&d->fx, (int)d->fx_type, d->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        d->fx_amt = v;
        fx_config(&d->fx, (int)d->fx_type, d->fx_amt);
    }
    /* Unknown keys ignored (dsp.c owns PK_MODEL/PK_MASTER_VOL/PK_UI_HIER). */
}

/* ---- Trigger (note-on) --------------------------------------------------- */
static void dig_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note;
    dig_state *d = (dig_state *)inst->model_state;
    float velf = (float)velocity / 127.0f;

    /* Dual pitch sweep (D-06/D-B03) — same time constants as FM2/WTR/ANA. */
    float c909 = env_coeff_from_ms(15.0f);
    float c808 = env_coeff_from_ms(300.0f);
    env_trigger(&d->pitch_env_fast, 1.0f, c909);
    env_trigger(&d->pitch_env_slow, 1.0f, c808);

    /* Amplitude decay from LENGTH (cached ms). */
    float len_ms = d->length_ms > 0.0f ? d->length_ms : 400.0f;
    env_trigger(&d->amp_env, velf, env_coeff_from_ms(len_ms));

    /* Sample-layer thump: a short punchy attack (Page-1 TRS DEC length). */
    float samp_ms = d->samp_dec_ms > 0.0f ? d->samp_dec_ms : 25.0f;
    env_trigger(&d->samp_env, velf, env_coeff_from_ms(samp_ms));

    /* Reset oscillator phases + all filter states for a deterministic attack. */
    d->body_phase = 0.0f;
    d->samp_phase = 0.0f;
    d->color_lp.s = 0.0f;

    /* Reset FX sample-and-hold, preserve precomputed crush_levels (control rate). */
    d->fx.last     = 0.0f;
    d->fx.hold_ctr = 0;
}

/* ---- Render (per-sample; only the bounded crush() round) ----------------- */
static void dig_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    dig_state *d = (dig_state *)inst->model_state;

    /* Effective bit level count: BIT DEPTH, further reduced (harsher) by the
     * Page-1 TRS TNE crunch bias. Computed once per block at control rate — no
     * per-sample powf. tne=1 halves the levels (~ -1 bit) for extra crunch. */
    float levels = d->bit_levels > 0.0f ? d->bit_levels : 1024.0f;
    float eff_levels = levels * (1.0f - 0.5f * d->tne);
    if (eff_levels < 2.0f) eff_levels = 2.0f;

    /* PITCH ENV scales the sweep depth (chip "pew" amount); 0.25..1.75x. */
    float sweep = d->sweep_hz * (0.25f + 1.5f * d->pitchenv);

    for (int n = 0; n < frames; n++) {
        float p_fast = env_tick(&d->pitch_env_fast);
        float p_slow = env_tick(&d->pitch_env_slow);
        float pitch  = p_slow + d->curve * (p_fast - p_slow);   /* 808<->909 lerp */
        float fbody  = d->f0 + pitch * sweep;

        /* Body: digital-character band-limited wave, with a Page-1 TRS TNE
         * brightness morph toward the brightest digital chip wave (adds upper
         * harmonics / zero crossings so the tone knob measurably moves). */
        float bsel = wt_read_bl(d->waveidx, 0, d->body_phase);
        float bbri = wt_read_bl(DIG_WAVE_MAX, 0, d->body_phase);
        float body = bsel + d->tne * (bbri - bsel);
        d->body_phase += fbody / OMEGA_SR;
        if (d->body_phase >= 1.0f) d->body_phase -= 1.0f;

        float amp = env_tick(&d->amp_env) * (0.5f + 0.5f * d->sustain);
        body *= amp;

        /* BIT DEPTH: bit-reduce the body as a TIMBRAL crunch (shared crush()).
         * The level count is precomputed at control rate; this is a bounded
         * round (rounding a bounded input stays bounded — no per-sample powf). */
        body = crush(body, eff_levels);

        /* Sample layer: a short synthesized attack thump (higher-pitched click
         * for digital snap). ATTACK scales its amplitude. */
        float samp = wt_read(g_sine_table, d->samp_phase);
        d->samp_phase += (fbody * 2.0f) / OMEGA_SR;   /* higher = crisp click */
        if (d->samp_phase >= 1.0f) d->samp_phase -= 1.0f;
        samp *= env_tick(&d->samp_env) * d->samp_mix * (0.4f + 0.6f * d->attack);

        /* Sum: crunchy digital body + the attack thump. Self-limited < 1.0. */
        float s = body * 0.7f + samp * 0.4f;
        s = tpt1_lp(&d->color_lp, s, d->color_g);       /* COLOR output LP */

        /* Post-kick FX (KICK-14): fx_type is integer 0..4 (Bug #1 fix). */
        int fx_mode = (int)d->fx_type;
        s = fx_process(fx_mode, s, d->fx_amt, &d->fx);

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot delegation --------------------------------------------- */
static void dig_set_p2(bohm_instance_t *inst, const char *key, const char *val) {
    dig_set_param(inst, key, val);
}

/* ---- Page-2 slot descriptor (full JSON objects; Pattern 3) --------------- */
static int dig_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;
    static const char json[] =
        "{\"key\":\"" PK_DIG_WAVEIDX  "\",\"name\":\"WAVE IDX\",\"type\":\"enum\",\"options\":[\"Saw\",\"Square\",\"Digital\"]},"
        "{\"key\":\"" PK_DIG_SAMPLE   "\",\"name\":\"SAMPLE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_DIG_BITDEPTH "\",\"name\":\"BIT DEPTH\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_DIG_PITCHENV "\",\"name\":\"PITCH ENV\",\"type\":\"float\",\"min\":0.0,\"max\":1.0}";
    int len = (int)(sizeof(json) - 1);
    if (buf_len <= len) return 0;        /* bounded: no overflow */
    memcpy(buf, json, (size_t)len);
    buf[len] = '\0';
    return len;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_dig_vtable = {
    .name         = "DIG",
    .trigger      = dig_trigger,
    .render       = dig_render,
    .set_param    = dig_set_param,
    .set_p2       = dig_set_p2,
    .p2_slot_desc = dig_p2_slot_desc,
};
