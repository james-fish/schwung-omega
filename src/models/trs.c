/* trs.c — TRS / VX-T model: advanced wavetable + transient (909 clarity, KICK-08).
 *
 * The 909 attack specialist (B-RESEARCH §TRS): like WTR (a band-limited
 * wavetable body pitch-swept with the dual-env CURVE blend) but with a MORE
 * ADVANCED transient synth that morphs between TWO GENUINELY DIFFERENT sources
 * (TRANS TONE), a WT COLOR body-timbre morph, and its OWN Page-2 pitch-sweep-
 * CURVE (PK_TRS_CURVE). Distinctness vs WTR: WTR = clean/neutral transient;
 * TRS = aggressive 909 attack with a click<->modal-snap morph.
 *
 * Transient sources (TRANS TONE 0→1):
 *   CLICK (0): sharp decaying sine tick at ~2.5 kHz — tight 909 beater stick
 *   SNAP  (1): modal resonator burst (frequency set by TRS TNE, 100–1200 Hz) —
 *              a pitched "snap" with body-focused tonal character
 *
 * FILTER ROUTE (Kick Page 2, shared): routes the COLOR lowpass to the synthesised
 * body only (Synth=0), the transient only (Trans=1), or the full mix (Both=2).
 *
 * Signal flow:
 *   body   = wt_read_bl(wave_a/b morph by WT COLOR) * amp_env
 *   click  = decaying sine tick (~2.5 kHz) * trs_env
 *   snap   = modal_tick (self-decaying at trs_dec_ms rate)
 *   trans  = (click*(1-trs_tone) + snap*trs_tone) * trs_amp
 *   route → apply COLOR LP to body, trans, or sum; then fx_process
 *
 * All coeff/transcendental work is in set_param/trigger; render is
 * powf/expf/tanf-free. Copies WTR's structure (Pattern 4).
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- TRS per-instance state (overlays bohm_instance.model_state) --------- */
typedef struct trs_state {
    /* Pitch / body oscillator */
    float f0;                 /* fundamental Hz (from PITCH) */
    float sweep_hz;           /* pitch-env depth in Hz above f0 */
    env_t pitch_env_fast;     /* 909 fast sweep */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* Page-1 CURVE: 0 = 808 slow, 1 = 909 fast */
    float p2_curve;           /* PK_TRS_CURVE: P2 pitch-sweep-curve morph (909-biased) */
    float body_phase;         /* body wavetable phase [0,1) */
    int   wave_a, wave_b;     /* WT COLOR morphs between these two factory waves */
    float wt_morph;           /* WT COLOR blend a<->b in [0,1] */

    /* Amplitude envelope + tail contour */
    env_t amp_env;
    float sustain;            /* tail contour scalar (from SUSTAIN) */
    float length_ms;          /* amp decay time (from LENGTH) */

    /* Advanced transient synth: click (sine tick) <-> modal snap */
    float   click_phase;      /* sharp beater "tick" oscillator phase */
    float   click_hz;         /* click pitch (bright, fixed ~2.5 kHz) */
    env_t   trs_env;          /* transient decay gate (click side) */
    modal_t trs_modal;        /* modal resonator — the SNAP transient source */
    float   trs_modal_hz;     /* snap resonant freq (100..1200 Hz, from TRS TNE) */
    float   trs_amp;          /* transient amplitude (from ATTACK) */
    float   trs_dec_ms;       /* TRANS DECAY (from PK_TRS_TDEC / Page-1 TRS DEC) */
    float   trs_tone;         /* TRANS TONE: click(0) <-> modal-snap(1) morph */

    /* Output COLOR lowpass + FILTER ROUTE */
    tpt1_t color_lp;    float color_g;
    int    filter_route;  /* 0=Synth, 1=Trans, 2=Both (from PK_FILTER_ROUTE) */

    /* Post-kick FX chain (KICK-14). */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} trs_state;

_Static_assert(sizeof(trs_state) <= 4096, "trs_state fits model_state");

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

static inline float tpt_g_from_hz(float fc) {
    return tanf((float)M_PI * fc / OMEGA_SR);
}

/* ---- Parameter dispatch (Page 1 + TRS Page 2 keys) ----------------------- */
void trs_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    trs_state *t = (trs_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        t->f0 = omega_pitch_hz(val);       /* exp map [35,120] Hz */
        t->sweep_hz = clampf(t->f0 * (1.5f + t->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        t->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        t->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        t->curve = v;                                  /* Page-1 808<->909 */
        t->sweep_hz = clampf(t->f0 * (1.5f + t->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_ATTACK) == 0) {
        t->trs_amp = v;                                /* transient amplitude */
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        /* Page-1 TRS DEC also sets the transient length (2..40 ms). */
        t->trs_dec_ms = 2.0f + v * (40.0f - 2.0f);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        /* TRS TNE: tuning for the modal SNAP source (100..1200 Hz).
         * Low = deep thuddy impact; high = crisp mid-range snap. */
        t->trs_modal_hz = 100.0f + v * (1200.0f - 100.0f);
    } else if (strcmp(key, PK_COLOR) == 0) {
        float fc = 50.0f + v * (18000.0f - 50.0f);   /* floor ~50 Hz (fully closed) */
        t->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_TRS_TONE) == 0) {
        /* TRANS TONE: morph from a sharp sine tick CLICK (0) to a pitched
         * modal SNAP (1). Genuinely different character endpoints. */
        t->trs_tone = v;
    } else if (strcmp(key, PK_FILTER_ROUTE) == 0) {
        /* FILTER ROUTE: 0=Synth body only, 1=Transient only, 2=Both. */
        int r = (int)(parse_f(val) + 0.5f);
        t->filter_route = (r < 0) ? 0 : (r > 2) ? 2 : r;
    } else if (strcmp(key, PK_TRS_TDEC) == 0) {
        /* TRANS DECAY: transient length (2..40 ms; ~10-18 default at v=0.5). */
        t->trs_dec_ms = 2.0f + v * (40.0f - 2.0f);
    } else if (strcmp(key, PK_TRS_WTCOL) == 0) {
        /* WT COLOR: morph the BODY wavetable timbre between two factory waves
         * (sine <-> saw-ish) — warmer to brighter body. */
        t->wt_morph = v;
    } else if (strcmp(key, PK_TRS_CURVE) == 0) {
        /* PK_TRS_CURVE: TRS's OWN P2 pitch-sweep-curve morph, distinct from the
         * Page-1 PK_CURVE. Biased toward the fast 909 side for attack clarity;
         * higher = faster, deeper sweep = punchier 909 snap. */
        t->p2_curve = v;
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        t->fx_type = (float)(int)(parse_f(val) + 0.5f);
        if (t->fx_type < 0.0f) t->fx_type = 0.0f;
        if (t->fx_type > 4.0f) t->fx_type = 4.0f;
        fx_config(&t->fx, (int)t->fx_type, t->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        t->fx_amt = v;
        fx_config(&t->fx, (int)t->fx_type, t->fx_amt);
    }
    /* Unknown keys ignored. */
}

/* ---- Trigger (note-on) --------------------------------------------------- */
static void trs_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note;
    trs_state *t = (trs_state *)inst->model_state;
    float velf = (float)velocity / 127.0f;

    /* WT COLOR selects the two body waves to morph between (0=sine .. 2=saw). */
    t->wave_a = 0;                                     /* sine (warm sub) */
    t->wave_b = 2;                                     /* saw-ish (bright body) */

    /* Dual pitch sweep. The P2 CURVE (PK_TRS_CURVE) makes the FAST 909 env even
     * faster and deeper for extra attack clarity; blend the two env time
     * constants toward a snappier value as p2_curve rises. */
    float fast_ms = 15.0f - t->p2_curve * 9.0f;        /* 15 ms .. 6 ms (909 snap) */
    if (fast_ms < 3.0f) fast_ms = 3.0f;
    float c909 = env_coeff_from_ms(fast_ms);
    float c808 = env_coeff_from_ms(300.0f);
    env_trigger(&t->pitch_env_fast, 1.0f, c909);
    env_trigger(&t->pitch_env_slow, 1.0f, c808);

    /* Amplitude decay from LENGTH. */
    float len_ms = t->length_ms > 0.0f ? t->length_ms : 400.0f;
    env_trigger(&t->amp_env, velf, env_coeff_from_ms(len_ms));

    /* Transient decay from TRANS DECAY. */
    float trs_ms = t->trs_dec_ms > 0.0f ? t->trs_dec_ms : 12.0f;
    float trs_decay = env_coeff_from_ms(trs_ms);
    env_trigger(&t->trs_env, 1.0f, trs_decay);

    /* CLICK: bright beater tick at ~2.5 kHz (sharp 909 stick). */
    t->click_hz    = 2500.0f;
    t->click_phase = 0.0f;

    /* SNAP: modal resonator at TRS TNE frequency; same decay rate as click. */
    float snap_hz = t->trs_modal_hz > 0.0f ? t->trs_modal_hz : 650.0f;
    modal_excite(&t->trs_modal, snap_hz, trs_decay, 1.0f);

    /* Reset body phase + COLOR filter for deterministic attack. */
    t->body_phase = 0.0f;
    t->color_lp.s = 0.0f;

    /* Reset FX sample-and-hold, preserve precomputed crush_levels. */
    t->fx.last     = 0.0f;
    t->fx.hold_ctr = 0;
}

/* ---- Render (per-sample; no transcendentals here) ------------------------ */
static void trs_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    trs_state *t = (trs_state *)inst->model_state;

    for (int n = 0; n < frames; n++) {
        float p_fast = env_tick(&t->pitch_env_fast);
        float p_slow = env_tick(&t->pitch_env_slow);
        /* Blend the Page-1 CURVE first, then bias further toward the fast env by
         * the P2 CURVE (909 clarity) — both morph the SAME dual-env outputs. */
        float base  = p_slow + t->curve * (p_fast - p_slow);
        float pitch = base + t->p2_curve * (p_fast - base);
        float fbody = t->f0 + pitch * t->sweep_hz;

        /* Body: WT COLOR morphs between two band-limited waves (warm<->bright). */
        float ba = wt_read_bl(t->wave_a, 0, t->body_phase);
        float bb = wt_read_bl(t->wave_b, 0, t->body_phase);
        float body = ba + t->wt_morph * (bb - ba);
        t->body_phase += fbody / OMEGA_SR;
        if (t->body_phase >= 1.0f) t->body_phase -= 1.0f;

        float amp = env_tick(&t->amp_env) * (0.5f + 0.5f * t->sustain);
        body *= amp;

        /* CLICK: sharp decaying sine tick (~2.5 kHz), gated by trs_env. */
        float tenv  = env_tick(&t->trs_env);
        float click = wt_read(g_sine_table, t->click_phase) * tenv;
        t->click_phase += t->click_hz / OMEGA_SR;
        if (t->click_phase >= 1.0f) t->click_phase -= 1.0f;

        /* SNAP: modal resonator self-decays at the same rate (modal_excite set
         * trs_decay as its per-sample multiplier). Genuinely different from the
         * click — tonal, body-focused, pitched by TRS TNE. */
        float snap  = modal_tick(&t->trs_modal);

        /* TRANS TONE: crossfade click(0) <-> modal snap(1). */
        float trans = (click * (1.0f - t->trs_tone) + snap * t->trs_tone) * t->trs_amp;

        /* FILTER ROUTE: apply COLOR LP selectively.
         *   0 = Synth  → filter body only;  transient stays bright
         *   1 = Trans  → filter transient;  body stays full-bandwidth
         *   2 = Both   → filter the mix (original behavior) */
        float s;
        if (t->filter_route == 0) {
            float fb = tpt1_lp(&t->color_lp, body, t->color_g);
            s = fb * 0.5f + trans * 1.1f;
        } else if (t->filter_route == 1) {
            float ft = tpt1_lp(&t->color_lp, trans, t->color_g);
            s = body * 0.5f + ft * 1.1f;
        } else {
            s = tpt1_lp(&t->color_lp, body * 0.5f + trans * 1.1f, t->color_g);
        }

        /* Post-kick FX (KICK-14): fx_type is integer 0..4 (Bug #1 fix). */
        int fx_mode = (int)t->fx_type;
        s = fx_process(fx_mode, s, t->fx_amt, &t->fx);

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot descriptor (full JSON objects; Pattern 3) --------------- */
static int trs_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;
    static const char json[] =
        "{\"key\":\"" PK_TRS_TONE  "\",\"name\":\"CLICK/SNAP\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_TRS_TDEC  "\",\"name\":\"TRANS DEC\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_TRS_WTCOL "\",\"name\":\"WT COLOR\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_TRS_CURVE "\",\"name\":\"CURVE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0}";
    int len = (int)(sizeof(json) - 1);
    if (buf_len <= len) return 0;
    memcpy(buf, json, (size_t)len);
    buf[len] = '\0';
    return len;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_trs_vtable = {
    .name         = "TRS",
    .trigger      = trs_trigger,
    .render       = trs_render,
    .set_param    = trs_set_param,
    .set_p2       = trs_set_param,
    .p2_slot_desc = trs_p2_slot_desc,
};
