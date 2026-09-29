---
phase: B-remaining-9-kick-models
plan: 07
subsystem: phy-modal-physical
tags: [KICK-05, modal-synthesis, physical-modeling, complex-rotation, resonator, noise-burst, clamped-freq-decay, voicing, D-B02, model-registry, fx-chain]
requires:
  - "B-01: kick_model_vtable_t.set_param dispatch, MODEL_COUNT=10 designated-initializer registry with NULL slots, clean model-switch re-init (PK_MODEL memset + re-prime), NULL-slot silence"
  - "B-02: complex-rotation modal_t + modal_excite (freq clamped [20,0.45*SR], decay clamped (0,1)) + modal_tick (transcendental-free), noise_t + noise_seed + noise_tick, prng_t xorshift64, tpt1_lp, env_t + env_coeff_from_ms, fx_config/fx_process + fx_state_t"
  - "B-03: reusable assert_param_responsive voicing battery + pairwise distinctness metric, fm2.c reference-bar structure (parse_f, clampf, tpt_g_from_hz, dual-env CURVE blend, control-rate/render-rate split)"
  - "B-06: hrd.c model .c pattern (FM2 skeleton over B-02 primitives, control-rate coeff precompute, own fx_state, full trigger re-init)"
provides:
  - "PHY (KICK-05, MODEL_PHY): the ONLY non-oscillator engine — organic/woody/springy modal kick. 3 damped modes via B-02 complex-rotation modal_t (head dominant pitched mode + body1/body2 shell modes) excited by a short bright filtered-noise beater burst (noise_t + tpt1_lp). g_phy_vtable; state <=4096; render loop transcendental-free"
  - "Param map: HEAD TENS -> dominant mode freq (exp 45..110 Hz) + downward-swept excitation (excited at head_f*sweep_mult so the mode settles down, CURVE deepens the drop); SHELL SIZE -> body1/body2 freqs (bigger shell = lower, 110..200 / 180..320 Hz); DAMPING -> shared decay time (exp 400..60 ms, ms->decay_per_sample clamped (0,1)); BEATER -> burst brightness LP cutoff (800..12k Hz) + burst length"
  - "Defense-in-depth NaN prevention (Pitfall 5): modal_decay_from_ms clamps decay into (0,1) and all freqs clamped to [20,0.45*SR] in trigger BEFORE modal_excite (which re-clamps); plus a bounded x/(1+|x|) output self-limit so the summed modal transient never exceeds [-1,1]"
  - "PHY registered by replacing its [MODEL_PHY] NULL slot with &g_phy_vtable; array length still == MODEL_COUNT, remaining PHY/USR->USR/GEN slots NULL/guarded"
  - "test_params.c PHY (4 P2 + FX + 8 Page-1) voicing battery + an EXPLICIT worst-case modal-corner assertion (assert_phy_extremes_no_nan: HEAD TENS=1.0 / DAMPING=0.0 / SHELL=1.0, 2048 frames all isfinite + |x|<=1.0); test_distinct now a live 8-registered / 28-pairs gate; test_switch grows to 64 pairs"
  - "Full trigger re-init (pitch-sweep + amp + burst envelopes, both filter states, noise reseed, fx sample-and-hold) so a model switch + trigger is deterministic (KICK-13)"
affects:
  - "src/models/phy.c (new)"
  - "src/models/model_registry.c ([MODEL_PHY] NULL slot replaced with designated initializer)"
  - "tests/test_params.c (PHY assert_param_responsive call + P2 key list + explicit modal-corner NaN assertion)"
tech-stack:
  added: []
  patterns:
    - "PHY is a thin recipe over the B-02 modal_t complex-rotation resonator: 3 modes = 3 complex multiplies/sample (modal_tick), summed and amp-enveloped. cos_w/sin_w/decay are precomputed in modal_excite at trigger, NEVER per sample (CLAUDE.md — the render loop is verified free of sinf/cosf/expf/tanf/powf)."
    - "Downward head pitch sweep WITHOUT per-sample re-tuning (the modal primitive is fixed-tune once excited): the head mode is excited at head_f * sweep_mult (a swept-UP start freq, CURVE deepens the multiplier for the 909 character) so the fixed mode reads as the settled fundamental while the beater burst supplies the bright attack transient. This trades a literal glissando for a woody attack-then-settle that stays RT-safe."
    - "Defense-in-depth freq/decay clamping (Pitfall 5): every computed mode freq is clampf'd to [20, 0.45*SR] and every decay goes through modal_decay_from_ms which clamps into (0,1) BEFORE modal_excite (which clamps again). Verified by the explicit worst-case corner assertion — the highest head freq + longest decay (closest to 1.0) still renders finite + bounded."
    - "Bounded output self-limit: the summed modal transient can transiently exceed 1.0 at the excitation instant, so a monotone x/(1+|x|) soft-limit is applied when |s|>1 (D-12 — engines self-limit, the int16 clamp is a net not the plan)."
    - "Beater noise is reseeded (noise_seed with a fixed constant) on EVERY trigger: the model-switch memset zeroes exc.rng.s, and xorshift64 of 0 stays 0 (silence) — reseeding guarantees a non-silent, deterministic beater after a switch."
key-files:
  created:
    - "src/models/phy.c"
  modified:
    - "src/models/model_registry.c"
    - "tests/test_params.c"
decisions:
  - "PHY realizes the downward head pitch sweep by exciting the head mode at a swept-UP start frequency (head_f * (1 + 0.8 + 1.2*curve)) rather than per-sample re-tuning, because the complex-rotation modal_t is fixed-tune once excited and re-exciting per block would cost transcendentals. The beater burst carries the bright attack; the settled mode carries the tone. This keeps the render loop transcendental-free while still giving a curve-responsive woody attack."
  - "PHY maps PITCH onto the same head_tens field as HEAD TENS (both drive the dominant mode register) so the Page-1 PITCH knob still moves the fundamental (KICK-12 contract) while HEAD TENS remains the Page-2 fine tension control; the last-written of the two wins, which is acceptable since they are the same musical axis for a modal kick."
  - "Mode amplitudes are weighted head 0.9 / body1 0.5 / body2 0.25 and body decays are shortened (0.5x / 0.3x of head) so the head is the sustained pitched tone and the shell modes are the shorter thud/body — the organic woody character that distinguishes PHY from every oscillator/wavetable model (confirmed distinct: 28/28 pairwise)."
metrics:
  duration: 4min
  tasks: 2
  files: 3
  completed: 2026-09-29
---

# Phase B Plan 07: PHY Modal Physical-Model Kick Summary

**One-liner:** PHY (KICK-05) — the only non-oscillator engine: an organic/woody modal kick built from 3 damped complex-rotation resonators (B-02 `modal_t`, head + 2 shell body modes) excited by a bright filtered-noise beater burst, with defense-in-depth freq/decay clamping proven NaN-free at the worst-case modal corner.

## What Was Built

PHY is the physical-modeling kick model, the single non-oscillator engine in the 10-model set. It sums three exponentially-decaying sinusoids realized via the B-02 complex-rotation `modal_t` primitive (one complex multiply per mode per sample, transcendental-free), excited at trigger by a short bright filtered-noise burst (the "beater").

Param mapping (B-RESEARCH §PHY verbatim):
- **HEAD TENS** -> the dominant pitched mode's frequency (exp 45..110 Hz) plus a downward-swept excitation (excited at a swept-up start freq so the fixed mode settles to the fundamental; CURVE deepens the drop for a 909 character).
- **SHELL SIZE** -> the two lower-Q body modes' frequencies (bigger shell = lower: body1 110..200 Hz, body2 180..320 Hz).
- **DAMPING** -> the shared decay time of all modes (exp 400..60 ms, converted ms->decay_per_sample and clamped into (0,1)).
- **BEATER** -> the excitation character: brightness (noise-burst LP cutoff 800 Hz..12 kHz) and burst length.

The render loop is verified free of per-sample transcendentals; all `cos_w/sin_w/decay` and filter/env coefficients are precomputed in `set_param`/`trigger`. Output routes through COLOR LP then the shared `fx_process` post-kick FX chain (KICK-14), with a bounded `x/(1+|x|)` self-limit so the summed modal transient never exceeds [-1,1].

## Tasks Completed

| Task | Name | Commit | Files |
| ---- | ---- | ------ | ----- |
| 1 | PHY modal engine (head + body modes, beater excitation) | de84f82 | src/models/phy.c |
| 2 | Register PHY (replace NULL slot); extend battery + explicit NaN-at-extremes assert | 3e06d61 | src/models/model_registry.c, tests/test_params.c |

## Verification

- `make test` exits 0 with all suites green:
  - `test_params`: PHY voicing battery passes (non-silent default, all 4 P2 + 8 Page-1 params measurably responsive, bounded across full lo->hi sweep) + the explicit worst-case modal-corner assertion (HEAD TENS=1.0 / DAMPING=0.0 / SHELL=1.0, 2048 frames all `isfinite` + |x|<=1.0).
  - `test_distinct`: **8 registered / 28 pairs** — PHY is distinct from all 7 prior models.
  - `test_switch`: **64 pairs** — clean model-switch re-init holds with PHY in the registry.
  - `test_fm2`, `test_fx`, `test_render`: pass.
- `[MODEL_PHY] = &g_phy_vtable` present in the registry; array length still `== MODEL_COUNT` (static_assert intact); USR/GEN slots remain NULL and guarded.
- `phy_render` confirmed free of `sinf/cosf/expf/tanf/tanhf/powf`.
- `tests/output/PHY_kick.wav` written (262188 bytes).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Beater noise silent after a model-switch memset**
- **Found during:** Task 1 (writing phy.c).
- **Issue:** `noise_t exc` was never seeded. The KICK-13 model-switch path memsets `model_state` to 0, leaving `exc.rng.s == 0`; xorshift64 of 0 is a fixed point (stays 0), so `noise_tick` would return 0 forever and the beater burst would be silent — breaking the non-silent-default and param-responsive criteria after a switch.
- **Fix:** Reseed the noise source with a fixed constant (`noise_seed(&p->exc, ...)`) on every trigger. Keeps the beater deterministic (stable test hashes/asserts) and never silent.
- **Files modified:** src/models/phy.c
- **Commit:** de84f82

## Known Stubs

None. No hardcoded empty/placeholder values; PHY renders a full modal kick from real DSP.

## Deferred / Out-of-Scope

- **Cross-build + glibc gate** (`make dsp.so && ./scripts/glibc_gate.sh build/dsp.so`): no local `aarch64-linux-gnu-gcc` / Docker on the macOS host (confirmed absent). Deferred to CI, which is the authoritative build gate — matching the B-04/B-05/B-06 precedent recorded in STATE.md. The PHY render loop is transcendental-free by inspection, so no new libmvec `_ZGV*` risk is introduced.
- **On-device manual voicing sign-off** for PHY (D-B02 manual / D-B04 audit): the automated criteria are green; the ear round lands in B-09 + the on-device audit doc.

## Self-Check: PASSED

- src/models/phy.c FOUND
- .planning/phases/B-remaining-9-kick-models/B-07-SUMMARY.md FOUND
- tests/output/PHY_kick.wav FOUND
- commit de84f82 FOUND
- commit 3e06d61 FOUND
