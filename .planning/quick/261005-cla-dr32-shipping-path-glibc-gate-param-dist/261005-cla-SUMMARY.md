---
phase: quick-261005-cla
plan: 01
subsystem: ship + ui
tags: [dr32, ci, packaging, accessibility, ui_hierarchy, a11y]
requires: [dsp.so, dr32_engine.so build targets (already on main), pr1:help.json ref]
provides:
  - dr32_engine.so end-to-end shipping path (gate, dist, CI, deploy)
  - repo-root help.json (6 on-device Help topics)
  - accessible ui_hierarchy (spoken names + short_options codes)
  - engine param metadata (short_name/step/unit on all 9 models)
affects: [scripts/glibc_gate.sh, Makefile, .github/workflows/ci.yml, scripts/deploy.sh, src/ui.c, 9 model descriptors, tests/test_readback.c, tests/test_samples.c]
tech-stack:
  added: []
  patterns:
    - "glibc_gate.sh parameterized export symbol (2nd arg, default move_plugin_init_v2)"
    - "uiparam_t.short_options: spoken options + separate OLED code array"
    - "snprintf %% escape for literal percent in usr.c unit field"
key-files:
  created: [help.json, .planning/quick/261005-cla-dr32-shipping-path-glibc-gate-param-dist/261005-cla-SUMMARY.md]
  modified:
    - scripts/glibc_gate.sh
    - Makefile
    - .github/workflows/ci.yml
    - scripts/deploy.sh
    - src/ui.c
    - src/models/fm2.c
    - src/models/fm4.c
    - src/models/wtr.c
    - src/models/phy.c
    - src/models/hrd.c
    - src/models/dig.c
    - src/models/trs.c
    - src/models/ana.c
    - src/models/usr.c
    - tests/test_readback.c
    - tests/test_samples.c
decisions:
  - "TRS model spoken as 'Transient' (not pr1's 'Transistor') — accurate to the transient-click engine"
  - "USR spoken as 'User Sample'; usr.c unit literals escaped %% (snprintf path, unlike the other models' plain string literals)"
  - "Discarded pr1's groove.c/groove.h/params.c/test_groove.c changes entirely (superseded by main v0.4.1); re-applied accessibility intent only"
metrics:
  duration: 7min
  tasks: 3
  files: 16
  completed: 2026-10-05
---

# Phase quick-261005-cla Plan 01: DR32 Shipping Path + Accessibility Re-base Summary

Shipped the already-merged DR32 engine plugin end-to-end (parameterized glibc gate, dist/CI/deploy packaging) and re-based PR #1's screen-reader/Help accessibility layer onto current main v0.4 without regressing the v0.4.1 DSP — spoken ui_hierarchy names with OLED codes preserved in short_options, engine param metadata across all 9 models, two TRS name-collision renames, and current_pad dropped.

## What Was Built

**Part 1 — DR32 shipping path (Task 1):**
- `scripts/glibc_gate.sh` takes an optional 2nd export-symbol arg (`EXPECT`, default `move_plugin_init_v2`); the GLIBC/libmvec/export checks all gate on it. `./scripts/glibc_gate.sh build/dr32_engine.so dr32_engine_plugin` now passes; the existing no-arg dsp.so invocation is unchanged.
- `Makefile dist:` prereq extended to `dsp.so dr32_engine.so`; copies dsp.so + dr32_engine.so + module.json + help.json into the release tarball.
- `.github/workflows/ci.yml` cross-build-gate job adds a `make dr32_engine.so` build step and a dr32 gate step, and uploads dr32_engine.so + help.json alongside dsp.so + module.json.
- `scripts/deploy.sh` atomically uploads dr32_engine.so (`.new` + ssh mv) in a block mirroring the dsp.so one, with its own existence check.

**Part 2a — help.json + ui.c accessibility (Task 2):**
- `help.json` written verbatim from `git show pr1:help.json` (byte-identical, 6 topics, lines ≤20 chars).
- `uiparam_t` gains a trailing `short_options` field (positional initializers default it to NULL). `ui_emit_param` UP_ENUM branch emits `"short_options":%s` when non-NULL.
- MODEL and FX enums carry spoken `options` (e.g. "FM 2-Op", "Saturate") with the original codes in `OPT_MODEL_SHORT`/`OPT_FX_SHORT` wired via short_options.
- P_PERF/P_KICK1/P_KICK2/P_GROOVE1 names restyled to spoken English (CMPDR → "Compressor Drive", HPF → "High-Pass", etc.); OLED short_name grid codes untouched. All v0.4 Gen params preserved.
- Root object drops `child_index_param:"current_pad"` (get_param never served it).

**Part 2b — engine metadata + collisions + tests (Task 3):**
- All 9 model `p2_slot_desc` JSON interiors gain `short_name` + (floats) `step`/`unit`; spoken `name` values.
- `trs.c`: `TRANS DEC` → "Click Decay" (CLKDEC), `CURVE` → "Sweep Curve" (SWPCRV) — disambiguates the kick-1 shared CURVE.
- `test_readback.c`: assertions updated to the spoken MODEL/FX options + `short_options` code migration.
- `test_samples.c`: Sample Select spoken-name assertion.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] usr.c snprintf literal-percent warning**
- **Found during:** Task 3 (make test compile)
- **Issue:** usr.c builds its descriptor via `snprintf` (dynamic sample enum), unlike the other 8 models which use plain string literals. Adding `"unit":"%"` made the compiler read `%"` as a format specifier (`-Wformat-invalid-specifier`).
- **Fix:** Escaped the literal percent as `%%` in the three usr.c float unit fields.
- **Files modified:** src/models/usr.c
- **Commit:** 2f32b65

**2. [Rule 1 - Bug] test_samples.c name-string assertion broke on rename**
- **Found during:** Task 3 (make test)
- **Issue:** test_samples line 75 asserted the old `"SAMPLE SEL"` literal; Task 3 renamed it to "Sample Select".
- **Fix:** Updated the assertion to the new spoken name (grid short_name "SMPSEL").
- **Files modified:** tests/test_samples.c
- **Commit:** 2f32b65

### Intentional divergences from plan text
- Plan example used pr1's "Transistor" for TRS; chose "Transient" (the model is the transient-click engine) and "User Sample" for USR. Documented as a decision.

## Verification

- `make test` GREEN — all 12 suites (test-fm2, test-fx, test-dr32, test-switch, test-params, test-distinct, test-gen, test-groove, test-readback, test-samples, test-perf, test-taps-redesign). `test_dr32_engine: PASS`. No compiler warnings.
- `bash -n scripts/glibc_gate.sh` clean; `bash -n scripts/deploy.sh` clean.
- `git show pr1:help.json | diff - help.json` empty (byte-identical).
- `grep current_pad src/ui.c` returns nothing.
- Docker cross-compile / glibc gate / on-device deploy verified by INSPECTION only (no Docker/hardware on this macOS host) — CI/hardware authoritative, as the plan states.

## Known Stubs

None. All ui_hierarchy changes are display/accessibility metadata strings; no stubbed data sources introduced. module.json untouched (p-lock automation unaffected). No audio-thread / DSP math changed.

## Self-Check: PASSED
- help.json: FOUND
- src/ui.c short_options: FOUND
- Commits ae334b6, 300f553, 2f32b65: FOUND (see below)
