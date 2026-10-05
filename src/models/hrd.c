/* hrd.c — HRD / PX-3 model: hard-techno wavetable + sample + distortion (KICK-06).
 *
 * The loudest, most aggressive kick (B-RESEARCH §HRD): a digital-character
 * wavetable body (like WTR) summed with a punchy SAMPLE LAYER, blended by MIX,
 * then driven HARD through the shared FX SAT/Fold (DRIVE) and bit/SR reduced by
 * the shared crush() (CRUSH). Distinctness = the only intentionally distorted /
 * crushed kick (rave / industrial). Contrast with DIG (DIG = lo-fi digital
 * crunch as timbre; HRD = aggressive drive + crush, output-maximizing).
 *
 * BOUNDED DISTORTION (STATE.md bug #2 / B-RESEARCH Pitfall 5): DRIVE reuses the
 * shared fx_process SAT/Fold bounded forms — the reference `fast_tanh` x/(1-x)
 * is FORBIDDEN. crush() is a bounded round. All output stays within [-1,1]
 * before the int16 boundary even at MAX DRIVE + MAX CRUSH + high pitch.
 *
 * Signal flow (B-RESEARCH §HRD VERBATIM):
 *   body = wt_read_bl(waveidx, 0, ph) * amp_env * (0.5+0.5*sustain)
 *   samp = synth attack thump * samp_env
 *   sig  = body + mix*samp                         (MIX blends the sample layer)
 *   sig  = fx_process(SAT|Fold, sig, drive_amt, &drive_fx)   (DRIVE — bounded)
 *   sig  = crush(sig, crush_levels)                (CRUSH — bounded round)
 *   s    = COLOR output LP
 *   s    = fx_process(post-kick FX TYPE/AMT, s, fx_amt, &fx)  (KICK-14, own state)
 *
 * State overlays bohm_instance.model_state; all coeff/transcendental work
 * happens in set_param/trigger, never per-sample. The DRIVE stage uses its OWN
 * fx_state_t (drive_fx) separate from the post-kick FX fx_state_t (fx) so the
 * two Crush sample-and-holds never collide. Shares fm2.c/dig.c structure
 * (Pattern 4): parse_f + clampf + tpt_g_from_hz.
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* HRD body waves span the aggressive/bright factory tables. SAMPLE LAYER (the
 * PK_HRD_SAMPLE selector) picks which factory wave feeds the punchy layer. */
#define HRD_WAVE_MIN 2   /* saw-ish (harmonically rich body) */
#define HRD_WAVE_MAX 4   /* digital chip (brightest) */

/* ---- HRD per-instance state (overlays bohm_instance.model_state) --------- */
typedef struct hrd_state {
    /* Pitch / body oscillator */
    float f0;                 /* fundamental Hz (from PITCH) */
    float sweep_hz;           /* pitch-env depth (Hz) from CURVE coupling */
    env_t pitch_env_fast;     /* 909 fast sweep */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* 0 = 808 (slow), 1 = 909 (fast) */
    float body_phase;         /* body wavetable phase [0,1) */

    /* Amplitude envelope + tail contour */
    env_t amp_env;
    float sustain;            /* tail contour scalar (from SUSTAIN) */
    float length_ms;          /* amp decay time (from LENGTH) */

    /* Sample layer (synthesized punchy attack thump). PK_HRD_SAMPLE selects the
     * factory wave that drives the layer; PK_HRD_MIX blends it under the body. */
    int   samp_wave;          /* SAMPLE LAYER: factory wave index for the layer */
    float samp_mix;           /* MIX: sample-layer blend [0,1] (default ~0.3) */
    env_t samp_env;           /* short thump env */
    float samp_phase;         /* thump phase */
    float samp_dec_ms;        /* thump length (Page-1 TRS DEC) */
    float attack;             /* Page-1 ATTACK: sample-thump amplitude */

    /* DRIVE: reuse the shared FX SAT/Fold at high amt. drive_amt is the FX AMT
     * fed into fx_process; drive_fx is the DRIVE stage's OWN fx_state (separate
     * from the post-kick FX below). At high DRIVE we push into Fold for a harder
     * industrial edge; low/mid DRIVE stays in SAT for warm grit. */
    float      drive_amt;     /* DRIVE amount [0,1] */
    int        drive_mode;    /* FX_SAT (warm) .. FX_FOLD (harder) — chosen by DRIVE */
    fx_state_t drive_fx;      /* DRIVE stage state (separate Crush s&h) */

    /* CRUSH: shared crush() bit/SR reduction. crush_levels precomputed at
     * CONTROL rate (crush() per sample is a bounded round — no per-sample powf).
     * Default low-to-off (many levels = clean); low knob = few levels = crushed. */
    float crush_levels;       /* 2^bits, bits ~ 4..16 (default ~16 = off) */

    /* Page-1 TRS TNE: extra body brightness (blend toward the brightest wave) so
     * the tone knob measurably moves the attack-window spectrum (B-04/B-05 lesson). */
    float tne;
    int   body_wave;          /* WAVE: selected body wave (from PK_HRD_SAMPLE too) */

    /* Output COLOR lowpass */
    tpt1_t color_lp;    float color_g;

    /* Post-kick FX chain (KICK-14) — its OWN fx_state, separate from drive_fx. */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} hrd_state;

_Static_assert(sizeof(hrd_state) <= 4096, "hrd_state fits model_state");

/* ---- Locale-independent float parser (UI-01; copied from dig.c) ---------- */
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

/* bits -> level count (2^bits), at CONTROL rate so crush() per sample is a
 * bounded round (no per-sample powf). */
static inline float bits_to_levels(float bits) {
    return powf(2.0f, bits);
}

/* ---- Parameter dispatch (Page 1 + HRD Page 2 keys) ----------------------- */
void hrd_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    hrd_state *h = (hrd_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        /* Same exp map as FM2/DIG (D-B03): techno pocket ~50 Hz at v=0.5. */
        h->f0 = omega_pitch_hz(val);
        h->sweep_hz = clampf(h->f0 * (1.5f + h->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        h->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        h->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        h->curve = v;
        h->sweep_hz = clampf(h->f0 * (1.5f + h->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_ATTACK) == 0) {
        h->attack = v;                                 /* sample-thump amplitude */
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        /* Page-1 TRS DEC lengthens the sample-layer thump (5..120 ms exp). */
        h->samp_dec_ms = 5.0f * powf(120.0f / 5.0f, v);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        /* Page-1 TRS TNE = body brightness morph (spectral lever, not a dead
         * nudge — same B-04/B-05 lesson): blend the body toward the brightest
         * factory wave, adding upper harmonics / zero crossings. */
        h->tne = v;
    } else if (strcmp(key, PK_COLOR) == 0) {
        /* HRD stays aggressive: COLOR opens a wide LP (~50 Hz .. 18 kHz). */
        float fc = 50.0f + v * (18000.0f - 50.0f);
        h->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_HRD_SAMPLE) == 0) {
        /* SAMPLE LAYER: enum 0=Saw,1=Square,2=Digital → wave indices 2/3/4. */
        int idx = (int)(parse_f(val) + 0.5f);
        if (idx < 0) idx = 0; if (idx > 2) idx = 2;
        h->body_wave = HRD_WAVE_MIN + idx;
        h->samp_wave = h->body_wave;
    } else if (strcmp(key, PK_HRD_MIX) == 0) {
        /* MIX: sample-layer blend under the body (default ~0.3). */
        h->samp_mix = v;
    } else if (strcmp(key, PK_HRD_DRIVE) == 0) {
        /* DRIVE: reuse the shared FX SAT/Fold at high amt. Low DRIVE (<=0.3) = SAT
         * (warm grit); past 30% push into Fold for a harder industrial edge so
         * fold character is audible early in the knob sweep (not buried until 0.6).
         * Reconfigure the DRIVE fx_state at CONTROL rate (its Crush powf, if any,
         * runs here — Fold/SAT themselves are transcendental-free in render). */
        h->drive_amt  = v;
        h->drive_mode = (v > 0.3f) ? FX_FOLD : FX_SAT;
        fx_config(&h->drive_fx, h->drive_mode, h->drive_amt);
    } else if (strcmp(key, PK_HRD_CRUSH) == 0) {
        /* CRUSH: aggressive bit reduction (default low-to-off). Higher knob =
         * FEWER bits = more crush. Map v -> bits in [16,2]: v=0 -> 16-bit (clean),
         * v=1 -> 2-bit (extreme crush, 4 quantization levels). Extended from the
         * old [16,4] range so mid-knob (v=0.5 -> 9-bit) is already audible and
         * max crush is truly extreme. Precompute level count; crush() is a
         * bounded round per sample (no per-sample powf). */
        float bits = 16.0f - v * (16.0f - 2.0f);   /* v=0: 16-bit (clean), v=1: 2-bit (extreme) */
        h->crush_levels = bits_to_levels(bits);
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        h->fx_type = (float)(int)(parse_f(val) + 0.5f);
        if (h->fx_type < 0.0f) h->fx_type = 0.0f;
        if (h->fx_type > 4.0f) h->fx_type = 4.0f;
        fx_config(&h->fx, (int)h->fx_type, h->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        h->fx_amt = v;
        fx_config(&h->fx, (int)h->fx_type, h->fx_amt);
    }
    /* Unknown keys ignored (dsp.c owns PK_MODEL/PK_MASTER_VOL/PK_UI_HIER). */
}

/* ---- Trigger (note-on) --------------------------------------------------- */
static void hrd_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note;
    hrd_state *h = (hrd_state *)inst->model_state;
    float velf = (float)velocity / 127.0f;

    /* Dual pitch sweep (D-06/D-B03) — same time constants as FM2/DIG/ANA. */
    float c909 = env_coeff_from_ms(15.0f);
    float c808 = env_coeff_from_ms(300.0f);
    env_trigger(&h->pitch_env_fast, 1.0f, c909);
    env_trigger(&h->pitch_env_slow, 1.0f, c808);

    /* Amplitude decay from LENGTH (cached ms). */
    float len_ms = h->length_ms > 0.0f ? h->length_ms : 400.0f;
    env_trigger(&h->amp_env, velf, env_coeff_from_ms(len_ms));

    /* Sample-layer thump: a short punchy attack (Page-1 TRS DEC length). */
    float samp_ms = h->samp_dec_ms > 0.0f ? h->samp_dec_ms : 20.0f;
    env_trigger(&h->samp_env, velf, env_coeff_from_ms(samp_ms));

    /* Reset oscillator phases + filter states for a deterministic attack. */
    h->body_phase = 0.0f;
    h->samp_phase = 0.0f;
    h->color_lp.s = 0.0f;

    /* Reset BOTH fx sample-and-holds (DRIVE + post-kick), preserve precomputed
     * crush_levels / configured drive_fx state (control rate — recomputing needs
     * powf). Both fx states are reset so a trigger is fully deterministic. */
    h->drive_fx.last     = 0.0f;
    h->drive_fx.hold_ctr = 0;
    h->fx.last           = 0.0f;
    h->fx.hold_ctr       = 0;
}

/* ---- Render (per-sample; only shared bounded fx_process/crush) ----------- */
static void hrd_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    hrd_state *h = (hrd_state *)inst->model_state;

    /* Effective crush level count (control rate; default off = clean). */
    float clevels = h->crush_levels > 0.0f ? h->crush_levels : 65536.0f;

    for (int n = 0; n < frames; n++) {
        float p_fast = env_tick(&h->pitch_env_fast);
        float p_slow = env_tick(&h->pitch_env_slow);
        float pitch  = p_slow + h->curve * (p_fast - p_slow);   /* 808<->909 lerp */
        float fbody  = h->f0 + pitch * h->sweep_hz;

        /* Body: aggressive band-limited wave with a TRS TNE brightness morph
         * toward the brightest factory wave (adds upper harmonics). */
        float bsel = wt_read_bl(h->body_wave, 0, h->body_phase);
        float bbri = wt_read_bl(HRD_WAVE_MAX, 0, h->body_phase);
        float body = bsel + h->tne * (bbri - bsel);
        h->body_phase += fbody / OMEGA_SR;
        if (h->body_phase >= 1.0f) h->body_phase -= 1.0f;

        float amp = env_tick(&h->amp_env) * (0.5f + 0.5f * h->sustain);
        body *= amp;

        /* Sample layer: a punchy attack thump (factory wave, higher-pitched). */
        float samp = wt_read_bl(h->samp_wave, 0, h->samp_phase);
        h->samp_phase += (fbody * 1.5f) / OMEGA_SR;   /* higher = crisper punch */
        if (h->samp_phase >= 1.0f) h->samp_phase -= 1.0f;
        samp *= env_tick(&h->samp_env) * (0.4f + 0.6f * h->attack);

        /* MIX blends the sample layer under the body. Keep the pre-drive sum
         * bounded (0.7 body + mix*samp), then DRIVE + CRUSH shape it. */
        float sig = body * 0.7f + h->samp_mix * samp * 0.5f;

        /* DRIVE: reuse the shared FX SAT/Fold (bounded, no bespoke distortion).
         * fx_process reads only precomputed drive_fx state; SAT/Fold are
         * transcendental-free in render and self-limit to [-1,1] (Pitfall 5). */
        sig = fx_process(h->drive_mode, sig, h->drive_amt, &h->drive_fx);

        /* CRUSH: shared bit-reducer (bounded round; levels precomputed). */
        sig = crush(sig, clevels);

        /* COLOR output LP. */
        float s = tpt1_lp(&h->color_lp, sig, h->color_g);

        /* Post-kick FX (KICK-14): fx_type is integer 0..4 (Bug #1 fix). */
        int fx_mode = (int)h->fx_type;
        s = fx_process(fx_mode, s, h->fx_amt, &h->fx);

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot delegation --------------------------------------------- */
static void hrd_set_p2(bohm_instance_t *inst, const char *key, const char *val) {
    hrd_set_param(inst, key, val);
}

/* ---- Page-2 slot descriptor (full JSON objects; Pattern 3) --------------- */
static int hrd_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;
    static const char json[] =
        "{\"key\":\"" PK_HRD_SAMPLE "\",\"name\":\"Sample Layer\",\"short_name\":\"SAMPLE\",\"type\":\"enum\",\"options\":[\"Saw\",\"Square\",\"Digital\"]},"
        "{\"key\":\"" PK_HRD_MIX    "\",\"name\":\"Mix\",\"short_name\":\"MIX\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"},"
        "{\"key\":\"" PK_HRD_DRIVE  "\",\"name\":\"Drive\",\"short_name\":\"DRIVE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"},"
        "{\"key\":\"" PK_HRD_CRUSH  "\",\"name\":\"Crush\",\"short_name\":\"CRUSH\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"}";
    int len = (int)(sizeof(json) - 1);
    if (buf_len <= len) return 0;        /* bounded: no overflow */
    memcpy(buf, json, (size_t)len);
    buf[len] = '\0';
    return len;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_hrd_vtable = {
    .name         = "HRD",
    .trigger      = hrd_trigger,
    .render       = hrd_render,
    .set_param    = hrd_set_param,
    .set_p2       = hrd_set_p2,
    .p2_slot_desc = hrd_p2_slot_desc,
};
