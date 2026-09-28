/* model_registry.c — the vtable registry (KICK-01).
 *
 * This is the ONLY file that knows the full model list. dsp.c dispatches
 * through g_models[] and never calls into a model TU directly. Phase B appends
 * new entries here (append-only; model IDs are permanent — never renumber).
 */
#include "omega.h"

extern const kick_model_vtable_t g_fm2_vtable;   /* defined in fm2.c */

const kick_model_vtable_t *g_models[MODEL_COUNT] = {
    &g_fm2_vtable,   /* MODEL_FM2 = 0 */
};
