---
phase: B-remaining-9-kick-models
plan: 05
type: execute
wave: 4
depends_on: ["B-01", "B-02", "B-03", "B-04"]
files_modified:
  - src/models/ana.c
  - src/models/dig.c
  - src/models/model_registry.c
  - tests/test_params.c
autonomous: true
requirements: [KICK-09, KICK-07]
must_haves:
  truths:
    - "Selecting ANA produces a warm, fat 808-sub-boom kick: wavetable morph + dedicated sub-oscillator + long sub decay (KICK-09)"
    - "Selecting DIG produces a digital/retro kick: chip/bit-reduced wavetables + bit-depth character + pitch env (KICK-07)"
    - "ANA and DIG each render non-silent default, bounded across full param sweep, param-responsive, and distinct from each other and prior models"
    - "Each exposes its 4 Page-2 slots via p2_slot_desc as valid bounded JSON; routes through fx_process; re-inits on trigger"
  artifacts:
    - path: "src/models/ana.c"
      provides: "ANA engine (wavetable morph + sub-osc + sample layer), state <=4096, g_ana_vtable"
      contains: "g_ana_vtable"
    - path: "src/models/dig.c"
      provides: "DIG engine (digital wavetables + crush bit-depth + pitch env), state <=4096, g_dig_vtable"
      contains: "g_dig_vtable"
  key_links:
    - from: "src/models/model_registry.c g_models[]"
      to: "g_ana_vtable at MODEL_ANA, g_dig_vtable at MODEL_DIG"
      via: "append-only registry in exact enum order"
      pattern: "g_ana_vtable"
    - from: "dig_render"
      to: "crush() (shared bit-reducer)"
      via: "B-02 shared crush primitive"
      pattern: "crush"
---

<objective>
Implement the analog/digital wavetable family: ANA (KICK-09, the 808 sub-boom king — dedicated sub-oscillator + long decay) and DIG (KICK-07, the digital/retro kick — chip/bit-reduced waves + bit-depth as timbral character). Both reuse the B-02 band-limited wavetable read; DIG reuses the shared `crush()` bit-reducer.

Purpose: The warmest model (ANA) vs the lo-fi digital model (DIG) — two distinct wavetable characters sharing the primitive set.
Output: src/models/ana.c, src/models/dig.c, registry entries, extended param battery.
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
<!-- Copy wtr.c/fm2.c as template. From B-01: MODEL_ANA/MODEL_DIG, PK_ANA_*/PK_DIG_*,
     extern g_ana_vtable/g_dig_vtable, vtable has .set_param. From B-02: wt_read_bl,
     crush(float x, float bits), fx_process + fx_state_t, env_t, g_sine_table (sub-osc).
     Each state struct _Static_assert <= 4096. -->
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: ANA model (KICK-09) — analog wavetable morph + sub-osc + sample layer</name>
  <read_first>src/models/wtr.c, src/models/fm2.c, src/dsp_primitives.h (wt_read_bl, g_sine_table, env_t, fx_process), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§ANA recipe + defaults), src/omega.h (PK_ANA_* keys)</read_first>
  <behavior>
    - ANA default render + trigger: non-silent, finite, |x|<=1, with prominent low-frequency energy (sub-boom: more energy below ~80 Hz than DIG default).
    - Each P2 param (WAVE MORPH, SUB LEVEL, SUB DECAY, SAMPLE) at lo vs hi changes output.
    - SUB LEVEL=0 vs SUB LEVEL=1 changes low-band energy measurably (sub-osc present).
    - Long SUB DECAY (400-800 ms) render stays bounded (no runaway).
  </behavior>
  <action>
    Create `src/models/ana.c` (copy wtr.c). Recipe (B-RESEARCH §ANA VERBATIM):
    - Body = morph across warm "analog" factory wavetables via WAVE MORPH (`PK_ANA_MORPH`): interpolate between two `wt_read_bl` reads (2-table crossfade: `body = (1-morph)*wt_read_bl(waveA,0,ph) + morph*wt_read_bl(waveB,0,ph)`).
    - Dedicated SUB-OSCILLATOR: a pure low sine (`wt_read` on `g_sine_table`) at the fundamental, with SUB LEVEL (`PK_ANA_SUBLVL`, default ~0.5 prominent) and SUB DECAY (`PK_ANA_SUBDEC`, long 400-800 ms default for the 808 boom) via its own env_t. This is the 808-boom distinctness lever.
    - SAMPLE (`PK_ANA_SAMPLE`) = a sample-layer mix (for Phase B a factory `.rodata` one-shot or a synthesized layer; USR handles real user samples). Keep bounded.
    - Reuse CURVE for 808-vs-909 character, biased to 808 (slow) by default; default f0 ~45 Hz.
    - Sum body + sub + sample; COLOR LP; final `fx_process`; `fx_state_t fx`; trigger fully resets all phases/filters/envelopes/fx. Self-limit < 1.0.
    - `ana_p2_slot_desc` emits 4 ANA slots as full JSON objects (bounded).
    - `g_ana_vtable = { .name="ANA", ... .set_param=ana_set_param, .set_p2=ana_set_param, ... };` + `_Static_assert(sizeof(ana_state) <= 4096, ...)`.
    NO per-sample transcendentals; parse_f; no logging/alloc/file-IO.
  </action>
  <verify>
    <automated>make test-params && echo ANA_OK</automated>
  </verify>
  <done>ANA renders a warm 808 sub-boom kick with wavetable morph + dedicated sub-osc (long decay) + sample layer; 4 P2 slots exposed; FX + reinit wired; state <=4096; param battery green.</done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: DIG model (KICK-07) — digital wavetable + bit-depth + pitch env</name>
  <read_first>src/models/ana.c (Task 1), src/models/wtr.c, src/dsp_primitives.h (wt_read_bl, crush, fx_process), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§DIG recipe + defaults; contrast with HRD), src/omega.h (PK_DIG_* keys)</read_first>
  <behavior>
    - DIG default render + trigger: non-silent, finite, |x|<=1, with crisper highs / more upper-harmonic energy than ANA (digital character).
    - Each P2 param (WAVE IDX, SAMPLE LAYER, BIT DEPTH, PITCH ENV) at lo vs hi changes output.
    - BIT DEPTH audibly changes the timbre (low bits vs high bits render differs) and stays bounded at extreme low bit depth.
  </behavior>
  <action>
    Create `src/models/dig.c` (copy ana.c/wtr.c). Recipe (B-RESEARCH §DIG VERBATIM):
    - Body = digital-character wavetables (chip/square/additive/bit-reduced factory tables via `wt_read_bl`); WAVE IDX (`PK_DIG_WAVEIDX`) selects among them (index into NUM_WAVES).
    - BIT DEPTH (`PK_DIG_BITDEPTH`) applies the shared `crush(body, bits)` bit-reduction as a TIMBRAL control (retro digital character, always somewhat on — unlike HRD's aggressive CRUSH). Map v→bits ~ 6..14 (default ~10-12 bits = subtle crunch). Compute bits at control rate in set_param; crush() per sample is a bounded round.
    - PITCH ENV (`PK_DIG_PITCHENV`) = dedicated pitch-sweep amount (scales sweep_hz).
    - SAMPLE LAYER (`PK_DIG_SAMPLE`) = optional sample-layer mix (factory one-shot / synth).
    - COLOR LP; final `fx_process`; `fx_state_t fx`; trigger fully resets phases/filters/envelopes/fx; self-limit < 1.0.
    - `dig_p2_slot_desc` emits 4 DIG slots (bounded full JSON objects).
    - `g_dig_vtable = { .name="DIG", ... .set_param=dig_set_param, .set_p2=dig_set_param, ... };` + `_Static_assert(sizeof(dig_state) <= 4096, ...)`.
    Distinctness: DIG = lo-fi/digital-clean-crunch (bit-reduction as character, chip waves); contrast HRD = distortion/loud. NO per-sample transcendentals except the bounded crush round; parse_f; no logging/alloc.
  </action>
  <verify>
    <automated>make test-params && echo DIG_OK</automated>
  </verify>
  <done>DIG renders a digital/retro kick with chip waves + bit-depth timbre + pitch env; 4 P2 slots exposed; FX + reinit wired; state <=4096; distinct from ANA; param battery green.</done>
</task>

<task type="auto">
  <name>Task 3: Register ANA + DIG; extend param battery; wave gate</name>
  <read_first>src/models/model_registry.c, src/omega.h (MODEL_ANA/MODEL_DIG order), tests/test_params.c, .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (Pitfall 6)</read_first>
  <files>src/models/model_registry.c, tests/test_params.c</files>
  <action>
    In `src/models/model_registry.c`: add extern decls + register `&g_ana_vtable` (MODEL_ANA) and `&g_dig_vtable` (MODEL_DIG) at their exact enum indices; keep the array in enum order (Pitfall 6). model_registry.c is shared across the model plans; this plan depends on B-04 so its registry edits build on B-04's (sequential, no parallel conflict).
    In `tests/test_params.c`: add `assert_param_responsive` calls for ANA (PK_ANA_MORPH, PK_ANA_SUBLVL, PK_ANA_SUBDEC, PK_ANA_SAMPLE + 8 Page-1) and DIG (PK_DIG_WAVEIDX, PK_DIG_SAMPLE, PK_DIG_BITDEPTH, PK_DIG_PITCHENV + 8 Page-1).
    Run full suite + cross-build + glibc gate.
  </action>
  <acceptance_criteria>
    - `grep -q 'g_ana_vtable' src/models/model_registry.c && grep -q 'g_dig_vtable' src/models/model_registry.c`
    - `tests/test_params.c` references PK_ANA_MORPH and PK_DIG_BITDEPTH
    - `make test` exits 0; `tests/output/ANA_kick.wav` and `tests/output/DIG_kick.wav` non-empty
    - `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes
  </acceptance_criteria>
  <verify>
    <automated>make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so && echo WAVE_OK</automated>
  </verify>
  <done>ANA + DIG registered in enum order; param battery covers both; per-model WAVs written; full suite + cross-build + glibc gate green.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 with ANA + DIG registered and covered.
- `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes.
- Both state structs `_Static_assert` <= 4096; both route through fx_process; both fully re-init on trigger.
</verification>

<success_criteria>
- ANA (KICK-09) and DIG (KICK-07) are distinct, non-silent, bounded, param-responsive kicks with correct 4-slot Page-2 descriptors and FX chain — automated criteria green (manual sign-off in B-09).
</success_criteria>

<output>
After completion, create `.planning/phases/B-remaining-9-kick-models/B-05-SUMMARY.md`
</output>
