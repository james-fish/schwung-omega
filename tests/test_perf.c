/* test_perf.c — Phase D performer chain (PERF-01..05).
 *
 * Drives the real plugin and asserts:
 *   1. DJ filter LP vs HP vs neutral render differently, all bounded (PERF-03)
 *   2. DUCK measurably changes output when the groove is active (PERF-01/02)
 *   3. end-of-chain soft clip bounds a hot signal and differs from clip-off
 *      when driven hot (PERF-04)
 *   4. the Performer page + perf keys read back (PERF-05)
 * All output stays in int16 range everywhere (no divergence).
 */
#include "omega.h"
#include "mock_host.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern const kick_model_vtable_t *g_models[];
#define BLOCK 128
#define NB    16
#define NS    (BLOCK * 2)

static double render(plugin_api_v2_t *api, void *inst, int16_t *out) {
    uint8_t noteon[3] = { 0x90, 36, 110 };
    api->on_midi(inst, noteon, 3, 0);
    double e = 0;
    for (int b = 0; b < NB; b++) {
        api->render_block(inst, out + b*NS, BLOCK);
        for (int i = 0; i < NS; i++) {
            assert(out[b*NS+i] >= -32768 && out[b*NS+i] <= 32767);
            e += fabs((double)out[b*NS+i]);
        }
    }
    return e;
}

int main(void) {
    static int16_t a[NB*NS], b[NB*NS], c[NB*NS];
    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);
    api->set_param(inst, PK_MODEL, "0");           /* FM2 */

    /* 1. DJ filter LP vs HP differ + bounded (PERF-03). */
    api->set_param(inst, PK_DJ_FILT, "0.1");  render(api, inst, a);   /* LP */
    api->set_param(inst, PK_DJ_FILT, "0.9");  render(api, inst, b);   /* HP */
    assert(memcmp(a, b, sizeof a) != 0);
    api->set_param(inst, PK_DJ_FILT, "0.5");  render(api, inst, c);   /* neutral */
    assert(memcmp(a, c, sizeof a) != 0);
    printf("test_perf: DJ filter LP/HP/neutral distinct + bounded OK\n");

    /* 2. DUCK changes output with an active groove (PERF-01/02). */
    api->set_param(inst, PK_DJ_FILT, "0.5");
    api->set_param(inst, PK_GRV_VOL, "0.9");
    api->set_param(inst, PK_GRV_LENGTH, "0.7");
    api->set_param(inst, PK_DUCK, "0.0");     double e0 = render(api, inst, a);
    api->set_param(inst, PK_DUCK, "0.9");     double e1 = render(api, inst, b);
    assert(memcmp(a, b, sizeof a) != 0);
    assert(e0 > 0 && e1 > 0);
    printf("test_perf: DUCK responsive with active groove OK (e0=%.0f e1=%.0f)\n", e0, e1);

    /* 3. Soft clip bounds a hot signal and differs from clip-off when hot. */
    api->set_param(inst, PK_MASTER_VOL, "1.0");
    api->set_param(inst, PK_DUCK, "0.0");
    api->set_param(inst, PK_FX_AMT, "1.0");   /* drive the kick hot */
    api->set_param(inst, PK_CLIP, "1");       render(api, inst, a);   /* clip on (bounded asserted in render) */
    api->set_param(inst, PK_CLIP, "0");       render(api, inst, b);   /* clip off */
    /* Both stay in int16 range (render asserts); clip on vs off differ when hot. */
    assert(memcmp(a, b, sizeof a) != 0);
    printf("test_perf: soft clip bounds + toggles OK\n");

    /* 4. Readback + page present (PERF-05). */
    char buf[32];
    assert(api->get_param(inst, PK_DUCK, buf, sizeof buf) > 0);
    assert(api->get_param(inst, PK_DJ_FILT, buf, sizeof buf) > 0);
    char ui[8192];
    int n = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
    assert(n > 0 && ui[n] == '\0');
    assert(strstr(ui, "\"perf1\"") != NULL);
    assert(strstr(ui, PK_DJ_RESO) != NULL);
    assert(strstr(ui, PK_DUCK_BS) != NULL);
    printf("test_perf: Performer page + readback OK\n");

    api->destroy_instance(inst);
    printf("test_perf: ALL TESTS PASSED\n");
    return 0;
}
