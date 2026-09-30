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
typedef struct groove_state {
    float buf_l[GRV_DELAY_LEN];   /* circular delay rings (~512 KB each) */
    float buf_r[GRV_DELAY_LEN];
    unsigned write_pos;

    /* Tempo clock (GRV-02) — the ONLY tap-interval source. */
    double prev_beat;
    bool   have_prev_beat;
    float  bpm_smooth;            /* one-pole EMA of the derived BPM */
    float  last_bpm;              /* last BPM the interval was locked to */
    int    samples_per_16th;      /* current 16th-note tap spacing (frames) */

    /* Page-1 params (control-rate). */
    float vol;                    /* VOL 0..1 */
    float tap_level[4];           /* TAP1..4 level 0..1 */
    float tap_decay[4];           /* LENGTH-derived per-tap decay weights */
    float color_g;               /* TPT COLOR coefficient tanf(pi*fc/SR) */
    float color_lp_l_s;          /* tpt1 lowpass state (the tpt1_t `.s`), L */
    float color_lp_r_s;          /* tpt1 lowpass state (the tpt1_t `.s`), R */
    bool  mono;                   /* MONO force-sum toggle (GRV-05) */

    /* --- Redesign (C1, GRVX-01/02) ---------------------------------------
     * type: 0 = TAPS (feedback multitap rumble), 1 = GEN (generative groove).
     * The feedback loop turns the dry 4-tap echo into a CONTINUOUS resonant
     * rumble (fixes "bit-crushed & quiet"): energy recirculates through the ring
     * at the 16th-note interval, darkened by a one-pole LP in the loop and
     * bounded by a gentle saturator. fb_amount comes from LENGTH. */
    int   type;                   /* GROOVE_TYPE_TAPS / _GEN */
    float fb_amount;              /* feedback gain 0..~0.9 (from LENGTH) */
    float fb_lp_l_s, fb_lp_r_s;   /* one-pole LP state in the feedback path */

    /* --- Groove FX (C1-02, GRVX-03) — TAPS Page 2 ------------------------- */
    float drive;                  /* DRIVE 0..1 (saturation + makeup) */
    int   filter_type;            /* 0 LP (COLOR), 1 HP, 2 Off */
    float lfo_phase;              /* tremolo LFO phase [0,1) */
    float lfo_inc;               /* per-sample phase increment (from LFO SPD) */
    float lfo_amt;                /* LFO depth 0..1 */
    /* Cheap mono Schroeder reverb: 2 combs + 1 allpass. Buffers by value. */
    float rv_comb1[1557], rv_comb2[1617], rv_ap[556];
    int   rv_c1i, rv_c2i, rv_api;
    float rv_c1_lp, rv_c2_lp;     /* comb damping LP state */
    float rv_fb;                  /* comb feedback (from DECAY) */
    float rv_damp;                /* comb damping coeff (from TONE) */
    float rv_mix;                 /* dry/wet mix */

    /* --- GEN groove voice (C1-03, GRVX-04/05) — decoupled from the kick model.
     * A transport-clocked, scale-quantized step sequencer driving a wavetable
     * oscillator + wavefolder, feeding the shared groove FX tail. Runs when
     * type==GEN regardless of which kick model is selected. */
    bool  gen_unquantized;        /* true when SCALE = Unquantized (UI idx 0) */
    int   gen_scale;              /* SCALE index into g_scales (0..NUM_SCALES-1) */
    float gen_root_param;         /* raw 0..1: note 0..83 (quantized) or 30..200 Hz (unquantized) */
    int   gen_range;              /* sequence degree span 1..24 */
    int   gen_seqlen;             /* SEQ LEN 1..32 (16th steps) */
    int   gen_wave;               /* WAVE type index into g_wavetables */
    int   gen_retrig;             /* RETRIG: 0 none,1/2/4/8 bars,5 on-note */
    float gen_density;            /* DENSITY 0..1 (Euclidean gate) */
    float gen_rotate;             /* ROTATE -1..1 (bidirectional step rotate) */
    float gen_swing;              /* SWING 0..1 */
    float gen_fold;               /* WAVEFOLDER 0..1 */
    float gen_base_hz;            /* base pitch (Hz) for degree 0 */
    unsigned long long gen_rng;   /* xorshift64 PRNG (seeded from SEED) */
    unsigned gen_seed_raw;        /* raw SEED (for reseed-on-change) */
    signed char gen_seq[32];      /* per-step scale degree */
    unsigned char gen_gate[32];   /* per-step on/off (Euclidean) */
    int   gen_step;               /* current step index */
    int   gen_step_ctr;           /* samples remaining in the current step */
    float gen_osc_phase;          /* oscillator phase [0,1) */
    float gen_env;                /* per-note amplitude env (decaying) */
    float gen_env_coef;           /* env decay coefficient */
    float gen_freq;               /* current note frequency (Hz) */
    int   gen_bar16;              /* 16th counter for bar-based retrigger */
    bool  gen_running;            /* transport running (stop when it stops) */
} groove_state_t;

enum { GROOVE_TYPE_TAPS = 0, GROOVE_TYPE_GEN = 1 };
enum { GRV_FILT_LP = 0, GRV_FILT_HP = 1, GRV_FILT_OFF = 2 };
enum { GRV_RETRIG_NONE = 0, GRV_RETRIG_1BAR, GRV_RETRIG_2BAR,
       GRV_RETRIG_4BAR, GRV_RETRIG_8BAR, GRV_RETRIG_NOTE };

/* Rebuild the GEN sequence from SEED/SEQ LEN/DENSITY/ROTATE (control-rate). */
void groove_gen_rebuild(groove_state_t *g);
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
