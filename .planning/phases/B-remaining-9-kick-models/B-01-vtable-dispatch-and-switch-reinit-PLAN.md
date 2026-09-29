---
phase: B-remaining-9-kick-models
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/omega.h
  - src/dsp.c
  - src/models/fm2.c
  - src/models/model_registry.c
  - tests/test_render.c
  - tests/test_switch.c
  - Makefile
autonomous: true
requirements: [KICK-13]
must_haves:
  truths:
    - "Every model (not just FM2) receives its Page-1 and Page-2 params via set_param"
    - "Switching MODEL fully re-inits the incoming model's state (no stale bytes / NaN carried across a switch)"
    - "A switch A->B->A followed by a trigger produces finite, bounded, non-stale output"
    - "The registry array size matches MODEL_COUNT at compile time"
  artifacts:
    - path: "src/omega.h"
      provides: "kick_model_vtable_t extended with set_param fn ptr; model_id_t append-only enum grown to MODEL_COUNT=10; new PK_* keys for all model Page-2 params"
      contains: "set_param"
    - path: "src/dsp.c"
      provides: "vtable-dispatched set_param + memset-on-switch re-init + re-prime through active model"
      contains: "memset(inst->model_state"
    - path: "tests/test_switch.c"
      provides: "model-switch A->B->A + trigger finite/non-stale assertions (KICK-13)"
      contains: "PK_MODEL"
  key_links:
    - from: "src/dsp.c omega_set_param"
      to: "g_models[inst->model]->set_param"
      via: "vtable dispatch (replaces hardcoded fm2_set_param)"
      pattern: "g_models\\[inst->model\\]->set_param"
    - from: "src/dsp.c omega_set_param PK_MODEL branch"
      to: "inst->model_state re-init"
      via: "memset + re-prime defaults on model change"
      pattern: "memset\\(inst->model_state"
---

<objective>
Fix the two Phase-A blocking bugs the research found, BEFORE any new model is added, and stand up the Wave-0 test scaffolding every model plan depends on.

Bug 1 (Pitfall 2): `omega_set_param` in dsp.c hardcodes `fm2_set_param` (line 118) — every non-FM2 model would never receive its params. Add a `set_param` function pointer to the internal `kick_model_vtable_t` (NOT the ABI-locked `plugin_api_v2_t` — the vtable is internal and has no `_Static_assert` binding its layout, so it is safe to extend) and dispatch all kick keys through `g_models[inst->model]->set_param`.

Bug 2 (Pitfall 1, KICK-13): switching `PK_MODEL` only sets `inst->model` and leaves the previous model's bytes in the shared `model_state[4096]`. The incoming model then reads stale phase/filter/envelope state → click, NaN, or garbage. Add a clean re-init on switch (memset the state region + re-prime kick defaults through the active model's `set_param`).

Purpose: Unblocks all 9 model plans (they cannot receive params or switch cleanly without this) and creates the Wave-0 switch test harness.
Output: Extended internal vtable, corrected dispatch + switch re-init, grown append-only enum + PK_* keys, and `tests/test_switch.c`.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/B-remaining-9-kick-models/B-RESEARCH.md
@.planning/phases/B-remaining-9-kick-models/B-CONTEXT.md
@.planning/phases/B-remaining-9-kick-models/B-VALIDATION.md
@src/omega.h
@src/dsp.c
@src/models/fm2.c
@src/models/model_registry.c

<interfaces>
<!-- Current internal vtable (src/omega.h lines 96-102) — EXTEND with set_param.
     The plugin ABI structs (host_api_v1_t, plugin_api_v2_t) are LOCKED by
     _Static_assert — do NOT touch them. kick_model_vtable_t is internal and
     safe to extend (no _Static_assert binds it). -->
typedef struct {
    const char *name;                                   /* "FM2" */
    void (*trigger)(bohm_instance_t *inst, int note, int velocity);
    void (*render)(bohm_instance_t *inst, float *out_l, float *out_r, int frames);
    void (*set_p2)(bohm_instance_t *inst, const char *key, const char *val);
    int  (*p2_slot_desc)(bohm_instance_t *inst, char *buf, int buf_len);
} kick_model_vtable_t;

/* Current enum (src/omega.h line 105) — APPEND ONLY, never renumber: */
typedef enum { MODEL_FM2 = 0, MODEL_COUNT } model_id_t;

/* Current buggy dispatch (src/dsp.c lines 104-120): PK_MODEL just sets
   inst->model; else-branch hardcodes fm2_set_param. */
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Extend the internal vtable + grow the append-only enum + add all model PK_* keys</name>
  <read_first>src/omega.h, src/models/fm2.c (vtable + fm2_set_param shape), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§Pattern 1, §Pattern 2, Open Question 1, per-model Page-2 slot lists)</read_first>
  <files>src/omega.h</files>
  <action>
    In `src/omega.h`:
    1. Add a `set_param` function pointer to `kick_model_vtable_t` (Open Question 1 recommendation, verbatim signature):
       `void (*set_param)(bohm_instance_t *inst, const char *key, const char *val);`
       Place it after `render` and before `set_p2`. Do NOT remove `set_p2` or `p2_slot_desc` (they stay; `set_p2` may delegate to `set_param`). Do NOT add a `_Static_assert` on this struct.
    2. Grow `model_id_t` APPEND-ONLY (never renumber; `MODEL_FM2 = 0` is permanent):
       `typedef enum { MODEL_FM2 = 0, MODEL_FM4, MODEL_WTR, MODEL_PHY, MODEL_HRD, MODEL_DIG, MODEL_TRS, MODEL_ANA, MODEL_USR, MODEL_GEN, MODEL_COUNT } model_id_t;`  /* MODEL_COUNT becomes 10 */
    3. Add PK_* string-key macros for every new model's Page-2 params (used by set_param dispatch + ui.c splice in B-09). Use these EXACT keys (each unique; note TRS gets its own curve key distinct from Page-1 CURVE):
       FM4: `PK_FM4_ALGO "fm4_algo"`, `PK_FM4_OPRATIO "fm4_opratio"`, `PK_FM4_OPINDEX "fm4_opindex"`, `PK_FM4_OPAMP "fm4_opamp"`, `PK_FM4_FEEDBACK "fm4_feedback"`, `PK_FM4_ALGO2 "fm4_algo2"`
       WTR: `PK_WTR_WAVE "wtr_wave"`, `PK_WTR_BODYPITCH "wtr_bodypitch"`, `PK_WTR_TRANSDEC "wtr_transdec"`, `PK_WTR_TRANSCOL "wtr_transcol"`
       PHY: `PK_PHY_BEATER "phy_beater"`, `PK_PHY_SHELL "phy_shell"`, `PK_PHY_HEADTENS "phy_headtens"`, `PK_PHY_DAMPING "phy_damping"`
       HRD: `PK_HRD_SAMPLE "hrd_sample"`, `PK_HRD_MIX "hrd_mix"`, `PK_HRD_DRIVE "hrd_drive"`, `PK_HRD_CRUSH "hrd_crush"`
       DIG: `PK_DIG_WAVEIDX "dig_waveidx"`, `PK_DIG_SAMPLE "dig_sample"`, `PK_DIG_BITDEPTH "dig_bitdepth"`, `PK_DIG_PITCHENV "dig_pitchenv"`
       TRS: `PK_TRS_TONE "trs_tone"`, `PK_TRS_TDEC "trs_tdec"`, `PK_TRS_WTCOL "trs_wtcol"`, `PK_TRS_CURVE "trs_curve"`
       ANA: `PK_ANA_MORPH "ana_morph"`, `PK_ANA_SUBLVL "ana_sublvl"`, `PK_ANA_SUBDEC "ana_subdec"`, `PK_ANA_SAMPLE "ana_sample"`
       USR: `PK_USR_SAMPLE "usr_sample"`, `PK_USR_WTMORPH "usr_wtmorph"`, `PK_USR_LAYERVOL "usr_layervol"`, `PK_USR_PITCHENV "usr_pitchenv"`
       GEN: `PK_GEN_SEED "gen_seed"`, `PK_GEN_SCALE "gen_scale"`, `PK_GEN_DENSITY "gen_density"`
    4. Add the extern declarations for all 9 new vtables (defined in their model .c files):
       `extern const kick_model_vtable_t g_fm4_vtable, g_wtr_vtable, g_phy_vtable, g_hrd_vtable, g_dig_vtable, g_trs_vtable, g_ana_vtable, g_usr_vtable, g_gen_vtable;`
    Do NOT alter `host_api_v1_t`, `plugin_api_v2_t`, or their `_Static_assert`s.
  </action>
  <acceptance_criteria>
    - `grep -c "MODEL_FM4\|MODEL_WTR\|MODEL_PHY\|MODEL_HRD\|MODEL_DIG\|MODEL_TRS\|MODEL_ANA\|MODEL_USR\|MODEL_GEN" src/omega.h` returns >= 9
    - `grep "MODEL_FM2 = 0" src/omega.h` still present (permanent index 0)
    - `src/omega.h` contains `void (*set_param)(bohm_instance_t *inst, const char *key, const char *val);` inside kick_model_vtable_t
    - `grep -c "^#define PK_" src/omega.h` increases by >= 33 vs current
    - `grep "offsetof(plugin_api_v2_t, render_block) == 56" src/omega.h` still present (ABI untouched)
  </acceptance_criteria>
  <verify>
    <automated>grep -q 'set_param)(bohm_instance_t' src/omega.h && grep -q 'MODEL_GEN' src/omega.h && grep -q 'MODEL_FM2 = 0' src/omega.h && echo OK</automated>
  </verify>
  <done>Internal vtable has a set_param fn ptr; enum grown append-only to MODEL_COUNT=10; all 33+ new PK_* keys and 9 vtable externs declared; ABI structs and their static_asserts untouched.</done>
</task>

<task type="auto">
  <name>Task 2: Fix dsp.c dispatch (vtable set_param) + clean model-switch re-init; add fm2 set_param field</name>
  <read_first>src/dsp.c (omega_set_param lines 104-120, omega_create prime loop lines 74-88), src/models/fm2.c (fm2_set_param, g_fm2_vtable), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§Pattern 2 code sample, Pitfall 1, Pitfall 2)</read_first>
  <files>src/dsp.c, src/models/fm2.c</files>
  <action>
    In `src/models/fm2.c`: add the new `set_param` field to `g_fm2_vtable`, pointing at the existing `fm2_set_param` (already handles both Page-1 and Page-2 keys). Keep `.set_p2 = fm2_set_p2` as-is. Result:
      `.set_param = fm2_set_param,` added to the initializer.
    In `src/dsp.c`:
    1. Replace the hardcoded `fm2_set_param(inst, key, val)` in the `omega_set_param` else-branch (line 118) with `g_models[inst->model]->set_param(inst, key, val);` so the ACTIVE model receives its params (Pitfall 2 fix).
    2. In the `PK_MODEL` branch, after clamping `m` to `[0, MODEL_COUNT-1]`, only act when the model actually changes, then fully re-init (Pitfall 1 / KICK-13 fix, §Pattern 2 verbatim contract):
       ```c
       if ((model_id_t)m != inst->model) {
           inst->model = (model_id_t)m;
           memset(inst->model_state, 0, sizeof inst->model_state);  /* clear stale bytes */
           for (size_t i = 0; i < sizeof(k_kick_keys)/sizeof(k_kick_keys[0]); i++)
               g_models[inst->model]->set_param(inst, k_kick_keys[i], "0.5");  /* re-prime through active model */
       }
       ```
    3. Change `omega_create`'s prime loop (line 85-86) to route through the active model's vtable instead of the hardcoded `fm2_set_param`:
       `g_models[inst->model]->set_param(inst, k_kick_keys[i], "0.5");`
    Keep everything else in dsp.c unchanged (FPCR, int16 boundary, no host->log anywhere). `k_kick_keys[]` stays the Page-1 + FM2 Page-2 defaults prime list; each model ignores keys it does not recognize (parse_f pattern), so priming with the shared list is safe.
  </action>
  <acceptance_criteria>
    - `grep -q 'g_models\[inst->model\]->set_param' src/dsp.c` (dispatch fixed)
    - `grep -q 'memset(inst->model_state, 0, sizeof inst->model_state)' src/dsp.c` (switch re-init)
    - `grep -qv 'fm2_set_param' src/dsp.c` — dsp.c no longer references fm2_set_param directly: `! grep -q 'fm2_set_param' src/dsp.c`
    - `grep -q '.set_param' src/models/fm2.c` and fm2 vtable initializer lists set_param = fm2_set_param
    - No `host->log` / `g_host->log` added anywhere in dsp.c
  </acceptance_criteria>
  <verify>
    <automated>grep -q 'g_models\[inst->model\]->set_param' src/dsp.c && grep -q 'memset(inst->model_state, 0' src/dsp.c && ! grep -q 'fm2_set_param' src/dsp.c && echo OK</automated>
  </verify>
  <done>set_param dispatches through the active model's vtable; MODEL switch memsets model_state and re-primes defaults through the active model; create() primes via the vtable; fm2 vtable exposes set_param; no logging added.</done>
</task>

<task type="auto">
  <name>Task 3: Wave-0 model-switch test harness (KICK-13) + wire into Makefile</name>
  <read_first>tests/test_render.c, tests/mock_host.c, tests/test_fm2.c (assertion + render-loop patterns), tests/wav.h, Makefile (TEST_SRCS / test targets), .planning/phases/B-remaining-9-kick-models/B-VALIDATION.md (Wave 0 requirements)</read_first>
  <files>tests/test_switch.c, Makefile</files>
  <action>
    Create `tests/test_switch.c` — a native harness (drives the real move_plugin_init_v2 -> create_instance via mock_host, same pattern as test_render.c) that, for the model-switch hazard (KICK-13):
    1. Creates one instance.
    2. For every ordered pair (A, B) in a sweep across all MODEL_COUNT models: `set_param(PK_MODEL, "<A/(COUNT-1)>")`, prime + trigger + render 512 frames; then `set_param(PK_MODEL, "<B/...>")`, trigger, render; then switch back to A, trigger, render. NOTE: MODEL is set via the normalized 0..1 value that dsp.c maps with `(int)dsp_parse_f(val)*...` — check how dsp.c parses PK_MODEL (currently `(int)dsp_parse_f(val)`, i.e. an integer index, clamped). Set PK_MODEL with the integer index as a string (e.g. "3") to match dsp.c's parse. Confirm against the dsp.c PK_MODEL branch you edited in Task 2 and match its parsing exactly.
    3. Assert on EVERY rendered buffer: all samples `isfinite` and `|x| <= 1.0` (no NaN/Inf/stale-blowup carried across a switch). Assert at least one switched-then-triggered render is non-silent (RMS > 1e-4) to prove re-init produced a live voice, not silence-from-garbage.
    4. Add a `_Static_assert`-style runtime check (or an assert) that `sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT` is validated in the registry (Pitfall 6) — if the registry TU is not yet full (models added in later plans), guard the loop by `MODEL_COUNT` and skip NULL vtable entries so this test compiles+passes in Wave 1 with only FM2 registered, and automatically covers new models as they land.
    Use plain C `assert` + a tiny main returning 0 on success (mirrors test_fm2.c). No third-party framework.
    In `Makefile`: add a `test-switch` target compiling `tests/test_switch.c tests/mock_host.c tests/wav.c tests/malloc_trap.c src/dsp.c src/ui.c src/models/*.c src/dsp_primitives.c` natively and running it; make `test` depend on `test-switch`. Use the existing `$(wildcard src/models/*.c)` idiom so new model TUs are picked up automatically.
  </action>
  <acceptance_criteria>
    - `tests/test_switch.c` exists and contains `PK_MODEL` and an `isfinite` assertion
    - `Makefile` contains a `test-switch` target and `test:` lists `test-switch` as a prerequisite (grep `test-switch` in Makefile returns >= 2)
    - `make test` exits 0 (`echo $?` == 0) with only FM2 registered
    - `tests/test_switch.c` asserts `sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT` OR skips NULL entries so it is forward-compatible with unregistered models
  </acceptance_criteria>
  <verify>
    <automated>make test && echo TEST_OK</automated>
  </verify>
  <done>tests/test_switch.c drives model-switch A->B->A + trigger and asserts finite/bounded/non-stale output; Makefile runs it under `make test`; suite green with FM2 registered and forward-compatible with later models.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 (native harness including the new switch test).
- dsp.c dispatches set_param through the vtable and memsets model_state on switch (grep gates above).
- Internal vtable extended, enum grown append-only, ABI structs + static_asserts untouched.
</verification>

<success_criteria>
- Both Phase-A blocking bugs are fixed (vtable-dispatched set_param; clean model-switch re-init) — verified by grep + green switch test.
- All 9 new model IDs and their PK_* keys are declared append-only in omega.h (MODEL_FM2=0 permanent).
- tests/test_switch.c is the KICK-13 automated gate and is forward-compatible as models are registered in later plans.
</success_criteria>

<output>
After completion, create `.planning/phases/B-remaining-9-kick-models/B-01-SUMMARY.md`
</output>
