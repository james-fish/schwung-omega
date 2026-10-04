/* test_dr32_engine.c — the DR32 engine plugin (src/dr32_engine.c).
 *
 * Drives the adapter the way DR32 does: dr32_engine_plugin() for the tables,
 * then per model create -> set every knob -> note_on -> render. Proves:
 *   T1  the plugin offers the eight kick models, one engine each
 *   T2  it refuses a host it cannot serve (older contract, other sample rate)
 *   T3  every model sounds, stays finite, and decays to silence
 *   T4  RT-safety: set / note_on / render never allocate (the malloc trap is
 *       armed around them; create and destroy are outside it)
 *   T5  the pad's tune moves the pitch
 *   T6  a model's CHOICE knob reaches every option: FM4's four algorithms
 *       render four different sounds (they are indices, not a 0..1 amount)
 *   T7  a voice is deterministic: two instances given the same hit agree
 */
#include "dr32_engine_api.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef OMEGA_MALLOC_TRAP
extern bool g_audio_thread_active;
#define RT_BEGIN() (g_audio_thread_active = true)
#define RT_END()   (g_audio_thread_active = false)
#else
#define RT_BEGIN() ((void)0)
#define RT_END()   ((void)0)
#endif

#define SR      44100
#define BLOCK   128
#define NBLOCKS 3000            /* 8.7 s: longer than any model's tail */

typedef struct { double energy; float peak, tail; uint64_t hash; } run_t;

/* One hit on a fresh voice holding `values`, with knob `knob` (if >= 0) set to
 * `knob_val` instead, played `tune` semitones from its own pitch. */
static run_t play(const dr32x_engine *e, const float *values, int knob, float knob_val, float tune) {
    run_t r = { 0.0, 0.0f, 0.0f, 1469598103934665603ull };
    void *v = e->create(SR);
    assert(v);
    float out[BLOCK];
    RT_BEGIN();
    for (int i = 0; i < e->nparams; i++) e->set(v, i, i == knob ? knob_val : values[i]);
    e->note_on(v, 100.0f / 127.0f, tune);
    for (int b = 0; b < NBLOCKS; b++) {
        e->render(v, out, BLOCK);
        float bp = 0.0f;
        for (int n = 0; n < BLOCK; n++) {
            assert(isfinite(out[n]));
            float a = fabsf(out[n]);
            if (a > bp) bp = a;
            r.energy += a;
            if (b < 344) { uint32_t u; memcpy(&u, &out[n], 4); r.hash = (r.hash ^ u) * 1099511628211ull; }
        }
        if (bp > r.peak) r.peak = bp;
        if (b >= NBLOCKS - 8 && bp > r.tail) r.tail = bp;
    }
    RT_END();
    e->destroy(v);
    return r;
}

int main(void) {
    dr32x_host host = { DR32X_API_VERSION, SR, "." };
    const dr32x_plugin *pl = dr32_engine_plugin(&host);

    /* T1 */
    assert(pl && pl->api_version == DR32X_API_VERSION && pl->struct_size == sizeof(dr32x_plugin));
    assert(!strcmp(pl->id, "omega"));
    assert(pl->nengines == 8 && pl->nmodels == 8);

    /* T2 */
    dr32x_host older = { DR32X_API_VERSION - 1, SR, "." }, rate48 = { DR32X_API_VERSION, 48000, "." };
    dr32x_host newer = { DR32X_API_VERSION + 1, SR, "." };
    assert(dr32_engine_plugin(&older) == NULL);
    assert(dr32_engine_plugin(&rate48) == NULL);
    assert(dr32_engine_plugin(&newer) == pl);           /* a later DR32 still reads this contract */
    assert(dr32_engine_plugin(NULL) == NULL);

    for (int m = 0; m < pl->nmodels; m++) {
        const dr32x_model *md = &pl->models[m];
        const dr32x_engine *e = &pl->engines[md->engine];
        assert(e->nparams >= 1 && e->nparams <= DR32X_MAX_PARAMS);
        assert(e->create && e->destroy && e->set && e->note_on && e->render);
        for (int i = 0; i < e->nparams; i++)
            assert(md->values[i] >= e->params[i].min && md->values[i] <= e->params[i].max);

        /* T3, T4 */
        run_t a = play(e, md->values, -1, 0.0f, 0.0f);
        assert(a.peak > 0.05f);                          /* it sounds          */
        assert(a.peak <= 1.5f);                          /* and is not runaway */
        assert(a.tail < 1.0e-4f);                        /* and it ends        */

        /* T7 */
        run_t b = play(e, md->values, -1, 0.0f, 0.0f);
        assert(a.hash == b.hash);

        /* T5: an octave up is a different sound (hash), for every model. */
        run_t up = play(e, md->values, -1, 0.0f, 12.0f);
        assert(up.hash != a.hash);
        printf("  %-4s peak %5.1f dBFS\n", md->slug, 20.0 * log10((double)a.peak));
    }

    /* T6: FM4's algorithm knob is the first knob of its "FM" page. */
    {
        const dr32x_engine *fm4 = NULL;
        const dr32x_model *md = NULL;
        for (int m = 0; m < pl->nmodels; m++)
            if (!strcmp(pl->models[m].slug, "fm4")) { md = &pl->models[m]; fm4 = &pl->engines[md->engine]; }
        assert(fm4 && md);
        int algo = -1;
        for (int i = 0; i < fm4->nparams; i++) if (!strcmp(fm4->params[i].key, "algo")) algo = i;
        assert(algo >= 0 && fm4->params[algo].options && fm4->params[algo].max == 3.0f);
        uint64_t h[4];
        for (int k = 0; k < 4; k++) h[k] = play(fm4, md->values, algo, (float)k, 0.0f).hash;
        for (int i = 0; i < 4; i++) for (int j = 0; j < i; j++) assert(h[i] != h[j]);
    }

    printf("test_dr32_engine: PASS\n");
    return 0;
}
