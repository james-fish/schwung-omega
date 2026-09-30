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

/* ---- Mono Schroeder reverb helper (vhr §B.2, ONE instance, pre OR post) ----
 * Runs the existing 2-comb + 1-allpass Schroeder on a mono input and returns
 * the mono wet. Called at the PRE point (on the kick) OR the POST point (on the
 * tap sum) — never both duplicated. NO transcendental; buffers wrap by branch
 * (non-power-of-two sizes), never `%` per sample. */
static inline float groove_reverb_mono(groove_state_t *g, float in) {
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
    return ao;
}

static inline float tpt_g_from_hz(float fc) {
    return tanf((float)M_PI * fc / OMEGA_SR);   /* control-rate only */
}

/* ---- Equal-power tap normalisation (control-rate, vhr §A.3) --------------- */
/* Precompute 1/sqrt(max(sum of active tap gains, 1)) so the per-sample tap sum
 * needs NO sqrt. sum = Σ tap_level[t] (the global tap_trim is applied per tap
 * separately). At LENGTH=1 with all four taps=1 this divisor is sqrt(4)=2, so
 * each clean copy comes out at 0.5x the raw kick (× vol) — the stated
 * equal-power scale for the clean-copies invariant. */
static void groove_update_tap_norm(groove_state_t *g) {
    float sum = 0.0f;
    for (int t = 0; t < 4; t++) sum += g->tap_level[t];
    if (sum < 1.0f) sum = 1.0f;
    g->tap_norm = 1.0f / sqrtf(sum);
}

/* ---- LENGTH -> opposing feedback / tap-trim / diffusion (control-rate) ---- */
/* Bidirectional morph (vhr §A.2). RIGHT (v=1): fb EXACTLY 0 + full tap trim ->
 * exact clean copies. LEFT (v=0): fb 0.85 (stability cap) + reduced tap trim
 * (equal-power loudness comp) + full diffusion -> smeared resonant drone. NO
 * per-tap pow decay (the deleted loudness bug). */
static void groove_set_length(groove_state_t *g, float v) {
    g->fb_amount   = 0.85f * (1.0f - v);   /* 0 at v=1 (clean), 0.85 at v=0 (drone) */
    g->tap_trim    = 0.6f + 0.4f * v;      /* 0.6 (drone) .. 1.0 (clean) */
    g->diffuse_amt = 1.0f - v;             /* bypass diffusion at the clean end */
}

/* ---- GEN groove sequencer (C1-03, GRVX-04/05) ---------------------------- */
static inline unsigned long long grv_xorshift(unsigned long long *s) {
    unsigned long long x = *s ? *s : 0x9E3779B97F4A7C15ull;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    *s = x; return x;
}

/* Rebuild the step sequence + Euclidean gate from SEED/SEQ LEN/DENSITY/ROTATE.
 * Control-rate (set_param). Deterministic for a given SEED (reseeds the PRNG). */
void groove_gen_rebuild(groove_state_t *g) {
    g->gen_rng = 0x2545F4914F6CDD1Dull ^ ((unsigned long long)g->gen_seed_raw * 0x9E3779B1u + 1u);
    int len = g->gen_seqlen; if (len < 1) len = 1; if (len > 32) len = 32;
    /* Random scale degrees bounded by RANGE (1..24 degrees). */
    int rng = g->gen_range < 1 ? 1 : (g->gen_range > 24 ? 24 : g->gen_range);
    for (int i = 0; i < 32; i++)
        g->gen_seq[i] = (signed char)(grv_xorshift(&g->gen_rng) % (unsigned)rng);
    /* Euclidean gate: DENSITY -> npulses of len, spread evenly (Bresenham). */
    int npulse = (int)(g->gen_density * (float)len + 0.5f);
    if (npulse < 1) npulse = 1; if (npulse > len) npulse = len;
    int bucket = 0;
    for (int i = 0; i < 32; i++) {
        if (i < len) { bucket += npulse; if (bucket >= len) { bucket -= len; g->gen_gate[i] = 1; } else g->gen_gate[i] = 0; }
        else g->gen_gate[i] = 0;
    }
    /* ROTATE: bidirectional rotate of the gate pattern within [0,len). */
    int rot = (int)(g->gen_rotate * (float)len);
    if (rot != 0 && len > 0) {
        unsigned char tmp[32];
        for (int i = 0; i < len; i++) { int j = ((i - rot) % len + len) % len; tmp[i] = g->gen_gate[j]; }
        for (int i = 0; i < len; i++) g->gen_gate[i] = tmp[i];
    }
}

void groove_gen_restart(groove_state_t *g) {
    g->gen_step = 0;
    g->gen_step_ctr = 0;   /* fire step 0 on the next tick */
    g->gen_bar16 = 0;
    g->gen_env = 0.0f;
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
    g->vol = 0.0f;
    for (int t = 0; t < 4; t++) g->tap_level[t] = 0.6f;
    groove_set_length(g, 0.5f);
    groove_update_tap_norm(g);
    g->color_g       = tpt_g_from_hz(8000.0f);
    g->color_lp_l_s  = 0.0f;
    g->color_lp_r_s  = 0.0f;
    g->color_lp2_l_s = 0.0f;
    g->color_lp2_r_s = 0.0f;
    g->mono          = false;

    /* Redesign defaults (C1 + vhr). */
    g->type        = GROOVE_TYPE_TAPS;
    g->fb_lp_l_s   = 0.0f;
    g->fb_lp_r_s   = 0.0f;
    g->fb_lp2_l_s  = 0.0f;
    g->fb_lp2_r_s  = 0.0f;
    g->loop_hp_l_s = 0.0f;
    g->loop_hp_r_s = 0.0f;
    /* ~30 Hz feedback-path HP coefficient (structural sub-cleaner; vhr §C.2). */
    g->loop_hp_g   = tpt_g_from_hz(30.0f);
    g->ap1i = 0; g->ap2i = 0;
    /* Reverb routing: center = off on a bare create (vhr §B.2). */
    g->rv_pre_amt  = 0.0f;
    g->rv_post_amt = 0.0f;

    /* Groove FX defaults (C1-02) — off/neutral (calloc already zeroed buffers). */
    g->drive       = 0.0f;
    g->filter_type = GRV_FILT_LP;
    g->lfo_phase   = 0.0f;
    g->lfo_inc     = 0.5f / OMEGA_SR;   /* ~0.5 Hz at LFO SPD default */
    g->lfo_amt     = 0.0f;
    g->rv_fb       = 0.7f;
    g->rv_damp     = 0.3f;

    /* GEN groove voice defaults (C1-03 / E3). */
    g->gen_unquantized = false;
    g->gen_scale   = 1;            /* major by default (UI idx 2 = Chromatic→0, Major→1) */
    g->gen_root_param = 0.542f;    /* A1 (MIDI 45) = 55 Hz */
    g->gen_range   = 12;           /* 12 degrees ≈ one octave of sequence variation */
    g->gen_seqlen  = 16;
    g->gen_wave    = 0;            /* sine */
    g->gen_retrig  = GRV_RETRIG_NONE;   /* free-run by default (GRVX-05) */
    g->gen_density = 0.6f;
    g->gen_rotate  = 0.0f;
    g->gen_swing   = 0.0f;
    g->gen_fold    = 0.0f;
    g->gen_base_hz = 55.0f;        /* A1 — set from gen_root_param at init */
    g->gen_seed_raw = 12345u;
    g->gen_env_coef = 0.9995f;
    g->gen_running = false;
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
    float bpm = 0.0f;
    bool running = false;   /* GRVX-05: GEN sequencer stops when transport stops */

    /* (1) PRIMARY: beat-delta from get_beat_position (GRV-02). */
    if (host && host->get_beat_position) {
        double beat = host->get_beat_position();
        if (beat >= 0.0) {                          /* >=0 => transport running */
            if (g->have_prev_beat) {
                double dbeat = beat - g->prev_beat;
                if (dbeat > 0.0 && dbeat < 4.0) {   /* sane per-block advance */
                    bpm = (float)(dbeat * 60.0 * (double)OMEGA_SR / (double)frames);
                    running = true;                 /* beat advanced => transport live */
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
        g->samples_per_16th = spq;                          /* integer clock (GEN) */

        /* TAPS: set the FLOAT slew target (no rounding); groove_tick glides
         * g->spq toward it so live BPM changes do not click (vhr §A.6). Clamp
         * the reach so 4*spq stays inside the ring. */
        float fspq = (60.0f / g->bpm_smooth) * OMEGA_SR / 4.0f;
        if (fspq < 1.0f) fspq = 1.0f;
        float fmaxspq = (float)((int)(GRV_DELAY_MASK) / 4);
        if (fspq > fmaxspq) fspq = fmaxspq;
        g->spq_target = fspq;
    }

    /* If the host exposes NO transport at all, treat the GEN sequencer as free-
     * running (so it plays under the null-transport fallback); otherwise gate it
     * on real beat advance so it STOPS when the transport stops (GRVX-05). */
    if (!host || (!host->get_beat_position && !host->get_bpm)) running = true;
    g->gen_running = running;
}

/* ---- groove_tick (VERBATIM C-RESEARCH §Pattern 1 + §Pattern 3) ----------- */
/* Per sample. kick_l/kick_r are this sample's kick output. Writes the kick into
 * the ring, reads four mask-wrapped 16th-note taps behind the write head, then
 * applies the COLOR lowpass + MONO force-sum + VOL. NO transcendental here. */
void groove_tick(groove_state_t *g, float kick_l, float kick_r,
                 float *out_gl, float *out_gr) {
    float gl = 0.0f, gr = 0.0f;

    if (g->type == GROOVE_TYPE_GEN) {
        /* GEN groove voice (C1-03): a transport-clocked scale-quantized step
         * sequencer -> wavetable osc + wavefolder. Decoupled from the kick model
         * (kick_l/r are ignored). Advances only while the transport runs (GRVX-05). */
        if (g->gen_running) {
            if (g->gen_step_ctr <= 0) {
                int len = g->gen_seqlen < 1 ? 1 : (g->gen_seqlen > 32 ? 32 : g->gen_seqlen);
                int step = g->gen_step % len;
                if (g->gen_gate[step]) {
                    int semi = g->gen_unquantized
                        ? (int)g->gen_seq[step]   /* raw chromatic offset when unquantized */
                        : scale_quantize(g->gen_scale, g->gen_seq[step]);
                    g->gen_freq = g->gen_base_hz * powf(2.0f, (float)semi / 12.0f);  /* per-step only */
                    g->gen_env = 1.0f;
                    g->gen_osc_phase = 0.0f;
                }
                /* SWING: delay odd steps by up to ~60% of a 16th. */
                int dur = g->samples_per_16th;
                if (step & 1) dur += (int)(g->gen_swing * 0.6f * (float)g->samples_per_16th);
                g->gen_step_ctr = dur > 1 ? dur : 1;
                g->gen_step++;
                if (++g->gen_bar16 >= 16) g->gen_bar16 = 0;
                /* Bar-based retrigger: restart at the top of the bar window. */
                int bars = 0;
                switch (g->gen_retrig) {
                    case GRV_RETRIG_1BAR: bars = 1; break;
                    case GRV_RETRIG_2BAR: bars = 2; break;
                    case GRV_RETRIG_4BAR: bars = 4; break;
                    case GRV_RETRIG_8BAR: bars = 8; break;
                    default: break;
                }
                if (bars && g->gen_step >= 16 * bars) g->gen_step = 0;
            }
            g->gen_step_ctr--;
        }
        float osc = wt_read_bl(g->gen_wave, 0, g->gen_osc_phase);
        /* WAVEFOLDER: fold the osc for extra harmonics as FOLD rises. */
        if (g->gen_fold > 0.0f) {
            float d = 1.0f + g->gen_fold * 3.0f;
            float v = osc * d;
            v = v - 4.0f * floorf(v * 0.25f + 0.5f);   /* fold into ~[-2,2] */
            osc = osc + g->gen_fold * (0.5f * v - osc);
        }
        float s = osc * g->gen_env;
        g->gen_env *= g->gen_env_coef;
        g->gen_osc_phase += g->gen_freq / OMEGA_SR;
        if (g->gen_osc_phase >= 1.0f) g->gen_osc_phase -= 1.0f;
        gl = gr = s;
        /* Keep the ring write-head advancing so a later TAPS switch is coherent. */
        g->buf_l[g->write_pos] = 0.0f; g->buf_r[g->write_pos] = 0.0f;
        g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;
    } else {
        /* TAPS (vhr redesign): ONE 16th-note feedback delay line read at 4
         * fractional tap points. LENGTH morphs between clean equal-level copies
         * (fb=0, right) and a smeared damped resonant drone (fb=0.85, left). The
         * RAW kick is written to the ring; the ~30 Hz HP is FEEDBACK-PATH ONLY,
         * so at fb=0 the taps are exact copies of the raw kick. All algebraic —
         * only floorf per sample (single aarch64 instruction). */

        /* (1) Per-sample slew of the fractional 16th spacing (§A.6). */
        g->spq += (g->spq_target - g->spq) * SPQ_SLEW;
        float spq = g->spq;
        if (spq < 1.0f) spq = 1.0f;                  /* guard read distance */
        float fmaxspq = (float)((int)(GRV_DELAY_MASK) / 4);
        if (spq > fmaxspq) spq = fmaxspq;            /* 4*spq inside the ring */

        /* (2) Feedback tap read at 1*spq behind the write head (fractional). */
        float d1  = spq;
        float rp1 = (float)g->write_pos - d1;
        float f1  = rp1 - floorf(rp1);
        int   a0  = ((int)floorf(rp1)) & GRV_DELAY_MASK;
        int   a1  = (a0 + 1) & GRV_DELAY_MASK;
        float fb_l = g->buf_l[a0] + f1 * (g->buf_l[a1] - g->buf_l[a0]);
        float fb_r = g->buf_r[a0] + f1 * (g->buf_r[a1] - g->buf_r[a0]);

        /* (3) ~30 Hz HP on the FEEDBACK signal ONLY (structural sub-cleaner +
         * stability; §C.2/§A.4). hp = x - lp(x@30Hz). Do NOT high-pass the ring
         * input — the raw kick must stay intact for the clean-copies invariant. */
        {
            tpt1_t hl = { g->loop_hp_l_s };
            tpt1_t hr = { g->loop_hp_r_s };
            float lo_l = tpt1_lp(&hl, fb_l, g->loop_hp_g);
            float lo_r = tpt1_lp(&hr, fb_r, g->loop_hp_g);
            g->loop_hp_l_s = hl.s;
            g->loop_hp_r_s = hr.s;
            fb_l = fb_l - lo_l;
            fb_r = fb_r - lo_r;
        }

        /* (4) In-loop diffusion: two short Schroeder allpasses in series,
         * crossfaded by diffuse_amt (0 clean .. 1 drone; §A.5). Mono-diffuse the
         * L/R average through each allpass, then re-apply to both channels
         * (rumble is sub-bass / near-mono; keeps RAM tiny and stays stable). */
        {
            const float g_ap = 0.5f;
            float in_m  = 0.5f * (fb_l + fb_r);
            float ab1   = g->ap1[g->ap1i];
            float ao1   = -g_ap * in_m + ab1;
            g->ap1[g->ap1i] = in_m + g_ap * ao1;
            if (++g->ap1i >= 241) g->ap1i = 0;
            float d1m   = in_m + g->diffuse_amt * (ao1 - in_m);
            float ab2   = g->ap2[g->ap2i];
            float ao2   = -g_ap * d1m + ab2;
            g->ap2[g->ap2i] = d1m + g_ap * ao2;
            if (++g->ap2i >= 113) g->ap2i = 0;
            float d2m   = d1m + g->diffuse_amt * (ao2 - d1m);
            float delta = d2m - in_m;                /* diffusion contribution */
            fb_l += delta;
            fb_r += delta;
        }

        /* (5) COLOR-linked 2-pole LP in the loop (darkens the drone as it
         * decays; §C.3). Two cascaded tpt1 stages share the COLOR coefficient. */
        {
            tpt1_t l1 = { g->fb_lp_l_s },  r1 = { g->fb_lp_r_s };
            fb_l = tpt1_lp(&l1, fb_l, g->color_g);
            fb_r = tpt1_lp(&r1, fb_r, g->color_g);
            g->fb_lp_l_s = l1.s;  g->fb_lp_r_s = r1.s;
            tpt1_t l2 = { g->fb_lp2_l_s }, r2 = { g->fb_lp2_r_s };
            fb_l = tpt1_lp(&l2, fb_l, g->color_g);
            fb_r = tpt1_lp(&r2, fb_r, g->color_g);
            g->fb_lp2_l_s = l2.s; g->fb_lp2_r_s = r2.s;
        }

        /* (6) Ring input = RAW kick + pre-reverb (if PRE) + fb_amount*feedback.
         * NO ~30 Hz HP on this sum. NO x3.0 makeup. At fb=0 and rv_pre_amt=0 the
         * ring input is EXACTLY the raw kick (the clean-copies invariant). The
         * reverb-into-ring send is attenuated to <= ~0.5 for stability (§B.2). */
        float wl = kick_l + g->fb_amount * fb_l;
        float wr = kick_r + g->fb_amount * fb_r;
        if (g->rv_pre_amt > 0.0f) {
            float rin = 0.5f * (kick_l + kick_r);        /* reverb is mono-in */
            float rwet = groove_reverb_mono(g, rin);
            float send = 0.5f * g->rv_pre_amt * rwet;    /* cap effective send <= ~0.5 */
            wl += send;
            wr += send;
        }
        g->buf_l[g->write_pos] = wl;
        g->buf_r[g->write_pos] = wr;

        /* (7) Sum 4 EQUAL taps at k*spq behind the write head (fractional reads),
         * each weighted by tap_level[t]*tap_trim, normalised equal-power by the
         * precomputed tap_norm (1/sqrt(sum active gains); §A.3). NO per-tap pow
         * decay, NO per-sample sqrt. */
        for (int t = 0; t < 4; t++) {
            float dk  = (float)(t + 1) * spq;
            float rpk = (float)g->write_pos - dk;
            float fk  = rpk - floorf(rpk);
            int   k0  = ((int)floorf(rpk)) & GRV_DELAY_MASK;
            int   k1  = (k0 + 1) & GRV_DELAY_MASK;
            float sl  = g->buf_l[k0] + fk * (g->buf_l[k1] - g->buf_l[k0]);
            float sr  = g->buf_r[k0] + fk * (g->buf_r[k1] - g->buf_r[k0]);
            float w   = g->tap_level[t] * g->tap_trim;
            gl += sl * w;
            gr += sr * w;
        }
        gl *= g->tap_norm;
        gr *= g->tap_norm;
        g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;
    }

    /* FILTER: the COLOR LP gives LP; HP = input - LP; Off is a TRUE bypass (no
     * attenuation, no phase shift) so the clean-copies gate can neutralise the
     * output LP via GRV_FILTYPE=OFF. TAPS uses a 2-pole cascade (12 dB/oct — the
     * "dial in darkness" authority, vhr §C.1); the GEN branch keeps the original
     * 1-pole behaviour BYTE-IDENTICAL (constraint 6). The LP state is always
     * advanced so toggling type is click-free. */
    if (g->filter_type == GRV_FILT_OFF) {
        /* True bypass: still advance the LP state(s) so re-enabling is click-free.
         * For GEN keep advancing only the single stage (byte-identical to before
         * when it later re-enables). */
        tpt1_t lpl = { g->color_lp_l_s };
        tpt1_t lpr = { g->color_lp_r_s };
        (void)tpt1_lp(&lpl, gl, g->color_g);
        (void)tpt1_lp(&lpr, gr, g->color_g);
        g->color_lp_l_s = lpl.s;
        g->color_lp_r_s = lpr.s;
        if (g->type == GROOVE_TYPE_TAPS) {
            tpt1_t l2 = { g->color_lp2_l_s }, r2 = { g->color_lp2_r_s };
            (void)tpt1_lp(&l2, gl, g->color_g);
            (void)tpt1_lp(&r2, gr, g->color_g);
            g->color_lp2_l_s = l2.s; g->color_lp2_r_s = r2.s;
        }
        /* leave gl/gr unfiltered */
    } else if (g->type == GROOVE_TYPE_TAPS) {
        /* 2-pole cascade LP (two tpt1 stages sharing the COLOR coefficient). */
        tpt1_t lpl = { g->color_lp_l_s },  lpr = { g->color_lp_r_s };
        float s1l = tpt1_lp(&lpl, gl, g->color_g);
        float s1r = tpt1_lp(&lpr, gr, g->color_g);
        g->color_lp_l_s = lpl.s; g->color_lp_r_s = lpr.s;
        tpt1_t l2 = { g->color_lp2_l_s }, r2 = { g->color_lp2_r_s };
        float lo_l = tpt1_lp(&l2, s1l, g->color_g);
        float lo_r = tpt1_lp(&r2, s1r, g->color_g);
        g->color_lp2_l_s = l2.s; g->color_lp2_r_s = r2.s;
        if (g->filter_type == GRV_FILT_LP)      { gl = lo_l;      gr = lo_r; }
        else /* GRV_FILT_HP */                  { gl = gl - lo_l; gr = gr - lo_r; }
    } else {
        /* GEN: original 1-pole COLOR (byte-identical). */
        tpt1_t lpl = { g->color_lp_l_s };
        tpt1_t lpr = { g->color_lp_r_s };
        float lo_l = tpt1_lp(&lpl, gl, g->color_g);
        float lo_r = tpt1_lp(&lpr, gr, g->color_g);
        g->color_lp_l_s = lpl.s;
        g->color_lp_r_s = lpr.s;
        if (g->filter_type == GRV_FILT_LP)      { gl = lo_l;        gr = lo_r; }
        else if (g->filter_type == GRV_FILT_HP) { gl = gl - lo_l;   gr = gr - lo_r; }
    }

    /* DRIVE (C1-02): saturation with makeup, dry/wet by amount. */
    if (g->drive > 0.0f) {
        float k = 1.0f + g->drive * 4.0f;
        float mk = 1.0f / (1.0f + g->drive * 0.7f);
        float wl = (gl * k) / (1.0f + (gl < 0 ? -gl : gl) * k) * mk;
        float wr = (gr * k) / (1.0f + (gr < 0 ? -gr : gr) * k) * mk;
        gl = gl + g->drive * (wl - gl);
        gr = gr + g->drive * (wr - gr);
    }

    /* REVERB POST (vhr §B.2): when the bidirectional MIX knob is on the RIGHT,
     * run the SAME Schroeder reverb on the tap-summed (or GEN) output and mix
     * rv_post_amt wet. The PRE point (§B.2) is handled inside the TAPS branch
     * above; only ONE reverb instance exists. */
    if (g->rv_post_amt > 0.0f) {
        float in = 0.5f * (gl + gr);
        float wet = groove_reverb_mono(g, in);
        gl = gl + g->rv_post_amt * wet;
        gr = gr + g->rv_post_amt * wet;
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
        int nt = (v >= 0.5f) ? GROOVE_TYPE_GEN : GROOVE_TYPE_TAPS;
        if (nt != g->type && nt == GROOVE_TYPE_GEN) groove_gen_restart(g);
        g->type = nt;
    } else if (strcmp(key, PK_GRV_VOL) == 0) {
        g->vol = v;
    } else if (strcmp(key, PK_GRV_LENGTH) == 0) {
        groove_set_length(g, v);
    } else if (strcmp(key, PK_GRV_COLOR) == 0) {
        /* 30 Hz..20 kHz log sweep; always-LP (type selector removed in E2). */
        float fc = 30.0f * powf(20000.0f / 30.0f, v);
        g->color_g = tpt_g_from_hz(fc);
        g->filter_type = GRV_FILT_LP;
    } else if (strcmp(key, PK_GRV_TAP1) == 0) {
        g->tap_level[0] = v; groove_update_tap_norm(g);
    } else if (strcmp(key, PK_GRV_TAP2) == 0) {
        g->tap_level[1] = v; groove_update_tap_norm(g);
    } else if (strcmp(key, PK_GRV_TAP3) == 0) {
        g->tap_level[2] = v; groove_update_tap_norm(g);
    } else if (strcmp(key, PK_GRV_TAP4) == 0) {
        g->tap_level[3] = v; groove_update_tap_norm(g);
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
        /* Bidirectional reverb routing (vhr §B.2): center (0.5) = OFF (deadzone
         * +/-0.03); LEFT = PRE (reverb pre-smear into the tap ring input); RIGHT
         * = POST (reverb on the tap-summed output). ONE reverb instance; the sign
         * of the knob selects its input/output tap point. The old linear
         * post-only rv_mix=v semantics are gone. */
        if (v < 0.47f) {                       /* PRE */
            g->rv_pre_amt  = (0.5f - v) * 2.0f;
            g->rv_post_amt = 0.0f;
        } else if (v > 0.53f) {                /* POST */
            g->rv_post_amt = (v - 0.5f) * 2.0f;
            g->rv_pre_amt  = 0.0f;
        } else {                               /* center deadzone: OFF */
            g->rv_pre_amt  = 0.0f;
            g->rv_post_amt = 0.0f;
        }
    } else if (strcmp(key, PK_GRV_RVDECAY) == 0) {
        g->rv_fb = 0.5f + 0.49f * v;           /* comb feedback 0.5..0.99 */
    } else if (strcmp(key, PK_GRV_RVTONE) == 0) {
        g->rv_damp = 0.1f + 0.85f * (1.0f - v); /* brighter as TONE rises */
    } else if (strcmp(key, PK_GRV_RVTYPE) == 0) {
        /* Reserved: type scales comb tunings (Room/Hall/Plate). Stored via the
         * cache; current tunings are fixed — a musical default across types. */
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
        /* Recompute root Hz since mode may have changed. */
        if (g->gen_unquantized)
            g->gen_base_hz = 30.0f + g->gen_root_param * 170.0f;
        else {
            int semi = (int)(g->gen_root_param * 83.0f + 0.5f);
            g->gen_base_hz = 8.175f * powf(2.0f, (float)semi / 12.0f);
        }
    } else if (strcmp(key, PK_GRV_GROOT) == 0) {
        g->gen_root_param = v;
        if (g->gen_unquantized)
            g->gen_base_hz = 30.0f + v * 170.0f;
        else {
            int semi = (int)(v * 83.0f + 0.5f);
            g->gen_base_hz = 8.175f * powf(2.0f, (float)semi / 12.0f);
        }
    } else if (strcmp(key, PK_GRV_GRANGE) == 0) {
        g->gen_range = 1 + (int)(v * 23.0f + 0.5f);   /* 1..24 degrees */
        if (g->gen_range < 1) g->gen_range = 1;
        if (g->gen_range > 24) g->gen_range = 24;
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GSEED) == 0) {
        g->gen_seed_raw = (unsigned)(v * 65535.0f);
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GSEQLEN) == 0) {
        g->gen_seqlen = 1 + (int)(v * 31.0f + 0.5f);   /* 1..32 (GRVX-04) */
        if (g->gen_seqlen < 1) g->gen_seqlen = 1; if (g->gen_seqlen > 32) g->gen_seqlen = 32;
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GDENSITY) == 0) {
        g->gen_density = v;
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GROTATE) == 0) {
        g->gen_rotate = v * 2.0f - 1.0f;            /* bidirectional -1..1 */
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GSWING) == 0) {
        g->gen_swing = v;
    } else if (strcmp(key, PK_GRV_GWAVE) == 0) {
        /* enum 0..5: bypass the 0..1 clamp at top of function */
        int idx = (int)(parse_f(val) + 0.5f);
        g->gen_wave = idx < 0 ? 0 : (idx >= NUM_WAVES ? NUM_WAVES - 1 : idx);
    } else if (strcmp(key, PK_GRV_GFOLD) == 0) {
        g->gen_fold = v;
    } else if (strcmp(key, PK_GRV_GRETRIG) == 0) {
        int m = (int)(parse_f(val) + 0.5f);
        g->gen_retrig = (m < 0) ? 0 : (m > 5 ? 5 : m);
    }
    /* Unknown keys ignored. */
}
