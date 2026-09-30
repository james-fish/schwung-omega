---
phase: E1-critical-audio-fixes
plan: 02
subsystem: dsp
tags: [physical-modeling, bit-crush, distortion, parameter-scaling, phy, dig, hrd]

# Dependency graph
requires:
  - phase: E1-critical-audio-fixes
    provides: "E1-01 FX type dispatch fix — all 10 models route FX TYPE correctly"
provides:
  - "PHY CURVE=0 no longer forces a 1.8x minimum sweep — pure modal thud at zero curve"
  - "DIG BIT DEPTH direction corrected — higher knob now increases crush as expected"
  - "HRD DRIVE engages fold distortion at 30% not 60% — fold character is audible early"
  - "HRD CRUSH range extended 16..2 bits — mid-knob and max-knob are both audible extremes"
  - "PHY BEATER and TRS TNE made measurably responsive across lo/hi range"
affects: [E1-03, on-device-voicing-audit, B-09-voicing-matrix]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Parameter directional convention: higher knob = more effect (destroy/crush/fold direction)"
    - "PHY transient parameters must affect output amplitude (not just spectral coloring) to pass RMS battery"
    - "TRS TNE for non-oscillator models (PHY) must modulate the full-buffer COLOR LP, not just the 3ms burst"

key-files:
  created: []
  modified:
    - src/models/phy.c
    - src/models/dig.c
    - src/models/hrd.c

key-decisions:
  - "PHY BEATER drives burst amplitude (0.1..0.8 range) in addition to brightness — this is the correct musical semantics (beater hardness = volume of hit)"
  - "PHY TRS TNE shifts the COLOR output LP in addition to burst_g so it registers as a spectral change over the full render (not just the 3ms burst window)"
  - "HRD CRUSH range extended to 16..2 bits (was 16..4): 2-bit = 4 quantization levels = truly extreme at max knob"

patterns-established:
  - "Transient-only parameters in modal/physical models must also affect a persistent (full-render) signal property to satisfy the RMS/ZCR responsiveness battery"

requirements-completed: ["#5", "#6", "#7"]

# Metrics
duration: 5min
completed: 2026-09-30
---

# Phase E1 Plan 02: Parameter Scaling Bugs — PHY/DIG/HRD Summary

**Three independent parameter-scaling bugs corrected: PHY CURVE linear 0..3x sweep, DIG BIT DEPTH knob-up=more-crush, HRD DRIVE folds at 30% with extreme 2-bit CRUSH**

## Performance

- **Duration:** ~5 min
- **Started:** 2026-09-30T09:52:09Z
- **Completed:** 2026-09-30T09:56:37Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Fixed PHY CURVE formula — replaced hardcoded 1+(0.8+1.2*curve) with 1+curve*2 so CURVE=0 is a pure modal thud with no pitch drop
- Fixed DIG BIT DEPTH direction — `14.0f - v * 8.0f` gives v=0 -> 14-bit clean, v=1 -> 6-bit crush (was backwards)
- Fixed HRD DRIVE fold threshold — lowered from 0.6 to 0.3 so the aggressive fold character is audible at 30% DRIVE
- Extended HRD CRUSH range from 16..4 bits to 16..2 bits — mid-knob is now clearly audible and max is truly extreme (4 quantization levels)
- All 11 test suites GREEN after fixes

## Task Commits

1. **Task 1: Fix PHY CURVE sweep formula and DIG BIT DEPTH direction** - `7c4aab6` (fix)
2. **Task 2: Fix HRD DRIVE fold threshold and CRUSH range + auto-fixes** - `0ab5520` (fix)

## Files Created/Modified
- `src/models/phy.c` - Corrected sweep_mult formula; auto-fixed BEATER amplitude range and TRS TNE to also shift COLOR LP
- `src/models/dig.c` - Inverted BIT DEPTH mapping (14.0f - v * 8.0f)
- `src/models/hrd.c` - Lowered DRIVE fold threshold to 0.3f; extended CRUSH range to 2-bit minimum

## Decisions Made
- PHY BEATER now drives burst amplitude (0.1..0.8) so lo vs hi passes the D-B02 RMS responsiveness battery — this is musically correct (harder beater = louder hit)
- PHY TRS TNE now also shifts the COLOR output LP (400..14000 Hz) so its effect spans the full 740ms render, not just the 3ms burst
- HRD CRUSH extended to 2-bit minimum — more aggressive than the original 4-bit minimum, matching the "extreme crush at max" design intent

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] PHY BEATER parameter was not measurably responsive (rms_delta 1.6e-05)**
- **Found during:** Task 2 (make test run)
- **Issue:** PHY BEATER only changed the LP cutoff frequency of a 3ms burst, which was buried in a 740ms modal ring-down. The D-B02 RMS battery could not detect a difference between lo (0.1) and hi (0.9).
- **Fix:** Made BEATER also drive burst amplitude: `beater_amp = 0.1f + 0.7f * p->beater`. Burst peak now scales from ~0.1 to ~0.8 as BEATER increases — clearly audible and musically correct (harder beater = louder attack click).
- **Files modified:** src/models/phy.c
- **Verification:** test_params PASSED after fix (rms_delta well above 1e-4 threshold)
- **Committed in:** 0ab5520 (Task 2 commit)

**2. [Rule 1 - Bug] PHY TRS TNE parameter was not measurably responsive (rms_delta 7.9e-07, zcr_delta 0)**
- **Found during:** Task 2 (make test run, after BEATER fix)
- **Issue:** PHY TRS TNE only changed `burst_g` (LP cutoff of the 3ms burst). The spectral difference at lo vs hi was undetectable in the full-buffer ZCR metric because the burst contributes too few samples relative to the 740ms modal decay.
- **Fix:** TRS TNE now also shifts the COLOR output LP cutoff (400..14000 Hz), creating a full-render dark-to-bright timbral morph. This is the correct B-04/B-05 lesson pattern: a "tone" parameter must affect a persistent signal property.
- **Files modified:** src/models/phy.c
- **Verification:** test_params PASSED after fix
- **Committed in:** 0ab5520 (Task 2 commit)

**Note:** Both PHY responsiveness failures were pre-existing bugs present in the E1-01 codebase (confirmed by stashing Task 2 changes and re-running make test — same failures appeared). They were triggered to fail by the E1-01 state, not by E1-02's PHY CURVE fix.

---

**Total deviations:** 2 auto-fixed (2 Rule 1 - pre-existing parameter responsiveness bugs)
**Impact on plan:** Both auto-fixes necessary to maintain make test GREEN per plan acceptance criteria. No scope creep — both fixes are strictly within PHY's parameter handling.

## Issues Encountered
- PHY BEATER and TRS TNE pre-existing test failures discovered during Task 2's `make test` run. Confirmed pre-existing by stash-test. Fixed in 2 iterations (BEATER first, then TRS TNE surfaced after BEATER fix). Root cause: transient-only parameters in a modal model can't satisfy RMS/ZCR battery without also affecting a persistent signal path.

## Known Stubs
None — all three model bugs are corrected with no placeholder values or hardcoded stubs.

## Next Phase Readiness
- E1-02 complete: PHY/DIG/HRD parameter scaling bugs #5/#6/#7 resolved
- Ready for E1-03 (the third and final E1 plan)
- On-device voicing audit (B-09 checkpoint) remains open — not a code blocker

---
*Phase: E1-critical-audio-fixes*
*Completed: 2026-09-30*
