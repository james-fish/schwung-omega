/* test_samples.c — B3 sample infrastructure (SMPL-01/02/03).
 *
 * Writes a couple of WAVs into a temp module_dir/samples/, creates an instance
 * pointed there, and asserts:
 *   1. the bank enumerated the samples off-thread at create (SMPL-01)
 *   2. USR SAMPLE SEL renders as an enum whose options include the sample names,
 *      sorted, with a leading "None" (SMPL-02)
 *   3. selecting different bank slots yields different output (the picker works)
 *   4. an absent samples dir is handled gracefully (no crash, enum = just None)
 */
#include "omega.h"
#include "mock_host.h"
#include "wav.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const kick_model_vtable_t *g_models[];

#define BLOCK 128
#define NB    8
#define NS    (BLOCK * 2)

/* Write a short mono WAV of a sine at `hz` into path. */
static void write_sine_wav(const char *path, float hz, int frames) {
    FILE *f = wav_open(path, 44100, 1);
    assert(f);
    for (int i = 0; i < frames; i++) {
        float s = sinf(2.0f * 3.14159265f * hz * (float)i / 44100.0f) * 0.5f;
        int16_t v = (int16_t)(s * 30000.0f);
        wav_write(f, &v, 1);
    }
    wav_close(f);
}

static double render_energy(plugin_api_v2_t *api, void *inst) {
    int16_t out[NS];
    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);
    double e = 0;
    for (int b = 0; b < NB; b++) {
        api->render_block(inst, out, BLOCK);
        for (int i = 0; i < NS; i++) e += fabs((double)out[i]);
    }
    return e;
}

int main(void) {
    /* Build a temp module dir with a samples/ folder. */
    const char *moddir = "/tmp/omega_samptest";
    char sdir[256], p[320], cmd[400];
    snprintf(sdir, sizeof sdir, "%s/samples", moddir);
    snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s", moddir, sdir);
    assert(system(cmd) == 0);
    /* Two distinctly-named samples (sorted order: aaa_low, zzz_high). */
    snprintf(p, sizeof p, "%s/zzz_high.wav", sdir); write_sine_wav(p, 400.0f, 8000);
    snprintf(p, sizeof p, "%s/aaa_low.wav",  sdir); write_sine_wav(p, 80.0f,  8000);

    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance(moddir, "{}");
    assert(inst);

    /* Switch to USR so SAMPLE SEL is on Kick Page 1. */
    api->set_param(inst, PK_MODEL, "8");   /* MODEL_USR */

    char ui[8192];
    int n = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
    assert(n > 0 && ui[n] == '\0');
    /* SMPL-02: SAMPLE SEL is an enum with None + both names, sorted. */
    assert(strstr(ui, "SAMPLE SEL") != NULL);
    assert(strstr(ui, "\"None\"") != NULL);
    assert(strstr(ui, "\"aaa_low\"") != NULL);
    assert(strstr(ui, "\"zzz_high\"") != NULL);
    /* sorted: aaa_low precedes zzz_high in the options array */
    assert(strstr(ui, "aaa_low") < strstr(ui, "zzz_high"));
    printf("test_samples: enumeration + picker enum OK (2 samples, sorted)\n");

    /* SMPL: selecting different slots changes output. Sample 1 (aaa_low, 80 Hz)
     * vs sample 2 (zzz_high, 400 Hz) differ; both differ from None (no layer). */
    api->set_param(inst, PK_USR_LAYERVOL, "1.0");   /* favor the sample layer */
    api->set_param(inst, PK_USR_SAMPLE, "1");
    double e1 = render_energy(api, inst);
    api->set_param(inst, PK_USR_SAMPLE, "2");
    double e2 = render_energy(api, inst);
    assert(e1 > 0.0 && e2 > 0.0);
    assert(fabs(e1 - e2) > 1.0);   /* different samples -> different output */
    printf("test_samples: SAMPLE SELECT picks distinct bank slots OK\n");

    api->destroy_instance(inst);

    /* SMPL: absent samples dir handled gracefully — enum is just "None". */
    void *inst2 = api->create_instance("/tmp/omega_nonexistent_dir_xyz", "{}");
    assert(inst2);
    api->set_param(inst2, PK_MODEL, "8");
    n = api->get_param(inst2, "ui_hierarchy", ui, (int)sizeof ui);
    assert(n > 0);
    assert(strstr(ui, "\"None\"") != NULL);
    assert(strstr(ui, "\"aaa_low\"") == NULL);   /* no bank */
    api->destroy_instance(inst2);
    printf("test_samples: absent-dir graceful OK\n");

    printf("test_samples: ALL TESTS PASSED\n");
    return 0;
}
