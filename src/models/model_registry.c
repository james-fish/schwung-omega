/* model_registry.c — the vtable registry (KICK-01).
 *
 * This is the ONLY file that knows the full model list. dsp.c dispatches
 * through g_models[] and never calls into a model TU directly.
 *
 * INTERMEDIATE-COMPILATION STRATEGY (Phase B): g_models is sized to
 * MODEL_COUNT and uses DESIGNATED INITIALIZERS. Only implemented slots are
 * set; every unlisted slot is C-guaranteed zero-initialised (NULL). This lets
 * the registry compile+link at every wave. Each later model plan REPLACES its
 * NULL by ADDING its own designated line `[MODEL_XXX] = &g_xxx_vtable,`
 * together with the model .c that DEFINES that symbol (the extern lives in
 * omega.h; a designated line for an undefined symbol would fail to link, so
 * do NOT add a line until its .c exists).
 */
#include "omega.h"

extern const kick_model_vtable_t g_fm2_vtable;   /* defined in fm2.c */
extern const kick_model_vtable_t g_wtr_vtable;   /* defined in wtr.c (B-04) */
extern const kick_model_vtable_t g_trs_vtable;   /* defined in trs.c (B-04) */
extern const kick_model_vtable_t g_ana_vtable;   /* defined in ana.c (B-05) */
extern const kick_model_vtable_t g_dig_vtable;   /* defined in dig.c (B-05) */

const kick_model_vtable_t *g_models[MODEL_COUNT] = {
    [MODEL_FM2] = &g_fm2_vtable,   /* implemented (Phase A) */
    [MODEL_WTR] = &g_wtr_vtable,   /* implemented (B-04) */
    [MODEL_TRS] = &g_trs_vtable,   /* implemented (B-04) */
    [MODEL_ANA] = &g_ana_vtable,   /* implemented (B-05) */
    [MODEL_DIG] = &g_dig_vtable,   /* implemented (B-05) */
    /* All other slots are NULL until their model plan lands:
     *   [MODEL_FM4] = &g_fm4_vtable,   (B-06)
     *   [MODEL_PHY] = &g_phy_vtable,   (B-07)
     *   [MODEL_HRD] = &g_hrd_vtable,   (B-06)
     *   [MODEL_USR] = &g_usr_vtable,   (B-08)
     *   [MODEL_GEN] = &g_gen_vtable,   (B-08)
     * C zero-initialises any element not named above, so those slots are
     * guaranteed NULL. dsp.c + the test harness guard/skip NULL slots. */
};

_Static_assert(sizeof(g_models) / sizeof(g_models[0]) == MODEL_COUNT,
               "registry length must equal MODEL_COUNT");
