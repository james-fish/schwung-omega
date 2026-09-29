---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
stopped_at: Completed B-07-phy-modal-physical-PLAN.md
last_updated: "2026-09-29T12:57:45.706Z"
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
**Plan:** 7 of 9 complete (B-01..B-07 done); next is B-08 (USR/GEN userload + generative)
**Status:** Executing Phase B
**Progress:** Phase B 7/9 plans complete

```
[◐○○○○○○] 0/7 phases (A: 3/4 plans + A-04 runbooks pending on-device; B: 7/9 plans)
```

---

## Performance Metrics

*(Populated as phases complete)*

| Metric | Value |
|--------|-------|
| Phases complete | 0/7 |
| Requirements delivered | 17/42 (FNDTN-01/02/03/04/05/06/07, KICK-01/02/03/04/06/07/08/09/12/15) |
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
| Phase B-remaining-9-kick-models P05 | 6min | 3 tasks | 4 files |
| Phase B-remaining-9-kick-models P06 | 7min | 3 tasks | 4 files |
| Phase B-remaining-9-kick-models P07 | 4min | 2 tasks | 3 files |

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
- **[B-04] Wavetable+transient family (WTR/TRS)** — WTR (KICK-04, clean separable transient) + TRS (KICK-08, 909 click<->noise transient morph + own PK_TRS_CURVE) as thin recipes over B-02 primitives; transient brightness reworked to LP<->raw BLENDS (cascaded LPs made the click inaudible/unresponsive) with the bright transient LEADING the attack; both registered by replacing their NULL slots; Makefile TEST_SRCS switched to `$(wildcard src/models/*.c)` so registry-named symbols always resolve. 3 registered / 3 distinct pairs.
- **[B-05] ANA = dedicated sub-oscillator (KICK-09)** — a 2-table analog WAVE MORPH body (sine<->analog `wt_read_bl` crossfade) pitch-swept via the FM2 CURVE blend, PLUS a dedicated, independently-enveloped SUB-OSCILLATOR (pure `g_sine_table` sine at the UN-swept f0, own SUB LEVEL + long SUB DECAY exp 100..900 ms) = the 808 sub-boom lever no other model has, PLUS a synthesized SAMPLE attack-thump. PITCH mapped lower [30,110] Hz (~45 Hz) for sub territory. The sub tracks un-swept f0 so the boom is a stable low fundamental under the body's downward sweep.
- **[B-05] DIG = digital body + BIT DEPTH as timbre (KICK-07)** — digital-character `wt_read_bl` body (bright saw/square/digital factory waves via WAVE IDX) + BIT DEPTH applying the shared `crush()` as a TIMBRAL control (v->bits[6,14], `powf(2,bits)`->levels precomputed at control rate; render is a bounded `roundf` only — no per-sample powf), contrasting HRD's future aggressive CRUSH. Warm-ANA-vs-crunchy-DIG split on the wavetable-source axis (ANA morphs sine/analog, DIG selects saw/square/digital) on top of ANA's sub-osc / DIG's crush. DIG's Page-1 TRS TNE reworked from a dead bit-level nudge to a body-brightness wave morph after the battery flagged it (rms_delta 9e-5, zcr_delta 0) — a spectral lever, not a subtle attenuation (same lesson as B-04). Now 5 registered / 10 distinct pairs; test_switch 25 pairs.
- **[B-06] HRD = bounded distortion + crush (KICK-06)** — hard-techno wavetable body (bright factory waves) + punchy SAMPLE LAYER blended by MIX + DRIVE reusing the shared `fx_process` SAT (v<0.6) / Fold (v>0.6, harder edge) at high amt as its ONLY distortion (no bespoke `fast_tanh` — STATE.md bug #2; the shared FX forms self-limit to [-1,1] at max drive) + CRUSH via the shared `crush()` (v->bits[16,4], default-OFF aggressive vs DIG's always-on bits[6,14] timbre; levels precomputed at control rate, render is a bounded roundf). Uses TWO separate `fx_state_t` fields (drive_fx + post-kick fx) so the two Crush sample-and-holds never collide, both reset on trigger. The loudest/most aggressive kick; HRD-vs-DIG split = distortion/loud vs lo-fi/digital-crunch.
- **[B-06] FM4 = static routing tables, no per-sample algo branching (KICK-03)** — 4-op FM extending the FM2 core; the 4 OPL3-style algorithms are `static const uint8_t g_fm4_algo[4][NUM_OPS]` (modulator-source per op, FM4_NONE sentinel) + `g_fm4_carrier[4][NUM_OPS]` routing tables WALKED in render (CLAUDE.md — the active algo's two rows snapshotted once per block, ops evaluated op3->op0 in a single forward pass so each modulator is computed before its target; NO per-sample branching on algorithm). Per-op AM + FM-index envs; op3 self-FEEDBACK scaled + hard-clamped to <=0.7 (no runaway); OP RATIO spread + ALGO detune recomputed at control rate; carriers summed with 1/ncar normalization (self-limit <1.0). ALGORITHM (PK_FM4_ALGO) selects the table 0..3; ALGO (PK_FM4_ALGO2) is a per-op metallic detune morph (research disambiguation). Now 7 registered / 21 distinct pairs; test_switch 49 pairs. KICK-06 + KICK-03 delivered (commits 828f423, c04866c).
- **[B-07] PHY = modal physical model (KICK-05)** — the ONLY non-oscillator engine: 3 damped complex-rotation `modal_t` modes (head dominant pitched mode + body1/body2 shell modes, amps 0.9/0.5/0.25, body decays 0.5x/0.3x head) excited by a short bright filtered-noise beater burst (`noise_t` + `tpt1_lp`). HEAD TENS -> head freq (exp 45..110 Hz) + a SWEPT-UP excitation start (`head_f*(1+0.8+1.2*curve)`, CURVE deepens the 909 drop; NO per-sample re-tune since modal_t is fixed-tune once excited — the beater carries the bright attack, the mode carries the settled tone); SHELL SIZE -> body mode freqs (bigger=lower); DAMPING -> shared decay (exp 400..60 ms, ms->decay clamped (0,1)); BEATER -> burst brightness LP + length. Defense-in-depth: every freq clamped [20,0.45*SR] + every decay via `modal_decay_from_ms` clamped (0,1) BEFORE modal_excite (which re-clamps) + a bounded `x/(1+|x|)` output self-limit — proven NaN-free/bounded at the worst-case corner (HEAD TENS=1.0/DAMPING=0.0/SHELL=1.0, explicit `assert_phy_extremes_no_nan`). Beater RESEEDED per trigger (the KICK-13 switch memset zeroes the xorshift seed; xorshift of 0 stays 0 -> silence otherwise). Render loop transcendental-free. Registered by replacing its NULL slot; 8 registered / 28 distinct pairs, test_switch 64 pairs. KICK-05 delivered (commits de84f82, 3e06d61).
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

**Next action:** Execute Plan B-08 (USR + GEN, KICK-10/KICK-11) — USR userload (off-thread WAV/wavetable at create_instance, dedicated pre-sized buffer since it won't fit model_state[4096]; SAMPLE SELECT/WT MORPH/LAYER VOL/PITCH ENV, built-in fallback so it's non-silent) + GEN generative engine (PRNG xorshift64 seeded from SEED + scale_quantize over g_scales + Euclidean density gating, self-clocking for offline audition; transport-sync to get_beat_position + full Groove Page 2 deferred to Phase C). Register both by replacing the final two NULL slots -> registry fully populated (10/10, test_distinct 10 registered / 45 pairs). Then B-09 splices Page 2 + runs the on-device voicing audit. (A-04 on-device validation still pending hardware — see Todos.)

**Stopped at:** Completed B-07-phy-modal-physical-PLAN.md

**Recent activity:**

- 2026-09-29: B-07 complete — PHY (KICK-05, the only non-oscillator engine: an organic/woody modal kick from 3 damped complex-rotation modal_t modes — head dominant pitched mode + 2 shell body modes — excited by a bright filtered-noise beater burst; HEAD TENS->head freq + swept-UP excitation start so the fixed mode settles down without per-sample re-tune, SHELL SIZE->body mode freqs, DAMPING->shared decay ms->decay clamped (0,1), BEATER->burst brightness/length) as a thin recipe over the B-02 modal_t/noise primitives + FM2 dual-env CURVE; defense-in-depth freq/decay clamps + x/(1+|x|) self-limit proven NaN-free/bounded at the worst-case corner (explicit assert_phy_extremes_no_nan: HEAD TENS=1.0/DAMPING=0.0/SHELL=1.0); beater reseeded per trigger (memset zeroes the xorshift seed otherwise); registered by replacing its NULL slot; render transcendental-free; `make test` green — 8 registered / 28 distinct pairs, test_switch 64 pairs; cross-build/glibc gate deferred to CI (no local Docker). KICK-05 delivered (commits de84f82, 3e06d61)

- 2026-09-29: B-06 complete — HRD (KICK-06, the loudest/most aggressive kick: bright wavetable body + punchy SAMPLE LAYER blended by MIX + DRIVE reusing shared fx_process SAT<0.6/Fold>0.6 at high amt as its ONLY distortion — no bespoke fast_tanh, STATE.md bug #2 — + CRUSH via shared crush() default-off aggressive; TWO separate fx_state fields so the drive + post-kick Crush s&h never collide) + FM4 (KICK-03, complex 4-op FM: 4 OPL3-style algorithms as static const g_fm4_algo + g_fm4_carrier routing tables WALKED in render with NO per-sample algo branching, ops evaluated op3->op0 single forward pass; per-op AM/index envs; op3 self-FEEDBACK hard-clamped <=0.7; OP RATIO spread + ALGO detune at control rate) as thin recipes over B-02 FX/crush + the FM2 FM core; both registered by replacing their NULL slots; both render loops transcendental-free; `make test` green — 7 registered / 21 distinct pairs, test_switch 49 pairs; cross-build/glibc gate deferred to CI (no local Docker). KICK-06 + KICK-03 delivered (commits 828f423, c04866c)
- 2026-09-29: B-05 complete — ANA (KICK-09, the warm 808 sub-boom king: 2-table analog WAVE MORPH body + dedicated independently-enveloped SUB-OSC at un-swept f0 with long SUB DECAY + sample thump; PITCH lowered to [30,110] Hz) + DIG (KICK-07, the digital/retro kick: bright saw/square/digital body via WAVE IDX + BIT DEPTH as timbre via shared crush() with control-rate level precompute + PITCH ENV) as thin recipes over B-02 primitives + FM2 voicing; both registered by replacing their NULL slots; DIG's Page-1 TRS TNE reworked from a dead bit-nudge to a body-brightness wave morph after the battery flagged it (rms 9e-5, zcr 0); `make test` green — 5 registered / 10 distinct pairs, test_switch 25 pairs; cross-build/glibc gate deferred to CI (no local Docker). KICK-09 + KICK-07 delivered (commits 49dccdd, acec86a)
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
