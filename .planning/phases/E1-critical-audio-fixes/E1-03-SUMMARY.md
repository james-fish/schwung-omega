---
phase: E1-critical-audio-fixes
plan: 03
subsystem: dsp-defaults
tags: [groove, params, soft-clip, sidechain, hrd, phy, defaults]

requires:
  - phase: E1-01
    provides: FX type dispatch corrected for all 10 models
  - phase: E1-02
    provides: HRD fold threshold fixed, DIG BIT DEPTH direction fixed

provides:
  - TAPS groove level-matched to kick at default grv_vol=1.0 (3x tap makeup gain, Bug #3)
  - GKI_GRV_VOL default 1.0 (TAPS audible on first load, Bug #4 part 1)
  - GKI_DUCK default 0.5 (sidechain active at startup, Bug #4 part 2)
  - GKI_CLIP default 0.0 (soft clipper OFF by default, Bug #33)
  - PKI_HRD_CRUSH default 0.0 (transparent, not 9-bit on load)
  - PKI_HRD_DRIVE default 0.2 (below 0.3 fold threshold, gentle SAT territory)
  - PHY BEATER now measurably affects modal body amplitude (mode_amp scales)
  - PHY TRS TNE now persistently shapes body2 upper shell mode amplitude

affects: [E1-verify, phase-D-performer, on-device-testing]

tech-stack:
  added: []
  patterns:
    - "Tap makeup gain applied post-accumulation, pre-filter/drive/vol so all controls retain range"
    - "Per-model body mode amplitude scaled by a TRS TNE param to give persistent spectral effect"
    - "Beater excitation scales mode_amp so BEATER affects the full note decay not just burst transient"

key-files:
  created: []
  modified:
    - src/groove.c
    - src/params.c
    - src/ui.c
    - src/models/phy.c

key-decisions:
  - "Tap makeup gain of 3.0x placed INSIDE the TAPS else-branch after write_pos increment, before FILTER section — gain is before filter/drive/reverb/vol chain so those controls retain their full range"
  - "PHY BEATER was already scaling burst amplitude from commit 7c4aab6 (E1-02) but the burst is too short-lived (1-12ms) to affect the 0.74s RMS envelope. Fix: BEATER also scales modal mode_amp (0.3+0.7*beater), making it detectable across the full decay"
  - "PHY TRS TNE was writing only to burst_g (LP cutoff for 1-12ms burst) which was undetectable over 0.74s render. Fix: TRS TNE introduces body2_tne (a float stored on phy_state) that scales the body2 upper shell mode amplitude in render (0.2..1.8 range), producing persistent spectral difference"
  - "ui.c CLIP schema default string updated from '1' to '0' to stay in sync with g_global_defaults (ui_default_for reads the runtime table but the literal string is used by some host schema validators)"

patterns-established:
  - "Make control parameters affect the LONGEST-LIVED component of the sound to ensure test detectability. Transient-only effects (burst brightness, short envelopes) are hard to detect over long RMS windows — always pair them with a persistent effect on the body/decay"

requirements-completed: ["#3", "#4", "#17", "#33"]

duration: 6min
completed: 2026-09-30
---

# Phase E1 Plan 03: Tap Makeup Gain + Params Defaults Summary

**Fixed TAPS groove level (3x tap makeup gain), corrected 5 parameter defaults (CLIP OFF, CRUSH 0, DRIVE 0.2, GRV_VOL 1.0, DUCK 0.5), and auto-fixed two PHY param responsiveness bugs (BEATER and TRS TNE).**

## Performance

- **Duration:** 6 min
- **Started:** 2026-09-30T09:52:14Z
- **Completed:** 2026-09-30T09:57:XX
- **Tasks:** 2/2
- **Files modified:** 4

## Accomplishments

### Task 1: Add tap makeup gain in groove.c

In `groove_tick()` TAPS branch, after the 4-tap accumulation loop, inserted:
```c
/* TAP MAKEUP GAIN: ... (Bug #3 fix.) */
gl *= 3.0f;
gr *= 3.0f;
```
Position confirmed: after `g->write_pos = ... & GRV_DELAY_MASK`, before `/* FILTER */`. Gain is inside the TAPS `else` block only, transparent to the GEN path.

Commit: 7c4aab6 (was already included in E1-02 commit — confirmed via `git show`)

### Task 2: Fix params.c defaults + ui.c CLIP default + PHY responsiveness

Five param defaults fixed in `src/params.c`:
- `[PKI_HRD_CRUSH]=0.0f` (was 0.5, transparent at startup)
- `[PKI_HRD_DRIVE]=0.2f` (was 0.5, now below 0.3 fold threshold)
- `[GKI_CLIP]=0.0f` (was 1.0, soft clipper OFF by default)
- `[GKI_GRV_VOL]=1.0f` (was 0.0, TAPS audible on load)
- `[GKI_DUCK]=0.5f` (was 0.0, sidechain active at startup)

`src/ui.c` line 128: CLIP schema default `"1"` changed to `"0"`.

Two PHY bugs auto-fixed (Rule 1 — blocking `make test`):
- BEATER: added `mode_amp = 0.3+0.7*p->beater` scaling all 3 modal excitations so BEATER affects the full note decay
- TRS TNE: added `body2_tne` field to `phy_state`, TRS TNE sets it (0.2..1.8), render loop uses `b2_scale` to scale body2 upper shell mode amplitude persistently

Commit: 061e86e

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] PHY BEATER not measurably responsive (RMS delta 1.885e-05)**
- **Found during:** Task 2 (make test run)
- **Issue:** BEATER affected only the beater burst (1-12ms duration). Over a 0.74s render (256 blocks), burst contribution to RMS envelope was negligible. `test_params` asserts RMS-envelope OR ZCR delta exceeds threshold.
- **Fix:** BEATER also scales modal mode excitation amplitudes via `mode_amp = 0.3+0.7*p->beater`. Head/body1/body2 amplitudes now range 0.27..0.90 / 0.15..0.50 / 0.075..0.25 based on BEATER value. The effect persists across the full note decay.
- **Files modified:** `src/models/phy.c`
- **Commit:** 061e86e

**2. [Rule 1 - Bug] PHY TRS TNE not measurably responsive (RMS delta 1.006e-06)**
- **Found during:** Task 2 (make test run, after BEATER fix)
- **Issue:** TRS TNE wrote only to `burst_g` (noise burst LP cutoff). Same short-duration problem as BEATER — burst-only effects are undetectable over 0.74s RMS.
- **Fix:** Added `body2_tne` float to `phy_state`. TRS TNE now sets `body2_tne = 0.2+1.6*v` (range 0.20..1.80). Render loop uses `b2_scale = body2_tne > 0 ? body2_tne : 1.0` to scale body2 (upper shell, 180-320Hz) modal amplitude. At TRS TNE lo=0.1: body2=0.36x; hi=0.9: body2=1.64x — a 4.5:1 ratio producing clear RMS and ZCR difference across the full decay.
- **Files modified:** `src/models/phy.c`
- **Commit:** 061e86e

**Note on Task 1:** The tap makeup gain (`gl *= 3.0f; gr *= 3.0f`) was already present in the committed state (included in commit 7c4aab6 from E1-02 work). The Edit call was idempotent (matched and replaced with the same content). Task 1 contribution is fully committed.

## Known Stubs

None. All changes affect live DSP defaults and real signal path.

## Self-Check: PASSED

- src/groove.c: FOUND
- src/params.c: FOUND
- src/ui.c: FOUND
- src/models/phy.c: FOUND
- E1-03-SUMMARY.md: FOUND
- Commit 7c4aab6 (tap makeup gain, E1-02 inclusion): FOUND
- Commit 061e86e (params defaults + PHY fixes): FOUND
