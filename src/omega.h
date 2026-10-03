/* omega.h — Shared contracts for the Omega Schwung module.
 *
 * Single shared header (C11). Defines:
 *   - Host/plugin ABI structs (host_api_v1_t, plugin_api_v2_t) — VERBATIM from
 *     Context/01_SCHWUNG_DEV_ARCHITECTURE.md. Do NOT alter field order or types.
 *   - kick_model_vtable_t contract + model_id_t (append-only, permanent).
 *   - struct bohm_instance (shared instance layout; per-model state overlaid later).
 *   - PK_* param-key string macros for set_param/get_param dispatch (D-08).
 *   - Global constants OMEGA_SR / OMEGA_MAX_BLOCK.
 */
#ifndef OMEGA_H
#define OMEGA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --- Global constants -------------------------------------------------- */
#define OMEGA_SR        44100.0f
/* Scratch buffers are sized to OMEGA_MAX_BLOCK, comfortably above the host's
 * 128-frame block. Never size scratch to exactly 2x128 (CLAUDE.md: add margin). */
#define OMEGA_MAX_BLOCK 256
/* USR user-wavetable geometry (KICK-10). Numerically mirrors WT_LEN/WT_GUARD in
 * dsp_primitives.h (single-cycle length + 1 guard so wt_read is branch-free),
 * defined here so the bohm_instance layout does not pull in dsp_primitives.h.
 * A _Static_assert in usr.c binds these to the primitive geometry. */
#define OMEGA_WT_LEN    2048
#define OMEGA_WT_GUARD  (OMEGA_WT_LEN + 1)

/* Sample bank (B3, SMPL-01..03). A bounded set of one-shot samples enumerated
 * and loaded from module_dir/samples/ (and optionally an SD path) at
 * create_instance — the ONLY place file I/O is allowed. SAMPLE SELECT picks a
 * bank slot by index; the names feed the picker enum (SMPL-02). */
#define OMEGA_MAX_SAMPLES     8
#define OMEGA_SAMPLE_CAP      22050   /* 0.5 s @44.1k per bank slot */
#define OMEGA_SAMPLE_NAMELEN  24

/* Param-cache dimensions (B1, UIX-01/04). Sized here so the cache arrays can
 * live on bohm_instance without pulling in params.h (which includes THIS
 * header). params.c binds these to the pk_kick_index_t / pk_global_index_t enum
 * counts with a _Static_assert so they can never drift. */
#define OMEGA_PKI_COUNT 55   /* == PKI_COUNT (every kick param) */
#define OMEGA_GKI_COUNT 41   /* +1 FX route (P1) +3 v0.4 dual-voice (genvol/grootnote/genfilt) */

/* --- Host ABI — VERBATIM from the real schwung src/host/plugin_api_v1.h ----
 * This MUST match the host struct byte-for-byte or callback offsets shift and
 * the host/module call through garbage (device-wide crash / boot-loop). The
 * `reserved[8]` tail is load-bearing: reserved must start at +120 (a shipped
 * module over-reads there), enforced by the static_assert below. Do NOT insert
 * fields before `reserved`, and do NOT shorten the run. New host capabilities
 * arrive as dlsym'd exports, never as fields here. */
typedef int  (*move_mod_emit_value_fn)(void *ctx, const char *source_id,
                                       const char *target, const char *param,
                                       float signal, float depth, float offset,
                                       int bipolar, int enabled);
typedef void (*move_mod_clear_source_fn)(void *ctx, const char *source_id);

typedef struct host_api_v1 {
    uint32_t api_version;

    int sample_rate;
    int frames_per_block;

    /* Direct mailbox access */
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;

    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
    int (*get_clock_status)(void);

    /* Optional runtime modulation callbacks (NULL if unsupported). */
    move_mod_emit_value_fn  mod_emit_value;
    move_mod_clear_source_fn mod_clear_source;
    void *mod_host_ctx;

    float (*get_bpm)(void);
    int (*midi_inject_to_move)(const uint8_t *msg, int len);
    int (*slot_recv_channel)(void *instance);
    double (*get_beat_position)(void);

    /* Load-bearing zero-run: over-reads land on NULL and pass caller guards. */
    void *reserved[8];
} host_api_v1_t;

_Static_assert(offsetof(host_api_v1_t, reserved) == 120,
               "host_api_v1_t::reserved must start at +120 (ABI contract)");

/* --- Plugin ABI — VERBATIM from the real schwung src/host/plugin_api_v1.h --
 * get_error sits BETWEEN get_param and render_block. Omitting it put our
 * render_block at the get_error offset and left render_block reading one slot
 * past the struct — the host then called garbage (SIGSEGV 0x1000300000000). */
typedef struct plugin_api_v2 {
    uint32_t api_version;        /* = 2 */
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void  (*destroy_instance)(void *instance);
    void  (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void  (*set_param)(void *instance, const char *key, const char *val);
    int   (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int   (*get_error)(void *instance, char *buf, int buf_len);
    void  (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;

/* Lock the vtable layout: render_block must land at +56 (after 7 slots on
 * LP64). If a slot is dropped or reordered, the host reads render_block past
 * the struct and calls garbage — this catches it at compile time. */
_Static_assert(offsetof(plugin_api_v2_t, render_block) == 56,
               "plugin_api_v2_t layout drift: render_block must be at +56");

plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);

/* --- Model dispatch contract (A-RESEARCH §Pattern 1) — LOCKED ---------- */
typedef struct bohm_instance bohm_instance_t;   /* fwd decl */

typedef struct {
    const char *name;                                   /* "FM2" */
    void (*trigger)(bohm_instance_t *inst, int note, int velocity);
    void (*render)(bohm_instance_t *inst, float *out_l, float *out_r, int frames);
    /* set_param handles both Page-1 and this model's Page-2 keys; dsp.c
     * dispatches ALL kick keys through g_models[inst->model]->set_param
     * (Pitfall 2 fix). Internal vtable — safe to extend, no _Static_assert
     * binds its layout, and it is NOT part of the host ABI. */
    void (*set_param)(bohm_instance_t *inst, const char *key, const char *val);
    void (*set_p2)(bohm_instance_t *inst, const char *key, const char *val);
    int  (*p2_slot_desc)(bohm_instance_t *inst, char *buf, int buf_len);
} kick_model_vtable_t;

/* model IDs are append-only and permanent — never renumber (KICK-01).
 * MODEL_FM2 = 0 is permanent. MODEL_COUNT becomes 10. */
typedef enum {
    MODEL_FM2 = 0, MODEL_FM4, MODEL_WTR, MODEL_PHY, MODEL_HRD,
    MODEL_DIG, MODEL_TRS, MODEL_ANA, MODEL_USR, MODEL_GEN,
    MODEL_COUNT
} model_id_t;

/* --- Groove rumble state (Phase C) ------------------------------------- */
/* groove_state_t (delay rings + tempo clock + Page-1 params + COLOR LP) is
 * defined in groove.h and placed BY VALUE on bohm_instance so the single
 * create_instance calloc grows to cover it (DC-01/DC-08). groove.h is included
 * HERE — after host_api_v1 is declared (its API decls reference it) and it does
 * NOT re-include omega.h/dsp_primitives.h, so no include cycle is created (it
 * stores the TPT COLOR state as bare floats to stay independent of
 * dsp_primitives.h). */
#include "groove.h"

/* --- Shared instance layout -------------------------------------------- */
/* The full struct lives here so downstream plans share ONE definition.
 * Phase B overlays per-model state via a union in a later plan; Phase A uses a
 * fixed opaque region (model_state) that the FM2 plan casts to its own struct.
 * The whole instance is zero-initialised by a single calloc in create_instance
 * (CLAUDE.md single-allocation strategy) — all fields start at 0/false. */
struct bohm_instance {
    model_id_t model;             /* active model (MODEL_FM2 in Phase A) */
    float      main_volume;       /* master output gain (0..1) */
    bool       logged_buflen;     /* D-10 one-shot ui_hierarchy buf_len log flag */
    char       model_state[4096]; /* per-model state region; FM2 plan overlays this */

    /* --- USR (KICK-10) user content — off-render load region -------------
     * The user WAV/wavetable does NOT fit in model_state[4096], so its storage
     * lives HERE, still inside the SINGLE calloc (B-RESEARCH Pitfall 4: grow the
     * one allocation, never a per-file malloc). All fields are hard-capped and
     * zero-initialised by the calloc; usr_loaded gates the built-in fallback so
     * USR is non-silent with no user file. Populated by a bounded one-time read
     * in create_instance (dsp.c) — the ONLY file I/O in the module. */
    float usr_wavetable[OMEGA_WT_GUARD]; /* user single-cycle wavetable + guard (~8 KB) */
    float usr_sample[44100];         /* up to 1 s user one-shot @44.1k (~176 KB) */
    int   usr_sample_len;            /* valid frames in usr_sample (0 if none) */
    bool  usr_wt_loaded;             /* a user wavetable was read */
    bool  usr_loaded;                /* any user content (sample or wavetable) */

    /* --- Groove rumble voice (Phase C) — by value in the single calloc ----
     * Two 131072-float delay rings dominate the instance footprint (~1.0 MB);
     * the whole groove_state_t stays inside the ONE calloc (DC-01/DC-08), never
     * a separate malloc. This is the member that raises the size assert below
     * from the pre-C ~0.2 MB to the true ~1.3 MB mask footprint. */
    groove_state_t groove;

    /* --- Central raw-value param cache (B1, UIX-01/04) --------------------
     * The single source of truth for readback and per-model memory. kick_cache
     * stores the last raw normalized value of every kick param PER MODEL, so a
     * model switch restores that model's knob positions; global_cache holds the
     * non-per-model keys (master/model + the 8 groove keys). set_param records
     * here at control rate; get_param formats these back; a model switch replays
     * kick_cache[model][*]. All zero from the calloc; seeded to defaults in
     * omega_create. ~2.7 KB — negligible against the ~1.24 MB groove rings. */
    float kick_cache[MODEL_COUNT][OMEGA_PKI_COUNT];
    bool  kick_cache_set[MODEL_COUNT][OMEGA_PKI_COUNT];
    float global_cache[OMEGA_GKI_COUNT];

    /* FX TONE post-kick tilt state (B2, VOICE-05). A one-pole split applied to
     * the kick voice in render: neutral at 0.5, darker below, brighter above.
     * Zeroed by the calloc. */
    float fx_tone_lp_l, fx_tone_lp_r;

    /* --- Performer chain (Phase D, PERF-01..05) --------------------------- */
    float duck_depth;             /* DUCK depth 0..1 */
    float duck_rel_coef;          /* per-sample duck-env decay (from DUCK REL) */
    float duck_smt_a;             /* gain-slew coefficient (from DUCK SMT) */
    float duck_bs_g;              /* DUCK BS crossover cutoff (tpt g) */
    float duck_env;              /* duck envelope: 1 on note-on -> 0 over release */
    float duck_gain_s;            /* smoothed duck gain state */
    float duck_bs_lp_l, duck_bs_lp_r;  /* BS crossover LP state (duck the lows) */
    float dj_g, dj_a0, dj_k;      /* DJ SVF coefficients (control rate) */
    int   dj_mode;                /* 0 LP, 1 HP, 2 bypass */
    float svf1_l, svf2_l, svf1_r, svf2_r;  /* SVF integrator states */
    bool  clip_on;                /* legacy (unused after CMPDR) */
    /* CMPDR (iter-2): one-knob comp+drive master glue (replaces CLIP). */
    float cmpdr_amt;              /* 0 = bypass .. 1 = max glue+grit */
    float cmp_thr;                /* compressor threshold (from amount) */
    float cmp_makeup;             /* makeup gain (from amount) */
    float cmp_env;                /* peak-env detector state */
    float cmp_atk, cmp_rel;       /* env attack/release coefficients */

    /* --- Sample bank (B3, SMPL-01..03) — loaded off-thread in create -------
     * Enumerated one-shots (mono, bounded). sample_count is how many loaded;
     * sample_name[i] is the display name (basename, no extension) for the
     * picker; sample_len[i] the valid frame count. ~705 KB. */
    float sample_bank[OMEGA_MAX_SAMPLES][OMEGA_SAMPLE_CAP];
    int   sample_len[OMEGA_MAX_SAMPLES];
    char  sample_name[OMEGA_MAX_SAMPLES][OMEGA_SAMPLE_NAMELEN];
    int   sample_count;
};

/* Size assert RAISED for the Phase-C groove delay rings. The two 131072-float
 * rings alone are 2*131072*4 = 1,048,576 B; plus model_state (4096) + the USR
 * buffers (usr_wavetable 2049*4 + usr_sample 44100*4 ~= 184 KB) + small fields
 * the real sizeof is ~1.24 MB. Bound set just above at 1,300,000 (DC-08's
 * 1,100,000 was illustrative; sized to the TRUE mask footprint, no padding). */
_Static_assert(sizeof(struct bohm_instance) < 2200000, "instance under 2.2MB");

/* --- Param key macros (D-08) — must match set_param/get_param dispatch -- */
#define PK_PITCH      "pitch"
#define PK_LENGTH     "length"
#define PK_SUSTAIN    "sustain"
#define PK_CURVE      "curve"
#define PK_ATTACK     "attack"
#define PK_TRS_DEC    "trs_dec"
#define PK_TRS_TNE    "trs_tne"
#define PK_COLOR      "color"
#define PK_FM_RATIO   "fm_ratio"
#define PK_FM_INDEX   "fm_index"
#define PK_OP2_WAVE   "op2_wave"
#define PK_FX_TYPE    "fx_type"
#define PK_FX_AMT     "fx_amt"
/* B2 (VOICE-04/05): shared Kick Page 2 additions. FX TONE tilts the post-FX
 * output; FILTER ROUTE selects whether COLOR (the body filter) is applied to the
 * synth, the transient, or both. COLOR is now surfaced as "FILTER" in the UI. */
#define PK_FX_TONE      "fx_tone"
#define PK_FILTER_ROUTE "filter_route"
#define PK_MODEL      "model"
#define PK_MASTER_VOL "master_vol"
#define PK_UI_HIER    "ui_hierarchy"
#define PK_STATE      "state"        /* preset save/restore: full param snapshot (JSON) */

/* --- Performer chain keys (Phase D, PERF-01..05) ----------------------- */
#define PK_DUCK      "duck"        /* sidechain duck depth */
#define PK_DUCK_REL  "duck_rel"    /* duck release time */
#define PK_DUCK_SMT  "duck_smt"    /* duck envelope smoothing (slew) */
#define PK_DUCK_BS   "duck_bs"     /* duck bass focus (crossover) */
#define PK_DJ_FILT   "dj_filt"     /* bidirectional LP<->neutral<->HP */
#define PK_DJ_RESO   "dj_reso"     /* DJ filter resonance */
#define PK_CLIP      "cmpdr"       /* iter-2: one-knob comp+drive master glue (was soft clip) */

/* --- Groove Page-1 param keys (Phase C, GRV-03/05) --------------------- */
/* Model-independent groove-voice keys; dsp.c routes these to groove_set_param
 * BEFORE the model-vtable fallback (they must NOT go through the kick model).
 * The string values MUST match the literals tests/test_groove.c uses. */
#define PK_GRV_TYPE   "grv_type"   /* C1 GRVX-01: TAPS/GEN groove type selector */
#define PK_GRV_VOL    "grv_vol"
#define PK_GRV_LENGTH "grv_length"
#define PK_GRV_COLOR  "grv_color"
#define PK_GRV_TAP1   "grv_tap1"
#define PK_GRV_TAP2   "grv_tap2"
#define PK_GRV_TAP3   "grv_tap3"
#define PK_GRV_TAP4   "grv_tap4"
#define PK_GRV_MONO   "grv_mono"

/* --- Groove FX keys (C1-02, GRVX-03) — TAPS Page 2 --------------------- */
#define PK_GRV_DRIVE   "grv_drive"
#define PK_GRV_FILTYPE "grv_filtype"
#define PK_GRV_LFOSPD  "grv_lfospd"
#define PK_GRV_LFOAMT  "grv_lfoamt"
#define PK_GRV_RVMIX   "grv_rvmix"
#define PK_GRV_RVDECAY "grv_rvdecay"
#define PK_GRV_RVTONE  "grv_rvtone"
#define PK_GRV_RVTYPE  "grv_rvtype"
#define PK_GRV_ROUTE   "grv_route"   /* Phase 1 FX-ROUTE: {RUMBLE,DRIVE,REVERB} order enum */

/* --- GEN groove-type keys (C1-03, GRVX-04/05) — decoupled from MODEL_GEN --- */
#define PK_GRV_GSCALE   "grv_gscale"
#define PK_GRV_GSEED    "grv_gseed"
#define PK_GRV_GSEQLEN  "grv_gseqlen"
#define PK_GRV_GDENSITY "grv_gdensity"
#define PK_GRV_GROTATE  "grv_grotate"
#define PK_GRV_GSWING   "grv_gswing"
#define PK_GRV_GWAVE    "grv_gwave"
#define PK_GRV_GFOLD    "grv_gfold"
#define PK_GRV_GRETRIG  "grv_gretrig"
#define PK_GRV_GROOT    "grv_groot"   /* GEN root Hz (Unquantized mode) */
#define PK_GRV_GRANGE   "grv_grange"  /* sequence degree span 1..24 (E3) */
/* v0.4 dual-voice groove: TAPS + GEN run simultaneously (no TYPE selector), each
 * with its own VOL; GEN gets a separate note-domain root + its own filter. */
#define PK_GRV_GENVOL    "grv_genvol"    /* GEN voice volume (independent of TAPS grv_vol) */
#define PK_GRV_GROOTNOTE "grv_grootnote" /* GEN root as a note index (Scale mode), MIDI 0..84 */
#define PK_GRV_GENFILT   "grv_genfilt"   /* GEN filter cutoff (own state; TAPS uses grv_color) */

/* --- Per-model Page-2 param keys (Phase B, KICK-03..11) ---------------- */
/* Each key is unique across all models (dispatch is a flat strcmp chain).
 * Model .c files own their own key handling in their set_param; unknown keys
 * are ignored (parse_f pattern), so priming a model with the shared list is
 * safe. ui.c (B-09) splices these into the active model's Kick Page 2. */

/* FM4 / OLP-4 (KICK-03) */
#define PK_FM4_ALGO     "fm4_algo"
#define PK_FM4_OPRATIO  "fm4_opratio"
#define PK_FM4_OPINDEX  "fm4_opindex"
#define PK_FM4_OPAMP    "fm4_opamp"
#define PK_FM4_FEEDBACK "fm4_feedback"
#define PK_FM4_ALGO2    "fm4_algo2"

/* WTR / HZ-1 (KICK-04) */
#define PK_WTR_WAVE      "wtr_wave"
#define PK_WTR_BODYPITCH "wtr_bodypitch"
#define PK_WTR_TRANSDEC  "wtr_transdec"
#define PK_WTR_TRANSCOL  "wtr_transcol"

/* PHY / PM-K1 (KICK-05) */
#define PK_PHY_BEATER   "phy_beater"
#define PK_PHY_SHELL    "phy_shell"
#define PK_PHY_HEADTENS "phy_headtens"
#define PK_PHY_DAMPING  "phy_damping"

/* HRD / PX-3 (KICK-06) */
#define PK_HRD_SAMPLE "hrd_sample"
#define PK_HRD_MIX    "hrd_mix"
#define PK_HRD_DRIVE  "hrd_drive"
#define PK_HRD_CRUSH  "hrd_crush"

/* DIG / SP-6 (KICK-07) */
#define PK_DIG_WAVEIDX  "dig_waveidx"
#define PK_DIG_SAMPLE   "dig_sample"
#define PK_DIG_BITDEPTH "dig_bitdepth"
#define PK_DIG_PITCHENV "dig_pitchenv"

/* TRS / VX-T (KICK-08) — note PK_TRS_CURVE is a P2 pitch-curve morph,
 * distinct from the Page-1 PK_CURVE. */
#define PK_TRS_TONE  "trs_tone"
#define PK_TRS_TDEC  "trs_tdec"
#define PK_TRS_WTCOL "trs_wtcol"
#define PK_TRS_CURVE "trs_curve"

/* ANA / WT-4 (KICK-09) */
#define PK_ANA_MORPH  "ana_morph"
#define PK_ANA_SUBLVL "ana_sublvl"
#define PK_ANA_SUBDEC "ana_subdec"
#define PK_ANA_SAMPLE "ana_sample"

/* USR / XT-88 (KICK-10) */
#define PK_USR_SAMPLE   "usr_sample"
#define PK_USR_WTMORPH  "usr_wtmorph"
#define PK_USR_LAYERVOL "usr_layervol"
#define PK_USR_PITCHENV "usr_pitchenv"

/* GEN / HPN (KICK-11 + GRV-04) — the full Groove Page 2 control set. SEED /
 * SCALE / DENSITY landed in Phase B; SEQ LEN / LPF FREQ / LPF POLE are the
 * Phase-C transport-clock + sub-bass LPF additions (all six live on the
 * conditional Groove Page 2 for GEN, DC-05). */
#define PK_GEN_SEED    "gen_seed"
#define PK_GEN_SCALE   "gen_scale"
#define PK_GEN_DENSITY "gen_density"
#define PK_GEN_SEQLEN  "gen_seqlen"
#define PK_GEN_LPFFREQ "gen_lpffreq"
#define PK_GEN_LPFPOLE "gen_lpfpole"

/* Model registry — defined in model_registry.c (Plan A-02). */
extern const kick_model_vtable_t *g_models[MODEL_COUNT];

/* --- FM2 engine exports (Plan A-02, src/models/fm2.c) ------------------- */
/* The FM2 vtable (defined in fm2.c) and the Page-1 param setter that dsp.c
 * dispatches all kick keys to. dsp.c never calls DSP math directly (D-01);
 * it routes PK_* kick keys through the active model's vtable set_param. */
extern const kick_model_vtable_t g_fm2_vtable;
void fm2_set_param(bohm_instance_t *inst, const char *key, const char *val);

/* --- Phase B model vtable exports (defined in their model .c files) ----- */
/* Declared here so model_registry.c can reference them once each model plan
 * lands (B-04..B-08). A registry designated line referencing one of these
 * before its .c defines the symbol would fail to link, so each later plan
 * adds its designated registry line together with the .c that defines it. */
extern const kick_model_vtable_t g_fm4_vtable, g_wtr_vtable, g_phy_vtable,
                                 g_hrd_vtable, g_dig_vtable, g_trs_vtable,
                                 g_ana_vtable, g_usr_vtable, g_gen_vtable;

/* --- UI hierarchy (Plan A-03, src/ui.c) -------------------------------- */
/* Defining OMEGA_HAS_UI tells dsp.c that ui.c owns the real omega_build_ui, so
 * dsp.c drops its temporary #ifndef OMEGA_HAS_UI fallback (D-08/D-09). Assembles
 * Kick Page 1 (8 slots) + the active model's Kick Page 2 into the caller's
 * buffer with zero allocation, bounded to buf_len; returns bytes written. */
#define OMEGA_HAS_UI 1
int omega_build_ui(bohm_instance_t *inst, char *buf, int buf_len);

#endif /* OMEGA_H */
