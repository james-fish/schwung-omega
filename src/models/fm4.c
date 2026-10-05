/* fm4.c — FM4 / OLP-4 model: 4-operator FM, OPL3-style algorithms (KICK-03).
 *
 * Extends the FM2 2-op FM core (fm2.c) to FOUR operators with 4 selectable
 * OPL3-inspired routing algorithms, per-op AM (amplitude) + FM-index envelopes,
 * and clamped operator feedback. Distinctness (B-RESEARCH §FM4): aggressive,
 * woody, hollow, complex enharmonic overtones — richer than FM2's simple 2-op
 * punch.
 *
 * STATIC ROUTING TABLES (CLAUDE.md — NO per-sample branching on algorithm):
 * the 4 algorithms are encoded as `static const uint8_t g_fm4_algo[4][NUM_OPS]`
 * routing tables that the render loop WALKS. Two tables:
 *   g_fm4_algo[algo][op]    = index of the op that phase-modulates `op`
 *                             (FM4_NONE = 0xFF: no modulator, a pure carrier/root)
 *   g_fm4_carrier[algo][op] = 1 if `op` sums to the output (a carrier)
 * Operators are evaluated in a FIXED order op0..op3 where a higher-index op may
 * modulate a lower-index op (op3 -> op2 -> op1 -> op0), so a single forward pass
 * reads each modulator's already-computed output this sample (classic FM chain).
 *
 *   ALGO0: 3->2->1->0 chain (one carrier op0, deep evolving)
 *   ALGO1: (3->2)+(1->0) two stacks summed (carriers op2, op0 — richer 2-voice)
 *   ALGO2: 3->{2,1,0} one modulator, 3 carriers (bright, additive-ish)
 *   ALGO3: (3->0)+1+2 one FM pair + 2 additive carriers (hollow / woody)
 *
 * All 4 ops read the shared .rodata g_sine_table (D-04); no per-sample sinf.
 * FEEDBACK is op3 self-feedback (last output folded into its phase), scaled and
 * HARD-CLAMPED so it cannot run away (B-RESEARCH: "clamp to avoid runaway").
 * Body f0 + CURVE pitch sweep reuse the FM2 dual-envelope 808<->909 blend.
 *
 * State overlays bohm_instance.model_state; all coeff/transcendental work
 * happens in set_param/trigger, never per-sample (no tanf/expf/powf in render).
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FM4_NUM_OPS 4
#define FM4_NONE    0xFFu   /* sentinel: op has no modulator (pure carrier/root) */
#define FM4_NALGO   4

/* Modulator source per op, per algorithm. Ops evaluate op0..op3 but a mod source
 * is always a HIGHER index than its target (op3 modulates op2/op1/op0, op1 mod
 * op0), so we walk op3->op0 and each op reads its already-computed modulator this
 * sample — a single forward pass, NO per-sample algorithm branching. */
static const uint8_t g_fm4_algo[FM4_NALGO][FM4_NUM_OPS] = {
    /* op0        op1        op2        op3   */
    { 1u,        2u,        3u,        FM4_NONE }, /* ALGO0 chain 3->2->1->0        */
    { 1u,        FM4_NONE,  3u,        FM4_NONE }, /* ALGO1 (3->2)+(1->0)           */
    { 3u,        3u,        3u,        FM4_NONE }, /* ALGO2 3->{2,1,0} (3 carriers) */
    { 3u,        FM4_NONE,  FM4_NONE,  FM4_NONE }, /* ALGO3 (3->0)+1+2              */
};

/* Which ops sum to the output (carriers) per algorithm. */
static const uint8_t g_fm4_carrier[FM4_NALGO][FM4_NUM_OPS] = {
    /* op0 op1 op2 op3 */
    { 1u, 0u, 0u, 0u }, /* ALGO0: only op0 is heard */
    { 1u, 0u, 1u, 0u }, /* ALGO1: op0 + op2 (two stacks) */
    { 1u, 1u, 1u, 0u }, /* ALGO2: op0,op1,op2 carriers (op3 is the shared modulator) */
    { 1u, 1u, 1u, 0u }, /* ALGO3: op0 (FM'd by op3) + op1 + op2 additive */
};

/* Default per-op frequency ratios (harmonic thump): {1,1,2,3}. OP RATIO spreads
 * these; ALGO (the second control) detunes/spreads for metallic character. */
static const float g_fm4_base_ratio[FM4_NUM_OPS] = { 1.0f, 1.0f, 2.0f, 3.0f };

/* ---- FM4 per-instance state (overlays bohm_instance.model_state) --------- */
typedef struct fm4_state {
    /* Pitch / carrier body */
    float f0;                 /* fundamental Hz (from PITCH) */
    float sweep_hz;           /* pitch-env depth (Hz) from CURVE coupling */
    env_t pitch_env_fast;     /* 909 fast sweep */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* 0 = 808 (slow), 1 = 909 (fast) */

    /* Amplitude envelope + tail contour */
    env_t amp_env;
    float sustain;
    float length_ms;

    /* 4 operators: phase, per-op ratio, per-op AM env, per-op index env. */
    float op_phase[FM4_NUM_OPS];
    float op_ratio[FM4_NUM_OPS];    /* effective ratio (base * spread [* detune]) */
    env_t op_am[FM4_NUM_OPS];       /* per-op amplitude (AM) envelope */
    env_t op_idx[FM4_NUM_OPS];      /* per-op FM index decay envelope */
    float op_last[FM4_NUM_OPS];     /* last op output (for feedback) */

    /* Page-2 macro controls */
    int   algo;                     /* ALGORITHM (0..3): routing table select */
    float op_ratio_spread;          /* OP RATIO: per-op ratio spread [0,1] */
    float op_index;                 /* OP INDEX: global FM depth [0,1] */
    float op_amp;                   /* OP AMP: carrier-mix balance [0,1] */
    float feedback;                 /* FEEDBACK: op3 self-feedback (clamped) */
    float algo2;                    /* ALGO: algo-morph / per-op detune spread */

    /* Page-1 ATTACK/TRS: a short transient click layered on the FM body. */
    float trs_amp;                  /* ATTACK: click amplitude */
    env_t trs_env;                  /* click decay (Page-1 TRS DEC) */
    float trs_dec_ms;
    tpt1_t trs_tone_lp; float trs_tone_g;  /* Page-1 TRS TNE: click brightness LP */
    float trs_phase;

    /* Output COLOR lowpass */
    tpt1_t color_lp;    float color_g;

    /* Post-kick FX chain (KICK-14). */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} fm4_state;

_Static_assert(sizeof(fm4_state) <= 4096, "fm4_state fits model_state");

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

/* Recompute the 4 effective op ratios from OP RATIO spread + ALGO detune. The
 * spread pushes the upper ops away from the fundamental (more inharmonic);
 * ALGO adds a small per-op fractional detune for a metallic character. All done
 * at CONTROL rate (called from set_param), never per sample. */
static void fm4_recompute_ratios(fm4_state *f) {
    for (int i = 0; i < FM4_NUM_OPS; i++) {
        float base = g_fm4_base_ratio[i];
        /* Spread: stretch the non-fundamental ops upward (0 = harmonic, 1 = wide). */
        float spread = 1.0f + f->op_ratio_spread * (float)i * 0.5f;
        /* ALGO detune: small fractional offset per op for metallic enharmonics. */
        float detune = 1.0f + f->algo2 * ((i & 1) ? 0.03f : -0.02f) * (float)(i + 1);
        f->op_ratio[i] = base * spread * detune;
    }
}

/* ---- Parameter dispatch (Page 1 + FM4 Page 2 keys) ----------------------- */
void fm4_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    fm4_state *f = (fm4_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        f->f0 = omega_pitch_hz(val);
        f->sweep_hz = clampf(f->f0 * (1.5f + f->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        f->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        f->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        f->curve = v;
        f->sweep_hz = clampf(f->f0 * (1.5f + f->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_ATTACK) == 0) {
        f->trs_amp = v;
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        f->trs_dec_ms = 1.0f + v * (30.0f - 1.0f);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        float fc = 500.0f + v * (16000.0f - 500.0f);
        f->trs_tone_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_COLOR) == 0) {
        float fc = 50.0f + v * (18000.0f - 50.0f);   /* floor ~50 Hz (fully closed) */
        f->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_FM4_ALGO) == 0) {
        /* ALGORITHM: enum index 0..3; parse raw string to avoid 0..1 clamp. */
        int a = (int)(parse_f(val) + 0.5f);
        f->algo = a < 0 ? 0 : (a >= FM4_NALGO ? FM4_NALGO - 1 : a);
    } else if (strcmp(key, PK_FM4_OPRATIO) == 0) {
        f->op_ratio_spread = v;
        fm4_recompute_ratios(f);
    } else if (strcmp(key, PK_FM4_OPINDEX) == 0) {
        f->op_index = v;                    /* global FM depth */
    } else if (strcmp(key, PK_FM4_OPAMP) == 0) {
        f->op_amp = v;                      /* carrier-mix balance */
    } else if (strcmp(key, PK_FM4_FEEDBACK) == 0) {
        /* FEEDBACK: op3 self-feedback. Scale to a musically-useful, SAFE range
         * and hard-clamp so it cannot run away (default ~0.2 at v=0.5). */
        f->feedback = clampf(v * 0.7f, 0.0f, 0.7f);
    } else if (strcmp(key, PK_FM4_ALGO2) == 0) {
        f->algo2 = v;                       /* ALGO: per-op detune / metallic spread */
        fm4_recompute_ratios(f);
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        f->fx_type = (float)(int)(parse_f(val) + 0.5f);
        if (f->fx_type < 0.0f) f->fx_type = 0.0f;
        if (f->fx_type > 4.0f) f->fx_type = 4.0f;
        fx_config(&f->fx, (int)f->fx_type, f->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        f->fx_amt = v;
        fx_config(&f->fx, (int)f->fx_type, f->fx_amt);
    }
    /* Unknown keys ignored (dsp.c owns PK_MODEL/PK_MASTER_VOL/PK_UI_HIER). */
}

/* ---- Trigger (note-on) --------------------------------------------------- */
static void fm4_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note;
    fm4_state *f = (fm4_state *)inst->model_state;
    float velf = (float)velocity / 127.0f;

    float c909 = env_coeff_from_ms(15.0f);
    float c808 = env_coeff_from_ms(300.0f);
    env_trigger(&f->pitch_env_fast, 1.0f, c909);
    env_trigger(&f->pitch_env_slow, 1.0f, c808);

    float len_ms = f->length_ms > 0.0f ? f->length_ms : 400.0f;
    env_trigger(&f->amp_env, velf, env_coeff_from_ms(len_ms));

    /* Ensure the op ratios are valid even if OP RATIO / ALGO were never set
     * (a bare trigger with a zero-initialised state would otherwise leave all
     * ratios at 0 -> DC). Recompute from the current spread/detune (0 by
     * default -> the harmonic base {1,1,2,3}). Control rate — never per sample. */
    if (f->op_ratio[0] <= 0.0f) fm4_recompute_ratios(f);

    /* Per-op AM + index envelopes: higher ops (modulators) decay faster so the
     * attack is bright / complex and the tail settles toward the carriers. */
    for (int i = 0; i < FM4_NUM_OPS; i++) {
        float am_ms  = 400.0f - (float)i * 80.0f;   /* op0 longest, op3 shortest */
        float idx_ms = 60.0f  - (float)i * 10.0f;   /* index env decays quickly */
        if (am_ms  < 20.0f) am_ms  = 20.0f;
        if (idx_ms < 10.0f) idx_ms = 10.0f;
        env_trigger(&f->op_am[i],  1.0f, env_coeff_from_ms(am_ms));
        env_trigger(&f->op_idx[i], 1.0f, env_coeff_from_ms(idx_ms));
        f->op_phase[i] = 0.0f;
        f->op_last[i]  = 0.0f;
    }

    /* Transient click. */
    float trs_ms = f->trs_dec_ms > 0.0f ? f->trs_dec_ms : 8.0f;
    env_trigger(&f->trs_env, 1.0f, env_coeff_from_ms(trs_ms));
    f->trs_phase = 0.0f;

    f->color_lp.s    = 0.0f;
    f->trs_tone_lp.s = 0.0f;

    f->fx.last     = 0.0f;
    f->fx.hold_ctr = 0;
}

/* ---- Render (per-sample; no transcendentals; walk the static routing table) */
static void fm4_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    fm4_state *f = (fm4_state *)inst->model_state;

    /* Snapshot the active algorithm's routing rows ONCE per block (control rate) —
     * the per-sample loop only indexes these arrays, never branches on algo. */
    const uint8_t *modsrc  = g_fm4_algo[f->algo];
    const uint8_t *carrier = g_fm4_carrier[f->algo];

    /* Count carriers so the summed output is normalized (self-limit < 1.0). */
    float ncar = 0.0f;
    for (int i = 0; i < FM4_NUM_OPS; i++) ncar += (float)carrier[i];
    float carnorm = ncar > 0.0f ? (1.0f / ncar) : 1.0f;

    /* Global FM depth scalar (OP INDEX). iter-2: QUADRATIC taper (0..~4) so low
     * and mid settings are gentle/musical instead of harsh noise/transients, with
     * headroom only at the very top. */
    float gidx = f->op_index * f->op_index * 4.0f;

    for (int n = 0; n < frames; n++) {
        float p_fast = env_tick(&f->pitch_env_fast);
        float p_slow = env_tick(&f->pitch_env_slow);
        float pitch  = p_slow + f->curve * (p_fast - p_slow);   /* 808<->909 lerp */
        float fcar   = f->f0 + pitch * f->sweep_hz;

        /* Per-op index/AM envelopes ticked once per sample. */
        float idxenv[FM4_NUM_OPS], amenv[FM4_NUM_OPS];
        for (int i = 0; i < FM4_NUM_OPS; i++) {
            idxenv[i] = env_tick(&f->op_idx[i]);
            amenv[i]  = env_tick(&f->op_am[i]);
        }

        /* Evaluate ops op3 -> op0 so a modulator (higher index) is computed
         * before the op it feeds. op3 uses its own last output for FEEDBACK. */
        float opout[FM4_NUM_OPS];
        for (int i = FM4_NUM_OPS - 1; i >= 0; i--) {
            float fmod_in = 0.0f;
            uint8_t src = modsrc[i];
            if (src != FM4_NONE) {
                /* Modulator already computed this sample (src > i). */
                fmod_in = opout[src] * idxenv[i] * gidx;
            }
            /* op3 self-feedback (clamped): fold its previous output into phase. */
            if (i == FM4_NUM_OPS - 1) {
                fmod_in += f->op_last[i] * f->feedback;
            }
            float ph = f->op_phase[i] + fmod_in;
            ph -= floorf(ph);                       /* wrap into [0,1) */
            float o = wt_read(g_sine_table, ph);
            opout[i] = o;

            /* Advance this op's phase by its ratio*carrier frequency. */
            f->op_phase[i] += (f->op_ratio[i] * fcar) / OMEGA_SR;
            if (f->op_phase[i] >= 1.0f) f->op_phase[i] -= 1.0f;
        }
        /* Store op3's output for next-sample feedback. */
        f->op_last[FM4_NUM_OPS - 1] = opout[FM4_NUM_OPS - 1];

        /* Sum carriers (AM-enveloped), normalized. OP AMP tilts the balance
         * toward the higher carriers for a brighter mix. */
        float voice = 0.0f;
        for (int i = 0; i < FM4_NUM_OPS; i++) {
            if (carrier[i]) {
                float bal = 0.6f + 0.4f * f->op_amp * (float)(i + 1) / (float)FM4_NUM_OPS;
                voice += opout[i] * amenv[i] * bal;
            }
        }
        voice *= carnorm;

        float amp = env_tick(&f->amp_env) * (0.5f + 0.5f * f->sustain);
        voice *= amp;

        /* Transient click (Page-1 ATTACK/TRS DEC/TRS TNE). */
        float click = f->trs_amp * env_tick(&f->trs_env)
                    * wt_read(g_sine_table, f->trs_phase);
        f->trs_phase += (fcar * 3.0f) / OMEGA_SR;
        if (f->trs_phase >= 1.0f) f->trs_phase -= 1.0f;
        click = tpt1_lp(&f->trs_tone_lp, click, f->trs_tone_g);

        /* Body + click (self-limited < 1.0; clamp is a net). */
        float s = voice * 0.7f + click * 0.4f;
        s = tpt1_lp(&f->color_lp, s, f->color_g);       /* COLOR output LP */

        /* Post-kick FX (KICK-14): fx_type is integer 0..4 (Bug #1 fix). */
        int fx_mode = (int)f->fx_type;
        s = fx_process(fx_mode, s, f->fx_amt, &f->fx);

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot delegation --------------------------------------------- */
static void fm4_set_p2(bohm_instance_t *inst, const char *key, const char *val) {
    fm4_set_param(inst, key, val);
}

/* ---- Page-2 slot descriptor (all 6 FM4 slots; Pattern 3) ----------------- */
static int fm4_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;
    static const char json[] =
        "{\"key\":\"" PK_FM4_ALGO     "\",\"name\":\"Algorithm\",\"short_name\":\"ALGO\",\"type\":\"enum\",\"options\":[\"Chain\",\"Stacks\",\"Bright\",\"Hollow\"]},"
        "{\"key\":\"" PK_FM4_OPRATIO  "\",\"name\":\"Op Ratio\",\"short_name\":\"OPRAT\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"},"
        "{\"key\":\"" PK_FM4_OPINDEX  "\",\"name\":\"Op Index\",\"short_name\":\"OPIDX\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"},"
        "{\"key\":\"" PK_FM4_OPAMP    "\",\"name\":\"Op Amp\",\"short_name\":\"OPAMP\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"},"
        "{\"key\":\"" PK_FM4_FEEDBACK "\",\"name\":\"Feedback\",\"short_name\":\"FBACK\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"step\":\"0.01\",\"unit\":\"%\"}";
        /* B2 (VOICE-04): ALGO2 (metallic-detune morph) merged away to fit FM4's
         * unique params on Kick Page 1 (PITCH/LENGTH/CURVE + 5). PK_FM4_ALGO2 is
         * still handled in set_param (defaulted via the cache prime), just not a
         * separate UI slot — its detune folds into OP RATIO spread. */
    int len = (int)(sizeof(json) - 1);
    if (buf_len <= len) return 0;        /* bounded: no overflow */
    memcpy(buf, json, (size_t)len);
    buf[len] = '\0';
    return len;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_fm4_vtable = {
    .name         = "FM4",
    .trigger      = fm4_trigger,
    .render       = fm4_render,
    .set_param    = fm4_set_param,
    .set_p2       = fm4_set_p2,
    .p2_slot_desc = fm4_p2_slot_desc,
};
