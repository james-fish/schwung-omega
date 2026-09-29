---
phase: B-remaining-9-kick-models
plan: 02
type: execute
wave: 1
depends_on: []
files_modified:
  - src/dsp_primitives.h
  - src/dsp_primitives.c
  - tools/gen_wavetables.c
  - src/wavetables.h
  - tests/test_fx.c
  - Makefile
autonomous: true
requirements: [KICK-14, KICK-15]
must_haves:
  truths:
    - "All 5 FX modes (Diode, Clip, SAT, Fold, Crush) are selectable and bounded (output finite and |x|<=1) at max amt and high input"
    - "At FX amt=0 each mode is approximately transparent (passes a test tone through with small delta)"
    - "The band-limited wavetable read, modal resonator, xorshift PRNG, scale-quantize table, and noise burst are available as shared inline primitives for all models"
    - "g_wavetables lives in .rodata (static const), shared read-only across instances, with a 2048+1 guard sample per wave"
    - "Crush's powf is computed ONCE at control rate (fx-config/set_param) into fx_state_t.crush_levels; the fx_process render path contains NO powf/sinf/expf/tanf"
  artifacts:
    - path: "src/dsp_primitives.h"
      provides: "wt_read_bl, modal_t + modal_excite + modal_tick, prng_t + prng_seed + prng_next_f, scale_quantize, noise_t + noise_tick, fx_process + fx_state_t (with crush_levels), fx_config, crush()"
      contains: "fx_process"
    - path: "src/wavetables.h"
      provides: "static const _Alignas(16) g_wavetables[NUM_WAVES][BANDS][2049] in .rodata (build-time generated)"
      contains: "g_wavetables"
    - path: "tools/gen_wavetables.c"
      provides: "host-side generator emitting wavetables.h (like sine_table.h)"
      contains: "g_wavetables"
    - path: "tests/test_fx.c"
      provides: "per-FX-mode audible-change + bounded-at-max asserts (KICK-14)"
      contains: "fx_process"
  key_links:
    - from: "src/dsp_primitives.h fx_process"
      to: "bounded [-1,1] output"
      via: "x/(1+|x|) / triangle-fold / quantize forms (NO unbounded x/(1-x))"
      pattern: "fx_process"
    - from: "src/dsp_primitives.h fx_config"
      to: "fx_state_t.crush_levels (precomputed powf)"
      via: "control-rate powf in fx_config/set_param, NEVER in fx_process render loop"
      pattern: "crush_levels"
    - from: "tools/gen_wavetables.c"
      to: "src/wavetables.h"
      via: "Makefile generator target"
      pattern: "wavetables\\.h"
---

<objective>
Build the shared DSP primitives and the 5-mode post-kick FX chain (KICK-14) ONCE and well, so each of the 9 models becomes a thin recipe over them (no model hand-rolls a resonator, PRNG, band-limited table, or clipper). Add the build-time wavetable generator emitting `src/wavetables.h` in `.rodata` (KICK-15 pattern, mirroring the existing `sine_table.h`).

Purpose: Every model plan (B-03..B-08) depends on these primitives. The FX chain is a self-contained, testable unit reused by HRD/DIG's DRIVE/CRUSH.
Output: Extended `dsp_primitives.h/.c`, generated `wavetables.h`, host generator `tools/gen_wavetables.c`, and `tests/test_fx.c`.
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
@src/dsp_primitives.h
@src/dsp_primitives.c

<interfaces>
<!-- Existing primitives to REUSE, not reimplement (src/dsp_primitives.h):
     env_t + env_trigger/env_tick/env_coeff_from_ms; wt_read (linear, guard);
     tpt1_t + tpt1_lp; omega_to_i16; g_sine_table[2049]. All static inline.
     The sine table is DEFINED in dsp_primitives.c via generated sine_table.h —
     replicate that exact generator+include pattern for wavetables.h. -->
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: FX chain (KICK-14) — 5 bounded modes + crush(), with tests</name>
  <read_first>src/dsp_primitives.h (env_t/wt_read/tpt1 style, static inline convention), src/dsp_primitives.c, tests/test_fm2.c (assert/render-loop pattern), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§FX Chain table + §Code Examples bounded FX, Pitfall 5, Don't-Hand-Roll fast_tanh warning), STATE.md (bug #2: unbounded fast_tanh forbidden), CLAUDE.md (precompute transcendentals — no powf/sinf/expf in the audio render inner loop)</read_first>
  <behavior>
    - Test: each mode 0..4 renders a 60 Hz test tone at amt=1.0 and high-pitch (say 400 Hz) input; output stays finite and |y| <= 1.0 for all samples (Pitfall 5, no divergence/aliasing blow-up).
    - Test: at amt=0.0 each mode is approximately transparent (RMS(out) within a small tolerance of RMS(in), and no NaN).
    - Test: at amt=1.0 each mode measurably alters the tone (RMS or spectral delta vs dry above a threshold) — proves the mode is audible.
    - Test: Crush is stateful across calls (its state struct holds last-sample + phase); two identical successive amt values produce a stepped/held waveform, not per-sample identity.
    - Test: Crush uses the PRECOMPUTED crush_levels from fx_config — calling fx_process without ever computing crush_levels (levels==0) is handled safely (no div-by-zero / NaN).
  </behavior>
  <action>
    Add to `src/dsp_primitives.h` (all `static inline`, all bounded to [-1,1], NO per-sample tanhf/expf/powf — use rational/approx forms + PRECOMPUTED coefficients; the reference `fast_tanh` x/(1-x) is FORBIDDEN, STATE.md bug #2):
    - `typedef struct { float last; int hold_ctr; float crush_levels; } fx_state_t;` — Crush is the only stateful mode; `crush_levels` caches the precomputed quantization step-count so `fx_process` needs NO powf.
    - `void fx_config(fx_state_t *st, int mode, float amt)` — the CONTROL-RATE configurator called from each model's set_param / fx-parameter path (NOT per sample). For Crush it computes the bit-reduction levels ONCE:
      `st->crush_levels = powf(2.0f, 16.0f - amt*12.0f);`  /* powf lives HERE, control rate only */
      (For non-Crush modes fx_config may cache any per-amt gain constants similarly; the point is ALL powf/expf stay in fx_config.)
    - `float fx_process(int mode, float x, float amt, fx_state_t *st)` dispatching on mode 0..4 → Diode/Clip/SAT/Fold/Crush. It reads only PRECOMPUTED state (`st->crush_levels`, `st->last`, `st->hold_ctr`) and does NO powf/sinf/expf/tanf in the render inner loop. Use these EXACT bounded formulas from B-RESEARCH §FX Chain:
      * Diode (0): `y = copysignf(1.0f - approx_exp_neg(fabsf(x)*k), x)`, `k = 1 + amt*K` (K ~ 6). approx_exp_neg = a cheap `.rodata` LUT OR the rational `1/(1+z+0.5f*z*z)` clamped for z>=0 — NO per-sample expf. Asymptotes to ±1 → always bounded.
      * Clip (1): symmetric bounded soft clip `y = gx/(1+fabsf(gx))`, `gx = (1+amt*G)*x` (G ~ 4). (Use the SYMMETRIC form — B-RESEARCH: prefer symmetric where "always bounded" matters; do NOT use the x/(1-x) asymmetric neg branch.)
      * SAT (2): warm parallel saturation `y = (1-amt)*x + amt*(x/(1+fabsf(x)))`. Bounded by construction.
      * Fold (3): triangle wavefolder `v = g*x + 1.0f; v = v - 4.0f*floorf(v*0.25f); y = fabsf(v-2.0f) - 1.0f;` with `g = 1 + amt*F` (F ~ 4). Bounded to [-1,1] by the triangle fold (B-RESEARCH §Code Examples fx_fold verbatim).
      * Crush (4): bit + sample-rate reduction using the PRECOMPUTED `st->crush_levels` (set in fx_config) — NO powf here. Guard `levels = (st->crush_levels > 0.0f) ? st->crush_levels : 1.0f;` for safety. SR reduction: sample-and-hold, `hold = 1 + (int)(amt*H)` (H ~ 15) samples held in `st->hold_ctr`/`st->last`. `y = roundf(held * levels)/levels`. Bounded (rounding a bounded input stays bounded).
    - Add a standalone `float crush(float x, float bits)` bit-reducer in dsp_primitives.h reused by HRD/DIG (`levels = <precomputed by caller>; return roundf(x*levels)/levels;`) — models that use it must precompute `levels` at control rate (map bits→levels in set_param), keeping powf out of the render loop; the shared bit-reduction (Don't-Hand-Roll: HRD+DIG+FX share one impl).
    Clamp all gains so no branch can diverge. If a `.rodata` shaping LUT is used for Diode, define it in dsp_primitives.c.
    Create `tests/test_fx.c` implementing the behavior cases above (plain C assert + tiny main): each test calls `fx_config` to set up the state, then renders a tone through `fx_process`. Wire a `test-fx` Makefile target (compile `tests/test_fx.c tests/malloc_trap.c src/dsp_primitives.c` natively, run it) and add it as a `test` prerequisite.
  </action>
  <acceptance_criteria>
    - `grep -q 'crush_levels' src/dsp_primitives.h` and `fx_state_t` contains a `crush_levels` field
    - `grep -q 'fx_config' src/dsp_primitives.h` — a control-rate configurator that computes crush_levels via powf exists
    - The `fx_process` render function body contains NO `powf`/`sinf`/`expf`/`tanf` (extract the fx_process function and grep it): e.g. `! sed -n '/fx_process(int mode/,/^}/p' src/dsp_primitives.h | grep -Eq 'powf|sinf|expf|tanf'`
    - `powf` appears ONLY in `fx_config` (control rate), not in `fx_process`
    - `make test-fx` exits 0 (bounded-at-max + transparent-at-zero + audible-at-max + Crush statefulness + safe-uninitialized-levels)
  </acceptance_criteria>
  <verify>
    <automated>make test-fx && grep -q 'crush_levels' src/dsp_primitives.h && grep -q 'fx_config' src/dsp_primitives.h && ! sed -n '/fx_process(int mode/,/^}/p' src/dsp_primitives.h | grep -Eq 'powf|sinf|expf|tanf' && echo FX_OK</automated>
  </verify>
  <done>fx_process implements all 5 bounded modes reading only precomputed state; Crush's powf is computed once in fx_config (control rate) into fx_state_t.crush_levels; no powf/sinf/expf/tanf in the fx_process render path; crush() shared; test_fx.c asserts bounded-at-max + transparent-at-zero + audible-at-max + Crush statefulness + safe uninitialized levels; runs green under make.</done>
</task>

<task type="auto">
  <name>Task 2: Shared synthesis primitives — modal, PRNG, scale-quantize, noise, band-limited wt read</name>
  <read_first>src/dsp_primitives.h, src/dsp_primitives.c, .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§New shared primitives table, §Code Examples modal_t, Don't-Hand-Roll), CLAUDE.md (modal complex-rotation, xorshift64, scale LUTs)</read_first>
  <files>src/dsp_primitives.h, src/dsp_primitives.c</files>
  <action>
    Add to `src/dsp_primitives.h` (all `static inline`; coeff/transcendental work only in excite/seed, never per-sample tick):
    - Modal resonator (complex-rotation, B-RESEARCH §Code Examples VERBATIM):
      `typedef struct { float re, im, cos_w, sin_w, decay; } modal_t;`
      `modal_excite(modal_t *m, float freq_hz, float decay_per_sample, float amp)` — computes `w = 2*M_PI*freq_hz/OMEGA_SR`, `m->cos_w=cosf(w); m->sin_w=sinf(w); m->decay=decay_per_sample; m->re=amp; m->im=0;`. CLAMP freq_hz to [20, 0.45*OMEGA_SR] and decay_per_sample to (0,1) INSIDE excite to prevent NaN/blowup (Pitfall 5).
      `modal_tick(modal_t *m)` — one complex multiply + decay, returns `m->re` (verbatim from research).
    - PRNG xorshift64 (NOT rand()): `typedef struct { uint64_t s; } prng_t;` + `prng_seed(prng_t*, uint64_t)` (avoid seed==0 → force to a nonzero constant) + `prng_next_f(prng_t*)` → [0,1). Deterministic: same seed → same sequence (KICK-11).
    - Noise burst: `typedef struct { prng_t rng; } noise_t;` + `noise_tick(noise_t*)` → `2*prng_next_f-1` in [-1,1].
    - Band-limited wavetable read: `wt_read_bl(int wave, int band, float phase01)` indexing `g_wavetables[wave][band]` with the SAME linear-interp + guard-sample math as `wt_read` (relies on t[2048]==t[0]). Declare `extern const float g_wavetables[NUM_WAVES][BANDS][2049];` here; it is DEFINED in wavetables.h (Task 3) included from dsp_primitives.c.
    Add to `src/dsp_primitives.c`:
    - Scale-quantize: `static const int8_t g_scales[NUM_SCALES][12]` in `.rodata` (at least: chromatic, major, minor, minor-pentatonic) + `int scale_quantize(int scale, int degree)` returning a semitone offset (table lookup; free-freq mode bypasses at the caller). Declare the function + `NUM_SCALES` in the header.
    Define `NUM_WAVES`, `BANDS` (start with BANDS=1 per research — kicks rarely alias 40-200 Hz; add bands only if the harness shows aliasing), `NUM_SCALES` as macros in the header.
    Keep the FPCR/denormal assumptions from Phase A; no logging, no allocation, no file I/O in any of these.
  </action>
  <acceptance_criteria>
    - `grep -q 'modal_excite' src/dsp_primitives.h && grep -q 'modal_tick' src/dsp_primitives.h`
    - `grep -q 'prng_next_f' src/dsp_primitives.h && ! grep -q '\brand()' src/dsp_primitives.h src/dsp_primitives.c`
    - `grep -q 'wt_read_bl' src/dsp_primitives.h && grep -q 'g_wavetables' src/dsp_primitives.h`
    - `grep -q 'g_scales' src/dsp_primitives.c && grep -q 'scale_quantize' src/dsp_primitives.h`
    - modal_excite clamps freq and decay (grep for a clamp on freq_hz / decay in dsp_primitives.h)
  </acceptance_criteria>
  <verify>
    <automated>grep -q 'modal_tick' src/dsp_primitives.h && grep -q 'prng_next_f' src/dsp_primitives.h && grep -q 'scale_quantize' src/dsp_primitives.h && grep -q 'wt_read_bl' src/dsp_primitives.h && echo PRIM_OK</automated>
  </verify>
  <done>modal_t (clamped), prng_t xorshift64, noise_t, wt_read_bl, and scale_quantize are shared inline primitives; g_scales in .rodata; no rand()/alloc/log.</done>
</task>

<task type="auto">
  <name>Task 3: Wavetable generator + generated wavetables.h in .rodata; wire Makefile</name>
  <read_first>tools/ (existing sine-table generator if present — search for how sine_table.h is generated), src/dsp_primitives.c (the sine_table.h include pattern), Makefile (existing generator target for sine_table.h), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (§New shared primitives — band-limited tables, %.9e literal note from STATE.md A-01)</read_first>
  <files>tools/gen_wavetables.c, src/wavetables.h, Makefile</files>
  <action>
    Mirror the EXACT pattern used to generate `sine_table.h` (STATE.md A-01: literals printed with `%.9e` so bare 0/1/-1 become valid C float literals; `.rodata`, guard sample at index 2048 == index 0, `_Alignas(16)`).
    1. Create `tools/gen_wavetables.c` — a host-side (native `cc`) program that emits `src/wavetables.h` defining:
       `_Alignas(16) const float g_wavetables[NUM_WAVES][BANDS][2049] = { ... };`
       Generate a small set of factory single-cycle waves (NUM_WAVES; e.g. sine, triangle, a couple of harmonically-richer / bit-reduced / square-ish "digital" tables for DIG, and a warm "analog" saturated variant for ANA — enough for WTR WAVE SELECT, DIG WAVE IDX, ANA WAVE MORPH). Each table is 2048 samples + a duplicated guard sample at index 2048. BANDS=1 to start (research: add band-limited variants only if the harness detects aliasing on a hi-pitch sweep). Print floats with `%.9e`.
    2. Add a Makefile generator target (like the sine_table.h target) that builds and runs `tools/gen_wavetables.c` with native `cc` to (re)emit `src/wavetables.h`. Make the `test` and `dsp.so` targets depend on `src/wavetables.h` existing.
    3. `#include "wavetables.h"` in `src/dsp_primitives.c` so `g_wavetables` is defined exactly once (shared `.rodata` mapping, KICK-15). Ensure `NUM_WAVES`/`BANDS` in wavetables.h match the header macros from Task 2.
    Wave generation runs on the host, never on device (no audio-thread cost).
  </action>
  <acceptance_criteria>
    - `tools/gen_wavetables.c` exists and its output contains `g_wavetables`
    - `src/wavetables.h` is generated and contains `_Alignas(16)` and `g_wavetables[` and `[2049]`
    - `grep -q '#include "wavetables.h"' src/dsp_primitives.c`
    - Makefile has a target that builds/runs the generator; `make test` exits 0 (tables compile + link)
    - float literals in wavetables.h use exponent notation (grep `e+` or `e-` present)
  </acceptance_criteria>
  <verify>
    <automated>make test && grep -q 'g_wavetables' src/wavetables.h && grep -q '_Alignas(16)' src/wavetables.h && echo WT_OK</automated>
  </verify>
  <done>Host generator emits src/wavetables.h (g_wavetables in .rodata, 2048+1 guard, %.9e literals); dsp_primitives.c includes it once; Makefile regenerates it; suite compiles and links green.</done>
</task>

</tasks>

<verification>
- `make test-fx` exits 0 (5 FX modes bounded/transparent/audible/stateful; Crush uses precomputed crush_levels).
- `make test` exits 0 with the new primitives + generated wavetables linked.
- grep gates confirm modal/PRNG/scale/noise/wt_read_bl/fx_process/crush present and rand()-free; g_wavetables in .rodata with guard sample; no powf/sinf/expf/tanf in the fx_process render path.
</verification>

<success_criteria>
- The 5-mode FX chain is bounded at extremes (KICK-14 automated criterion) and transparent at amt=0, with Crush's powf precomputed at control rate (no transcendentals in the fx_process render loop).
- All shared synthesis primitives (modal, PRNG, scale, noise, band-limited wt) are available for the model plans, RT-safe (no per-sample transcendentals in tick, no alloc/log/file-IO).
- Band-limited wavetables are generated into .rodata at build time (KICK-15 pattern extended).
</success_criteria>

<output>
After completion, create `.planning/phases/B-remaining-9-kick-models/B-02-SUMMARY.md`
</output>
</output>
