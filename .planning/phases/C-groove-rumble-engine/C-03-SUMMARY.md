---
phase: C-groove-rumble-engine
plan: 03
subsystem: groove-rumble
tags: [gen, transport-clock, groove-page2, lpf-cascade, tpt, ui-hierarchy, GRV-04]
requires:
  - "C-02 groove_state_t + tempo clock (inst->groove.samples_per_16th) on bohm_instance"
  - "B-08 GEN generative engine (prng_t sequence + Euclidean gate + wt_read_bl body)"
  - "dsp_primitives.h tpt1_lp (TPT 1-pole), OMEGA_SR"
  - "ui.c dynamic Kick Page 2 splice (B-09)"
provides:
  - "Transport-clocked GEN: step interval from inst->groove.samples_per_16th (GEN_STEP_FRAMES demoted to a guarded fallback)"
  - "Runtime SEQ LEN (g->seq_len 1..16) + sub-bass LPF cascade (1 vs 2 tpt1_lp stages = 2/4-pole, DC-06)"
  - "Three PK_GEN_* macros: PK_GEN_SEQLEN / PK_GEN_LPFFREQ / PK_GEN_LPFPOLE"
  - "Conditional Groove Page 2 (groove2) in ui_hierarchy emitted iff model==MODEL_GEN; always-present Groove Page 1 (groove1)"
  - "GEN bypasses the kick-fed multitap (its output IS the rumble, DC-05) while groove_update_tempo still runs to feed it samples_per_16th"
affects:
  - "src/models/gen.c (transport clock + SEQ LEN + LPF cascade + empty Kick Page 2 descriptor)"
  - "src/omega.h (three new PK_GEN_* macros)"
  - "src/dsp.c (multitap gated on model!=MODEL_GEN; tempo clock unconditional)"
  - "src/ui.c (UI_GROOVE1 always + UI_GROOVE2 iff GEN)"
tech-stack:
  patterns:
    - "GEN clocks off the groove tempo clock (samples_per_16th), never a hardcoded live BPM (GEN analogue of the DC-02 bug)"
    - "Cascaded TPT 1-pole low-pass for 2/4-pole toggle (never a biquad, DC-06)"
    - "Leading-comma / no-trailing-comma UI fragment discipline to keep the levels map brace-balanced with conditional levels"
    - "Determinism-under-transport tested with fresh instances so the tempo EMA starts identically"
key-files:
  created:
    - ".planning/phases/C-groove-rumble-engine/C-03-SUMMARY.md"
  modified:
    - "src/models/gen.c"
    - "src/omega.h"
    - "src/dsp.c"
    - "src/ui.c"
    - "tests/test_switch.c"
    - "tests/test_gen.c"
    - "tests/test_groove.c"
decisions:
  - "GEN moved ALL six controls (SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY) to the conditional Groove Page 2; its Kick Page 2 now emits a 0-length interior (FX TYPE/AMT only), resolving C-RESEARCH Open Q2 in favour of REQUIREMENTS' GRV-04 layout"
  - "UI_GROOVE1/UI_GROOVE2 use a LEADING comma + NO trailing comma so the levels map closes cleanly whether or not groove2 is present (brace-balanced with no dangling comma before UI_CLOSE)"
  - "Transport-clock determinism asserted with a FRESH instance per render + a settle phase before trigger, because the groove tempo EMA (bpm_smooth/prev_beat) is instance state that only re-locks samples_per_16th when BPM drifts >0.5 — per-voice-from-reset determinism, matching test_groove's fresh-instance BPM sweep"
metrics:
  duration: "7min"
  completed: "2026-09-29"
  tasks: 3
  files: 7
---

# Phase C Plan 03: GEN Transport Clock and Groove Page 2 Summary

Wired the Phase-B GEN generative engine to the C-02 transport clock and built the conditional Groove Page 2. GEN's self-clock now reads `inst->groove.samples_per_16th` (the `GEN_STEP_FRAMES` ~130-BPM hardcode is demoted to a guarded last-resort fallback — the GEN analogue of the DC-02 bug), gained a runtime SEQ LEN and a sub-bass LPF cascade (1 vs 2 TPT stages = 2/4-pole, DC-06), and its Kick Page 2 collapsed to FX-only so all six GRV-04 controls live on a Groove Page 2 that ui.c emits only when `model==MODEL_GEN`. For GEN the kick-fed multitap is bypassed (its output IS the rumble, DC-05) while the tempo clock still runs to feed it the interval. Completes GRV-04; `make test` fully green.

## What Was Built

- **`src/models/gen.c`** — (1) Transport clock: `gen_render` and `gen_trigger` source the step interval from `inst->groove.samples_per_16th` with `GEN_STEP_FRAMES` as a `< 1` guard fallback only; every `step_ctr` reload uses this transport-derived interval. (2) Runtime SEQ LEN: a new `g->seq_len` (1..16) replaces the fixed `GEN_SEQ_LEN`; the step wrap (`% seq_len`), the Euclidean gate, and the DENSITY->npulses map all use it, and SEQ LEN recomputes npulses proportionally. (3) Sub-bass LPF cascade: new `tpt1_t lpf1, lpf2; float lpf_g; int lpf_pole` — stage 1 always, stage 2 iff `lpf_pole` (2 vs 4-pole, DC-06), reset on trigger, `tanf` cutoff computed at control rate so `gen_render` stays transcendental-free. (4) `gen_p2_slot_desc` now returns 0 (empty, null-terminated) — GEN advertises no Kick Page 2 interior.
- **`src/omega.h`** — three new keys next to the existing GEN macros: `PK_GEN_SEQLEN "gen_seqlen"`, `PK_GEN_LPFFREQ "gen_lpffreq"`, `PK_GEN_LPFPOLE "gen_lpfpole"`. The LOCKED host ABI structs/asserts are untouched.
- **`src/dsp.c`** — `groove_update_tempo` now runs UNCONDITIONALLY (so GEN reads a live `samples_per_16th`); the kick-fed multitap `groove_tick` sum is gated on `inst->model != MODEL_GEN` (GEN's own output already carries the rumble, DC-05). The labeled `PHASE D INSERTION POINT` and the no-premature-clamp boundary are preserved.
- **`src/ui.c`** — two new `.rodata` level fragments following the `UI_KICK1` pattern: `UI_GROOVE1` (8 Page-1 groove params, always appended) and `UI_GROOVE2` (6 GRV-04 controls, appended iff `inst->model == MODEL_GEN`). Both carry a LEADING comma and NO trailing comma so the levels map stays brace-balanced with no dangling comma before `UI_CLOSE` regardless of whether groove2 is present.
- **`tests/test_switch.c`** — GEN expected Kick-Page-2 count `3 -> 0`; `assert_p2_json_valid` guards the 0-interior case (skips interior-content asserts but still runs the full-hierarchy balance/null-terminator check); new `assert_groove2_gating` proves `groove2` is absent for FM2 and present for GEN (with the SEQ LEN / LPF FREQ / LPF POLE keys), `groove1` present for both, and the full `ui_hierarchy` stays brace/bracket-balanced + null-terminated.
- **`tests/test_gen.c`** — `test_gen_transport`: same SEED at a driven 128 BPM renders BYTE-IDENTICAL (determinism survives the transport clock) using fresh instances so the tempo EMA starts identically; SEQ LEN 1 vs 16 produces different buffers (both non-silent).
- **`tests/test_groove.c`** — `test_gen_clocks_to_bpm`: settles the tempo EMA at the target BPM before triggering (so `gen_trigger` latches the fully-locked interval), then asserts 120 vs 174 BPM render DIFFERENT buffers with a min-energy guard on both (so the "differ" assert can't pass with both silent) — proving GEN reads `samples_per_16th`, not the hardcode. `test_gen_lpf_pole`: at a low cutoff, 2-pole vs 4-pole measurably differ and the 4-pole is never louder than the 2-pole.

## Verification

`make test` fully GREEN (all sub-suites):
- `test_switch`: p2 JSON valid for 10 models (GEN now 0-interior), Groove Page 2 gating OK (groove2 iff GEN; groove1 always), 100 switch pairs, hierarchy balanced.
- `test_gen`: determinism (seed-stable/seed-differ/density gate) + transport-clock determinism (byte-identical @128 BPM) + SEQ LEN 1-vs-16 differ + USR load/fallback + 10/10 registry.
- `test_groove`: GRV-02 BPM sweep (5513/5168/3802 distinct) + fallback chain + GRV-01 tap energy + GRV-03 7/7 responsive + GRV-05 MONO L==R + GRV-04 GEN clocks to BPM (120 vs 174 differ, both live) + GRV-04 LPF POLE 2-pole r=0.4708 vs 4-pole r=0.4686.
- No regressions: test_fm2, test_fx, test_params, test_distinct (45 pairs) all pass.
- `cc -std=gnu11 -O2 -Isrc -c src/models/gen.c` compiles; `gen_state` still fits `model_state` (the `_Static_assert(sizeof(gen_state) <= 4096)` holds — added a few ints + two `tpt1_t` floats).
- `GEN_STEP_FRAMES` appears only in the `#define` + two guarded `< 1` fallback assignments + comments — never as a live reload (grep-confirmed). `gen_render` is transcendental-free (the only `powf`/`tanf` match is a comment reference to control-rate `gen_step_pitch`).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Transport-clock determinism required fresh instances, not the plan's reuse-one-instance render**
- **Found during:** Task 3 (extending test_gen for transport-clock determinism)
- **Issue:** The plan sketched rendering the same SEED twice on the SAME instance under a driven transport and asserting byte-identical. That fails: the groove tempo clock's EMA state (`bpm_smooth`, `prev_beat`, `have_prev_beat`) is per-instance and only re-locks `samples_per_16th` when BPM drifts >0.5, so the second driven render converges from a different starting EMA than the first — the buffers diverge legitimately.
- **Fix:** `test_gen_transport` (test_gen.c) creates a FRESH instance per render so the tempo EMA starts identically; likewise `test_gen_clocks_to_bpm` (test_groove.c) settles the EMA at the target BPM (40 warm-up blocks) BEFORE triggering so `gen_trigger` latches the fully-locked interval. This is the correct determinism contract: per-voice-from-reset, matching C-02's existing fresh-instance BPM sweep.
- **Files modified:** tests/test_gen.c, tests/test_groove.c
- **Commit:** 2e68f21

### Planner-delegated decision resolved

C-RESEARCH Open Q2 left it to the planner/executor whether GEN keeps its Phase-B minimal Kick Page 2 (SEED/SCALE/DENSITY) AND adds a Groove Page 2, or MOVES those three to Groove Page 2. Followed the plan's DC-05 directive and REQUIREMENTS GRV-04 layout: all six controls moved to the conditional Groove Page 2; GEN's Kick Page 2 emits a 0-length interior (FX-only). This is why the test_switch GEN slot count changed 3 -> 0.

## Known Stubs

None. GEN is fully wired to the transport clock and the Groove Page 2 controls; the LPF cascade, SEQ LEN, and conditional UI emission are all live and asserted offline. The Phase-D duck / DJ-filter / soft-clip chain remains intentionally out of scope behind the labeled `PHASE D INSERTION POINT` in dsp.c.

## Self-Check: PASSED

- SUMMARY.md: FOUND
- Commit 5df02a9 (Task 1: transport clock + SEQ LEN + LPF + macros): FOUND
- Commit 7fcb2ba (Task 2: GEN bypass + conditional Groove Page 2): FOUND
- Commit 2e68f21 (Task 3: harness extensions): FOUND
