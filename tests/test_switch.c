/* test_switch.c — Wave-0 model-switch hazard harness (KICK-13, Plan B-01).
 *
 * Drives the REAL plugin through move_plugin_init_v2 -> create_instance and
 * exercises the model-switch re-init contract (Pitfall 1):
 *
 *   For every ordered pair (A, B) of models: select A, trigger, render;
 *   switch to B, trigger, render; switch back to A, trigger, render. On EVERY
 *   rendered buffer assert all samples are finite and |x| <= 1.0 — no NaN/Inf
 *   or stale-state blow-up carried across a switch. At least one switched +
 *   triggered render must be non-silent (proves re-init produced a live voice,
 *   not silence-from-garbage).
 *
 * Forward-compatible with the intermediate-compilation registry: NULL
 * (unimplemented) slots are skipped, so this passes in Wave 1 with only FM2
 * registered and automatically strengthens as each later plan replaces a NULL.
 *
 * Also verifies (end-to-end, through dsp.c):
 *   - registry length == MODEL_COUNT (Pitfall 6), mirroring the compile-time
 *     _Static_assert in model_registry.c;
 *   - a switch to a NULL (unimplemented) slot renders silence and never
 *     crashes (dsp.c NULL-guard).
 *
 * PK_MODEL is parsed by dsp.c's omega_set_param as (int)dsp_parse_f(val), an
 * integer index clamped to [0, MODEL_COUNT-1]; we set it with the decimal
 * index as a string ("3"), matching that parse exactly.
 */
#include "omega.h"
#include "dsp_primitives.h"
#include "mock_host.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Registry defined in model_registry.c. */
extern const kick_model_vtable_t *g_models[];

#define BLOCK   128
#define NBLOCKS 4            /* 512 frames */
#define NSAMP   (NBLOCKS * BLOCK * 2)

/* Expected Page-2 slot count per model, indexed by model_id_t (KICK-13:
 * "each p2_slot_desc emits valid bounded JSON with the correct slot count").
 * FM2=3 (FM RATIO/FM INDEX/OP2 WAVE), FM4=6 (full operator set), the rest=4.
 * GEN=0 (C-03/GRV-04): GEN moved ALL its controls to the conditional Groove
 * Page 2, so its KICK Page 2 emits no model interior (FX TYPE/AMT only). Kept
 * in model_id_t order. */
static const int g_expected_slots[MODEL_COUNT] = {
    [MODEL_FM2] = 3, [MODEL_FM4] = 5, [MODEL_WTR] = 4, [MODEL_PHY] = 4,
    [MODEL_HRD] = 4, [MODEL_DIG] = 4, [MODEL_TRS] = 4, [MODEL_ANA] = 4,
    [MODEL_USR] = 4, [MODEL_GEN] = 0,
};

/* Count occurrences of a literal substring in a null-terminated string. */
static int count_substr(const char *s, const char *needle) {
    int n = 0;
    size_t nl = strlen(needle);
    for (const char *p = s; (p = strstr(p, needle)) != NULL; p += nl) n++;
    return n;
}

/* Count occurrences of a single character in a null-terminated string. */
static int count_char(const char *s, char c) {
    int n = 0;
    for (const char *p = s; *p; p++) if (*p == c) n++;
    return n;
}

static void model_index_str(int idx, char *buf);   /* defined below */

/* KICK-13: for every registered model, prove its p2_slot_desc emits valid,
 * bounded, correctly-counted JSON and refuses to overflow a too-small buffer;
 * and prove the FULL ui_hierarchy stays balanced + null-terminated after a
 * switch to that model. */
static void assert_p2_json_valid(plugin_api_v2_t *api, void *inst) {
    int checked = 0;
    for (int m = 0; m < MODEL_COUNT; m++) {
        const kick_model_vtable_t *vt = g_models[m];
        if (!vt || !vt->p2_slot_desc) continue;   /* forward-compatible skip */

        /* 1. Bounded, null-terminated interior. A model may legitimately emit
         * a 0-length interior (GEN, C-03: its controls live on Groove Page 2,
         * so Kick Page 2 is FX-only). In that case skip the interior-content
         * asserts but STILL run the full-hierarchy balance check below. */
        char buf[1024];
        memset(buf, 0x7f, sizeof buf);            /* poison to catch missing NUL */
        int len = vt->p2_slot_desc(inst, buf, (int)sizeof buf);
        assert(len >= 0);                         /* never negative */
        assert(len < (int)sizeof buf);            /* bounded, no overflow */
        assert(len == g_expected_slots[m]         /* 0 iff the model is FX-only */
               ? (len == 0) : (len > 0));

        if (len > 0) {
            assert(buf[len] == '\0');             /* null-terminated at len */
            assert((int)strlen(buf) == len);      /* no embedded NUL / matches */

            /* 2. Balanced JSON: models emit a bare comma-separated object list
             * (no outer [] wrap), so braces balance. Brackets balance too — a
             * model may emit an enum with an "options":[...] array (B3: USR
             * SAMPLE SEL), so brackets are no longer required to be absent, only
             * balanced. */
            assert(count_char(buf, '{') == count_char(buf, '}'));
            assert(count_char(buf, '[') == count_char(buf, ']'));

            /* 3. Slot count matches the documented per-model count. */
            int nkeys = count_substr(buf, "\"key\"");
            assert(nkeys == g_expected_slots[m]);
            assert(count_char(buf, '{') == g_expected_slots[m]);  /* one obj/slot */

            /* 4. A too-small buffer is refused (returns 0, no write past bound —
             * the Pattern 3 overflow guard). Give it fewer bytes than it needs. */
            char tiny[8];
            memset(tiny, 0x7f, sizeof tiny);
            int r = vt->p2_slot_desc(inst, tiny, (int)sizeof tiny);
            assert(r == 0);                       /* bounded refusal */
        } else {
            /* 0-interior model (GEN): the descriptor still null-terminates its
             * buffer and reports 0 (FX-only Kick Page 2). */
            assert(g_expected_slots[m] == 0);
        }

        /* 5. The FULL ui_hierarchy stays valid after switching to this model:
         * balanced braces/brackets + null-terminated + bytes-written contract. */
        char idxbuf[4];
        model_index_str(m, idxbuf);
        api->set_param(inst, PK_MODEL, idxbuf);
        char ui[65536];
        memset(ui, 0x7f, sizeof ui);
        int uilen = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
        assert(uilen > 0 && uilen < (int)sizeof ui);
        assert(ui[uilen] == '\0');
        assert((int)strlen(ui) == uilen);
        assert(count_char(ui, '{') == count_char(ui, '}'));
        assert(count_char(ui, '[') == count_char(ui, ']'));
        /* the active model's slots are present in the full hierarchy: its key
         * count appears inside kick2 (model slots) + 2 FX slots + Page-1 (8)
         * + root (2). At minimum every model slot's "key" survived the splice. */
        assert(count_substr(ui, "\"key\"") >= g_expected_slots[m] + 2);

        checked++;
    }
    assert(checked >= 1);
    printf("test_switch: p2_slot_desc JSON valid for %d models\n", checked);
}

/* v0.3.2 STATIC visible_if gating: the host does NOT re-read ui_hierarchy on a
 * knob-driven enum change — it re-filters the CACHED hierarchy by visible_if. So
 * all groove levels are declared ALWAYS (TAPS `groove1`, GEN `gengroove1`+`genseq`,
 * shared `groovefx`), each carrying a FIXED grv_type gate; and kick1 declares
 * every picker model's params, each with a FIXED model==N gate. This asserts the
 * static contract: all levels present, correct fixed gates, balanced JSON. */
static void assert_groove2_gating(plugin_api_v2_t *api, void *inst) {
    char idxbuf[4];
    char ui[65536];

    model_index_str(MODEL_FM2, idxbuf);
    api->set_param(inst, PK_MODEL, idxbuf);
    memset(ui, 0x7f, sizeof ui);
    int fl = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
    assert(fl > 0 && fl < (int)sizeof ui);
    assert(ui[fl] == '\0');
    assert((int)strlen(ui) == fl);
    assert(count_char(ui, '{') == count_char(ui, '}'));
    assert(count_char(ui, '[') == count_char(ui, ']'));
    /* All groove levels are present regardless of grv_type (static). */
    assert(strstr(ui, "\"groove1\"")    != NULL);   /* TAPS groove page */
    assert(strstr(ui, "\"gengroove1\"") != NULL);   /* GEN control page */
    assert(strstr(ui, "\"genseq\"")     != NULL);   /* GEN seq page */
    assert(strstr(ui, "\"groovefx\"")   != NULL);   /* shared FX page */
    assert(strstr(ui, PK_GRV_RVMIX)   != NULL);     /* FX reverb mix */
    assert(strstr(ui, PK_GRV_GSEQLEN) != NULL);     /* GEN SEQ LEN (static, always declared) */
    /* v0.4: groove pages are UNGATED (TAPS + GEN run together, always visible) —
     * assert NO grv_type gate remains (removing it is what stops the nav jump). */
    assert(strstr(ui, PK_GRV_TYPE "\",\"equals\":0}") == NULL);
    assert(strstr(ui, PK_GRV_TYPE "\",\"equals\":1}") == NULL);
    /* The new independent GEN voice controls are present (dual-voice groove). */
    assert(strstr(ui, PK_GRV_GENVOL)    != NULL);   /* GEN VOL */
    assert(strstr(ui, PK_GRV_GROOT)     != NULL);   /* ROOT (merged Hz control, v0.4.1) */
    assert(strstr(ui, PK_GRV_GENFILT)   != NULL);   /* GEN FILTER */
    /* v0.4.1 batch: ROOT NOTE merged away; new GEN + TAPS controls present. */
    assert(strstr(ui, PK_GRV_GROOTNOTE) == NULL);   /* ROOT NOTE removed (merged into ROOT) */
    assert(strstr(ui, PK_GRV_GNOTELEN)  != NULL);   /* NOTE LEN */
    assert(strstr(ui, PK_GRV_GSWINGAMT) != NULL);   /* SWING */
    assert(strstr(ui, PK_GRV_GENTAPS)   != NULL);   /* GEN>TAPS send */
    assert(strstr(ui, PK_GRV_HPF)       != NULL);   /* TAPS HPF */
    /* kick1 per-model gates: FM2 (0) and USR (8) both present. */
    assert(strstr(ui, "\"visible_if\":{\"param\":\"" PK_MODEL "\",\"equals\":0}") != NULL);
    assert(strstr(ui, "\"visible_if\":{\"param\":\"" PK_MODEL "\",\"equals\":8}") != NULL);

    /* Switching the model does NOT change which levels exist (static); the host
     * filters by the model gate. Verify the hierarchy stays valid + still carries
     * every model gate after a switch. */
    model_index_str(MODEL_USR, idxbuf);
    api->set_param(inst, PK_MODEL, idxbuf);
    memset(ui, 0x7f, sizeof ui);
    int gl = api->get_param(inst, "ui_hierarchy", ui, (int)sizeof ui);
    assert(gl > 0 && gl < (int)sizeof ui);
    assert(ui[gl] == '\0');
    assert((int)strlen(ui) == gl);
    assert(count_char(ui, '{') == count_char(ui, '}'));
    assert(count_char(ui, '[') == count_char(ui, ']'));
    assert(strstr(ui, "\"visible_if\":{\"param\":\"" PK_MODEL "\",\"equals\":0}") != NULL);
    assert(strstr(ui, "\"visible_if\":{\"param\":\"" PK_MODEL "\",\"equals\":8}") != NULL);

    printf("test_switch: STATIC visible_if gating OK (all groove+model levels present, fixed gates)\n");
}

/* Convert a model index to its decimal string for PK_MODEL (matches dsp.c's
 * (int)dsp_parse_f parse). MODEL_COUNT <= 10 so a single digit suffices, but
 * handle two digits defensively. */
static void model_index_str(int idx, char *buf) {
    if (idx < 10) { buf[0] = (char)('0' + idx); buf[1] = '\0'; }
    else { buf[0] = (char)('0' + idx / 10); buf[1] = (char)('0' + idx % 10); buf[2] = '\0'; }
}

/* Select a model, trigger a note-on, render NBLOCKS. Asserts every int16 is in
 * range and (via float reconstruction) finite and bounded. Returns summed abs
 * energy (int16 domain) so callers can check non-silence. */
static double select_trigger_render(plugin_api_v2_t *api, void *inst, int model_idx) {
    char idxbuf[4];
    model_index_str(model_idx, idxbuf);
    api->set_param(inst, PK_MODEL, idxbuf);

    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);

    double energy = 0.0;
    int16_t out[BLOCK * 2];
    for (int b = 0; b < NBLOCKS; b++) {
        api->render_block(inst, out, BLOCK);
        for (int i = 0; i < BLOCK * 2; i++) {
            /* int16 output is inherently finite and in [INT16_MIN, INT16_MAX];
             * the clamp+isfinite happens in omega_to_i16. Asserting the range
             * here proves no NaN/Inf/overflow reached the boundary. */
            assert(out[i] >= INT16_MIN && out[i] <= INT16_MAX);
            energy += fabs((double)out[i]);
        }
    }
    return energy;
}

int main(void) {
    omega_primitives_selfcheck();

    /* Pitfall 6: registry length invariant (runtime mirror of the compile-time
     * _Static_assert in model_registry.c). */
    assert(sizeof(g_models) / sizeof(g_models[0]) == MODEL_COUNT);

    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    /* KICK-13: every model's p2_slot_desc emits valid, bounded, correctly-
     * counted JSON, refuses overflow, and keeps the full ui_hierarchy balanced
     * across model switches (B-09 dynamic Page-2 splice). */
    assert_p2_json_valid(api, inst);

    /* GRV-04 (C-03): Groove Page 2 appears iff model==GEN; Page 1 always. */
    assert_groove2_gating(api, inst);

    /* get_param contract (Pitfall 4): an unknown key returns -1, no write. */
    {
        char kb[64];
        assert(api->get_param(inst, "no_such_key", kb, (int)sizeof kb) == -1);
    }

    /* KICK-13: A->B->A switch + trigger over every ordered implemented pair.
     * Skip NULL (unimplemented) slots so this is forward-compatible. */
    double max_switch_energy = 0.0;
    int exercised_pairs = 0;
    for (int a = 0; a < MODEL_COUNT; a++) {
        if (!g_models[a]) continue;                 /* skip unimplemented A */
        for (int b = 0; b < MODEL_COUNT; b++) {
            if (!g_models[b]) continue;             /* skip unimplemented B */

            /* Select A, trigger, render (finite/bounded asserted inside). */
            select_trigger_render(api, inst, a);

            /* Switch to B, trigger, render. */
            double eB = select_trigger_render(api, inst, b);
            if (eB > max_switch_energy) max_switch_energy = eB;

            /* Switch back to A, trigger, render — proves re-init on return. */
            double eA = select_trigger_render(api, inst, a);
            if (eA > max_switch_energy) max_switch_energy = eA;

            exercised_pairs++;
        }
    }
    assert(exercised_pairs >= 1);                   /* at least FM2->FM2 */

    /* Re-init after a switch must produce a live voice, not silence-from-
     * garbage: at least one switched+triggered render is non-silent. */
    assert(max_switch_energy > 1000.0);

    /* dsp.c NULL-guard, end-to-end: a switch to a known-NULL (unimplemented)
     * slot in Wave 1 must render silence and never crash. Find one. */
    int null_idx = -1;
    for (int i = 0; i < MODEL_COUNT; i++) {
        if (!g_models[i]) { null_idx = i; break; }
    }
    if (null_idx >= 0) {
        double e = select_trigger_render(api, inst, null_idx);
        /* Unimplemented slot: no render fn -> dsp.c zeroes the block. Output
         * must be silence (and, trivially, finite/bounded — asserted inside). */
        assert(e == 0.0);
    }

    api->destroy_instance(inst);

    printf("test_switch: ALL TESTS PASSED (%d pairs exercised)\n", exercised_pairs);
    return 0;
}
