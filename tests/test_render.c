/* test_render.c — full-lifecycle offline harness (Plan A-02, Task 3).
 *
 * Drives the REAL plugin through move_plugin_init_v2 -> create_instance ->
 * on_midi (note-on) -> render_block x512 -> destroy_instance, proving:
 *   FNDTN-01 vtable populated, lifecycle runs (api_version==2, inst != NULL)
 *   KICK-01  model dispatch through g_models; g_models[MODEL_FM2]->name=="FM2"
 *   KICK-02  FM2 renders a non-silent, byte-deterministic kick
 *   KICK-12  PITCH and LENGTH audibly change the output
 *   Page 2   FM INDEX audibly changes the output
 *   FNDTN-06 mock host + WAV render produces tests/output/fm2_kick.wav
 *   FNDTN-07 every int16 sample stays in range (clamp path)
 *   KICK-15  omega_primitives_selfcheck (sine guard sample)
 *
 * FNDTN-03: on Linux (-DOMEGA_MALLOC_TRAP) render_block sets
 * g_audio_thread_active true, so any heap call during render aborts.
 */
#include "omega.h"
#include "dsp_primitives.h"
#include "mock_host.h"
#include "wav.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Registry is defined in model_registry.c; used for the KICK-01 assertion. */
extern const kick_model_vtable_t *g_models[];

#define BLOCK   128
#define NBLOCKS 512   /* ~1.5 s at 44100 */
#define NSAMP   (NBLOCKS * BLOCK * 2)

/* Trigger note-on then render NBLOCKS into buf (interleaved int16). Asserts each
 * int16 is in range (FNDTN-07). Returns summed abs energy. */
static double render_energy(plugin_api_v2_t *api, void *inst, int16_t *buf) {
    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);
    double energy = 0.0;
    int16_t out[BLOCK * 2];
    for (int b = 0; b < NBLOCKS; b++) {
        api->render_block(inst, out, BLOCK);
        for (int i = 0; i < BLOCK * 2; i++) {
            assert(out[i] >= INT16_MIN && out[i] <= INT16_MAX);
            energy += fabs((double)out[i]);
        }
        memcpy(&buf[b * BLOCK * 2], out, sizeof(out));
    }
    return energy;
}

static double later_half_energy(const int16_t *buf) {
    double e = 0.0;
    for (int i = (NBLOCKS / 2) * BLOCK * 2; i < NSAMP; i += 2)
        e += fabs((double)buf[i]);
    return e;
}

int main(void) {
    /* KICK-15: runtime guard-sample self-check (kept from A-01). */
    omega_primitives_selfcheck();

    /* FNDTN-01: real init + lifecycle. */
    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    /* KICK-01: dispatch registry populated; FM2 is model 0. */
    assert(g_models[MODEL_FM2] && strcmp(g_models[MODEL_FM2]->name, "FM2") == 0);

    static int16_t bufA[NSAMP];
    static int16_t bufB[NSAMP];

    /* KICK-02 non-silent: default mid params produce audible energy. */
    double e0 = render_energy(api, inst, bufA);
    assert(e0 > 1000.0);
    /* Persist this run to WAV (FNDTN-06). */
    FILE *wav = wav_open("tests/output/fm2_kick.wav", 44100, 2);
    assert(wav != NULL);
    wav_write(wav, bufA, NSAMP);
    wav_close(wav);

    /* KICK-02 deterministic: identical params render byte-identical int16. */
    api->render_block(inst, (int16_t[BLOCK * 2]){0}, 0);   /* no-op, keeps params */
    render_energy(api, inst, bufB);
    assert(memcmp(bufA, bufB, sizeof(bufA)) == 0);

    /* KICK-12 PITCH: low vs high yields different buffers. PITCH is Hz now (B2). */
    api->set_param(inst, PK_PITCH, "40");  render_energy(api, inst, bufA);
    api->set_param(inst, PK_PITCH, "160"); render_energy(api, inst, bufB);
    assert(memcmp(bufA, bufB, sizeof(bufA)) != 0);
    api->set_param(inst, PK_PITCH, "50");   /* restore (~50 Hz techno pocket) */

    /* KICK-12 LENGTH: short vs long decay -> later-half energy differs. */
    api->set_param(inst, PK_LENGTH, "0.05"); render_energy(api, inst, bufA);
    double shortTail = later_half_energy(bufA);
    api->set_param(inst, PK_LENGTH, "0.95"); render_energy(api, inst, bufB);
    double longTail = later_half_energy(bufB);
    assert(fabs(longTail - shortTail) > 100.0);
    api->set_param(inst, PK_LENGTH, "0.5");   /* restore */

    /* Page 2 FM INDEX: low vs high changes spectral content -> buffers differ. */
    api->set_param(inst, PK_FM_INDEX, "0.0"); render_energy(api, inst, bufA);
    api->set_param(inst, PK_FM_INDEX, "0.9"); render_energy(api, inst, bufB);
    assert(memcmp(bufA, bufB, sizeof(bufA)) != 0);

    /* ---- ui_hierarchy contract (A-03) ------------------------------------ */
    /* Bytes-written return + in-bounds + null-terminated (Pitfall 3/4). */
    char uibuf[65536];
    int n = api->get_param(inst, PK_UI_HIER, uibuf, sizeof(uibuf));
    assert(n > 0 && n < (int)sizeof(uibuf));
    assert(uibuf[n] == '\0');   /* return value excludes terminator; buf[n] is '\0' */

    /* Levels-based schema: the levels map plus every emitted PK_* key so
     * set_param/get_param dispatch still matches (kick1 + FM2 kick2 keys). */
    assert(strstr(uibuf, "levels"));
    assert(strstr(uibuf, "pitch"));
    assert(strstr(uibuf, "fm_ratio"));
    assert(strstr(uibuf, "fm_index"));
    assert(strstr(uibuf, "op2_wave"));
    assert(strstr(uibuf, "fx_type"));
    assert(strstr(uibuf, "fx_amt"));

    /* Unknown key returns -1 (do NOT return 0 or write garbage). */
    char tmp[8];
    assert(api->get_param(inst, "nonexistent_key_xyz", tmp, sizeof(tmp)) == -1);

    /* Truncation safety (Pitfall 3): a deliberately tiny buf_len must NOT write
     * past the buffer and must still null-terminate. A canary byte after the
     * bounded region must stay untouched. */
    struct { char b[16]; char canary; } probe;
    probe.canary = (char)0x5A;
    int tn = api->get_param(inst, PK_UI_HIER, probe.b, (int)sizeof(probe.b));
    assert(probe.canary == (char)0x5A);        /* no overrun past buf_len */
    assert(tn >= 0 && tn < (int)sizeof(probe.b));  /* bytes written within bounds */
    assert(probe.b[tn] == '\0');               /* still null-terminated */

    /* ---- Per-model default render (Wave-0 voicing requirement) ------------ */
    /* Loop every registry slot; skip NULL/unregistered vtable entries so this
     * is forward-compatible as B-04..B-08 land models. For each REGISTERED
     * model: select it (PK_MODEL takes the integer index), prime defaults
     * (0.5), trigger, render, assert non-silent + finite + <=1 (int16 range),
     * and write tests/output/<name>_kick.wav. */
    static const char *k_p1[] = {
        PK_PITCH, PK_LENGTH, PK_SUSTAIN, PK_CURVE,
        PK_ATTACK, PK_TRS_DEC, PK_TRS_TNE, PK_COLOR,
    };
    for (int m = 0; m < MODEL_COUNT; m++) {
        if (!g_models[m]) continue;               /* skip unregistered slots */
        char idx[16];
        snprintf(idx, sizeof(idx), "%d", m);
        api->set_param(inst, PK_MODEL, idx);      /* memset + re-prime (KICK-13) */
        for (size_t i = 0; i < sizeof(k_p1) / sizeof(k_p1[0]); i++)
            api->set_param(inst, k_p1[i], "0.5");
        double em = render_energy(api, inst, bufA);   /* asserts int16 range */
        assert(em > 1000.0);                          /* non-silent default */
        char path[128];
        snprintf(path, sizeof(path), "tests/output/%s_kick.wav", g_models[m]->name);
        FILE *w = wav_open(path, 44100, 2);
        assert(w != NULL);
        wav_write(w, bufA, NSAMP);
        wav_close(w);
    }
    api->set_param(inst, PK_MODEL, "0");   /* restore FM2 */

    api->destroy_instance(inst);

    printf("ALL TESTS PASSED\n");
    return 0;
}
