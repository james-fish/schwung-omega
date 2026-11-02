/* groove.c — Groove rumble voice (Phase C, GRV-01/02/03/05).
 *
 * The non-GEN groove voice: a pre-allocated circular delay ring read back at
 * four 16th-note offsets (GRV-01), a transport-locked tempo clock (GRV-02, the
 * phase's value-at-risk), Groove Page-1 controls + a COLOR lowpass + a MONO
 * force-sum (GRV-03/05). All state lives on bohm_instance's groove_state_t by
 * value (one calloc); this TU adds NO allocation.
 *
 * RT-SAFETY: groove_tick is called PER SAMPLE and contains NO malloc/free, NO
 * host->log, NO file I/O, and NO per-sample division/transcendental. Every
 * powf/tanf runs at CONTROL rate (groove_update_tempo / groove_set_param). The
 * ring wrap uses `& GRV_DELAY_MASK`, never `%` (DC-01, branch-free).
 *
 * TEMPO SOURCE (GRV-02): the tap interval is derived ONLY from the guarded host
 * transport chain — beat-delta from get_beat_position, then get_bpm, then a
 * last-resort 120 constant. There is NO hardcoded live tap interval (the
 * reference-code hardcoded-120 bug this phase kills).
 */
#include "groove.h"
#include "dsp_primitives.h"   /* tpt1_lp, tpt1_t, OMEGA_SR */
#include "omega.h"            /* host_api_v1_t */

#include <math.h>
#include <stddef.h>
#include <string.h>

/* ---- Locale-independent float parser (UI-01; copied from gen.c/dsp.c) ---- */
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

/* Fractional-tap slew rate (~30 ms one-pole toward spq_target; vhr §A.6). */
#define SPQ_SLEW 0.0005f

/* Number of ghost-kick taps in the FIR rumble (reuses the four TAP1..4 knobs). */
#ifndef NTAPS
#define NTAPS 4
#endif

/* ---- Warehouse reverb (v0.6) ----------------------------------------------
 * The 2010s-techno warehouse: concrete early reflections, a big diffuse FDN tail
 * and grit. Signal path (mono in, stereo out):
 *
 *   in -> HPF (keeps the kick/sub out of the room) -> early-reflection line
 *      -> taps (6, alternating L/R: the walls)                    -> ER out
 *      -> pre-delay -> 8-line Hadamard FDN                        -> tail out
 *         each line: damping LP (TONE) -> in-loop HP (~90 Hz) -> grit
 *         (soft saturation + optional bit reduction) -> x DECAY gain
 *   (ER + tail) -> sample-and-hold decimator (lo-fi) -> dry/wet MIX
 *
 * Grit lives INSIDE the loop, so the tail roughens as it recirculates; the bit
 * reduction truncates the very end of the tail into a gritty gate. Every
 * coefficient is control-rate (groove_set_param / rv_apply_type); the per-sample
 * cost is 8 line reads + 1 FWHT + 8 one-poles + 6 ER taps, no transcendentals.
 * Bounded: loop gain < 1, saturation is unity-slope at 0 and caps |x| <= 1.5. */
#define RV_LINE_MASK (RV_LINE_LEN - 1u)
#define RV_ER_MASK   (RV_ER_LEN - 1u)

/* Mutually prime base lengths, ~25..73 ms at 44.1 kHz — big-room spacing. */
static const int RV_BASE[RV_LINES] = { 1123, 1367, 1597, 1871, 2141, 2473, 2797, 3203 };

typedef struct {
    float size;           /* line-length scale */
    float pre_ms;         /* FDN pre-delay */
    int   er[6];          /* early reflection taps (frames) */
    float erg[6];         /* their gains (sign = wall side polarity) */
    float er_mix;
    float grit;           /* in-loop saturation */
    float bits;           /* in-loop quantiser steps, 0 = off */
    float hold;           /* output S&H ratio (1 = off) */
    float hp_hz;          /* input HPF */
    float mod;            /* line modulation depth (frames) */
} rv_type_t;

static const rv_type_t RV_TYPES[4] = {
    /* Warehouse: big, slightly dark, a little dirty */
    { 1.00f, 22.0f, { 331, 787, 1291, 1777, 2459, 3121 },
      { 0.80f, -0.66f, 0.55f, -0.47f, 0.36f, -0.28f }, 0.55f, 0.30f, 0.0f, 1.35f, 160.0f, 6.0f },
    /* Concrete: smaller bunker, dense hard slap */
    { 0.62f, 7.0f, { 181, 397, 613, 887, 1123, 1499 },
      { 0.90f, -0.80f, 0.70f, -0.60f, 0.50f, -0.40f }, 0.80f, 0.18f, 0.0f, 1.0f, 200.0f, 3.0f },
    /* Tunnel: long pre-delay, fluttering evenly spaced echoes */
    { 0.88f, 48.0f, { 620, 1240, 1860, 2480, 3100, 3720 },
      { 0.70f, -0.62f, 0.54f, -0.46f, 0.38f, -0.30f }, 0.70f, 0.22f, 0.0f, 1.0f, 180.0f, 12.0f },
    /* Dirt: the warehouse through a cheap sampler — 6-bit loop, ~14 kHz hold */
    { 1.00f, 18.0f, { 331, 787, 1291, 1777, 2459, 3121 },
      { 0.80f, -0.66f, 0.55f, -0.47f, 0.36f, -0.28f }, 0.50f, 0.70f, 48.0f, 3.2f, 220.0f, 5.0f },
};

static void rv_apply_type(groove_state_t *g, int t) {
    if (t < 0) t = 0; if (t > 3) t = 3;
    const rv_type_t *T = &RV_TYPES[t];
    g->rv_type = t;
    for (int i = 0; i < RV_LINES; i++) {
        int n = (int)((float)RV_BASE[i] * T->size + 0.5f);
        if (n < 64) n = 64;
        if (n > (int)RV_LINE_LEN - 24) n = (int)RV_LINE_LEN - 24;
        g->rv_len[i] = n;
    }
    for (int k = 0; k < 6; k++) {
        int d = T->er[k]; if (d > (int)RV_ER_LEN - 2) d = (int)RV_ER_LEN - 2;
        g->rv_er_tap[k] = d; g->rv_er_gain[k] = T->erg[k];
    }
    int pre = (int)(T->pre_ms * 0.001f * OMEGA_SR + 0.5f);
    if (pre < 1) pre = 1; if (pre > (int)RV_ER_LEN - 2) pre = (int)RV_ER_LEN - 2;
    g->rv_pre = pre;
    g->rv_er_mix = T->er_mix;
    g->rv_grit = T->grit;
    g->rv_bits = T->bits;
    g->rv_hold = T->hold;
    g->rv_mod_depth = T->mod;
    g->rv_in_g = tanf((float)M_PI * T->hp_hz / OMEGA_SR);
}

/* unity slope at 0, saturates toward +-1.5 (rational, no transcendental) */
static inline float rv_sat(float x) {
    float ax = x < 0.0f ? -x : x;
    return x * (1.0f + 0.25f * ax) / (1.0f + 0.5f * ax + 0.1667f * ax * ax);
}

static inline float tpt_g_from_hz(float fc) {
    return tanf((float)M_PI * fc / OMEGA_SR);   /* control-rate only */
}

/* v0.4 dual root: derive gen_base_hz from whichever control matches the mode —
 * Unquantized uses the Hz control, a scale uses the note control. No dynamic UI
 * descriptor swap (two separate controls, distinct keys); control-rate only. */
static inline void gen_recompute_base_hz(groove_state_t *g) {
    /* v0.4.1: ROOT merged into a SINGLE Hz control used in BOTH modes (the separate
     * ROOT NOTE control is gone). The Hz knob is the base pitch; SCALE only governs
     * how the sequence degrees quantize relative to that base (scale_quantize). */
    g->gen_base_hz = clampf(g->gen_root_hz, GROOT_HZ_MIN, GROOT_HZ_MAX);
}

/* NOTE LEN enum -> step-duration multiplier in 16th-notes. Options (ui.c OPT_NOTELEN):
 * 0:1/4d(6) 1:1/4(4) 2:1/8d(3) 3:1/8(2) 4:1/16d(1.5) 5:1/16(1) 6:1/32(0.5). */
static inline float gen_notelen_to_mult(int idx) {
    static const float M[7] = { 6.0f, 4.0f, 3.0f, 2.0f, 1.5f, 1.0f, 0.5f };
    if (idx < 0) idx = 0; if (idx > 6) idx = 6;
    return M[idx];
}

/* GEN amp/filter env decay times from the DECAY knob (v0.4.1, item 11): pluckier
 * than before — a v*v expo so mid-knob is short. tau: 4 ms .. 160 ms. The amp env
 * decays toward a 0.5 sustain floor (see groove_tick); the filter env decays to 0
 * at 0.65× the amp tau (closes before the note settles). expf at CONTROL rate. */
#define GEN_SUSTAIN 0.5f
static void gen_ahd_coefs(groove_state_t *g);
static inline void gen_set_decay(groove_state_t *g, float v) {
    g->gen_decay = v;
    float vp    = v * v;                         /* plucky expo spread */
    float tau_s = 0.004f * powf(40.0f, vp);      /* 4 ms .. ~160 ms (pluck) */
    g->gen_env_coef      = expf(-1.0f / (tau_s * OMEGA_SR));
    /* The 50% body sustains ~8x the pluck so the note has an audible sustained tone
     * (not just a transient click) yet still releases to silence when the sequencer
     * stops. At the default DECAY this is ~80 ms; raising DECAY lengthens both. */
    float body_tau_s     = 8.0f * tau_s;
    g->gen_sus_coef      = expf(-1.0f / (body_tau_s * OMEGA_SR));
    /* Filter env MIRRORS the amp env (same pluck + 50% body shape) but at 65% of
     * each time constant -> the filter always closes a bit ahead of the amp (plucky
     * filter action). Attack is instant for both (envs set to 1.0 on the gate). */
    float filt_tau_s      = 1.3f * tau_s;    /* v0.6.2: slower = less clicky */
    g->gen_filt_env_coef  = expf(-1.0f / (filt_tau_s * OMEGA_SR));
    float filt_body_tau_s = 1.0f * body_tau_s;
    g->gen_filt_sus_coef  = expf(-1.0f / (filt_body_tau_s * OMEGA_SR));
    /* Acid voices: one long filter sweep (303-style), ~60 ms .. ~1 s with DECAY. */
    g->gen_aenv_coef      = expf(-1.0f / ((0.03f + 6.0f * tau_s) * OMEGA_SR));
    gen_ahd_coefs(g);
}

/* ---- FIR tap-weight precompute (control-rate, Phase 1 §Rumble Core) ------- */
/* Each ghost-kick tap k (k=1..NTAPS) is read at k·spq behind the write head and
 * weighted by tap_level[k-1] · exp(-age_k/tau), where age_k = k·spq/SR seconds
 * and tau = tap_tau_s (from LENGTH). expf runs HERE (control rate), never per
 * sample. tap_norm = rumble_makeup/sqrt(Σ tap_w²) gives equal-power makeup so a
 * long smeared tail is not louder than a short distinct one, and a short tail is
 * still audible (divisor floored at sqrt(0.25)=0.5). */
static void groove_update_tap_weights(groove_state_t *g) {
    float tau = g->tap_tau_s > 1e-4f ? g->tap_tau_s : 1e-4f;
    float inv_tau = 1.0f / tau;
    float energy = 0.0f;
    for (int k = 1; k <= NTAPS; k++) {
        float age_s = (float)k * g->spq_target / OMEGA_SR;   /* seconds */
        float w = g->tap_level[k - 1] * expf(-age_s * inv_tau);  /* expf: CONTROL rate */
        g->tap_w[k - 1] = w;
        energy += w * w;
    }
    float norm = sqrtf(energy > 0.25f ? energy : 0.25f);
    g->tap_norm = g->rumble_makeup / norm;
}

/* ---- LENGTH -> decay time constant (control-rate) -------------------------- */
/* iter-3: MORE length = LONGER decay = more overlapping taps (intuitive). v=0
 * (left) tau ~30 ms → only tap1, distinct plucks; v=1 (right) tau ~1200 ms → all
 * taps sustain into a smeared continuous rumble. Exponential (30·40^v) so the
 * multi-tap region spans ~v>0.4 upward instead of collapsing to tap1 past 60%.
 * powf at control rate only; NO feedback — LENGTH shapes the FIR tap envelope. */
static void groove_set_length(groove_state_t *g, float v) {
    g->tap_tau_s = (30.0f * powf(40.0f, v)) * 0.001f;        /* powf: CONTROL rate */
    groove_update_tap_weights(g);
}

/* ---- Routable FX blocks (Phase 1 FX-ROUTE) -------------------------------- */
/* DRIVE block: saturation + makeup, dry/wet by amount, then the LFO tremolo
 * (DRIVE's "+ its FX: LFO"). The LFO phase is advanced ONCE per sample in
 * groove_tick (after the route loop) so the tremolo rate is order-independent. */
static inline void groove_drive_block(groove_state_t *g, float *l, float *r) {
    if (g->drive > 0.0f) {
        /* DIODE drive (Finding 7 — matched to the kick FX diode character):
         * asymmetric soft-clip (positive half saturates harder than the negative)
         * → even-harmonic "diode" grit. The pre-gain curve is strengthened from
         * 1+drive*4 to 1+drive*9 so full-right is noticeably aggressive (comparable
         * to the kick's FX drive). Output-gain makeup mirrors the kick's
         * out_gain = 1/(1+amt*comp) form (stronger comp to track the hotter drive),
         * so raising DRIVE changes CHARACTER not just level. The x/(1±x) transfer
         * self-limits → bounded output, no new transcendental (RT-safe). */
        float k  = 1.0f + g->drive * 9.0f;
        float mk = 1.0f / (1.0f + g->drive * 1.3f);
        float gl = *l, gr = *r;
        float xl = gl * k, xr = gr * k;
        float wl = (xl >= 0.0f ? xl / (1.0f + xl) : xl / (1.0f - 0.5f * xl)) * mk;
        float wr = (xr >= 0.0f ? xr / (1.0f + xr) : xr / (1.0f - 0.5f * xr)) * mk;
        *l = gl + g->drive * (wl - gl);
        *r = gr + g->drive * (wr - gr);
    }
}

/* v0.6.2 SEQ delay tone (control rate): below noon the repeats darken (LP 12 kHz
 * -> 600 Hz), above noon they thin out (HP 30 Hz -> 1.2 kHz). Both live in the loop,
 * so every repeat is a little darker / thinner than the last. */
static void groove_dly_tone(groove_state_t *g, float t) {
    g->dly_tone = t;
    float lp = t < 0.5f ? 600.0f * powf(20.0f, t * 2.0f) : 12000.0f;
    float hp = t > 0.5f ? 30.0f * powf(40.0f, (t - 0.5f) * 2.0f) : 30.0f;
    g->dly_lp_a = 1.0f - expf(-2.0f * (float)M_PI * lp / OMEGA_SR);
    g->dly_hp_a = 1.0f - expf(-2.0f * (float)M_PI * hp / OMEGA_SR);
}
/* delay times in 16ths: 1/16, 1/8T, 1/8, 1/8D, 1/4, 1/4D */
static const float DLY_16THS[6] = { 1.0f, 4.0f / 3.0f, 2.0f, 3.0f, 4.0f, 6.0f };

/* REVERB block (v0.6 warehouse). NEVER writes to the taps delay ring. */
static inline void groove_reverb_block(groove_state_t *g, float *l, float *r) {
    if (g->rv_mix <= 0.0f) {
        /* keep the tail decaying silently would cost the full FDN; at MIX 0 the
         * reverb is off and costs nothing (the tail is simply dropped). */
        return;
    }
    float in = g->rv_send;          /* v0.6.2: per-source sends (Space page) */
    tpt1_t hp = { g->rv_in_lp };
    float lo = tpt1_lp(&hp, in, g->rv_in_g);
    g->rv_in_lp = hp.s;
    float x = in - lo;

    /* early reflections + pre-delay share one line */
    unsigned ei = g->rv_er_i;
    g->rv_er[ei & RV_ER_MASK] = x;
    float erl = 0.0f, err = 0.0f;
    for (int k = 0; k < 6; k += 2) {
        erl += g->rv_er_gain[k]     * g->rv_er[(ei - (unsigned)g->rv_er_tap[k])     & RV_ER_MASK];
        err += g->rv_er_gain[k + 1] * g->rv_er[(ei - (unsigned)g->rv_er_tap[k + 1]) & RV_ER_MASK];
    }
    float fin = g->rv_er[(ei - (unsigned)g->rv_pre) & RV_ER_MASK];
    g->rv_er_i = ei + 1u;

    /* FDN line reads; lines 0 and 5 are slowly modulated (fractional read) */
    unsigned wi = g->rv_wi;
    float y[RV_LINES];
    float mph = g->rv_mod_ph;
    float tri = (mph < 0.5f) ? (4.0f * mph - 1.0f) : (3.0f - 4.0f * mph);
    g->rv_mod_ph = mph + 0.37f / OMEGA_SR; if (g->rv_mod_ph >= 1.0f) g->rv_mod_ph -= 1.0f;
    for (int i = 0; i < RV_LINES; i++) {
        if (i == 0 || i == 5) {
            float d = (float)g->rv_len[i] + 8.0f + (i == 0 ? tri : -tri) * g->rv_mod_depth;
            float rp = (float)wi - d;
            float fl = floorf(rp);
            float fr = rp - fl;
            unsigned i0 = ((unsigned)(int)fl) & RV_LINE_MASK, i1 = (i0 + 1u) & RV_LINE_MASK;
            y[i] = g->rv_line[i][i0] + fr * (g->rv_line[i][i1] - g->rv_line[i][i0]);
        } else {
            y[i] = g->rv_line[i][(wi - (unsigned)g->rv_len[i]) & RV_LINE_MASK];
        }
    }
    float outl = y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] - y[7]);
    float outr = y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] - y[4]);

    /* Hadamard mix (fast Walsh-Hadamard, normalised) */
    float h[RV_LINES];
    for (int i = 0; i < RV_LINES; i++) h[i] = y[i];
    for (int st = 1; st < RV_LINES; st <<= 1)
        for (int i = 0; i < RV_LINES; i += st << 1)
            for (int j = i; j < i + st; j++) {
                float a = h[j], b = h[j + st];
                h[j] = a + b; h[j + st] = a - b;
            }
    const float norm = 0.35355339f;
    float d = g->rv_damp_eff, fb = g->rv_fb, gr = g->rv_grit, bits = g->rv_bits, hk = g->rv_hp_k;
    for (int i = 0; i < RV_LINES; i++) {
        float v = h[i] * norm;
        g->rv_lp[i] = v + d * (g->rv_lp[i] - v);           /* damping LP */
        v = g->rv_lp[i];
        g->rv_hp[i] += hk * (v - g->rv_hp[i]);             /* in-loop HP */
        v -= g->rv_hp[i];
        if (gr > 0.0f) v += gr * (rv_sat(v * 2.0f) * 0.5f - v);
        if (bits > 0.0f) v = floorf(v * bits + 0.5f) / bits;
        float sgn = (i & 1) ? -0.5f : 0.5f;
        g->rv_line[i][wi & RV_LINE_MASK] = fin * sgn + v * fb;
    }
    g->rv_wi = wi + 1u;

    float wl = 0.42f * outl + g->rv_er_mix * erl;
    float wr = 0.42f * outr + g->rv_er_mix * err;
    /* lo-fi output decimator */
    if (g->rv_hold > 1.01f) {
        g->rv_hold_ph += 1.0f;
        if (g->rv_hold_ph >= g->rv_hold) { g->rv_hold_ph -= g->rv_hold; g->rv_hold_l = wl; g->rv_hold_r = wr; }
        wl = g->rv_hold_l; wr = g->rv_hold_r;
    }
    /* v0.6.2: a send/return reverb, so the dry path stays at full level */
    float m = g->rv_mix;
    *l += wl * m;
    *r += wr * m;
}

/* ---- GEN groove sequencer (C1-03, GRVX-04/05) ---------------------------- */
static inline unsigned long long grv_xorshift(unsigned long long *s) {
    unsigned long long x = *s ? *s : 0x9E3779B97F4A7C15ull;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    *s = x; return x;
}

/* ---- GEN Variation (v0.6) -------------------------------------------------
 * PATTERN picks how pitches walk over the gated steps (Random / Up / Down /
 * Ping-Pong / Leap / Pedal) or loads a 16-step techno RIFF (Offbeat, Rolling,
 * Acid, Call+Resp, Stabs, Octaves) whose notes are snapped to the scale. The
 * per-step dice (DROP, NOTE RND, OCT JUMP, RATCHET) are a hash of (seed, dice
 * index, salt); LOOP=N makes the dice index wrap every N steps so the SAME
 * variation repeats (Super Arp's loop), LOOP=0 re-rolls every pass. gen_seq
 * now holds SEMITONES (scale already applied) so the tick does no quantising. */
enum { GEN_PAT_RANDOM = 0, GEN_PAT_UP, GEN_PAT_DOWN, GEN_PAT_PINGPONG, GEN_PAT_LEAP,
       GEN_PAT_PEDAL, GEN_PAT_RIFF0, GEN_PAT_COUNT = GEN_PAT_RIFF0 + 6 };
#define GEN_REST (-128)
#define R GEN_REST
static const signed char GEN_RIFF[6][16] = {
    /* Offbeat bleep: the classic 2010s hypnotic offbeat */
    { R, R, 0, R,   R, R, 12, R,   R, R, 0, R,   R, R, 7, 12 },
    /* Rolling bass: 16th roll with octave pops, kick-gap on the downbeat */
    { R, 0, 0, 12,  R, 0, 0, 12,  R, 0, 0, 12,  R, 0, 3, 12 },
    /* Acid: 303-style line */
    { 0, 0, 12, 0,  3, R, 0, 15,  0, 7, R, 0,   12, 10, 0, 3 },
    /* Call + response: a rising call, a falling answer */
    { 0, R, 3, R,   7, R, R, 5,   12, R, 10, R,  7, R, 3, R },
    /* Stabs: dotted-8th minimal stabs */
    { 0, R, R, 0,   R, R, 0, R,   R, R, 0, R,   R, 0, R, R },
    /* Octaves: bouncing octave bleeps */
    { 0, 12, R, 12, 0, 12, R, 24, 0, 12, R, 12, 0, 12, 7, 12 },
};
static const unsigned short GEN_RIFF_ACC[6] = {
    0x2222, 0x1111, 0xA108, 0x8080, 0x8204, 0x4104,   /* bit 15 = step 0 */
};
#undef R

static inline float gen_rnd(unsigned seed, unsigned idx, unsigned salt) {
    unsigned x = seed * 0x9E3779B1u ^ (idx + 1u) * 0x85EBCA77u ^ salt * 0xC2B2AE3Du;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

/* pitch-class mask of the active scale (bits 0..11) */
static void gen_update_scale_mask(groove_state_t *g) {
    unsigned m = 0;
    for (int d = 0; d < 12; d++) {
        int t = scale_quantize(g->gen_scale, d);
        if (t >= 0 && t < 12) m |= 1u << t;
    }
    g->gen_scale_mask = (unsigned short)(m ? m : 0xFFFu);
}

/* nearest scale tone (downward first on a tie); Unquantized passes through */
static int gen_snap(const groove_state_t *g, int semi) {
    if (g->gen_unquantized) return semi;
    unsigned m = g->gen_scale_mask;
    for (int d = 0; d < 12; d++) {
        int c = semi - d, pc = ((c % 12) + 12) % 12;
        if ((m >> pc) & 1u) return c;
        c = semi + d; pc = ((c % 12) + 12) % 12;
        if ((m >> pc) & 1u) return c;
    }
    return semi;
}

static int gen_degree_semi(const groove_state_t *g, int deg) {
    return g->gen_unquantized ? deg : scale_quantize(g->gen_scale, deg);
}

/* Rebuild the step sequence + gate from PATTERN/SEED/SEQ LEN/DENSITY/ROTATE/
 * RANGE/SCALE. Control-rate (set_param). Deterministic for a given SEED. */
void groove_gen_rebuild(groove_state_t *g) {
    g->gen_rng = 0x2545F4914F6CDD1Dull ^ ((unsigned long long)g->gen_seed_raw * 0x9E3779B1u + 1u);
    gen_update_scale_mask(g);
    int len = g->gen_seqlen; if (len < 1) len = 1; if (len > 64) len = 64;
    int span = g->gen_range < 1 ? 1 : (g->gen_range > 24 ? 24 : g->gen_range);
    unsigned seed = g->gen_seed_raw * 7919u + 17u;
    int pat = g->gen_pattern; if (pat < 0) pat = 0; if (pat >= GEN_PAT_COUNT) pat = GEN_PAT_COUNT - 1;

    if (pat >= GEN_PAT_RIFF0) {
        int ri = pat - GEN_PAT_RIFF0;
        float dens = g->gen_density;
        for (int i = 0; i < 64; i++) {
            int t = i & 15;
            int semi = GEN_RIFF[ri][t];
            int acc = (GEN_RIFF_ACC[ri] >> (15 - t)) & 1;
            int on = semi != GEN_REST;
            if (on) {
                /* SEED mutates ~1 in 6 notes to a nearby scale tone */
                if (gen_rnd(seed, (unsigned)t, 11u) < 0.17f)
                    semi += (int)(gen_rnd(seed, (unsigned)t, 12u) * 10.0f) - 5;
                /* DENSITY below half thins the riff */
                if (dens < 0.5f && gen_rnd(seed, (unsigned)t, 13u) > dens * 2.0f + 0.05f) on = 0;
            } else if (dens > 0.5f && gen_rnd(seed, (unsigned)t, 14u) < (dens - 0.5f) * 1.2f) {
                /* DENSITY above half fills rests with ghost notes */
                on = 1; acc = 0;
                semi = (gen_rnd(seed, (unsigned)t, 15u) < 0.5f) ? 0 : 12;
            }
            if (!on) { g->gen_gate[i] = 0; g->gen_seq[i] = 0; g->gen_acc[i] = 0; continue; }
            g->gen_gate[i] = i < len;
            g->gen_seq[i] = (signed char)gen_snap(g, semi);
            g->gen_acc[i] = (unsigned char)acc;
        }
    } else {
        /* Euclidean gate: DENSITY -> npulses of len, spread evenly (Bresenham). */
        int npulse = (int)(g->gen_density * (float)len + 0.5f);
        if (npulse < 1) npulse = 1; if (npulse > len) npulse = len;
        int bucket = 0;
        for (int i = 0; i < 64; i++) {
            if (i < len) { bucket += npulse; if (bucket >= len) { bucket -= len; g->gen_gate[i] = 1; } else g->gen_gate[i] = 0; }
            else g->gen_gate[i] = 0;
        }
        /* walk degrees over the gated steps */
        int n = span / 2 + 1; if (n > 12) n = 12;      /* monotonic window of the scale table */
        int k = 0;
        for (int i = 0; i < 64; i++) {
            int deg;
            int raw = (int)(grv_xorshift(&g->gen_rng) % (unsigned)span);
            switch (pat) {
                case GEN_PAT_UP:       deg = k % n; break;
                case GEN_PAT_DOWN:     deg = (n - 1) - (k % n); break;
                case GEN_PAT_PINGPONG: { int p2 = n > 1 ? 2 * (n - 1) : 1, q = k % p2; deg = q < n ? q : p2 - q; } break;
                case GEN_PAT_LEAP:     deg = (k & 1) ? (n / 2 + raw % (n - n / 2 + 1)) : 0; break;
                case GEN_PAT_PEDAL:    deg = (k & 1) ? 0 : 1 + (k / 2) % (n > 1 ? n - 1 : 1); break;
                default:               deg = raw - span / 2; break;  /* Random, centred on the root */
            }
            g->gen_seq[i] = (signed char)gen_degree_semi(g, deg);
            /* accents: the beat (every 4th step) plus a seeded scatter */
            g->gen_acc[i] = (unsigned char)(((i & 3) == 0) || gen_rnd(seed, (unsigned)i, 16u) < 0.18f);
            if (g->gen_gate[i]) k++;
        }
    }
    /* ROTATE: bidirectional rotate of the pattern (gates, pitches, accents). */
    int rot = (int)(g->gen_rotate * (float)len);
    if (rot != 0 && len > 0) {
        unsigned char tg[64], ta[64]; signed char ts[64];
        for (int i = 0; i < len; i++) {
            int j = ((i - rot) % len + len) % len;
            tg[i] = g->gen_gate[j]; ta[i] = g->gen_acc[j]; ts[i] = g->gen_seq[j];
        }
        for (int i = 0; i < len; i++) { g->gen_gate[i] = tg[i]; g->gen_acc[i] = ta[i]; g->gen_seq[i] = ts[i]; }
    }
}

/* One note-on of the GEN voice: velocity/filter depth from the accent, pitch
 * from the step (+ the per-step dice), envelopes and oscillator phases reset. */
/* v0.6.2 amp AHD per voice: attack ms, hold as a fraction of the step, decay scale. */
static const float GEN_ATK_MS[GEN_V_COUNT]  = { 3.0f, 4.0f, 2.5f, 1.5f, 1.5f, 3.0f, 3.0f, 1.0f, 5.0f };
static const float GEN_HOLD_F[GEN_V_COUNT]  = { 0.5f, 0.55f, 0.45f, 0.7f, 0.7f, 0.45f, 0.45f, 0.15f, 0.4f };
static const float GEN_DEC_K[GEN_V_COUNT]   = { 1.0f, 1.1f, 0.9f, 0.8f, 0.8f, 0.9f, 0.9f, 0.5f, 1.0f };

static void gen_ahd_coefs(groove_state_t *g) {
    int v = g->gen_voice;
    float tau = 0.02f * powf(20.0f, g->gen_decay) * GEN_DEC_K[v];   /* 20 ms .. 400 ms */
    g->gen_dec_coef = expf(-1.0f / (tau * OMEGA_SR));
    g->gen_atk_inc  = 1.0f / (GEN_ATK_MS[v] * 0.001f * OMEGA_SR);
}

/* (Re)start the amp AHD from wherever it is (no jump = no click). */
static void gen_ahd_trigger(groove_state_t *g, float peak_scale, int dur) {
    int v = g->gen_voice;
    g->gen_stage = 0;
    g->gen_hold_n = (int)(GEN_HOLD_F[v] * (float)dur * (0.5f + g->gen_decay) * peak_scale);
}

static void gen_fire(groove_state_t *g, int step, unsigned di) {
    unsigned seed = g->gen_seed_raw * 7919u + 17u;
    int semi = g->gen_seq[step];
    if (g->gen_nrand > 0.0f && gen_rnd(seed, di, 2u) < g->gen_nrand)
        semi = gen_snap(g, semi + (int)(gen_rnd(seed, di, 3u) * 15.0f) - 7);
    if (g->gen_octjump > 0.0f && gen_rnd(seed, di, 4u) < g->gen_octjump * 0.7f)
        semi += (gen_rnd(seed, di, 5u) < 0.3f * g->gen_octjump) ? 24 : 12;
    int acc = g->gen_acc[step];
    float aa = g->gen_accent;
    g->gen_vel      = acc ? 1.0f : 1.0f - 0.55f * aa;
    g->gen_fenv_amt = acc ? 1.0f + 1.4f * aa : 1.0f - 0.45f * aa;
    /* Each voice plays in its own register (Bleep high, acid and stabs an octave up). */
    static const float MUL[GEN_V_COUNT] = { 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 1.0f, 1.0f, 4.0f, 2.0f };
    float f = g->gen_base_hz * MUL[g->gen_voice] * powf(2.0f, (float)semi / 12.0f);
    if (f > 8000.0f) f = 8000.0f;
    int acid = g->gen_voice == GEN_V_ACIDSAW || g->gen_voice == GEN_V_ACIDSQ;
    if (acid && g->gen_slide > 0.0f && g->gen_amp > 0.02f
        && gen_rnd(seed, di, 9u) < g->gen_slide * 0.8f) {
        /* SLIDE: tie into this note like a 303. The pitch glides, the gate holds,
         * and nothing retriggers (no new pluck, no new filter sweep). */
        g->gen_freq_tgt = f;
        if (g->gen_stage == 2) { g->gen_stage = 1; }       /* hold the gate through the tie */
        g->gen_hold_n += (int)(GEN_HOLD_F[g->gen_voice] * (float)g->gen_dur * (0.5f + g->gen_decay));
        return;
    }
    g->gen_freq = g->gen_freq_tgt = f;
    g->gen_aenv = 1.0f;
    gen_ahd_trigger(g, 1.0f, g->gen_dur);
    g->gen_filt_env = 1.0f;
    g->gen_filt_sus = g->gen_sus_lvl;
    g->gen_osc_phase = 0.0f;
    g->gen_sub_phase = 0.0f;
    g->gen_ph2 = 0.33f; g->gen_ph3 = 0.71f; g->gen_ph4 = 0.52f;
    g->gen_blip = g->gen_voice == GEN_V_BLEEP ? 1.0f : 0.0f;
}

/* Control-rate per-voice coefficients from the voice's own A/B knobs: Detune's second
 * oscillator ratio, the Stab chord, and the acid resonance. powf lives here, never per sample. */
static void gen_voice_coefs(groove_state_t *g) {
    /* v0.6.2: the stock resonance halved (it gave every voice the same whistle) */
    static const float KRES[GEN_V_COUNT] = { 0.31f, 0.31f, 0.28f, 0.0f, 0.0f, 0.4f, 0.4f, 0.41f, 0.35f };
    int v = g->gen_voice;
    g->gen_nosc = 1; g->gen_r2 = g->gen_r3 = g->gen_r4 = 1.0f;
    g->gen_kres = KRES[v];
    if (v == GEN_V_DETUNE) {
        /* first half: a smooth 0..-40 cent detune; second half snaps to 4th, 5th, sub octave */
        float b = g->gen_vb[v], st;
        if (b < 0.5f) st = -0.4f * (b / 0.5f);
        else st = b < 0.6667f ? -5.0f : (b < 0.8333f ? -7.0f : -12.0f);
        g->gen_r2 = powf(2.0f, st / 12.0f);
        g->gen_nosc = 2;
    } else if (v == GEN_V_STAB) {
        static const int CH[4][3] = { { 7, 12, 0 }, { 3, 7, 0 }, { 5, 7, 0 }, { 3, 7, 10 } };
        int c = (int)(g->gen_vb[v] + 0.5f); if (c < 0) c = 0; if (c > 3) c = 3;
        g->gen_r2 = powf(2.0f, CH[c][0] / 12.0f) * 1.0015f;   /* a hair of detune: thicker */
        g->gen_r3 = powf(2.0f, CH[c][1] / 12.0f) * 0.9985f;
        g->gen_r4 = powf(2.0f, CH[c][2] / 12.0f) * 1.001f;
        g->gen_nosc = c == 3 ? 4 : 3;
    } else if (v == GEN_V_ACIDSAW || v == GEN_V_ACIDSQ) {
        /* RES as a fraction of the self-oscillation point (see the filter in groove_tick) */
        g->gen_kres = 0.05f + 0.9f * g->gen_va[v];
    }
}

static void gen_set_voice(groove_state_t *g, int v) {
    if (v < 0) v = 0; if (v >= GEN_V_COUNT) v = GEN_V_COUNT - 1;
    g->gen_voice = v;
    /* body level (how long the note holds) and makeup gain per voice */
    static const float SUS[GEN_V_COUNT] = { 0.5f, 0.5f, 0.4f, 0.55f, 0.55f, 0.4f, 0.4f, 0.12f, 0.3f };
    static const float VG[GEN_V_COUNT]  = { 1.0f, 1.3f, 1.0f, 1.0f, 0.5f, 1.0f, 1.0f, 1.75f, 2.4f };
    g->gen_sus_lvl = SUS[v];
    g->gen_vgain = VG[v];
    /* v0.6.2 output trim: every voice lands at about the same loudness, ~6 dB
     * under a default kick so SEQ VOL 100% sits under the kick, not on top of it. */
    static const float TRIM[GEN_V_COUNT] = { 0.221f, 0.389f, 0.251f, 0.376f, 0.312f, 0.299f, 0.263f, 0.257f, 0.295f };
    g->gen_trim = TRIM[v];
    gen_voice_coefs(g);
    gen_ahd_coefs(g);
}

void groove_gen_restart(groove_state_t *g) {
    g->gen_step = 0;
    g->gen_step_ctr = 0;   /* fire step 0 on the next tick */
    g->gen_bar16 = 0;
    g->gen_sixteenth_acc = 0.0f;   /* v0.4.1: reset the bar accumulator too */
    g->gen_amp = 0.0f; g->gen_stage = 2; g->gen_hold_n = 0;
    g->gen_filt_env = 0.0f;
    g->gen_filt_sus = 0.0f;
    g->gen_dice_ctr = 0u;
    g->gen_rat_n = 0;
    g->gen_abs_step = -0x7fffffffL;   /* beat-locked: the next crossing always fires */
}

/* ---- groove_init: seed a valid tap interval + musical Page-1 middles ----- */
/* Called from create_instance (off the audio thread). The calloc already
 * zeroed everything; seed the tempo clock so the very FIRST render block has a
 * valid, div-by-zero-free samples_per_16th (NOTE in <tempo_algorithm>). */
void groove_init(groove_state_t *g) {
    g->write_pos      = 0u;
    g->prev_beat      = 0.0;
    g->have_prev_beat = false;

    g->bpm_smooth       = 120.0f;
    g->last_bpm         = 120.0f;
    g->samples_per_16th = (int)((60.0f / 120.0f) * OMEGA_SR / 4.0f + 0.5f);
    if (g->samples_per_16th < 1) g->samples_per_16th = 1;

    /* Fractional slewed tap spacing (TAPS): seed to the 120-BPM target. */
    g->spq_target = (60.0f / 120.0f) * OMEGA_SR / 4.0f;
    g->spq        = g->spq_target;

    /* Page-1 defaults. VOL starts at 0 so the groove rumble is SILENT on a bare
     * create and does not color the per-model kick voicing (the groove is an
     * opt-in performance voice the user opens via grv_vol; the FX voicing
     * battery renders each kick model in isolation). tap_level/decay/COLOR are
     * seeded to musical middles so the moment grv_vol is raised the rumble is
     * immediately shaped and audible (test_groove's prime_groove opens it). */
    g->vol = 0.0f;          /* TAPS voice: opt-in (silent until raised) */
    g->gen_vol = 0.0f;      /* GEN voice: opt-in, independent, summed (v0.4) */
    for (int t = 0; t < 4; t++) g->tap_level[t] = 0.6f;
    /* Makeup gain so the FIR rumble is level-competitive with the kick at a
     * musical default VOL without DRIVE (ONDEVICE #3 "way too quiet"). */
    g->rumble_makeup = 2.0f;
    groove_set_length(g, 0.5f);          /* seeds tap_tau_s + tap_w[] + tap_norm */
    g->color_g       = tpt_g_from_hz(8000.0f);
    g->color_lp_l_s  = 0.0f;
    g->color_lp_r_s  = 0.0f;
    g->color_lp2_l_s = 0.0f;
    g->color_lp2_r_s = 0.0f;
    /* TAPS HPF (v0.4.1): default OFF (hpf_g==0 sentinel bypasses the stage). */
    g->hpf_g         = 0.0f;
    g->hpf_lp_l_s    = 0.0f;
    g->hpf_lp_r_s    = 0.0f;
    g->mono          = false;

    /* Redesign defaults (Phase 1: feedback-free FIR). */
    g->type        = GROOVE_TYPE_TAPS;
    /* FX routing: default RUMBLE→DRIVE→REVERB (reverb last, classic); reverb MIX
     * off on a bare create. */
    g->route_order[0] = BLK_RUMBLE;
    g->route_order[1] = BLK_DRIVE;
    g->route_order[2] = BLK_REVERB;
    g->rv_mix      = 0.25f;        /* v0.6.2: return level; the sends decide what goes in */
    g->rv_s_rmb = 0.5f; g->rv_s_seq = 0.25f; g->rv_s_kick = 0.0f; g->rv_s_dly = 0.5f;
    g->rv_send = 0.0f;
    g->dly_send = 0.0f; g->dly_fb = 0.92f * 0.4f; g->dly_time = 3; g->dly_active = 0;
    g->dly_wi = 0u; g->dly_lp = g->dly_hp = 0.0f;
    g->dly_len = g->dly_len_tgt = g->spq_target * DLY_16THS[3];
    groove_dly_tone(g, 0.5f);

    /* Groove FX defaults (C1-02) — off/neutral (calloc already zeroed buffers). */
    g->drive       = 0.0f;
    g->filter_type = GRV_FILT_LP;
    g->lfo_phase   = 0.0f;
    g->lfo_inc     = 0.5f / OMEGA_SR;   /* ~0.5 Hz at LFO SPD default */
    g->lfo_amt     = 0.0f;
    g->rv_fb       = 0.6f + 0.375f * 0.5f;
    g->rv_damp     = expf(-2.0f * (float)M_PI * 4200.0f / OMEGA_SR);
    g->rv_damp_eff = g->rv_damp;
    g->rv_hp_k     = 1.0f - expf(-2.0f * (float)M_PI * 90.0f / OMEGA_SR);
    rv_apply_type(g, 0);                 /* Warehouse default */

    /* GEN groove voice defaults (C1-03 / E3). */
    g->gen_unquantized = false;
    g->gen_scale   = 1;            /* major by default (UI idx 2 = Chromatic→0, Major→1) */
    g->gen_root_param = 0.18f;     /* legacy (unused in v0.4) */
    g->gen_root_hz    = 45.0f;     /* ROOT (Hz) default — Unquantized mode */
    g->gen_root_note  = 30;        /* ROOT NOTE default MIDI 30 (F#0 ≈ 46 Hz) — Scale mode */
    g->gen_color_g    = tpt_g_from_hz(2500.0f);  /* GEN filter default (mid-open) */
    g->gen_range   = 12;           /* 12 degrees ≈ one octave of sequence variation */
    g->gen_seqlen  = 16;
    g->gen_wave    = 0;
    g->gen_wave_pos = 0.0f;        /* sine end of the scan */
    g->gen_retrig  = 0;            /* RESET None: free loop by default (v0.4.1) */
    g->gen_density = 0.6f;
    g->gen_rotate  = 0.0f;
    g->gen_decay   = 0.5f;         /* medium gen-note D/R time */
    /* v0.4.1 batch defaults. */
    g->gen_notelen      = 5;       /* 1/16 */
    g->gen_notelen_mult = gen_notelen_to_mult(5);
    g->gen_swing        = 0.0f;    /* center = no swing */
    g->gen_gentaps      = 0.0f;    /* GEN>TAPS send off */
    g->gen_sixteenth_acc = 0.0f;
    g->gen_fold    = 0.0f;
    g->gen_base_hz = 45.0f;        /* set from gen_root_param (unified log map) */
    g->gen_osc_phase = 0.0f;
    g->gen_sub_phase = 0.0f;       /* Finding 3: built-in sub-octave oscillator */
    g->gen_seed_raw = 12345u;
    g->gen_filt_env      = 0.0f;
    gen_set_decay(g, g->gen_decay);   /* seed gen_env_coef + gen_filt_env_coef (v0.4.1) */
    g->gen_filt_lp3_l_s  = 0.0f;
    g->gen_filt_lp3_r_s  = 0.0f;
    g->gen_running = false;
    g->gen_pattern = GEN_PAT_RANDOM;
    {   /* per-voice A/B defaults: keep in step with params.c */
        static const float DA[GEN_V_COUNT] = { 0.0f, 0.67f, 0.0f, 0.6f, 0.6f, 0.3f, 0.4f, 0.33f, 0.67f };
        static const float DB[GEN_V_COUNT] = { 0.0f, 0.25f, 0.4f, 0.6f, 0.6f, 0.4f, 0.4f, 0.5f, 0.0f };
        for (int i = 0; i < GEN_V_COUNT; i++) { g->gen_va[i] = DA[i]; g->gen_vb[i] = DB[i]; }
    }
    g->gen_noise = 0x9E3779B9u;
    g->gen_freq = g->gen_freq_tgt = 45.0f;
    g->tap_drv = g->gen_drv = 0.0f;
    gen_set_voice(g, 0);
    g->gen_amp = 0.0f; g->gen_stage = 2; g->gen_hold_n = 0; g->gen_dur = g->samples_per_16th;
    g->gen_vel = 1.0f;
    g->gen_fenv_amt = 1.0f;
    g->gen_accent = 0.0f; g->gen_octjump = 0.0f; g->gen_ratchet = 0.0f;
    g->gen_drop = 0.0f; g->gen_nrand = 0.0f; g->gen_loop = 0;
    groove_gen_rebuild(g);
    groove_gen_restart(g);
}

/* ---- groove_update_tempo (VERBATIM C-RESEARCH §Pattern 2, GRV-02) -------- */
/* Called ONCE per render block, before the per-sample loop. NEVER per sample.
 * Derives BPM from the guarded host transport chain, smooths jitter, and
 * recomputes the 16th-note tap interval ONLY when BPM actually moves. */
void groove_update_tempo(groove_state_t *g, const struct host_api_v1 *host_fwd,
                         int frames) {
    const host_api_v1_t *host = (const host_api_v1_t *)host_fwd;
    float bpm = 0.0f, derived = 0.0f;
    bool running = false;   /* GRVX-05: GEN sequencer stops when transport stops */
    bool clock_running = false, have_beat = false;
    double beat = -1.0;

    /* (1) Host beat position (24-PPQN, interpolated per block by the host). */
    if (host && host->get_beat_position) {
        beat = host->get_beat_position();
        have_beat = beat >= 0.0;
        if (have_beat) {
            if (g->have_prev_beat) {
                double dbeat = beat - g->prev_beat;
                if (dbeat > 0.0 && dbeat < 4.0) {   /* sane per-block advance */
                    derived = (float)(dbeat * 60.0 * (double)OMEGA_SR / (double)frames);
                    running = true;                 /* beat advanced => transport live */
                }
            }
            g->prev_beat = beat;
            g->have_prev_beat = true;
        } else {
            g->have_prev_beat = false;              /* stopped: drop stale prev */
        }
    }
    /* The clock status (when the host has it) says "running" on the very first
     * block, before the beat has had a chance to advance. */
    if (have_beat && host->get_clock_status && host->get_clock_status() == 2 /* RUNNING */)
        clock_running = true;

    /* (2) Tempo: the host's own BPM is exact; the beat delta is only a fallback
     * (it jitters with block-quantised beat reports). */
    if (host && host->get_bpm) {
        float b = host->get_bpm();
        if (b >= 20.0f && b <= 999.0f) bpm = b;
    }
    if (bpm <= 0.0f) bpm = derived;

    /* (3) LAST RESORT: the last tempo, else 120 — ONLY when no transport info at all. */
    if (bpm <= 0.0f) bpm = g->last_bpm > 0.0f ? g->last_bpm : 120.0f;

    /* Smooth jitter, then re-lock when the tempo really moved. The threshold is
     * small (v0.6.1): the old 0.5 BPM dead-band let the clock settle up to half a
     * BPM off the real tempo, so GEN slid behind the kick by 2-5 ms every bar. */
    bpm = clampf(bpm, 20.0f, 300.0f);
    g->bpm_smooth += (bpm - g->bpm_smooth) * 0.20f;         /* one-pole EMA */
    if (fabsf(bpm - g->bpm_smooth) < 0.005f) g->bpm_smooth = bpm;   /* land exactly */
    if (fabsf(g->bpm_smooth - g->last_bpm) > 0.004f) {      /* control-rate re-lock */
        g->last_bpm = g->bpm_smooth;
        int spq = (int)((60.0f / g->bpm_smooth) * OMEGA_SR / 4.0f + 0.5f);  /* samples_per_16th */
        if (spq < 1) spq = 1;
        if (spq * 4 > (int)(GRV_DELAY_MASK)) spq = (int)(GRV_DELAY_MASK) / 4;  /* clamp reach */
        g->samples_per_16th = spq;                          /* integer clock (GEN) */

        /* TAPS: set the FLOAT slew target (no rounding); groove_tick glides
         * g->spq toward it so live BPM changes do not click (vhr §A.6). Clamp
         * the reach so 4*spq stays inside the ring. */
        float fspq = (60.0f / g->bpm_smooth) * OMEGA_SR / 4.0f;
        if (fspq < 1.0f) fspq = 1.0f;
        float fmaxspq = (float)((int)(GRV_DELAY_MASK) / 4);
        if (fspq > fmaxspq) fspq = fmaxspq;
        g->spq_target = fspq;
        /* FIR tap ages depend on spq_target → recompute the decay weights at the
         * new tempo (control rate, once per re-lock; never per sample). */
        groove_update_tap_weights(g);
    }

    /* Beat lock: snap GEN's beat to the host's every block and advance it per
     * sample at the host tempo, so GEN steps land on the same 16ths as the kick. */
    if (have_beat && (running || clock_running)) {
        g->gen_locked = 1;
        g->gen_beat = beat;
        g->gen_bpf = (double)bpm / (60.0 * (double)OMEGA_SR);
        running = true;
    } else {
        if (g->gen_locked) g->gen_abs_step = -0x7fffffffL;   /* re-fire on the next start */
        g->gen_locked = 0;
    }

    /* If the host exposes NO transport at all, treat the GEN sequencer as free-
     * running (so it plays under the null-transport fallback); otherwise gate it
     * on real beat advance so it STOPS when the transport stops (GRVX-05). */
    if (!host || (!host->get_beat_position && !host->get_bpm)) running = true;
    g->gen_running = running;
}

/* Wave morph for the GEN voices: sine -> triangle -> saw -> square across 0..1. */
static inline float gen_wav4(float pos, float ph) {
    float wp = pos * 3.0f;
    int oi = (int)wp; if (oi < 0) oi = 0; if (oi > 2) oi = 2;
    float wf = wp - (float)oi;
    float a = wt_read_bl(oi, 0, ph);
    return a + wf * (wt_read_bl(oi + 1, 0, ph) - a);
}

/* ~35 ms glide time constant for SLIDE (1 - e^(-1/(0.035 s * SR))). */
#define GEN_GLIDE_K 0.000648f

/* Warm saturation used by RUMBLE / SEQ volume past noon: a rational tanh, pre-gain
 * 1..(1+maxg) with d^2 so the first part of the turn is gentle, and makeup so a
 * typical level (0.5) comes out where it went in: the knob changes tone, not volume. */
static inline float sat_tanh(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
#define DRV_REF 0.25f
static inline float drive_stage_r(float x, float d, float maxg, float ref) {
    float gn = 1.0f + maxg * d * d;
    return sat_tanh(x * gn) * (ref / sat_tanh(ref * gn));
}
static inline float drive_stage(float x, float d, float maxg) { return drive_stage_r(x, d, maxg, DRV_REF); }
#ifndef GEN_DRV_REF
#define GEN_DRV_REF 0.35f   /* v0.6.2: ~even loudness with the AHD body */
#endif

/* Play one GEN step: gate, DROP dice, the note, then maybe a RATCHET. */
static void gen_step_fire(groove_state_t *g, int step, unsigned di, int dur) {
    unsigned seed = g->gen_seed_raw * 7919u + 17u;
    g->gen_cur_step = step;
    g->gen_rat_n = 0;
    g->gen_dur = dur;
    if (g->gen_gate[step]
        && !(g->gen_drop > 0.0f && gen_rnd(seed, di, 1u) < g->gen_drop * 0.9f)) {
        gen_fire(g, step, di);
        if (g->gen_ratchet > 0.0f && gen_rnd(seed, di, 6u) < g->gen_ratchet * 0.8f) {
            int n = gen_rnd(seed, di, 7u) < 0.6f ? 2 : 3;
            g->gen_rat_n = n - 1;
            g->gen_rat_len = dur / n > 1 ? dur / n : 1;
            g->gen_rat_ctr = g->gen_rat_len;
        }
    }
}

/* ---- groove_tick (VERBATIM C-RESEARCH §Pattern 1 + §Pattern 3) ----------- */
/* Per sample. kick_l/kick_r are this sample's kick output. Writes the kick into
 * the ring, reads four mask-wrapped 16th-note taps behind the write head, then
 * applies the COLOR lowpass + MONO force-sum + VOL. NO transcendental here. */
void groove_tick(groove_state_t *g, float kick_l, float kick_r,
                 float *out_gl, float *out_gr) {
    /* ======================= GEN voice (v0.4) =========================
     * GEN and TAPS now run SIMULTANEOUSLY (TYPE selector removed) and are summed
     * with independent volumes. The GEN sequencer + wavetable osc is unchanged; it
     * no longer owns the ring and has its OWN 3-pole resonant filter state
     * (gen_filt_lp1/2/3 + gen_color_g) so it never fights the TAPS COLOR filter. */
    float gen_l = 0.0f, gen_r = 0.0f;
    {
        if (g->gen_running) {
            float mult = g->gen_notelen_mult > 0.0f ? g->gen_notelen_mult : 1.0f;
            int len = g->gen_seqlen < 1 ? 1 : (g->gen_seqlen > 64 ? 64 : g->gen_seqlen);
            int bars = (g->gen_retrig >= 1 && g->gen_retrig <= 8) ? g->gen_retrig : 0;
            if (g->gen_locked) {
                /* ---- beat-locked: steps are the host beat's NOTE-LEN crossings ---- */
                double u = g->gen_beat * 4.0 / (double)mult;   /* position in steps */
                g->gen_beat += g->gen_bpf;
                long st = (long)floor(u);
                double sw = (double)g->gen_swing;              /* odd steps start at n+sw */
                if (sw > 0.0 && (st & 1) && u - (double)st < sw) st--;
                else if (sw < 0.0 && !(st & 1) && (double)(st + 1) - u <= -sw) st++;
                if (g->gen_restart_pending) {
                    /* On Note / restart: step 0 belongs to the nearest step boundary.
                     * If that boundary already fired (the note came a hair late), fire
                     * it again now as step 0. */
                    long near = (long)floor(u + 0.5);
                    g->gen_origin = near;
                    if (near <= g->gen_abs_step) g->gen_abs_step = near - 1;
                    g->gen_restart_pending = 0;
                }
                if (st != g->gen_abs_step && (st > g->gen_abs_step || g->gen_abs_step - st > 1)) {
                    g->gen_abs_step = st;
                    /* Step 0 sits on beat 0 (the bar line) unless RESET = On Note
                     * re-anchored it to the last kick. */
                    long local = st - (g->gen_retrig == GRV_RETRIG_ONNOTE ? g->gen_origin : 0);
                    if (bars) {   /* RESET n bars: restart the pattern on real bar lines */
                        double seg = 16.0 * (double)bars;
                        double s16 = (double)st * (double)mult;
                        long seg_start = (long)ceil(floor(s16 / seg) * seg / (double)mult - 1e-6);
                        local = st - seg_start;
                    }
                    int step = (int)(((local % len) + len) % len);
                    int dur = (int)((float)g->samples_per_16th * mult
                                    * ((st & 1) ? (1.0f - g->gen_swing) : (1.0f + g->gen_swing)) + 0.5f);
                    if (dur < 1) dur = 1;
                    unsigned lp = g->gen_loop > 0 ? (unsigned)g->gen_loop : 0u;
                    unsigned long ul = (unsigned long)(local < 0 ? -local : local);
                    unsigned di = lp ? (unsigned)(ul % lp) : (unsigned)ul;
                    gen_step_fire(g, step, di, dur);
                    g->gen_step = step + 1;
                }
            } else {
                /* ---- free-running (host gives no beat position) ---- */
                g->gen_restart_pending = 0;
                if (g->gen_step_ctr <= 0) {
                    int step = g->gen_step % len;         /* pattern ALWAYS loops (item 4) */
                    float sw = (g->gen_step & 1) ? (1.0f - g->gen_swing) : (1.0f + g->gen_swing);
                    int dur = (int)((float)g->samples_per_16th * mult * sw + 0.5f);
                    if (dur < 1) dur = 1;
                    /* dice index: LOOP=N repeats the same variation every N steps */
                    unsigned di = g->gen_loop > 0 ? g->gen_dice_ctr % (unsigned)g->gen_loop : g->gen_dice_ctr;
                    gen_step_fire(g, step, di, dur);
                    g->gen_dice_ctr++;
                    g->gen_step_ctr = dur;
                    g->gen_sixteenth_acc += mult;        /* 16ths elapsed (bar-based RESET) */
                    g->gen_step++;
                    if (bars && g->gen_sixteenth_acc >= (float)(16 * bars) - 1e-3f) {
                        g->gen_step = 0;
                        g->gen_sixteenth_acc = 0.0f;
                    }
                }
                g->gen_step_ctr--;
            }
            /* ratchet: re-strike the same note inside the step, a little softer */
            if (g->gen_rat_n > 0 && --g->gen_rat_ctr <= 0) {
                g->gen_rat_n--;
                g->gen_rat_ctr = g->gen_rat_len;
                g->gen_vel *= 0.88f;
                gen_ahd_trigger(g, 1.0f, g->gen_rat_len);
                g->gen_filt_env = 0.85f; g->gen_filt_sus = g->gen_sus_lvl;
                if (g->gen_voice == 1) g->gen_blip = 0.6f;
            }
        }
        /* ---- the voice: oscillators from the voice's own A/B knobs ---- */
        const int vc = g->gen_voice;
        const float A = g->gen_va[vc], B = g->gen_vb[vc];
        const float sl = g->gen_sus_lvl;
        const float fe = g->gen_filt_env * (1.0f - sl) + g->gen_filt_sus;   /* pluck+body 0..1 */
        const float ph = g->gen_osc_phase;
        float osc, nfm = 0.0f;
        switch (vc) {
        case GEN_V_DETUNE:                    /* two oscillators, B pulls the second down */
            osc = 0.62f * (gen_wav4(A, ph) + gen_wav4(A, g->gen_ph2));
            break;
        case GEN_V_FM: {                      /* 1:1 sine modulator, depth follows the note */
            float p = ph + B * B * 1.2f * (0.35f + 0.65f * fe) * wt_read_bl(0, 0, ph);
            osc = gen_wav4(A, p - floorf(p));
            break;
        }
        case GEN_V_ACIDSAW: osc = wt_read_bl(2, 0, ph); break;
        case GEN_V_ACIDSQ:  osc = wt_read_bl(3, 0, ph); break;
        case GEN_V_DIRT: case GEN_V_DIRT2: {  /* triangle + noise; noise also shakes the cutoff */
            unsigned x = g->gen_noise; x ^= x << 13; x ^= x >> 17; x ^= x << 5; g->gen_noise = x;
            float n = (float)(int)x * (1.0f / 2147483648.0f);
            g->gen_nlp += 0.045f * (n - g->gen_nlp);          /* ~320 Hz: high-pass the audible noise */
            float p = ph;
            if (vc == GEN_V_DIRT2) {
                p += A * A * 1.2f * (0.35f + 0.65f * fe) * wt_read_bl(0, 0, ph);
                p -= floorf(p);
            }
            osc = wt_read_bl(1, 0, p);
            if (vc == GEN_V_DIRT) osc += A * 0.9f * (n - g->gen_nlp);
            nfm = B * n;
            break;
        }
        case GEN_V_STAB: {                    /* root + chord tones (B picks the chord) */
            float c = gen_wav4(A, ph) + 0.8f * gen_wav4(A, g->gen_ph2) + 0.7f * gen_wav4(A, g->gen_ph3);
            if (g->gen_nosc > 3) c += 0.6f * gen_wav4(A, g->gen_ph4);
            osc = c * (g->gen_nosc > 3 ? 0.4f : 0.5f);
            break;
        }
        case GEN_V_BASIC: case GEN_V_BLEEP: default:
            osc = gen_wav4(A, ph);
            break;
        }
        if (vc == GEN_V_BASIC && B > 0.0f) {  /* FOLD */
            float v = osc * (1.0f + B * 3.0f);
            v = v - 4.0f * floorf(v * 0.25f + 0.5f);   /* fold into ~[-2,2] */
            osc = osc + B * (0.5f * v - osc);
        }
        /* v0.6.2 amp AHD: short attack from the current level, hold, then decay. */
        if (g->gen_stage == 0) {
            g->gen_amp += g->gen_atk_inc;
            if (g->gen_amp >= 1.0f) { g->gen_amp = 1.0f; g->gen_stage = 1; }
        } else if (g->gen_stage == 1) {
            if (--g->gen_hold_n <= 0) g->gen_stage = 2;
        } else {
            g->gen_amp *= g->gen_dec_coef;
        }
        float amp = g->gen_amp * g->gen_vel * g->gen_vgain;
        float s = osc * amp;
        const int acid = vc == GEN_V_ACIDSAW || vc == GEN_V_ACIDSQ;
        float filt_env = (acid ? g->gen_aenv : fe) * g->gen_fenv_amt;
        g->gen_filt_env *= g->gen_filt_env_coef;
        g->gen_filt_sus *= g->gen_filt_sus_coef;
        g->gen_aenv *= g->gen_aenv_coef;
        g->gen_freq += (g->gen_freq_tgt - g->gen_freq) * GEN_GLIDE_K;   /* SLIDE glide */
        float fq = g->gen_freq;
        if (vc == GEN_V_BLEEP && g->gen_blip > 1e-4f) {
            fq *= 1.0f + 1.2f * B * g->gen_blip; g->gen_blip *= 0.9965f;
        }
        float inc = fq / OMEGA_SR;
        g->gen_osc_phase += inc; if (g->gen_osc_phase >= 1.0f) g->gen_osc_phase -= 1.0f;
        if (g->gen_nosc > 1) { g->gen_ph2 += inc * g->gen_r2; if (g->gen_ph2 >= 1.0f) g->gen_ph2 -= 1.0f; }
        if (g->gen_nosc > 2) { g->gen_ph3 += inc * g->gen_r3; if (g->gen_ph3 >= 1.0f) g->gen_ph3 -= 1.0f; }
        if (g->gen_nosc > 3) { g->gen_ph4 += inc * g->gen_r4; if (g->gen_ph4 >= 1.0f) g->gen_ph4 -= 1.0f; }

        /* SEQ volume past noon: drive into the filter (it makes the resonance bite) */
        if (g->gen_drv > 0.0f) s = drive_stage_r(s, g->gen_drv, 14.0f, GEN_DRV_REF);

        /* GEN 3-pole (18 dB/oct) RESONANT filter, driven by the filter env. Acid voices
         * use their own ENV AMT (B) over a long 303-ish sweep; Dirt adds noise FM. */
        float cg_eff = g->gen_color_g * (acid ? 1.0f + 20.0f * B * B * filt_env
                                              : 1.0f + 1.8f * filt_env);   /* v0.6.2: half the old depth */
        if (nfm != 0.0f) cg_eff *= 1.0f + 1.6f * nfm;
        if (cg_eff < 1e-4f) cg_eff = 1e-4f; else if (cg_eff > 0.99f) cg_eff = 0.99f;
        /* Acid RES rides the cutoff: this filter starts to self-oscillate at about
         * k = 7.6 / (1 + 3g), so k is scaled to stay under that as the env sweeps. */
        const float kres = acid ? g->gen_kres * 7.6f / (1.0f + 3.0f * cg_eff) : g->gen_kres;
        if (acid) s *= 1.0f + 0.55f * kres;       /* resonance eats the passband: give it back */
        float in = clampf(s - kres * g->gen_filt_lp3_l_s, -2.0f, 2.0f);
        tpt1_t l1 = { g->gen_filt_lp1_l_s }, l2 = { g->gen_filt_lp2_l_s }, l3 = { g->gen_filt_lp3_l_s };
        float f1 = tpt1_lp(&l1, in, cg_eff);
        float f2 = tpt1_lp(&l2, f1, cg_eff);
        float lo = tpt1_lp(&l3, f2, cg_eff);
        g->gen_filt_lp1_l_s = l1.s; g->gen_filt_lp2_l_s = l2.s; g->gen_filt_lp3_l_s = l3.s;
        if (g->gen_drv > 0.0f) lo = drive_stage_r(lo, g->gen_drv, 4.0f, GEN_DRV_REF);   /* and a little after */
        lo *= g->gen_trim;         /* v0.6.2 level match per voice, kick-relative staging */
        gen_l = gen_r = lo;        /* GEN is mono */
    }

    /* ======================= TAPS voice (v0.4) ========================
     * FIR multitap rumble fed by the live kick: write the raw kick into the ring,
     * sum NTAPS decay-weighted ghost copies, then the 2-pole COLOR filter (LFO-
     * modulated, LP/HP/Off). Own state (color_lp*). */
    float taps_l = 0.0f, taps_r = 0.0f;
    {
        g->spq += (g->spq_target - g->spq) * SPQ_SLEW;
        float spq = g->spq;
        if (spq < 1.0f) spq = 1.0f;
        float fmaxspq = (float)((int)(GRV_DELAY_MASK) / NTAPS);
        if (spq > fmaxspq) spq = fmaxspq;
        float acc_l = 0.0f, acc_r = 0.0f;
        for (int k = 1; k <= NTAPS; k++) {
            float dk  = (float)k * spq;
            float rpk = (float)g->write_pos - dk;
            float fk  = rpk - floorf(rpk);
            int   k0  = ((int)floorf(rpk)) & GRV_DELAY_MASK;
            int   k1  = (k0 + 1) & GRV_DELAY_MASK;
            float sl  = g->buf_l[k0] + fk * (g->buf_l[k1] - g->buf_l[k0]);
            float sr  = g->buf_r[k0] + fk * (g->buf_r[k1] - g->buf_r[k0]);
            acc_l += sl * g->tap_w[k - 1];
            acc_r += sr * g->tap_w[k - 1];
        }
        /* Ring input = live kick + GEN>TAPS send (item 10): routing GEN into the taps
         * ring makes the rumble/reverb act as a delay/echo on the GEN voice. */
        g->buf_l[g->write_pos] = kick_l + gen_l * g->gen_gentaps;
        g->buf_r[g->write_pos] = kick_r + gen_r * g->gen_gentaps;
        g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;
        taps_l = acc_l * g->tap_norm;
        taps_r = acc_r * g->tap_norm;

        /* TAPS HPF (item 12): one-pole highpass BEFORE the COLOR lowpass (hp = x - lp).
         * Bypassed when hpf_g==0 (control at minimum) so "off" is bit-transparent. */
        if (g->hpf_g > 0.0f) {
            tpt1_t hl = { g->hpf_lp_l_s }, hr = { g->hpf_lp_r_s };
            float lol = tpt1_lp(&hl, taps_l, g->hpf_g);
            float lor = tpt1_lp(&hr, taps_r, g->hpf_g);
            g->hpf_lp_l_s = hl.s; g->hpf_lp_r_s = hr.s;
            taps_l -= lol; taps_r -= lor;
        }

        /* RUMBLE volume past noon: drive into the COLOR filter (more after it, below) */
        if (g->tap_drv > 0.0f) {
            taps_l = drive_stage(taps_l, g->tap_drv, 14.0f);
            taps_r = drive_stage(taps_r, g->tap_drv, 14.0f);
        }

        /* LFO sweeps the TAPS cutoff + reverb damping (reverb reads rv_damp_eff). */
        float cg = g->color_g;
        g->rv_damp_eff = g->rv_damp;
        if (g->lfo_amt > 0.0f) {
            float ph  = g->lfo_phase;
            float tri = (ph < 0.5f) ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph);
            cg = g->color_g * (1.0f + 0.9f * g->lfo_amt * tri);
            if (cg < 1e-4f) cg = 1e-4f; else if (cg > 0.99f) cg = 0.99f;
            float dm = g->rv_damp + 0.4f * g->lfo_amt * tri;
            g->rv_damp_eff = dm < 0.0f ? 0.0f : (dm > 0.95f ? 0.95f : dm);
        }
        if (g->filter_type == GRV_FILT_OFF) {
            tpt1_t lpl = { g->color_lp_l_s }, lpr = { g->color_lp_r_s };
            (void)tpt1_lp(&lpl, taps_l, cg); (void)tpt1_lp(&lpr, taps_r, cg);
            g->color_lp_l_s = lpl.s; g->color_lp_r_s = lpr.s;
            tpt1_t l2 = { g->color_lp2_l_s }, r2 = { g->color_lp2_r_s };
            (void)tpt1_lp(&l2, taps_l, cg); (void)tpt1_lp(&r2, taps_r, cg);
            g->color_lp2_l_s = l2.s; g->color_lp2_r_s = r2.s;
        } else {
            tpt1_t lpl = { g->color_lp_l_s }, lpr = { g->color_lp_r_s };
            float s1l = tpt1_lp(&lpl, taps_l, cg);
            float s1r = tpt1_lp(&lpr, taps_r, cg);
            g->color_lp_l_s = lpl.s; g->color_lp_r_s = lpr.s;
            tpt1_t l2 = { g->color_lp2_l_s }, r2 = { g->color_lp2_r_s };
            float lo_l = tpt1_lp(&l2, s1l, cg);
            float lo_r = tpt1_lp(&r2, s1r, cg);
            g->color_lp2_l_s = l2.s; g->color_lp2_r_s = r2.s;
            if (g->filter_type == GRV_FILT_LP) { taps_l = lo_l; taps_r = lo_r; }
            else { taps_l = taps_l - lo_l; taps_r = taps_r - lo_r; }   /* HP */
        }
    }

    if (g->tap_drv > 0.0f) {
        taps_l = drive_stage(taps_l, g->tap_drv, 4.0f);
        taps_r = drive_stage(taps_r, g->tap_drv, 4.0f);
    }

    /* ============================ Mix ================================
     * Independent per-voice volumes (both default 0 — opt-in). The shared FX tail
     * (drive/reverb route + mono below) processes the SUM. */
    /* v0.6.2 gain staging: the rumble at RMB VOL 100% sits ~5 dB under the kick
     * (it was ~5 dB over it and peaked at full scale); -4.4 dB. */
#define RMB_TRIM 0.6f
    const float rv = g->vol * RMB_TRIM;
    float sq_l = gen_l * g->gen_vol, sq_r = gen_r * g->gen_vol;
    float gl = taps_l * rv + sq_l;
    float gr = taps_r * rv + sq_r;

    /* v0.6.2 SEQ delay: tempo-synced mono echo of the sequence (post SEQ VOL). */
    float dout = 0.0f;
    if (g->dly_send > 0.0f || g->dly_active > 0) {
        g->dly_len_tgt = g->spq_target * DLY_16THS[g->dly_time];
        if (g->dly_len_tgt > (float)(GRV_DLY_LEN - 4)) g->dly_len_tgt = (float)(GRV_DLY_LEN - 4);
        g->dly_len += (g->dly_len_tgt - g->dly_len) * 0.0004f;     /* tape-ish glide on change */
        float rp = (float)g->dly_wi - g->dly_len;
        float fl = floorf(rp), fr = rp - fl;
        unsigned i0 = ((unsigned)(int)fl) & GRV_DLY_MASK, i1 = (i0 + 1u) & GRV_DLY_MASK;
        float y = g->dly_buf[i0] + fr * (g->dly_buf[i1] - g->dly_buf[i0]);
        g->dly_lp += g->dly_lp_a * (y - g->dly_lp);                 /* tone, in the loop */
        g->dly_hp += g->dly_hp_a * (g->dly_lp - g->dly_hp);
        dout = g->dly_lp - g->dly_hp;
        float w = 0.5f * (sq_l + sq_r) * g->dly_send + sat_tanh(dout * g->dly_fb);
        g->dly_buf[g->dly_wi & GRV_DLY_MASK] = w;
        g->dly_wi++;
        /* keep running ~until the repeats have died out after the send closes */
        if (g->dly_send > 0.0f || fabsf(w) > 1e-5f) g->dly_active = 2 * GRV_DLY_LEN;
        else if (g->dly_active > 0) g->dly_active--;
        gl += dout; gr += dout;
    }
    /* v0.6.2 reverb send bus: each source has its own send into the shared room */
    g->rv_send = 0.5f * ((taps_l + taps_r) * rv * g->rv_s_rmb + (sq_l + sq_r) * g->rv_s_seq
                         + (kick_l + kick_r) * g->rv_s_kick) + dout * g->rv_s_dly;

    /* FX ROUTING (Phase 1): apply DRIVE (+ its LFO tremolo) and the plain dry/wet
     * REVERB in the user-selected order. The rumble/GEN source already produced
     * gl/gr above (BLK_RUMBLE is a no-op slot whose POSITION decides whether DRIVE
     * or REVERB comes first). The reverb NEVER writes to the delay ring. Dispatch
     * is a branch-predictable switch over a 3-byte control-rate order table — no
     * per-sample transcendental. */
    for (int i = 0; i < 3; i++) {
        switch (g->route_order[i]) {
            case BLK_DRIVE:  groove_drive_block(g, &gl, &gr);  break;
            case BLK_REVERB: groove_reverb_block(g, &gl, &gr); break;
            case BLK_RUMBLE: default: break;   /* source slot (already produced) */
        }
    }
    /* Advance the tremolo LFO once per sample (order-independent rate). */
    g->lfo_phase += g->lfo_inc;
    if (g->lfo_phase >= 1.0f) g->lfo_phase -= 1.0f;

    if (g->mono) { float m = 0.5f * (gl + gr); gl = gr = m; }   /* GRV-05 sub-bass mono sum */
    /* v0.4: per-voice VOL already applied in the mix above (no final master VOL). */
    *out_gl = gl; *out_gr = gr;
}

/* The voices' own knobs: key -> (voice, A or B). Stab's CHORD is a RAW enum. */
static int gen_voice_key(groove_state_t *g, const char *key, const char *val, float v) {
    static const struct { const char *k; unsigned char voice, b; } K[] = {
        { PK_GRV_GWAVE, GEN_V_BASIC, 0 },     { PK_GRV_GFOLD, GEN_V_BASIC, 1 },
        { PK_GRV_GDTWAVE, GEN_V_DETUNE, 0 },  { PK_GRV_GDETUNE, GEN_V_DETUNE, 1 },
        { PK_GRV_GFMWAVE, GEN_V_FM, 0 },      { PK_GRV_GFMAMT, GEN_V_FM, 1 },
        { PK_GRV_GASRES, GEN_V_ACIDSAW, 0 },  { PK_GRV_GASENV, GEN_V_ACIDSAW, 1 },
        { PK_GRV_GAQRES, GEN_V_ACIDSQ, 0 },   { PK_GRV_GAQENV, GEN_V_ACIDSQ, 1 },
        { PK_GRV_GD1NOISE, GEN_V_DIRT, 0 },   { PK_GRV_GD1NFM, GEN_V_DIRT, 1 },
        { PK_GRV_GD2FM, GEN_V_DIRT2, 0 },     { PK_GRV_GD2NFM, GEN_V_DIRT2, 1 },
        { PK_GRV_GBLWAVE, GEN_V_BLEEP, 0 },   { PK_GRV_GBLIP, GEN_V_BLEEP, 1 },
        { PK_GRV_GSTWAVE, GEN_V_STAB, 0 },    { PK_GRV_GCHORD, GEN_V_STAB, 1 },
    };
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
        if (strcmp(key, K[i].k) != 0) continue;
        float x = v;
        if (K[i].voice == GEN_V_STAB && K[i].b) {
            int c = (int)(parse_f(val) + 0.5f); x = (float)(c < 0 ? 0 : (c > 3 ? 3 : c));
        }
        if (K[i].b) g->gen_vb[K[i].voice] = x; else g->gen_va[K[i].voice] = x;
        if (K[i].voice == GEN_V_BASIC) { if (K[i].b) g->gen_fold = x; else g->gen_wave_pos = x; }
        if (K[i].voice == g->gen_voice) gen_voice_coefs(g);
        return 1;
    }
    return 0;
}

/* ---- groove_set_param (control-rate Page-1 mappings, GRV-03/05) ---------- */
/* All powf/tanf run HERE, never in groove_tick. Unknown keys are ignored. */
void groove_set_param(groove_state_t *g, const char *key, const char *val) {
    if (!key) return;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_GRV_TYPE) == 0) {
        int nt = (v >= 0.5f) ? GROOVE_TYPE_GEN : GROOVE_TYPE_TAPS;
        if (nt != g->type && nt == GROOVE_TYPE_GEN) groove_gen_restart(g);
        g->type = nt;
    } else if (strcmp(key, PK_GRV_VOL) == 0 || strcmp(key, PK_GRV_GENVOL) == 0) {
        /* RUMBLE / SEQ volume 0..2 (RAW): up to noon (1.0) plain level, past noon a
         * gain-compensated drive before and after the voice's filter. Old sets stored
         * 0..1, which still means exactly the level it did. */
        float r = clampf(parse_f(val), 0.0f, 2.0f);
        float lvl = r < 1.0f ? r : 1.0f, drv = r > 1.0f ? r - 1.0f : 0.0f;
        if (strcmp(key, PK_GRV_VOL) == 0) { g->vol = lvl; g->tap_drv = drv; }
        else { g->gen_vol = lvl; g->gen_drv = drv; }                /* grv_genvol */
    } else if (strcmp(key, PK_GRV_GENFILT) == 0) {
        /* GEN's own filter cutoff (30 Hz..20 kHz log) — separate from TAPS COLOR. */
        float fc = 30.0f * powf(20000.0f / 30.0f, v);
        g->gen_color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_GRV_LENGTH) == 0) {
        groove_set_length(g, v);
    } else if (strcmp(key, PK_GRV_COLOR) == 0) {
        /* 30 Hz..20 kHz log sweep; always-LP (type selector removed in E2). */
        float fc = 30.0f * powf(20000.0f / 30.0f, v);
        g->color_g = tpt_g_from_hz(fc);
        g->filter_type = GRV_FILT_LP;
    } else if (strcmp(key, PK_GRV_TAP1) == 0) {
        g->tap_level[0] = v; groove_update_tap_weights(g);
    } else if (strcmp(key, PK_GRV_TAP2) == 0) {
        g->tap_level[1] = v; groove_update_tap_weights(g);
    } else if (strcmp(key, PK_GRV_TAP3) == 0) {
        g->tap_level[2] = v; groove_update_tap_weights(g);
    } else if (strcmp(key, PK_GRV_TAP4) == 0) {
        g->tap_level[3] = v; groove_update_tap_weights(g);
    } else if (strcmp(key, PK_GRV_MONO) == 0) {
        g->mono = (v >= 0.5f);
    } else if (strcmp(key, PK_GRV_DRIVE) == 0) {
        g->drive = v;
    } else if (strcmp(key, PK_GRV_FILTYPE) == 0) {
        int t = (int)(parse_f(val) + 0.5f);
        g->filter_type = (t < 0) ? 0 : (t > 2 ? 2 : t);
    } else if (strcmp(key, PK_GRV_LFOSPD) == 0) {
        /* 0.05 .. 8 Hz exp-ish LFO rate. */
        float hz = 0.05f + v * v * 8.0f;
        g->lfo_inc = hz / OMEGA_SR;
    } else if (strcmp(key, PK_GRV_LFOAMT) == 0) {
        g->lfo_amt = v;
    } else if (strcmp(key, PK_GRV_RVMIX) == 0) {
        /* Phase 1: plain dry/wet reverb MIX. 0 = dry (off), 1 = full wet. The
         * reverb runs as an in-line block placed by PK_GRV_ROUTE; it NEVER feeds
         * the delay ring (the old bidirectional PRE/POST hack is gone). */
        g->rv_mix = v;
    } else if (strcmp(key, PK_GRV_ROUTE) == 0) {
        /* FX routing order: permutation of {RUMBLE,DRIVE,REVERB}. BLK_RUMBLE marks
         * the source slot; its position decides whether DRIVE or REVERB comes
         * first. Parse the integer enum index (like PK_GRV_RVTYPE/GWAVE). */
        int idx = (int)(parse_f(val) + 0.5f);
        static const unsigned char orders[4][3] = {
            { BLK_RUMBLE, BLK_DRIVE,  BLK_REVERB }, /* 0 Rumble>Drive>Reverb (default) */
            { BLK_RUMBLE, BLK_REVERB, BLK_DRIVE  }, /* 1 Rumble>Reverb>Drive */
            { BLK_REVERB, BLK_RUMBLE, BLK_DRIVE  }, /* 2 Reverb first */
            { BLK_DRIVE,  BLK_RUMBLE, BLK_REVERB }, /* 3 Drive first */
        };
        if (idx < 0) idx = 0; if (idx > 3) idx = 3;
        for (int i = 0; i < 3; i++) g->route_order[i] = orders[idx][i];
    } else if (strcmp(key, PK_GRV_RVRMB) == 0) {  g->rv_s_rmb = v;
    } else if (strcmp(key, PK_GRV_RVSEQ) == 0) {  g->rv_s_seq = v;
    } else if (strcmp(key, PK_GRV_RVKICK) == 0) { g->rv_s_kick = v;
    } else if (strcmp(key, PK_GRV_RVDLY) == 0) {  g->rv_s_dly = v;
    } else if (strcmp(key, PK_GRV_DLYSEND) == 0) { g->dly_send = v;
    } else if (strcmp(key, PK_GRV_DLYFB) == 0) {   g->dly_fb = 0.92f * v;
    } else if (strcmp(key, PK_GRV_DLYTONE) == 0) { groove_dly_tone(g, v);
    } else if (strcmp(key, PK_GRV_DLYTIME) == 0) {
        int t = (int)(parse_f(val) + 0.5f); if (t < 0) t = 0; if (t > 5) t = 5;   /* RAW enum */
        g->dly_time = t;
    } else if (strcmp(key, PK_GRV_RVDECAY) == 0) {
        g->rv_fb = 0.6f + 0.375f * v;           /* FDN loop gain 0.6..0.975 (<1, bounded) */
    } else if (strcmp(key, PK_GRV_RVTONE) == 0) {
        /* damping LP corner 1.5 kHz (dark concrete) .. 12 kHz (bright) */
        float fc = 1500.0f * powf(8.0f, v);
        g->rv_damp = expf(-2.0f * (float)M_PI * fc / OMEGA_SR);
    } else if (strcmp(key, PK_GRV_RVTYPE) == 0) {
        rv_apply_type(g, (int)(parse_f(val) + 0.5f));
    } else if (strcmp(key, PK_GRV_GSCALE) == 0) {
        /* UI enum 0..12: 0=Unquantized, 1=Chromatic..12=Diminished → g_scales[0..11] */
        int idx = (int)(parse_f(val) + 0.5f);
        if (idx < 0) idx = 0; if (idx > 12) idx = 12;
        if (idx == 0) {
            g->gen_unquantized = true;
        } else {
            g->gen_unquantized = false;
            g->gen_scale = idx - 1;  /* maps UI 1..12 → g_scales[0..11] */
        }
        /* v0.4: two separate always-visible root controls (grv_groot Hz, grv_grootnote
         * note). SCALE just picks which one feeds gen_base_hz — recompute from the
         * active control so a scale change re-expresses the stored roots (no jump). */
        gen_recompute_base_hz(g);
        groove_gen_rebuild(g);                /* gen_seq holds scale-applied semitones */
    } else if (strcmp(key, PK_GRV_GROOT) == 0) {
        /* ROOT (Hz) — single merged control used in BOTH modes (v0.4.1). Defensive
         * parse: a raw Hz value (20..520) passes through; a stray normalized 0..1 is
         * mapped log across the Hz range (fixes the "shows 1 Hz then snaps to 20 Hz"
         * report where the knob fed a 0..1 value and clamped to the 20 Hz floor). */
        float raw = parse_f(val);
        if (raw <= 1.5f) {
            float t = raw < 0.0f ? 0.0f : (raw > 1.0f ? 1.0f : raw);
            raw = GROOT_HZ_MIN * powf(GROOT_HZ_MAX / GROOT_HZ_MIN, t);
        }
        g->gen_root_hz = clampf(raw, GROOT_HZ_MIN, GROOT_HZ_MAX);
        gen_recompute_base_hz(g);
    } else if (strcmp(key, PK_GRV_GROOTNOTE) == 0) {
        /* Legacy ROOT NOTE key — kept for preset/ABI compatibility (no longer on the
         * UI; ROOT is merged into grv_groot). Still stored if a saved preset sets it. */
        int m = (int)(parse_f(val) + 0.5f);
        if (m < 0) m = 0; if (m > GROOT_NOTE_MAX) m = GROOT_NOTE_MAX;
        g->gen_root_note = m;
    } else if (strcmp(key, PK_GRV_GRANGE) == 0) {
        g->gen_range = 1 + (int)(v * 23.0f + 0.5f);   /* 1..24 degrees */
        if (g->gen_range < 1) g->gen_range = 1;
        if (g->gen_range > 24) g->gen_range = 24;
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GSEED) == 0) {
        /* 7-bit stepped SEED (128 discrete patterns): each detent = a distinct
         * sequence, not a smooth 16-bit sweep. */
        g->gen_seed_raw = (unsigned)(v * 127.0f + 0.5f);
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GSEQLEN) == 0) {
        /* v0.4.1 fix: SEQ LEN is an `int` param — the host sends the RAW integer
         * (1..64), exactly like grv_grootnote. The old `v*63` normalized map clamped
         * every nonzero value to 64 ("snap to 64"). Parse raw. */
        int n = (int)(parse_f(val) + 0.5f);
        if (n < 1) n = 1; if (n > 64) n = 64;
        g->gen_seqlen = n;
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GDENSITY) == 0) {
        g->gen_density = v;
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GROTATE) == 0) {
        g->gen_rotate = v * 2.0f - 1.0f;            /* bidirectional -1..1 → ±len steps */
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GSWING) == 0) {
        /* DECAY = gen amp D/R time (v0.4.1, item 11): pluckier curve, decays toward a
         * 0.5 sustain floor; filter env = 65% of amp env. See gen_set_decay. */
        gen_set_decay(g, v);
    } else if (strcmp(key, PK_GRV_GSWINGAMT) == 0) {
        /* SWING (v0.4.1, item 7): bipolar, 0.5 = none. Subtle ±8% max so it stays
         * musical even when DENSITY isn't full. Lengthens even / shortens odd steps. */
        g->gen_swing = (v - 0.5f) * 2.0f * 0.08f;
    } else if (strcmp(key, PK_GRV_GNOTELEN) == 0) {
        /* NOTE LEN (v0.4.1, item 4/5): step division enum (raw index). */
        int idx = (int)(parse_f(val) + 0.5f);
        if (idx < 0) idx = 0; if (idx > 6) idx = 6;
        g->gen_notelen = idx;
        g->gen_notelen_mult = gen_notelen_to_mult(idx);
    } else if (strcmp(key, PK_GRV_GENTAPS) == 0) {
        /* GEN>TAPS send (v0.4.1, item 10): feeds the GEN voice into the taps delay
         * ring so the rumble/reverb act as a delay/echo on GEN. */
        g->gen_gentaps = v;
    } else if (strcmp(key, PK_GRV_HPF) == 0) {
        /* TAPS high-pass (v0.4.1, item 12): fully OFF at the bottom (hpf_g==0 bypass),
         * else ~20 Hz .. ~2 kHz log. */
        if (v <= 0.001f) {
            g->hpf_g = 0.0f;
        } else {
            float fc = 20.0f * powf(2000.0f / 20.0f, v);
            g->hpf_g = tpt_g_from_hz(fc);
        }
    } else if (gen_voice_key(g, key, val, v)) {
        /* a voice's own A/B knob (handled) */
    } else if (strcmp(key, PK_GRV_GSLIDE) == 0) {
        g->gen_slide = v;
    } else if (strcmp(key, PK_GRV_GRETRIG) == 0) {
        /* RESET (v0.4.1, item 4): 0 None, 1..8 = that many bars, 9 On Note. */
        int m = (int)(parse_f(val) + 0.5f);
        g->gen_retrig = (m < 0) ? 0 : (m > 9 ? 9 : m);
    } else if (strcmp(key, PK_GRV_GPATTERN) == 0) {
        int m = (int)(parse_f(val) + 0.5f);
        g->gen_pattern = m < 0 ? 0 : (m >= GEN_PAT_COUNT ? GEN_PAT_COUNT - 1 : m);
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GVOICE) == 0) {
        gen_set_voice(g, (int)(parse_f(val) + 0.5f));
    } else if (strcmp(key, PK_GRV_GACCENT) == 0) {
        g->gen_accent = v;
    } else if (strcmp(key, PK_GRV_GOCTJUMP) == 0) {
        g->gen_octjump = v;
    } else if (strcmp(key, PK_GRV_GRATCHET) == 0) {
        g->gen_ratchet = v;
    } else if (strcmp(key, PK_GRV_GDROP) == 0) {
        g->gen_drop = v;
    } else if (strcmp(key, PK_GRV_GNRAND) == 0) {
        g->gen_nrand = v;
    } else if (strcmp(key, PK_GRV_GLOOP) == 0) {
        int n = (int)(parse_f(val) + 0.5f);       /* RAW int 0..16 */
        g->gen_loop = n < 0 ? 0 : (n > 16 ? 16 : n);
    }
    /* Unknown keys ignored. */
}

/* Read-only view of the GEN pattern for the UI necklace widget: one char per
 * step ('.' rest; else a 62-symbol pitch code, the upper half = accented). */
int groove_gen_view(const groove_state_t *g, char *buf, int buf_len) {
    static const char AL[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int len = g->gen_seqlen < 1 ? 1 : (g->gen_seqlen > 64 ? 64 : g->gen_seqlen);
    if (!buf || buf_len < len + 1) return -1;
    for (int i = 0; i < len; i++) {
        if (!g->gen_gate[i]) { buf[i] = '.'; continue; }
        int n = g->gen_seq[i] + 15; if (n < 0) n = 0; if (n > 30) n = 30;
        buf[i] = AL[n + (g->gen_acc[i] ? 31 : 0)];
    }
    buf[len] = '\0';
    return len;
}
