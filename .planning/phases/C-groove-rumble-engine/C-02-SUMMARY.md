---
phase: C-groove-rumble-engine
plan: 02
subsystem: groove-rumble
tags: [groove, tempo-clock, multitap, delay-ring, tpt, mono, GRV-01, GRV-02, GRV-03, GRV-05]
requires:
  - "C-01 tempo-drivable mock host + RED test_groove (grv_* keys)"
  - "dsp_primitives.h tpt1_lp / OMEGA_SR / omega_to_i16"
  - "host_api_v1_t get_beat_position / get_bpm (LOCKED ABI)"
provides:
  - "groove_state_t (delay rings + tempo clock + Page-1 params + COLOR LP) on bohm_instance by value"
  - "groove_update_tempo (guarded transport chain) / groove_tick (mask-wrapped 4-tap + COLOR + MONO + VOL) / groove_set_param / groove_init"
  - "kick+groove sum in render_block behind a labeled PHASE D INSERTION POINT"
  - "PK_GRV_* macros + is_groove_key dispatch (model-independent groove keys)"
affects:
  - "src/omega.h (bohm_instance grew; instance-size assert raised to 1.3MB)"
  - "src/dsp.c (render sum + set_param branch + groove_init in create)"
  - "Makefile (src/groove.c linked into every dsp.c-linking test)"
tech-stack:
  added:
    - "src/groove.c / src/groove.h (Phase-C groove rumble voice)"
  patterns:
    - "Beat-delta tempo derivation from get_beat_position (never a live hardcoded 120)"
    - "Power-of-two circular delay ring with branch-free & GRV_DELAY_MASK wrap"
    - "Control-rate transcendentals (powf/tanf) only; transcendental-free per-sample tick"
    - "TPT 1-pole COLOR lowpass (tpt1_lp), never a biquad"
key-files:
  created:
    - "src/groove.h"
    - "src/groove.c"
    - ".planning/phases/C-groove-rumble-engine/C-02-SUMMARY.md"
  modified:
    - "src/omega.h"
    - "src/dsp.c"
    - "Makefile"
decisions:
  - "groove_state_t defined fully in groove.h with TPT COLOR state as bare floats (color_lp_l_s/r_s) so groove.h avoids including dsp_primitives.h — breaks the omega.h<->dsp_primitives.h include cycle while keeping the delay rings by value in the single calloc"
  - "Instance-size _Static_assert raised 800000 -> 1300000; true sizeof(bohm_instance) is 1,237,376 B (two 131072-float rings dominate)"
  - "Groove VOL defaults to 0 (silent opt-in voice) so the kick+groove sum does not color per-model voicing tests; tap/COLOR seeded to musical middles for immediate shaping when opened"
metrics:
  duration: "6min"
  completed: "2026-09-29"
  tasks: 3
  files: 5
---

# Phase C Plan 02: Groove Contracts, Tempo Clock, and Multitap Summary

The non-GEN groove rumble voice: a pre-allocated 131072-frame circular delay ring read back at four 16th-note offsets, a transport-locked tempo clock derived only from the guarded `get_beat_position` -> `get_bpm` -> last-resort-120 chain (killing the reference hardcoded-120 bug), Groove Page-1 controls + a TPT COLOR lowpass + a MONO force-sum, summed with the kick in `render_block` behind a labeled Phase-D insertion point. Turns C-01's RED `test_groove` GREEN for GRV-01/02/03/05.

## What Was Built

- **`src/groove.h`** — `groove_state_t` (two 131072-float delay rings, `write_pos`, the tempo clock `prev_beat/have_prev_beat/bpm_smooth/last_bpm/samples_per_16th`, and the Page-1 params `vol/tap_level[4]/tap_decay[4]/color_g/color_lp_*_s/mono`), the `GRV_DELAY_LEN/MASK` macros, and the four-function groove API. The TPT COLOR lowpass state is stored as bare `float color_lp_l_s/r_s` so this header needs only `<stdbool.h>` and does not include `dsp_primitives.h` — resolving the `omega.h` <-> `dsp_primitives.h` include cycle (omega.h includes groove.h to place the state by value).
- **`src/omega.h`** — `#include "groove.h"` after the host ABI declaration; `groove_state_t groove;` added as the last `bohm_instance` member (by value, inside the single calloc, DC-01/DC-08); the instance-size `_Static_assert` raised from `< 800000` to `< 1300000` (true sizeof is 1,237,376 B); the eight `PK_GRV_*` macros matching test_groove's `grv_*` literals. The LOCKED `host_api_v1_t`/`plugin_api_v2_t` structs and their `+120`/`+56` asserts are untouched.
- **`src/groove.c`** — `groove_update_tempo` (VERBATIM C-RESEARCH Pattern 2: beat-delta primary, `get_bpm` fallback, 120 last-resort, EMA jitter smoothing, control-rate re-lock of `samples_per_16th = (60/bpm)*SR/4`), `groove_init` (seeds a valid interval + musical middles so the first block never divides by zero), `groove_tick` (writes the kick into the ring, reads four `& GRV_DELAY_MASK`-wrapped 16th-note taps, applies `tpt1_lp` COLOR + `0.5*(gl+gr)` MONO + VOL — no per-sample transcendental), and `groove_set_param` (control-rate VOL/LENGTH(`powf`)/COLOR(`tanf`)/TAP1-4/MONO).
- **`src/dsp.c`** — `groove_init` in `omega_create`; `is_groove_key()` routes the eight `grv_*` keys to `groove_set_param` before the model-vtable fallback; `omega_render_block` calls `groove_update_tempo` once per block then `groove_tick` per sample, sums `l[n]+=gl; r[n]+=gr`, and leaves the labeled `PHASE D INSERTION POINT` with no premature clamp (bounded only at the int16 boundary).
- **`Makefile`** — `src/groove.c` added to every test src list that links `src/dsp.c` (TEST/SWITCH/PARAMS/DISTINCT/GEN/GROOVE). `dsp.so`'s `$(wildcard src/*.c)` already covers it.

## Verification

`make test` fully GREEN:
- `test_groove`: GRV-02 BPM sweep (spq 120=5513, 128=5168, 174=3802, distinct), GRV-02 fallback chain (NULL-transport + negative-beat), GRV-01 4-tap delayed energy (late=26707694), GRV-03 Page-1 responsiveness (7/7 keys), GRV-05 MONO force-sum (L==R).
- No regressions: test_fm2, test_fx, test_switch (100 pairs), test_params, test_distinct (45 pairs), test_gen (10/10), test_render all pass.
- `! grep '\* 0.125f'` on groove.c (forbidden hardcoded interval absent); `grep 'get_beat_position'` present; `& GRV_DELAY_MASK` wrap (no `%`); groove_tick body transcendental-free.
- Host ABI asserts (+120/+56) intact; instance-size assert raised; true sizeof = 1,237,376 B < 1,300,000.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Groove VOL defaulted to 0 instead of the planned 0.7**
- **Found during:** Task 3 (wiring the kick+groove sum into every model's output)
- **Issue:** With the groove rumble summed into every model at the plan's `vol=0.7` default, the constant groove energy floor diluted FM2's subtle `trs_tne` param delta below `test_params`' responsiveness threshold (regression: `atk_zcr_delta=0`, `rms_delta=3.5e-5 < 1e-4`). The plan's "musical middles so a bare create is audible" (explicitly at planner discretion) conflicted with the pre-existing locked `test_params` contract.
- **Fix:** `groove_init` now seeds `vol = 0.0f` (silent opt-in performance voice) while keeping `tap_level/tap_decay/color_g` at musical middles, so raising `grv_vol` yields an immediately-shaped rumble. `test_groove`'s `prime_groove` sets `grv_vol=0.8` explicitly on every path, so GRV-01/02/03/05 stay GREEN.
- **Files modified:** src/groove.c
- **Commit:** 1b40e36

### Include-cycle resolution (planner-delegated decision)

The planner left the `groove.h -> dsp_primitives.h -> omega.h` cycle to executor discretion. Chosen path: define `groove_state_t` fully in `groove.h` but store the TPT COLOR lowpass state as bare `float` fields, so `groove.h` includes only `<stdbool.h>` (no `dsp_primitives.h`). `groove.c` wraps those bare floats in local `tpt1_t` views to reuse the exact `tpt1_lp` math. This keeps the delay rings by value in the single calloc with no cycle and no per-instance pointer.

## Known Stubs

None. The groove voice is fully wired for all non-GEN models. GEN's own output-as-rumble routing (DC-05), the Groove Page-2 UI, and the Phase-D duck/DJ-filter/clip chain are intentionally out of C-02 scope (C-03 and Phase D respectively); the labeled `PHASE D INSERTION POINT` marks the seam.

## Self-Check: PASSED
