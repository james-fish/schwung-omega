---
phase: C-groove-rumble-engine
plan: 01
subsystem: test-infrastructure
tags: [groove, tempo, mock-host, tdd, test-harness]
requires: []
provides:
  - "Tempo-drivable mock host (settable beat/BPM + advance helper)"
  - "NULL-transport mock host variant (both callbacks NULL)"
  - "RED test_groove.c harness encoding GRV-01/02/03/05 offline assertions"
  - "test-groove Makefile target folded into make test"
affects:
  - tests/mock_host.c
  - tests/mock_host.h
  - tests/test_groove.c
  - Makefile
tech-stack:
  added: []
  patterns:
    - "Drivable module-static transport state (g_mock_beat/g_mock_bpm) + setters, reset on make_mock_host"
    - "BPM-sweep harness: advance mock beat per block by dbeat = (bpm/60)*(frames/sr)"
    - "Groove keys referenced as grv_* string literals mirroring C-02 PK_GRV_* so the RED harness compiles independently"
key-files:
  created:
    - tests/test_groove.c
  modified:
    - tests/mock_host.c
    - tests/mock_host.h
    - Makefile
decisions:
  - "Mock transport is a settable module-static double/float (g_mock_beat/g_mock_bpm) driven off the audio thread; make_mock_host resets it to (0, 120) so every test starts from a known state"
  - "test_groove references groove params as grv_* string literals (not PK_GRV_* macros, which land in C-02) so the harness compiles + links in C-01; unknown keys are ignored by set_param today, so assertions bite only once C-02 makes the keys live"
  - "GRV-05 load-bearing check is MONO-on L==R equality (holds regardless of base-kick symmetry); the off-case L!=R depends on C-02 producing channel-asymmetric taps and is documented, not asserted, in C-01"
metrics:
  duration: 4min
  completed: 2026-09-29
---

# Phase C Plan 01: Tempo-Drivable Mock Host and test_groove Summary

Made the offline mock host tempo-drivable (settable/advanceable beat + BPM stub + a both-callbacks-NULL variant) and landed the RED `test_groove.c` harness wired into `make test`, retiring the phase's value-at-risk (GRV-02 live tempo derivation) by making it testable offline before the groove engine exists.

## What Was Built

### Task 1 — Tempo-drivable mock host (commit 8b38227)
`tests/mock_host.c` / `tests/mock_host.h` extended:
- `mock_beat()` now reads a module-static `g_mock_beat`; `mock_get_bpm()` reads `g_mock_bpm`.
- Public drivers `mock_host_set_beat` / `mock_host_advance_beat` / `mock_host_set_bpm`.
- `make_mock_host()` wires `h.get_bpm = mock_get_bpm` (was NULL) and RESETS the transport to `(beat=0, bpm=120)` on every call.
- New `make_mock_host_null_transport()` returns a host with BOTH `get_beat_position` and `get_bpm` NULL, exercising the groove clock's last-resort 120-constant path (C-RESEARCH Pitfall 2).
- `src/omega.h` ABI untouched (`git diff --quiet src/omega.h` clean).

### Task 2 — RED test_groove harness + Makefile wiring (commit 981f0c0)
`tests/test_groove.c` drives the real plugin (`move_plugin_init_v2` → `create_instance` → `set_param` → `on_midi` → `render_block`) through the drivable mock and encodes:
- **GRV-02** parametric BPM sweep {120, 128, 174}: asserts `samples_per_16th = (60/bpm)*sr/4` yields distinct intervals (5513 / 5168 / 3802) and that each BPM, driven via `mock_host_advance_beat` per block, renders finite/bounded/non-silent. Plus the fallback chain: NULL-transport variant (120 constant) and negative-beat (get_bpm fallback), both finite/bounded/non-silent.
- **GRV-01** 4-tap delayed-energy presence (late-window energy after the attack).
- **GRV-03** per-key Page-1 responsiveness for grv_vol/length/color/tap1-4 (RMS lo-vs-hi delta).
- **GRV-05** MONO force-sum: L == R sample-for-sample under `grv_mono=1` with asymmetric taps.
- `main()` guards `g_models[MODEL_FM2]` registered + non-NULL render so the eventual GREEN assertions are non-trivial (groove fed by a live kick, not silence-in/silence-out).

Makefile: added `GROOVE_TEST_SRCS`, the `test-groove` target (mirrors `test-gen`), `test-groove` on `.PHONY`, and as a prerequisite of the aggregate `test:` target.

## Verification

- `make test-switch` exits 0 — existing suite unaffected by the mock_host extension (verified after both tasks).
- `git diff --quiet src/omega.h` — no host ABI drift.
- `tests/test_groove.c` compiles cleanly (`-Wall -Wextra`; only a pre-existing unrelated `rd_u32le` unused-function warning in dsp.c, out of scope).
- `make test-groove` fails RED now (exit 134): GRV-02 sweep + fallback chain PASS (harness math + finite/non-silent render), GRV-01 late-energy PASSes on the FM2 tail, and GRV-03 responsiveness FAILS because the `grv_*` keys are inert until C-02 — the intended enabling state. Goes GREEN when C-02 lands the groove engine.

## Deviations from Plan

None - plan executed exactly as written.

The one checker note (bake a minimum-energy guard so GREEN assertions aren't trivially true) is honored: `main()` asserts FM2 is registered before running, and every render path asserts non-silence (`energy > 1000.0`) so the groove voice must actually be fed by a live kick rather than passing on silence.

## Known Stubs

None. `test_groove.c` is a real harness (not a stub): it drives `render_block` and the DC-02 BPM math end-to-end. It is intentionally RED (the groove engine it tests lands in C-02); the `grv_*` keys are documented as mirroring C-02's PK_GRV_* macros. This is the planned TDD RED state, not a stub.

## For C-02 (next plan)

- Define `PK_GRV_VOL`/`PK_GRV_LENGTH`/`PK_GRV_COLOR`/`PK_GRV_TAP1..4`/`PK_GRV_MONO` in omega.h matching the `grv_*` strings this harness uses.
- Implement the groove voice (circular delay, tempo clock per C-RESEARCH Pattern 2, Page-1 controls, MONO sum) so `make test-groove` goes GREEN.
- The tempo clock must derive BPM the way the harness drives it: `bpm = dbeat*60*sr/frames` from `get_beat_position` deltas, with the guarded fallback chain (get_bpm → 120 constant) the NULL-transport and negative-beat cases exercise.

## Self-Check: PASSED

All created/modified files present on disk; both task commits (8b38227, 981f0c0) found in git log.
