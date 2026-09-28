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

/* --- Host ABI (Context/01 lines 30-50) — VERBATIM ---------------------- */
typedef struct {
    int api_version;             /* = 1 */
    int sample_rate;             /* Fixed at 44100 Hz */
    int frames_per_block;        /* typically 128 */
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
    int (*get_clock_status)(void);
    double (*get_beat_position)(void); /* 24-PPQN synced */
} host_api_v1_t;

/* --- Plugin ABI (Context/01 lines 54-66) — VERBATIM -------------------- */
typedef struct {
    int api_version;             /* = 2 */
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void  (*destroy_instance)(void *instance);
    void  (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void  (*set_param)(void *instance, const char *key, const char *val);
    int   (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    void  (*render_block)(void *instance, int16_t *out_lr, int frames);
} plugin_api_v2_t;

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

#endif /* OMEGA_H */
