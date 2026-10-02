/* gen.c — GEN / HPN model: generative techno kick (KICK-11).
 *
 * ===================== PHASE B ENGINE + PHASE C WIRING ====================
 * Phase B built the generative ENGINE: an xorshift64 PRNG (seeded from SEED)
 * driving a scale-quantized pitch sequence, gated by a Euclidean DENSITY
 * pattern. Same SEED -> byte-identical render (determinism, KICK-11).
 *
 * Phase C (GRV-02 / GRV-04, DC-05) wires that engine to the transport and the
 * Groove Page 2 controls:
 *   - The step interval now comes from the groove tempo clock
 *     (inst->groove.samples_per_16th), NOT the fixed GEN_STEP_FRAMES hardcode
 *     (which survives ONLY as a last-resort fallback when the clock has not yet
 *     initialised — the GEN analogue of the DC-02 hardcoded-120 bug).
 *   - SEQ LEN is a runtime Euclidean pattern length (g->seq_len, 1..16).
 *   - LPF FREQ + LPF POLE add a cascaded TPT low-pass (1 stage = 2-pole,
 *     2 stages = 4-pole, DC-06) on the GEN body.
 * All six controls (SEED / SCALE / SEQ LEN / LPF FREQ / LPF POLE / DENSITY)
 * live on the conditional Groove Page 2 (ui.c), NOT Kick Page 2.
 * ==========================================================================
 *
 * The distinctive model: the ONLY kick whose pitch changes per hit — a rolling,
 * evolving, hypnotic 16th-note sub-bass variation. Each step: the PRNG picks a
 * scale degree (scale-quantized against a fixed root), sets the body f0, and the
 * Euclidean pattern decides whether the step fires (re-triggers the amp/pitch
 * envelopes). Reseeding from SEED reproduces the exact sequence (determinism).
 *
 * Body reuses the WTR/ANA band-limited wavetable kick + the FM2 dual-envelope
 * 808<->909 CURVE sweep. All coeff/transcendental work in set_param/trigger,
 * never per-sample (no tanf/expf/powf in render; NO rand() — prng_t only).
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* LAST-RESORT fallback interval only: 16th notes at ~130 BPM = 44100*60/(130*4)
 * ~= 5088 frames. The LIVE step interval comes from the groove tempo clock
 * (inst->groove.samples_per_16th, GRV-02/DC-05); GEN_STEP_FRAMES is used ONLY
 * when that clock has not initialised (samples_per_16th < 1), mirroring the
 * groove 120-BPM last-resort. It is NOT the live reload value. */
#define GEN_STEP_FRAMES 5088
#define GEN_SEQ_LEN_MAX 16          /* max Euclidean pattern length (SEQ LEN cap) */

/* ---- GEN per-instance state (overlays bohm_instance.model_state) --------- */
typedef struct gen_state {
    /* Body oscillator */
    float base_f0;            /* PITCH -> root fundamental Hz */
    float f0;                 /* current step fundamental (root * 2^(semi/12)) */
    float sweep_hz;           /* pitch-env depth in Hz above f0 */
    env_t pitch_env_fast;     /* 909 fast sweep */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* 0 = 808 (slow), 1 = 909 (fast) */
    float body_phase;
    int   wave;               /* body wave index */

    /* Amplitude envelope + tail contour */
    env_t amp_env;
    float sustain;
    float length_ms;

    /* Generative sequencer (self-clocking, deterministic) */
    uint64_t seed;            /* SEED (raw) — reseeds prng on change */
    prng_t   rng;             /* xorshift64 sequence source */
    int      scale;           /* SCALE index into g_scales */
    float    density;         /* DENSITY [0,1] -> Euclidean pulses/seq_len */
    int      npulses;         /* precomputed pulse count from density */
    int      seq_len;         /* SEQ LEN: runtime Euclidean pattern length 1..16 */
    int      step;            /* current step index [0,seq_len) */
    int      step_ctr;        /* frames until next step advance */
    int      degree;          /* running scale degree (climbs for evolution) */

    /* Attack transient */
    tpt1_t  color_lp;    float color_g;

    /* Sub-bass LPF cascade (GRV-04/DC-06): stage 1 always, stage 2 iff lpf_pole
     * (0 = 2-pole/1 stage, 1 = 4-pole/2 stages). g set from LPF FREQ. */
    tpt1_t  lpf1, lpf2;  float lpf_g;  int lpf_pole;

    /* Post-kick FX chain (KICK-14). */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} gen_state;

_Static_assert(sizeof(gen_state) <= 4096, "gen_state fits model_state");

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

/* Euclidean rhythm: does step `s` (of `len`) fire given `pulses` pulses spread
 * evenly (Bjorklund's even distribution: floor((s+1)*p/len) > floor(s*p/len)).
 * No allocation, no per-sample cost (called once per step boundary). */
static inline int euclid_hit(int s, int pulses, int len) {
    if (pulses <= 0) return 0;
    if (pulses >= len) return 1;
    return ((s + 1) * pulses) / len != (s * pulses) / len;
}

/* Recompute the pitch of the current step from a fresh PRNG draw, scale-quantized
 * against the root. Called on each self-clock step advance (control-rate). */
static void gen_step_pitch(gen_state *g) {
    /* Draw a degree in a musical window (0..~14), climbing slightly so the
     * sequence evolves rather than sitting on the root. */
    int draw = (int)(prng_next_f(&g->rng) * 8.0f);        /* 0..7 */
    g->degree = (g->degree + draw) % 24;                  /* wrap two octaves */
    int semi = scale_quantize(g->scale, g->degree) - 12;  /* center around root */
    /* Body fundamental = root * 2^(semi/12). exp2 via powf at CONTROL rate. */
    g->f0 = clampf(g->base_f0 * powf(2.0f, (float)semi / 12.0f), 20.0f, 400.0f);
    g->sweep_hz = clampf(g->f0 * (1.5f + g->curve * 7.0f), 0.0f, 1000.0f);
}

/* Fire a step: re-trigger the amp + pitch envelopes for the current f0. */
static void gen_fire_step(gen_state *g) {
    float c909 = env_coeff_from_ms(15.0f);
    float c808 = env_coeff_from_ms(300.0f);
    env_trigger(&g->pitch_env_fast, 1.0f, c909);
    env_trigger(&g->pitch_env_slow, 1.0f, c808);
    float len_ms = g->length_ms > 0.0f ? g->length_ms : 220.0f;
    env_trigger(&g->amp_env, 0.9f, env_coeff_from_ms(len_ms));
    g->body_phase = 0.0f;
}

/* ---- Parameter dispatch (Page 1 + GEN Page 2 keys) ----------------------- */
void gen_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    gen_state *g = (gen_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        g->base_f0 = omega_pitch_hz(val);   /* sub-bass [30,110] Hz */
        g->f0 = g->base_f0;
        g->sweep_hz = clampf(g->f0 * (1.5f + g->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        g->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        g->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        g->curve = v;
        g->sweep_hz = clampf(g->f0 * (1.5f + g->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_ATTACK) == 0) {
        /* ATTACK biases the body wave brighter (more attack presence). */
        g->wave = (int)(v * (float)(NUM_FACTORY_WAVES - 1) + 0.5f);
        if (g->wave < 0) g->wave = 0;
        if (g->wave >= NUM_FACTORY_WAVES) g->wave = NUM_FACTORY_WAVES - 1;
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        /* TRS DEC nudges the tail contour (kept responsive; body-only model). */
        g->sustain = clampf(g->sustain + (v - 0.5f) * 0.2f, 0.0f, 1.0f);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        float fc = 200.0f + v * (16000.0f - 200.0f);
        g->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_COLOR) == 0) {
        float fc = 200.0f + v * (18000.0f - 200.0f);
        g->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_GEN_SEED) == 0) {
        /* SEED: reseed the PRNG so the same seed reproduces the sequence
         * (determinism, KICK-11). Map [0,1] to a wide integer seed space. */
        g->seed = (uint64_t)(v * 4294967295.0f) * 0x9E3779B1u + 1u;
        prng_seed(&g->rng, g->seed);
        g->degree = 0;
        gen_step_pitch(g);   /* recompute step 0 pitch from the new seed */
    } else if (strcmp(key, PK_GEN_SCALE) == 0) {
        g->scale = (int)(v * (float)(NUM_SCALES - 1) + 0.5f);
        if (g->scale < 0) g->scale = 0;
        if (g->scale >= NUM_SCALES) g->scale = NUM_SCALES - 1;
    } else if (strcmp(key, PK_GEN_DENSITY) == 0) {
        /* DENSITY -> Euclidean pulse count over seq_len (1..seq_len). */
        int len = g->seq_len > 0 ? g->seq_len : GEN_SEQ_LEN_MAX;
        g->density  = v;
        g->npulses  = 1 + (int)(v * (float)(len - 1) + 0.5f);
        if (g->npulses < 1) g->npulses = 1;
        if (g->npulses > len) g->npulses = len;
    } else if (strcmp(key, PK_GEN_SEQLEN) == 0) {
        /* SEQ LEN -> runtime Euclidean pattern length 1..16. Recompute npulses
         * from the standing density against the NEW length so DENSITY stays a
         * proportion of the pattern (GRV-04). */
        g->seq_len = 1 + (int)(v * 15.0f + 0.5f);
        if (g->seq_len < 1) g->seq_len = 1;
        if (g->seq_len > GEN_SEQ_LEN_MAX) g->seq_len = GEN_SEQ_LEN_MAX;
        g->npulses = 1 + (int)(g->density * (float)(g->seq_len - 1) + 0.5f);
        if (g->npulses < 1) g->npulses = 1;
        if (g->npulses > g->seq_len) g->npulses = g->seq_len;
    } else if (strcmp(key, PK_GEN_LPFFREQ) == 0) {
        /* LPF FREQ -> cascade cutoff (sub-bass LPF, HPN spec). tanf at control
         * rate only; render reads only g->lpf_g. */
        float fc = 30.0f + v * (18000.0f - 30.0f);
        g->lpf_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_GEN_LPFPOLE) == 0) {
        /* LPF POLE -> 0 = 2-pole (1 stage), 1 = 4-pole (2 stages), DC-06. */
        g->lpf_pole = (v >= 0.5f);
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        g->fx_type = (float)(int)(parse_f(val) + 0.5f);
        if (g->fx_type < 0.0f) g->fx_type = 0.0f;
        if (g->fx_type > 4.0f) g->fx_type = 4.0f;
        fx_config(&g->fx, (int)g->fx_type, g->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        g->fx_amt = v;
        fx_config(&g->fx, (int)g->fx_type, g->fx_amt);
    }
    /* Unknown keys ignored (dsp.c owns PK_MODEL/PK_MASTER_VOL/PK_UI_HIER). */
}

/* ---- Trigger (note-on) --------------------------------------------------- */
/* Resets phases/filters/FX + RESEEDS the sequence from SEED so the generative
 * sequence is deterministic per trigger (KICK-11). The seeded sequence is
 * PRESERVED across the switch memset because SEED is re-primed on model switch. */
static void gen_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note; (void)velocity;
    gen_state *g = (gen_state *)inst->model_state;

    /* Runtime defaults for Groove Page 2 controls that may never have been
     * primed (dsp.c primes only the shared Page-1 keys). Keep GEN musical +
     * non-silent out of the box (D-B02): a full 16-step pattern, a wide-open
     * cutoff, 2-pole path. */
    if (g->seq_len < 1) g->seq_len = GEN_SEQ_LEN_MAX;
    if (g->lpf_g <= 0.0f) g->lpf_g = tpt_g_from_hz(12000.0f);
    /* lpf_pole defaults to 0 (2-pole) via calloc; nothing to force here. */

    /* Deterministic restart of the seeded sequence. */
    prng_seed(&g->rng, g->seed ? g->seed : 0x9E3779B97F4A7C15ULL);
    g->degree   = 0;
    g->step     = 0;
    /* Transport clock (GRV-02/DC-05): the step interval comes from the groove
     * tempo clock, NOT the GEN_STEP_FRAMES hardcode (the GEN analogue of the
     * DC-02 bug). GEN_STEP_FRAMES is only a last-resort fallback when the clock
     * has not yet initialised (samples_per_16th < 1). */
    int step_frames = inst->groove.samples_per_16th;
    if (step_frames < 1) step_frames = GEN_STEP_FRAMES;
    g->step_ctr = step_frames;   /* step 0 plays a full step before advancing */
    gen_step_pitch(g);   /* pitch for step 0 */

    /* Default the density if GEN's Page-2 was never primed. A moderate default
     * keeps GEN audible out of the box (D-B02 non-silent default). */
    if (g->npulses < 1) g->npulses = 1 + (int)(0.5f * (float)(g->seq_len - 1) + 0.5f);

    /* Fully zero the voice envelopes + phase FIRST so a trigger is byte-identical
     * regardless of any tail left by a prior render (determinism, KICK-11). */
    env_trigger(&g->amp_env, 0.0f, 0.0f);
    env_trigger(&g->pitch_env_fast, 0.0f, 0.0f);
    env_trigger(&g->pitch_env_slow, 0.0f, 0.0f);
    g->body_phase = 0.0f;

    /* The downbeat ALWAYS fires (a triggered kick must sound immediately); the
     * Euclidean DENSITY pattern then gates the subsequent self-clocked steps. */
    gen_fire_step(g);

    g->color_lp.s = 0.0f;
    g->lpf1.s     = 0.0f;   /* reset the LPF cascade so a trigger is byte-identical */
    g->lpf2.s     = 0.0f;
    g->fx.last     = 0.0f;
    g->fx.hold_ctr = 0;
}

/* ---- Render (per-sample; no transcendentals here) ------------------------ */
static void gen_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    gen_state *g = (gen_state *)inst->model_state;

    /* Transport clock (GRV-02/DC-05): the live step interval is the groove
     * tempo clock's 16th-note spacing — NOT the GEN_STEP_FRAMES hardcode (the
     * GEN analogue of the DC-02 bug). GEN_STEP_FRAMES survives ONLY as the
     * last-resort fallback when the clock has not initialised. Read once per
     * block (control rate); dsp.c calls groove_update_tempo before render so
     * this reflects the driven tempo. */
    int step_frames = inst->groove.samples_per_16th;
    if (step_frames < 1) step_frames = GEN_STEP_FRAMES;
    int seq_len = g->seq_len > 0 ? g->seq_len : GEN_SEQ_LEN_MAX;

    for (int n = 0; n < frames; n++) {
        /* Self-clock: count down to the next step boundary; on reaching it,
         * advance the step, draw the next pitch, and fire it if the Euclidean
         * pattern hits. Control-rate work (powf in gen_step_pitch) runs only at
         * step boundaries, not per sample. */
        if (g->step_ctr <= 0) {
            g->step = (g->step + 1) % seq_len;
            gen_step_pitch(g);
            if (euclid_hit(g->step, g->npulses, seq_len)) gen_fire_step(g);
            g->step_ctr = step_frames;   /* transport-derived reload (GRV-02) */
        }
        g->step_ctr--;

        float p_fast = env_tick(&g->pitch_env_fast);
        float p_slow = env_tick(&g->pitch_env_slow);
        float pitch  = p_slow + g->curve * (p_fast - p_slow);
        float fbody  = g->f0 + pitch * g->sweep_hz;

        float body = wt_read_bl(g->wave, 0, g->body_phase);
        g->body_phase += fbody / OMEGA_SR;
        if (g->body_phase >= 1.0f) g->body_phase -= 1.0f;

        float amp = env_tick(&g->amp_env) * (0.5f + 0.5f * g->sustain);
        float s = body * amp * 0.85f;
        s = tpt1_lp(&g->color_lp, s, g->color_g);   /* COLOR output LP */

        /* Sub-bass LPF cascade (GRV-04/DC-06): stage 1 always, stage 2 iff
         * lpf_pole. 1 stage = 2-pole path, 2 stages = 4-pole. No new
         * transcendental — g->lpf_g is precomputed at control rate. */
        s = tpt1_lp(&g->lpf1, s, g->lpf_g);
        if (g->lpf_pole) s = tpt1_lp(&g->lpf2, s, g->lpf_g);

        /* Post-kick FX (KICK-14): fx_type is integer 0..4 (Bug #1 fix). */
        int fx_mode = (int)g->fx_type;
        s = fx_process(fx_mode, s, g->fx_amt, &g->fx);

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot delegation --------------------------------------------- */
static void gen_set_p2(bohm_instance_t *inst, const char *key, const char *val) {
    gen_set_param(inst, key, val);
}

/* ---- Page-2 slot descriptor --------------------------------------------- */
/* GEN exposes ALL of its controls (SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/
 * DENSITY) on the conditional Groove Page 2 (GRV-04, DC-05), NOT Kick Page 2.
 * So GEN's Kick Page 2 emits NO model interior — ui.c's splice then shows only
 * the always-present FX TYPE/AMT (it drops the leading FX comma when slot_len
 * is 0). Return 0 (empty, null-terminated). */
static int gen_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;
    if (buf && buf_len > 0) buf[0] = '\0';
    return 0;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_gen_vtable = {
    .name         = "GEN",
    .trigger      = gen_trigger,
    .render       = gen_render,
    .set_param    = gen_set_param,
    .set_p2       = gen_set_p2,
    .p2_slot_desc = gen_p2_slot_desc,
};
