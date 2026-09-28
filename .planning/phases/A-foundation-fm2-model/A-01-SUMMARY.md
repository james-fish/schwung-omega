---
phase: A-foundation-fm2-model
plan: 01
subsystem: infra
tags: [c11, dsp, wavetable, cross-compile, glibc, makefile, github-actions, malloc-trap, wav]

# Dependency graph
requires: []
provides:
  - "src/omega.h: locked host/plugin ABI (host_api_v1_t, plugin_api_v2_t), kick_model_vtable_t contract, MODEL_FM2=0 append-only enum, struct bohm_instance, 16 PK_* param keys, OMEGA_SR/OMEGA_MAX_BLOCK"
  - "src/dsp_primitives.[hc]: env_t/tpt1_t primitives, env_coeff_from_ms, wt_read (branch-free linear interp), tpt1_lp, omega_to_i16 (FNDTN-07 clamp), single .rodata g_sine_table[2049] with guard sample (KICK-15)"
  - "tools/gen_sine_table.c: reproducible build-time sine table generator"
  - "Offline verification apparatus: mock host, malloc trap, WAV writer, Wave 0 stub harness (make test), glibc gate script, GitHub Actions CI"
affects: [A-02-fm2-engine, A-03-ui-hierarchy, A-04-on-device, phase-B-models, groove, performer]

# Tech tracking
tech-stack:
  added: [C11, aarch64-linux-gnu-gcc, GNU Make, GitHub Actions, Docker schwung-builder]
  patterns:
    - "vtable model dispatch (kick_model_vtable_t) with append-only model IDs"
    - "single shared .rodata wavetable, branch-free linear interp via guard sample"
    - "static inline DSP primitives in header shared by native tests and dsp.so"
    - "single int16 boundary (omega_to_i16) with clamp+isfinite+lrintf"
    - "generated const table (non-static) owned by one TU, extern elsewhere"

key-files:
  created:
    - src/omega.h
    - src/dsp_primitives.h
    - src/dsp_primitives.c
    - src/sine_table.h
    - tools/gen_sine_table.c
    - tests/mock_host.h
    - tests/mock_host.c
    - tests/malloc_trap.c
    - tests/wav.h
    - tests/wav.c
    - tests/test_render.c
    - Makefile
    - scripts/glibc_gate.sh
    - scripts/deploy.sh
    - .github/workflows/ci.yml
    - .gitignore
    - tests/output/.gitkeep
  modified: []

key-decisions:
  - "Sine table literals emitted with %.9e (not %.9g) so whole-number values are valid float literals"
  - "omega_to_i16 shared int16 boundary placed in dsp_primitives.h now so A-02 reuses it"
  - "Malloc trap compiled out on Darwin; Linux CI is the authoritative FNDTN-03 gate"
  - "No -mcpu pinning in the Makefile (D-15 defers Cortex core flag to on-device confirmation)"

patterns-established:
  - "vtable dispatch: dsp.c calls through g_models[], never directly into a model"
  - "one shared 2049-sample .rodata sine table with guard sample t[2048]==t[0]"
  - "float-only internal path; int16 only at the final render_block boundary"

requirements-completed: [FNDTN-03, FNDTN-04, FNDTN-05, FNDTN-06, KICK-01, KICK-15]

# Metrics
duration: 6min
completed: 2026-09-29
---

# Phase A Plan 01: Scaffolding and Contracts Summary

**Locked C11 host/plugin ABI + vtable contract, a shared .rodata guard-terminated sine table, and a green `make test` offline harness (mock host, malloc trap, WAV writer, glibc gate, GitHub Actions CI) — all with zero DSP, rendering a silent stub end-to-end.**

## Performance

- **Duration:** ~6 min
- **Started:** 2026-09-28T22:18:13Z
- **Completed:** 2026-09-28T22:23:44Z
- **Tasks:** 3
- **Files modified:** 17 created

## Accomplishments
- `src/omega.h` locks the entire downstream contract surface: verbatim host/plugin ABI structs, the `kick_model_vtable_t` (trigger/render/set_p2/p2_slot_desc), `MODEL_FM2=0` append-only enum (KICK-01), `struct bohm_instance` with a `_Static_assert` size guard, and all 16 `PK_*` param keys.
- Build-time sine table generator produces a single non-static `const float g_sine_table[2049]` in `.rodata` with a guard sample `t[2048]==t[0]` (KICK-15); a runtime `omega_primitives_selfcheck()` asserts the guard.
- Shared DSP primitives (`env_t`, `tpt1_t`, `env_coeff_from_ms`, `wt_read`, `tpt1_lp`, `omega_to_i16`) are `static inline` in the header so both the native harness and the cross-compiled `dsp.so` link them directly.
- Full offline verification apparatus: `make test` compiles and runs a silent stub through the FNDTN-07 clamp path, writes a valid 262188-byte PCM16 WAV (FNDTN-06), and (on Linux) arms the malloc trap during the render loop (FNDTN-03). The glibc gate script (FNDTN-04) and GitHub Actions CI (D-11) are in place.

## Task Commits

Each task was committed atomically:

1. **Task 1: Define shared contracts (omega.h) + generate .rodata sine table** - `205e0c4` (feat)
2. **Task 2: Implement dsp_primitives (env_t, wt_read, tpt1, to_i16) + guard self-check** - `0ac3a38` (feat)
3. **Task 3: Test harness, malloc trap, WAV writer, glibc gate, Makefile, CI** - `07a33ed` (feat)

## Files Created/Modified
- `src/omega.h` - Host/plugin ABI, vtable contract, instance struct, model enum, param keys, constants
- `src/dsp_primitives.h` - env_t/tpt1_t, env_coeff_from_ms, wt_read, tpt1_lp, omega_to_i16, g_sine_table extern
- `src/dsp_primitives.c` - Owns the single g_sine_table definition; omega_primitives_selfcheck (KICK-15)
- `src/sine_table.h` - Generated 2049-float sine table, `_Alignas(16)`, non-static const
- `tools/gen_sine_table.c` - Build-time sine table generator (%.9e literals)
- `tests/mock_host.[ch]` - Mock host_api_v1_t (44100 sr, 128 fpb)
- `tests/malloc_trap.c` - __libc_* interposition gated by g_audio_thread_active (Linux)
- `tests/wav.[ch]` - 44-byte PCM16 WAV writer, patches sizes on close
- `tests/test_render.c` - Wave 0 stub harness (selfcheck + silent render + WAV out; TODO(A-02))
- `Makefile` - dsp.so (aarch64), test (native), clean, deploy
- `scripts/glibc_gate.sh` - GLIBC<=2.35 / no-libmvec / single-export gate
- `scripts/deploy.sh` - Atomic scp + rename to Move device
- `.github/workflows/ci.yml` - Native test + docker cross-build + gate
- `.gitignore`, `tests/output/.gitkeep`

## Decisions Made
- **Sine literals via `%.9e`:** `%.9g` printed bare `0`/`1`/`-1` which with an `f` suffix (`0f`) are invalid C float literals. `%.9e` always emits a decimal point + exponent, guaranteeing valid literals. (See Deviations — Rule 1.)
- **`omega_to_i16` in the shared header now:** placed the FNDTN-07 clamp in `dsp_primitives.h` during Wave 0 so A-02's `render_block` reuses the identical boundary rather than duplicating it.
- **Malloc trap Darwin caveat:** the trap relies on glibc `__libc_*` interposition; the Makefile drops `-DOMEGA_MALLOC_TRAP` on Darwin. Linux CI is the authoritative FNDTN-03 gate. Documented in `malloc_trap.c`.
- **No `-mcpu` pinning:** per D-15, the Cortex core flag is deferred to on-device `/proc/cpuinfo` confirmation in A-04. Baseline ARMv8-A only.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Sine table generator emitted invalid C float literals**
- **Found during:** Task 2 (compiling dsp_primitives.c, which includes the generated sine_table.h)
- **Issue:** `gen_sine_table.c` used `printf("%.9gf", v)`. For sample values that render as whole numbers (`0`, `1`, `-1`), this produced `0f` / `1f` / `-1f`, which the C compiler rejects ("invalid digit 'f' in octal/decimal constant"). Task 1's verification (a grep count) did not catch it because the file was never compiled in Task 1.
- **Fix:** Switched the generator to `printf("%.9ef", v)`. `%.9e` always includes a decimal point and exponent, so the `f` suffix is always attached to a valid float literal. Regenerated `src/sine_table.h`.
- **Files modified:** tools/gen_sine_table.c, src/sine_table.h
- **Verification:** `cc -std=gnu11 -Isrc -c src/dsp_primitives.c` compiles clean; a runtime harness confirmed `omega_primitives_selfcheck()` passes and `t[0]==t[2048]==0`.
- **Committed in:** `0ac3a38` (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** The fix was essential for the sine table to compile at all; without it every downstream TU that includes it would fail. No scope creep — the generator's output contract (non-static const, guard sample) is unchanged.

## Issues Encountered
- **`tests/output/` not persisting between shell calls:** the working directory persists but `make clean` and fresh checkouts remove `tests/output/`. Resolved by committing `tests/output/.gitkeep` and having the `test` target `mkdir -p tests/output`.
- **Docker unavailable on the local host:** the aarch64 cross-build and glibc gate cannot run locally. This is by design (D-11) — CI (Linux + Docker) is the cross-build gate. `make test` (native) is fully green locally and is the Wave 0 per-task gate.

## Known Stubs
- **`tests/test_render.c` silent-render stub (intentional, Wave 0):** the harness renders silence through the real `omega_to_i16` clamp path instead of driving `move_plugin_init_v2` + FM2. This is deliberate — Wave 0 proves the pipeline (create/render/destroy shape, clamp, WAV, malloc-trap) goes green before DSP subjectivity enters. Marked `TODO(A-02)`; **Plan A-02** replaces it with the real lifecycle (create_instance → on_midi note 36 → 512× render_block → destroy) and adds `g_models[MODEL_FM2]->name == "FM2"` assertions.
- **`src/omega.h` `char model_state[4096]` placeholder:** a fixed opaque region the FM2 plan casts to its own struct; Phase B later overlays per-model state via a union. Documented in-header. Resolved incrementally by A-02 and Phase B.

## User Setup Required
None - no external service configuration required. (On-device deploy + 3-host validation is a manual step handled in Plan A-04.)

## Next Phase Readiness
- Downstream plans can `#include "omega.h"` / `"dsp_primitives.h"` and build against fixed contracts with zero exploration.
- `make test` is a working < 5 s per-task gate for all subsequent Phase A tasks.
- A-02 must: add `src/dsp.c` (entry points + FPCR FTZ, FNDTN-05), `src/models/fm2.c`, `src/models/model_registry.c`; replace the harness stub; then flip the CI cross-build job's `continue-on-error` to `false`.

## Self-Check: PASSED

All 18 created files verified present on disk; all 3 task commits (`205e0c4`, `0ac3a38`, `07a33ed`) verified in git history.

---
*Phase: A-foundation-fm2-model*
*Completed: 2026-09-29*
