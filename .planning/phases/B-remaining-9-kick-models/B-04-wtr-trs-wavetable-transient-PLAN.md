---
phase: B-remaining-9-kick-models
plan: 04
type: execute
wave: 3
depends_on: ["B-01", "B-02", "B-03"]
files_modified:
  - src/models/wtr.c
  - src/models/trs.c
  - src/models/model_registry.c
  - tests/test_params.c
  - tests/test_render.c
autonomous: true
requirements: [KICK-04, KICK-08]
must_haves:
  truths:
    - "Selecting WTR produces a clean, tight kick with a body oscillator and an independently-enveloped transient (KICK-04)"
    - "Selecting TRS produces a punchy 909-style kick with an advanced transient (click<->noise) and extreme attack clarity (KICK-08)"
    - "WTR and TRS each render non-silent default, bounded across full param sweep, param-responsive, and distinct from each other and other models"
    - "Each model's 4 Page-2 slots (+ FX TYPE/AMT) are exposed via p2_slot_desc as valid bounded JSON"
    - "Both models route final output through fx_process and re-init cleanly on trigger"
  artifacts:
    - path: "src/models/wtr.c"
      provides: "WTR engine (wt_read_bl body + dedicated transient synth), state <=4096, vtable g_wtr_vtable"
      contains: "g_wtr_vtable"
    - path: "src/models/trs.c"
      provides: "TRS engine (wavetable body + advanced transient click/noise), state <=4096, vtable g_trs_vtable"
      contains: "g_trs_vtable"
    - path: "src/models/model_registry.c"
      provides: "g_wtr_vtable + g_trs_vtable registered by replacing the NULL at MODEL_WTR / MODEL_TRS with designated initializers"
      contains: "g_wtr_vtable"
  key_links:
    - from: "src/models/model_registry.c g_models[]"
      to: "g_wtr_vtable at index MODEL_WTR, g_trs_vtable at index MODEL_TRS"
      via: "designated initializer replacing the NULL slot (append-only, enum-indexed)"
      pattern: "g_wtr_vtable"
    - from: "wtr_render / trs_render"
      to: "fx_process + wt_read_bl + noise_t"
      via: "shared primitives from B-02"
      pattern: "wt_read_bl"
---

<objective>
Implement the wavetable-body + dedicated-transient family: WTR (KICK-04, the clean/precise kick) and TRS (KICK-08, the 909 attack specialist). Both reuse the FM2 skeleton (Pattern 4), the B-02 band-limited wavetable read, the shared noise/transient + FX primitives, and the FM2 CURVE blend.

Purpose: Two distinct wavetable+transient voices sharing one primitive set; TRS's advanced transient (click<->noise morph) is its distinctness lever vs WTR's cleanly-separable transient.
Output: src/models/wtr.c, src/models/trs.c, registry entries (replacing their NULL slots), and extended param/render tests covering both.
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
<!-- Copy fm2.c as the model template (state overlay + parse_f + clampf +
     trigger resets phases/filters + render + set_param + p2_slot_desc + vtable).
     From B-01 omega.h: MODEL_WTR, MODEL_TRS enum values; PK_WTR_*, PK_TRS_* keys;
     extern g_wtr_vtable/g_trs_vtable; kick_model_vtable_t now has .set_param.
     From B-01 model_registry.c: g_models[MODEL_COUNT] uses designated initializers;
     MODEL_WTR and MODEL_TRS are currently NULL — this plan REPLACES those NULLs.
     From B-02: wt_read_bl(wave,band,phase01), noise_t + noise_tick, tpt1_lp,
     fx_process + fx_state_t, env_t. Each state struct MUST _Static_assert <= 4096. -->
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: WTR model (KICK-04) — wavetable body + dedicated transient synth</name>
  <read_first>src/models/fm2.c (template), src/dsp_primitives.h (wt_read_bl, noise_t, tpt1_lp, env_t, fx_process), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§WTR recipe + defaults), src/omega.h (PK_WTR_* keys, MODEL_WTR)</read_first>
  <behavior>
    - WTR default render (all params 0.5) + trigger: non-silent (RMS>1e-4), finite, |x|<=1.
    - Each of WTR's 4 P2 params (WAVE SELECT, BODY PITCH, TRANS DECAY, TRANS COLOR) at lo vs hi changes the output measurably.
    - Full lo->hi sweep of every param stays finite + bounded.
    - Transient is separable: with TRANS DECAY near 0 the attackless body differs from full-transient render.
  </behavior>
  <action>
    Create `src/models/wtr.c` by copying the fm2.c structure. Recipe (B-RESEARCH §WTR VERBATIM):
    - Body = `wt_read_bl(wave, 0, phase01)` where WAVE SELECT (`PK_WTR_WAVE`) picks among the factory band-limited waves (sine, tri-ish, richer). Body pitch-swept exactly like FM2 (reuse the dual-env CURVE blend + Page-1 PITCH/CURVE). BODY PITCH (`PK_WTR_BODYPITCH`) = fundamental fine-tune, default ~50 Hz.
    - Dedicated transient synth in PARALLEL, INDEPENDENTLY enveloped: a short filtered noise/click via `noise_t` + `tpt1_lp`, own env from TRANS DECAY (`PK_WTR_TRANSDEC`, ~3-8 ms default) and TRANS COLOR (`PK_WTR_TRANSCOL`, bright 2-6 kHz LP default).
    - Sum body + transient; apply COLOR output LP; final `fx_process` stage (map fx_type→mode). Include `fx_state_t fx` + Page-1 keys handled in wtr_set_param (copy fm2's Page-1 handling), Page-2 keys as above.
    - `wtr_trigger` resets all phases, filter states, envelopes, and `fx.last/hold_ctr` (deterministic attack; KICK-13 requires trigger to fully init).
    - Self-limit body+transient sum below 1.0 (FM2 precedent: engines self-limit, clamp is a net).
    - `wtr_p2_slot_desc` emits the full `{"key":..,"name":..,"type":"float","min":0.0,"max":1.0}` objects for the 4 WTR slots (research Pattern 3: models emit full objects so ui.c stays generic); bounded, returns 0 on overflow.
    - `const kick_model_vtable_t g_wtr_vtable = { .name="WTR", .trigger=wtr_trigger, .render=wtr_render, .set_param=wtr_set_param, .set_p2=wtr_set_param, .p2_slot_desc=wtr_p2_slot_desc };`
    - `_Static_assert(sizeof(wtr_state) <= 4096, "wtr_state fits model_state");`
    NO per-sample transcendentals in render; locale-independent parse_f; no logging/alloc/file-IO.
  </action>
  <verify>
    <automated>make test-params && echo WTR_OK</automated>
  </verify>
  <done>WTR renders a clean wavetable body + separable transient; 4 P2 slots exposed; FX + trigger-reinit wired; state <=4096; param battery green.</done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: TRS model (KICK-08) — advanced wavetable + transient (909 clarity)</name>
  <read_first>src/models/fm2.c, src/models/wtr.c (Task 1, share the body pattern), src/dsp_primitives.h (noise_t, wt_read_bl, tpt1_lp, fx_process), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§TRS recipe + defaults; note CURVE here is a P2 pitch-curve morph distinct from Page-1 CURVE — key PK_TRS_CURVE)</read_first>
  <behavior>
    - TRS default render + trigger: non-silent, finite, |x|<=1, with a fast bright attack (higher spectral centroid in the first ~10 ms than WTR default → 909 clarity).
    - Each of TRS's 4 P2 params (TRANS TONE, TRANS DECAY, WT COLOR, PK_TRS_CURVE) at lo vs hi changes output.
    - Full param sweep stays finite + bounded.
  </behavior>
  <action>
    Create `src/models/trs.c` (copy wtr.c structure). Recipe (B-RESEARCH §TRS VERBATIM):
    - Like WTR but with a more ADVANCED transient synth: TRANS TONE (`PK_TRS_TONE`) morphs the click spectrum from a sharp stick/beater click ↔ white/pink noise burst (`noise_t` filtered through `tpt1_lp`); TRANS DECAY (`PK_TRS_TDEC`, ~2-6 ms default) its length.
    - WT COLOR (`PK_TRS_WTCOL`) morphs the body wavetable timbre (e.g. blend `wt_read_bl` between two tables or shift the COLOR LP).
    - `PK_TRS_CURVE` = TRS's own P2 pitch-sweep-curve morph (distinct from Page-1 CURVE — do NOT reuse PK_CURVE); default biased to the 909 side (fast sweep) for attack clarity + thick sub tail.
    - 909 voicing: emphasize a fast, bright, clear attack and a thick sub tail (Context/02 §2.7). Default TRANS TONE ~mid (click+noise mix).
    - Same skeleton: COLOR LP, `fx_process` final stage, `fx_state_t fx`, trigger fully resets phases/filters/envelopes/fx state, self-limit < 1.0.
    - `trs_p2_slot_desc` emits the 4 TRS slots as full JSON objects (bounded).
    - `const kick_model_vtable_t g_trs_vtable = { .name="TRS", ... .set_param=trs_set_param, .set_p2=trs_set_param, ... };`
    - `_Static_assert(sizeof(trs_state) <= 4096, ...)`.
    Distinctness vs WTR: WTR = clean/neutral transient; TRS = aggressive 909 attack with noise-burst option. NO per-sample transcendentals; parse_f; no logging/alloc.
  </action>
  <verify>
    <automated>make test-params && echo TRS_OK</automated>
  </verify>
  <done>TRS renders a 909-clarity kick with an advanced click/noise transient + WT-color body morph + its own pitch-curve; 4 P2 slots exposed; FX + reinit wired; state <=4096; distinct from WTR; param battery green.</done>
</task>

<task type="auto">
  <name>Task 3: Register WTR + TRS (replace their NULL slots); extend param/render batteries; wave gate</name>
  <read_first>src/models/model_registry.c (designated-initializer array from B-01), src/omega.h (MODEL_WTR/MODEL_TRS enum order), tests/test_params.c + tests/test_render.c + tests/test_distinct.c (reusable batteries from B-03), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (Pitfall 6 registry order)</read_first>
  <files>src/models/model_registry.c, tests/test_params.c, tests/test_render.c</files>
  <action>
    In `src/models/model_registry.c`: REPLACE the NULL at index MODEL_WTR with `[MODEL_WTR] = &g_wtr_vtable` and the NULL at MODEL_TRS with `[MODEL_TRS] = &g_trs_vtable`, using designated initializers (B-01 established the array with all-NULL placeholders). Concretely: add the two designated-initializer lines to the `g_models[MODEL_COUNT]` initializer and remove the corresponding `[MODEL_WTR] = ...`/`[MODEL_TRS] = ...` placeholder comment lines. The array still has exactly MODEL_COUNT entries; the remaining unimplemented slots stay NULL (untouched). Add `extern const kick_model_vtable_t g_wtr_vtable, g_trs_vtable;` to model_registry.c if not already provided by omega.h.
    These model plans are STRICTLY SEQUENTIAL by wave (B-04 → B-05 → B-06 → B-07); each plan replaces only its OWN NULL slot in model_registry.c — there is no collision. Every intermediate `make test` MUST compile and link because unimplemented slots remain NULL and dsp.c/tests guard NULL slots (B-01 contract).
    In `tests/test_params.c`: add calls to `assert_param_responsive` for WTR (keys: PK_WTR_WAVE, PK_WTR_BODYPITCH, PK_WTR_TRANSDEC, PK_WTR_TRANSCOL) and TRS (PK_TRS_TONE, PK_TRS_TDEC, PK_TRS_WTCOL, PK_TRS_CURVE), each also including the 8 Page-1 keys.
    `tests/test_render.c` already loops MODEL_COUNT (B-03) and skips NULL slots — confirm WTR/TRS now produce non-silent WAVs (they auto-cover once their NULL is replaced). No change needed beyond confirming.
    Run the full suite including cross-build + glibc gate.
  </action>
  <acceptance_criteria>
    - `grep -q '\[MODEL_WTR\] = &g_wtr_vtable' src/models/model_registry.c && grep -q '\[MODEL_TRS\] = &g_trs_vtable' src/models/model_registry.c`
    - The `sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT` assert from B-01 still present; array unchanged in length
    - `tests/test_params.c` references PK_WTR_WAVE and PK_TRS_TONE
    - `make test` exits 0 (remaining unimplemented slots still NULL, guarded); `tests/output/WTR_kick.wav` and `tests/output/TRS_kick.wav` written non-empty
    - `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes (no libmvec/_ZGV symbols, glibc <=2.35)
  </acceptance_criteria>
  <verify>
    <automated>make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so && echo WAVE_OK</automated>
  </verify>
  <done>WTR + TRS registered by replacing their NULL slots with designated initializers; array length still == MODEL_COUNT (other slots NULL); param battery covers both; per-model WAVs written; full suite + cross-build + glibc gate green.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 with WTR + TRS registered (their NULLs replaced) and covered by the param + distinctness batteries; remaining slots still NULL and guarded.
- `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes.
- Both state structs `_Static_assert` <= 4096; both route through fx_process; both fully re-init in trigger.
</verification>

<success_criteria>
- WTR (KICK-04) and TRS (KICK-08) are distinct, non-silent, bounded, param-responsive kicks with correct 4-slot Page-2 descriptors and FX chain — all automated criteria green (manual ear sign-off tracked in B-09).
</success_criteria>

<output>
After completion, create `.planning/phases/B-remaining-9-kick-models/B-04-SUMMARY.md`
</output>
</output>
