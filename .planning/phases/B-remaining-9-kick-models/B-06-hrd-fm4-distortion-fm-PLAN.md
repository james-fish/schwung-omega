---
phase: B-remaining-9-kick-models
plan: 06
type: execute
wave: 5
depends_on: ["B-01", "B-02", "B-03", "B-05"]
files_modified:
  - src/models/hrd.c
  - src/models/fm4.c
  - src/models/model_registry.c
  - tests/test_params.c
autonomous: true
requirements: [KICK-06, KICK-03]
must_haves:
  truths:
    - "Selecting HRD produces an aggressive, distorted hard-techno kick: wavetable body + sample layer + DRIVE (SAT/Fold) + CRUSH, output still bounded (KICK-06)"
    - "Selecting FM4 produces a complex 4-operator FM kick with 4 selectable OPL3-style routing algorithms, per-op AM envelopes, and feedback (KICK-03)"
    - "HRD and FM4 each render non-silent default, bounded across full param sweep (incl. high DRIVE/CRUSH/FEEDBACK), param-responsive, and distinct"
    - "FM4's 6 Page-2 slots and HRD's 4 slots are exposed via p2_slot_desc; both route through fx_process; both re-init on trigger"
  artifacts:
    - path: "src/models/hrd.c"
      provides: "HRD engine (wavetable + sample layer + bounded distortion/crush), state <=4096, g_hrd_vtable"
      contains: "g_hrd_vtable"
    - path: "src/models/fm4.c"
      provides: "FM4 engine (4-op FM, static routing tables, per-op AM env, feedback), state <=4096, g_fm4_vtable"
      contains: "g_fm4_vtable"
  key_links:
    - from: "hrd_render DRIVE/CRUSH"
      to: "fx_process SAT/Fold + crush() (shared, bounded)"
      via: "reuse B-02 FX/crush — no bespoke unbounded distortion"
      pattern: "crush"
    - from: "fm4_render"
      to: "static const routing table (no per-sample branching)"
      via: "g_fm4_algo[4][NUM_OPS]"
      pattern: "g_fm4_algo"
---

<objective>
Implement the distortion + FM family: HRD (KICK-06, the loudest/most aggressive kick — distortion + crush, all bounded) and FM4 (KICK-03, the complex 4-operator FM kick with selectable OPL3-style algorithms). HRD reuses the B-02 FX SAT/Fold + shared `crush()` for DRIVE/CRUSH (no bespoke unbounded distortion — STATE.md bug #2). FM4 extends the FM2 FM core to 4 ops with static routing tables.

Purpose: The distorted kick and the deep-FM kick; both must stay bounded at extreme drive/index/feedback.
Output: src/models/hrd.c, src/models/fm4.c, registry entries (replacing their NULL slots), extended param battery.
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
@src/models/wtr.c
@src/dsp_primitives.h
@src/models/model_registry.c
@src/omega.h

<interfaces>
<!-- From B-01: MODEL_HRD/MODEL_FM4, PK_HRD_*/PK_FM4_*, extern g_hrd_vtable/g_fm4_vtable,
     vtable has .set_param. From B-01 registry: g_models uses designated initializers;
     MODEL_HRD and MODEL_FM4 are currently NULL — this plan REPLACES those NULLs.
     From B-02: fx_process + fx_state_t (SAT=mode2, Fold=mode3),
     crush(float x, float bits), wt_read_bl, g_sine_table (FM4 ops), env_t.
     From fm2.c: the FM carrier/modulator phase-accum + own index env pattern.
     Each state struct _Static_assert <= 4096. -->
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: HRD model (KICK-06) — hard techno: wavetable + sample + bounded distortion/crush</name>
  <read_first>src/models/wtr.c, src/dsp_primitives.h (fx_process SAT/Fold, crush), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§HRD recipe + defaults; Pitfall 5 bounded distortion; Don't-Hand-Roll fast_tanh forbidden), src/omega.h (PK_HRD_* keys), STATE.md (bug #2)</read_first>
  <behavior>
    - HRD default render + trigger: non-silent, finite, |x|<=1, audibly grittier than WTR default (higher harmonic content).
    - Each P2 param (SAMPLE LAYER, MIX, DRIVE, CRUSH) at lo vs hi changes output.
    - At MAX DRIVE and MAX CRUSH + high pitch, output stays finite + |x|<=1 (Pitfall 5: distortion is the bounded FX forms, no divergence).
  </behavior>
  <action>
    Create `src/models/hrd.c` (copy wtr.c). Recipe (B-RESEARCH §HRD VERBATIM):
    - Wavetable body (like WTR, `wt_read_bl`) + a SAMPLE LAYER (`PK_HRD_SAMPLE` selects a short punchy factory `.rodata` one-shot / synthesized layer) mixed by MIX (`PK_HRD_MIX`, default ~0.3 sample layer).
    - DRIVE (`PK_HRD_DRIVE`) feeds the summed signal through the shared FX SAT/Fold at high amt: `driven = fx_process(2 /*SAT*/, sig, drive_amt, &st->drive_fx);` (or Fold for harder edge). REUSE fx_process — do NOT write a bespoke distortion; the reference `fast_tanh` x/(1-x) is FORBIDDEN (STATE.md bug #2). Output stays ≤ 1.0.
    - CRUSH (`PK_HRD_CRUSH`) applies the shared `crush(sig, bits)` bit/SR reduction (aggressive; default low-to-off). Map v→bits.
    - COLOR LP; final `fx_process` (the post-kick FX TYPE/AMT stage) with its OWN `fx_state_t fx` separate from the DRIVE fx_state; trigger resets all phases/filters/envelopes + BOTH fx states; self-limit < 1.0.
    - `hrd_p2_slot_desc` emits 4 HRD slots (bounded full JSON objects).
    - `g_hrd_vtable = { .name="HRD", ... .set_param=hrd_set_param, .set_p2=hrd_set_param, ... };` + `_Static_assert(sizeof(hrd_state) <= 4096, ...)`.
    Distinctness: the only intentionally distorted/crushed kick (rave/industrial). NO per-sample transcendentals beyond fx_process/crush bounded forms; parse_f; no logging/alloc.
  </action>
  <verify>
    <automated>make test-params && echo HRD_OK</automated>
  </verify>
  <done>HRD renders an aggressive distorted/crushed kick reusing bounded FX SAT/Fold + crush; 4 P2 slots exposed; two fx_state fields (drive + post-FX); trigger reinit; state <=4096; bounded at max drive/crush; param battery green.</done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: FM4 model (KICK-03) — 4-operator FM, OPL3-style algorithms + feedback</name>
  <read_first>src/models/fm2.c (FM carrier/mod phase-accum + own index env), src/dsp_primitives.h (g_sine_table, wt_read, env_t, fx_process), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§FM4 recipe + defaults + the 4 algorithm sketches + ALGORITHM vs ALGO disambiguation), src/omega.h (PK_FM4_* keys)</read_first>
  <behavior>
    - FM4 default render + trigger: non-silent, finite, |x|<=1, spectrally richer/more complex than FM2 default (more overtones).
    - Each P2 param (ALGORITHM, OP RATIO, OP INDEX, OP AMP, FEEDBACK, ALGO) at lo vs hi changes output.
    - Switching ALGORITHM (0..3) changes the output (each algorithm routes differently).
    - At MAX FEEDBACK + MAX OP INDEX + high pitch, output stays finite + |x|<=1 (feedback clamped, no runaway).
  </behavior>
  <action>
    Create `src/models/fm4.c` (extend the fm2.c FM core to 4 operators). Recipe (B-RESEARCH §FM4 VERBATIM):
    - 4 operators, all reading `g_sine_table` (no sinf). Each op has its own phase, FM index env, and AM (amplitude) env (reuse env_t). Op frequencies = ratio*carrier; OP RATIO (`PK_FM4_OPRATIO`) sets per-op ratio spread (integer near {1,1,2,3} default for harmonic thump; allow non-integer for metallic).
    - Encode the 4 algorithms as `static const uint8_t g_fm4_algo[4][NUM_OPS]` routing tables (which op modulates which; which ops sum to output) so the render loop WALKS the table — NO per-sample branching on algorithm (CLAUDE.md). Use the 4 sketches from research:
      ALGO0: 4→3→2→1 chain (one carrier, deep); ALGO1: (4→3)+(2→1) two stacks summed (richer); ALGO2: 4→(3,2,1) one mod 3 carriers (bright); ALGO3: (4→1)+2+3 one FM pair + 2 additive carriers (hollow/woody).
    - ALGORITHM (`PK_FM4_ALGO`) selects the routing table (0..3, quantized from v). ALGO (`PK_FM4_ALGO2`) is the disambiguated second control — use it as an algo-morph / per-op ratio-spread / detune (research: "one can be per-op ratio spread"). OP INDEX (`PK_FM4_OPINDEX`) global FM depth; OP AMP (`PK_FM4_OPAMP`) carrier-mix balance; FEEDBACK (`PK_FM4_FEEDBACK`) op-self-feedback (default ~0.1-0.3, CLAMP to avoid runaway — feedback scaled and hard-limited).
    - Body f0 + CURVE pitch sweep same as FM2 (reuse the dual-env blend). COLOR LP; final `fx_process`; `fx_state_t fx`; trigger fully resets all 4 op phases + all envs + fx state; self-limit < 1.0.
    - `fm4_p2_slot_desc` emits ALL 6 FM4 slots (ALGORITHM, OP RATIO, OP INDEX, OP AMP, FEEDBACK, ALGO) as full JSON objects (bounded).
    - `g_fm4_vtable = { .name="FM4", ... .set_param=fm4_set_param, .set_p2=fm4_set_param, ... };` + `_Static_assert(sizeof(fm4_state) <= 4096, ...)`.
    NO per-sample transcendentals; all coeffs (ratios, envs, feedback gain) precomputed in set_param/trigger; parse_f; no logging/alloc.
  </action>
  <verify>
    <automated>make test-params && echo FM4_OK</automated>
  </verify>
  <done>FM4 renders a complex 4-op FM kick with 4 static-table routing algorithms, per-op AM+index envs, clamped feedback; 6 P2 slots exposed; FX + reinit wired; state <=4096; bounded at extremes; param battery green.</done>
</task>

<task type="auto">
  <name>Task 3: Register HRD + FM4 (replace their NULL slots); extend param battery; wave gate</name>
  <read_first>src/models/model_registry.c (designated-initializer array from B-01), src/omega.h (MODEL_HRD/MODEL_FM4 order), tests/test_params.c, .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (Pitfall 6)</read_first>
  <files>src/models/model_registry.c, tests/test_params.c</files>
  <action>
    In `src/models/model_registry.c`: REPLACE the NULL at MODEL_FM4 (index 1) with `[MODEL_FM4] = &g_fm4_vtable` and the NULL at MODEL_HRD with `[MODEL_HRD] = &g_hrd_vtable`, using designated initializers (B-01 established the all-NULL array; B-04/B-05 replaced earlier slots). Add each designated line and remove the corresponding placeholder comment. Add `extern const kick_model_vtable_t g_hrd_vtable, g_fm4_vtable;` if not already provided by omega.h. Array length stays MODEL_COUNT; remaining unimplemented slots stay NULL.
    These model plans are STRICTLY SEQUENTIAL by wave (B-04 → B-05 → B-06 → B-07); each replaces only its OWN NULL slot — no collision. `make test` compiles+links because unimplemented slots remain NULL and are guarded (B-01 contract).
    In `tests/test_params.c`: add `assert_param_responsive` for HRD (PK_HRD_SAMPLE, PK_HRD_MIX, PK_HRD_DRIVE, PK_HRD_CRUSH + 8 Page-1) and FM4 (PK_FM4_ALGO, PK_FM4_OPRATIO, PK_FM4_OPINDEX, PK_FM4_OPAMP, PK_FM4_FEEDBACK, PK_FM4_ALGO2 + 8 Page-1).
    Run full suite + cross-build + glibc gate.
  </action>
  <acceptance_criteria>
    - `grep -q '\[MODEL_HRD\] = &g_hrd_vtable' src/models/model_registry.c && grep -q '\[MODEL_FM4\] = &g_fm4_vtable' src/models/model_registry.c`
    - The `sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT` assert still present; array length unchanged
    - `tests/test_params.c` references PK_HRD_DRIVE and PK_FM4_ALGO
    - `grep -q 'g_fm4_algo' src/models/fm4.c` (static routing tables present)
    - `make test` exits 0 (remaining unimplemented slots still NULL, guarded); `tests/output/HRD_kick.wav` and `tests/output/FM4_kick.wav` non-empty
    - `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes
  </acceptance_criteria>
  <verify>
    <automated>make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so && echo WAVE_OK</automated>
  </verify>
  <done>HRD + FM4 registered by replacing their NULL slots with designated initializers (array length still == MODEL_COUNT); param battery covers both; per-model WAVs written; full suite + cross-build + glibc gate green.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 with HRD + FM4 registered (their NULLs replaced) and covered; remaining slots still NULL and guarded.
- `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes.
- Both state structs `_Static_assert` <= 4096; HRD reuses bounded FX/crush (no fast_tanh); FM4 uses static routing tables (no per-sample branching); both fully re-init on trigger.
</verification>

<success_criteria>
- HRD (KICK-06) and FM4 (KICK-03) are distinct, non-silent, bounded (even at max drive/crush/feedback/index), param-responsive kicks with correct Page-2 descriptors (4 + 6 slots) and FX chain — automated criteria green (manual sign-off in B-09).
</success_criteria>

<output>
After completion, create `.planning/phases/B-remaining-9-kick-models/B-06-SUMMARY.md`
</output>
</output>
