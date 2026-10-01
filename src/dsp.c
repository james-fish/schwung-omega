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
#include "groove.h"   /* Phase C: groove rumble voice (kick+groove sum) */
#include "params.h"   /* B1: central raw-value param cache (UIX-01/04) */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>   /* USR (KICK-10): bounded one-time WAV read in create ONLY */
#include <dirent.h>  /* B3 (SMPL-01): sample-folder enumeration in create ONLY */
#include <math.h>    /* Phase D: performer coefficient math (control rate only) */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

/* Read a canonical PCM16 WAV (as written by tests/wav.c) from `path` into `buf`,
 * bounded to `cap` frames. Mono-downmixes multi-channel. Returns the frame count
 * loaded (0 on any absence/malformed/empty). Bounded reads only; the ONE file
 * read path in the module (create_instance, off the audio thread). */
static int load_wav_into(const char *path, float *buf, int cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char hdr[44];
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) { fclose(f); return 0; }
    if (memcmp(hdr + 0, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0 ||
        memcmp(hdr + 12, "fmt ", 4) != 0 || memcmp(hdr + 36, "data", 4) != 0) {
        fclose(f); return 0;
    }
    uint16_t fmt      = rd_u16le(hdr + 20);
    uint16_t channels = rd_u16le(hdr + 22);
    uint16_t bits     = rd_u16le(hdr + 34);
    if (fmt != 1 || bits != 16 || channels < 1 || channels > 8) { fclose(f); return 0; }
    int nframes = 0;
    int16_t frame[8];
    while (nframes < cap) {
        size_t got = fread(frame, sizeof(int16_t), channels, f);
        if (got != channels) break;
        float acc = 0.0f;
        for (int c = 0; c < channels; c++) acc += (float)frame[c] / 32768.0f;
        buf[nframes++] = acc / (float)channels;
    }
    fclose(f);
    return nframes;
}

/* Load module_dir/user/kick.wav into inst->usr_sample (KICK-10, backward compat). */
static void usr_load_wav(bohm_instance_t *inst, const char *path) {
    int n = load_wav_into(path, inst->usr_sample,
                          (int)(sizeof inst->usr_sample / sizeof inst->usr_sample[0]));
    if (n > 0) { inst->usr_sample_len = n; inst->usr_loaded = true; }
}

/* Case-insensitive ".wav" suffix test. */
static bool ends_with_wav(const char *nm, size_t l) {
    if (l < 5) return false;
    const char *e = nm + l - 4;
    return e[0] == '.' && (e[1]=='w'||e[1]=='W') && (e[2]=='a'||e[2]=='A') && (e[3]=='v'||e[3]=='V');
}

/* Enumerate *.wav in `dir` into the sample bank (B3, SMPL-01/03), bounded to
 * OMEGA_MAX_SAMPLES total across all calls. Off the audio thread (create only).
 * Names are the basename without extension, JSON-sanitized (alnum/_/-/space);
 * results appended, then the caller sorts for a stable picker index. Missing
 * dir => no-op (opendir fails), so calling it for an absent SD path is safe. */
static void enumerate_samples(bohm_instance_t *inst, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    char path[600];
    while (inst->sample_count < OMEGA_MAX_SAMPLES && (ent = readdir(d)) != NULL) {
        const char *nm = ent->d_name;
        size_t l = strlen(nm);
        if (!ends_with_wav(nm, l)) continue;
        size_t dl = strlen(dir);
        if (dl + 1 + l + 1 > sizeof path) continue;      /* bounded join */
        memcpy(path, dir, dl); path[dl] = '/';
        memcpy(path + dl + 1, nm, l); path[dl + 1 + l] = '\0';

        int idx = inst->sample_count;
        int nf = load_wav_into(path, inst->sample_bank[idx], OMEGA_SAMPLE_CAP);
        if (nf <= 0) continue;
        inst->sample_len[idx] = nf;
        /* Name = basename minus ".wav", sanitized + bounded. */
        int cn = (int)(l - 4);
        if (cn > OMEGA_SAMPLE_NAMELEN - 1) cn = OMEGA_SAMPLE_NAMELEN - 1;
        int w = 0;
        for (int i = 0; i < cn; i++) {
            char c = nm[i];
            int ok = (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c==' ';
            inst->sample_name[idx][w++] = ok ? c : '_';
        }
        inst->sample_name[idx][w] = '\0';
        inst->sample_count++;
    }
    closedir(d);
}

/* Stable sort the bank by name (insertion sort — tiny N, off-thread) so a given
 * SAMPLE SELECT index maps to the same sample across runs (preset-safe). */
static void sort_samples(bohm_instance_t *inst) {
    for (int i = 1; i < inst->sample_count; i++) {
        for (int j = i; j > 0 && strcmp(inst->sample_name[j-1], inst->sample_name[j]) > 0; j--) {
            char tn[OMEGA_SAMPLE_NAMELEN];
            memcpy(tn, inst->sample_name[j], OMEGA_SAMPLE_NAMELEN);
            memcpy(inst->sample_name[j], inst->sample_name[j-1], OMEGA_SAMPLE_NAMELEN);
            memcpy(inst->sample_name[j-1], tn, OMEGA_SAMPLE_NAMELEN);
            int tl = inst->sample_len[j]; inst->sample_len[j] = inst->sample_len[j-1]; inst->sample_len[j-1] = tl;
            /* swap the audio rows */
            for (int k = 0; k < OMEGA_SAMPLE_CAP; k++) {
                float tf = inst->sample_bank[j][k];
                inst->sample_bank[j][k] = inst->sample_bank[j-1][k];
                inst->sample_bank[j-1][k] = tf;
            }
        }
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

/* Re-init a model's DSP state from the param cache (B1, UIX-04). Zeroes the
 * shared model_state region, then replays every kick param's cached raw value
 * (seeded to defaults in omega_create) through the model's set_param so both the
 * SOUND and the reported knob positions match the cache. The model ignores keys
 * it does not own (unknown-key = no-op), so replaying the full key list is safe.
 * Control-rate only (create + model switch), never per render. */
static void omega_prime_model(bohm_instance_t *inst, model_id_t m) {
    memset(inst->model_state, 0, sizeof inst->model_state);
    const kick_model_vtable_t *vt = g_models[m];
    if (!vt || !vt->set_param) return;
    char vbuf[24];
    for (int i = 0; i < PKI_COUNT; i++) {
        const char *k = pk_kick_key(i);
        if (!k) continue;
        pk_format_value(inst->kick_cache[m][i], 4, vbuf, (int)sizeof vbuf);
        vt->set_param(inst, k, vbuf);
    }
}

/* Performer chain coefficient configurator (Phase D). Recomputes all duck / DJ-
 * filter / clip coefficients from the cached raw values. ALL transcendentals
 * (expf/tanf) live here at CONTROL rate — the render chain is transcendental-
 * free. Called on any performer param change and once at create. */
static void perf_config(bohm_instance_t *inst) {
    float duck = inst->global_cache[GKI_DUCK];
    float rel  = inst->global_cache[GKI_DUCK_REL];
    float smt  = inst->global_cache[GKI_DUCK_SMT];
    float bs   = inst->global_cache[GKI_DUCK_BS];
    float filt = inst->global_cache[GKI_DJ_FILT];
    float reso = inst->global_cache[GKI_DJ_RESO];
    float cmpdr = inst->global_cache[GKI_CLIP];   /* GKI_CLIP slot repurposed as CMPDR */

    inst->duck_depth = duck < 0 ? 0 : (duck > 1 ? 1 : duck);
    float rel_ms = 10.0f + rel * 490.0f;                       /* 10..500 ms (PERF-01) */
    inst->duck_rel_coef = expf(-1.0f / (rel_ms * 0.001f * OMEGA_SR));
    inst->duck_smt_a = 0.02f + (1.0f - smt) * 0.6f;           /* slew: more SMT = slower */
    float bs_fc = 150.0f + bs * 2350.0f;                      /* 150..2500 Hz crossover
     * (iter-3: raised from 60..600 — ducking only the deep sub was too subtle;
     * ducking up through the low-mids makes the pump clearly audible) */
    inst->duck_bs_g = tanf((float)M_PI * bs_fc / OMEGA_SR);

    /* DJ filter: LP below neutral, HP above (PERF-03). */
    if (filt < 0.48f) {
        inst->dj_mode = 0;                                    /* LP */
        float t = filt / 0.48f;                               /* 0..1 */
        float fc = 120.0f * powf(18000.0f / 120.0f, t);       /* closed->open */
        float g = tanf((float)M_PI * fc / OMEGA_SR);
        float k = 2.0f - 1.7f * reso;                         /* resonance (PERF-03) */
        inst->dj_g = g; inst->dj_k = k; inst->dj_a0 = 1.0f / (1.0f + g * (g + k));
    } else if (filt > 0.52f) {
        inst->dj_mode = 1;                                    /* HP */
        float t = (filt - 0.52f) / 0.48f;                     /* 0..1 */
        float fc = 30.0f * powf(3000.0f / 30.0f, t);          /* open->high */
        float g = tanf((float)M_PI * fc / OMEGA_SR);
        float k = 2.0f - 1.7f * reso;
        inst->dj_g = g; inst->dj_k = k; inst->dj_a0 = 1.0f / (1.0f + g * (g + k));
    } else {
        inst->dj_mode = 2;                                    /* neutral bypass */
    }
    /* CMPDR (iter-2, PERF-04 replacement): one-knob comp + drive master glue.
     * As amount rises the compressor threshold drops, makeup + drive increase.
     * amount 0 = full bypass. Attack/release fixed (fast glue). */
    float a = cmpdr < 0 ? 0 : (cmpdr > 1 ? 1 : cmpdr);
    inst->cmpdr_amt  = a;
    inst->cmp_thr    = 1.0f - a * 0.75f;                 /* 1.0 (no comp) .. 0.25 */
    inst->cmp_makeup = 1.0f + a * 1.5f;                  /* up to +3.5 dB-ish */
    inst->cmp_atk    = expf(-1.0f / (0.002f * OMEGA_SR));/* ~2 ms attack */
    inst->cmp_rel    = expf(-1.0f / (0.080f * OMEGA_SR));/* ~80 ms release */
    inst->clip_on    = false;
}

static bool is_perf_key(const char *key) {
    return strcmp(key, PK_DUCK)==0 || strcmp(key, PK_DUCK_REL)==0 ||
           strcmp(key, PK_DUCK_SMT)==0 || strcmp(key, PK_DUCK_BS)==0 ||
           strcmp(key, PK_DJ_FILT)==0 || strcmp(key, PK_DJ_RESO)==0 ||
           strcmp(key, PK_CLIP)==0;
}

static void omega_apply_state(bohm_instance_t *inst, const char *json);  /* fwd (preset restore) */

static void *omega_create(const char *module_dir, const char *json_defaults) {
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

        /* B3 (SMPL-01/03): enumerate the module's samples/ folder, then an
         * optional SD-card location, into the bounded bank — off the audio
         * thread. Missing dirs are skipped (opendir fails). Sorted for a stable
         * SAMPLE SELECT index. The SD path is a best-guess to confirm on-device. */
        size_t dl = strlen(module_dir);
        if (dl + 9 < sizeof path) {
            memcpy(path, module_dir, dl);
            memcpy(path + dl, "/samples", 9);   /* includes NUL */
            enumerate_samples(inst, path);
        }
        enumerate_samples(inst, "/media/sdcard/samples");   /* optional SD (SMPL-03) */
        sort_samples(inst);
    }

    /* Groove rumble voice (Phase C): seed the tempo clock + Page-1 middles so
     * the first render block has a valid tap interval (no div-by-zero) and the
     * rumble is audible. The delay rings live by value in this single calloc. */
    groove_init(&inst->groove);

    /* Seed the param cache (B1, UIX-01) so a bare create reports musical values
     * rather than 0: every model's row starts at the schema defaults, and the
     * globals (master/model + groove) match groove_init + main_volume. Then
     * prime the active model's DSP from its cache row (UIX-04). */
    for (int m = 0; m < MODEL_COUNT; m++)
        for (int i = 0; i < PKI_COUNT; i++)
            inst->kick_cache[m][i] = g_kick_defaults[i];
    for (int i = 0; i < GKI_COUNT; i++)
        inst->global_cache[i] = g_global_defaults[i];
    omega_prime_model(inst, inst->model);

    /* Performer chain (Phase D): seed the duck gain to unity (no duck) and
     * compute coefficients from the seeded cache. */
    inst->duck_gain_s = 1.0f;
    inst->duck_env    = 0.0f;
    inst->cmp_env     = 0.0f;
    perf_config(inst);

    /* PRESET RESTORE on load: if the host passes a saved state blob as
     * json_defaults (what get_param("state") emitted), apply it over the seeded
     * defaults. A bare "{}" or NULL leaves the musical defaults in place. */
    if (json_defaults && json_defaults[0] == '{' && json_defaults[1] != '}') {
        omega_apply_state(inst, json_defaults);
        perf_config(inst);
    }
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
        /* GEN groove RETRIG=On Note: restart the generative sequence on each kick
         * (GRVX-05). Other retrigger modes are bar-based / free-run. */
        if (inst->groove.type == GROOVE_TYPE_GEN &&
            inst->groove.gen_retrig == GRV_RETRIG_NOTE)
            groove_gen_restart(&inst->groove);
        /* Sidechain duck triggers on the kick note-on (PERF-01), not amplitude —
         * set the duck envelope to full; it recovers over DUCK REL. */
        inst->duck_env = 1.0f;
    }
}

/* Groove Page-1 keys are model-independent (Phase C): they dispatch to
 * groove_set_param, never the kick model vtable. */
/* All groove keys share the "grv_" prefix (never a kick-model key), so route any
 * grv_* key to groove_set_param. The kick keys have no such prefix. */
static bool is_groove_key(const char *key) {
    return strncmp(key, "grv_", 4) == 0;
}

static void omega_apply_state(bohm_instance_t *inst, const char *json);

static void omega_set_param(void *instance, const char *key, const char *val) {
    bohm_instance_t *inst = instance;

    /* PRESET RESTORE: a "state" blob (from get_param("state")) is applied as a
     * batch of key/value pairs (see omega_apply_state). */
    if (key && strcmp(key, PK_STATE) == 0) { omega_apply_state(inst, val); return; }

    /* Record the raw value into the cache (B1, UIX-01/04) BEFORE dispatch so
     * get_param can echo it and a model switch can replay it. Kick keys are
     * stored per-model; master/model/groove are global. */
    int gi = pk_global_index(key);
    if (gi >= 0) inst->global_cache[gi] = dsp_parse_f(val);
    else {
        int ki = pk_kick_index(key);
        if (ki >= 0) {
            inst->kick_cache[inst->model][ki] = dsp_parse_f(val);
            inst->kick_cache_set[inst->model][ki] = true;
        }
    }

    if (strcmp(key, PK_MODEL) == 0) {
        int m = (int)dsp_parse_f(val);
        if (m < 0) m = 0;
        if (m >= MODEL_COUNT) m = MODEL_COUNT - 1;
        /* Only act on an actual change. Clean re-init on switch (Pitfall 1 /
         * KICK-13). Instead of blindly re-priming to 0.5 (which lost per-model
         * state and left the UI showing 0), replay the incoming model's cached
         * values so both its sound AND its knob positions are restored (UIX-04).
         * omega_prime_model NULL-guards an unimplemented slot -> silence. */
        if ((model_id_t)m != inst->model) {
            inst->model = (model_id_t)m;
            omega_prime_model(inst, inst->model);
        }
    } else if (strcmp(key, PK_MASTER_VOL) == 0) {
        float v = dsp_parse_f(val);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        inst->main_volume = v;
    } else if (is_groove_key(key)) {
        /* Groove Page-1 keys are model-independent (Phase C) — they must NOT go
         * through the kick model vtable. Route to groove_set_param directly. */
        groove_set_param(&inst->groove, key, val);
    } else if (is_perf_key(key)) {
        /* Performer chain keys (Phase D): the raw value is already cached above;
         * recompute the control-rate coefficients. */
        perf_config(inst);
    } else {
        /* All kick keys (Page 1 + Page 2) dispatch through the active model's
         * vtable (Pitfall 2 fix). NULL-guarded so an unimplemented slot is a
         * no-op, never a NULL deref. */
        if (g_models[inst->model] && g_models[inst->model]->set_param)
            g_models[inst->model]->set_param(inst, key, val);
    }
}

/* Parse a "state" JSON object {"key":"val",...} and apply each pair. Two passes:
 * PK_MODEL first (so kick params land on the right model), then everything else.
 * Tolerant scanner — expects simple quoted keys/values, no nested objects or
 * escapes (matches what omega_get_param("state") emits). */
static void omega_apply_state_pass(bohm_instance_t *inst, const char *s, int model_only) {
    if (!s) return;
    const char *p = s;
    char key[48], val[48];
    while (*p) {
        while (*p && *p != '"') p++; if (!*p) break; p++;
        int ki = 0; while (*p && *p != '"' && ki < 47) key[ki++] = *p++; key[ki] = '\0';
        if (*p != '"') break; p++;
        while (*p && *p != ':') p++; if (!*p) break; p++;
        while (*p && *p != '"') p++; if (!*p) break; p++;
        int vi = 0; while (*p && *p != '"' && vi < 47) val[vi++] = *p++; val[vi] = '\0';
        if (*p != '"') break; p++;
        int is_model = (strcmp(key, PK_MODEL) == 0);
        if (model_only == is_model) omega_set_param(inst, key, val);
    }
}
static void omega_apply_state(bohm_instance_t *inst, const char *json) {
    omega_apply_state_pass(inst, json, 1);   /* model first */
    omega_apply_state_pass(inst, json, 0);   /* then the rest */
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
    if (strcmp(key, PK_STATE) == 0) {
        /* PRESET SAVE: serialize the full param snapshot as a JSON object the host
         * stores and later feeds back via set_param("state", ...) or create's
         * json_defaults. Covers every global (incl. model select + groove +
         * performer) and the ACTIVE model's kick params. Locale-independent, no
         * alloc. "model" is emitted FIRST so restore selects the model before its
         * kick params land. Returns bytes written, or -1 if buf is too small. */
        if (!buf || buf_len < 4) return -1;
        int off = 0;
        char vb[24];
        buf[off++] = '{';
        for (int i = 0; i < GKI_COUNT; i++) {
            const char *k = pk_global_key(i);
            if (!k) continue;
            int vl = pk_format_value(inst->global_cache[i], 4, vb, (int)sizeof vb);
            int need = (int)strlen(k) + vl + 6;               /* "k":"v", */
            if (off + need >= buf_len) return -1;
            off += snprintf(buf + off, (size_t)(buf_len - off), "\"%s\":\"%s\",", k, vb);
        }
        for (int i = 0; i < PKI_COUNT; i++) {
            const char *k = pk_kick_key(i);
            if (!k) continue;
            int vl = pk_format_value(inst->kick_cache[inst->model][i], 4, vb, (int)sizeof vb);
            int need = (int)strlen(k) + vl + 6;
            if (off + need >= buf_len) return -1;
            off += snprintf(buf + off, (size_t)(buf_len - off), "\"%s\":\"%s\",", k, vb);
        }
        if (off > 1 && buf[off - 1] == ',') off--;            /* drop trailing comma */
        if (off + 2 >= buf_len) return -1;
        buf[off++] = '}';
        buf[off] = '\0';
        return off;
    }
    /* Per-key value readback (B1, UIX-01): the host reads each param's current
     * value to position its knobs/selectors. Format the cached raw value back
     * out (locale-independent). Global keys first, then the active model's kick
     * keys, so switching models reports THAT model's stored values (UIX-04). */
    int gi = pk_global_index(key);
    if (gi >= 0) return pk_format_value(inst->global_cache[gi], 4, buf, buf_len);
    int ki = pk_kick_index(key);
    if (ki >= 0) return pk_format_value(inst->kick_cache[inst->model][ki], 4, buf, buf_len);
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

    /* FX TONE post-kick tilt (B2, VOICE-05). A cheap fixed ~1.2 kHz one-pole
     * split: out = low + high*(2*tone), neutral at tone=0.5 (out==x), darker
     * below, brighter above; lows are always preserved so a kick never thins
     * out. Transcendental-free (constant coefficient); reads the active model's
     * cached FX TONE. Applied to the kick voice before the groove taps so the
     * rumble echoes the toned kick. */
    {
        float tone = inst->kick_cache[inst->model][PKI_FX_TONE];
        float hi_gain = 2.0f * tone;
        for (int n = 0; n < frames; n++) {
            inst->fx_tone_lp_l += 0.157f * (l[n] - inst->fx_tone_lp_l);
            inst->fx_tone_lp_r += 0.157f * (r[n] - inst->fx_tone_lp_r);
            l[n] = inst->fx_tone_lp_l + (l[n] - inst->fx_tone_lp_l) * hi_gain;
            r[n] = inst->fx_tone_lp_r + (r[n] - inst->fx_tone_lp_r) * hi_gain;
        }
    }

    /* Groove rumble (Phase C). Update the tempo clock ONCE per block (Pattern 2
     * — never per sample) UNCONDITIONALLY: GEN reads inst->groove.samples_per_16th
     * to clock its generative sequence (GRV-02/DC-05), so the clock must run even
     * for GEN. For NON-GEN models, the groove voice is the kick-fed multitap
     * (DC-01) summed into the output. For GEN, that multitap is BYPASSED — GEN's
     * own model output already IS the rumble (DC-05). No clamp here: the sum may
     * exceed 1.0 and is bounded only at the int16 boundary below (FNDTN-07);
     * headroom management (duck/DJ filter/soft clip) is Phase D. */
    groove_update_tempo(&inst->groove, (const struct host_api_v1 *)g_host, frames);
    if (inst->model != MODEL_GEN) {
        for (int n = 0; n < frames; n++) {
            float gl, gr;
            groove_tick(&inst->groove, l[n], r[n], &gl, &gr);
            /* SIDECHAIN DUCK (Phase D, PERF-01/02): the kick note-on set duck_env=1;
             * it recovers to 0 over DUCK REL. The duck gain (smoothed by DUCK SMT)
             * attenuates the groove LOWS (below the DUCK BS crossover) so the kick
             * punches through while the rumble pumps. Transcendental-free here. */
            inst->duck_env *= inst->duck_rel_coef;
            float target = 1.0f - inst->duck_depth * inst->duck_env;
            inst->duck_gain_s += inst->duck_smt_a * (target - inst->duck_gain_s);
            float dg = inst->duck_gain_s;
            inst->duck_bs_lp_l += inst->duck_bs_g * (gl - inst->duck_bs_lp_l) /
                                  (1.0f + inst->duck_bs_g);
            inst->duck_bs_lp_r += inst->duck_bs_g * (gr - inst->duck_bs_lp_r) /
                                  (1.0f + inst->duck_bs_g);
            float dl = inst->duck_bs_lp_l * dg + (gl - inst->duck_bs_lp_l);
            float dr = inst->duck_bs_lp_r * dg + (gr - inst->duck_bs_lp_r);
            l[n] += dl;  r[n] += dr;                       /* kick + ducked groove */
        }
    }

    /* DJ FILTER (Phase D, PERF-03): TPT state-variable filter, bidirectional
     * LP<->neutral<->HP with resonance. Coefficients precomputed in perf_config;
     * the per-sample loop is a handful of multiplies (no transcendental). */
    if (inst->dj_mode != 2) {
        float g = inst->dj_g, k = inst->dj_k, a0 = inst->dj_a0;
        int hp = (inst->dj_mode == 1);
        for (int n = 0; n < frames; n++) {
            float x = l[n];
            float v3 = x - inst->svf2_l;
            float v1 = a0 * (inst->svf1_l + g * v3);
            float v2 = inst->svf2_l + g * v1;
            inst->svf1_l = 2.0f * v1 - inst->svf1_l;
            inst->svf2_l = 2.0f * v2 - inst->svf2_l;
            l[n] = hp ? (x - k * v1 - v2) : v2;
            x = r[n];
            v3 = x - inst->svf2_r;
            v1 = a0 * (inst->svf1_r + g * v3);
            v2 = inst->svf2_r + g * v1;
            inst->svf1_r = 2.0f * v1 - inst->svf1_r;
            inst->svf2_r = 2.0f * v2 - inst->svf2_r;
            r[n] = hp ? (x - k * v1 - v2) : v2;
        }
    }

    /* Master volume + CMPDR (iter-2, one-knob comp+drive) + FNDTN-07 int16 clamp.
     * Peak-env soft compressor (bus glue) then a bounded diode-ish drive + makeup.
     * amount 0 = transparent (cmp_thr=1, no gain reduction, drive bypassed). */
    for (int n = 0; n < frames; n++) {
        float sl = l[n] * inst->main_volume;
        float sr = r[n] * inst->main_volume;
        if (inst->cmpdr_amt > 0.0f) {
            float peak = fabsf(sl) > fabsf(sr) ? fabsf(sl) : fabsf(sr);
            float coef = peak > inst->cmp_env ? inst->cmp_atk : inst->cmp_rel;
            inst->cmp_env = peak + coef * (inst->cmp_env - peak);
            float gain = 1.0f;
            if (inst->cmp_env > inst->cmp_thr) {
                /* soft 3:1-ish knee above threshold */
                gain = (inst->cmp_thr + 0.33f * (inst->cmp_env - inst->cmp_thr)) / inst->cmp_env;
            }
            float mk = gain * inst->cmp_makeup;
            sl *= mk; sr *= mk;
            /* bounded diode drive scaled by amount (even-harmonic grit). */
            float k = 1.0f + inst->cmpdr_amt * 2.0f;
            float xl = sl * k, xr = sr * k;
            float dl = (xl >= 0.0f ? xl / (1.0f + xl) : xl / (1.0f - 0.5f * xl));
            float dr = (xr >= 0.0f ? xr / (1.0f + xr) : xr / (1.0f - 0.5f * xr));
            sl += inst->cmpdr_amt * (dl - sl);
            sr += inst->cmpdr_amt * (dr - sr);
        }
        out_lr[n * 2]     = omega_to_i16(sl);
        out_lr[n * 2 + 1] = omega_to_i16(sr);
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
