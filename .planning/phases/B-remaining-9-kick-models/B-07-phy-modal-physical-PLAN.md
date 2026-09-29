---
phase: B-remaining-9-kick-models
plan: 07
type: execute
wave: 6
depends_on: ["B-01", "B-02", "B-03", "B-06"]
files_modified:
  - src/models/phy.c
  - src/models/model_registry.c
  - tests/test_params.c
autonomous: true
requirements: [KICK-05]
must_haves:
  truths:
    - "Selecting PHY produces an organic, woody, resonant kick from 2-3 damped modal resonators excited by a beater burst (KICK-05)"
    - "PHY renders non-silent default, bounded across full param sweep with NO NaN (modal freq/decay clamped), param-responsive, distinct from all oscillator/wavetable models"
    - "PHY's 4 Page-2 slots (BEATER, SHELL SIZE, HEAD TENS, DAMPING) are exposed via p2_slot_desc; routes through fx_process; re-inits on trigger"
  artifacts:
    - path: "src/models/phy.c"
      provides: "PHY modal engine (head + 1-2 body modes via modal_t, noise-burst excitation), state <=4096, g_phy_vtable"
      contains: "g_phy_vtable"
  key_links:
    - from: "phy_render"
      to: "modal_tick (complex-rotation resonators, clamped in modal_excite)"
      via: "B-02 modal_t primitive — no NaN/blowup"
      pattern: "modal_tick"
    - from: "src/models/model_registry.c g_models[]"
      to: "g_phy_vtable at MODEL_PHY"
      via: "append-only registry in enum order"
      pattern: "g_phy_vtable"
---

<objective>
Implement PHY (KICK-05) — the modal physical-model kick, the only NON-oscillator engine. 2-3 damped resonant modes (complex-rotation `modal_t` from B-02) excited by a short filtered noise/click burst, mapping BEATER (excitation), SHELL SIZE (body-mode freqs), HEAD TENS (dominant pitched mode + downward sweep), DAMPING (decay). This is the highest-risk DSP (NaN/blowup if freq/decay unclamped) — the modal primitive already clamps freq to [20, 0.45*SR] and decay to (0,1) (B-02), and PHY re-verifies bounds.

Purpose: The organic/woody/springy voice that no FM or wavetable model provides; proves the modal primitive in a real model.
Output: src/models/phy.c, registry entry, extended param battery.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/B-remaining-9-kick-models/B-RESEARCH.md
@.planning/phases/B-remaining-9-kick-models/B-VALIDATION.md
@src/models/fm2.c
@src/dsp_primitives.h
@src/models/model_registry.c
@src/omega.h

<interfaces>
<!-- From B-01: MODEL_PHY, PK_PHY_* keys, extern g_phy_vtable, vtable has .set_param.
     From B-02: modal_t + modal_excite(m, freq_hz, decay_per_sample, amp) [clamps freq
     to [20,0.45*SR] and decay to (0,1)] + modal_tick(m); noise_t + noise_tick;
     tpt1_lp; env_t; fx_process + fx_state_t. Research skeleton (§Code Examples):
     typedef struct phy_state { modal_t head, body1, body2; env_t amp; noise_t exc; ... }
     _Static_assert(sizeof(phy_state) <= 4096). -->
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: PHY modal engine (KICK-05) — head + body modes, beater excitation</name>
  <read_first>src/models/fm2.c (template shape), src/dsp_primitives.h (modal_t, modal_excite, modal_tick, noise_t, tpt1_lp, env_t, fx_process), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§PHY recipe + defaults + §Code Examples modal skeleton, Pitfall 5 NaN/clamp), src/omega.h (PK_PHY_* keys)</read_first>
  <behavior>
    - PHY default render + trigger: non-silent (RMS>1e-4), finite, |x|<=1, with a natural resonant decay (energy present after the initial excitation, springy tail).
    - Each P2 param (BEATER, SHELL SIZE, HEAD TENS, DAMPING) at lo vs hi changes output.
    - CRITICAL: full lo->hi sweep of every param produces NO NaN/Inf and |x|<=1 (modal freq/decay clamped — Pitfall 5). Test extreme HEAD TENS (highest freq) + lowest DAMPING (longest decay) explicitly.
    - DAMPING high vs low measurably changes tail length.
  </behavior>
  <action>
    Create `src/models/phy.c` (copy fm2.c skeleton; §Code Examples PHY VERBATIM). Recipe (B-RESEARCH §PHY VERBATIM):
    - State: `modal_t head, body1, body2; env_t amp; noise_t exc;` + excitation env + cached param values + `fx_state_t fx`.
    - 2-3 damped modes via modal_t (complex-rotation). Map:
      * HEAD TENS (`PK_PHY_HEADTENS`) → the dominant pitched mode's frequency (~55-80 Hz default) + the classic downward pitch envelope on it (reuse the CURVE dual-env to sweep head freq downward at trigger).
      * SHELL SIZE (`PK_PHY_SHELL`) → 1-2 lower-Q body modes' frequencies (bigger shell = lower freq; body ~120-180 Hz default).
      * DAMPING (`PK_PHY_DAMPING`) → decay coefficient of all modes (more damping = shorter/deader; decays 150-400 ms default). Convert ms→decay_per_sample; CLAMP so decay stays in (0,1).
      * BEATER (`PK_PHY_BEATER`) → excitation character: a short bright filtered-noise burst (`noise_t` + `tpt1_lp`), ~2-5 ms, brighter/harder = more HF + shorter.
    - Per trigger: `modal_excite(&st->head, head_freq, head_decay, amp)` etc. (precompute cos_w/sin_w/decay — NEVER per sample); trigger the excitation burst env; reset fx state. modal_excite clamps freq to [20, 0.45*SR] and decay to (0,1) (B-02) — additionally clamp your computed freqs/decays before passing, defense in depth (Pitfall 5).
    - Per sample: `float s = amp_env * (a0*modal_tick(&st->head) + a1*modal_tick(&st->body1) + a2*modal_tick(&st->body2)) + excite_burst;` then COLOR LP; then `fx_process`. Self-limit < 1.0.
    - `phy_p2_slot_desc` emits 4 PHY slots (bounded full JSON objects).
    - `g_phy_vtable = { .name="PHY", .trigger=phy_trigger, .render=phy_render, .set_param=phy_set_param, .set_p2=phy_set_param, .p2_slot_desc=phy_p2_slot_desc };` + `_Static_assert(sizeof(phy_state) <= 4096, ...)`.
    NO per-sample transcendentals (cosf/sinf/expf only in excite/set_param); parse_f; no logging/alloc.
  </action>
  <verify>
    <automated>make test-params && echo PHY_OK</automated>
  </verify>
  <done>PHY renders an organic modal kick (head + body modes + beater burst, downward head sweep); 4 P2 slots exposed; freq/decay clamped (no NaN across full sweep); FX + reinit wired; state <=4096; param battery green.</done>
</task>

<task type="auto">
  <name>Task 2: Register PHY; extend param battery; add explicit NaN-at-extremes assert; wave gate</name>
  <read_first>src/models/model_registry.c, src/omega.h (MODEL_PHY order), tests/test_params.c, .planning/phases/B-remaining-9-kick-models/B-VALIDATION.md (KICK-05 "modal freq/decay clamped (no NaN)"), B-RESEARCH.md (Pitfall 5, Pitfall 6)</read_first>
  <files>src/models/model_registry.c, tests/test_params.c</files>
  <action>
    In `src/models/model_registry.c`: add extern decl + register `&g_phy_vtable` (MODEL_PHY) at its exact enum index; array stays in enum order (Pitfall 6). Depends on B-06 (sequential).
    In `tests/test_params.c`: add `assert_param_responsive` for PHY (PK_PHY_BEATER, PK_PHY_SHELL, PK_PHY_HEADTENS, PK_PHY_DAMPING + 8 Page-1). ADD an EXPLICIT extreme-bounds assertion for PHY (KICK-05 automated criterion): set HEAD TENS=1.0 (max freq), DAMPING=0.0 (min damping / longest decay), SHELL SIZE=1.0, trigger, render 2048 frames, assert every sample `isfinite` and `|x|<=1.0` (proves no modal blowup/NaN at the worst-case corner).
    Run full suite + cross-build + glibc gate.
  </action>
  <acceptance_criteria>
    - `grep -q 'g_phy_vtable' src/models/model_registry.c`
    - `tests/test_params.c` references PK_PHY_HEADTENS and has an explicit PHY extreme-bounds (isfinite) assertion
    - `make test` exits 0; `tests/output/PHY_kick.wav` non-empty
    - `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes
  </acceptance_criteria>
  <verify>
    <automated>make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so && echo WAVE_OK</automated>
  </verify>
  <done>PHY registered in enum order; param battery + explicit NaN-at-extremes assertion cover PHY; per-model WAV written; full suite + cross-build + glibc gate green.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 with PHY registered and covered incl. the extreme-bounds NaN assertion.
- `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes.
- phy_state `_Static_assert` <= 4096; modal freq/decay clamped; routes through fx_process; fully re-inits on trigger.
</verification>

<success_criteria>
- PHY (KICK-05) is a distinct, non-silent, bounded (no NaN even at worst-case modal corner), param-responsive organic kick with correct 4-slot Page-2 descriptor and FX chain — automated criteria green (manual sign-off in B-09).
</success_criteria>

<output>
After completion, create `.planning/phases/B-remaining-9-kick-models/B-07-SUMMARY.md`
</output>
