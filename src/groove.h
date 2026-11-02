/* groove.h — Groove rumble voice (Phase C, GRV-01/02/03/05).
 *
 * Defines the NEW groove_state_t (Phase C owns it; NOT part of the locked host
 * ABI). This state lives BY VALUE on bohm_instance so the single create_instance
 * calloc grows to cover it (DC-01/DC-08, CLAUDE.md single-allocation strategy).
 *
 * INCLUDE-CYCLE NOTE: dsp_primitives.h includes omega.h, and omega.h includes
 * THIS header (to place groove_state_t on bohm_instance by value). To avoid a
 * cycle, groove.h does NOT include dsp_primitives.h — it stores the TPT COLOR
 * lowpass state as bare `float` fields (the tpt1_t is a single `float s`), and
 * groove.c (which DOES include dsp_primitives.h) applies the exact tpt1_lp math
 * to them. This keeps omega.h free of a dsp_primitives.h dependency while the
 * delay rings still sit by value inside the one calloc.
 */
#ifndef OMEGA_GROOVE_H
#define OMEGA_GROOVE_H

#include <stdbool.h>

/* Power-of-two circular delay ring (~3 s @44.1k). GRV_DELAY_MASK gives a
 * branch-free `& mask` wrap in the hot loop (DC-01 — never `% LEN`). */
#define GRV_DELAY_LEN  131072u
#define GRV_DELAY_MASK (GRV_DELAY_LEN - 1u)

/* Groove rumble state. All fields zero-initialised by the single calloc; the
 * tempo clock + Page-1 params are then seeded to musical middles by groove_init
 * so the very first render block already has a valid tap interval (no div-by-0).
 */
#define GRV_DLY_LEN  65536
#define GRV_DLY_MASK (GRV_DLY_LEN - 1)

typedef struct groove_state {
    float buf_l[GRV_DELAY_LEN];   /* circular delay rings (~512 KB each) */
    float buf_r[GRV_DELAY_LEN];
    float dly_buf[GRV_DLY_LEN];   /* v0.6.2 SEQ delay line, mono (~256 KB, 1.49 s) */
    unsigned write_pos;

    /* Tempo clock (GRV-02) — the ONLY tap-interval source. */
    double prev_beat;
    bool   have_prev_beat;
    float  bpm_smooth;            /* one-pole EMA of the derived BPM */
    float  last_bpm;              /* last BPM the interval was locked to */
    int    samples_per_16th;      /* current 16th-note tap spacing (frames) — GEN clock */

    /* Fractional slewed 16th-note tap spacing (TAPS redesign, vhr §A.6).
     * The TAPS branch reads at k*spq (fractional, linear-interpolated) so live
     * BPM changes GLIDE (spq slews toward spq_target) instead of jumping whole
     * samples (which clicks). The integer samples_per_16th above is kept for the
     * GEN branch verbatim. */
    float spq;                    /* current fractional samples/16th (slewed) */
    float spq_target;             /* BPM-derived target (re-locked at control rate) */

    /* Page-1 params (control-rate). */
    float vol;                    /* TAPS voice VOL 0..1 (v0.4: TAPS-only) */
    float gen_vol;                /* GEN voice VOL 0..1 (v0.4: independent, summed) */
    float tap_level[4];           /* TAP1..4 level 0..1 */
    float tap_w[4];               /* precomputed per-tap decay weights (control rate) */
    float tap_tau_s;              /* LENGTH-derived decay time constant (seconds) */
    float rumble_makeup;          /* makeup gain for level-competitiveness (ONDEVICE #3) */
    float tap_norm;               /* equal-power divisor = makeup/sqrt(Σ tap_w²) precompute */
    float color_g;               /* TPT COLOR coefficient tanf(pi*fc/SR) */
    float color_lp_l_s;          /* tpt1 lowpass state (the tpt1_t `.s`), L */
    float color_lp_r_s;          /* tpt1 lowpass state (the tpt1_t `.s`), R */
    float color_lp2_l_s;         /* 2nd cascade stage state (2-pole COLOR), L */
    float color_lp2_r_s;         /* 2nd cascade stage state (2-pole COLOR), R */
    /* TAPS high-pass (v0.4.1): a TPT 1-pole highpass applied to the taps voice
     * BEFORE the COLOR lowpass (hp = x - lp). hpf_g from grv_hpf cutoff. */
    float hpf_g;                  /* TPT coefficient for the taps HPF */
    float hpf_lp_l_s, hpf_lp_r_s; /* HPF's internal lowpass state (hp = x - lp) */
    bool  mono;                   /* MONO force-sum toggle (GRV-05) */

    /* --- Redesign (Phase 1: feedback-free FIR rumble) --------------------
     * type: 0 = TAPS (FIR decay-enveloped ghost-kick rumble), 1 = GEN.
     * TAPS writes ONLY the raw kick into the ring; rumble = a bounded sum of
     * NTAPS ghost copies read at k·spq (16th-note) offsets, each weighted by a
     * decay envelope sampled at that tap's age (tap_w[], precomputed at control
     * rate). LENGTH sets the decay time (tap_tau_s): short → distinct separated
     * ghost-kicks, long → overlapping smeared rumble. There is NO recirculating
     * feedback — the output is an FIR of past raw-kick samples, so it is bounded
     * by construction and cannot run away (the Phase 1 crackle/bit-crush fix). */
    int   type;                   /* GROOVE_TYPE_TAPS / _GEN */

    /* --- Groove FX (C1-02, GRVX-03) — TAPS Page 2 ------------------------- */
    float drive;                  /* DRIVE 0..1 (saturation + makeup) */
    int   filter_type;            /* 0 LP (COLOR), 1 HP, 2 Off */
    float lfo_phase;              /* tremolo LFO phase [0,1) */
    float lfo_inc;               /* per-sample phase increment (from LFO SPD) */
    float lfo_amt;                /* LFO depth 0..1 */
    /* Warehouse reverb (v0.6): early reflections + an 8-line Hadamard FDN with
     * in-loop grit (soft saturation + bit reduction) and an output sample-and-
     * hold decimator for the lo-fi warehouse character. Buffers by value,
     * power-of-two masked (RV_LINE_LEN). See groove.c groove_reverb_block. */
#define RV_LINES     8
#define RV_LINE_LEN  4096u
#define RV_ER_LEN    4096u
    float rv_line[RV_LINES][RV_LINE_LEN];
    unsigned rv_wi;               /* shared FDN write index (masked) */
    int   rv_len[RV_LINES];       /* per-line delay (frames, < RV_LINE_LEN - 16) */
    float rv_lp[RV_LINES];        /* per-line damping LP state */
    float rv_hp[RV_LINES];        /* per-line in-loop HP state (keeps the tail off the sub) */
    float rv_er[RV_ER_LEN];       /* early-reflection / pre-delay line */
    unsigned rv_er_i;
    int   rv_er_tap[6];           /* reflection taps (frames) — set per type */
    float rv_er_gain[6];
    int   rv_pre;                 /* FDN pre-delay (frames) */
    float rv_fb;                  /* loop gain (from DECAY) */
    float rv_damp;                /* damping LP coefficient (from TONE) */
    float rv_damp_eff;            /* per-sample LFO-modulated damping (reverb reads this) */
    float rv_hp_k;                /* in-loop HP coefficient */
    float rv_grit;                /* in-loop saturation drive 0..1 (per type) */
    float rv_bits;                /* in-loop quantiser steps (0 = off) */
    float rv_hold;                /* output S&H decimation ratio (1 = off) */
    float rv_hold_ph, rv_hold_l, rv_hold_r;
    float rv_er_mix;              /* early-reflection level */
    float rv_mod_ph;              /* slow line modulation phase */
    float rv_mod_depth;           /* frames */
    int   rv_type;                /* 0 Warehouse / 1 Concrete / 2 Tunnel / 3 Dirt */
    float rv_mix;                 /* v0.6.2: reverb RETURN level 0..1 (sends feed it) */
    /* v0.6.2 Space: sends into the shared reverb and a synced mono SEQ delay */
    float rv_s_rmb, rv_s_seq, rv_s_kick, rv_s_dly;   /* send levels */
    float rv_send;                /* this sample's reverb input (mono send bus) */
    float dly_send, dly_fb;       /* seq -> delay send, feedback (0..0.92) */
    int   dly_time;               /* 0 1/16, 1 1/8T, 2 1/8, 3 1/8D, 4 1/4, 5 1/4D */
    float dly_len, dly_len_tgt;   /* delay in samples (slewed: no clicks on change) */
    float dly_lp_a, dly_hp_a;     /* tone one-poles in the loop */
    float dly_lp, dly_hp;         /* tone filter state */
    unsigned dly_wi;              /* write index */
    float dly_tone;
    int   dly_active;             /* samples to keep the delay running after the send closes */
    float rv_in_lp;               /* input HPF state (hp = x - lp) */
    float rv_in_g;                /* input HPF coefficient */
    /* FX routing order (Phase 1 FX-ROUTE): permutation of {RUMBLE,DRIVE,REVERB}
     * applied per-sample; set at control rate from PK_GRV_ROUTE. */
    unsigned char route_order[3];

    /* --- GEN groove voice (C1-03, GRVX-04/05) — decoupled from the kick model.
     * A transport-clocked, scale-quantized step sequencer driving a wavetable
     * oscillator + wavefolder, feeding the shared groove FX tail. Runs when
     * type==GEN regardless of which kick model is selected. */
    bool  gen_unquantized;        /* true when SCALE = Unquantized (UI idx 0) */
    int   gen_scale;              /* SCALE index into g_scales (0..NUM_SCALES-1) */
    float gen_root_param;         /* legacy (unused in v0.4; kept for struct stability) */
    /* v0.4 dual root: separate controls (no dynamic descriptor swap — that corrupted
     * the host page). gen_base_hz is derived from whichever matches the mode:
     * Unquantized -> gen_root_hz; a scale -> note_to_hz(gen_root_note). */
    float gen_root_hz;            /* ROOT (Hz) control value, Unquantized mode */
    int   gen_root_note;          /* ROOT NOTE control value (MIDI 0..84), Scale mode */
    int   gen_range;              /* sequence degree span 1..24 */
    int   gen_seqlen;             /* SEQ LEN 1..64 (16th steps) */
    int   gen_wave;               /* legacy discrete WAVE index (kept for compat) */
    float gen_wave_pos;           /* WAVE scan 0..1 (continuous morph across tables + fold) */
    int   gen_retrig;             /* RESET: 0 none, 1..8 = that many bars, 9 on-note */
    float gen_density;            /* DENSITY 0..1 (Euclidean gate) */
    float gen_rotate;             /* ROTATE -1..1 (bidirectional step rotate, ±len) */
    float gen_decay;              /* DECAY 0..1 — gen amp D/R time (plucky env) */
    /* v0.4.1 batch controls */
    int   gen_notelen;            /* NOTE LEN enum index (step division) */
    float gen_notelen_mult;       /* precomputed step duration multiplier in 16ths */
    float gen_swing;              /* swing fraction -0.08..+0.08 (0 = none) */
    float gen_gentaps;            /* GEN>TAPS send amount 0..1 */
    float gen_sixteenth_acc;      /* accumulated 16th-notes since last bar reset */
    float gen_fold;               /* WAVEFOLDER 0..1 */
    float gen_base_hz;            /* base pitch (Hz) for degree 0 */
    unsigned long long gen_rng;   /* xorshift64 PRNG (seeded from SEED) */
    unsigned gen_seed_raw;        /* raw SEED (for reseed-on-change) */
    signed char gen_seq[64];      /* per-step scale degree (SEQ LEN up to 64) */
    unsigned char gen_gate[64];   /* per-step on/off (Euclidean) */
    int   gen_step;               /* current step index */
    int   gen_step_ctr;           /* samples remaining in the current step */
    float gen_osc_phase;          /* oscillator phase [0,1) */
    float gen_sub_phase;          /* sub-octave phase [0,1) — Finding 3 built-in sub */
    float gen_env;                /* per-note amp PLUCK component (fast, 1->0) */
    float gen_env_coef;           /* pluck decay coefficient (from DECAY) */
    float gen_sus;                /* per-note 50% BODY component (slower release, ->0) */
    float gen_sus_coef;           /* body release coefficient (= 2.5x pluck tau) */
    /* GEN filter envelope + 3rd-pole resonant cascade state (Finding 4).
     * The first two poles reuse color_lp_*_s + color_lp2_*_s; this adds the 3rd
     * pole (→ 18 dB/oct) plus a per-note filter env that opens the cutoff ~20% at
     * onset and decays at 0.65× the amplitude tau (closes before the note ends). */
    float gen_filt_env;           /* filter PLUCK component (mirrors amp, 65% tau) */
    float gen_filt_env_coef;      /* filter pluck coef (= 0.65 * amp pluck tau) */
    float gen_filt_sus;           /* filter 50% BODY component (mirrors amp body) */
    float gen_filt_sus_coef;      /* filter body coef (= 0.65 * amp body tau) */
    /* v0.4: GEN runs simultaneously with TAPS, so it needs its OWN filter coeff +
     * state (TAPS keeps color_g / color_lp*). gen_color_g from PK_GRV_GENFILT. */
    float gen_color_g;            /* GEN filter TPT coefficient (own cutoff) */
    float gen_filt_lp1_l_s, gen_filt_lp1_r_s;  /* GEN 1st pole state */
    float gen_filt_lp2_l_s, gen_filt_lp2_r_s;  /* GEN 2nd pole state */
    float gen_filt_lp3_l_s;       /* 3rd TPT 1-pole stage state, L */
    float gen_filt_lp3_r_s;       /* 3rd TPT 1-pole stage state, R */
    float gen_freq;               /* current note frequency (Hz) */
    int   gen_bar16;              /* 16th counter for bar-based retrigger */
    bool  gen_running;            /* transport running (stop when it stops) */
    /* --- GEN Variation (v0.6): arp-style pattern modes, bleep/stab voices and
     * per-step dice (accent / octave jump / ratchet / drop / note random) with a
     * LOOP lock that repeats the same variation every N steps (Super Arp idea). */
    int   gen_pattern;            /* 0 Random .. see GEN_PAT_* in groove.c */
    int   gen_voice;              /* GEN_V_* */
    float gen_accent;             /* accent amount 0..1 */
    float gen_octjump;            /* octave-jump probability 0..1 */
    float gen_ratchet;            /* ratchet probability 0..1 */
    float gen_drop;               /* drop probability 0..1 */
    float gen_nrand;              /* note-random probability 0..1 */
    int   gen_loop;               /* 0 = free dice, N = dice repeat every N steps */
    unsigned gen_dice_ctr;        /* steps fired since restart (dice index) */
    unsigned short gen_scale_mask;/* 12-bit pitch-class mask of the active scale */
    float gen_vel;                /* current note velocity (accent) */
    float gen_fenv_amt;           /* current note filter-env depth (accent) */
    float gen_sus_lvl;            /* per-voice filter body level */
    /* v0.6.2 amp AHD: a short attack (no click), a hold tied to the step length
     * (body), then an exponential decay from DECAY. Times are per voice. */
    float gen_amp;                /* AHD level 0..1 */
    float gen_atk_inc;            /* attack ramp per sample */
    int   gen_hold_n;             /* hold samples left */
    int   gen_stage;              /* 0 attack, 1 hold, 2 decay */
    float gen_dec_coef;           /* decay coefficient (voice x DECAY) */
    int   gen_dur;                /* samples in the step being fired */
    float gen_trim;               /* per-voice output level match */
    int   gen_rat_n;              /* ratchet hits left in this step */
    int   gen_rat_len;            /* frames between ratchet hits */
    int   gen_rat_ctr;            /* frames to next ratchet hit */
    float gen_ph2, gen_ph3;       /* stab voice extra oscillator phases */
    float gen_blip;               /* bleep pitch blip (decays to 0) */
    /* v0.6.1 beat lock: when the host reports a running beat position, GEN steps are
     * the exact 16th crossings of that position (no free-running counter to drift). */
    int    gen_locked;            /* 1 = this block's steps come from the host beat */
    double gen_beat;              /* beat position of the next sample (snapped each block) */
    double gen_bpf;               /* beats advanced per sample */
    long   gen_abs_step;          /* last fired absolute step (beat*4/notelen units) */
    long   gen_origin;            /* absolute step that plays pattern step 0 */
    int    gen_restart_pending;   /* restart requested (On Note): re-anchor the origin */
    int   gen_cur_step;           /* last fired step (for the UI playhead) */
    float gen_kres;               /* filter resonance for the current voice */
    float gen_vgain;              /* per-voice makeup gain */
    unsigned char gen_acc[64];    /* per-step accent flags (pattern accents) */
    /* v0.6 GEN voices: every voice owns two knobs, A (main control) and B (character). */
    float gen_va[9], gen_vb[9];   /* indexed by GEN_V_*; Basic = WAVE / FOLD */
    float gen_ph4;                /* 4th stab oscillator (minor 7th chord) */
    float gen_r2, gen_r3, gen_r4; /* extra-oscillator frequency ratios (Detune / Stab) */
    int   gen_nosc;               /* oscillators sounding: 1, 2 (Detune), 3-4 (Stab) */
    float gen_aenv, gen_aenv_coef;/* acid filter env (one decay, no body) */
    unsigned gen_noise;           /* xorshift32 noise state (Dirt voices) */
    float gen_nlp;                /* noise high-pass state */
    float gen_slide;              /* SLIDE dice 0..1 (acid voices only) */
    float gen_freq_tgt;           /* slide target; gen_freq glides toward it */
    float tap_drv, gen_drv;       /* RUMBLE / SEQ volume past noon: drive 0..1 */
} groove_state_t;

/* GEN voices (grv_gvoice enum order). */
enum { GEN_V_BASIC = 0, GEN_V_DETUNE, GEN_V_FM, GEN_V_ACIDSAW, GEN_V_ACIDSQ,
       GEN_V_DIRT, GEN_V_DIRT2, GEN_V_BLEEP, GEN_V_STAB, GEN_V_COUNT };

enum { GROOVE_TYPE_TAPS = 0, GROOVE_TYPE_GEN = 1 };
enum { GRV_FILT_LP = 0, GRV_FILT_HP = 1, GRV_FILT_OFF = 2 };
/* FX routing block IDs (Phase 1 FX-ROUTE). BLK_RUMBLE marks the source slot. */
enum { BLK_RUMBLE = 0, BLK_DRIVE = 1, BLK_REVERB = 2 };
/* RESET (v0.4.1, item 4): 0 = None, 1..8 = that many bars (literal index), 9 = On Note. */
enum { GRV_RETRIG_NONE = 0, GRV_RETRIG_ONNOTE = 9 };

/* Rebuild the GEN sequence from SEED/SEQ LEN/DENSITY/ROTATE (control-rate). */
void groove_gen_rebuild(groove_state_t *g);
/* UI view of the GEN pattern (read-only get_param "grv_gview"). */
int groove_gen_view(const groove_state_t *g, char *buf, int buf_len);
/* Reset the GEN sequencer to step 0 (retrigger). */
void groove_gen_restart(groove_state_t *g);

/* Groove API (implemented in src/groove.c). host_api_v1 is defined in omega.h;
 * this header is included by omega.h AFTER host_api_v1 is declared, so the type
 * is visible here. */
struct host_api_v1;

void groove_init(groove_state_t *g);
void groove_update_tempo(groove_state_t *g, const struct host_api_v1 *host, int frames);
void groove_tick(groove_state_t *g, float kick_l, float kick_r,
                 float *out_gl, float *out_gr);
void groove_set_param(groove_state_t *g, const char *key, const char *val);

#endif /* OMEGA_GROOVE_H */
