---
phase: A-foundation-fm2-model
plan: 02
subsystem: dsp-engine
tags: [c11, fm-synthesis, wavetable, vtable-dispatch, plugin-abi, fpcr, module-manifest, tdd]

# Dependency graph
requires:
  - "A-01: src/omega.h (ABI, kick_model_vtable_t, MODEL_FM2 enum, PK_* keys, bohm_instance), src/dsp_primitives.h (env_t/tpt1_t, wt_read, env_coeff_from_ms, omega_to_i16), g_sine_table[2049], mock host + malloc trap + WAV writer + Makefile + glibc gate"
provides:
  - "src/models/fm2.c: full 2-op wavetable FM kick — fm2_trigger/render/set_p2/p2_slot_desc + g_fm2_vtable + fm2_set_param; dual 808/909 pitch env, own FM-index decay env, COLOR + TRS TNE TPT filters, all 8 Page-1 + 3 Page-2 params"
  - "src/dsp.c: move_plugin_init_v2 (single exported symbol) + 6 vtable fns; dispatch-only, single calloc/free, FPCR FTZ, clamped int16, locale-independent parse; temporary omega_build_ui stub (OMEGA_HAS_UI gate for A-03)"
  - "src/models/model_registry.c: g_models[MODEL_COUNT] = { &g_fm2_vtable } (KICK-01 append-only)"
  - "module.json: id omega, sound_generator, drums, api_version 2 (FNDTN-02)"
  - "tests/test_render.c: real move_plugin_init_v2 lifecycle harness; tests/test_fm2.c: focused FM2 DSP unit test"
affects: [A-03-ui-hierarchy, A-04-on-device, phase-B-models]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "dispatch-only entry-point TU (dsp.c) — zero DSP math, routes to g_models[] + fm2_set_param"
    - "model state overlaid on bohm_instance.model_state via cast + _Static_assert size guard"
    - "all transcendentals (tanf/expf) precomputed in set_param/trigger; render loop is pure float arithmetic"
    - "OMEGA_HAS_UI compile gate: dsp.c ships a stub omega_build_ui that A-03's ui.c replaces"
    - "output engine self-limits to [-1,1] (0.6 body / 0.4 click split) so no clip before the int16 boundary"

key-files:
  created:
    - src/models/fm2.c
    - src/dsp.c
    - src/models/model_registry.c
    - module.json
    - tests/test_fm2.c
  modified:
    - src/omega.h
    - tests/test_render.c
    - Makefile

key-decisions:
  - "FM2 output scaled 0.6 body / 0.4 click so the natural (unclipped) engine output stays within [-1,1] before int16 conversion — satisfies the FNDTN-07/D-12 |x|<=1.0 requirement without relying on the clamp to mask overflow"
  - "TDD RED+GREEN committed together for Task 1: the RED test file (tests/test_fm2.c) is itself a Task-1 deliverable, so a single feat commit carries the failing-then-passing test plus the engine"
  - "dsp.c provides a temporary omega_build_ui stub behind #ifndef OMEGA_HAS_UI so A-02 links a standalone module; A-03's ui.c defines OMEGA_HAS_UI and owns the real hierarchy + the D-10 buf_len log"
  - "Two native test targets: test-fm2 (focused DSP unit test on a stack instance) + test (full move_plugin_init_v2 lifecycle); make test runs both"

requirements-completed: [FNDTN-01, FNDTN-02, FNDTN-05, FNDTN-07, KICK-01, KICK-02, KICK-12]

# Metrics
duration: 6min
completed: 2026-09-29
---

# Phase A Plan 02: FM2 Engine and Entry Points Summary

**A complete, fully-parameterized 2-op wavetable FM techno kick (dual 808/909 pitch sweep, dedicated FM-index decay env, COLOR/TRS-TNE TPT filters) wired through a dispatch-only plugin entry point (single exported `move_plugin_init_v2`, FPCR flush-to-zero, single calloc/free, clamped int16) and proven audible + byte-deterministic + parameter-responsive by a real move_plugin_init_v2 lifecycle harness.**

## Performance

- **Duration:** ~6 min
- **Started:** 2026-09-28T22:27:12Z
- **Completed:** 2026-09-28T22:33:13Z
- **Tasks:** 3
- **Files:** 5 created, 3 modified

## Accomplishments
- `src/models/fm2.c` is the full FM2 engine (no stubs except FX TYPE/AMT identity passthrough, per D-03). Both carrier and modulator read the shared `.rodata` `g_sine_table` (D-04); the CURVE param lerps a fast 909 pitch env against a slow 808 env (D-06); the FM index has its own decay envelope so the attack is bright and the tail is pure (KICK-02); COLOR and TRS TNE are TPT 1-pole lowpasses (D-07) with cutoffs precomputed in `set_param` (no per-sample `tanf`). All 8 Page-1 + 3 Page-2 params map to audible DSP.
- `src/dsp.c` is dispatch-only (D-01): `move_plugin_init_v2` is the single exported symbol; `render_block` sets FPCR flush-to-zero (FNDTN-05), dispatches through `g_models[inst->model]->render`, and writes the clamped int16 boundary (FNDTN-07). Exactly one `calloc` in create and one `free` in destroy (FNDTN-03); no `atof`/`strtod` anywhere; `get_param` returns `-1` for unknown keys.
- `src/models/model_registry.c` holds the append-only `g_models[]` (KICK-01), the only file that knows the full model list.
- `module.json` declares `id omega`, `sound_generator`, `drums`, `api_version 2` (FNDTN-02); parses as valid JSON.
- `tests/test_render.c` now drives the real lifecycle (create → on_midi note 36 → 512× render_block → destroy) and asserts FM2 is non-silent, byte-deterministic, and that PITCH/LENGTH/FM INDEX audibly change the output. `tests/test_fm2.c` is a focused DSP unit test on a stack instance. `make test` runs both and writes `tests/output/fm2_kick.wav` (262188 bytes).

## Task Commits

Each task committed atomically:

1. **Task 1: Implement the FM2 engine (fm2.c)** — `bccf4e9` (feat) — TDD RED (link failure) → GREEN (test passes) + one output-scale fix
2. **Task 2: Plugin entry points (dsp.c) + registry + module.json** — `c4d028c` (feat)
3. **Task 3: Wire real lifecycle + param-response assertions into harness** — `3cac29b` (test)

## Files Created/Modified
- `src/models/fm2.c` (created) — FM2 engine: fm2_state overlay + `_Static_assert`, locale-independent `parse_f`, fm2_set_param (11 keys), fm2_trigger (dual pitch env + amp/index/trs envs), fm2_render (per-sample loop, no transcendentals), fm2_set_p2, fm2_p2_slot_desc (JSON fragment), g_fm2_vtable
- `src/dsp.c` (created) — move_plugin_init_v2 + omega_create/destroy/on_midi/set_param/get_param/render_block; omega_set_ftz (FPCR FZ); dsp_parse_f; stub omega_build_ui
- `src/models/model_registry.c` (created) — g_models[MODEL_COUNT] = { &g_fm2_vtable }
- `module.json` (created) — manifest (FNDTN-02)
- `tests/test_fm2.c` (created) — focused FM2 DSP unit test (TDD)
- `src/omega.h` (modified) — added `extern const kick_model_vtable_t g_fm2_vtable;` + `void fm2_set_param(...)` prototypes
- `tests/test_render.c` (modified) — replaced the A-01 stub with the real move_plugin_init_v2 lifecycle + param-response assertions
- `Makefile` (modified) — added `test-fm2` target; `test` depends on it; `TEST_SRCS` now compiles dsp.c + fm2.c + model_registry.c

## Decisions Made
- **FM2 output scaled 0.6 body / 0.4 click:** the summed carrier (peak ~amp) + click (peak ~trs_amp) can exceed 1.0. Rather than lean on the int16 clamp to hide overflow, the engine self-limits so the *float* output naturally stays within [-1,1] before conversion — directly satisfying the T6 / FNDTN-07 / D-12 requirement. (See Deviations — Rule 1.)
- **TDD RED+GREEN in one Task-1 commit:** `tests/test_fm2.c` is a Task-1 deliverable and its RED failure was demonstrated (undefined `g_fm2_vtable`/`fm2_set_param`) before the engine was written; the single feat commit carries the now-passing test plus the engine.
- **`omega_build_ui` stub behind `#ifndef OMEGA_HAS_UI`:** lets A-02 link a standalone module while leaving A-03's ui.c to own the real hierarchy and the D-10 buf_len log.
- **Two native test targets:** `test-fm2` (unit) + `test` (lifecycle); `make test` runs both as the < 5 s per-task gate.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] FM2 engine output exceeded [-1,1] before the int16 boundary**
- **Found during:** Task 1 (running the TDD GREEN test; T6 asserts every float sample is finite and |x|<=1.0)
- **Issue:** With amp (velf ~0.79 × sustain contour), full carrier (±1.0), and the summed click layer, `car_out*amp + click` peaked above 1.0, tripping the T6 bound assertion. The int16 clamp would have masked it, but D-12/FNDTN-07 require the *float* magnitude to be ≤ 1.0 before conversion.
- **Fix:** Scaled the mix to `car_out * amp * 0.6f + click * 0.4f` so the natural, unclipped engine output stays in range while preserving a present transient.
- **Files modified:** src/models/fm2.c
- **Verification:** `make test` — both test_fm2 and test_render green; every sample passes the isfinite + |x|<=1.0 assertions.
- **Committed in:** `bccf4e9` (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Cosmetic to the recipe (an output gain split); the per-sample structure, envelope wiring, and param maps are exactly as planned. No scope change.

## Issues Encountered
- **Docker unavailable on the local host:** the aarch64 cross-build (`make dsp.so`) and glibc/libmvec/export gate cannot run locally — this is by design (D-11); CI (Linux + Docker) is the authoritative cross-build gate. Native `make test` is fully green locally and is the per-task gate. A-01 already flagged this.
- **`-DOMEGA_MALLOC_TRAP` link check fails on macOS:** expected — `__libc_*` interposition is glibc/Linux-only; the Makefile drops the flag on Darwin. Native `make test` (no trap) links and runs clean; Linux CI arms the FNDTN-03 trap.

## Known Stubs
- **`src/dsp.c` `omega_build_ui` (intentional, gated):** returns a minimal `{"pages":[]}` behind `#ifndef OMEGA_HAS_UI`. This is the deliberate A-02→A-03 handoff: A-03's `src/ui.c` defines `OMEGA_HAS_UI` and owns the real `ui_hierarchy` (D-08) plus the one-shot D-10 buf_len log. Marked `TODO(A-03)` in-code. Does not block A-02's goal (KICK-01/02/12, FNDTN-01/05/07 are all proven end-to-end offline).
- **FX TYPE / FX AMT identity passthrough (intentional, per D-03/Claude's Discretion):** `fm2_set_param` stores `fx_type`/`fx_amt` but applies no processing. The 5 real FX modes are KICK-14 / Phase B. Marked `TODO(Phase B)` in-code. This is the one explicitly-allowed FM2 stub.

## User Setup Required
None. (On-device deploy + 3-host validation is a manual step handled in Plan A-04.)

## Next Phase Readiness
- A module now builds end-to-end: `make test` renders a real, parameter-responsive FM2 kick to WAV; `make dsp.so` should now succeed in the Docker image (dsp.c provides `move_plugin_init_v2`, so the `-Wl,--no-undefined` link resolves — modulo the `omega_build_ui` symbol which is satisfied by the A-02 stub or A-03's ui.c).
- A-03 must: add `src/ui.c` (real `ui_hierarchy`, D-08), define `OMEGA_HAS_UI` (removing dsp.c's stub), implement the D-10 one-shot buf_len log, and consume `fm2_p2_slot_desc` for the FM2 Page-2 slots. Then flip the CI cross-build job's `continue-on-error` to `false`.
- Phase B appends models to `g_models[]` in `model_registry.c` (append-only) and overlays their state on `bohm_instance.model_state` the same way fm2.c does.

## Self-Check: PASSED

All 5 created files verified present on disk; all 3 task commits (`bccf4e9`, `c4d028c`, `3cac29b`) verified in git history.

---
*Phase: A-foundation-fm2-model*
*Completed: 2026-09-29*
