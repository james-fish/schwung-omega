---
phase: B-remaining-9-kick-models
plan: 01
subsystem: kick-model-dispatch
tags: [vtable, dispatch, model-switch, re-init, registry, test-harness, KICK-13]
requires:
  - "Phase A: locked ABI (omega.h host_api_v1_t/plugin_api_v2_t), kick_model_vtable_t, g_models registry, dsp.c dispatch, fm2.c, offline WAV/malloc-trap harness"
provides:
  - "kick_model_vtable_t.set_param fn ptr (internal vtable extension; ABI untouched)"
  - "model_id_t append-only enum grown to MODEL_COUNT=10 (FM2=0 permanent)"
  - "37 new PK_* Page-2 param-key macros for FM4/WTR/PHY/HRD/DIG/TRS/ANA/USR/GEN"
  - "9 extern kick_model_vtable_t declarations (g_fm4_vtable ... g_gen_vtable)"
  - "designated-initializer g_models[MODEL_COUNT] registry with FM2 set + 9 NULL slots (intermediate-compilation contract)"
  - "vtable-dispatched set_param in dsp.c (Pitfall 2 fix)"
  - "clean model-switch re-init: memset(model_state) + re-prime on PK_MODEL change (Pitfall 1 / KICK-13)"
  - "NULL-slot guards on set_param/trigger/render/create-prime (unimplemented model = silence, no NULL deref)"
  - "tests/test_switch.c model-switch hazard harness (KICK-13 automated gate)"
affects:
  - "src/omega.h (internal vtable + enum + PK_* keys — ABI structs untouched)"
  - "src/dsp.c (dispatch + switch re-init + NULL guards)"
  - "src/models/model_registry.c (designated-init NULL placeholders)"
  - "src/models/fm2.c (vtable .set_param field)"
  - "Makefile (test-switch target)"
tech-stack:
  added: []
  patterns:
    - "Designated-initializer registry with NULL placeholders — array always MODEL_COUNT long, compiles+links at every wave; later plans replace their own NULL"
    - "memset-on-switch + re-prime-through-active-model re-init for the shared model_state[4096] overlay"
    - "NULL-guarded vtable dispatch so partial registries are safe (unimplemented model renders silence)"
key-files:
  created:
    - "tests/test_switch.c"
  modified:
    - "src/omega.h"
    - "src/dsp.c"
    - "src/models/model_registry.c"
    - "src/models/fm2.c"
    - "Makefile"
decisions:
  - "Added kick_model_vtable_t.set_param placed after render, before set_p2; set_p2/p2_slot_desc retained (set_p2 delegates to set_param in fm2). No _Static_assert binds the internal vtable, so extension is ABI-safe."
  - "Enum order locked append-only: FM2,FM4,WTR,PHY,HRD,DIG,TRS,ANA,USR,GEN (MODEL_COUNT=10). FM2=0 permanent."
  - "Registry uses designated initializers; only FM2 non-NULL in Wave 1; each later model plan adds its own [MODEL_XXX]=&g_xxx_vtable line WITH the .c that defines the symbol (avoids link error on undefined externs)."
  - "test_switch.c asserts finite/bounded via the int16-range check at the omega_to_i16 boundary (same pattern as test_render.c) — equivalent to isfinite+clamp proof."
metrics:
  duration: "4min"
  tasks: 3
  files: 5
  completed: "2026-09-29"
---

# Phase B Plan 01: Vtable Dispatch and Switch Re-init Summary

Fixed the two Phase-A blocking bugs (hardcoded `fm2_set_param` dispatch; missing `model_state` re-init on model switch) and stood up the Wave-0 designated-initializer registry + KICK-13 switch-test scaffolding that every later Phase-B model plan depends on.

## What Was Built

- **Internal vtable extension (`src/omega.h`):** added a `set_param` function pointer to `kick_model_vtable_t` (internal struct, no `_Static_assert` binding it, not part of the host ABI). The locked `host_api_v1_t`/`plugin_api_v2_t` structs and their `+120`/`+56` `_Static_assert`s were left untouched.
- **Append-only enum growth:** `model_id_t` grown to `{ MODEL_FM2=0, MODEL_FM4, MODEL_WTR, MODEL_PHY, MODEL_HRD, MODEL_DIG, MODEL_TRS, MODEL_ANA, MODEL_USR, MODEL_GEN, MODEL_COUNT }` (MODEL_COUNT=10). FM2=0 remains permanent.
- **37 new `PK_*` Page-2 keys** for all 9 models, each unique (TRS gets its own `PK_TRS_CURVE` distinct from Page-1 `PK_CURVE`), plus 9 `extern` vtable declarations for the model `.c` files that later plans add.
- **Designated-initializer registry (`src/models/model_registry.c`):** `g_models[MODEL_COUNT]` now uses `[MODEL_FM2] = &g_fm2_vtable` with all other slots C-zero-initialised to NULL, guarded by `_Static_assert(sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT)`. This is the intermediate-compilation contract: the array is always MODEL_COUNT long and links at every wave; each later plan replaces its own NULL.
- **dsp.c dispatch + switch re-init (`src/dsp.c`):**
  - `omega_set_param` else-branch now dispatches through `g_models[inst->model]->set_param` (NULL-guarded) instead of the hardcoded `fm2_set_param` (Pitfall 2).
  - `PK_MODEL` branch acts only on an actual change: sets `inst->model`, `memset`s `model_state`, and re-primes defaults through the incoming model's `set_param` (Pitfall 1 / KICK-13). NULL incoming slot clears state and renders silence.
  - `on_midi` trigger, `omega_render_block` render, and `omega_create`'s prime loop are all NULL-guarded; an unimplemented model renders a zeroed block.
- **fm2.c:** `.set_param = fm2_set_param` added to `g_fm2_vtable` (the existing `fm2_set_param` already handles Page-1 + FM2 Page-2 keys). `.set_p2` retained.
- **Switch harness (`tests/test_switch.c`) + Makefile:** drives the real lifecycle, iterates every ordered pair of implemented models doing A->trigger->render, B->trigger->render, A->trigger->render; asserts finite/bounded output on every buffer, non-silent re-init (RMS/energy > threshold), registry length == MODEL_COUNT, and that a switch to a NULL slot renders silence without crashing. Skips NULL slots so it is forward-compatible. `make test` now depends on `test-switch`, which uses the `$(wildcard src/models/*.c)` idiom to pick up future model TUs automatically.

## Verification

- `make test` exits 0: `test_fm2` (FM2 unit), `test_switch` (1 pair exercised in Wave 1 — FM2->FM2 — plus NULL-slot silence + registry-length asserts), and `test_render` (full lifecycle) all pass with the partial registry (only FM2 non-NULL, 9 NULL slots).
- Grep gates satisfied: `g_models[inst->model]->set_param` present; `memset(inst->model_state, 0, sizeof inst->model_state)` present; no live `fm2_set_param()` call in dsp.c (only comment references); NULL guards present; no `host->log` invocation added; ABI `render_block) == 56` assert intact; `[MODEL_FM2] = &g_fm2_vtable` designated init + registry length assert present; PK count 16 -> 53 (+37).

## Deviations from Plan

None - plan executed exactly as written. (The `.set_param` field on `g_fm2_vtable` was set in the Task 1 commit rather than the Task 2 commit, because adding the new vtable field in Task 1 required initializing it for FM2 for the partial registry to link and pass `make test` at the Task 1 boundary. Functionally identical to the plan; no behavior change.)

## Known Stubs

None. All 9 non-FM2 registry slots are intentionally NULL per the documented intermediate-compilation contract (each later Phase-B plan replaces its own NULL). dsp.c and the test harness treat NULL slots as silence, which is the designed Wave-1 behavior, not a stub blocking this plan's goal.

## Self-Check: PASSED

- FOUND: tests/test_switch.c
- FOUND: .planning/phases/B-remaining-9-kick-models/B-01-SUMMARY.md
- FOUND commit 0308842 (Task 1)
- FOUND commit 08d6255 (Task 2)
- FOUND commit 970fca3 (Task 3)
