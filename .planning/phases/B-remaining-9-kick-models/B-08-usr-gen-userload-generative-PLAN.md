---
phase: B-remaining-9-kick-models
plan: 08
type: execute
wave: 7
depends_on: ["B-01", "B-02", "B-03", "B-07"]
files_modified:
  - src/omega.h
  - src/dsp.c
  - src/models/usr.c
  - src/models/gen.c
  - src/models/model_registry.c
  - tests/test_gen.c
  - tests/test_params.c
  - tests/fixtures/user_kick.wav
  - Makefile
autonomous: true
requirements: [KICK-10, KICK-11]
must_haves:
  truths:
    - "USR loads a custom WAV/wavetable from module_dir/user/ at create_instance with ZERO file I/O on render/set_param/on_midi/get_param; falls back to a built-in wavetable when no user file is present (non-silent)"
    - "USR's user-sample buffer lives in a pre-sized region of the SINGLE instance calloc (not model_state[4096], not a per-file malloc)"
    - "GEN renders an audible generative kick: xorshift PRNG (seeded from SEED) + scale-quantize + Euclidean density gating, self-clocking for offline audition; same seed -> byte-identical render (determinism)"
    - "GEN Phase-B scope is the generative ENGINE only; transport-sync (get_beat_position) + full Groove Page 2 UI are explicitly deferred to Phase C"
    - "USR and GEN each render non-silent, bounded, param-responsive, distinct; expose their Page-2 slots; route through fx_process; re-init on trigger"
  artifacts:
    - path: "src/omega.h"
      provides: "USR sample/wavetable buffer added to bohm_instance (grows the single calloc; hard-capped)"
      contains: "usr_"
    - path: "src/dsp.c"
      provides: "off-render file enumeration/load in create_instance (module_dir/user/) with graceful fallback"
      contains: "module_dir"
    - path: "src/models/usr.c"
      provides: "USR playback engine (sample + user wavetable morph), g_usr_vtable"
      contains: "g_usr_vtable"
    - path: "src/models/gen.c"
      provides: "GEN generative engine (prng + scale_quantize + Euclidean density, self-clocking), g_gen_vtable"
      contains: "g_gen_vtable"
    - path: "tests/test_gen.c"
      provides: "GEN determinism: same seed -> identical hash; different seed -> differs (KICK-11)"
      contains: "PK_GEN_SEED"
  key_links:
    - from: "src/dsp.c omega_create"
      to: "module_dir/user/ file read (bounded, one-time, off render)"
      via: "create_instance file load into the pre-sized instance buffer"
      pattern: "module_dir"
    - from: "gen_render"
      to: "prng_t + scale_quantize + Euclidean density gate"
      via: "B-02 PRNG/scale primitives; deterministic from SEED"
      pattern: "prng_"
---

<objective>
Implement the last two models: USR (KICK-10, user WAV + user wavetable loaded off the render loop at create_instance) and GEN (KICK-11, the generative kick — PRNG + scale-quantize + Euclidean density). Both are the trickiest for different reasons: USR needs a file read that respects RT-safety (grows the single calloc; buffer does NOT fit in model_state[4096]); GEN must clearly scope Phase B = the generative engine only (transport-sync + Groove Page 2 UI are Phase C).

Purpose: Complete all 10 models. USR proves the off-thread file-load pattern; GEN proves deterministic generative synthesis.
Output: USR buffer in bohm_instance (omega.h), create_instance file load (dsp.c), usr.c, gen.c, registry, GEN determinism test, USR fixture WAV.
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
@.planning/phases/B-remaining-9-kick-models/B-CONTEXT.md
@src/omega.h
@src/dsp.c
@src/models/fm2.c
@src/dsp_primitives.h
@src/models/model_registry.c
@tests/wav.h
@tests/mock_host.c

<interfaces>
<!-- From B-01: MODEL_USR/MODEL_GEN, PK_USR_*/PK_GEN_*, extern g_usr_vtable/g_gen_vtable,
     vtable has .set_param. From B-02: prng_t + prng_seed + prng_next_f, scale_quantize +
     NUM_SCALES + g_scales, wt_read_bl/wt_read, g_sine_table, env_t, fx_process + fx_state_t.
     tests/wav.h provides a WAV reader/writer (used by the harness). create_instance in
     dsp.c receives (const char *module_dir, const char *json_defaults) — currently ignored.
     Each state struct _Static_assert <= 4096; USR's large buffer lives in bohm_instance,
     NOT model_state. bohm_instance is zero-init by the single calloc. -->
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Grow the instance for USR + off-render file load in create_instance (KICK-10)</name>
  <read_first>src/omega.h (bohm_instance struct + its _Static_assert < 800KB), src/dsp.c (omega_create single calloc, module_dir param), tests/wav.h + tests/wav.c (WAV read format), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§USR recipe + Pitfall 4 + Open Question 2: bounded one-time read in create_instance with hard cap + fallback), CLAUDE.md (single calloc, zero file I/O on audio thread), B-VALIDATION.md (fixtures/user_kick.wav)</read_first>
  <files>src/omega.h, src/dsp.c, tests/fixtures/user_kick.wav, Makefile</files>
  <action>
    RT-safety contract (B-RESEARCH Open Question 2, recommendation 1): a BOUNDED, one-time file read in create_instance is acceptable (that is where the single calloc happens, off the hot render loop); render/set_param/on_midi/get_param stay file-I/O-free ABSOLUTELY. No host->log anywhere.
    1. In `src/omega.h`: add a pre-sized USR region to `bohm_instance` (grows the single calloc — still ONE allocation; Pitfall 4). Hard cap it: e.g.
       `float usr_wavetable[2049];   /* user single-cycle wavetable + guard (~8 KB) */`
       `float usr_sample[44100];     /* up to 1 s user one-shot @44.1k (~176 KB) */`
       `int   usr_sample_len; bool usr_loaded;`
       Keep the existing `_Static_assert(sizeof(struct bohm_instance) < 800000, ...)` passing (it will — ~184 KB added, well under 800 KB).
    2. In `src/dsp.c` `omega_create`: USE the `module_dir` param (currently ignored). After the calloc + priming, if `module_dir` is non-NULL, attempt to enumerate/read a bounded user file from `module_dir/user/` (e.g. a fixed filename like `user/kick.wav` and/or `user/wavetable.raw`) into the pre-sized `usr_*` buffers using a BOUNDED read (cap bytes read to the buffer size; `fopen`/`fread`/`fclose` are permitted HERE in create_instance only). On any failure or absence: leave `usr_loaded=false` and rely on the built-in fallback wavetable (usr.c handles fallback so USR is non-silent). This is the ONLY file I/O in the module and it is one-time at create.
    3. Create `tests/fixtures/user_kick.wav` — a small valid WAV (a synthesized short kick or a few cycles) using the existing `tests/wav.c` writer, generated by a tiny Makefile step OR committed as a fixture. Add a Makefile step that ensures the fixture exists before `make test` (generate it with a small host program using wav.c if not committed).
    Keep the malloc-trap semantics intact (create_instance is NOT the audio render path; the trap guards render_block). Confirm the file read does not run inside render.
  </action>
  <acceptance_criteria>
    - `src/omega.h` bohm_instance contains `usr_` buffer fields and `_Static_assert(sizeof(struct bohm_instance) < 800000` still present and passing (build succeeds)
    - `src/dsp.c` omega_create references `module_dir` and contains a bounded file read (`fread` present in create only; `! grep -q 'fread\|fopen' <(sed -n '/omega_render_block/,/^}/p' src/dsp.c)` — no file I/O in render)
    - `tests/fixtures/user_kick.wav` exists and is a non-empty valid WAV
    - No `host->log`/`g_host->log` added in dsp.c
    - `make test` exits 0
  </acceptance_criteria>
  <verify>
    <automated>make test && test -s tests/fixtures/user_kick.wav && grep -q 'module_dir' src/dsp.c && echo USRLOAD_OK</automated>
  </verify>
  <done>bohm_instance grown by a hard-capped USR buffer (single calloc, <800KB); create_instance does a bounded one-time read from module_dir/user/ with graceful fallback; no file I/O outside create_instance; fixture WAV present; suite green.</done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: USR model (KICK-10) + GEN model (KICK-11) engines</name>
  <read_first>src/models/fm2.c + src/models/wtr.c (template), src/dsp_primitives.h (prng_t, scale_quantize, wt_read, wt_read_bl, fx_process, env_t), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§USR recipe + §GEN recipe + the CRITICAL Phase-B-vs-Phase-C scope split, Open Question 3), src/omega.h (usr_* buffer, PK_USR_*/PK_GEN_* keys)</read_first>
  <behavior>
    - USR default render + trigger (NO user file → fallback built-in wavetable): non-silent, finite, |x|<=1. With the fixture WAV loaded: plays the sample; SAMPLE SELECT/WT MORPH/LAYER VOL/PITCH ENV each change output; full sweep bounded.
    - GEN default render + trigger: non-silent, finite, |x|<=1, self-clocking (pitch changes across successive triggers/steps — evolving).
    - GEN determinism: same SEED → byte-identical render buffer (hash equal); different SEED → different buffer.
    - GEN density gates steps (low DENSITY → fewer hits than high DENSITY over a fixed window).
  </behavior>
  <action>
    Create `src/models/usr.c` (copy wtr.c). Recipe (B-RESEARCH §USR VERBATIM):
    - Reads from the instance's `usr_wavetable`/`usr_sample` buffers (populated off-render in Task 1). SAMPLE SELECT (`PK_USR_SAMPLE`) picks a loaded file/slot; WT MORPH (`PK_USR_WTMORPH`) morphs the user wavetable; LAYER VOL (`PK_USR_LAYERVOL`) mixes sample vs wavetable; PITCH ENV (`PK_USR_PITCHENV`) sweeps.
    - FALLBACK: when `inst->usr_loaded == false`, synth a built-in wavetable body (e.g. `wt_read_bl(0,0,ph)` or a sine) so USR is non-silent with no user file (D-B02 non-silent default).
    - COLOR LP; final `fx_process`; `fx_state_t fx`; trigger fully resets phases/filters/envelopes/fx (sample playback position reset). Self-limit < 1.0.
    - `usr_p2_slot_desc` emits 4 USR slots (bounded). `g_usr_vtable = { .name="USR", ... .set_param=usr_set_param, .set_p2=usr_set_param, ... };` `_Static_assert(sizeof(usr_state) <= 4096)` (state holds only playback cursors/coeffs; the big buffer is in bohm_instance).
    Create `src/models/gen.c` (copy wtr.c body + add generative sequencer). Recipe (B-RESEARCH §GEN VERBATIM) — PHASE B SCOPE = the generative ENGINE ONLY:
    - A wavetable/synth kick body (reuse WTR/ANA body). Driven by a generative pitch/velocity sequence: `prng_t` seeded from SEED (`PK_GEN_SEED`) produces a repeatable pitch sequence, scale-quantized via `scale_quantize` (`PK_GEN_SCALE` selects scale, or free-freq mode). Euclidean density gating (`PK_GEN_DENSITY`) selects which steps fire.
    - SELF-CLOCKING for Phase B: free-running at a default internal step rate (e.g. advance the step every N samples) so it is audible and auditionable offline and on-device NOW. Reseed the PRNG from SEED on change → same seed reproduces the sequence (determinism, KICK-11).
    - EXPLICIT scope comment at the top of gen.c: "Phase B = generative engine (PRNG + scale-quantize + Euclidean density + self-clocking). Transport-sync via get_beat_position and the full Groove Page 2 UI (SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY) are Phase C (GRV-02/GRV-04). Do NOT pull Phase C work in." (Open Question 3.)
    - Minimal Phase-B Page-2 slots: SEED, SCALE, DENSITY (+ body controls as needed). `gen_p2_slot_desc` emits these (bounded). COLOR LP; final `fx_process`; `fx_state_t fx`; trigger resets phases/filters/envelopes/fx but PRESERVES the seeded sequence determinism. Self-limit < 1.0.
    - `g_gen_vtable = { .name="GEN", ... .set_param=gen_set_param, .set_p2=gen_set_param, ... };` `_Static_assert(sizeof(gen_state) <= 4096)`.
    NO per-sample transcendentals; NO rand() (use prng_t); parse_f; no logging/alloc/file-IO in either model.
  </action>
  <verify>
    <automated>make test-params && echo USRGEN_OK</automated>
  </verify>
  <done>USR plays user buffers with a non-silent built-in fallback (4 P2 slots); GEN is a deterministic self-clocking generative engine (PRNG+scale+Euclidean, Phase-B scope commented, 3+ P2 slots); both route through fx_process, re-init on trigger, state <=4096; param battery green.</done>
</task>

<task type="auto">
  <name>Task 3: Register USR + GEN; GEN determinism test; update Model enum options; final wave gate</name>
  <read_first>src/models/model_registry.c, src/omega.h (MODEL_USR/MODEL_GEN order), tests/test_params.c, tests/test_render.c, src/ui.c (root Model enum "options" list — currently ["FM2"]), .planning/phases/B-remaining-9-kick-models/B-VALIDATION.md (GEN determinism Wave 0), B-RESEARCH.md (Pitfall 6, §Pattern 3 update Model enum options)</read_first>
  <files>src/models/model_registry.c, tests/test_gen.c, tests/test_params.c, src/ui.c, Makefile</files>
  <action>
    In `src/models/model_registry.c`: add extern decls + register `&g_usr_vtable` (MODEL_USR) and `&g_gen_vtable` (MODEL_GEN) at their exact enum indices. This is the LAST model plan, so after this edit `g_models[]` MUST list ALL 10 vtables in exact enum order (FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN). Add `_Static_assert(sizeof(g_models)/sizeof(g_models[0]) == MODEL_COUNT, "registry has all models")` (Pitfall 6) so a missing entry fails at compile time.
    In `src/ui.c`: update the root-level Model enum `"options":["FM2"]` to list all 10 names in enum order: `["FM2","FM4","WTR","PHY","HRD","DIG","TRS","ANA","USR","GEN"]` (research §Pattern 3). (Full per-model Page-2 splice from p2_slot_desc is B-09's job; here just fix the enum options so the MODEL selector shows all 10.)
    Create `tests/test_gen.c` (KICK-11 determinism): render GEN with SEED=X twice → assert byte-identical (or hash-equal) buffers; render with SEED=Y → assert differs from SEED=X; render at low vs high DENSITY → assert hit-count differs. Wire a `test-gen` Makefile target and add it as a `test` prerequisite.
    In `tests/test_params.c`: add `assert_param_responsive` for USR (PK_USR_SAMPLE, PK_USR_WTMORPH, PK_USR_LAYERVOL, PK_USR_PITCHENV + 8 Page-1) and GEN (PK_GEN_SEED, PK_GEN_SCALE, PK_GEN_DENSITY + 8 Page-1).
    Run the FULL suite: `make test` (all batteries incl. distinctness now covering all 10) + `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so`.
  </action>
  <acceptance_criteria>
    - `grep -q 'g_usr_vtable' src/models/model_registry.c && grep -q 'g_gen_vtable' src/models/model_registry.c`
    - `grep -q 'sizeof(g_models)/sizeof(g_models\[0\]) == MODEL_COUNT' src/models/model_registry.c`
    - `src/ui.c` Model options list contains all 10 names (`grep -q '"GEN"' src/ui.c` and `grep -q '"FM4"' src/ui.c`)
    - `tests/test_gen.c` exists, references `PK_GEN_SEED`, and asserts seed-determinism
    - `make test` exits 0; all 10 `tests/output/<MODEL>_kick.wav` files non-empty; distinctness test now covers all 10 pairs
    - `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes
  </acceptance_criteria>
  <verify>
    <automated>make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so && grep -q '"GEN"' src/ui.c && echo ALLMODELS_OK</automated>
  </verify>
  <done>All 10 models registered (compile-time count assert); Model enum lists 10 names; GEN determinism test green; param + distinctness batteries cover all 10; full suite + cross-build + glibc gate green.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 with all 10 models registered and covered (param, distinctness, switch, gen-determinism, fx batteries).
- `make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` passes.
- USR file I/O only in create_instance; bohm_instance < 800 KB; GEN deterministic + Phase-B-scoped; registry compile-time count assert present.
</verification>

<success_criteria>
- USR (KICK-10) loads user content off-render with a non-silent fallback; GEN (KICK-11) is a deterministic self-clocking generative engine scoped to Phase B (transport/Groove Page 2 deferred to Phase C).
- All 10 models are registered, distinct, non-silent, bounded, and param-responsive — every automated D-B02 criterion green across the full battery (manual ear sign-off tracked in B-09).
</success_criteria>

<output>
After completion, create `.planning/phases/B-remaining-9-kick-models/B-08-SUMMARY.md`
</output>
