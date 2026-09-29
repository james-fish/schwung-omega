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
    void (*set_p2)(bohm_instance_t *inst, const char *key, const char *val);
    int  (*p2_slot_desc)(bohm_instance_t *inst, char *buf, int buf_len);
} kick_model_vtable_t;

/* model IDs are append-only and permanent — never renumber (KICK-01) */
typedef enum { MODEL_FM2 = 0, MODEL_COUNT } model_id_t;

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
};

_Static_assert(sizeof(struct bohm_instance) < 800000, "instance under 800KB");

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

/* Model registry — defined in model_registry.c (Plan A-02). */
extern const kick_model_vtable_t *g_models[MODEL_COUNT];

/* --- FM2 engine exports (Plan A-02, src/models/fm2.c) ------------------- */
/* The FM2 vtable (defined in fm2.c) and the Page-1 param setter that dsp.c
 * dispatches all kick keys to. dsp.c never calls DSP math directly (D-01);
 * it routes PK_* kick keys to fm2_set_param and triggers/renders via g_models. */
extern const kick_model_vtable_t g_fm2_vtable;
void fm2_set_param(bohm_instance_t *inst, const char *key, const char *val);

/* --- UI hierarchy (Plan A-03, src/ui.c) -------------------------------- */
/* Defining OMEGA_HAS_UI tells dsp.c that ui.c owns the real omega_build_ui, so
 * dsp.c drops its temporary #ifndef OMEGA_HAS_UI fallback (D-08/D-09). Assembles
 * Kick Page 1 (8 slots) + the active model's Kick Page 2 into the caller's
 * buffer with zero allocation, bounded to buf_len; returns bytes written. */
#define OMEGA_HAS_UI 1
int omega_build_ui(bohm_instance_t *inst, char *buf, int buf_len);

#endif /* OMEGA_H */
