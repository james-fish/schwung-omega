/* dsp.c — Omega plugin entry point + the 6 vtable functions.
 *
 * DISPATCH ONLY (D-01): no DSP math lives here. This TU:
 *   - exports the single symbol move_plugin_init_v2 (FNDTN-01),
 *   - creates/destroys the instance with exactly one calloc/free (FNDTN-03),
 *   - routes on_midi note-ons to the active model's trigger,
 *   - routes set_param kick keys through the active model's vtable set_param
 *     (NULL-guarded); model/master handled here, memset re-init on switch,
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
#include <stdio.h>   /* USR (KICK-10): bounded one-time WAV read in create ONLY */

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

/* ---- USR off-render file load (KICK-10) ---------------------------------- */
/* RT-SAFETY CONTRACT (B-RESEARCH Open Question 2, recommendation 1): a BOUNDED,
 * one-time file read is permitted HERE — in create_instance — because this is
 * where the single calloc happens, OFF the hot render loop. render_block,
 * set_param, on_midi and get_param stay ABSOLUTELY file-I/O-free. No host->log
 * anywhere (get_param/render run on the SPI audio callback where logging is a
 * write() syscall that drops audio). fopen/fread/fclose appear only below.
 *
 * On any absence/failure the usr_* buffers are left zeroed and inst->usr_loaded
 * stays false; usr.c then synthesises a built-in fallback so USR is non-silent. */

static uint16_t rd_u16le(const unsigned char *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd_u32le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Read a canonical PCM16 WAV (as written by tests/wav.c) from `path` into
 * inst->usr_sample, bounded to the buffer cap. Mono-downmixes multi-channel to
 * a single one-shot. Sets usr_sample_len + usr_loaded on success. Bounded reads
 * only; ignores malformed/oversized content gracefully. */
static void usr_load_wav(bohm_instance_t *inst, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;

    unsigned char hdr[44];
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) { fclose(f); return; }
    if (memcmp(hdr + 0, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0 ||
        memcmp(hdr + 12, "fmt ", 4) != 0 || memcmp(hdr + 36, "data", 4) != 0) {
        fclose(f); return;   /* not the canonical 44-byte PCM16 layout */
    }
    uint16_t fmt      = rd_u16le(hdr + 20);   /* 1 = PCM */
    uint16_t channels = rd_u16le(hdr + 22);
    uint16_t bits     = rd_u16le(hdr + 34);
    if (fmt != 1 || bits != 16 || channels < 1 || channels > 8) { fclose(f); return; }

    const int CAP = (int)(sizeof inst->usr_sample / sizeof inst->usr_sample[0]);
    int nframes = 0;
    int16_t frame[8];
    /* Bounded loop: never write past CAP; one downmixed float per source frame. */
    while (nframes < CAP) {
        size_t got = fread(frame, sizeof(int16_t), channels, f);
        if (got != channels) break;   /* EOF or short read */
        float acc = 0.0f;
        for (int c = 0; c < channels; c++) acc += (float)frame[c] / 32768.0f;
        inst->usr_sample[nframes++] = acc / (float)channels;
    }
    fclose(f);

    if (nframes > 0) {
        inst->usr_sample_len = nframes;
        inst->usr_loaded     = true;
    }
}

/* Read a raw single-cycle float32 wavetable (OMEGA_WT_LEN samples) from `path`
 * into inst->usr_wavetable, appending the guard sample (t[WT_LEN]=t[0]) so the
 * branch-free wt_read wrap holds. Bounded to exactly one cycle. */
static void usr_load_wavetable(bohm_instance_t *inst, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    float cyc[OMEGA_WT_LEN];
    size_t got = fread(cyc, sizeof(float), (size_t)OMEGA_WT_LEN, f);
    fclose(f);
    if (got != (size_t)OMEGA_WT_LEN) return;   /* need a full cycle */
    for (int i = 0; i < OMEGA_WT_LEN; i++) inst->usr_wavetable[i] = cyc[i];
    inst->usr_wavetable[OMEGA_WT_LEN] = cyc[0];   /* guard sample */
    inst->usr_wt_loaded = true;
    inst->usr_loaded    = true;
}

/* Join module_dir + "/user/" + name into buf (bounded). Returns false on
 * overflow. No allocation; used only in create_instance. */
static bool usr_join_path(char *buf, size_t buflen,
                          const char *module_dir, const char *name) {
    size_t dl = strlen(module_dir), nl = strlen(name);
    const char *sep = "/user/";
    size_t sl = strlen(sep);
    if (dl + sl + nl + 1 > buflen) return false;
    memcpy(buf, module_dir, dl);
    memcpy(buf + dl, sep, sl);
    memcpy(buf + dl + sl, name, nl);
    buf[dl + sl + nl] = '\0';
    return true;
}

/* ---- vtable functions ---------------------------------------------------- */

/* All 11 kick param keys, defaulted to mid on create. */
static const char *const k_kick_keys[] = {
    PK_PITCH, PK_LENGTH, PK_SUSTAIN, PK_CURVE, PK_ATTACK,
    PK_TRS_DEC, PK_TRS_TNE, PK_COLOR, PK_FM_RATIO, PK_FM_INDEX, PK_OP2_WAVE
};

static void *omega_create(const char *module_dir, const char *json_defaults) {
    (void)json_defaults;
    /* Single allocation for the whole instance (CLAUDE.md single-alloc). calloc
     * zero-inits every field, giving a deterministic denormal/NaN-free start. */
    bohm_instance_t *inst = calloc(1, sizeof(bohm_instance_t));
    if (!inst) return NULL;
    inst->model         = MODEL_FM2;
    inst->main_volume   = 1.0f;
    inst->logged_buflen = false;

    /* USR (KICK-10): bounded, one-time user-content load from module_dir/user/.
     * This is the ONLY file I/O in the whole module and it runs here, off the
     * render loop (B-RESEARCH Open Question 2). On absence/failure usr_loaded
     * stays false and usr.c falls back to a built-in wavetable (non-silent).
     * The trap guards render_block, NOT create — so fread here is compliant. */
    if (module_dir && module_dir[0]) {
        char path[512];
        if (usr_join_path(path, sizeof path, module_dir, "kick.wav"))
            usr_load_wav(inst, path);
        if (usr_join_path(path, sizeof path, module_dir, "wavetable.raw"))
            usr_load_wavetable(inst, path);
    }

    /* Prime all kick params to mid so a bare create -> trigger is audible.
     * Route through the active model's vtable (NULL-guarded), not a hardcoded
     * fm2_set_param, so every model receives its primed defaults. */
    if (g_models[inst->model] && g_models[inst->model]->set_param)
        for (size_t i = 0; i < sizeof(k_kick_keys) / sizeof(k_kick_keys[0]); i++)
            g_models[inst->model]->set_param(inst, k_kick_keys[i], "0.5");
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
        /* NULL-guard: an unimplemented (NULL) model slot ignores triggers. */
        if (g_models[inst->model] && g_models[inst->model]->trigger)
            g_models[inst->model]->trigger(inst, msg[1], msg[2]);
    }
}

static void omega_set_param(void *instance, const char *key, const char *val) {
    bohm_instance_t *inst = instance;
    if (strcmp(key, PK_MODEL) == 0) {
        int m = (int)dsp_parse_f(val);
        if (m < 0) m = 0;
        if (m >= MODEL_COUNT) m = MODEL_COUNT - 1;
        /* Only act on an actual change. Clean re-init on switch (Pitfall 1 /
         * KICK-13): the incoming model reinterprets the SAME model_state[4096]
         * bytes the previous model left. Zero them, then re-prime defaults
         * through the incoming model so it starts from a known-good state.
         * NULL-guard the re-prime so a switch to an unimplemented (NULL) slot
         * clears state and renders silence — never a NULL deref. */
        if ((model_id_t)m != inst->model) {
            inst->model = (model_id_t)m;
            memset(inst->model_state, 0, sizeof inst->model_state);
            if (g_models[inst->model] && g_models[inst->model]->set_param)
                for (size_t i = 0; i < sizeof(k_kick_keys) / sizeof(k_kick_keys[0]); i++)
                    g_models[inst->model]->set_param(inst, k_kick_keys[i], "0.5");
        }
    } else if (strcmp(key, PK_MASTER_VOL) == 0) {
        float v = dsp_parse_f(val);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        inst->main_volume = v;
    } else {
        /* All kick keys (Page 1 + Page 2) dispatch through the active model's
         * vtable (Pitfall 2 fix). NULL-guarded so an unimplemented slot is a
         * no-op, never a NULL deref. */
        if (g_models[inst->model] && g_models[inst->model]->set_param)
            g_models[inst->model]->set_param(inst, key, val);
    }
}

static int omega_get_param(void *instance, const char *key, char *buf, int buf_len) {
    bohm_instance_t *inst = instance;
    if (strcmp(key, PK_UI_HIER) == 0) {
        /* D-10 buf_len measurement removed: get_param runs on the SPI audio
         * callback, where the real plugin_api_v1.h forbids logging of ANY kind
         * (host->log included) — it is a write() syscall that causes device-
         * wide audio dropouts. Capture buf_len off-thread in a later phase. */
        return omega_build_ui(inst, buf, buf_len);
    }
    return -1;   /* unhandled key (Pitfall 4) */
}

/* get_error: host queries this after create_instance to check init state.
 * Omega has no error state in Phase A — always report "no error" (return 0). */
static int omega_get_error(void *instance, char *buf, int buf_len) {
    (void)instance;
    if (buf && buf_len > 0) buf[0] = '\0';
    return 0;
}

static void omega_render_block(void *instance, int16_t *out_lr, int frames) {
    g_audio_thread_active = true;          /* D-13 malloc-trap gate */
    omega_set_ftz();                       /* FPCR FZ (FNDTN-05) */
    bohm_instance_t *inst = instance;

    if (frames > OMEGA_MAX_BLOCK) frames = OMEGA_MAX_BLOCK;   /* margin guard */

    float l[OMEGA_MAX_BLOCK], r[OMEGA_MAX_BLOCK];
    /* NULL-guard: an unimplemented (NULL) model slot renders silence rather
     * than dereferencing a NULL vtable (intermediate-compilation contract). */
    if (g_models[inst->model] && g_models[inst->model]->render) {
        g_models[inst->model]->render(inst, l, r, frames);    /* dispatch (KICK-01) */
    } else {
        memset(l, 0, sizeof(float) * (size_t)frames);
        memset(r, 0, sizeof(float) * (size_t)frames);
    }

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
    g_api.get_error        = omega_get_error;
    g_api.render_block     = omega_render_block;
    return &g_api;
}
