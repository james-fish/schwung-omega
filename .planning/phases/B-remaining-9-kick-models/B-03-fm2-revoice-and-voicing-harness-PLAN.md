---
phase: B-remaining-9-kick-models
plan: 03
type: execute
wave: 2
depends_on: ["B-01", "B-02"]
files_modified:
  - src/models/fm2.c
  - tests/test_render.c
  - tests/test_params.c
  - tests/test_distinct.c
  - Makefile
autonomous: true
requirements: [KICK-14, KICK-13]
# Decision coverage: this plan implements D-B03 (FM2 re-voicing) — a LOCKED decision,
# not a REQ-ID. D-B03 traceability is called out explicitly in the objective and in
# the must_haves truths below so a frontmatter/must_haves scan can see it. KICK-14
# (FX chain in a real model) and KICK-13 (voicing/switch harness) are the REQ-IDs.
must_haves:
  truths:
    - "D-B03 (FM2 re-voicing, locked decision) is delivered here: FM2 param ranges + CURVE 808<->909 shape are re-tuned to the research voicing defaults; FM2 is the reference bar for all other models"
    - "FM2 re-voiced (D-B03): default (all params 0.5) renders a usable, non-silent, bounded techno kick"
    - "FM2 PITCH default sits ~50 Hz; CURVE 808<->909 blend uses tuned time constants (909 ~10-20 ms, 808 ~150-400 ms)"
    - "All 11 FM2 params measurably change the output (lo-vs-hi render differs)"
    - "FM2 routes its final per-sample output through fx_process (KICK-14) — FX modes audibly alter the FM2 kick, output stays bounded — and configures Crush via fx_config at control rate (no powf in render)"
    - "A reusable per-model automated voicing battery exists (non-silent, param-responsive, bounded-at-extremes, distinctness) and covers FM2"
  artifacts:
    - path: "src/models/fm2.c"
      provides: "re-voiced param ranges + tuned CURVE blend + fx_process call in render"
      contains: "fx_process"
    - path: "tests/test_params.c"
      provides: "per-model P2 param lo-vs-hi render-differ battery (D-B02 automated)"
      contains: "PK_PITCH"
    - path: "tests/test_distinct.c"
      provides: "pairwise distinctness metric across all registered models' default renders"
      contains: "MODEL_COUNT"
  key_links:
    - from: "src/models/fm2.c fm2_render"
      to: "fx_process (dsp_primitives)"
      via: "final per-sample FX stage before output"
      pattern: "fx_process"
    - from: "tests/test_params.c"
      to: "each FM2 param lo/hi render delta"
      via: "render-buffer RMS/spectral delta assertion"
      pattern: "PK_"
---

<objective>
Re-voice FM2 (D-B03) — the explicit first voicing target and the reference bar for all other models — and wire the KICK-14 FX chain into FM2's render. Establish the reusable AUTOMATED voicing battery (param-responsiveness + distinctness) that every subsequent model plan reuses.

D-B03 is a LOCKED decision (not a REQ-ID) and is fully in scope here even though FM2 lives in `src/models/fm2.c` from Phase A: the user reports FM2 "sounds like FM but the parameter ranges and CURVE don't sound good yet." This plan tunes ranges + the CURVE 808<->909 shape using the research's voicing defaults verbatim. (D-B03 coverage is also surfaced in the must_haves truths above for frontmatter scanning.)

Purpose: FM2 is the reference sound; the voicing-test scaffold created here is reused by B-04..B-08. FX wiring (KICK-14) into FM2 proves the FX chain in a real model.
Output: Re-voiced fm2.c with FX, and the shared voicing test files (test_params.c, test_distinct.c) + extended test_render.c.
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
@src/models/fm2.c
@src/dsp_primitives.h
@tests/test_fm2.c
@tests/test_render.c

<interfaces>
<!-- fx_process(int mode, float x, float amt, fx_state_t *st) from B-02 (reads only
     precomputed state — no powf/sinf/expf in the render loop).
     fx_config(fx_state_t *st, int mode, float amt) from B-02 is the CONTROL-RATE
     configurator that precomputes Crush's crush_levels (the powf lives there).
     FM2 already stores fx_type (0..1) and fx_amt (0..1). Map fx_type*4 -> mode 0..4.
     FM2 must add an fx_state_t field to fm2_state (Crush needs state) and pass &st->fx;
     call fx_config from fm2_set_param whenever fx_type/fx_amt change (control rate).
     env_coeff_from_ms, wt_read, tpt1_lp reused. sizeof(fm2_state) must stay <= 4096. -->
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Re-voice FM2 param ranges + CURVE blend + wire FX chain (D-B03, KICK-14)</name>
  <read_first>src/models/fm2.c (fm2_set_param ranges, fm2_trigger CURVE coeffs, fm2_render body/click split + FX passthrough), src/dsp_primitives.h (fx_process, fx_config, fx_state_t), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§FM2 re-voicing D-B03 — the exact voicing fixes), B-CONTEXT.md (D-B03)</read_first>
  <files>src/models/fm2.c</files>
  <action>
    Apply the research's FM2 voicing fixes VERBATIM (§FM2 re-voicing):
    1. PITCH: bias default lower (techno kicks ~45-55 Hz). Map `f0 = 35 + v*(120-35)` (exponential map preferred for even sweep); default v=0.5 → ~50 Hz range center. Replace the current `30 + v*(200-30)`.
    2. Sweep: decouple from a pure `f0*4`. Make sweep a musical depth: sweep START ~300-500 Hz down to f0 (Context/03: 909 sweeps ~400-500 Hz → f_fund). Use `sweep_hz = clamp(f0*(2..6), up to ~500 Hz)`; e.g. `sweep_hz = clampf(f0 * (2.0f + curve*4.0f), 0.0f, 480.0f)` so 909-side sweeps deeper. (Recompute sweep in trigger from current curve.)
    3. CURVE (D-06): keep the dual-env output blend (already correct — blends outputs, not coeffs). Tune the two time constants: 909 fast ~10-20 ms (use 15 ms), 808 slow ~150-400 ms (use ~300 ms). Default CURVE 0.5 = balanced. Leave the lerp `pitch = p_slow + curve*(p_fast - p_slow)` intact.
    4. LENGTH: keep 50-1500 ms; use an exponential map so mid-knob is musically centered; default ~300-400 ms.
    5. FM INDEX: narrow range to 0-8 (was 0-12; tail gets buzzy); keep its own fast decay 30-60 ms so attack is bright / tail clean; default index ~2-4.
    6. OP2 WAVE: keep the fold-blend distinctness lever.
    7. Body/click split: let ATTACK scale click amplitude so default is punchy not clicky (already `fm->trs_amp = v`); keep the 0.6/0.4 body/click sum that self-limits below 1.0.
    8. Wire FX (KICK-14): add `fx_state_t fx;` to `fm2_state` (keep `_Static_assert(sizeof(fm2_state) <= 4096)`). In `fm2_set_param`, when `PK_FX_TYPE` or `PK_FX_AMT` changes, call `fx_config(&fm->fx, (int)(fm->fx_type * 4.0f + 0.5f), fm->fx_amt);` (control rate — this is where Crush's powf runs, per B-02). In `fm2_render`, after COLOR, pass the sample through `s = fx_process((int)(fm->fx_type * 4.0f + 0.5f), s, fm->fx_amt, &fm->fx);` (map fx_type 0..1 → mode 0..4). Remove the "identity passthrough" TODO. In `fm2_trigger`, reset `fm->fx.last = 0; fm->fx.hold_ctr = 0;` for a deterministic attack (preserve the precomputed `fm->fx.crush_levels`, or re-run fx_config in trigger).
    Keep all coeff/transcendental math in set_param/trigger; NO sinf/expf/tanf/powf in render (fx_process is approx/LUT-based per B-02, and Crush reads precomputed crush_levels). No logging anywhere. Keep the locale-independent parse_f.
  </action>
  <acceptance_criteria>
    - `grep -q 'fx_process' src/models/fm2.c` and no `TODO(Phase B)` identity-passthrough comment remains (`! grep -q 'identity passthrough' src/models/fm2.c`)
    - `grep -q 'fx_config' src/models/fm2.c` (Crush configured at control rate)
    - `grep -q 'fx_state_t fx' src/models/fm2.c` and `_Static_assert(sizeof(fm2_state) <= 4096` still present
    - PITCH map changed: `grep -q '35' src/models/fm2.c` region for f0 (no longer `30.0f + v * (200.0f`)
    - FM INDEX range narrowed: `! grep -q 'v \* 12.0f' src/models/fm2.c` (was 0-12)
    - No per-sample `sinf`/`expf`/`tanf`/`tanhf`/`powf` inside fm2_render
    - `make test` exits 0
  </acceptance_criteria>
  <verify>
    <automated>make test && grep -q 'fx_process' src/models/fm2.c && grep -q 'fx_state_t fx' src/models/fm2.c && echo FM2_OK</automated>
  </verify>
  <done>FM2 re-voiced per D-B03 (PITCH ~50 Hz default, tuned CURVE constants, decoupled sweep, narrowed FM index, exp LENGTH map); FX chain wired into render with per-mode selection + state reset; Crush configured via fx_config at control rate (no powf in render); state still fits 4096; suite green.</done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: Reusable per-model voicing battery — param-responsiveness (test_params.c)</name>
  <read_first>tests/test_render.c, tests/test_fm2.c, tests/mock_host.c, .planning/phases/B-remaining-9-kick-models/B-VALIDATION.md (Per-Task Verification Map, Wave 0), B-CONTEXT.md (D-B02 automated checklist)</read_first>
  <behavior>
    - Test: for FM2, for each of its 11 params (Page-1 8 + FM RATIO/FM INDEX/OP2 WAVE), rendering at param=0.1 vs param=0.9 (others at 0.5) yields render buffers whose RMS-envelope or spectral-centroid delta exceeds a small threshold — i.e. EACH param measurably changes the output (D-B02 "each param changes output"). Skip FX TYPE/AMT if desired or include them (FX now active).
    - Test: default render (all 0.5) + trigger is non-silent (RMS > 1e-4) and every sample finite + |x| <= 1.0.
    - Test: a full lo->hi sweep of every param keeps output finite + bounded (D-B02 "no divergence at extremes").
    - Structured so a model index / key-list is a parameter, making the battery reusable for models added in later plans.
  </behavior>
  <action>
    Create `tests/test_params.c` — a native harness (mock_host + real plugin) implementing a reusable function `assert_param_responsive(inst, model_index, keys[], nkeys)` that runs the behavior cases above. Drive it for FM2 (model 0) in Wave 2; the function is written to accept any model's key list so B-04..B-08 pass their own P2 keys. Use RMS-envelope delta as the "measurably changes" metric (research: "render buffer differs measurably"). Provide a small `render_default_wav` helper (or reuse test_render.c's) writing `tests/output/<model>_kick.wav`.
    Wire a `test-params` Makefile target (native compile of `tests/test_params.c tests/mock_host.c tests/wav.c tests/malloc_trap.c src/dsp.c src/ui.c src/models/*.c src/dsp_primitives.c`; run it) and add it as a `test` prerequisite.
    Also extend `tests/test_render.c` with a per-model loop over `MODEL_COUNT` (skip NULL/unregistered vtable entries so it is forward-compatible): prime defaults → trigger → render → assert non-silent + finite + <=1; write `tests/output/<name>_kick.wav` per model (Wave-0 requirement).
  </action>
  <verify>
    <automated>make test-params && echo PARAMS_OK</automated>
  </verify>
  <done>test_params.c provides a reusable param-responsiveness battery (lo-vs-hi delta, bounded sweep, non-silent default) covering FM2; test_render.c writes a per-model WAV; both forward-compatible with later models; green under make.</done>
</task>

<task type="auto">
  <name>Task 3: Pairwise distinctness metric across registered models (test_distinct.c)</name>
  <read_first>tests/test_render.c (per-model default render), tests/test_params.c (helpers from Task 2), .planning/phases/B-remaining-9-kick-models/B-VALIDATION.md ("distinct" = render buffer differs measurably), B-CONTEXT.md (D-B02 "distinct sonic character")</read_first>
  <files>tests/test_distinct.c, Makefile</files>
  <action>
    Create `tests/test_distinct.c` — for every REGISTERED model (loop `MODEL_COUNT`, skip NULL vtable entries so it is meaningful in Wave 2 with only FM2 present and automatically strengthens as models land), render the default (all params 0.5) kick and compute a feature vector (RMS-envelope over time + spectral centroid). Assert that every registered pair of models differs by more than a distinctness threshold (D-B02 "distinct character"; research metric = RMS-envelope / spectral-centroid delta). In Wave 2 with only FM2 registered the pairwise set is empty and the test trivially passes; it becomes a real gate as B-04..B-08 register models. Document this "grows-as-models-land" behavior in a comment.
    Wire a `test-distinct` Makefile target and add it as a `test` prerequisite.
  </action>
  <acceptance_criteria>
    - `tests/test_distinct.c` exists and references `MODEL_COUNT` and a distinctness threshold
    - `Makefile` has `test-distinct` and `test:` depends on it
    - `make test` exits 0
    - The distinctness loop skips unregistered (NULL) vtable entries (grep for a NULL/registered guard)
  </acceptance_criteria>
  <verify>
    <automated>make test && grep -q 'MODEL_COUNT' tests/test_distinct.c && echo DISTINCT_OK</automated>
  </verify>
  <done>test_distinct.c computes a pairwise distinctness metric over registered models; empty-trivial in Wave 2, a real gate as models land; wired into make test.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 (FM2 re-voice + FX + full voicing battery).
- FM2 render routes through fx_process; ranges retuned per D-B03; state <= 4096.
- test_params.c and test_distinct.c are reusable across models (loop MODEL_COUNT, skip unregistered).
</verification>

<success_criteria>
- FM2 is re-voiced to the reference bar (D-B03): ~50 Hz default, tuned CURVE, decoupled sweep, narrowed FM index, FX-active — all verified non-silent/bounded/param-responsive automatically (manual ear sign-off tracked in B-09).
- The automated D-B02 voicing battery (non-silent, param-responsive, bounded-at-extremes, distinctness) exists and covers FM2, ready for reuse by every model plan.
- FX chain (KICK-14) proven inside a real model.
</success_criteria>

<output>
After completion, create `.planning/phases/B-remaining-9-kick-models/B-03-SUMMARY.md`
</output>
</output>
