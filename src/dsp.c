/* dsp.c — Omega plugin entry point + the 6 vtable functions.
 *
 * DISPATCH ONLY (D-01): no DSP math lives here. This TU:
 *   - exports the single symbol move_plugin_init_v2 (FNDTN-01),
 *   - creates/destroys the instance with exactly one calloc/free (FNDTN-03),
 *   - routes on_midi note-ons to the active model's trigger,
 *   - routes set_param kick keys to fm2_set_param; model/master handled here,
 *   - serves get_param("ui_hierarchy") (A-03 owns the real hierarchy),
 *   - renders through the vtable with FPCR flush-to-zero (FNDTN-05) and the
 *     clamped int16 boundary (FNDTN-07).
 *
 * All six entry points run on the SCHED_FIFO audio thread; the single calloc in
 * create_instance is the one permitted allocation (CLAUDE.md single-alloc).
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- Host handle + returned plugin vtable -------------------------------- */
static const host_api_v1_t *g_host = NULL;
static plugin_api_v2_t g_api;

/* Malloc-trap gate (D-13). Under the trap build the symbol is owned by
 * malloc_trap.c; otherwise provide a local definition so both builds link. */
#ifdef OMEGA_MALLOC_TRAP
extern bool g_audio_thread_active;
#else
static bool g_audio_thread_active;
#endif

/* ---- FPCR flush-to-zero (FNDTN-05); no-op off aarch64 -------------------- */
static inline void omega_set_ftz(void) {
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1u << 24);   /* FZ: flush subnormals to zero (covers DAZ on AArch64) */
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
#endif
}

/* ---- Locale-independent float parse (UI-01: no libc string->float) ------- */
static float dsp_parse_f(const char *s) {
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

/* ---- ui_hierarchy provider ----------------------------------------------- */
/* A-03's src/ui.c owns the real omega_build_ui (declared in omega.h, gated by
 * OMEGA_HAS_UI). The temporary A-02 fallback has been removed; dsp.c only calls
 * through the omega.h declaration now. */

/* ---- D-10 buf_len measurement spike -------------------------------------- */
/* SPIKE (D-10): one-shot buf_len measurement to unblock Phase E hierarchy
 * sizing. host->log on the audio thread violates the no-log rule (A-RESEARCH
 * Pitfall 1 / Open Q4) — REMOVE or gate behind a debug build flag before ship.
 * Formats "ui_buflen=<v>" with manual digit extraction (no snprintf/atof/locale
 * dependency; UI-01 / CLAUDE.md). dst must hold at least 32 bytes. */
static void omega_itoa_msg(char *dst, int v) {
    static const char prefix[] = "ui_buflen=";
    int i = 0;
    for (const char *p = prefix; *p; p++) dst[i++] = *p;
    if (v < 0) { dst[i++] = '-'; v = -v; }
    /* Emit digits into a small reversed buffer, then copy in order. */
    char rev[10];
    int r = 0;
    do { rev[r++] = (char)('0' + (v % 10)); v /= 10; } while (v > 0 && r < 10);
    while (r > 0) dst[i++] = rev[--r];
    dst[i] = '\0';
}

/* ---- vtable functions ---------------------------------------------------- */

/* All 11 kick param keys, defaulted to mid on create. */
static const char *const k_kick_keys[] = {
    PK_PITCH, PK_LENGTH, PK_SUSTAIN, PK_CURVE, PK_ATTACK,
    PK_TRS_DEC, PK_TRS_TNE, PK_COLOR, PK_FM_RATIO, PK_FM_INDEX, PK_OP2_WAVE
};

static void *omega_create(const char *module_dir, const char *json_defaults) {
    (void)module_dir;
    (void)json_defaults;
    /* Single allocation for the whole instance (CLAUDE.md single-alloc). calloc
     * zero-inits every field, giving a deterministic denormal/NaN-free start. */
    bohm_instance_t *inst = calloc(1, sizeof(bohm_instance_t));
    if (!inst) return NULL;
    inst->model         = MODEL_FM2;
    inst->main_volume   = 1.0f;
    inst->logged_buflen = false;
    /* Prime all kick params to mid so a bare create -> trigger is audible. */
    for (size_t i = 0; i < sizeof(k_kick_keys) / sizeof(k_kick_keys[0]); i++)
        fm2_set_param(inst, k_kick_keys[i], "0.5");
    return inst;
}

static void omega_destroy(void *instance) {
    free(instance);
}

static void omega_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    (void)source;
    bohm_instance_t *inst = instance;
    /* Trigger only on note-on with velocity > 0 (Pitfall 5): vel-0 note-ons and
     * note-offs and other status bytes are ignored. */
    if (len >= 3 && (msg[0] & 0xF0) == 0x90 && msg[2] > 0) {
        g_models[inst->model]->trigger(inst, msg[1], msg[2]);
    }
}

static void omega_set_param(void *instance, const char *key, const char *val) {
    bohm_instance_t *inst = instance;
    if (strcmp(key, PK_MODEL) == 0) {
        int m = (int)dsp_parse_f(val);
        if (m < 0) m = 0;
        if (m >= MODEL_COUNT) m = MODEL_COUNT - 1;
        inst->model = (model_id_t)m;
    } else if (strcmp(key, PK_MASTER_VOL) == 0) {
        float v = dsp_parse_f(val);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        inst->main_volume = v;
    } else {
        /* All kick keys (Page 1 + Page 2) route to the FM2 param dispatch. */
        fm2_set_param(inst, key, val);
    }
}

static int omega_get_param(void *instance, const char *key, char *buf, int buf_len) {
    bohm_instance_t *inst = instance;
    if (strcmp(key, PK_UI_HIER) == 0) {
        /* SPIKE (D-10): log the host-supplied buf_len exactly once, guarded by
         * inst->logged_buflen. host->log on the audio thread is a deliberate
         * one-shot diagnostic (A-RESEARCH Pitfall 1 / Open Q4) — REMOVE or gate
         * behind a debug build flag before ship. */
        if (!inst->logged_buflen && g_host && g_host->log) {
            char m[32];
            omega_itoa_msg(m, buf_len);
            g_host->log(m);
            inst->logged_buflen = true;
        }
        return omega_build_ui(inst, buf, buf_len);
    }
    return -1;   /* unhandled key (Pitfall 4) */
}

static void omega_render_block(void *instance, int16_t *out_lr, int frames) {
    g_audio_thread_active = true;          /* D-13 malloc-trap gate */
    omega_set_ftz();                       /* FPCR FZ (FNDTN-05) */
    bohm_instance_t *inst = instance;

    if (frames > OMEGA_MAX_BLOCK) frames = OMEGA_MAX_BLOCK;   /* margin guard */

    float l[OMEGA_MAX_BLOCK], r[OMEGA_MAX_BLOCK];
    g_models[inst->model]->render(inst, l, r, frames);        /* dispatch (KICK-01) */

    for (int n = 0; n < frames; n++) {
        out_lr[n * 2]     = omega_to_i16(l[n] * inst->main_volume);   /* FNDTN-07 */
        out_lr[n * 2 + 1] = omega_to_i16(r[n] * inst->main_volume);
    }
    g_audio_thread_active = false;
}

/* ---- Entry point (the single exported symbol) ---------------------------- */
plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
    __attribute__((visibility("default")));
plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;                         /* stash for sample_rate/log/transport */
    g_api.api_version     = 2;
    g_api.create_instance  = omega_create;
    g_api.destroy_instance = omega_destroy;
    g_api.on_midi          = omega_on_midi;
    g_api.set_param        = omega_set_param;
    g_api.get_param        = omega_get_param;
    g_api.render_block     = omega_render_block;
    return &g_api;
}
