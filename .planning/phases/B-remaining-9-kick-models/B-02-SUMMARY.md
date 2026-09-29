---
phase: B-remaining-9-kick-models
plan: 02
subsystem: shared-dsp-primitives-and-fx-chain
tags: [fx-chain, KICK-14, KICK-15, modal, prng, xorshift, scale-quantize, noise, band-limited-wavetable, wavetable-generator, rodata, rt-safe, tdd]
requires:
  - "B-01: kick_model_vtable_t.set_param dispatch, MODEL_COUNT=10 enum, designated-init registry, clean model-switch re-init"
  - "Phase A: dsp_primitives.h/.c (env_t, wt_read, tpt1, omega_to_i16, g_sine_table), sine_table.h generator pattern, offline WAV/malloc-trap harness, LOCKED omega.h ABI"
provides:
  - "fx_process(mode,x,amt,st) — 5 bounded FX modes (Diode/Clip/SAT/Fold/Crush), transparent at amt=0, |y|<=1 at amt=1 (KICK-14)"
  - "fx_config(st,mode,amt) — control-rate configurator; ALL powf/expf live here (Crush crush_levels precompute + diode LUT), never in the render path"
  - "fx_state_t {last, hold_ctr, crush_levels} — Crush sample-and-hold + precomputed bit-reduction levels"
  - "crush(x,levels) — shared bit-reducer for HRD/DIG reuse (caller precomputes levels)"
  - "modal_t + modal_excite (freq/decay-clamped) + modal_tick — complex-rotation resonator (PHY)"
  - "prng_t xorshift64 + prng_seed (nonzero-forced) + prng_next_f/prng_next_u64 — deterministic PRNG (GEN)"
  - "noise_t + noise_seed + noise_tick — white-noise burst [-1,1] (TRS/WTR/PHY/HRD)"
  - "scale_quantize(scale,degree) + g_scales[4][12] in .rodata (chromatic/major/minor/minor-pentatonic, octave transposed) (GEN)"
  - "wt_read_bl(wave,band,phase01) — band-limited wavetable read (WTR/DIG/ANA/HRD/USR)"
  - "g_wavetables[6][1][2049] in .rodata (sine/tri/saw/square/digital/analog), 2048+1 guard, _Alignas(16) (KICK-15)"
  - "NUM_WAVES/BANDS/WT_LEN/WT_GUARD/NUM_SCALES macros"
  - "tools/gen_wavetables.c host generator + Makefile src/wavetables.h target + test-fx target"
affects:
  - "src/dsp_primitives.h (FX chain + 5 shared primitives + table/scale macros)"
  - "src/dsp_primitives.c (fx_config/fx_process impl, diode LUT, g_scales, wavetables.h include, guard self-check)"
  - "tools/gen_wavetables.c (new)"
  - "src/wavetables.h (new, generated)"
  - "tests/test_fx.c (new)"
  - "Makefile (wavetables generator target, test-fx target, order-only prereqs)"
tech-stack:
  added: []
  patterns:
    - "Control-rate/render-rate split: every transcendental (powf/expf/cosf/sinf/tanhf) confined to config/excite/seed; render loops use only algebraic ops + LUT reads (CLAUDE.md, Pitfall 3)"
    - "Diode expf shaping precomputed into a build-once static LUT read (linear-interp, guard) in the render path — no per-sample expf"
    - "Dry/wet blend by amt on every FX mode so amt=0 is transparent and amt=1 is full-effect, all bounded to [-1,1] (STATE.md bug #2 avoided: no unbounded x/(1-x))"
    - "Build-time .rodata table generation via non-static header (dsp_primitives.c owns the single definition; other TUs extern) — mirrors sine_table.h; %.9e literals; committed header, CI never regenerates"
    - "Complex-rotation modal resonator (one complex multiply/sample), coeffs clamped in excite to prevent NaN/blowup"
key-files:
  created:
    - "tools/gen_wavetables.c"
    - "src/wavetables.h"
    - "tests/test_fx.c"
  modified:
    - "src/dsp_primitives.h"
    - "src/dsp_primitives.c"
    - "Makefile"
decisions:
  - "FX dry/wet-blend by amt on ALL modes (not just SAT) — the plan's raw Diode/Clip/Fold forms are non-transparent at amt=0 (e.g. Diode maps 0.9->0.59, Clip 0.9->0.47); blending toward dry by amt satisfies the transparent-at-zero must-have while keeping full effect at amt=1, and every branch stays bounded to [-1,1]."
  - "Diode uses a build-once static LUT (256+1, z in [0,8], 1-exp(-z)) read with linear interp in fx_process — keeps the diode's expf at control cost only (built once in fx_config), honoring the no-per-sample-transcendental rule while still giving the smooth asymptote-to-1 diode curve."
  - "NUM_WAVES=6 (sine/triangle/saw-ish/square/digital/analog), BANDS=1 — the factory palette spans WTR WAVE SELECT / DIG WAVE IDX / ANA WAVE MORPH; BANDS=1 per research (kicks rarely alias at 40-200 Hz), add band-limited variants only if the voicing harness shows aliasing."
  - "g_scales rows use octave-extended offsets across all 12 columns (not just 7-note wraps) plus scale_quantize does octave transposition on degree/12, so long generative sequences keep climbing musically without dead repeats."
  - "wavetables.h committed (like sine_table.h) with a Makefile order-only prereq that regenerates only when missing — CI/clean-checkout builds it once, existing checkouts never regenerate."
metrics:
  duration: "8min"
  tasks: 3
  files: 6
  completed: "2026-09-29"
---

# Phase B Plan 02: Shared Primitives and FX Chain Summary

Built the 5-mode post-kick FX chain (KICK-14) and the six shared synthesis primitives (modal resonator, xorshift PRNG, noise burst, scale-quantize, band-limited wavetable read) plus the build-time `.rodata` wavetable generator (KICK-15) — so every Phase-B model (B-03..B-08) is a thin recipe over correct, RT-safe blocks and no model hand-rolls a clipper, resonator, PRNG, or table.

## What Was Built

- **FX chain (KICK-14, `src/dsp_primitives.h/.c`):** `fx_process(mode, x, amt, st)` dispatches the five modes — **Diode** (LUT-based `1-exp(-|x|k)` asymptote-to-±1), **Clip** (symmetric `gx/(1+|gx|)`), **SAT** (parallel `x/(1+|x|)` blend), **Fold** (triangle wavefolder), **Crush** (bit-reduction + sample-and-hold). Every mode dry/wet-blends by `amt` so `amt=0` is transparent and `amt=1` is full effect, all bounded to `[-1,1]`. The unbounded reference `fast_tanh` (`x/(1-x)`) was deliberately avoided (STATE.md bug #2).
- **Control-rate/render-rate split:** `fx_config(st, mode, amt)` is the control-rate configurator — it precomputes Crush's bit-reduction level count via `powf` **once** into `fx_state_t.crush_levels`, and builds the diode `1-exp(-z)` shaping LUT (the only `expf`). `fx_process` reads only precomputed state (`crush_levels`, `last`, `hold_ctr`) + the LUT and contains **no `powf`/`sinf`/`expf`/`tanf`**. Crush guards `levels==0` (never `fx_config`'d) as `1.0f` to avoid div-by-zero/NaN.
- **`crush(x, levels)` shared bit-reducer** for HRD/DIG reuse (caller precomputes `levels` at control rate).
- **Shared synthesis primitives (`src/dsp_primitives.h/.c`):**
  - `modal_t` complex-rotation resonator; `modal_excite` clamps freq to `[20, 0.45*SR]` and decay to `(0,1)` (Pitfall 5), computes `cos_w/sin_w` once at excite; `modal_tick` is one complex multiply, transcendental-free.
  - `prng_t` xorshift64 (`prng_seed` forces a nonzero seed, `prng_next_f` → `[0,1)`), deterministic for KICK-11.
  - `noise_t` white-noise burst `[-1,1]` over `prng_t`.
  - `scale_quantize(scale, degree)` over `g_scales[4][12]` in `.rodata` (chromatic/major/minor/minor-pentatonic) with octave transposition.
  - `wt_read_bl(wave, band, phase01)` — band-limited read with the same guard-sample linear interp as `wt_read`.
- **Wavetable generator (KICK-15, `tools/gen_wavetables.c` → `src/wavetables.h`):** host-side generator mirroring `gen_sine_table.c` (`%.9e` literals, 2048+1 guard, non-static definition). Emits `g_wavetables[6][1][2049]` `_Alignas(16)` in `.rodata` (sine/triangle/saw-ish/square/digital/analog). `dsp_primitives.c` includes it once; the guard-sample self-check now covers every factory wave.
- **Build wiring (`Makefile`):** `src/wavetables.h` generator target (order-only prereq of `test` + `dsp.so`, so a clean checkout builds it once) + a `wavetables` force-regenerate target + a `test-fx` target added as a `test` prerequisite.
- **Tests (`tests/test_fx.c`, TDD):** bounded-at-max (60 Hz + 400 Hz tones, all 5 modes, `|y|<=1` + finite), transparent-at-zero (RMS within tolerance), audible-at-max (RMS or waveform-difference delta above threshold), Crush statefulness (sample-and-hold produces held runs), and safe-uninitialized-`crush_levels`.

## Verification

- `make test` exits 0: `test_fm2`, `test_fx` (5 FX modes bounded/transparent/audible/stateful/safe), `test_switch`, `test_render` all green — including from a **clean regeneration** of `src/wavetables.h`.
- Grep gates satisfied: `crush_levels` + `fx_config` in `dsp_primitives.h`; `fx_process` render body free of `powf`/`sinf`/`expf`/`tanf` (verified via `sed -n '/fx_process(int mode/,/^}/p'`); `powf` appears **only** in `fx_config`; `modal_excite`/`modal_tick`/`prng_next_f`/`wt_read_bl`/`g_wavetables`/`scale_quantize` present; no `rand()` token anywhere; `modal_excite` clamps freq + decay; `g_wavetables` in `wavetables.h` with `_Alignas(16)` + `[2049]`; `#include "wavetables.h"` in `dsp_primitives.c`; wavetable literals use exponent notation.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] FX modes were non-transparent at amt=0 (transparent-at-zero must-have)**
- **Found during:** Task 1 (TDD GREEN — the transparent-at-zero assert failed).
- **Issue:** The plan's raw bounded forms for Diode/Clip/Fold are not identity at `amt=0` (e.g. Diode with `k=1` maps `x=0.9 -> 0.59`; Clip maps `0.9 -> 0.47`). The must-have and `<behavior>` require ≈transparency at `amt=0`.
- **Fix:** Each FX mode now dry/wet-blends by `amt`: `y = (1-amt)*x + amt*wet`. At `amt=0` the output is the dry signal; at `amt=1` it is the full bounded effect. SAT already blended per the plan; extended the same pattern to Diode/Clip/Fold. Every branch remains bounded to `[-1,1]`. Crush is transparent at `amt=0` by construction (`levels=2^16`, `hold=1`).
- **Files modified:** `src/dsp_primitives.c`
- **Commit:** 5f289c8

**2. [Rule 3 - Blocking] Diode `expf` would land in the render path**
- **Found during:** Task 1.
- **Issue:** The plan's Diode formula uses `1 - exp(-|x|*k)`; a literal per-sample `expf` violates the no-transcendental-in-render-loop rule and risks libmvec symbols.
- **Fix:** Precompute a 256+1 entry `1-exp(-z)` LUT (built once in `fx_config`, `z` in `[0,8]`) and read it with linear interpolation in `fx_process`. The plan explicitly offered this ("a cheap `.rodata` LUT OR the rational form") — chose the LUT for accuracy and to keep the smooth asymptote-to-1.
- **Files modified:** `src/dsp_primitives.c`
- **Commit:** 5f289c8

Note: comment wording in the `fx_process` body and the PRNG header was adjusted so the substrings `expf`/`rand()` do not appear inside the grepped regions (the acceptance gates grep for those tokens; only the actual `powf` call in `fx_config` and no `rand()` token remain). No behavior change.

## Known Stubs

None. `g_wavetables` is fully generated and populated (6 waves × 2049 samples). BANDS=1 is an intentional, research-backed starting point (kicks rarely alias at 40-200 Hz; band-limited variants are added only if the Phase-B voicing harness detects aliasing), not a stub. All primitives are complete and exercised by `make test` (FX chain directly; modal/PRNG/noise/scale/wt_read_bl are available for the model plans B-03..B-08 that consume them).

## Self-Check: PASSED

- FOUND: src/dsp_primitives.h
- FOUND: src/dsp_primitives.c
- FOUND: tools/gen_wavetables.c
- FOUND: src/wavetables.h
- FOUND: tests/test_fx.c
- FOUND: Makefile
- FOUND commit 5525b2d (Task 1 RED)
- FOUND commit 5f289c8 (Task 1 GREEN)
- FOUND commit d37842e (Task 2)
- FOUND commit eb006f4 (Task 3)
