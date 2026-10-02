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

/* ---- Mono Schroeder reverb helper (vhr §B.2, ONE instance, pre OR post) ----
 * Runs the existing 2-comb + 1-allpass Schroeder on a mono input and returns
 * the mono wet. Called at the PRE point (on the kick) OR the POST point (on the
 * tap sum) — never both duplicated. NO transcendental; buffers wrap by branch
 * (non-power-of-two sizes), never `%` per sample. */
static inline float groove_reverb_mono(groove_state_t *g, float in) {
    /* RV TYPE scales the effective comb/allpass lengths (set at control rate),
     * giving Room/Hall/Plate flavors at the SAME per-sample cost. Damping is the
     * LFO-modulated rv_damp_eff. Lengths are always ≤ the fixed buffer sizes. */
    float damp = g->rv_damp_eff;
    float c1 = g->rv_comb1[g->rv_c1i];
    g->rv_c1_lp = c1 * (1.0f - damp) + g->rv_c1_lp * damp;
    g->rv_comb1[g->rv_c1i] = in + g->rv_c1_lp * g->rv_fb;
    if (++g->rv_c1i >= g->rv_c1_len) g->rv_c1i = 0;
    float c2 = g->rv_comb2[g->rv_c2i];
    g->rv_c2_lp = c2 * (1.0f - damp) + g->rv_c2_lp * damp;
    g->rv_comb2[g->rv_c2i] = in + g->rv_c2_lp * g->rv_fb;
    if (++g->rv_c2i >= g->rv_c2_len) g->rv_c2i = 0;
    float wet = 0.5f * (c1 + c2);
    float ab = g->rv_ap[g->rv_api];              /* allpass diffusion */
    float ao = -wet + ab;
    g->rv_ap[g->rv_api] = wet + ab * 0.5f;
    if (++g->rv_api >= g->rv_ap_len) g->rv_api = 0;
    return ao;
}

static inline float tpt_g_from_hz(float fc) {
    return tanf((float)M_PI * fc / OMEGA_SR);   /* control-rate only */
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

/* REVERB block: plain dry/wet via the existing Schroeder reverb. NEVER writes to
 * the delay ring (that was the Phase-1 runaway bug); mono-in because the rumble
 * is a near-mono sub. rv_fb (comb feedback) is clamped <1 in set_param. */
static inline void groove_reverb_block(groove_state_t *g, float *l, float *r) {
    float wet = groove_reverb_mono(g, 0.5f * (*l + *r));
    *l += g->rv_mix * (wet - *l);
    *r += g->rv_mix * (wet - *r);
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
    /* Random scale degrees bounded by RANGE, CENTERED around 0 (Phase 1 GEN-PITCH):
     * offsets span roughly ±span/2 so degree 0 (the ROOT) is the most common value
     * and negative degrees play BELOW the root — the root fundamental is audible
     * and ROOT-in-Hz genuinely controls perceived pitch. scale_quantize handles
     * negative degrees; the shift is deterministic (same seed → same sequence). */
    int span = g->gen_range < 1 ? 1 : (g->gen_range > 24 ? 24 : g->gen_range);
    for (int i = 0; i < 64; i++) {
        int raw = (int)(grv_xorshift(&g->gen_rng) % (unsigned)span);  /* 0..span-1 */
        g->gen_seq[i] = (signed char)(raw - span / 2);                /* center ±span/2 */
    }
    /* Euclidean gate: DENSITY -> npulses of len, spread evenly (Bresenham). */
    int npulse = (int)(g->gen_density * (float)len + 0.5f);
    if (npulse < 1) npulse = 1; if (npulse > len) npulse = len;
    int bucket = 0;
    for (int i = 0; i < 64; i++) {
        if (i < len) { bucket += npulse; if (bucket >= len) { bucket -= len; g->gen_gate[i] = 1; } else g->gen_gate[i] = 0; }
        else g->gen_gate[i] = 0;
    }
    /* ROTATE: bidirectional rotate of the gate pattern within [0,len). */
    int rot = (int)(g->gen_rotate * (float)len);
    if (rot != 0 && len > 0) {
        unsigned char tmp[64];
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
    /* Makeup gain so the FIR rumble is level-competitive with the kick at a
     * musical default VOL without DRIVE (ONDEVICE #3 "way too quiet"). */
    g->rumble_makeup = 2.0f;
    groove_set_length(g, 0.5f);          /* seeds tap_tau_s + tap_w[] + tap_norm */
    g->color_g       = tpt_g_from_hz(8000.0f);
    g->color_lp_l_s  = 0.0f;
    g->color_lp_r_s  = 0.0f;
    g->color_lp2_l_s = 0.0f;
    g->color_lp2_r_s = 0.0f;
    g->mono          = false;

    /* Redesign defaults (Phase 1: feedback-free FIR). */
    g->type        = GROOVE_TYPE_TAPS;
    /* FX routing: default RUMBLE→DRIVE→REVERB (reverb last, classic); reverb MIX
     * off on a bare create. */
    g->route_order[0] = BLK_RUMBLE;
    g->route_order[1] = BLK_DRIVE;
    g->route_order[2] = BLK_REVERB;
    g->rv_mix      = 0.0f;

    /* Groove FX defaults (C1-02) — off/neutral (calloc already zeroed buffers). */
    g->drive       = 0.0f;
    g->filter_type = GRV_FILT_LP;
    g->lfo_phase   = 0.0f;
    g->lfo_inc     = 0.5f / OMEGA_SR;   /* ~0.5 Hz at LFO SPD default */
    g->lfo_amt     = 0.0f;
    g->rv_fb       = 0.7f;
    g->rv_damp     = 0.3f;
    g->rv_damp_eff = 0.3f;
    g->rv_type     = 1;                 /* Hall (full-length) default */
    g->rv_c1_len   = 1557;
    g->rv_c2_len   = 1617;
    g->rv_ap_len   = 556;

    /* GEN groove voice defaults (C1-03 / E3). */
    g->gen_unquantized = false;
    g->gen_scale   = 1;            /* major by default (UI idx 2 = Chromatic→0, Major→1) */
    g->gen_root_param = 0.18f;     /* unified log root → ~45 Hz sub-bass default */
    g->gen_range   = 12;           /* 12 degrees ≈ one octave of sequence variation */
    g->gen_seqlen  = 16;
    g->gen_wave    = 0;
    g->gen_wave_pos = 0.0f;        /* sine end of the scan */
    g->gen_retrig  = GRV_RETRIG_NONE;   /* free-run by default (GRVX-05) */
    g->gen_density = 0.6f;
    g->gen_rotate  = 0.0f;
    g->gen_decay   = 0.5f;         /* medium gen-note length */
    g->gen_fold    = 0.0f;
    g->gen_base_hz = 45.0f;        /* set from gen_root_param (unified log map) */
    g->gen_osc_phase = 0.0f;
    g->gen_sub_phase = 0.0f;       /* Finding 3: built-in sub-octave oscillator */
    g->gen_seed_raw = 12345u;
    g->gen_env_coef = 0.9995f;     /* recomputed from gen_decay in set_param */
    /* GEN filter envelope + 3rd-pole resonant cascade (Finding 4). */
    g->gen_filt_env      = 0.0f;
    g->gen_filt_env_coef = 0.9992f;   /* recomputed from gen_decay (0.65*tau) */
    g->gen_filt_lp3_l_s  = 0.0f;
    g->gen_filt_lp3_r_s  = 0.0f;
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
        /* FIR tap ages depend on spq_target → recompute the decay weights at the
         * new tempo (control rate, once per re-lock; never per sample). */
        groove_update_tap_weights(g);
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
                int len = g->gen_seqlen < 1 ? 1 : (g->gen_seqlen > 64 ? 64 : g->gen_seqlen);
                int step = g->gen_step % len;
                if (g->gen_gate[step]) {
                    int semi = g->gen_unquantized
                        ? (int)g->gen_seq[step]   /* raw chromatic offset when unquantized */
                        : scale_quantize(g->gen_scale, g->gen_seq[step]);
                    g->gen_freq = g->gen_base_hz * powf(2.0f, (float)semi / 12.0f);  /* per-step only */
                    g->gen_env = 1.0f;
                    g->gen_filt_env = 1.0f;    /* Finding 4: open the filter env on trigger */
                    g->gen_osc_phase = 0.0f;
                    g->gen_sub_phase = 0.0f;   /* Finding 3: re-zero the sub-octave phase on trigger */
                }
                int dur = g->samples_per_16th;   /* even 16th grid (SWING removed; DECAY shapes notes) */
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
        /* WAVE SCAN (continuous): gen_wave_pos 0..1 morphs across the factory
         * tables sine→tri→saw→square→digital→analog by crossfading adjacent bands
         * (no discrete jumps). The upper third also adds auto-wavefold for extra
         * harmonic "beef" (FM/fold character) on top of the FOLD knob. */
        float wp = g->gen_wave_pos * (float)(NUM_WAVES - 1);
        int   wa = (int)wp; if (wa < 0) wa = 0; if (wa > NUM_WAVES - 2) wa = NUM_WAVES - 2;
        float wf = wp - (float)wa;
        float osc = wt_read_bl(wa, 0, g->gen_osc_phase)
                  + wf * (wt_read_bl(wa + 1, 0, g->gen_osc_phase) - wt_read_bl(wa, 0, g->gen_osc_phase));
        /* Finding 3: built-in SUB-OCTAVE (no new UI param). Read the SAME morphed
         * wavetable position one octave below the fundamental (half phase
         * increment) and sum at a fixed blend. Added BEFORE the wavefolder/env so
         * the existing fold headroom + gen_env bound the summed signal; the sub is
         * an octave LOWER so it cannot alias at the top of the ROOT range. */
        float sub = wt_read_bl(wa, 0, g->gen_sub_phase)
                  + wf * (wt_read_bl(wa + 1, 0, g->gen_sub_phase) - wt_read_bl(wa, 0, g->gen_sub_phase));
        osc = osc + 0.45f * sub;
        /* Auto-fold ramps in over the top of the scan (0 below 0.6, up to ~0.5 at
         * the top) for beefy wavefolded character, additive to the FOLD knob. */
        float autofold = g->gen_wave_pos > 0.6f ? (g->gen_wave_pos - 0.6f) * 1.25f : 0.0f;
        float foldamt = g->gen_fold + autofold;
        if (foldamt > 1.0f) foldamt = 1.0f;
        if (foldamt > 0.0f) {
            float d = 1.0f + foldamt * 3.0f;
            float v = osc * d;
            v = v - 4.0f * floorf(v * 0.25f + 0.5f);   /* fold into ~[-2,2] */
            osc = osc + foldamt * (0.5f * v - osc);
        }
        float s = osc * g->gen_env;
        g->gen_env *= g->gen_env_coef;
        g->gen_filt_env *= g->gen_filt_env_coef;   /* Finding 4: filter env decays (0.65*amp tau) */
        /* One divide for both oscillators: fundamental at inc, sub at 0.5*inc. */
        float inc = g->gen_freq / OMEGA_SR;
        g->gen_osc_phase += inc;
        if (g->gen_osc_phase >= 1.0f) g->gen_osc_phase -= 1.0f;
        g->gen_sub_phase += 0.5f * inc;
        if (g->gen_sub_phase >= 1.0f) g->gen_sub_phase -= 1.0f;
        gl = gr = s;
        /* Keep the ring write-head advancing so a later TAPS switch is coherent. */
        g->buf_l[g->write_pos] = 0.0f; g->buf_r[g->write_pos] = 0.0f;
        g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;
    } else {
        /* TAPS (Phase 1: feedback-free FIR rumble). Write ONLY the raw kick into
         * the ring; sum NTAPS ghost copies read at k·spq (16th-note) offsets, each
         * weighted by the precomputed decay envelope tap_w[k-1] (control rate),
         * then equal-power makeup by tap_norm. There is NO recirculation: the
         * output is a finite weighted sum of past RAW kick samples (|ring| ≤
         * kick_peak, weights ∈ [0,1]) → bounded by construction, cannot run away.
         * Per sample: only MACs + floorf (single aarch64 instruction), no
         * transcendental, no division. */

        /* (1) Per-sample slew of the fractional 16th spacing. */
        g->spq += (g->spq_target - g->spq) * SPQ_SLEW;
        float spq = g->spq;
        if (spq < 1.0f) spq = 1.0f;                  /* guard read distance */
        float fmaxspq = (float)((int)(GRV_DELAY_MASK) / NTAPS);
        if (spq > fmaxspq) spq = fmaxspq;            /* NTAPS*spq inside the ring */

        /* (2) Sum NTAPS decay-enveloped ghost taps at k·spq behind the write head
         * (fractional, linear-interpolated, mask-wrapped). */
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

        /* (3) Write the RAW kick ONLY — no feedback term ever enters the ring. */
        g->buf_l[g->write_pos] = kick_l;
        g->buf_r[g->write_pos] = kick_r;
        g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;

        gl = acc_l * g->tap_norm;
        gr = acc_r * g->tap_norm;
    }

    /* FILTER: the COLOR LP gives LP; HP = input - LP; Off is a TRUE bypass (no
     * attenuation, no phase shift) so the clean-copies gate can neutralise the
     * output LP via GRV_FILTYPE=OFF. TAPS uses a 2-pole cascade (12 dB/oct — the
     * "dial in darkness" authority, vhr §C.1); the GEN branch keeps the original
     * 1-pole behaviour BYTE-IDENTICAL (constraint 6). The LP state is always
     * advanced so toggling type is click-free. */
    /* LFO (Phase 1 iter-2): the groove LFO now sweeps the COLOR/LPF cutoff AND the
     * reverb tone TOGETHER (replaces the old tremolo). Triangle, rate = LFO SPD,
     * depth = LFO AMT. Cheap — scales the already-computed coefficients, no
     * per-sample transcendental. When LFO AMT = 0, cg == color_g and rv_damp_eff
     * == rv_damp (byte-identical to the unmodulated path). */
    float cg = g->color_g;
    g->rv_damp_eff = g->rv_damp;
    if (g->lfo_amt > 0.0f) {
        float ph  = g->lfo_phase;
        float tri = (ph < 0.5f) ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph);   /* -1..1 */
        cg = g->color_g * (1.0f + 0.9f * g->lfo_amt * tri);
        if (cg < 1e-4f) cg = 1e-4f; else if (cg > 0.99f) cg = 0.99f;
        float dm = g->rv_damp + 0.4f * g->lfo_amt * tri;
        g->rv_damp_eff = dm < 0.0f ? 0.0f : (dm > 0.95f ? 0.95f : dm);
    }

    if (g->filter_type == GRV_FILT_OFF) {
        /* True bypass: still advance the LP state(s) so re-enabling is click-free. */
        tpt1_t lpl = { g->color_lp_l_s };
        tpt1_t lpr = { g->color_lp_r_s };
        (void)tpt1_lp(&lpl, gl, cg);
        (void)tpt1_lp(&lpr, gr, cg);
        g->color_lp_l_s = lpl.s;
        g->color_lp_r_s = lpr.s;
        if (g->type == GROOVE_TYPE_TAPS) {
            tpt1_t l2 = { g->color_lp2_l_s }, r2 = { g->color_lp2_r_s };
            (void)tpt1_lp(&l2, gl, cg);
            (void)tpt1_lp(&r2, gr, cg);
            g->color_lp2_l_s = l2.s; g->color_lp2_r_s = r2.s;
        }
        /* leave gl/gr unfiltered */
    } else if (g->type == GROOVE_TYPE_TAPS) {
        /* 2-pole cascade LP (two tpt1 stages sharing the COLOR coefficient). */
        tpt1_t lpl = { g->color_lp_l_s },  lpr = { g->color_lp_r_s };
        float s1l = tpt1_lp(&lpl, gl, cg);
        float s1r = tpt1_lp(&lpr, gr, cg);
        g->color_lp_l_s = lpl.s; g->color_lp_r_s = lpr.s;
        tpt1_t l2 = { g->color_lp2_l_s }, r2 = { g->color_lp2_r_s };
        float lo_l = tpt1_lp(&l2, s1l, cg);
        float lo_r = tpt1_lp(&r2, s1r, cg);
        g->color_lp2_l_s = l2.s; g->color_lp2_r_s = r2.s;
        if (g->filter_type == GRV_FILT_LP)      { gl = lo_l;      gr = lo_r; }
        else /* GRV_FILT_HP */                  { gl = gl - lo_l; gr = gr - lo_r; }
    } else {
        /* GEN (Finding 4): 3-pole (18 dB/oct) RESONANT TPT cascade with a per-note
         * filter-env cutoff lift. Three tpt1 LP stages share the env-modulated
         * COLOR coefficient cg_eff; resonance feeds the 3rd-stage output back into
         * the first stage input scaled by a fixed k (~0.2 → modest Q). The feedback
         * sum is hard-clamped to [-2,2] so the resonant corner stays bounded (no
         * runaway). The filter env opens the cutoff ~20% at onset:
         *   cg_eff = cg * (1 + 0.2*gen_filt_env), clamped to the stable tpt range.
         * All per-sample work is algebraic (adds/mults + one clampf) — RT-safe. */
        /* Finding 4 (iter-2, on-device feedback "filter env + resonance need to be
         * stronger"): the per-note filter env now opens the cutoff up to ~6x at
         * onset (was 1.2x) for a pronounced downward sweep, and the resonance
         * feedback is raised to ~0.45 for an audible resonant peak. The feedback
         * input stays clamped to [-2,2] so the resonant corner cannot run away. */
        float cg_eff = cg * (1.0f + 3.0f * g->gen_filt_env);
        if (cg_eff < 1e-4f) cg_eff = 1e-4f; else if (cg_eff > 0.99f) cg_eff = 0.99f;
        const float kres = 0.45f;                /* resonance feedback (~45% Q) */

        tpt1_t l1 = { g->color_lp_l_s },  r1 = { g->color_lp_r_s };
        tpt1_t l2 = { g->color_lp2_l_s }, r2 = { g->color_lp2_r_s };
        tpt1_t l3 = { g->gen_filt_lp3_l_s }, r3 = { g->gen_filt_lp3_r_s };

        /* Resonant feedback from the PREVIOUS 3rd-stage output (one-sample delay). */
        float inl = gl - kres * g->gen_filt_lp3_l_s;
        float inr = gr - kres * g->gen_filt_lp3_r_s;
        inl = clampf(inl, -2.0f, 2.0f);
        inr = clampf(inr, -2.0f, 2.0f);

        float s1l = tpt1_lp(&l1, inl, cg_eff);
        float s1r = tpt1_lp(&r1, inr, cg_eff);
        float s2l = tpt1_lp(&l2, s1l, cg_eff);
        float s2r = tpt1_lp(&r2, s1r, cg_eff);
        float lo_l = tpt1_lp(&l3, s2l, cg_eff);
        float lo_r = tpt1_lp(&r3, s2r, cg_eff);

        g->color_lp_l_s  = l1.s; g->color_lp_r_s  = r1.s;
        g->color_lp2_l_s = l2.s; g->color_lp2_r_s = r2.s;
        g->gen_filt_lp3_l_s = l3.s; g->gen_filt_lp3_r_s = r3.s;

        if (g->filter_type == GRV_FILT_LP)      { gl = lo_l;        gr = lo_r; }
        else if (g->filter_type == GRV_FILT_HP) { gl = gl - lo_l;   gr = gr - lo_r; }
    }

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
    } else if (strcmp(key, PK_GRV_RVDECAY) == 0) {
        g->rv_fb = 0.5f + 0.49f * v;           /* comb feedback 0.5..0.99 (<1, bounded) */
    } else if (strcmp(key, PK_GRV_RVTONE) == 0) {
        g->rv_damp = 0.1f + 0.85f * (1.0f - v); /* brighter as TONE rises */
    } else if (strcmp(key, PK_GRV_RVTYPE) == 0) {
        /* RV TYPE now REAL (same per-sample cost): scales the effective comb /
         * allpass lengths. Room = tight, Hall = long, Plate = medium. Lengths are
         * always ≤ the fixed buffer sizes (1557 / 1617 / 556). */
        int t = (int)(parse_f(val) + 0.5f);
        g->rv_type = (t < 0) ? 0 : (t > 2 ? 2 : t);
        switch (g->rv_type) {
            case 0: g->rv_c1_len = 1117; g->rv_c2_len = 1277; g->rv_ap_len = 411; break; /* Room */
            case 2: g->rv_c1_len = 1327; g->rv_c2_len = 1361; g->rv_ap_len = 500; break; /* Plate */
            default:g->rv_c1_len = 1557; g->rv_c2_len = 1617; g->rv_ap_len = 556; break; /* Hall */
        }
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
        /* UNIFIED root→Hz (Phase 1 iter-2 GEN-PITCH fix): the SAME log map in BOTH
         * modes (20 Hz..2 kHz) so switching scales NEVER changes the base pitch
         * (kills the cross-contamination / "stuck high" bug) and the old quantized
         * 8.175·2^(v·83/12) that jumped to ~1 kHz is gone. In quantized mode the
         * scale only quantises the per-step melodic intervals relative to this
         * root; the root itself is a free continuous Hz. */
        g->gen_base_hz = 20.0f * powf(100.0f, g->gen_root_param);   /* 20 Hz..2 kHz log */
    } else if (strcmp(key, PK_GRV_GROOT) == 0) {
        g->gen_root_param = v;
        g->gen_base_hz = 20.0f * powf(100.0f, v);                    /* identical in both modes */
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
        g->gen_seqlen = 1 + (int)(v * 63.0f + 0.5f);   /* 1..64 */
        if (g->gen_seqlen < 1) g->gen_seqlen = 1; if (g->gen_seqlen > 64) g->gen_seqlen = 64;
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GDENSITY) == 0) {
        g->gen_density = v;
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GROTATE) == 0) {
        g->gen_rotate = v * 2.0f - 1.0f;            /* bidirectional -1..1 → ±len steps */
        groove_gen_rebuild(g);
    } else if (strcmp(key, PK_GRV_GSWING) == 0) {
        /* DECAY = gen note length (SWING removed). Finding 5: apply an EXPO input
         * curve v' = v*v BEFORE the tau map so the musically useful short/plucky
         * range spreads across most of the knob travel (mid-knob now lands much
         * shorter). Endpoints unchanged: v'=0 -> 5 ms, v'=1 -> ~150 ms. */
        g->gen_decay = v;
        /* Finding 5 (iter-2, on-device "first 50% too aggressively short"): soften
         * the pure v*v expo to v*(0.5+0.5v) = 0.5v + 0.5v². Endpoints unchanged
         * (0 ms-end -> 5 ms, 1 -> ~150 ms) but the lower half lands notably longer
         * (v=0.5: vp 0.375 vs 0.25 -> ~16 ms vs ~12 ms; v=0.25: 0.156 vs 0.0625). */
        float vp    = v * (0.5f + 0.5f * v);        /* softened expo spread */
        float tau_s = 0.005f * powf(30.0f, vp);     /* 5 ms .. ~150 ms */
        g->gen_env_coef = expf(-1.0f / (tau_s * OMEGA_SR));
        /* Finding 4: filter env decays at 0.65× the amp tau (closes BEFORE the
         * note fully decays). Control-rate expf alongside the amp coef. */
        float filt_tau_s = 0.65f * tau_s;
        g->gen_filt_env_coef = expf(-1.0f / (filt_tau_s * OMEGA_SR));
    } else if (strcmp(key, PK_GRV_GWAVE) == 0) {
        /* Continuous WAVE SCAN 0..1 (not discrete): morphs sine→…→analog + fold. */
        g->gen_wave_pos = v;
    } else if (strcmp(key, PK_GRV_GFOLD) == 0) {
        g->gen_fold = v;
    } else if (strcmp(key, PK_GRV_GRETRIG) == 0) {
        int m = (int)(parse_f(val) + 0.5f);
        g->gen_retrig = (m < 0) ? 0 : (m > 5 ? 5 : m);
    }
    /* Unknown keys ignored. */
}
