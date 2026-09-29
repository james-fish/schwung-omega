---
phase: B-remaining-9-kick-models
plan: 06
subsystem: hrd-fm4-distortion-fm
tags: [KICK-06, KICK-03, distortion, bit-crush, fm, 4-operator, opl3, routing-tables, voicing, D-B02, model-registry, fx-chain]
requires:
  - "B-01: kick_model_vtable_t.set_param dispatch, MODEL_COUNT=10 designated-initializer registry with NULL slots, clean model-switch re-init (PK_MODEL memset + re-prime), NULL-slot silence"
  - "B-02: shared crush() bit-reducer, fx_config/fx_process + fx_state_t (SAT=mode2, Fold=mode3, Crush=mode4), wt_read + g_sine_table, wt_read_bl + g_wavetables .rodata, env_t, tpt1_lp"
  - "B-03: fm2.c reference-bar voicing (exp PITCH/LENGTH maps, curve-coupled sweep, 15ms/300ms CURVE dual-env blend), reusable assert_param_responsive battery + pairwise distinctness metric"
  - "B-05: dig.c model .c pattern (FM2 skeleton over B-02 primitives, control-rate crush-level precompute, body-brightness morph for a responsive tone knob)"
provides:
  - "HRD (KICK-06, MODEL_HRD): hard-techno wavetable body (bright factory waves via wt_read_bl) + punchy SAMPLE LAYER blended by MIX + DRIVE (shared fx_process SAT below 0.6, Fold above — bounded, NO bespoke fast_tanh) + CRUSH (shared crush(), v->bits[16,4], levels precomputed at control rate) + COLOR LP + OWN post-kick fx_state separate from the DRIVE fx_state; g_hrd_vtable; state <=4096; the only intentionally distorted/crushed kick"
  - "FM4 (KICK-03, MODEL_FM4): 4-operator FM extending the FM2 core; 4 OPL3-style algorithms encoded as static const g_fm4_algo[4][NUM_OPS] (modulator-source) + g_fm4_carrier[4][NUM_OPS] routing tables WALKED in render (no per-sample algorithm branching); per-op AM + FM-index envelopes; op3 self-FEEDBACK scaled + hard-clamped (<=0.7, no runaway); OP RATIO spread + ALGO detune recomputed at control rate; body f0 + CURVE dual-env sweep reused; g_fm4_vtable; state <=4096"
  - "Both models registered by replacing their NULL registry slots ([MODEL_HRD]/[MODEL_FM4]); array length still == MODEL_COUNT, 3 remaining slots (PHY/USR/GEN) NULL/guarded"
  - "test_params.c HRD (4 P2 + FX + 8 Page-1) + FM4 (6 P2 + FX + 8 Page-1) voicing batteries; test_distinct.c now a live 7-registered / 21-pairs gate; test_switch grows to 49 pairs"
  - "Both models' full trigger re-init (all phases, filter states, per-op/sample envelopes, BOTH HRD fx states + FM4 fx state) so a model switch + trigger is deterministic (KICK-13)"
affects:
  - "src/models/hrd.c (new)"
  - "src/models/fm4.c (new)"
  - "src/models/model_registry.c (HRD + FM4 NULL slots replaced with designated initializers)"
  - "tests/test_params.c (HRD + FM4 assert_param_responsive calls + P2 key lists)"
tech-stack:
  added: []
  patterns:
    - "HRD DRIVE reuses the shared FX SAT/Fold (fx_process) at high amt as its ONLY distortion — no bespoke unbounded distortion. Low/mid DRIVE = FX_SAT (warm grit), DRIVE > 0.6 switches to FX_FOLD (harder industrial edge), chosen at control rate in set_param. This is the STATE.md bug #2 avoidance: the reference fast_tanh x/(1-x) is FORBIDDEN; the shared FX forms self-limit to [-1,1] even at max drive."
    - "HRD uses TWO separate fx_state_t fields: drive_fx (the DRIVE stage) and fx (the post-kick FX TYPE/AMT stage). Separating them means the two Crush sample-and-holds never collide, and each is reset independently on trigger while its precomputed level state is preserved (control rate)."
    - "HRD CRUSH maps v->bits[16,4] (v=0 -> 16 bits clean/off, v=1 -> 4 bits heavily crushed), 2^bits -> levels precomputed ONCE in set_param; render calls the shared crush() which is a bounded roundf only (no per-sample powf). Contrasts DIG's BIT DEPTH (bits[6,14], always-on timbre) — HRD's CRUSH is default-off aggressive."
    - "FM4 encodes the 4 algorithms as static const uint8_t g_fm4_algo[4][NUM_OPS] (modulator-source index per op, FM4_NONE=0xFF sentinel) + g_fm4_carrier[4][NUM_OPS] (which ops sum to output). The render loop indexes the active algo's two rows (snapshotted once per block) and evaluates ops op3->op0 in a SINGLE forward pass so each modulator is computed before its target — NO per-sample branching on algorithm (CLAUDE.md)."
    - "FM4 op3 self-FEEDBACK is clamped at control rate to <=0.7 (v*0.7) so folding op3's last output into its own phase cannot diverge; verified bounded at MAX FEEDBACK + MAX OP INDEX + high pitch by the battery's full-sweep bound check. Carriers are summed with a 1/ncarriers normalization so the output self-limits < 1.0."
    - "FM4 op ratios are recomputed at control rate (fm4_recompute_ratios) from OP RATIO spread + ALGO detune, and ALSO re-derived on trigger if unset (guards a bare zero-init state from leaving all ratios at 0 -> DC)."
key-files:
  created:
    - "src/models/hrd.c"
    - "src/models/fm4.c"
  modified:
    - "src/models/model_registry.c"
    - "tests/test_params.c"
decisions:
  - "HRD DRIVE reuses fx_process SAT/Fold rather than a bespoke distortion, and switches SAT->Fold at v>0.6 for a harder edge; this satisfies both the recipe (DRIVE = SAT/Fold at high amt) and the hard bound (STATE.md bug #2 — no fast_tanh, output stays <=1.0 at max drive)."
  - "HRD CRUSH is default-OFF aggressive (v=0 -> 16 bits) vs DIG BIT DEPTH's always-on timbre (bits[6,14]); this is the intentional HRD-vs-DIG split — HRD is the loud/distorted rave kick, DIG is the lo-fi digital-clean-crunch kick — reinforced by HRD's brighter body waves + drive."
  - "FM4 evaluates operators in a FIXED op3->op0 order so a single forward pass reads each modulator's already-computed output this sample; the routing table only ever names a modulator with a HIGHER index than its target. This keeps the render loop branch-light and lets the 4 algorithms be pure data (g_fm4_algo / g_fm4_carrier) with no per-sample algorithm switch."
  - "The two P2 controls named ALGORITHM and ALGO (research flagged the disambiguation): ALGORITHM (PK_FM4_ALGO) selects the routing table 0..3; ALGO (PK_FM4_ALGO2) is a per-op detune / metallic ratio-spread morph. Both are proven responsive by the battery."
  - "Registration for both models was folded into Tasks 1-2 (each model's .c + its registry designated line + its battery call committed together) because the reusable voicing battery renders a model by selecting it via PK_MODEL, which requires the vtable to be non-NULL. Task 3's remaining work (full suite + cross-build/glibc gate + acceptance greps) is verification-only; no additional source change was needed."
metrics:
  duration: "7min"
  tasks: 3
  files: 4
  completed: "2026-09-29"
---

# Phase B Plan 06: HRD + FM4 Distortion + FM Summary

Implemented the distortion + deep-FM family — **HRD** (KICK-06, the loudest/most aggressive kick: wavetable body + sample layer + bounded DRIVE + CRUSH) and **FM4** (KICK-03, the complex 4-operator FM kick with 4 selectable OPL3-style algorithms) — as thin recipes over the B-02 shared FX/crush primitives and the FM2 FM core, registered both by replacing their NULL registry slots, and extended the D-B02 voicing + distinctness batteries to cover them. `make test` is green with **7 registered models and 21 mutually-distinct pairs** (test_switch 49 pairs); both render loops are transcendental-free, FM4's algorithms are pure static routing tables (grep-verifiable `g_fm4_algo`), HRD's distortion reuses only the bounded FX forms (no `fast_tanh`), and both state structs fit the 4096-byte overlay.

## What Was Built

- **HRD (`src/models/hrd.c`, KICK-06):** a hard-techno kick — a bright band-limited wavetable body (aggressive factory waves via `wt_read_bl`, WAVE selected by `PK_HRD_SAMPLE`) summed with a punchy **SAMPLE LAYER** blended by **MIX** (`PK_HRD_MIX`, ~0.5 default weight), driven through **DRIVE** (`PK_HRD_DRIVE`) which reuses the shared `fx_process` **SAT** (below 0.6) / **Fold** (above 0.6, harder edge) at high amt — the *only* distortion, no bespoke unbounded shaper (STATE.md bug #2) — then bit-reduced by **CRUSH** (`PK_HRD_CRUSH`) via the shared `crush()` (v -> bits[16,4], levels precomputed at control rate, render is a bounded `roundf` only). COLOR output LP + a **separate** post-kick `fx_process` stage with its OWN `fx_state_t` (the `drive_fx` and `fx` states never collide). Page-1 TRS DEC drives the sample-thump length, TRS TNE morphs the body brighter (a real spectral lever, per the B-04/B-05 lesson). `hrd_trigger` re-inits body/sample phases, COLOR filter, both envelopes, and BOTH fx sample-and-holds. `_Static_assert(sizeof(hrd_state) <= 4096)`. 4 P2 slots (`SAMPLE LAYER`, `MIX`, `DRIVE`, `CRUSH`) emitted as full `{key,name,type,min,max}` JSON.
- **FM4 (`src/models/fm4.c`, KICK-03):** a 4-operator FM kick extending the FM2 core. The 4 OPL3-style algorithms are encoded as `static const uint8_t g_fm4_algo[4][NUM_OPS]` (modulator-source index per op, `FM4_NONE` sentinel) + `g_fm4_carrier[4][NUM_OPS]` (which ops sum to output) — the render loop snapshots the active algo's two rows once per block and evaluates operators **op3 -> op0 in a single forward pass** so each modulator is computed before its target, with **NO per-sample branching on algorithm** (CLAUDE.md). ALGO0 = 3->2->1->0 chain; ALGO1 = (3->2)+(1->0) two stacks; ALGO2 = 3->{2,1,0} one modulator + 3 carriers; ALGO3 = (3->0)+1+2 FM pair + 2 additive carriers. Each op has its own AM + FM-index `env_t`; **OP INDEX** (`PK_FM4_OPINDEX`) is the global FM depth; **OP AMP** (`PK_FM4_OPAMP`) tilts the carrier-mix balance; **FEEDBACK** (`PK_FM4_FEEDBACK`) is op3 self-feedback scaled + hard-clamped to <=0.7 so it cannot run away; **OP RATIO** (`PK_FM4_OPRATIO`) spreads the base {1,1,2,3} ratios and **ALGO** (`PK_FM4_ALGO2`) adds a per-op metallic detune (both recomputed at control rate via `fm4_recompute_ratios`). Body f0 + the CURVE 808<->909 dual-env sweep reuse FM2. COLOR LP + post-kick `fx_process`; full trigger re-init (all 4 op phases + envs + feedback state + fx). `_Static_assert(sizeof(fm4_state) <= 4096)`. 6 P2 slots (`ALGORITHM`, `OP RATIO`, `OP INDEX`, `OP AMP`, `FEEDBACK`, `ALGO`).
- **Registration (`src/models/model_registry.c`):** replaced the `[MODEL_HRD]` and `[MODEL_FM4]` NULL slots with `&g_hrd_vtable` / `&g_fm4_vtable` designated initializers (+ their `extern` decls). Array length still `== MODEL_COUNT`; the 3 unimplemented slots (PHY/USR/GEN) stay NULL (guarded). The `_Static_assert(sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT)` is intact.
- **Voicing batteries (`tests/test_params.c`):** added `assert_param_responsive` calls for HRD (`PK_HRD_SAMPLE/MIX/DRIVE/CRUSH` + FX + 8 Page-1) and FM4 (`PK_FM4_ALGO/OPRATIO/OPINDEX/OPAMP/FEEDBACK/ALGO2` + FX + 8 Page-1). `tests/test_distinct.c` (unchanged, loops MODEL_COUNT) is now a live **7 registered / 21 pairs** gate — all distinct. `tests/test_render.c` (unchanged) auto-covers HRD/FM4, writing `tests/output/HRD_kick.wav` + `tests/output/FM4_kick.wav`.

## Verification

- `make test` exits 0: `test_fm2`, `test_fx`, `test_switch` (**49 pairs**), `test_params` (FM2 + WTR + TRS + ANA + DIG + HRD + FM4 batteries), `test_distinct` (**7 registered, 21 pairs**), and `test_render` (full lifecycle + per-model loop) all green.
- Registry grep gates: `[MODEL_HRD] = &g_hrd_vtable` and `[MODEL_FM4] = &g_fm4_vtable` present; `sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT` assert intact; `test_params.c` references `PK_HRD_DRIVE` + `PK_FM4_ALGO`; `g_fm4_algo` static routing table present in `fm4.c` (4 occurrences).
- Both `hrd_render` and `fm4_render` verified free of `sinf/expf/tanf/tanhf/powf/cosf/logf` (0 matches each) — all coeff/transcendental work is in set_param/trigger. HRD's only per-sample nonlinearities are the shared `fx_process` SAT/Fold + the shared `crush()` bounded round; FM4's only per-sample math is `wt_read` table lookups + `floorf` phase wraps.
- `tests/output/HRD_kick.wav` and `tests/output/FM4_kick.wav` written non-empty (262188 bytes each).

## Deviations from Plan

None functional — plan executed as written. One structural note: registration (`model_registry.c` designated lines) and the `test_params.c` battery calls, which the plan lists under Task 3, were committed together with each model's `.c` in Tasks 1-2. This is required because the reusable voicing battery renders a model by selecting it via `PK_MODEL`, which needs the vtable to be non-NULL — so a model cannot be tested without being registered. Task 3 therefore reduced to verification (full suite + acceptance greps + cross-build gate), with no additional source change. All Task 3 acceptance criteria were met.

## Could-not-verify (environment)

- **Cross-build + glibc gate not run locally:** `docker` / the `ghcr.io/charlesvestal/schwung-builder` image are unavailable on this macOS host (confirmed: `docker` absent), so `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` (a Task 3 acceptance criterion) could not be executed here. Same environment limitation recorded for A-04 / B-04 / B-05; **CI is the authoritative cross-build/glibc gate** (per CLAUDE.md and STATE.md [A-03]). Mitigations so the gate should pass: both render loops are verified transcendental-free (no new libm / `_ZGV*` / libmvec risk — HRD reuses the existing shared `crush()` + `fx_process`; FM4 reuses `wt_read` + `env_t`, adding only `floorf`/`fabsf` which are already linked), no new exported symbols (only the two internal vtable structs), and both TUs compile clean natively under `-std=gnu11 -O2`. **This gate remains UNVERIFIED locally and must be confirmed green in CI.**

## Known Stubs

None. HRD and FM4 are fully implemented, registered, FX-active, and pass the automated D-B02 voicing battery + the pairwise distinctness gate (7 registered / 21 pairs). HRD's SAMPLE LAYER is a synthesized punchy wavetable thump (the intended Phase-B approach — real user-sample playback is KICK-10's job, USR/B-08, per B-RESEARCH §HRD "a factory .rodata one-shot or a synthesized layer; USR handles user samples"). The 3 remaining registry slots (PHY/USR/GEN) stay NULL per B-01's intermediate-compilation contract (guarded as silence). Manual on-device ear sign-off for both models is tracked for the B-09 voicing audit (D-B04), as planned.

## Self-Check: PASSED

- FOUND: src/models/hrd.c
- FOUND: src/models/fm4.c
- FOUND: src/models/model_registry.c
- FOUND: tests/test_params.c
- FOUND commit 828f423 (Task 1 — HRD + registration + battery)
- FOUND commit c04866c (Task 2 — FM4 + registration + battery)
- FOUND: tests/output/HRD_kick.wav (non-empty, 262188 bytes)
- FOUND: tests/output/FM4_kick.wav (non-empty, 262188 bytes)
