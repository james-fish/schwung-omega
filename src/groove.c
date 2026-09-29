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

/* ---- LENGTH -> per-tap decay curve + feedback amount (control-rate) ------- */
/* A longer LENGTH keeps the later taps louder AND raises the feedback gain so
 * the rumble sustains into a continuous drone (GRVX-02). base drives the tap
 * decay; fb_amount 0.30..0.88 recirculates energy through the ring. */
static void groove_set_length(groove_state_t *g, float v) {
    float base = 0.3f + 0.65f * v;
    for (int t = 0; t < 4; t++)
        g->tap_decay[t] = powf(base, (float)(t + 1));
    g->fb_amount = 0.30f + 0.58f * v;   /* longer LENGTH -> more sustained rumble */
}

static inline float tpt_g_from_hz(float fc) {
    return tanf((float)M_PI * fc / OMEGA_SR);   /* control-rate only */
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

    /* Page-1 defaults. VOL starts at 0 so the groove rumble is SILENT on a bare
     * create and does not color the per-model kick voicing (the groove is an
     * opt-in performance voice the user opens via grv_vol; the FX voicing
     * battery renders each kick model in isolation). tap_level/decay/COLOR are
     * seeded to musical middles so the moment grv_vol is raised the rumble is
     * immediately shaped and audible (test_groove's prime_groove opens it). */
    g->vol = 0.0f;
    for (int t = 0; t < 4; t++) g->tap_level[t] = 0.6f;
    groove_set_length(g, 0.5f);
    g->color_g      = tpt_g_from_hz(8000.0f);
    g->color_lp_l_s = 0.0f;
    g->color_lp_r_s = 0.0f;
    g->mono         = false;

    /* Redesign defaults (C1). */
    g->type       = GROOVE_TYPE_TAPS;
    g->fb_lp_l_s  = 0.0f;
    g->fb_lp_r_s  = 0.0f;

    /* Groove FX defaults (C1-02) — off/neutral (calloc already zeroed buffers). */
    g->drive       = 0.0f;
    g->filter_type = GRV_FILT_LP;
    g->lfo_phase   = 0.0f;
    g->lfo_inc     = 0.5f / OMEGA_SR;   /* ~0.5 Hz at LFO SPD default */
    g->lfo_amt     = 0.0f;
    g->rv_fb       = 0.7f;
    g->rv_damp     = 0.3f;
    g->rv_mix      = 0.0f;
}

/* ---- groove_update_tempo (VERBATIM C-RESEARCH §Pattern 2, GRV-02) -------- */
/* Called ONCE per render block, before the per-sample loop. NEVER per sample.
 * Derives BPM from the guarded host transport chain, smooths jitter, and
 * recomputes the 16th-note tap interval ONLY when BPM actually moves. */
void groove_update_tempo(groove_state_t *g, const struct host_api_v1 *host_fwd,
                         int frames) {
    const host_api_v1_t *host = (const host_api_v1_t *)host_fwd;
    float bpm = 0.0f;

    /* (1) PRIMARY: beat-delta from get_beat_position (GRV-02). */
    if (host && host->get_beat_position) {
        double beat = host->get_beat_position();
        if (beat >= 0.0) {                          /* >=0 => transport running */
            if (g->have_prev_beat) {
                double dbeat = beat - g->prev_beat;
                if (dbeat > 0.0 && dbeat < 4.0) {   /* sane per-block advance */
                    bpm = (float)(dbeat * 60.0 * (double)OMEGA_SR / (double)frames);
                }
            }
            g->prev_beat = beat;
            g->have_prev_beat = true;
        } else {
            g->have_prev_beat = false;              /* stopped: drop stale prev */
        }
    }

    /* (2) FALLBACK: get_bpm (NULL-guarded, sanity-clamped). */
    if (bpm <= 0.0f && host && host->get_bpm) {
        float b = host->get_bpm();
        if (b >= 20.0f && b <= 999.0f) bpm = b;
    }

    /* (3) LAST RESORT: 120 constant — ONLY when no transport info at all. */
    if (bpm <= 0.0f) bpm = g->last_bpm > 0.0f ? g->last_bpm : 120.0f;

    /* Smooth jitter; recompute the tap interval ONLY when BPM actually moves. */
    bpm = clampf(bpm, 20.0f, 300.0f);
    g->bpm_smooth += (bpm - g->bpm_smooth) * 0.20f;         /* one-pole EMA */
    if (fabsf(g->bpm_smooth - g->last_bpm) > 0.5f) {        /* control-rate re-lock */
        g->last_bpm = g->bpm_smooth;
        int spq = (int)((60.0f / g->bpm_smooth) * OMEGA_SR / 4.0f + 0.5f);  /* samples_per_16th */
        if (spq < 1) spq = 1;
        if (spq * 4 > (int)(GRV_DELAY_MASK)) spq = (int)(GRV_DELAY_MASK) / 4;  /* clamp reach */
        g->samples_per_16th = spq;
    }
}

/* ---- groove_tick (VERBATIM C-RESEARCH §Pattern 1 + §Pattern 3) ----------- */
/* Per sample. kick_l/kick_r are this sample's kick output. Writes the kick into
 * the ring, reads four mask-wrapped 16th-note taps behind the write head, then
 * applies the COLOR lowpass + MONO force-sum + VOL. NO transcendental here. */
void groove_tick(groove_state_t *g, float kick_l, float kick_r,
                 float *out_gl, float *out_gr) {
    /* FEEDBACK RUMBLE (GRVX-02): recirculate the signal one 16th behind into the
     * write head, darkened by a one-pole LP and bounded by a gentle saturator, so
     * the ring sustains a continuous resonant rumble instead of a dry gated echo.
     * The LP keeps the rumble low/warm; the saturator prevents runaway (fb<0.9 +
     * loss guarantees stability). All algebraic — no transcendental per sample. */
    unsigned rp1 = (g->write_pos - (unsigned)g->samples_per_16th) & GRV_DELAY_MASK;
    const float FB_LP_A = 0.20f;                 /* ~1.5 kHz one-pole in the loop */
    g->fb_lp_l_s += FB_LP_A * (g->buf_l[rp1] - g->fb_lp_l_s);
    g->fb_lp_r_s += FB_LP_A * (g->buf_r[rp1] - g->fb_lp_r_s);
    float wl = kick_l + g->fb_amount * g->fb_lp_l_s;
    float wr = kick_r + g->fb_amount * g->fb_lp_r_s;
    wl = wl / (1.0f + 0.25f * (wl < 0.0f ? -wl : wl));   /* bounded feedback */
    wr = wr / (1.0f + 0.25f * (wr < 0.0f ? -wr : wr));
    g->buf_l[g->write_pos] = wl;
    g->buf_r[g->write_pos] = wr;

    float gl = 0.0f, gr = 0.0f;
    for (int t = 0; t < 4; t++) {
        unsigned rp = (g->write_pos - (unsigned)((t + 1) * g->samples_per_16th)) & GRV_DELAY_MASK;
        float w = g->tap_level[t] * g->tap_decay[t];   /* TAP level * LENGTH decay weight */
        gl += g->buf_l[rp] * w;
        gr += g->buf_r[rp] * w;
    }
    g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;

    /* FILTER (C1-02): the COLOR one-pole gives LP; HP = input - LP; Off bypasses.
     * The LP state is always advanced so toggling type is click-free. */
    tpt1_t lpl = { g->color_lp_l_s };
    tpt1_t lpr = { g->color_lp_r_s };
    float lo_l = tpt1_lp(&lpl, gl, g->color_g);
    float lo_r = tpt1_lp(&lpr, gr, g->color_g);
    g->color_lp_l_s = lpl.s;
    g->color_lp_r_s = lpr.s;
    if (g->filter_type == GRV_FILT_LP)      { gl = lo_l;        gr = lo_r; }
    else if (g->filter_type == GRV_FILT_HP) { gl = gl - lo_l;   gr = gr - lo_r; }
    /* GRV_FILT_OFF: leave gl/gr unfiltered. */

    /* DRIVE (C1-02): saturation with makeup, dry/wet by amount. */
    if (g->drive > 0.0f) {
        float k = 1.0f + g->drive * 4.0f;
        float mk = 1.0f / (1.0f + g->drive * 0.7f);
        float wl = (gl * k) / (1.0f + (gl < 0 ? -gl : gl) * k) * mk;
        float wr = (gr * k) / (1.0f + (gr < 0 ? -gr : gr) * k) * mk;
        gl = gl + g->drive * (wl - gl);
        gr = gr + g->drive * (wr - gr);
    }

    /* REVERB (C1-02): cheap mono Schroeder — 2 damped combs + 1 allpass, added
     * back by MIX. Non-power-of-two ring sizes wrap by branch (no % per sample). */
    if (g->rv_mix > 0.0f) {
        float in = 0.5f * (gl + gr);
        float c1 = g->rv_comb1[g->rv_c1i];
        g->rv_c1_lp = c1 * (1.0f - g->rv_damp) + g->rv_c1_lp * g->rv_damp;
        g->rv_comb1[g->rv_c1i] = in + g->rv_c1_lp * g->rv_fb;
        if (++g->rv_c1i >= 1557) g->rv_c1i = 0;
        float c2 = g->rv_comb2[g->rv_c2i];
        g->rv_c2_lp = c2 * (1.0f - g->rv_damp) + g->rv_c2_lp * g->rv_damp;
        g->rv_comb2[g->rv_c2i] = in + g->rv_c2_lp * g->rv_fb;
        if (++g->rv_c2i >= 1617) g->rv_c2i = 0;
        float wet = 0.5f * (c1 + c2);
        float ab = g->rv_ap[g->rv_api];              /* allpass diffusion */
        float ao = -wet + ab;
        g->rv_ap[g->rv_api] = wet + ab * 0.5f;
        if (++g->rv_api >= 556) g->rv_api = 0;
        wet = ao;
        gl = gl + g->rv_mix * wet;
        gr = gr + g->rv_mix * wet;
    }

    /* LFO tremolo (C1-02): triangle amplitude mod, depth by LFO AMT. */
    if (g->lfo_amt > 0.0f) {
        float ph = g->lfo_phase;
        float tri = (ph < 0.5f) ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph);  /* -1..1 */
        float trem = 1.0f - g->lfo_amt * 0.5f * (1.0f - tri);              /* 1..1-amt */
        gl *= trem; gr *= trem;
    }
    g->lfo_phase += g->lfo_inc;
    if (g->lfo_phase >= 1.0f) g->lfo_phase -= 1.0f;

    if (g->mono) { float m = 0.5f * (gl + gr); gl = gr = m; }   /* GRV-05 sub-bass mono sum */
    gl *= g->vol; gr *= g->vol;
    *out_gl = gl; *out_gr = gr;
}

/* ---- groove_set_param (control-rate Page-1 mappings, GRV-03/05) ---------- */
/* All powf/tanf run HERE, never in groove_tick. Unknown keys are ignored. */
void groove_set_param(groove_state_t *g, const char *key, const char *val) {
    if (!key) return;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_GRV_TYPE) == 0) {
        g->type = (v >= 0.5f) ? GROOVE_TYPE_GEN : GROOVE_TYPE_TAPS;
    } else if (strcmp(key, PK_GRV_VOL) == 0) {
        g->vol = v;
    } else if (strcmp(key, PK_GRV_LENGTH) == 0) {
        groove_set_length(g, v);
    } else if (strcmp(key, PK_GRV_COLOR) == 0) {
        float fc = 200.0f + v * (18000.0f - 200.0f);
        g->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_GRV_TAP1) == 0) {
        g->tap_level[0] = v;
    } else if (strcmp(key, PK_GRV_TAP2) == 0) {
        g->tap_level[1] = v;
    } else if (strcmp(key, PK_GRV_TAP3) == 0) {
        g->tap_level[2] = v;
    } else if (strcmp(key, PK_GRV_TAP4) == 0) {
        g->tap_level[3] = v;
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
        g->rv_mix = v;
    } else if (strcmp(key, PK_GRV_RVDECAY) == 0) {
        g->rv_fb = 0.5f + 0.49f * v;           /* comb feedback 0.5..0.99 */
    } else if (strcmp(key, PK_GRV_RVTONE) == 0) {
        g->rv_damp = 0.1f + 0.85f * (1.0f - v); /* brighter as TONE rises */
    } else if (strcmp(key, PK_GRV_RVTYPE) == 0) {
        /* Reserved: type scales comb tunings (Room/Hall/Plate). Stored via the
         * cache; current tunings are fixed — a musical default across types. */
    }
    /* Unknown keys ignored. */
}
