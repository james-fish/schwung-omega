---
phase: B-remaining-9-kick-models
plan: 03
subsystem: fm2-revoice-and-voicing-harness
tags: [voicing, D-B03, KICK-14, KICK-13, fx-chain, param-responsiveness, distinctness, reusable-harness, control-rate-split]
requires:
  - "B-01: kick_model_vtable_t.set_param dispatch, MODEL_COUNT=10 registry, clean model-switch re-init (PK_MODEL memset + re-prime), NULL-slot silence"
  - "B-02: fx_config(st,mode,amt) control-rate configurator + fx_process(mode,x,amt,st) render stage + fx_state_t (Crush powf/diode expf confined to fx_config)"
  - "Phase A: fm2.c engine, env_t/wt_read/tpt1/omega_to_i16 primitives, mock_host + wav harness, LOCKED omega.h ABI"
provides:
  - "Re-voiced FM2 (D-B03): exp PITCH map [35,120] Hz (~50 Hz default), curve-coupled sweep clamp(f0*(2+curve*4),<=480 Hz), exp LENGTH map [50,1500] ms, narrowed FM INDEX 0-8, tuned CURVE 15ms/300ms output-blend"
  - "FM2 routes final per-sample output through fx_process (KICK-14) with per-mode selection (fx_type*4 -> mode 0..4); Crush configured via fx_config at control rate; render powf/sinf/expf/tanf-free"
  - "tests/test_params.c: reusable assert_param_responsive(api,inst,model_idx,keys,nkeys) voicing battery — non-silent default, each-param-lo-vs-hi-differs, bounded-at-extremes (D-B02 automated), covers FM2"
  - "tests/test_distinct.c: pairwise distinctness metric over registered models (RMS-envelope + spectral ZCR), skips NULL slots, empty-trivial in Wave 2, grows into a real gate as models land"
  - "test_render.c per-model MODEL_COUNT render loop writing tests/output/<name>_kick.wav per registered model"
affects:
  - "src/models/fm2.c (re-voiced ranges + tuned CURVE + fx_state_t field + fx_config/fx_process wiring)"
  - "tests/test_params.c (new — reusable voicing battery)"
  - "tests/test_distinct.c (new — pairwise distinctness)"
  - "tests/test_render.c (per-model default-render loop)"
  - "Makefile (test-params + test-distinct targets, wired into test)"
tech-stack:
  added: []
  patterns:
    - "Control-rate/render-rate split preserved in a real model: FM2's powf (PITCH/LENGTH exp maps) + fx_config Crush powf run only in set_param; render loop is transcendental-free (CLAUDE.md, Pitfall 3)"
    - "Curve-coupled parameter recompute: sweep_hz depends on BOTH PITCH and CURVE, so both set_param handlers recompute it (avoids a stale sweep when either knob moves)"
    - "Reusable model-agnostic voicing battery: assert_param_responsive takes a key list, so B-04..B-08 reuse it by passing their own P2 keys — no per-model harness duplication"
    - "Multi-feature 'measurably changes' metric: RMS-envelope OR spectral-ZCR (whole-buffer + 30ms attack window) so amplitude, brightness, AND transient params all register (plan's RMS-OR-spectral-centroid behavior)"
    - "Grows-as-models-land distinctness gate: loops MODEL_COUNT, skips NULL vtable slots, so the pairwise set is empty in Wave 2 and strengthens automatically as models register"
key-files:
  created:
    - "tests/test_params.c"
    - "tests/test_distinct.c"
  modified:
    - "src/models/fm2.c"
    - "tests/test_render.c"
    - "Makefile"
decisions:
  - "PITCH uses an exponential map (35 * (120/35)^v) instead of the research's linear '35 + v*(120-35)' — the research explicitly preferred exp ('exp map better') for an even musical sweep and to avoid all-the-action-in-last-5% (D-B02); LENGTH likewise uses an exp map over [50,1500] ms."
  - "sweep_hz is curve-coupled: clamp(f0*(2+curve*4), <=480 Hz) is recomputed in BOTH the PITCH and CURVE set_param handlers (not only trigger) so a live CURVE or PITCH twist takes effect immediately without waiting for a re-trigger; the value is deterministic at trigger time regardless."
  - "The param-responsiveness metric was extended from RMS-envelope-only to RMS-OR-spectral (ZCR), measured over the whole buffer AND a 30 ms attack window. TRS TNE (a transient-brightness param) barely moves gross RMS or whole-buffer ZCR because it only shapes the short click; the attack-window spectral feature is what makes it detectable. The plan's <behavior> explicitly permits 'RMS-envelope OR spectral-centroid delta', so this is spec-compliant, not a threshold relaxation — FM2's TRS TNE genuinely changes the output, just spectrally in the attack."
  - "FX sample-and-hold state (fx.last, fx.hold_ctr) is reset in fm2_trigger for a deterministic attack, but the precomputed fx.crush_levels is preserved (recomputing it would require powf, which must stay at control rate per B-02)."
  - "Per-model WAVs use g_models[m]->name (e.g. 'FM2_kick.wav'); on case-insensitive macOS this collides with the legacy lowercase 'fm2_kick.wav' so only one file appears locally — on Linux CI both exist. The WAVs are gitignored build artifacts (tests/output/.gitkeep), not committed."
metrics:
  duration: "6min"
  tasks: 3
  files: 5
  completed: "2026-09-29"
---

# Phase B Plan 03: FM2 Re-voice and Voicing Harness Summary

Re-voiced FM2 to the reference-bar voicing (D-B03) — exponential PITCH/LENGTH maps landing the default in the ~50 Hz techno pocket, a curve-coupled downward sweep, a narrowed FM index, and tuned 808/909 CURVE time constants — wired the KICK-14 FX chain into FM2's render (control-rate `fx_config` for Crush's `powf`, per-sample `fx_process` that stays transcendental-free), and stood up the reusable automated D-B02 voicing battery (param-responsiveness + pairwise distinctness) that every subsequent model plan (B-04..B-08) reuses.

## What Was Built

- **FM2 re-voicing (D-B03, `src/models/fm2.c`):**
  - **PITCH** now maps exponentially over [35,120] Hz (`35 * (120/35)^v`), replacing the linear 30-200 Hz; default v=0.5 sits in the techno pocket and the downward sweep settles the perceived fundamental toward ~50 Hz.
  - **Sweep** decoupled from the fixed `f0*4`: `sweep_hz = clamp(f0*(2 + curve*4), <=480 Hz)`, so the 909 side (high curve) sweeps deeper; recomputed on both PITCH and CURVE changes.
  - **LENGTH** exp map over [50,1500] ms (mid-knob musically centered, ~274 ms at v=0.5).
  - **FM INDEX** narrowed to 0-8 (was 0-12; the tail got buzzy past ~8); default index 4 with its own 40 ms decay so the attack is bright and the tail is clean.
  - **CURVE** keeps the dual-envelope OUTPUT blend (`pitch = p_slow + curve*(p_fast - p_slow)`); the two time constants are the tuned voicing targets — 909 fast 15 ms, 808 slow 300 ms.
  - OP2 WAVE fold-blend distinctness lever and the 0.6/0.4 body/click self-limiting split are retained per the plan.
- **FX chain wired into FM2 (KICK-14):** added `fx_state_t fx;` to `fm2_state` (the `_Static_assert(sizeof(fm2_state) <= 4096)` still holds). `fm2_set_param` calls `fx_config(&fm->fx, (int)(fm->fx_type*4+0.5f), fm->fx_amt)` whenever FX TYPE or FX AMT changes (control rate — the only place Crush's `powf` runs). `fm2_render` applies `s = fx_process(mode, s, fm->fx_amt, &fm->fx)` as the final per-sample stage after COLOR; `mode = fx_type*4` selects Diode/Clip/SAT/Fold/Crush. `fm2_trigger` resets the FX sample-and-hold (`fx.last`, `fx.hold_ctr`) but preserves the precomputed `crush_levels`. The stale "identity passthrough" TODO is removed; the render loop is verified free of `sinf`/`expf`/`tanf`/`tanhf`/`powf`.
- **Reusable voicing battery (`tests/test_params.c`, D-B02 automated):** `assert_param_responsive(api, inst, model_index, keys, nkeys)` drives the real plugin lifecycle and proves, for any model's key list: (1) non-silent default (all 0.5 + trigger, RMS > 1e-4, every int16 in range), (2) each of the 8 Page-1 + N Page-2 params measurably changes the output lo(0.1)-vs-hi(0.9) via a multi-feature metric (RMS-envelope OR spectral ZCR over whole buffer + 30 ms attack window), (3) a full lo->hi sweep of every param stays finite + bounded. Wave 2 drives it for FM2 (5 P2 keys incl. now-active FX TYPE/AMT); the function is model-agnostic for later plans. Also writes `tests/output/<name>_kick.wav`.
- **Pairwise distinctness (`tests/test_distinct.c`, D-B02):** renders every REGISTERED model's default kick, computes an RMS-envelope + spectral-ZCR feature vector, and asserts every registered pair differs by more than a threshold. Loops MODEL_COUNT and skips NULL slots, so it is empty-trivial in Wave 2 (only FM2, 0 pairs) and automatically becomes a real gate as B-04..B-08 register models.
- **Per-model render loop (`tests/test_render.c`):** after the FM2 assertions, loops MODEL_COUNT, skips NULL vtable entries, selects each registered model (integer PK_MODEL index triggers the KICK-13 memset + re-prime), primes defaults, renders, asserts non-silent + int16-bounded, and writes a per-model WAV — forward-compatible with later models.
- **Build wiring (`Makefile`):** `test-params` and `test-distinct` targets (both using the `$(wildcard src/models/*.c)` idiom to pick up future model TUs) added as `test` prerequisites.

## Verification

- `make test` exits 0: `test_fm2`, `test_fx`, `test_switch` (1 pair), `test_params` (FM2 battery), `test_distinct` (1 registered, 0 pairs), and `test_render` (full lifecycle + per-model loop) all green.
- Grep gates satisfied: `fx_process` (3) + `fx_config` (4) + `fx_state_t fx` (1) present in fm2.c; no `identity passthrough` comment remains; `_Static_assert(sizeof(fm2_state) <= 4096)` intact; PITCH region uses `35.0f`, the old `30.0f + v * (200.0f` map is gone; `v * 12.0f` gone (FM index narrowed to `v * 8.0f`); no per-sample `sinf`/`expf`/`tanf`/`tanhf`/`powf` inside `fm2_render` (the only match in the render range is the descriptive comment). `test_distinct.c` references `MODEL_COUNT` + `DISTINCT_THRESHOLD` and guards NULL slots; `Makefile` `test:` depends on `test-distinct`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Param-responsiveness metric needed a spectral + attack-window feature**
- **Found during:** Task 2 (first battery run — the RMS-envelope-only metric failed TRS TNE at delta 1.98e-5).
- **Issue:** TRS TNE only shapes the brightness of the short (~8 ms) click, which is weighted 0.4 in a body-dominated mix. It barely moves gross RMS-envelope, and even whole-buffer zero-crossing rate (spectral proxy) moved only ~6e-5 — the tail dominates the average. An RMS-only "measurably changes" metric would wrongly flag a genuinely-responsive param as dead.
- **Fix:** The plan's `<behavior>` explicitly allows "RMS-envelope OR spectral-centroid delta". Extended the metric to accept a param if EITHER the RMS-envelope delta OR a spectral (ZCR) delta — measured over the whole buffer AND over a 30 ms attack window — exceeds threshold. The attack-window spectral feature is what correctly detects transient-brightness params like TRS TNE. This is spec-compliant metric coverage, not a threshold relaxation; FM2's TRS TNE genuinely changes the output.
- **Files modified:** `tests/test_params.c`
- **Commit:** dc37820

**2. [Rule 1 - Voicing map] PITCH/LENGTH use exponential maps (research preferred, spelled out)**
- **Found during:** Task 1.
- **Issue:** The research gave a linear PITCH form `35 + v*(120-35)` as the primary but noted "(exp map better)" and required LENGTH to "use an exp map so mid-knob is musically centered" (D-B02 anti-pattern: all-the-action-in-last-5%).
- **Fix:** Implemented PITCH as `35 * (120/35)^v` and LENGTH as `50 * (1500/50)^v` — the research's stated preference; `powf` runs only in set_param (control rate), never in render.
- **Files modified:** `src/models/fm2.c`
- **Commit:** 440c6a5

## Known Stubs

None. FM2 is fully re-voiced and FX-active; the voicing battery and distinctness metric are complete and run under `make test`. `test_distinct.c` reporting "0 pairs" in Wave 2 is the designed grows-as-models-land behavior (only FM2 registered), not a stub — it becomes a real gate automatically as B-04..B-08 register their vtables. The 9 non-FM2 registry slots remain NULL per B-01's documented intermediate-compilation contract.

## Self-Check: PASSED

- FOUND: src/models/fm2.c
- FOUND: tests/test_params.c
- FOUND: tests/test_distinct.c
- FOUND: tests/test_render.c
- FOUND: Makefile
- FOUND commit 440c6a5 (Task 1 — FM2 re-voice + FX)
- FOUND commit dc37820 (Task 2 — voicing battery + per-model loop)
- FOUND commit cf2dacf (Task 3 — distinctness metric)
