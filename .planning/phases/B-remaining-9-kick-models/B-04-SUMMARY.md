---
phase: B-remaining-9-kick-models
plan: 04
subsystem: wtr-trs-wavetable-transient
tags: [KICK-04, KICK-08, wavetable, transient, band-limited, voicing, D-B02, 909, model-registry, fx-chain]
requires:
  - "B-01: kick_model_vtable_t.set_param dispatch, MODEL_COUNT=10 designated-initializer registry with NULL slots, clean model-switch re-init (PK_MODEL memset + re-prime), NULL-slot silence"
  - "B-02: wt_read_bl band-limited read + g_wavetables[6][1][2049] .rodata (sine/tri/saw/square/digital/analog), noise_t + noise_tick, tpt1_lp, fx_config/fx_process + fx_state_t, env_t"
  - "B-03: fm2.c reference-bar voicing (exp PITCH/LENGTH maps, curve-coupled sweep, 15ms/300ms CURVE constants), reusable assert_param_responsive battery + pairwise distinctness metric"
provides:
  - "WTR (KICK-04, MODEL_WTR): band-limited wt_read_bl body pitch-swept via FM2 808<->909 CURVE blend + a dedicated, independently-enveloped noise/click transient (TRANS DECAY length, TRANS COLOR + TRS TNE brightness blends, ATTACK amplitude), COLOR output LP, fx_process final stage; g_wtr_vtable; state <=4096"
  - "TRS (KICK-08, MODEL_TRS): WT COLOR body-timbre morph (sine<->saw wt_read_bl blend) pitch-swept via Page-1 CURVE + its OWN 909-biased PK_TRS_CURVE P2 pitch-sweep-curve, plus an advanced transient morphing a sharp decaying-sine beater CLICK <-> a white NOISE burst (TRANS TONE), TRANS DECAY length, COLOR LP, fx_process; g_trs_vtable; state <=4096"
  - "Both models registered by replacing their NULL registry slots ([MODEL_WTR]/[MODEL_TRS]); array length still == MODEL_COUNT, remaining slots NULL"
  - "test_params.c WTR + TRS voicing batteries (8 Page-1 + 4 P2 + FX keys each); test_distinct.c now a live 3-registered / 3-pairs gate (FM2 != WTR != TRS)"
  - "Both models' full trigger re-init (phases, filter states, transient env, noise reseed, FX sample-and-hold) so a model switch + trigger is deterministic (KICK-13)"
affects:
  - "src/models/wtr.c (new)"
  - "src/models/trs.c (new)"
  - "src/models/model_registry.c (WTR + TRS NULL slots replaced with designated initializers)"
  - "tests/test_params.c (WTR + TRS assert_param_responsive calls + P2 key lists)"
  - "Makefile (TEST_SRCS switched to the src/models/*.c wildcard so registry-named model symbols resolve)"
tech-stack:
  added: []
  patterns:
    - "Wavetable+transient model = FM2 skeleton (Pattern 4) over B-02 primitives: wt_read_bl body + FM2 dual-env CURVE blend + noise_t/click transient + tpt1 tone + fx_process; each state _Static_assert <= 4096"
    - "Transient brightness as a LP<->raw BLEND, not a cascaded LP: cascading two lowpasses on white noise attenuates the click to inaudibility (kills responsiveness + audibility); blending the LP'd click toward the raw noise preserves click amplitude while measurably shifting brightness (more HF energy = more attack-window zero crossings)"
    - "The bright transient LEADS the attack (weighted ~1.1x vs body ~0.5x) while the low body sine ramps from zero, so its brightness governs the attack-window spectrum (detected by the battery's 30ms-window ZCR) yet the transient stays separable from the sub — the KICK-04 selling point and KICK-08 909 clarity"
    - "TRS advanced transient morphs a decaying-sine beater CLICK (pitched tick, g_sine_table) <-> a white NOISE burst via TRANS TONE, and biases the fast 909 pitch env harder via its own PK_TRS_CURVE (distinct from Page-1 PK_CURVE) for attack clarity + thick sub tail"
    - "Registry-named model symbols must be in the test link unit: TEST_SRCS switched from an explicit fm2/registry list to $(wildcard src/models/*.c) so a NULL-slot replacement never leaves an undefined vtable symbol at link (matches the other test targets' idiom)"
key-files:
  created:
    - "src/models/wtr.c"
    - "src/models/trs.c"
  modified:
    - "src/models/model_registry.c"
    - "tests/test_params.c"
    - "Makefile"
decisions:
  - "Transient brightness is a LP<->raw BLEND, not a cascaded lowpass. WTR's first cut cascaded TRANS COLOR then TRS TNE lowpasses on the white-noise click; each pole strips most of the noise power, leaving the transient inaudible and both brightness knobs unresponsive (atk_zcr_delta = 0 exactly — the low body sine owned every zero crossing). Reworked so each brightness control blends the LP'd (dark) click toward the raw (bright) noise: amplitude is preserved, HF (zero-crossing) content moves measurably, and the click audibly leads the attack. Applied the same lesson to TRS from the start (TRANS TONE morphs a bright decaying-sine click <-> raw noise)."
  - "The click is intentionally loud relative to the body in the attack (body ~0.5x, transient ~1.1x, self-limited < 1.0). A ~50 Hz body sine has almost no zero crossings in a 30 ms window, so a quiet transient never flips the summed sign and reads as 'dead' to the battery's spectral metric even though it is audibly present. Leading with the transient makes the separable-transient character (KICK-04) and 909 attack clarity (KICK-08) both real and measurable, and keeps the transient cleanly separable from the sub tail."
  - "WAVE SELECT (WTR) maps v in [0,1] onto the integer index of the 6 factory band-limited waves (sine/tri/saw/square/digital/analog); BODY PITCH is a +/- 1-octave exp fine-tune (powf at control rate). TRS's WT COLOR morphs the body between wave 0 (sine, warm sub) and wave 2 (saw-ish, bright body) via a wt_read_bl crossfade."
  - "PK_TRS_CURVE (TRS P2) is implemented as a SECOND pitch-curve morph layered on the Page-1 CURVE output blend AND a snap on the fast-env time constant (15 ms -> down to ~6 ms as it rises), biased to the fast 909 side per the recipe — distinct from Page-1 PK_CURVE, which is untouched."
  - "The dead trs_set_p2 wrapper (TRS binds .set_p2 = trs_set_param directly, per the plan's vtable spec) was removed after a -Wall -Wextra -Werror strict build flagged it; WTR keeps its wtr_set_p2 wrapper because its vtable references it."
metrics:
  duration: "9min"
  tasks: 3
  files: 5
  completed: "2026-09-29"
---

# Phase B Plan 04: WTR + TRS Wavetable + Transient Summary

Implemented the wavetable-body + dedicated-transient kick family — **WTR** (KICK-04, the clean/precise kick with a fully separable transient) and **TRS** (KICK-08, the 909 attack specialist with a click<->noise transient morph) — as thin recipes over the B-02 shared primitives and the FM2 reference voicing, registered both by replacing their NULL registry slots, and extended the D-B02 voicing + distinctness batteries to cover them. `make test` is green with 3 registered models and 3 mutually-distinct pairs; both models' render loops are transcendental-free and both state structs fit the 4096-byte overlay.

## What Was Built

- **WTR (`src/models/wtr.c`, KICK-04):** a band-limited `wt_read_bl` body oscillator (WAVE SELECT picks among the 6 factory waves; BODY PITCH is a +/- octave exp fine-tune) pitch-swept with the FM2 dual-envelope 808<->909 CURVE blend, summed with a **dedicated, independently-enveloped transient** — a white-noise/click burst with its OWN TRANS DECAY (length), ATTACK (amplitude), and two brightness controls (Page-1 TRS TNE + WTR P2 TRANS COLOR) implemented as LP<->raw blends so the click stays audible. COLOR output LP + `fx_process` final stage. `wtr_trigger` fully re-inits body phase, all filter states, the transient env, reseeds the noise, and resets the FX sample-and-hold. `_Static_assert(sizeof(wtr_state) <= 4096)`. 4 P2 slots (`WAVE SEL`, `BODY PITCH`, `TRANS DEC`, `TRANS COL`) emitted as full `{key,name,type,min,max}` JSON via `wtr_p2_slot_desc`.
- **TRS (`src/models/trs.c`, KICK-08):** like WTR but with an **advanced transient synth** — TRANS TONE morphs the transient from a sharp decaying-sine beater CLICK (a pitched 1.8 kHz tick read from `g_sine_table`) to a white NOISE burst; TRANS DECAY sets its length; WT COLOR morphs the BODY wavetable timbre between sine (warm sub) and saw-ish (bright body) via a `wt_read_bl` crossfade; and **PK_TRS_CURVE** is TRS's own P2 pitch-sweep-curve morph (distinct from Page-1 PK_CURVE) biased to the fast 909 side — it both re-blends the dual-env output toward the fast env and snaps the fast-env time constant from 15 ms down toward ~6 ms as it rises, for the 909 attack clarity + thick sub tail. COLOR LP + `fx_process`; full trigger re-init; `_Static_assert(sizeof(trs_state) <= 4096)`. 4 P2 slots (`TRANS TONE`, `TRANS DEC`, `WT COLOR`, `CURVE`).
- **Registration (`src/models/model_registry.c`):** replaced the `[MODEL_WTR]` and `[MODEL_TRS]` NULL slots with `&g_wtr_vtable` / `&g_trs_vtable` designated initializers (+ their `extern` decls). Array length still `== MODEL_COUNT`; the 7 unimplemented slots stay NULL (guarded). The `_Static_assert(sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT)` is intact.
- **Voicing batteries (`tests/test_params.c`):** added `assert_param_responsive` calls for WTR (`PK_WTR_WAVE/BODYPITCH/TRANSDEC/TRANSCOL` + FX) and TRS (`PK_TRS_TONE/TDEC/WTCOL/CURVE` + FX), each also sweeping the 8 Page-1 keys. `tests/test_distinct.c` (unchanged, loops MODEL_COUNT) is now a live gate: 3 registered / 3 pairs, all distinct. `tests/test_render.c` (unchanged) auto-covers WTR/TRS, writing `tests/output/WTR_kick.wav` + `tests/output/TRS_kick.wav`.
- **Build wiring (`Makefile`):** `TEST_SRCS` (the `test_render` link unit) switched from an explicit `fm2.c + model_registry.c` list to `$(wildcard src/models/*.c)`, matching the other test targets, so replacing a registry NULL slot never leaves an undefined vtable symbol at link.

## Verification

- `make test` exits 0: `test_fm2`, `test_fx`, `test_switch` (9 pairs), `test_params` (FM2 + WTR + TRS batteries), `test_distinct` (**3 registered, 3 pairs**), and `test_render` (full lifecycle + per-model loop, writes WTR/TRS WAVs) all green.
- Registry grep gates: `[MODEL_WTR] = &g_wtr_vtable` and `[MODEL_TRS] = &g_trs_vtable` present (registry lines 23-24); `sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT` assert intact (line 37); `test_params.c` references `PK_WTR_WAVE` + `PK_TRS_TONE`.
- Both `wtr_render` and `trs_render` verified free of `sinf/expf/tanf/tanhf/powf/cosf/log` (all coeff/transcendental work is in set_param/trigger). Both TUs compile clean under `-std=gnu11 -O3 -fPIC -Wall -Wextra -Werror`.
- `tests/output/WTR_kick.wav` and `tests/output/TRS_kick.wav` written non-empty (262188 bytes each).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Transient brightness controls were unresponsive + inaudible (cascaded LP)**
- **Found during:** Task 1 (WTR battery — `trs_tne` failed at `atk_zcr_delta = 0` exactly, then `attack` at `rms_delta = 6.4e-5`).
- **Issue:** WTR's first cut cascaded two lowpasses (TRANS COLOR then TRS TNE) on the white-noise click. Each pole strips most of the noise's HF power, leaving the transient inaudible; the ~50 Hz body sine then owned every zero crossing in the attack window, so the brightness knobs moved zero crossings (and barely any RMS) — a genuinely dead control, not a metric artifact.
- **Fix:** Reworked the transient path so each brightness control BLENDS the LP'd (dark) click toward the raw (bright) noise (amplitude preserved, HF content moves measurably), and made the transient lead the attack (body ~0.5x, transient ~1.1x, self-limited). WTR's transient is now audibly present and both brightness knobs + ATTACK are responsive. Applied the same design to TRS from the start (TRANS TONE morphs a decaying-sine click <-> raw noise), which passed the battery on first run.
- **Files modified:** `src/models/wtr.c`, `src/models/trs.c`
- **Commit:** 415e092 (WTR), 3e925a7 (TRS)

**2. [Rule 3 - Blocking] `test_render` link unit omitted the new model TUs**
- **Found during:** Task 1 (`make test` — `test_render` failed to link: `Undefined symbols "_g_wtr_vtable"`).
- **Issue:** The registry now names `g_wtr_vtable`/`g_trs_vtable`, but `TEST_SRCS` used an explicit `fm2.c + model_registry.c` list (no wildcard), so the vtable symbols were undefined at link. This blocks every future model plan too.
- **Fix:** Switched `TEST_SRCS` to `$(wildcard src/models/*.c)` (the idiom the other five test targets already use), so any registry NULL-slot replacement resolves its symbol automatically.
- **Files modified:** `Makefile`
- **Commit:** 415e092

**3. [Rule 1 - Bug] Dead `trs_set_p2` wrapper**
- **Found during:** Task 3 (strict `-Wall -Wextra -Werror` build verification).
- **Issue:** TRS's vtable binds `.set_p2 = trs_set_param` directly (per the plan's vtable spec), leaving the separate `trs_set_p2` wrapper unused — an error under `-Werror` (`-Wunused-function`).
- **Fix:** Removed `trs_set_p2`. WTR keeps its `wtr_set_p2` wrapper because its vtable references it.
- **Files modified:** `src/models/trs.c`
- **Commit:** 0e84606

### Could-not-verify (environment)

- **Cross-build + glibc gate not run locally:** `docker`/the `ghcr.io/charlesvestal/schwung-builder` image are unavailable on this macOS host (confirmed: `docker: command not found`), so `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` (a Task 3 acceptance criterion) could not be executed here. This is the same environment limitation recorded for A-04; **CI is the authoritative cross-build/glibc gate** (per CLAUDE.md and STATE.md [A-03]). Mitigations applied so the gate should pass: both render loops are verified transcendental-free (no new libm/`_ZGV*`/libmvec risk beyond B-02/B-03), no new exported symbols, and both TUs compile clean under the aarch64 flag subset (`-std=gnu11 -O3 -fPIC -Wall -Wextra -Werror`). **This gate remains UNVERIFIED locally and must be confirmed green in CI.**

## Known Stubs

None. WTR and TRS are fully implemented, registered, FX-active, and pass the automated D-B02 voicing battery + the pairwise distinctness gate (3 registered / 3 pairs). The 7 remaining registry slots stay NULL per B-01's documented intermediate-compilation contract (guarded as silence). Manual on-device ear sign-off for both models is tracked for the B-09 voicing audit (D-B04), as planned.

## Self-Check: PASSED

- FOUND: src/models/wtr.c
- FOUND: src/models/trs.c
- FOUND: src/models/model_registry.c
- FOUND: tests/test_params.c
- FOUND: Makefile
- FOUND commit 415e092 (Task 1 — WTR + registration + Makefile wildcard)
- FOUND commit 3e925a7 (Task 2 — TRS + registration + battery)
- FOUND commit 0e84606 (Task 3 — strict-build cleanup)
- FOUND: tests/output/WTR_kick.wav (non-empty)
- FOUND: tests/output/TRS_kick.wav (non-empty)
