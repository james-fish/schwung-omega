---
phase: E1-critical-audio-fixes
plan: 01
subsystem: kick-models
tags: [bugfix, fx, dispatch, audio-critical]
dependency_graph:
  requires: []
  provides: [correct-fx-type-dispatch]
  affects: [src/models/fm2.c, src/models/fm4.c, src/models/wtr.c, src/models/trs.c, src/models/ana.c, src/models/usr.c, src/models/phy.c, src/models/hrd.c, src/models/dig.c, src/models/gen.c, tests/test_fx.c]
tech_stack:
  added: []
  patterns: [integer-enum-dispatch, parse_f-direct-for-enums]
key_files:
  created: [tests/test_fx.c (T6 added)]
  modified:
    - src/models/fm2.c
    - src/models/fm4.c
    - src/models/wtr.c
    - src/models/trs.c
    - src/models/ana.c
    - src/models/usr.c
    - src/models/phy.c
    - src/models/hrd.c
    - src/models/dig.c
    - src/models/gen.c
    - tests/test_fx.c
decisions:
  - "fx_type stored as float 0..4 (not 0..1) — parse_f(val)+0.5f round-cast preserves integer enum identity through the float field"
  - "gen.c fixed as Rule 1 auto-fix — plan listed 9 files but gen.c had identical broken pattern"
  - "hrd.c drive_fx dispatch (h->drive_mode) left unchanged — it was correct, only the post-kick fx needed fixing"
metrics:
  duration: 4min
  completed: "2026-09-30"
  tasks: 2
  files: 11
requirements: ["#1", "#2"]
---

# Phase E1 Plan 01: FX Type Dispatch Fix Summary

Corrected the FX type selector across all 10 kick model files — Clip/SAT/Fold/Crush now dispatch correctly instead of all routing to FX_CRUSH (mode 4).

## What Was Done

**Bug #1 root cause:** Every model stored `fx_type` through `clampf(parse_f(val), 0.0f, 1.0f)` which mapped integer strings "1"-"4" to 1.0f. Downstream `(int)(fm->fx_type * 4.0f + 0.5f)` then mapped 1.0 → 4 = FX_CRUSH. All non-zero modes silently dispatched to Crush.

**Fix pattern (identical across all 10 models):**

Set param PK_FX_TYPE — replace `clampf(parse_f(val), 0, 1)` with:
```c
fm->fx_type = (float)(int)(parse_f(val) + 0.5f);
if (fm->fx_type < 0.0f) fm->fx_type = 0.0f;
if (fm->fx_type > 4.0f) fm->fx_type = 4.0f;
fx_config(&fm->fx, (int)fm->fx_type, fm->fx_amt);
```

Set param PK_FX_AMT — replace `(int)(fm->fx_type * 4.0f + 0.5f)` with `(int)fm->fx_type`.

Render loop — replace `int fx_mode = (int)(fm->fx_type * 4.0f + 0.5f)` with `int fx_mode = (int)fm->fx_type`.

**T6 regression test added** to `tests/test_fx.c`: verifies all 5 mode pairs produce distinct waveform output (diff_rms > 0.01) at amt=1.0. This mechanically catches any future regression where the broken clamp is reintroduced.

## Tasks Completed

| Task | Description | Commit |
|------|-------------|--------|
| 1 | Fix FX type dispatch in all 10 model files | b274465 |
| 2 | Add T6 dispatch test to test_fx.c + make test GREEN | a7d3786 |

## Verification

- `grep -rn "fx_type * 4.0f" src/models/` returns no matches (PASS)
- `grep -c "(int)fm->fx_type" src/models/fm2.c` returns 3 (PASS)
- `make test` exits 0 with all 11 suites PASSED (PASS)
- `hrd.c` still contains `fx_config(&h->drive_fx, h->drive_mode, h->drive_amt)` unchanged (PASS)

## Cascade Effect (Bug #2 resolved)

With FX_DIODE (mode 0) now dispatching correctly instead of FX_CRUSH, the FM2 ATTACK transient is no longer crushed at default settings. The hard limiter of FX_CRUSH at high amt was clamping the transient click to a stepped waveform, smearing the attack. FX_DIODE is transparent at amt=0 (the dry/wet blend ensures this), so the transient now fires cleanly — Bug #2 resolved as a cascade of the Bug #1 fix.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] gen.c also had the broken fx_type pattern**
- **Found during:** Task 1 verification grep
- **Issue:** Plan listed 9 model files, but gen.c (the 10th model) also had all three broken occurrences of `fx_type * 4.0f + 0.5f`
- **Fix:** Applied the same corrected pattern to gen.c — `(float)(int)(parse_f(val) + 0.5f)` in set_param PK_FX_TYPE, `(int)g->fx_type` in PK_FX_AMT fx_config call and render loop
- **Files modified:** `src/models/gen.c`
- **Commit:** b274465 (included in Task 1 commit)

## Known Stubs

None — all FX modes now dispatch to real DSP paths. No placeholders.

## Self-Check: PASSED
