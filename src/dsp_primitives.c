/* dsp_primitives.c — owns the single .rodata sine table definition and the
 * runtime guard-sample self-check.
 *
 * The DSP primitives themselves (env_t, wt_read, tpt1_lp, omega_to_i16) are
 * `static inline` in dsp_primitives.h so every TU gets them directly. This .c
 * exists to (1) own the ONE external definition of g_sine_table (pulled in via
 * the generated sine_table.h, which emits it non-static) and (2) provide
 * omega_primitives_selfcheck for the harness to assert KICK-15 at runtime.
 */
#include "dsp_primitives.h"
#include "sine_table.h"   /* defines: const float _Alignas(16) g_sine_table[2049] */
#include <assert.h>

/* KICK-15: the guard sample must duplicate index 0 so wt_read's t[i+1] at the
 * wrap point is branch-free and correct. Assert it at runtime from the harness. */
void omega_primitives_selfcheck(void) {
    assert(g_sine_table[2048] == g_sine_table[0]);
}
