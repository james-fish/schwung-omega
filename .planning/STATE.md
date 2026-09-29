---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
stopped_at: Completed B-04-wtr-trs-wavetable-transient-PLAN.md
last_updated: "2026-09-29T12:28:00.013Z"
progress:
  total_phases: 7
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State: Omega

**Last updated:** 2026-09-28

---

## Project Reference

**Core value:** A techno producer on Ableton Move should be able to dial in a full Bohm-style kick + rumble + performer system from one module, with immediate access to the most expressive performance controls on the root page.

**What it is:** A native C Schwung module for Ableton Move — a multi-engine kick synthesizer (10 models) + 4-tap groove rumble generator + live performer mixer, in a single `dsp.so` loadable in Schwung slots, DR32 pads, and Movy tracks.

**Current focus:** Phase B — Remaining 9 Kick Models

---

## Current Position

**Phase:** B — Remaining 9 Kick Models — EXECUTING
**Plan:** 4 of 9 complete (B-01, B-02, B-03, B-04 done); next is B-05 (ANA/DIG analog/digital)
**Status:** Executing Phase B
**Progress:** Phase B 4/9 plans complete

```
[◐○○○○○○] 0/7 phases (A: 3/4 plans + A-04 runbooks pending on-device; B: 4/9 plans)
```

---

## Performance Metrics

*(Populated as phases complete)*

| Metric | Value |
|--------|-------|
| Phases complete | 0/7 |
| Requirements delivered | 13/42 (FNDTN-01/02/03/04/05/06/07, KICK-01/02/04/08/12/15) |
| On-device CPU (full chain) | Not yet measured (Phase D target: 10-15%) |

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase A-foundation-fm2-model P01 | 6min | 3 tasks | 17 files |
| Phase A-foundation-fm2-model P02 | 6min | 3 tasks | 8 files |
| Phase A-foundation-fm2-model P03 | 3min | 3 tasks | 6 files |
| Phase A-foundation-fm2-model P03 | 3min | 3 tasks | 6 files |
| Phase A-foundation-fm2-model P04 | 2min | 2 tasks | 2 files |
| Phase B-remaining-9-kick-models P01 | 4min | 3 tasks | 5 files |
| Phase B-remaining-9-kick-models P02 | 8min | 3 tasks | 6 files |
| Phase B-remaining-9-kick-models P03 | 6min | 3 tasks | 5 files |
| Phase B-remaining-9-kick-models P04 | 9min | 3 tasks | 5 files |

## Accumulated Context

### Key Decisions

- **[B-01] Internal vtable extended, not the ABI** — `kick_model_vtable_t` gained a `set_param` fn ptr (no `_Static_assert` binds it; internal, not host ABI). dsp.c now dispatches all kick keys through `g_models[inst->model]->set_param` instead of the hardcoded `fm2_set_param` (Pitfall 2 fix). The locked `host_api_v1_t`/`plugin_api_v2_t` `+120`/`+56` asserts were left untouched.
- **[B-01] Designated-initializer registry (intermediate-compilation contract)** — `model_id_t` grown append-only to `MODEL_COUNT=10`; `g_models[]` uses designated initializers with `[MODEL_FM2]` set and 9 slots C-zero-init to NULL, guarded by `_Static_assert(len == MODEL_COUNT)`. The array links at every wave; each later model plan replaces its own NULL WITH the `.c` that defines its vtable symbol. dsp.c + the switch harness treat NULL slots as silence (no NULL deref).
- **[B-01] Clean model-switch re-init (KICK-13)** — `PK_MODEL` change memsets `model_state[4096]` and re-primes defaults through the incoming model's `set_param`, killing the stale-state/NaN hazard when a new model reinterprets the shared overlay bytes. `tests/test_switch.c` is the automated KICK-13 gate (A->B->A + trigger, finite/bounded/non-stale, registry-length + NULL-slot-silence asserts); forward-compatible via NULL-slot skip.
- **[B-02] FX chain (KICK-14) control-rate/render-rate split** — `fx_config` (control rate) precomputes ALL transcendentals: Crush's `powf` bit-level count into `fx_state_t.crush_levels` and the Diode `1-exp(-z)` shaping LUT (the only `expf`). `fx_process` (render) reads only precomputed state + the LUT — verified `powf`/`sinf`/`expf`/`tanf`-free. Shared `crush(x,levels)` for HRD/DIG (caller precomputes levels).
- **[B-02] All 5 FX modes dry/wet-blend by amt** — Diode/Clip/SAT/Fold/Crush each do `y=(1-amt)*x+amt*wet`, so amt=0 is transparent (must-have) and amt=1 is full effect, every branch bounded to [-1,1]. The plan's raw forms (Diode 0.9->0.59, Clip 0.9->0.47) are non-transparent at amt=0, hence the blend. The unbounded reference `fast_tanh` (`x/(1-x)`) is avoided (STATE.md bug #2).
- **[B-02] Shared synthesis primitives added to `dsp_primitives.*`** — `modal_t` complex-rotation resonator (freq/decay clamped in excite, transcendental-free tick), `prng_t` xorshift64 (nonzero-seed forced, deterministic — NOT the libc PRNG), `noise_t` burst, `scale_quantize` over `g_scales[4][12]` in `.rodata`, and `wt_read_bl` band-limited read. Each model plan (B-03..B-08) is now a thin recipe over these; no model hand-rolls a clipper/resonator/PRNG/table.
- **[B-02] Wavetables generated into `.rodata` at build time (KICK-15)** — `tools/gen_wavetables.c` emits `g_wavetables[6][1][2049]` `_Alignas(16)` (sine/tri/saw/square/digital/analog, 2048+1 guard, `%.9e` literals), mirroring the `sine_table.h` pattern; committed header, Makefile order-only prereq regenerates only when missing. BANDS=1 to start (kicks rarely alias at 40-200 Hz); add bands only if the voicing harness detects aliasing.
- **[B-03] FM2 re-voiced to the reference bar (D-B03)** — PITCH uses an exponential map `35*(120/35)^v` over [35,120] Hz (default ~50 Hz techno pocket, replaces linear 30-200); sweep decoupled from `f0*4` to a curve-coupled `clamp(f0*(2+curve*4), <=480 Hz)` recomputed on both PITCH and CURVE changes; LENGTH exp map [50,1500] ms; FM INDEX narrowed 0-8 (was 0-12; tail buzzy past 8); CURVE keeps the dual-env OUTPUT blend with tuned 15 ms (909) / 300 ms (808) constants. FM2 is the reference bar for B-04..B-08.
- **[B-03] FM2 routes final output through fx_process (KICK-14)** — `fx_state_t fx` added to `fm2_state` (static_assert <=4096 holds); `fm2_set_param` calls `fx_config` at control rate on FX_TYPE/FX_AMT (Crush's powf runs there, not render); `fm2_render` applies `fx_process(fx_type*4, s, amt, &fm->fx)` as the final per-sample stage; render verified free of sinf/expf/tanf/tanhf/powf. Trigger resets the FX sample-and-hold but preserves precomputed crush_levels. FX chain now proven inside a real model.
- **[B-03] Reusable automated voicing battery (D-B02)** — `tests/test_params.c` `assert_param_responsive(api,inst,model_idx,keys,nkeys)` proves non-silent default + each-param-lo-vs-hi-differs + bounded-at-extremes; `tests/test_distinct.c` computes pairwise RMS-envelope+spectral-ZCR distinctness. Both loop MODEL_COUNT + skip NULL slots so B-04..B-08 reuse them by passing their own P2 keys (distinctness empty-trivial in Wave 2, a real gate as models land). The 'measurably changes' metric is RMS-envelope OR spectral-ZCR (whole-buffer + 30 ms attack window) so transient/brightness params (e.g. TRS TNE) register — the plan's RMS-OR-spectral-centroid behavior.
- **Single module** (not split kick + rumble) — inter-pad routing may not be supported in DR32/Movy; single module is certain to work
- **Hybrid DSP fidelity** — accurate FM/wavetable/transient engines; modal damped resonator for PHY; TPT SVF instead of ZDF Moog ladder (~3-5% CPU saving)
- **Synthesis-method model IDs** (FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN) — IP avoidance + user clarity
- **Root page = 8 bidirectional performance macros** — Move's 8 encoders are prime real estate; no dedicated nav page
- **Float-only internal signal path** — int16→float once at input, float→int16 once at output; never round-trip through int16 mid-chain
- **vtable dispatch** per model (trigger, render, set_p2, p2_slot_desc); registry array indexed by model_id_t; no giant switch
- **Single instance struct, one calloc** (~730-780 KB, dominated by 705 KB groove delay buffer); wavetables in `.rodata`, shared across instances
- **[A-01] Sine table literals use `%.9e`** — `%.9g` printed bare `0`/`1`/`-1` which with an `f` suffix are invalid C float literals; `%.9e` guarantees a decimal point + exponent
- **[A-01] `omega_to_i16` int16 boundary lives in `dsp_primitives.h`** — shared by the test harness (Wave 0) and A-02's `render_block`, single clamp+isfinite+lrintf point
- **[A-01] Malloc trap compiled out on Darwin** — `__libc_*` interposition is Linux-only; the Makefile drops `-DOMEGA_MALLOC_TRAP` on macOS, so Linux CI is the authoritative FNDTN-03 gate
- **[A-01] No `-mcpu` pinning** — per D-15, the Cortex core flag is deferred to on-device `/proc/cpuinfo` confirmation (A-04); baseline ARMv8-A only
- **[A-02] FM2 output scaled 0.6 body / 0.4 click** — the summed carrier+click can exceed 1.0; the engine self-limits so the float output stays within [-1,1] before int16 conversion (FNDTN-07/D-12), rather than relying on the clamp to mask overflow
- **[A-02] `omega_build_ui` stub behind `#ifndef OMEGA_HAS_UI`** — dsp.c ships a minimal `{"pages":[]}` so A-02 links standalone; A-03's ui.c defines OMEGA_HAS_UI and owns the real `ui_hierarchy` (D-08) + the D-10 buf_len log
- **[A-03] Kick Page 2 spliced from the model** — ui.c drops the outer `[` `]` of the active model's `p2_slot_desc` array and injects the interior into its own `"slots":[...]`, then appends FX TYPE/AMT; the model owns its slot list, ui.c owns page structure (clean for Phase B models)
- **[A-03] `omega_build_ui` returns bytes-written excluding terminator** — bounded `ui_append` copies `min(len, remaining)` reserving the terminator byte so it never overruns `buf_len` and always null-terminates (Pitfall 3/4); matches `fm2_p2_slot_desc` convention
- **[A-03] D-10 buf_len log stays in dsp.c, formatted locale-independently** — dsp.c owns `g_host`; `omega_itoa_msg` uses manual digit extraction (no `snprintf`/`atof`); one-shot flag-guarded, `SPIKE (D-10)` comment schedules removal/gating before ship; native harness measured `ui_buflen=4096`
- **[A-03] CI cross-build + glibc gate flipped to blocking** — `continue-on-error: false` now that dsp.c/ui.c/registry/fm2.c all exist; `make dsp.so` + glibc/libmvec/export gate must pass
- **[A-04] On-device validation captured as runbooks, not fabricated** — `docs/SOC_IDENTIFICATION.md` (/proc/cpuinfo decode table, -mcpu deferred per D-15) + `docs/ON_DEVICE_VALIDATION.md` (build→deploy→3-host loads/audible/buf_len table). Cross-build (Docker), deploy (scp), and device SSH are unavailable on the macOS host (all probed and confirmed absent); CI is the authoritative build/glibc gate. Hardware-dependent fields marked `PENDING (on-device)`. **A-04 STOPS at the Task 3 human-verify checkpoint; SC1 (3-host), SC5 (buf_len), D-15 (SoC ID) remain UNVERIFIED pending hardware.**

### Todos / Watchpoints

- **[A-04 ON-DEVICE PENDING] Complete the two runbooks at the Move device** — obtain gate-passing dsp.so (CI artifact or Docker build + glibc_gate.sh), `deploy.sh`, load+listen in Schwung slot / DR32 pad / Movy track (SC1), `grep ui_buflen` the device log (SC5), read `/proc/cpuinfo` (D-15). Fill `docs/ON_DEVICE_VALIDATION.md` + `docs/SOC_IDENTIFICATION.md`, then Phase A is ready for `/gsd:verify-work`.

- **Five reference-code bugs to NOT carry forward**: hardcoded 120 BPM tap interval; unbounded asymmetric fast_tanh; amplitude-threshold ducking; missing int16 clamp/isfinite; zero-margin scratch buffer
- Set FPCR flush-to-zero bit explicitly in `render_block` (per-thread, not inherited)
- Use granular fast-math subset, NOT blanket `-ffast-math` (avoids libmvec symbols)
- Add `MAX_BLOCK` margin to scratch buffers

### Open Questions (from research)

| Question | Resolve in Phase |
|----------|------------------|
| What `buf_len` does each host pass to `get_param("ui_hierarchy")`? | A (spike) → E |
| Are `set_param`/`get_param` guaranteed single-threaded? | A |
| Move per-process RAM ceiling (groove buffer sizing)? | C |
| Exact Move SoC / Cortex core (build `-mcpu` flag)? | A |
| Does Schwung re-query `ui_hierarchy` on model change or cache? | E |
| Host state-blob byte cap for Movy/Schwung slots? | G |

### Blockers

None.

---

## Session Continuity

**Next action:** Execute Plan B-05 (ANA/DIG analog + digital models) — same recipe pattern over the B-02 primitives + FM2 reference voicing (wt_read_bl body + sub-osc for ANA's 808 boom; digital/bit-crushed body for DIG via the shared crush()); register their vtables (replace the [MODEL_ANA]/[MODEL_DIG] NULL slots) so test_distinct grows to a 5-model gate; add their assert_param_responsive calls. Reuse the B-04 lesson: transient/character controls should be LP<->raw blends (not cascaded LPs) and any lead-transient should genuinely lead the attack. (A-04 on-device validation still pending hardware — see Todos.)

**Stopped at:** Completed B-04-wtr-trs-wavetable-transient-PLAN.md

**Recent activity:**

- 2026-09-29: B-04 complete — WTR (KICK-04, clean wavetable body + dedicated separable transient) + TRS (KICK-08, advanced click<->noise transient + WT-color body morph + own 909-biased pitch curve) as thin recipes over B-02 primitives + FM2 voicing; both registered by replacing their NULL registry slots; transient brightness reworked to LP<->raw blends (cascaded LPs made the click inaudible/unresponsive) with the bright transient leading the attack; TEST_SRCS switched to the src/models/*.c wildcard; `make test` green — 3 registered / 3 distinct pairs; cross-build/glibc gate deferred to CI (no local Docker). KICK-04 + KICK-08 delivered (commits 415e092, 3e925a7, 0e84606)
- 2026-09-29: B-03 complete — re-voiced FM2 to the reference bar (D-B03): exp PITCH map [35,120] Hz (~50 Hz default), curve-coupled sweep clamp(f0*(2+curve*4),<=480 Hz), exp LENGTH [50,1500] ms, narrowed FM INDEX 0-8, tuned CURVE 15ms/300ms; wired KICK-14 FX into fm2_render (fx_config control-rate, fx_process render, powf/expf/tanf-free); reusable voicing battery (test_params.c) + pairwise distinctness (test_distinct.c) — both loop MODEL_COUNT + skip NULL, reusable by B-04..B-08; `make test` green (commits 440c6a5, dc37820, cf2dacf)
- 2026-09-29: Quick task 260929-cjj — fixed Phase A on-device load crash: `host_api_v1_t` was missing 3 fields (`mapped_memory`/`audio_out_offset`/`audio_in_offset`), shifting `g_host->log` onto a data pointer → segfault when the D-10 spike fired on first `get_param`. Also corrected `module.json` (nested `capabilities`) and `ui_hierarchy` (real `levels` schema) to match Context/01 verbatim. `make test` green (commits ce99134, f10d3b5, 5f799a7)
- 2026-09-29: A-03 complete — real `ui_hierarchy` (ui.c, D-08/D-09): static Page 1 (8 keyed slots) + dynamic FM2 Page 2 spliced from `p2_slot_desc` + FX TYPE/AMT; dsp.c wired to ui.c (fallback removed), D-10 one-shot locale-independent buf_len log; CI cross-build flipped to blocking; harness proves the get_param contract; `make test` green (commits 98e62a0, 3ac9e54, e8802b7)
- 2026-09-29: A-02 complete — FM2 engine (fm2.c), plugin entry points (dsp.c), model registry, module.json; real move_plugin_init_v2 lifecycle harness; `make test` green (commits bccf4e9, c4d028c, 3cac29b)
- 2026-09-29: A-01 complete — shared contracts (omega.h), .rodata sine table, dsp_primitives, full offline test harness + glibc gate + CI; `make test` green (commits 205e0c4, 0ac3a38, 07a33ed)
- 2026-09-28: PROJECT.md, REQUIREMENTS.md (42 v1 reqs), research/SUMMARY.md created
- 2026-09-28: ROADMAP.md created — 7 phases (A-G), 42/42 requirements mapped, coverage validated

---
*State initialized: 2026-09-28*
