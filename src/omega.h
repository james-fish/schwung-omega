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
};

/* Size assert RAISED for the Phase-C groove delay rings. The two 131072-float
 * rings alone are 2*131072*4 = 1,048,576 B; plus model_state (4096) + the USR
 * buffers (usr_wavetable 2049*4 + usr_sample 44100*4 ~= 184 KB) + small fields
 * the real sizeof is ~1.24 MB. Bound set just above at 1,300,000 (DC-08's
 * 1,100,000 was illustrative; sized to the TRUE mask footprint, no padding). */
_Static_assert(sizeof(struct bohm_instance) < 1300000, "instance under 1.3MB");

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
#define PK_MODEL      "model"
#define PK_MASTER_VOL "master_vol"
#define PK_UI_HIER    "ui_hierarchy"

/* --- Groove Page-1 param keys (Phase C, GRV-03/05) --------------------- */
/* Model-independent groove-voice keys; dsp.c routes these to groove_set_param
 * BEFORE the model-vtable fallback (they must NOT go through the kick model).
 * The string values MUST match the literals tests/test_groove.c uses. */
#define PK_GRV_VOL    "grv_vol"
#define PK_GRV_LENGTH "grv_length"
#define PK_GRV_COLOR  "grv_color"
#define PK_GRV_TAP1   "grv_tap1"
#define PK_GRV_TAP2   "grv_tap2"
#define PK_GRV_TAP3   "grv_tap3"
#define PK_GRV_TAP4   "grv_tap4"
#define PK_GRV_MONO   "grv_mono"

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

/* GEN / HPN (KICK-11) */
#define PK_GEN_SEED    "gen_seed"
#define PK_GEN_SCALE   "gen_scale"
#define PK_GEN_DENSITY "gen_density"

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
