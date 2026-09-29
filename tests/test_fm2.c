/* test_fm2.c — TDD RED/GREEN unit tests for the FM2 engine (Plan A-02, Task 1).
 *
 * Exercises fm2_trigger/fm2_render/fm2_set_param directly against a stack
 * bohm_instance (no dsp.c lifecycle needed — that is covered by the full
 * harness in Task 3). Proves the KICK-02 / KICK-12 / Page-2 behaviors:
 *   T1  non-silent      — triggered render has audible energy
 *   T2  deterministic   — identical params render byte-identical int16
 *   T3  PITCH differs    — low vs high PK_PITCH -> different buffers
 *   T4  LENGTH differs   — short vs long decay -> later-block energy differs
 *   T5  FM INDEX differs — low vs high PK_FM_INDEX -> different buffers
 *   T6  finite/bounded   — every float sample isfinite and |x|<=1.0
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BLOCK   128
#define NBLOCKS 512

/* Render `nblocks` of BLOCK frames into i16buf (interleaved stereo), checking
 * each float sample is finite + bounded (T6). Returns summed |L| energy. */
static double render_run(struct bohm_instance *inst, int16_t *i16buf, int nblocks) {
    double energy = 0.0;
    float l[BLOCK], r[BLOCK];
    for (int b = 0; b < nblocks; b++) {
        g_fm2_vtable.render(inst, l, r, BLOCK);
        for (int n = 0; n < BLOCK; n++) {
            assert(isfinite(l[n]) && isfinite(r[n]));        /* T6 */
            assert(fabsf(l[n]) <= 1.0f && fabsf(r[n]) <= 1.0f);
            energy += fabs((double)l[n]);
            i16buf[(b * BLOCK + n) * 2]     = omega_to_i16(l[n]);
            i16buf[(b * BLOCK + n) * 2 + 1] = omega_to_i16(r[n]);
        }
    }
    return energy;
}

/* Set all 11 kick keys to mid, then trigger note 36 vel 100. */
static void reset_mid(struct bohm_instance *inst) {
    memset(inst, 0, sizeof(*inst));
    inst->model = MODEL_FM2;
    inst->main_volume = 1.0f;
    const char *keys[] = {
        PK_PITCH, PK_LENGTH, PK_SUSTAIN, PK_CURVE, PK_ATTACK,
        PK_TRS_DEC, PK_TRS_TNE, PK_COLOR, PK_FM_RATIO, PK_FM_INDEX, PK_OP2_WAVE
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        fm2_set_param(inst, keys[i], "0.5");
}

static double later_half_energy(const int16_t *buf, int nblocks) {
    double e = 0.0;
    int start = (nblocks / 2) * BLOCK * 2;
    for (int i = start; i < nblocks * BLOCK * 2; i += 2)
        e += fabs((double)buf[i]);
    return e;
}

int main(void) {
    omega_primitives_selfcheck();

    static int16_t bufA[NBLOCKS * BLOCK * 2];
    static int16_t bufB[NBLOCKS * BLOCK * 2];
    struct bohm_instance inst;

    /* Sanity: vtable is well-formed. */
    assert(strcmp(g_fm2_vtable.name, "FM2") == 0);
    assert(g_fm2_vtable.trigger && g_fm2_vtable.render &&
           g_fm2_vtable.set_p2 && g_fm2_vtable.p2_slot_desc);

    /* T1 non-silent. */
    reset_mid(&inst);
    g_fm2_vtable.trigger(&inst, 36, 100);
    double eA = render_run(&inst, bufA, NBLOCKS);
    assert(eA > 1.0);   /* audible: far above a silent (0.0) run */

    /* T2 deterministic — identical params render byte-identical. */
    reset_mid(&inst);
    g_fm2_vtable.trigger(&inst, 36, 100);
    render_run(&inst, bufB, NBLOCKS);
    assert(memcmp(bufA, bufB, sizeof(bufA)) == 0);

    /* T3 PITCH differs. PITCH is a Hz value now (B2, VOICE-01). */
    reset_mid(&inst); fm2_set_param(&inst, PK_PITCH, "40");
    g_fm2_vtable.trigger(&inst, 36, 100); render_run(&inst, bufA, NBLOCKS);
    reset_mid(&inst); fm2_set_param(&inst, PK_PITCH, "160");
    g_fm2_vtable.trigger(&inst, 36, 100); render_run(&inst, bufB, NBLOCKS);
    assert(memcmp(bufA, bufB, sizeof(bufA)) != 0);

    /* T4 LENGTH differs (later-half energy). */
    reset_mid(&inst); fm2_set_param(&inst, PK_LENGTH, "0.05");
    g_fm2_vtable.trigger(&inst, 36, 100); render_run(&inst, bufA, NBLOCKS);
    double shortTail = later_half_energy(bufA, NBLOCKS);
    reset_mid(&inst); fm2_set_param(&inst, PK_LENGTH, "0.95");
    g_fm2_vtable.trigger(&inst, 36, 100); render_run(&inst, bufB, NBLOCKS);
    double longTail = later_half_energy(bufB, NBLOCKS);
    assert(fabs(longTail - shortTail) > 1.0);

    /* T5 FM INDEX differs. */
    reset_mid(&inst); fm2_set_param(&inst, PK_FM_INDEX, "0.0");
    g_fm2_vtable.trigger(&inst, 36, 100); render_run(&inst, bufA, NBLOCKS);
    reset_mid(&inst); fm2_set_param(&inst, PK_FM_INDEX, "0.9");
    g_fm2_vtable.trigger(&inst, 36, 100); render_run(&inst, bufB, NBLOCKS);
    assert(memcmp(bufA, bufB, sizeof(bufA)) != 0);

    /* Page-2 slot descriptor writes bounded JSON. */
    char jbuf[512];
    int jn = g_fm2_vtable.p2_slot_desc(&inst, jbuf, sizeof(jbuf));
    assert(jn > 0 && jn < (int)sizeof(jbuf));
    assert(strstr(jbuf, PK_FM_RATIO) && strstr(jbuf, PK_FM_INDEX) && strstr(jbuf, PK_OP2_WAVE));

    printf("test_fm2: ALL TESTS PASSED\n");
    return 0;
}
