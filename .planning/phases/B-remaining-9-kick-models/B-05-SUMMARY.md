---
phase: B-remaining-9-kick-models
plan: 05
subsystem: ana-dig-analog-digital
tags: [KICK-09, KICK-07, wavetable, sub-oscillator, bit-crush, morph, voicing, D-B02, 808, digital, model-registry, fx-chain]
requires:
  - "B-01: kick_model_vtable_t.set_param dispatch, MODEL_COUNT=10 designated-initializer registry with NULL slots, clean model-switch re-init (PK_MODEL memset + re-prime), NULL-slot silence"
  - "B-02: wt_read_bl band-limited read + g_wavetables[6][1][2049] .rodata (sine/tri/saw/square/digital/analog), shared crush() bit-reducer, wt_read + g_sine_table (sub-osc), tpt1_lp, fx_config/fx_process + fx_state_t, env_t"
  - "B-03: fm2.c reference-bar voicing (exp PITCH/LENGTH maps, curve-coupled sweep, 15ms/300ms CURVE constants), reusable assert_param_responsive battery + pairwise distinctness metric"
  - "B-04: wtr.c/trs.c model .c pattern (FM2 skeleton over B-02 primitives), Makefile TEST_SRCS src/models/*.c wildcard, LP<->raw brightness-blend lesson"
provides:
  - "ANA (KICK-09, MODEL_ANA): 2-table analog WAVE MORPH body (sine<->analog wt_read_bl crossfade) pitch-swept via FM2 808<->909 CURVE blend, PLUS a dedicated, independently-enveloped SUB-OSCILLATOR (pure g_sine_table sine at f0 with SUB LEVEL + long SUB DECAY 100..900 ms) = the 808 sub-boom lever, PLUS a synthesized SAMPLE attack-thump layer; COLOR LP; fx_process final stage; g_ana_vtable; state <=4096; PITCH mapped lower [30,110] Hz (~45 Hz default) for sub territory"
  - "DIG (KICK-07, MODEL_DIG): digital-character wt_read_bl body (WAVE IDX over saw/square/digital factory waves) pitch-swept via CURVE blend, BIT DEPTH applying the shared crush() bit-reducer as TIMBRE (bits 6..14, levels precomputed at control rate), PITCH ENV dedicated sweep-depth scalar, SAMPLE LAYER attack thump; crisper/brighter than ANA; COLOR LP; fx_process; g_dig_vtable; state <=4096"
  - "Both models registered by replacing their NULL registry slots ([MODEL_ANA]/[MODEL_DIG]); array length still == MODEL_COUNT, 5 remaining slots NULL/guarded"
  - "test_params.c ANA + DIG voicing batteries (8 Page-1 + 4 P2 + FX keys each); test_distinct.c now a live 5-registered / 10-pairs gate (FM2 != WTR != TRS != ANA != DIG); test_switch grows to 25 pairs"
  - "Both models' full trigger re-init (all phases, filter states, sub/samp envelopes, FX sample-and-hold) so a model switch + trigger is deterministic (KICK-13)"
affects:
  - "src/models/ana.c (new)"
  - "src/models/dig.c (new)"
  - "src/models/model_registry.c (ANA + DIG NULL slots replaced with designated initializers)"
  - "tests/test_params.c (ANA + DIG assert_param_responsive calls + P2 key lists)"
tech-stack:
  added: []
  patterns:
    - "ANA's distinctness lever = a DEDICATED sub-oscillator: a pure g_sine_table sine at the un-swept f0 with its OWN long-decay env (SUB LEVEL + SUB DECAY, exp 100..900 ms), summed heavy (0.7x) under the morphed body (0.5x). No other model has a separate, independently-enveloped 808 boom — this is what makes ANA the warmest/lowest model (PITCH also mapped lower, [30,110] Hz vs FM2's [35,120])."
    - "WAVE MORPH = a 2-table wt_read_bl crossfade between wave 0 (sine, warm sub) and wave 5 (analog, saturated warmth): body = ba + morph*(bb-ba). Branch-free, transcendental-free (two guard-sample reads + a lerp)."
    - "DIG uses the shared crush() as a control-rate/render-rate split: BIT DEPTH maps v->bits[6,14] then powf(2,bits) -> bit_levels ONCE in set_param; render calls crush(body, eff_levels) which is only a bounded round (roundf) — no per-sample powf. Effective levels fold in the Page-1 TRS TNE crunch bias, recomputed once per block."
    - "Page-1 knob responsiveness on wavetable models needs a real spectral lever, not a crush nudge: DIG's first cut wired TRS TNE as a bit-level halve, which moved RMS ~9e-5 and ZCR 0 (the battery's dead-control signal). Reworked TRS TNE to also morph the body wave toward the brightest chip wave (wave 4) — that adds upper harmonics / zero crossings so the tone knob measurably moves the attack-window spectrum. Same lesson as B-04's LP<->raw brightness blend."
    - "Both models keep all 8 Page-1 keys responsive by giving TRS DEC a real target (sample-thump length, exp 5..120 ms) and TRS TNE a real spectral target (body brightness morph / crunch bias), rather than leaving shared Page-1 keys as no-ops."
key-files:
  created:
    - "src/models/ana.c"
    - "src/models/dig.c"
  modified:
    - "src/models/model_registry.c"
    - "tests/test_params.c"
decisions:
  - "ANA's sub-oscillator tracks the UN-SWEPT f0 (not the pitch-swept fbody) so the 808 boom is a stable low fundamental that sustains under the body's downward sweep — this is the defining 808 character. Its env is independent of the amp env (SUB DECAY is its own long exp map 100..900 ms, ~400 ms default), so the boom can outlast or undercut the body tail per taste."
  - "ANA PITCH mapped to [30,110] Hz (~45 Hz at v=0.5), LOWER than the FM2/WTR/DIG [35,120] Hz pocket, to sit in 808 sub territory (B-RESEARCH §ANA default f0 ~45 Hz). This also helps ANA read as the lowest/warmest model vs DIG's crisper mid pocket in the distinctness gate."
  - "DIG BIT DEPTH is a TIMBRAL control (always somewhat on: bits 6..14, default ~10 at v=0.5), contrasting HRD's future aggressive CRUSH. Higher knob = MORE bits = cleaner; lower = fewer bits = crunchier. Level count precomputed at control rate; crush() per sample is a bounded round only."
  - "DIG body waves drawn from the bright end of the 6 factory waves (saw 2 / square 3 / digital 4) via WAVE IDX, while ANA morphs the warm end (sine 0 / analog 5) — the wavetable-source split is the primary ANA-vs-DIG distinctness axis on top of ANA's sub-osc and DIG's crush."
  - "DIG's Page-1 TRS TNE was reworked from a bit-level-only bias to ALSO morph the body toward the brightest chip wave after the battery flagged it dead (rms_delta 9e-5, zcr_delta 0). The crunch bias is retained and layered, but the wave morph is what makes the knob spectrally responsive (Rule 1 auto-fix)."
metrics:
  duration: "6min"
  tasks: 3
  files: 4
  completed: "2026-09-29"
---

# Phase B Plan 05: ANA + DIG Analog + Digital Summary

Implemented the analog/digital wavetable family — **ANA** (KICK-09, the warm 808 sub-boom king) and **DIG** (KICK-07, the digital/retro bit-crushed kick) — as thin recipes over the B-02 shared primitives and the FM2 reference voicing, registered both by replacing their NULL registry slots, and extended the D-B02 voicing + distinctness batteries to cover them. `make test` is green with **5 registered models and 10 mutually-distinct pairs**; both render loops are transcendental-free (DIG's only per-sample math is the shared `crush()` bounded round) and both state structs fit the 4096-byte overlay.

## What Was Built

- **ANA (`src/models/ana.c`, KICK-09):** a band-limited **WAVE MORPH** body — a 2-table `wt_read_bl` crossfade between wave 0 (sine, warm sub) and wave 5 (analog, saturated warmth) — pitch-swept with the FM2 dual-envelope 808<->909 CURVE blend, summed with a **dedicated, independently-enveloped SUB-OSCILLATOR** (a pure `g_sine_table` sine at the un-swept `f0`, with its own SUB LEVEL mix and long SUB DECAY, exp 100..900 ms) that is ANA's 808-boom distinctness lever, plus a synthesized SAMPLE attack-thump layer. PITCH is mapped lower ([30,110] Hz, ~45 Hz default) for sub territory. Page-1 TRS DEC drives the thump length and TRS TNE biases the body brighter so all 8 Page-1 keys stay responsive. COLOR output LP + `fx_process` final stage. `ana_trigger` fully re-inits body/sub/samp phases, all filter states, all envelopes, and the FX sample-and-hold. `_Static_assert(sizeof(ana_state) <= 4096)`. 4 P2 slots (`WAVE MORPH`, `SUB LEVEL`, `SUB DECAY`, `SAMPLE`) emitted as full `{key,name,type,min,max}` JSON via `ana_p2_slot_desc`.
- **DIG (`src/models/dig.c`, KICK-07):** a digital-character `wt_read_bl` body (WAVE IDX selects among the bright factory waves saw/square/digital) pitch-swept via the CURVE blend, with **BIT DEPTH** applying the shared `crush()` bit-reducer as a *timbral* control (v -> bits[6,14] -> level count precomputed once at control rate; render calls `crush()` as a bounded round only), a dedicated **PITCH ENV** sweep-depth scalar (the chip "pew"), and a SAMPLE LAYER attack thump. DIG is crisper/brighter than ANA (bright wave sources + crunch). Page-1 TRS DEC = thump length, TRS TNE = body-brightness morph toward the chip wave + crunch bias. COLOR LP + `fx_process`; full trigger re-init; `_Static_assert(sizeof(dig_state) <= 4096)`. 4 P2 slots (`WAVE IDX`, `SAMPLE`, `BIT DEPTH`, `PITCH ENV`).
- **Registration (`src/models/model_registry.c`):** replaced the `[MODEL_ANA]` and `[MODEL_DIG]` NULL slots with `&g_ana_vtable` / `&g_dig_vtable` designated initializers (+ their `extern` decls). Array length still `== MODEL_COUNT`; the 5 unimplemented slots (FM4/PHY/HRD/USR/GEN) stay NULL (guarded). The `_Static_assert(sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT)` is intact.
- **Voicing batteries (`tests/test_params.c`):** added `assert_param_responsive` calls for ANA (`PK_ANA_MORPH/SUBLVL/SUBDEC/SAMPLE` + FX) and DIG (`PK_DIG_WAVEIDX/SAMPLE/BITDEPTH/PITCHENV` + FX), each also sweeping the 8 Page-1 keys. `tests/test_distinct.c` (unchanged, loops MODEL_COUNT) is now a live **5 registered / 10 pairs** gate — all distinct. `tests/test_render.c` (unchanged) auto-covers ANA/DIG, writing `tests/output/ANA_kick.wav` + `tests/output/DIG_kick.wav`.

## Verification

- `make test` exits 0: `test_fm2`, `test_fx`, `test_switch` (**25 pairs**), `test_params` (FM2 + WTR + TRS + ANA + DIG batteries), `test_distinct` (**5 registered, 10 pairs**), and `test_render` (full lifecycle + per-model loop) all green.
- Registry grep gates: `[MODEL_ANA] = &g_ana_vtable` (line 27) and `[MODEL_DIG] = &g_dig_vtable` (line 28) present; `sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT` assert intact; `test_params.c` references `PK_ANA_MORPH` + `PK_DIG_BITDEPTH`.
- Both `ana_render` and `dig_render` verified free of `sinf/expf/tanf/tanhf/powf/cosf/logf` (all coeff/transcendental work is in set_param/trigger); DIG's only per-sample math is the shared `crush()` bounded `roundf`. Both TUs compile clean under `-std=gnu11 -O3 -fPIC -Wall -Wextra -Werror`.
- `tests/output/ANA_kick.wav` and `tests/output/DIG_kick.wav` written non-empty (262188 bytes each).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] DIG Page-1 TRS TNE was an unresponsive (dead) control**
- **Found during:** Task 2 (DIG voicing battery — `trs_tne` failed at `rms_delta=9.023e-05`, `zcr_delta=0.000e+00`, `atk_zcr_delta=0.000e+00`).
- **Issue:** DIG's first cut wired Page-1 TRS TNE as only a bit-level-count halve (extra crunch). At the default ~1024-level bit depth, halving to ~512 barely moved gross RMS and left zero-crossing content unchanged (crush adds no zero crossings at these low fundamentals) — a genuinely dead knob by the battery's RMS-OR-ZCR metric.
- **Fix:** Reworked TRS TNE to ALSO morph the body wave toward the brightest digital chip wave (wave 4) — adding upper harmonics / zero crossings so the tone knob measurably moves the attack-window spectrum. The crunch bias is retained and layered on top. Same lesson B-04 applied for transient brightness (spectral lever, not an attenuating/subtle nudge).
- **Files modified:** `src/models/dig.c`
- **Commit:** acec86a

### Could-not-verify (environment)

- **Cross-build + glibc gate not run locally:** `docker` / the `ghcr.io/charlesvestal/schwung-builder` image are unavailable on this macOS host (confirmed: `docker` absent), so `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` (a Task 3 acceptance criterion) could not be executed here. Same environment limitation recorded for A-04 and B-04; **CI is the authoritative cross-build/glibc gate** (per CLAUDE.md and STATE.md [A-03]). Mitigations so the gate should pass: both render loops are verified transcendental-free (no new libm/`_ZGV*`/libmvec risk beyond B-02/B-03 — DIG reuses the existing shared `crush()`), no new exported symbols, and both TUs compile clean under the aarch64 flag subset (`-std=gnu11 -O3 -fPIC -Wall -Wextra -Werror`). **This gate remains UNVERIFIED locally and must be confirmed green in CI.**

## Known Stubs

None. ANA and DIG are fully implemented, registered, FX-active, and pass the automated D-B02 voicing battery + the pairwise distinctness gate (5 registered / 10 pairs). The SAMPLE layers in both models are synthesized attack thumps (swept sine bursts), which is the intended Phase-B approach — real user-sample playback is KICK-10's job (USR, B-08), per B-RESEARCH §ANA/§DIG ("for Phase B a factory .rodata one-shot or a synthesized layer; USR handles real user samples"). The 5 remaining registry slots (FM4/PHY/HRD/USR/GEN) stay NULL per B-01's intermediate-compilation contract (guarded as silence). Manual on-device ear sign-off for both models is tracked for the B-09 voicing audit (D-B04), as planned.

## Self-Check: PASSED

- FOUND: src/models/ana.c
- FOUND: src/models/dig.c
- FOUND: src/models/model_registry.c
- FOUND: tests/test_params.c
- FOUND commit 49dccdd (Task 1 — ANA + registration + battery)
- FOUND commit acec86a (Task 2 — DIG + registration + battery + TRS TNE fix)
- FOUND: tests/output/ANA_kick.wav (non-empty, 262188 bytes)
- FOUND: tests/output/DIG_kick.wav (non-empty, 262188 bytes)
