/* test_readback.c — B1 param value readback + per-model memory (UIX-01/04).
 *
 * Drives the REAL plugin (move_plugin_init_v2 -> create -> set/get_param) and
 * asserts the defects found on-device are fixed:
 *   1. get_param(key) echoes the value the host last set (knobs no longer read 0)
 *   2. a bare create reports musical defaults, not 0
 *   3. switching models restores THAT model's stored values (per-model memory)
 *   4. global keys (master_vol, groove) round-trip
 *   5. unknown keys still return -1
 *   6. the locale-independent formatter produces the expected decimal text
 */
#include "omega.h"
#include "params.h"
#include "mock_host.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern const kick_model_vtable_t *g_models[];

/* Parse a decimal the same way the module does (locale-independent, '.' only). */
static float parse_back(const char *s) {
    float sign = 1.0f; if (*s == '-') { sign = -1.0f; s++; }
    float ip = 0.0f; while (*s >= '0' && *s <= '9') { ip = ip*10.0f + (*s-'0'); s++; }
    float fp = 0.0f, sc = 0.1f;
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { fp += (*s-'0')*sc; sc*=0.1f; s++; } }
    return sign * (ip + fp);
}

static float get_val(plugin_api_v2_t *api, void *inst, const char *key) {
    char buf[32];
    int n = api->get_param(inst, key, buf, (int)sizeof buf);
    assert(n > 0);              /* known key -> value written */
    assert(buf[n] == '\0');    /* null-terminated within bounds */
    return parse_back(buf);
}

static int approx(float a, float b) { return fabsf(a - b) < 1e-3f; }

int main(void) {
    /* --- formatter unit checks (locale-independent decimal) --------------- */
    char fb[32];
    pk_format_value(0.5f, 4, fb, sizeof fb);   assert(strcmp(fb, "0.5000") == 0);
    pk_format_value(0.0f, 4, fb, sizeof fb);   assert(strcmp(fb, "0.0000") == 0);
    pk_format_value(1.0f, 2, fb, sizeof fb);   assert(strcmp(fb, "1.00") == 0);
    pk_format_value(0.75f, 2, fb, sizeof fb);  assert(strcmp(fb, "0.75") == 0);
    pk_format_value(3.0f, 0, fb, sizeof fb);   assert(strcmp(fb, "3") == 0);
    printf("test_readback: formatter OK\n");

    /* key<->index sanity */
    assert(pk_kick_index(PK_PITCH) == PKI_PITCH);
    assert(pk_kick_index(PK_FM_RATIO) == PKI_FM_RATIO);
    assert(pk_kick_index("no_such") == -1);
    assert(pk_global_index(PK_MASTER_VOL) == GKI_MASTER_VOL);
    assert(pk_global_index(PK_GRV_VOL) == GKI_GRV_VOL);
    assert(pk_global_index(PK_PITCH) == -1);
    assert(strcmp(pk_kick_key(PKI_CURVE), PK_CURVE) == 0);

    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    /* 2. Bare create reports musical defaults, NOT 0. */
    assert(approx(get_val(api, inst, PK_PITCH), 0.5f));
    assert(approx(get_val(api, inst, PK_MASTER_VOL), 1.0f));   /* main vol full */
    assert(approx(get_val(api, inst, PK_FX_TYPE), 0.0f));
    printf("test_readback: create defaults OK (pitch=0.5, master=1.0)\n");

    /* 1. set -> get echoes the value the host set. */
    api->set_param(inst, PK_PITCH, "0.2500");
    assert(approx(get_val(api, inst, PK_PITCH), 0.25f));
    api->set_param(inst, PK_CURVE, "0.8000");
    assert(approx(get_val(api, inst, PK_CURVE), 0.8f));
    printf("test_readback: set->get echo OK\n");

    /* 4. Global keys round-trip. */
    api->set_param(inst, PK_MASTER_VOL, "0.3000");
    assert(approx(get_val(api, inst, PK_MASTER_VOL), 0.3f));
    api->set_param(inst, PK_GRV_VOL, "0.7000");
    assert(approx(get_val(api, inst, PK_GRV_VOL), 0.7f));
    printf("test_readback: global round-trip OK\n");

    /* 3. Per-model memory: set model 0 (FM2) PITCH, switch to model 1 (FM4),
     * set its PITCH differently, switch back — each model must report its own
     * stored value. */
    api->set_param(inst, PK_MODEL, "0");            /* FM2 */
    api->set_param(inst, PK_PITCH, "0.1000");
    assert(approx(get_val(api, inst, PK_PITCH), 0.1f));

    api->set_param(inst, PK_MODEL, "1");            /* FM4 */
    /* FM4 PITCH still at its default (0.5) — NOT reset to 0, NOT FM2's 0.1. */
    assert(approx(get_val(api, inst, PK_PITCH), 0.5f));
    api->set_param(inst, PK_PITCH, "0.9000");
    assert(approx(get_val(api, inst, PK_PITCH), 0.9f));

    api->set_param(inst, PK_MODEL, "0");            /* back to FM2 */
    assert(approx(get_val(api, inst, PK_PITCH), 0.1f));   /* FM2 remembered */

    api->set_param(inst, PK_MODEL, "1");            /* back to FM4 */
    assert(approx(get_val(api, inst, PK_PITCH), 0.9f));   /* FM4 remembered */
    printf("test_readback: per-model memory OK (FM2=0.1, FM4=0.9 across switches)\n");

    /* The MODEL key itself reports the current model index. */
    assert(approx(get_val(api, inst, PK_MODEL), 1.0f));

    /* Rich schema present (UIX-02/03/05): enum types, options, defaults, units,
     * short_names all emitted in the hierarchy. */
    {
        char ui[8192];
        int n = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
        assert(n > 0 && ui[n] == '\0');
        assert(strstr(ui, "\"type\":\"enum\"") != NULL);       /* discrete selectors */
        assert(strstr(ui, "\"options\":[\"FM2\"") != NULL);    /* MODEL enum options */
        assert(strstr(ui, "\"Diode\",\"Clip\",\"SAT\"") != NULL); /* FX TYPE enum */
        assert(strstr(ui, "\"default\":") != NULL);            /* knob start position */
        assert(strstr(ui, "\"short_name\":") != NULL);         /* OLED label */
        assert(strstr(ui, "\"unit\":") != NULL);               /* real-world unit */
        assert(strstr(ui, "\"step\":") != NULL);
        printf("test_readback: rich schema OK (enum/options/default/short_name/unit/step)\n");
    }

    /* 5. Unknown key -> -1, no write. */
    char kb[8]; kb[0] = '#';
    assert(api->get_param(inst, "no_such_key", kb, (int)sizeof kb) == -1);

    api->destroy_instance(inst);
    printf("test_readback: ALL TESTS PASSED\n");
    return 0;
}
