---
phase: C-groove-rumble-engine
plan: 03
type: execute
wave: 3
depends_on: [C-02]
files_modified:
  - src/omega.h
  - src/models/gen.c
  - src/dsp.c
  - src/ui.c
  - tests/test_switch.c
  - tests/test_gen.c
  - tests/test_groove.c
autonomous: true
requirements: [GRV-04]
must_haves:
  truths:
    - "With GEN active, its generative sequence clocks to the project tempo (never the ~130-BPM GEN_STEP_FRAMES hardcode)"
    - "Groove Page 2 (SEED, SCALE, SEQ LEN, LPF FREQ, LPF POLE, DENSITY) controls the GEN sequence; same SEED → byte-identical render"
    - "Groove Page 2 appears in ui_hierarchy only when model==GEN and is hidden for all other models"
    - "LPF POLE toggles a 2-pole vs 4-pole (1 vs 2 cascaded TPT stages) low-pass on the GEN body"
  artifacts:
    - path: "src/models/gen.c"
      provides: "Transport-clocked GEN (samples_per_16th from the groove tempo clock), runtime SEQ LEN, LPF FREQ + 2/4-pole cascade; Groove Page 2 slot descriptor"
      contains: "samples_per_16th"
    - path: "src/omega.h"
      provides: "PK_GEN_SEQLEN / PK_GEN_LPFFREQ / PK_GEN_LPFPOLE macros"
      contains: "PK_GEN_SEQLEN"
    - path: "src/ui.c"
      provides: "Conditional groove2 level emitted iff inst->model == MODEL_GEN"
      contains: "MODEL_GEN"
    - path: "src/dsp.c"
      provides: "GEN bypasses the kick-fed multitap; groove_update_tempo still runs to feed GEN its samples_per_16th"
      contains: "MODEL_GEN"
  key_links:
    - from: "src/models/gen.c render/step-advance"
      to: "the groove tempo clock samples_per_16th"
      via: "step_ctr reload uses the transport interval, not GEN_STEP_FRAMES"
      pattern: "samples_per_16th"
    - from: "src/ui.c omega_build_ui"
      to: "UI_GROOVE2 fragment"
      via: "append gated on inst->model == MODEL_GEN"
      pattern: "MODEL_GEN"
    - from: "src/dsp.c omega_render_block"
      to: "GEN model output"
      via: "if model==MODEL_GEN skip the multitap (GEN output IS the rumble) but still call groove_update_tempo"
      pattern: "MODEL_GEN"
---

<objective>
Wire the Phase-B GEN generative engine to the transport clock and the new Groove Page 2, and make Groove Page 2 appear only for GEN. Replace gen.c's hardcoded ~130-BPM `GEN_STEP_FRAMES` self-clock with the transport-derived `samples_per_16th` (the GEN analogue of the DC-02 bug), add runtime SEQ LEN + the LPF FREQ / 2-4-pole cascade, and conditionally emit the groove2 level in ui.c. Completes GRV-04.

Purpose: DC-05 — when GEN is active its generative scale-quantized sequence drives the Groove voice. All six GRV-04 controls (SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY) live on Groove Page 2, hidden for non-GEN models. Determinism (same SEED → byte-identical) must survive the transport clock.
Output: transport-clocked gen.c with SEQ LEN + LPF cascade + a Groove Page 2 slot descriptor; three new PK_GEN_* macros; conditional groove2 emission in ui.c; extended test_switch (JSON gating), test_gen (transport determinism), test_groove (GEN-clock + LPF-pole).
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/C-groove-rumble-engine/C-CONTEXT.md
@.planning/phases/C-groove-rumble-engine/C-RESEARCH.md
@.planning/phases/C-groove-rumble-engine/C-VALIDATION.md

<interfaces>
<!-- gen.c today (src/models/gen.c) — the state + the hardcode to replace. -->
```c
#define GEN_STEP_FRAMES 5088   /* ~130 BPM 16th — the latent bug to REMOVE */
#define GEN_SEQ_LEN     16     /* becomes a runtime g->seq_len */
typedef struct gen_state {
    /* ... body osc, envelopes ... */
    uint64_t seed; prng_t rng; int scale; float density; int npulses;
    int step; int step_ctr; int degree;
    tpt1_t color_lp; float color_g;
    float fx_type, fx_amt; fx_state_t fx;
} gen_state;
_Static_assert(sizeof(gen_state) <= 4096, "gen_state fits model_state");
/* render loop: if (g->step_ctr <= 0){ step=(step+1)%GEN_SEQ_LEN; gen_step_pitch;
   if(euclid_hit(step,npulses,GEN_SEQ_LEN)) gen_fire_step; step_ctr=GEN_STEP_FRAMES;} step_ctr--; */
/* body tail: s = tpt1_lp(&g->color_lp, s, g->color_g); s = fx_process(...); out=s; */
```
gen.c has parse_f, clampf, tpt_g_from_hz, euclid_hit already defined (reuse).

<!-- groove tempo clock (src/groove.h from C-02). -->
```c
typedef struct groove_state { /* ... */ int samples_per_16th; /* ... */ } groove_state_t;
void groove_update_tempo(groove_state_t *g, const struct host_api_v1 *host, int frames);
```
bohm_instance carries `groove_state_t groove;` — gen.c can read `inst->groove.samples_per_16th`.

<!-- ui.c splice seam (src/ui.c). -->
```c
static const char UI_KICK1[] = "...";        /* pattern: static .rodata fragment */
static void ui_append(char *buf,int buf_len,int *off,const char *src,int len);
/* omega_build_ui: appends UI_OPEN, UI_ROOT, UI_KICK1, kick2 (prefix+splice+FX), UI_CLOSE. */
```

<!-- dsp.c render seam (src/dsp.c, after C-02). -->
```c
groove_update_tempo(&inst->groove, g_host, frames);
for (int n=0;n<frames;n++){ float gl,gr; groove_tick(&inst->groove,l[n],r[n],&gl,&gr); l[n]+=gl; r[n]+=gr; }
/* <<< PHASE D INSERTION POINT >>> */
```
</interfaces>

<lpf_cascade>
<!-- GEN Page-2 LPF cascade (C-RESEARCH §Pattern 4, DC-06). 1 stage = 2-pole path, 2 = 4-pole. -->
```c
/* new gen_state fields: tpt1_t lpf1, lpf2; float lpf_g; int lpf_pole; int seq_len; int use_transport; */
/* body render tail, replacing the single color_lp line: */
float s = body * amp * 0.85f;
s = tpt1_lp(&g->lpf1, s, g->lpf_g);                  /* stage 1 (always) */
if (g->lpf_pole) s = tpt1_lp(&g->lpf2, s, g->lpf_g); /* stage 2 => 4-pole (DC-06) */
/* keep the existing color_lp + fx_process AFTER, or fold color into lpf — discretion; simplest: keep color_lp then the LPF cascade, then fx. */
```
</lpf_cascade>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Transport-clock gen.c + runtime SEQ LEN + LPF cascade; add PK_GEN_* macros</name>
  <files>src/models/gen.c, src/omega.h</files>
  <read_first>
    - src/models/gen.c (FULL — the GEN_STEP_FRAMES/GEN_SEQ_LEN hardcodes, gen_state, gen_render step-advance, gen_set_param, gen_trigger, gen_p2_slot_desc)
    - src/groove.h (samples_per_16th field on groove_state_t)
    - src/omega.h lines 226-229 (existing PK_GEN_SEED/SCALE/DENSITY) — append the three new keys nearby
    - The <lpf_cascade> block above (VERBATIM 1/2-stage tpt1_lp cascade)
    - C-RESEARCH.md §Pattern 4 + §Pitfall 1 (the GEN_STEP_FRAMES hardcode is the DC-02 analogue) + Open Q3 (key naming)
    - C-CONTEXT.md DC-05, DC-06
  </read_first>
  <behavior>
    - GEN clocks to the transport: with the groove tempo clock driven at 120 vs 174 BPM, the number of GEN steps fired over a fixed render window DIFFERS (faster tempo → more steps). The GEN_STEP_FRAMES constant no longer sets the live interval.
    - Determinism preserved: same SEED + same driven BPM rendered twice → BYTE-IDENTICAL buffer.
    - SEQ LEN control changes the Euclidean pattern length: SEQ LEN low vs high produces a different hit pattern (different output over a window).
    - LPF POLE toggle: with LPF FREQ low, 4-pole (2 stages) attenuates more than 2-pole (1 stage) — the two outputs differ measurably.
    - Unknown/absent Page-2 priming still yields an audible non-silent GEN (defaults hold).
  </behavior>
  <action>
    In src/omega.h, add three macros next to the existing GEN keys (lowercase strings, unique):
    ```c
    #define PK_GEN_SEQLEN  "gen_seqlen"
    #define PK_GEN_LPFFREQ "gen_lpffreq"
    #define PK_GEN_LPFPOLE "gen_lpfpole"
    ```

    In src/models/gen.c:
    1. Add gen_state fields: `int seq_len;` (runtime pattern length, replaces GEN_SEQ_LEN), `tpt1_t lpf1, lpf2; float lpf_g; int lpf_pole;`. Keep the `_Static_assert(sizeof(gen_state) <= 4096)` — verify it still holds (a few ints + two tpt1_t floats are tiny).

    2. TRANSPORT CLOCK — kill GEN_STEP_FRAMES as the live interval. gen.c cannot see the groove struct directly through the vtable render signature `(bohm_instance_t *inst, ...)` — but it CAN read `inst->groove.samples_per_16th` because bohm_instance carries the groove voice (C-02). In gen_render, source the step interval from `int step_frames = inst->groove.samples_per_16th; if (step_frames < 1) step_frames = GEN_STEP_FRAMES;` (GEN_STEP_FRAMES survives ONLY as a last-resort default when the tempo clock has not initialised, mirroring the groove 120 fallback — NOT as the live value). Replace every `g->step_ctr = GEN_STEP_FRAMES;` reload (in gen_render AND gen_trigger) with `g->step_ctr = step_frames;` (in gen_trigger, read `inst->groove.samples_per_16th` the same way with the guard). Add a comment: "Transport clock (GRV-02/DC-05): step interval comes from the groove tempo clock, NOT the GEN_STEP_FRAMES hardcode (the GEN analogue of the DC-02 bug)."

    3. RUNTIME SEQ LEN — replace `GEN_SEQ_LEN` uses in gen_render/gen_set_param/gen_trigger with `g->seq_len` (clamp 1..16). `step = (step+1) % g->seq_len;` and `euclid_hit(step, npulses, g->seq_len)` and the DENSITY→npulses map uses `g->seq_len`. Default `g->seq_len = 16` in gen_trigger if unset.

    4. GROOVE PAGE 2 params in gen_set_param — add branches:
       - PK_GEN_SEQLEN → `g->seq_len = 1 + (int)(v * 15.0f + 0.5f);` clamp 1..16; recompute npulses from density against the new seq_len.
       - PK_GEN_LPFFREQ → `float fc = 30.0f + v*(18000.0f-30.0f); g->lpf_g = tpt_g_from_hz(fc);` (sub-bass LPF, HPN spec).
       - PK_GEN_LPFPOLE → `g->lpf_pole = (v >= 0.5f);` (0 = 2-pole/1 stage, 1 = 4-pole/2 stages).
       Keep the existing SEED/SCALE/DENSITY branches unchanged.

    5. LPF CASCADE in gen_render — insert the <lpf_cascade> per-sample body: after the body/amp/color_lp compute, run stage 1 always and stage 2 iff g->lpf_pole, then fx_process. Reset `g->lpf1.s = g->lpf2.s = 0.0f;` in gen_trigger.

    6. DEFAULTS in gen_trigger: seed lpf_g to a wide-open cutoff (e.g. tpt_g_from_hz(12000.0f)) and seq_len=16 and lpf_pole=0 if unset, so GEN is non-silent/musical out of the box.

    RT-safety: all powf/tanf stay in set_param/trigger (control rate); gen_render adds only the tpt1_lp cascade (no new transcendental).
  </action>
  <acceptance_criteria>
    - omega.h: `grep -q "PK_GEN_SEQLEN" src/omega.h` and `PK_GEN_LPFFREQ` and `PK_GEN_LPFPOLE` present.
    - gen.c reads the transport interval: `grep -q "inst->groove.samples_per_16th" src/models/gen.c`
    - GEN_STEP_FRAMES is no longer the live reload — it survives only as a guarded fallback: `grep -q "samples_per_16th" src/models/gen.c` AND every `step_ctr =` assignment uses `step_frames`/`samples_per_16th` (verify by reading; `grep -c "GEN_STEP_FRAMES" src/models/gen.c` shows it only in the #define + the one fallback guard, not in the reload paths).
    - Runtime SEQ LEN: `grep -q "g->seq_len" src/models/gen.c` and the render step-advance uses it (`grep -q "% g->seq_len" src/models/gen.c`).
    - LPF cascade: `grep -q "g->lpf1" src/models/gen.c` and `grep -q "g->lpf2" src/models/gen.c` and `grep -q "g->lpf_pole" src/models/gen.c`.
    - The gen_state size assert still holds: `cc -std=gnu11 -O2 -Isrc -c src/models/gen.c -o /tmp/gen.o` compiles.
    - No new per-sample transcendental in gen_render (tanf/powf only in set_param/trigger): verify by reading the render loop.
  </acceptance_criteria>
  <verify>
    <automated>cc -std=gnu11 -O2 -Isrc -c src/models/gen.c -o /tmp/gen.o && grep -q "inst->groove.samples_per_16th" src/models/gen.c && grep -q "g->lpf2" src/models/gen.c && grep -q "PK_GEN_SEQLEN" src/omega.h</automated>
  </verify>
  <done>GEN clocks off the groove transport interval (GEN_STEP_FRAMES demoted to a guarded fallback), SEQ LEN is a runtime control, the LPF FREQ / 2-4-pole cascade is wired, and the three PK_GEN_* macros exist. gen.c compiles and its state still fits model_state.</done>
</task>

<task type="auto">
  <name>Task 2: GEN bypass in dsp.c + Groove Page 2 slot descriptor + conditional groove2 in ui.c</name>
  <files>src/models/gen.c, src/dsp.c, src/ui.c</files>
  <read_first>
    - src/dsp.c (C-02's render stage — the groove multitap loop to gate on model!=MODEL_GEN)
    - src/models/gen.c gen_p2_slot_desc (lines ~278-289 — the current Phase-B SEED/SCALE/DENSITY interior; per DC-05/Open Q2 MOVE these to a Groove Page 2 descriptor and make Kick Page 2 emit no GEN interior)
    - src/ui.c (FULL — UI_KICK1/UI_ROOT fragment pattern, ui_append, omega_build_ui append sequence, UI_CLOSE)
    - tests/test_switch.c lines 43-51 (g_expected_slots per-model counts) and 74-130 (assert_p2_json_valid) — GEN's Kick-Page-2 count changes from 3 to 0-interior
    - C-CONTEXT.md DC-05 + C-RESEARCH.md §Pattern 5 + Open Q2 (move SEED/SCALE/DENSITY to Groove Page 2; GEN Kick Page 2 becomes FX-only)
  </read_first>
  <action>
    Per DC-05 / Open Q2, all six GRV-04 controls live on Groove Page 2 for GEN; GEN's Kick Page 2 emits no model interior (just FX TYPE/AMT, which ui.c's splice already handles when slot_len==0).

    1. gen.c descriptor change: make `gen_p2_slot_desc` (the KICK Page 2 descriptor) return 0 / empty for GEN (so ui.c drops the leading FX comma and Kick Page 2 shows only FX TYPE/AMT). Add a SEPARATE static string for the GROOVE Page 2 interior — but ui.c owns page structure, so the cleanest split (matching how kick2 is built) is: define the groove2 params as a static .rodata fragment IN ui.c (Task step 3 below), listing all six keys. gen.c's job here is only to (a) still HANDLE all six keys in gen_set_param (SEED/SCALE/DENSITY already handled in Task 1's file; SEQLEN/LPFFREQ/LPFPOLE added in Task 1) and (b) stop advertising SEED/SCALE/DENSITY on Kick Page 2 by returning an empty interior from gen_p2_slot_desc. Implement gen_p2_slot_desc to `return 0;` (or write "" and return 0) with a comment: "GEN exposes its controls on Groove Page 2 (GRV-04), not Kick Page 2 (DC-05)."

    2. dsp.c GEN bypass: gate the C-02 multitap loop so GEN's own output IS the rumble (DC-05). Change the render stage to:
    ```c
    groove_update_tempo(&inst->groove, g_host, frames);   /* always: feeds GEN its samples_per_16th too */
    if (inst->model != MODEL_GEN) {
        for (int n = 0; n < frames; n++) {
            float gl, gr;
            groove_tick(&inst->groove, l[n], r[n], &gl, &gr);
            l[n] += gl;  r[n] += gr;
        }
    }
    /* <<< PHASE D INSERTION POINT ... >>> */
    ```
    (groove_update_tempo MUST still run for GEN so gen.c reads a live samples_per_16th; only the kick-fed multitap sum is skipped.)

    3. ui.c conditional Groove Page 2. Add two static .rodata fragments following the UI_KICK1 pattern:
    ```c
    static const char UI_GROOVE1[] =
      "\"groove1\":{\"name\":\"Groove 1\",\"params\":["
        "{\"key\":\"" PK_GRV_VOL    "\",\"name\":\"VOL\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GRV_LENGTH "\",\"name\":\"LENGTH\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GRV_COLOR  "\",\"name\":\"COLOR\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GRV_TAP1   "\",\"name\":\"TAP1\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GRV_TAP2   "\",\"name\":\"TAP2\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GRV_TAP3   "\",\"name\":\"TAP3\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GRV_TAP4   "\",\"name\":\"TAP4\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GRV_MONO   "\",\"name\":\"MONO\",\"type\":\"float\",\"min\":0.0,\"max\":1.0}"
      "],\"knobs\":[\"" PK_GRV_VOL "\",\"" PK_GRV_LENGTH "\",\"" PK_GRV_COLOR "\",\"" PK_GRV_TAP1
        "\",\"" PK_GRV_TAP2 "\",\"" PK_GRV_TAP3 "\",\"" PK_GRV_TAP4 "\",\"" PK_GRV_MONO "\"]},";
    static const char UI_GROOVE2[] =
      "\"groove2\":{\"name\":\"Groove 2\",\"params\":["
        "{\"key\":\"" PK_GEN_SEED    "\",\"name\":\"SEED\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GEN_SCALE   "\",\"name\":\"SCALE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GEN_SEQLEN  "\",\"name\":\"SEQ LEN\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GEN_LPFFREQ "\",\"name\":\"LPF FREQ\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GEN_LPFPOLE "\",\"name\":\"LPF POLE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_GEN_DENSITY "\",\"name\":\"DENSITY\",\"type\":\"float\",\"min\":0.0,\"max\":1.0}"
      "],\"knobs\":[\"" PK_GEN_SEED "\",\"" PK_GEN_SCALE "\",\"" PK_GEN_SEQLEN "\",\"" PK_GEN_LPFFREQ
        "\",\"" PK_GEN_LPFPOLE "\",\"" PK_GEN_DENSITY "\"]},";
    ```
    In omega_build_ui, after the kick2 splice and BEFORE UI_CLOSE, append UI_GROOVE1 always, and UI_GROOVE2 gated on `inst && inst->model == MODEL_GEN`:
    ```c
    ui_append(buf, buf_len, &off, UI_GROOVE1, (int)(sizeof(UI_GROOVE1) - 1));
    if (inst && inst->model == MODEL_GEN)
        ui_append(buf, buf_len, &off, UI_GROOVE2, (int)(sizeof(UI_GROOVE2) - 1));
    ```
    Watch trailing commas: UI_GROOVE1 and UI_GROOVE2 both end with `},` (a level entry inside the levels map); UI_CLOSE is `}}`. The last level before UI_CLOSE must NOT leave a dangling comma before the `}` that closes the levels map. Current code appends kick2 (no trailing comma — it ends `...}`) then UI_CLOSE `}}`. Adding groove levels AFTER kick2 means kick2 now needs a trailing comma. FIX: since kick2's UI_KICK2_FX ends `...}]}` (no comma), and groove1/groove2 follow, prepend a comma to UI_GROOVE1 (start it with `,"groove1":...`) so the sequence is `kick2},groove1},[groove2},]` and the final level (groove1 for non-GEN, groove2 for GEN) must drop its own trailing comma before UI_CLOSE. Simplest robust approach: give UI_GROOVE1 and UI_GROOVE2 a LEADING comma and NO trailing comma, matching how UI_KICK2_FX has no trailing comma; then the append order is kick2 (no comma) + ","+groove1 + optional ","+groove2 + UI_CLOSE. Implement it so the final serialized JSON is brace/bracket-balanced with no trailing comma before `}}` — the test_switch balance assertions (Task 3) are the gate. Verify by running test_switch.
  </action>
  <acceptance_criteria>
    - gen.c Kick-Page-2 interior is now empty: `grep -q "return 0" src/models/gen.c` in gen_p2_slot_desc (or it emits "" ) — GEN advertises no SEED/SCALE/DENSITY on Kick Page 2.
    - dsp.c gates the multitap on non-GEN but still updates tempo for GEN: `grep -q "inst->model != MODEL_GEN" src/dsp.c` AND groove_update_tempo is called unconditionally (outside the if).
    - ui.c has both fragments + the conditional: `grep -q "UI_GROOVE1" src/ui.c` and `grep -q "UI_GROOVE2" src/ui.c` and `grep -q "inst->model == MODEL_GEN" src/ui.c`.
    - UI_GROOVE2 lists all six GRV-04 keys: `grep -q "SEQ LEN" src/ui.c` and `grep -q "LPF FREQ" src/ui.c` and `grep -q "LPF POLE" src/ui.c`.
    - The full hierarchy stays valid: `make test-switch` exits 0 (its balance/null-terminator asserts pass with groove levels present — after Task 3 updates the GEN expected slot count).
  </acceptance_criteria>
  <verify>
    <automated>make test-switch</automated>
  </verify>
  <done>GEN's Kick Page 2 is FX-only; its six controls live on the conditional Groove Page 2 (emitted iff model==GEN); the kick-fed multitap is bypassed for GEN while the tempo clock still feeds it; ui_hierarchy stays balanced.</done>
</task>

<task type="auto">
  <name>Task 3: Extend the harnesses — JSON gating, transport determinism, GEN-clock + LPF-pole asserts</name>
  <files>tests/test_switch.c, tests/test_gen.c, tests/test_groove.c</files>
  <read_first>
    - tests/test_switch.c lines 43-51 (g_expected_slots — GEN was 3, now 0 interior on Kick Page 2) and 74-130 (assert_p2_json_valid) — add a groove2-gating assertion
    - tests/test_gen.c lines 40-60 (gen_prime_len) and the determinism assertions — add transport-clock determinism
    - tests/test_groove.c (C-01/C-02 harness) — add GEN-clock-tracks-BPM and LPF-pole-differs asserts
    - C-VALIDATION.md §Per-Task Verification Map GRV-04 row (JSON gating iff GEN; GEN clocks to driven BPM; SEQ LEN/DENSITY/SCALE/SEED change output; LPF POLE 2 vs 4 differ; same SEED → byte-identical)
    - C-RESEARCH.md §Pitfall 6 (groove2 must appear iff GEN; hierarchy balanced)
  </read_first>
  <action>
    Extend the three harnesses so GRV-04 is fully asserted offline. Keep `make test` green.

    test_switch.c:
    - Update g_expected_slots for GEN: its KICK Page 2 interior is now 0 (FX-only). Change `[MODEL_GEN] = 3` to `[MODEL_GEN] = 0` and adjust assert_p2_json_valid to handle the 0-interior case (a model that emits nothing on Kick Page 2 — the loop already `continue`s if p2_slot_desc is NULL, but GEN's returns 0; assert that a 0 return is tolerated and the full hierarchy still balances). The existing `assert(len > 0)` will fire for GEN — guard it: if the model legitimately emits 0 interior (GEN), skip the interior-content asserts but STILL run the full-hierarchy balance + null-terminator check.
    - Add a groove2-gating assertion: for a NON-GEN model (FM2), `get_param("ui_hierarchy")` must NOT contain `"groove2"`; after `set_param(PK_MODEL, "<MODEL_GEN index>")`, it MUST contain `"groove2"` and the keys `gen_seqlen`, `gen_lpffreq`, `gen_lpfpole`. Assert `strstr(ui, "groove2") == NULL` for FM2 and `!= NULL` for GEN. Assert `"groove1"` is present for BOTH (always emitted).

    test_gen.c:
    - Add a transport-clock determinism case: drive the mock beat (via mock_host_advance_beat per block at a fixed BPM, e.g. 128) and render GEN with a fixed SEED twice → assert BYTE-IDENTICAL (memcmp == 0). This proves determinism survives the transport clock (the tempo path must not introduce nondeterminism).
    - Add a SEQ LEN responsiveness case: SEQ LEN low ("0.0") vs high ("1.0") over a fixed window produces DIFFERENT buffers.

    test_groove.c:
    - Add a GEN-clocks-to-BPM assertion: select MODEL_GEN, drive the mock beat at 120 vs 174 BPM, count fired steps over a fixed window (detect step boundaries via output transient onsets, OR simply assert the rendered buffers DIFFER between the two BPMs since faster tempo fires more steps) — proves gen.c reads samples_per_16th, not GEN_STEP_FRAMES.
    - Add an LPF-POLE assertion: with LPF FREQ low ("0.1"), render GEN with `gen_lpfpole`="0" (2-pole) vs "1" (4-pole); assert the summed high-frequency energy (or overall RMS) DIFFERS measurably (4-pole attenuates more).

    All new renders: finite + `|x|<=1.0` + at least one non-silent, mirroring the existing style.
  </action>
  <acceptance_criteria>
    - test_switch.c: `grep -q "\\[MODEL_GEN\\] = 0" tests/test_switch.c` (GEN Kick-Page-2 interior count updated) and `grep -q "groove2" tests/test_switch.c` (gating assertion added).
    - test_gen.c: `grep -q "mock_host_advance_beat\|mock_host_set_beat" tests/test_gen.c` (transport-driven determinism) and a memcmp byte-identical assertion present.
    - test_gen.c: `grep -q "gen_seqlen\|PK_GEN_SEQLEN" tests/test_gen.c` (SEQ LEN responsiveness).
    - test_groove.c: `grep -q "gen_lpfpole\|PK_GEN_LPFPOLE" tests/test_groove.c` (LPF-pole assertion) and a GEN + 120-vs-174 comparison present.
    - Full suite green: `make test` exits 0 (test-switch, test-gen, test-groove, and all others).
    - Cross-build sanity (if Docker available in CI; not required locally): `make dsp.so` is expected to succeed in CI — do not gate this task on local Docker.
  </acceptance_criteria>
  <verify>
    <automated>make test</automated>
  </verify>
  <done>test_switch asserts groove2 appears iff GEN (and GEN's Kick Page 2 is FX-only), test_gen proves determinism survives the transport clock + SEQ LEN responsiveness, and test_groove proves the GEN sequence tracks driven BPM and the LPF pole toggle changes the sound. `make test` is fully green — GRV-04 verified offline.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 (all sub-suites, including the extended test-switch / test-gen / test-groove).
- `cc -std=gnu11 -O2 -Isrc -c src/models/gen.c` compiles; gen_state still fits model_state.
- gen.c reads `inst->groove.samples_per_16th`; GEN_STEP_FRAMES survives only as a guarded fallback (grep confirms it is not a live reload).
- ui_hierarchy contains `groove2` iff model==GEN (test_switch gating assertion).
- LPF POLE toggle and SEQ LEN measurably change GEN output; same SEED under the transport clock → byte-identical.
</verification>

<success_criteria>
- With GEN active, the generative sequence clocks to the project tempo (never the ~130-BPM hardcode) and drives the groove voice; non-GEN models use the kick-fed multitap.
- All six GRV-04 controls (SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY) live on Groove Page 2, shown only for GEN.
- LPF POLE gives a 2-pole vs 4-pole cascade; determinism holds under the transport clock.
- Groove Page 2 is hidden for FM2..USR; Groove Page 1 is always present.
</success_criteria>

<output>
After completion, create `.planning/phases/C-groove-rumble-engine/C-03-SUMMARY.md`
</output>
