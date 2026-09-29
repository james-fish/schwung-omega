/* test_distinct.c — pairwise model DISTINCTNESS metric (D-B02, Plan B-03 Task 3).
 *
 * D-B02 requires each model to have a "distinct sonic character vs the others
 * (FM2 != FM4 != ANA != HRD ... each earns its slot)". This harness renders
 * the DEFAULT (all params 0.5) kick for every REGISTERED model, computes a
 * small feature vector per model (RMS-envelope over time + a spectral-centroid
 * proxy via zero-crossing rate), and asserts every pair of registered models
 * differs by more than a distinctness threshold.
 *
 * GROWS AS MODELS LAND: it loops MODEL_COUNT and SKIPS unregistered (NULL)
 * vtable slots. In Wave 2 only FM2 is registered, so the pairwise set is empty
 * and the test trivially passes; as B-04..B-08 register FM4/WTR/PHY/HRD/DIG/
 * TRS/ANA/USR/GEN it automatically strengthens into a real gate that fails if
 * two models sound the same. No edits needed per model — just registration.
 */
#include "omega.h"
#include "dsp_primitives.h"
#include "mock_host.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern const kick_model_vtable_t *g_models[];

#define BLOCK    128
#define NBLOCKS  256
#define NSAMP    (NBLOCKS * BLOCK * 2)
#define NENV     NBLOCKS               /* one RMS-envelope point per block */

/* Distinctness threshold: the combined (RMS-envelope L2 + spectral) distance
 * between two models' default renders must exceed this. Tuned conservatively —
 * distinct engines differ by orders of magnitude more; near-zero would mean two
 * models render an essentially identical default kick (a real voicing failure). */
#define DISTINCT_THRESHOLD  1e-3

/* Kick Page 1 keys — the model-agnostic defaults every model understands. */
static const char *k_page1_keys[] = {
    PK_PITCH, PK_LENGTH, PK_SUSTAIN, PK_CURVE,
    PK_ATTACK, PK_TRS_DEC, PK_TRS_TNE, PK_COLOR,
};
#define N_PAGE1 (int)(sizeof(k_page1_keys) / sizeof(k_page1_keys[0]))

/* A model feature vector: an RMS-envelope curve + a spectral-centroid proxy. */
typedef struct {
    float  env[NENV];    /* per-block RMS envelope (amplitude/decay shape) */
    double zcr;          /* normalized zero-crossing rate (brightness proxy) */
} features_t;

/* Select model m (PK_MODEL takes the integer index; the switch memsets +
 * re-primes per KICK-13), set Page-1 defaults to 0.5, trigger, render, and
 * extract the feature vector. */
static void render_features(plugin_api_v2_t *api, void *inst, int m,
                            features_t *f) {
    char idx[16];
    snprintf(idx, sizeof(idx), "%d", m);
    api->set_param(inst, PK_MODEL, idx);
    for (int i = 0; i < N_PAGE1; i++) api->set_param(inst, k_page1_keys[i], "0.5");

    uint8_t noteon[3] = { 0x90, 36, 100 };
    api->on_midi(inst, noteon, 3, 0);

    long crossings = 0, counted = 0;
    int prev = 0;
    int16_t out[BLOCK * 2];
    for (int b = 0; b < NBLOCKS; b++) {
        api->render_block(inst, out, BLOCK);
        double sumsq = 0.0;
        for (int i = 0; i < BLOCK; i++) {
            int s = out[i * 2];                 /* L channel */
            double x = (double)s / 32768.0;
            sumsq += x * x;
            if (!(b == 0 && i == 0) && ((s >= 0) != (prev >= 0))) crossings++;
            prev = s; counted++;
        }
        f->env[b] = (float)sqrt(sumsq / (double)BLOCK);
    }
    f->zcr = counted ? (double)crossings / (double)counted : 0.0;
}

/* Combined distance between two feature vectors: L2 of the RMS-envelope
 * difference (amplitude/decay shape) + the absolute spectral (ZCR) difference. */
static double feature_distance(const features_t *a, const features_t *b) {
    double acc = 0.0;
    for (int i = 0; i < NENV; i++) {
        double d = (double)a->env[i] - (double)b->env[i];
        acc += d * d;
    }
    double env_dist = sqrt(acc / (double)NENV);
    double spec_dist = fabs(a->zcr - b->zcr);
    return env_dist + spec_dist;
}

int main(void) {
    omega_primitives_selfcheck();

    host_api_v1_t host = make_mock_host();
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    assert(api && api->api_version == 2);
    void *inst = api->create_instance("/tmp/omega", "{}");
    assert(inst);

    /* Collect the set of REGISTERED model indices (skip NULL vtable slots so
     * the pairwise comparison is meaningful now and strengthens as models
     * land). MODEL_COUNT-bounded; no per-model edits needed. */
    static features_t feats[MODEL_COUNT];
    int reg[MODEL_COUNT], nreg = 0;
    for (int m = 0; m < MODEL_COUNT; m++) {
        if (!g_models[m]) continue;            /* unregistered slot -> skip */
        render_features(api, inst, m, &feats[m]);
        reg[nreg++] = m;
    }

    /* Every REGISTERED pair must differ by more than the distinctness
     * threshold. In Wave 2 (only FM2) nreg==1 -> zero pairs -> trivially
     * passes; becomes a real gate as more models register. */
    int pairs = 0;
    for (int i = 0; i < nreg; i++) {
        for (int j = i + 1; j < nreg; j++) {
            double d = feature_distance(&feats[reg[i]], &feats[reg[j]]);
            if (!(d > DISTINCT_THRESHOLD)) {
                fprintf(stderr, "MODELS NOT DISTINCT: %s vs %s distance=%.3e\n",
                        g_models[reg[i]]->name, g_models[reg[j]]->name, d);
            }
            assert(d > DISTINCT_THRESHOLD);
            pairs++;
        }
    }

    api->destroy_instance(inst);
    printf("test_distinct: ALL TESTS PASSED (%d registered, %d pairs)\n",
           nreg, pairs);
    return 0;
}
