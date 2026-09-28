---
phase: A-foundation-fm2-model
plan: 02
type: execute
wave: 2
depends_on: ["01"]
files_modified:
  - src/dsp.c
  - src/models/fm2.c
  - src/models/model_registry.c
  - module.json
  - tests/test_render.c
autonomous: true
requirements: [FNDTN-01, FNDTN-02, FNDTN-05, FNDTN-07, KICK-01, KICK-02, KICK-12]
must_haves:
  truths:
    - "Triggering FM2 (note-on) produces an audible, non-silent, deterministic kick in the offline WAV"
    - "All 8 Kick Page 1 params (PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR) audibly change the output"
    - "The 3 FM2 Page 2 params (FM RATIO, FM INDEX, OP2 WAVE) audibly change the output"
    - "The plugin exposes move_plugin_init_v2 and the full create/render/destroy lifecycle runs with no allocation during render"
    - "module.json declares id omega, sound_generator, drums, api_version 2"
  artifacts:
    - path: "src/dsp.c"
      provides: "move_plugin_init_v2 + 6 vtable fns (create/destroy/on_midi/set_param/get_param/render_block); dispatch only, single calloc, FPCR FTZ, clamped int16"
      contains: "move_plugin_init_v2"
    - path: "src/models/fm2.c"
      provides: "fm2_trigger/fm2_render/fm2_set_p2/fm2_p2_slot_desc + g_fm2_vtable; full FM2 DSP"
      contains: "g_fm2_vtable"
    - path: "src/models/model_registry.c"
      provides: "g_models[MODEL_COUNT] = { &g_fm2_vtable }"
      contains: "g_models"
    - path: "module.json"
      provides: "manifest: id omega, sound_generator, drums, api_version 2"
      contains: "sound_generator"
    - path: "tests/test_render.c"
      provides: "real lifecycle harness: move_plugin_init_v2 -> create -> on_midi -> 512 blocks -> assertions"
      contains: "move_plugin_init_v2"
  key_links:
    - from: "src/dsp.c render_block"
      to: "src/models/model_registry.c g_models[inst->model]->render"
      via: "vtable dispatch"
      pattern: "g_models\\[.*\\]->render"
    - from: "src/dsp.c on_midi"
      to: "src/models/fm2.c fm2_trigger"
      via: "g_models[inst->model]->trigger on note-on velocity>0"
      pattern: "->trigger"
    - from: "src/models/fm2.c fm2_render"
      to: "src/dsp_primitives.h wt_read + g_sine_table"
      via: "carrier + modulator both read shared sine table"
      pattern: "wt_read\\(g_sine_table"
    - from: "src/dsp.c render_block"
      to: "src/dsp_primitives.h omega_to_i16"
      via: "clamp+isfinite+lrintf int16 conversion (FNDTN-07)"
      pattern: "omega_to_i16"
---

<objective>
Implement the complete FM2 kick engine and wire it into the plugin entry points so a note-on produces an audible, fully parameterized kick offline. This is the vertical DSP slice: dsp.c (entry point + 6 vtable functions, dispatch only), fm2.c (full 2-op wavetable FM with pitch/index/amp/transient envelopes and COLOR filter), model_registry.c (the vtable array), and module.json (manifest). It extends the Wave 0 harness to drive the real lifecycle.

FM2 must be FULLY complete per D-03 — no stubs except FX TYPE/AMT passthrough (Claude's Discretion; KICK-14 is Phase B). All 8 Kick Page 1 params and 3 FM2 Page 2 params are wired and audibly functional.

Purpose: Prove KICK-01 (vtable dispatch), KICK-02 (FM2), KICK-12 (Page 1 params), and the RT-safe entry-point path (FNDTN-01/05/07) end-to-end offline before on-device validation.
Output: A `dsp.so` that builds and a `make test` that renders a real, parameter-responsive FM2 kick to WAV.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/phases/A-foundation-fm2-model/A-CONTEXT.md
@.planning/phases/A-foundation-fm2-model/A-RESEARCH.md
@CLAUDE.md
@.planning/phases/A-foundation-fm2-model/A-01-SUMMARY.md

<interfaces>
<!-- Contracts from Plan A-01 (src/omega.h, src/dsp_primitives.h). Use directly, no exploration. -->

From src/omega.h (defined in A-01):
```c
#define OMEGA_SR 44100.0f
#define OMEGA_MAX_BLOCK 256
typedef struct bohm_instance bohm_instance_t;   /* full struct defined in omega.h */
typedef struct { const char *name;
  void (*trigger)(bohm_instance_t*, int note, int velocity);
  void (*render)(bohm_instance_t*, float *l, float *r, int frames);
  void (*set_p2)(bohm_instance_t*, const char *key, const char *val);
  int  (*p2_slot_desc)(bohm_instance_t*, char *buf, int buf_len);
} kick_model_vtable_t;
typedef enum { MODEL_FM2 = 0, MODEL_COUNT } model_id_t;
extern const kick_model_vtable_t *g_models[MODEL_COUNT];
/* PK_PITCH, PK_LENGTH, PK_SUSTAIN, PK_CURVE, PK_ATTACK, PK_TRS_DEC,
   PK_TRS_TNE, PK_COLOR, PK_FM_RATIO, PK_FM_INDEX, PK_OP2_WAVE,
   PK_FX_TYPE, PK_FX_AMT, PK_MODEL, PK_MASTER_VOL, PK_UI_HIER */
/* struct bohm_instance { model_id_t model; float main_volume; bool logged_buflen; char model_state[4096]; ... } */
```

From src/dsp_primitives.h (defined in A-01):
```c
typedef struct { float value; float coeff; } env_t;
typedef struct { float s; } tpt1_t;
extern const float g_sine_table[2049];
static inline void  env_trigger(env_t*, float start, float coeff);
static inline float env_tick(env_t*);
static inline float env_coeff_from_ms(float time_ms); /* expf(-1/(ms*0.001*OMEGA_SR)) */
static inline float wt_read(const float *t, float phase01);
static inline float tpt1_lp(tpt1_t *f, float x, float g);
static inline int16_t omega_to_i16(float x); /* clamp+isfinite+lrintf */
```

FM2 render recipe: A-RESEARCH.md lines 228-248 (per-sample loop) — use VERBATIM.
CURVE 808<->909 blend: A-RESEARCH.md lines 251-259 (c909=env_coeff_from_ms(15), c808=env_coeff_from_ms(300)).
FPCR FTZ snippet: A-RESEARCH.md lines 437-445. render_block skeleton: lines 344-358.
Entry point/init: lines 328-339. get_param contract: return bytes written, -1 if unhandled (lines 311-314).
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Implement the FM2 engine (fm2.c) — full 2-op wavetable FM kick</name>
  <read_first>
    - src/omega.h, src/dsp_primitives.h (A-01 contracts)
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md §"FM2 DSP Recipe" (lines 210-275: per-sample loop, CURVE blend, env_coeff formula, output stage)
    - .planning/phases/A-foundation-fm2-model/A-CONTEXT.md D-03, D-04, D-05, D-06, D-07 and <specifics> (lines 120-123)
    - Context/02_OHMFORCE_BOHM_SYSTEM_SPEC.md (PITCH/LENGTH/SUSTAIN/CURVE/ATTACK/TRS DEC/TRS TNE/COLOR semantics)
    - Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md (exponential pitch env, tau approx 15ms)
    - Context/04_BOHM_SCHWUNG_MODULE_DESIGN.md (STUDY for pattern; do NOT copy — carries the 5 known bugs listed in A-RESEARCH lines 203-208 and STATE.md watchpoints)
  </read_first>
  <behavior>
    - Test 1 (KICK-02 non-silent): after fm2_trigger + rendering 512 blocks, summed absolute energy of the buffer exceeds a small threshold (kick is audible, not silence).
    - Test 2 (KICK-02 deterministic): rendering the same trigger+params twice yields byte-identical int16 buffers (no denormal/uninit nondeterminism).
    - Test 3 (KICK-12 PITCH): rendering with PK_PITCH low vs high yields differing buffers (memcmp != 0).
    - Test 4 (KICK-12 LENGTH): PK_LENGTH short vs long changes the decay tail (later-block energy differs measurably).
    - Test 5 (Page 2 FM INDEX): PK_FM_INDEX low vs high changes spectral content (buffers differ).
    - Test 6 (finite/bounded): every rendered float sample is isfinite and |x|<=1.0 before int16 conversion.
  </behavior>
  <action>
    Create `src/models/fm2.c` (`#include "omega.h"`, `#include "dsp_primitives.h"`, `#include <math.h>`, `#include <string.h>`).

    Define `typedef struct fm2_state { ... } fm2_state;` overlaid onto `bohm_instance.model_state` (access via `fm2_state *fm = (fm2_state*)inst->model_state;`; add `_Static_assert(sizeof(fm2_state) <= 4096, "fm2_state fits model_state");`). Fields:
    - `float f0; float sweep_hz;` (pitch env depth in Hz above f0)
    - `env_t pitch_env_fast, pitch_env_slow;` (909 fast / 808 slow — D-06 dual-curve blend), `float curve;` (0=808, 1=909)
    - `env_t amp_env; env_t index_env; env_t trs_env;`
    - `float car_phase, mod_phase; float ratio; float fm_index; float op2_wave;`
    - `float sustain; float trs_amp;`
    - `tpt1_t color_lp; float color_g;`
    - `tpt1_t trs_tone_lp; float trs_tone_g;` (TRS TNE brightness shaping on the click)
    - cached raw ms values for LENGTH, ATTACK, TRS DEC so trigger recomputes coeffs.

    Write a locale-independent float parser `static float parse_f(const char *s)` — manual sign/integer/fraction scan; do NOT use `atof`/`strtod` (A-RESEARCH line 206; establishes the UI-01 habit project-wide).

    Implement `void fm2_set_param(bohm_instance_t *inst, const char *key, const char *val)` handling BOTH Page-1 and Page-2 keys (dsp.c dispatches all kick keys here). Values normalized 0..1 unless noted; map:
    - PK_PITCH -> `fm->f0 = 30.0f + v*(200.0f-30.0f)` Hz (Bohm sub-C1..C3), and `fm->sweep_hz` proportional to f0 (e.g. `fm->sweep_hz = fm->f0 * 4.0f`).
    - PK_LENGTH -> amp decay time_ms in ~[50,1500]; cache; recompute in trigger.
    - PK_SUSTAIN -> `fm->sustain = v` (tail contour scalar applied to amp).
    - PK_CURVE -> `fm->curve = v` (0=808 slow, 1=909 fast).
    - PK_ATTACK -> `fm->trs_amp` (click amplitude) scaled by v.
    - PK_TRS_DEC -> transient decay time_ms in [1,30]; cache.
    - PK_TRS_TNE -> `fm->trs_tone_g = tanf(M_PI * fc / OMEGA_SR)` where fc mapped from v in [500,16000] (precompute here, NOT per sample).
    - PK_COLOR -> `fm->color_g = tanf(M_PI * fc / OMEGA_SR)` where fc mapped from v in [200,18000] (precompute here, NOT per sample).
    - PK_FM_RATIO -> `fm->ratio` in [0.5,8.0].
    - PK_FM_INDEX -> `fm->fm_index` in [0,12].
    - PK_OP2_WAVE -> `fm->op2_wave` in [0,1] (modulator waveshape blend factor).
    - PK_FX_TYPE / PK_FX_AMT -> accept + store, identity passthrough only; comment `/* TODO(Phase B): Diode/Clip/SAT/Fold/Crush (KICK-14) */`.

    Implement `void fm2_trigger(bohm_instance_t *inst, int note, int velocity)`: `float velf = velocity/127.0f;` recompute coeffs: `float c909=env_coeff_from_ms(15.0f); float c808=env_coeff_from_ms(300.0f);` (A-RESEARCH 254-255). `env_trigger(&fm->pitch_env_fast, 1.0f, c909); env_trigger(&fm->pitch_env_slow, 1.0f, c808);` amp: `env_trigger(&fm->amp_env, velf, env_coeff_from_ms(length_ms));` index: `env_trigger(&fm->index_env, 1.0f, env_coeff_from_ms(40.0f));` trs: `env_trigger(&fm->trs_env, 1.0f, env_coeff_from_ms(trs_dec_ms));` Reset `fm->car_phase=fm->mod_phase=0.0f;` and `fm->color_lp.s = fm->trs_tone_lp.s = 0.0f;`.

    Implement `void fm2_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames)` — per-sample loop VERBATIM structure from A-RESEARCH lines 228-248, adapted for the dual pitch env (D-06):
    ```
    float p_fast = env_tick(&fm->pitch_env_fast);
    float p_slow = env_tick(&fm->pitch_env_slow);
    float pitch  = p_slow + fm->curve*(p_fast - p_slow);      /* 808<->909 lerp */
    float fcar   = fm->f0 + pitch * fm->sweep_hz;
    float fmod   = fm->ratio * fcar;
    float idx    = env_tick(&fm->index_env) * fm->fm_index;   /* FM index has its OWN env (KICK-02) */
    fm->mod_phase += fmod/OMEGA_SR; if (fm->mod_phase>=1.f) fm->mod_phase-=1.f;
    float mod_out = wt_read(g_sine_table, fm->mod_phase);
    /* OP2 WAVE: blend sine with a folded variant, e.g. mod_out = mod_out + fm->op2_wave*(fabsf(mod_out)*2.f-1.f - mod_out); */
    float car_ph = fm->car_phase + idx*mod_out; car_ph -= floorf(car_ph);
    float car_out = wt_read(g_sine_table, car_ph);
    fm->car_phase += fcar/OMEGA_SR; if (fm->car_phase>=1.f) fm->car_phase-=1.f;
    float amp   = env_tick(&fm->amp_env) * (0.5f + 0.5f*fm->sustain); /* SUSTAIN contour */
    float click = fm->trs_amp * env_tick(&fm->trs_env);
    click = tpt1_lp(&fm->trs_tone_lp, click, fm->trs_tone_g);  /* TRS TNE brightness */
    float s = car_out*amp + click;
    s = tpt1_lp(&fm->color_lp, s, fm->color_g);               /* COLOR */
    out_l[n]=out_r[n]=s;
    ```
    Output stays float — dsp.c does the int16 conversion. Do NOT copy the reference unclamped int16 cast.

    Implement `void fm2_set_p2(bohm_instance_t *inst, const char *key, const char *val)` — delegate PK_FM_RATIO/PK_FM_INDEX/PK_OP2_WAVE/PK_FX_TYPE/PK_FX_AMT to `fm2_set_param`.

    Implement `int fm2_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len)` — write a JSON fragment listing the 3 FM2 slots with keys PK_FM_RATIO/PK_FM_INDEX/PK_OP2_WAVE and labels "FM RATIO"/"FM INDEX"/"OP2 WAVE"; bounded to buf_len; return bytes written. (A-03's ui.c consumes this; keep format simple and documented in a comment.)

    Define `const kick_model_vtable_t g_fm2_vtable = { .name="FM2", .trigger=fm2_trigger, .render=fm2_render, .set_p2=fm2_set_p2, .p2_slot_desc=fm2_p2_slot_desc };`

    Add to `src/omega.h` the prototypes: `extern const kick_model_vtable_t g_fm2_vtable;` and `void fm2_set_param(bohm_instance_t*, const char*, const char*);` (so dsp.c can dispatch Page-1 keys).
  </action>
  <verify>
    <automated>make test</automated>
  </verify>
  <acceptance_criteria>
    - `src/models/fm2.c` contains `g_fm2_vtable` with `.name="FM2"` and all four members assigned
    - `src/models/fm2.c` contains `wt_read(g_sine_table, fm->mod_phase)` AND `wt_read(g_sine_table, car_ph)` (both operators read the shared table)
    - `src/models/fm2.c` contains `fm->curve*(p_fast - p_slow)` (D-06 dual-curve blend)
    - `src/models/fm2.c` contains `env_tick(&fm->index_env)` (FM index has its own decay env — KICK-02)
    - `src/models/fm2.c` contains a `parse_f` function and does NOT call `atof` or `strtod` (grep -c returns 0 for both)
    - `src/models/fm2.c` handles all 11 keys PK_PITCH, PK_LENGTH, PK_SUSTAIN, PK_CURVE, PK_ATTACK, PK_TRS_DEC, PK_TRS_TNE, PK_COLOR, PK_FM_RATIO, PK_FM_INDEX, PK_OP2_WAVE
    - `src/models/fm2.c` contains `_Static_assert(sizeof(fm2_state) <= 4096`
    - `tanf` appears in fm2_set_param but NOT inside fm2_render (no per-sample transcendentals)
    - `make test` exits 0 with A-02 harness assertions (Task 3) green
  </acceptance_criteria>
  <done>FM2 renders an audible, deterministic, fully-parameterized kick; all 8 Page-1 and 3 Page-2 params map to DSP; both oscillators read the shared sine table; FM index and pitch (808<->909) have dedicated envelopes; COLOR filters output; no atof, no unclamped cast, no per-sample transcendentals.</done>
</task>

<task type="auto">
  <name>Task 2: Implement plugin entry points (dsp.c) + registry + module.json</name>
  <read_first>
    - src/omega.h, src/dsp_primitives.h (A-01 contracts)
    - src/models/fm2.c (Task 1 — for g_fm2_vtable and fm2_set_param)
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md §"Code Examples" (lines 322-377), FPCR snippet (437-445), Pitfalls 1/4/5 (293-320)
    - .planning/phases/A-foundation-fm2-model/A-CONTEXT.md D-01 (dsp.c = dispatch only, no DSP math), integration points (lines 111-114)
    - Context/01_SCHWUNG_DEV_ARCHITECTURE.md (module.json manifest fields; on_midi source semantics)
  </read_first>
  <action>
    Create `src/models/model_registry.c` (`#include "omega.h"`): `extern const kick_model_vtable_t g_fm2_vtable;` then `const kick_model_vtable_t *g_models[MODEL_COUNT] = { &g_fm2_vtable };` (VERBATIM A-RESEARCH 152-154). Comment: "only file that knows the full model list; Phase B appends entries here (KICK-01 append-only)."

    Create `src/dsp.c` (`#include "omega.h"`, `#include "dsp_primitives.h"`, `#include <stdlib.h>`, `#include <string.h>`) — DISPATCH ONLY, no DSP math (D-01):
    - `static const host_api_v1_t *g_host = NULL; static plugin_api_v2_t g_api;`
    - Under `#ifdef OMEGA_MALLOC_TRAP` declare `extern bool g_audio_thread_active;` else `static bool g_audio_thread_active;` (so both builds compile).
    - FPCR helper VERBATIM A-RESEARCH 437-445: `static inline void omega_set_ftz(void)` with `#if defined(__aarch64__)` mrs/msr fpcr FZ bit 24; no-op otherwise.
    - Locale-independent parse: reuse `parse_f` — declare `extern float ... ` OR add a small static copy in dsp.c for PK_MODEL/PK_MASTER_VOL. Keep it locale-independent (no atof).
    - `omega_create(module_dir, json_defaults)`: single `bohm_instance_t *inst = calloc(1, sizeof(bohm_instance_t));` (CLAUDE.md single-alloc; the one permitted alloc, on audio thread). Set `inst->model=MODEL_FM2; inst->main_volume=1.0f; inst->logged_buflen=false;` call `fm2_set_param(inst, key, "0.5")` for all 11 kick keys to set mid defaults. Return inst.
    - `omega_destroy(inst)`: `free(inst);`
    - `omega_on_midi(inst, msg, len, source)`: trigger only on `len>=3 && (msg[0]&0xF0)==0x90 && msg[2]>0` (Pitfall 5): `g_models[inst->model]->trigger(inst, msg[1], msg[2]);` ignore note-off/vel-0/other status.
    - `omega_set_param(inst, key, val)`: if `strcmp(key,PK_MODEL)==0` set `inst->model` clamped `< MODEL_COUNT`; else if `strcmp(key,PK_MASTER_VOL)==0` set `inst->main_volume` via locale-independent parse; else `fm2_set_param(inst, key, val)`. No allocation.
    - `omega_get_param(inst, key, buf, buf_len)`: if `strcmp(key,PK_UI_HIER)==0` return `omega_build_ui(inst, buf, buf_len)` — declare `extern int omega_build_ui(bohm_instance_t*, char*, int);` (ui.c in A-03 owns it). For THIS plan to link a standalone dsp.so, provide a TEMP fallback: put `omega_build_ui` behind `#ifndef OMEGA_HAS_UI` returning a minimal 1-line JSON `{"pages":[]}` written via memcpy+null-term, marked `/* TODO(A-03): replaced by real ui.c hierarchy */` — A-03 defines OMEGA_HAS_UI (or simply owns the symbol and removes this fallback). Return bytes written; `return -1;` for unknown keys (Pitfall 4). Do NOT implement the D-10 buf_len log here (A-03 owns it with the one-shot flag). No allocation.
    - `omega_render_block(inst, out_lr, frames)` VERBATIM structure A-RESEARCH 344-358: `g_audio_thread_active=true;` `omega_set_ftz();` clamp `if(frames>OMEGA_MAX_BLOCK) frames=OMEGA_MAX_BLOCK;` `float l[OMEGA_MAX_BLOCK], r[OMEGA_MAX_BLOCK];` dispatch `g_models[inst->model]->render(inst,l,r,frames);` loop `out_lr[n*2]=omega_to_i16(l[n]*inst->main_volume); out_lr[n*2+1]=omega_to_i16(r[n]*inst->main_volume);` `g_audio_thread_active=false;`
    - `move_plugin_init_v2(host)` VERBATIM A-RESEARCH 328-339, marked `__attribute__((visibility("default")))`: stash g_host; `g_api.api_version=2;` assign the 6 fn pointers; `return &g_api;`

    Create `module.json` (FNDTN-02): `{"id":"omega","name":"Omega","component_type":"sound_generator","pad_layout":"drums","api_version":2,"version":"0.1.0"}` — valid JSON, < 8KB.
  </action>
  <verify>
    <automated>make test && python3 -c "import json; d=json.load(open('module.json')); assert d['id']=='omega' and d['component_type']=='sound_generator' and d['pad_layout']=='drums' and d['api_version']==2; print('module.json OK')"</automated>
  </verify>
  <acceptance_criteria>
    - `src/dsp.c` contains `move_plugin_init_v2` marked `__attribute__((visibility("default")))`
    - `src/dsp.c` contains `calloc(1, sizeof(bohm_instance_t))` exactly once and `free(` exactly once
    - `src/dsp.c` on_midi triggers only on `(msg[0]&0xF0)==0x90` and `msg[2]>0` (grep `0x90` and `msg[2]`)
    - `src/dsp.c` render_block sets `g_audio_thread_active=true` before and `false` after, calls `omega_set_ftz()`, and uses `omega_to_i16` for both channels
    - `src/dsp.c` render dispatches via `g_models[inst->model]->render`
    - `src/dsp.c` get_param returns `-1` for unknown keys (grep `return -1`)
    - `src/dsp.c` does NOT call `atof` or `strtod` (grep -c returns 0)
    - `src/models/model_registry.c` contains `g_models[MODEL_COUNT] = { &g_fm2_vtable }`
    - `module.json` parses as JSON and has the 4 required fields (verify command prints `module.json OK`)
    - `make test` exits 0
  </acceptance_criteria>
  <done>The plugin entry point, registry, and manifest are complete; create/render/destroy dispatches through the vtable, note-on triggers FM2, output goes through FPCR-FTZ + clamped int16, and no DSP math lives in dsp.c.</done>
</task>

<task type="auto">
  <name>Task 3: Wire the real lifecycle + parameter-response assertions into the test harness</name>
  <read_first>
    - tests/test_render.c (A-01 stub — this task replaces the TODO(A-02) block)
    - src/dsp.c, src/models/fm2.c (Tasks 1-2 — the lifecycle + params under test)
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md §"Test Harness" render-to-WAV (lines 480-498), Validation §Test Map (lines 512-529)
    - .planning/phases/A-foundation-fm2-model/A-VALIDATION.md Per-Task Verification Map
  </read_first>
  <action>
    Rewrite the `TODO(A-02)` block in `tests/test_render.c` to drive the real plugin lifecycle (VERBATIM structure A-RESEARCH 480-498) and update the Makefile `test` target to compile `src/dsp.c src/models/fm2.c src/models/model_registry.c` (define `OMEGA_HAS_UI` is NOT set in A-02; A-03 adds ui.c — for A-02, dsp.c's temporary `omega_build_ui` fallback compiles standalone).
    - `host_api_v1_t host = make_mock_host(); plugin_api_v2_t *api = move_plugin_init_v2(&host);`
    - `void *inst = api->create_instance("/tmp/omega", "{}");`
    - Helper `render_energy(api, inst, int16_t *buf)`: trigger note-on `{0x90,36,100}`, render 512 blocks of 128 into a caller buffer, assert each int16 in `[INT16_MIN,INT16_MAX]`, return summed abs energy.
    - Assertion FNDTN-01: `assert(api && api->api_version==2 && inst);`
    - Assertion KICK-01: `assert(g_models[MODEL_FM2] && strcmp(g_models[MODEL_FM2]->name,"FM2")==0);` (declare `extern const kick_model_vtable_t *g_models[];`).
    - Assertion KICK-02 non-silent: `assert(render_energy(...) > THRESHOLD);` write this run to `tests/output/fm2_kick.wav`.
    - Assertion KICK-02 deterministic: render twice into two buffers with identical params; `assert(memcmp(bufA,bufB,sizeof)==0);`
    - Assertion KICK-12 PITCH: set PK_PITCH "0.1" render bufLow; set "0.9" render bufHigh; `assert(memcmp(bufLow,bufHigh,...)!=0);`
    - Assertion KICK-12 LENGTH: set PK_LENGTH short vs long; assert later-half energy differs.
    - Assertion Page2 FM INDEX: set PK_FM_INDEX "0.0" vs "0.9"; assert buffers differ.
    - Assertion KICK-15: `omega_primitives_selfcheck();` (already present from A-01 — keep).
    - `api->destroy_instance(inst);` print `ALL TESTS PASSED`.
    - The isfinite/|x|<=1.0 check lives inside `omega_to_i16` path; add a debug assert variant in the harness if needed to check the float pre-conversion (D-12).
  </action>
  <verify>
    <automated>make test && test -s tests/output/fm2_kick.wav</automated>
  </verify>
  <acceptance_criteria>
    - `tests/test_render.c` contains `move_plugin_init_v2(&host)` and `api->create_instance`
    - `tests/test_render.c` contains a determinism check `memcmp(` returning 0 for identical params
    - `tests/test_render.c` contains PITCH-differ, LENGTH-differ, and FM_INDEX-differ assertions (grep PK_PITCH, PK_LENGTH, PK_FM_INDEX)
    - `tests/test_render.c` asserts `g_models[MODEL_FM2]->name` equals `"FM2"`
    - `tests/test_render.c` no longer contains `TODO(A-02)`
    - `make test` exits 0 and prints `ALL TESTS PASSED`
    - `tests/output/fm2_kick.wav` is non-empty
  </acceptance_criteria>
  <done>The harness drives the real move_plugin_init_v2 lifecycle, proves FM2 is audible + deterministic, and proves PITCH/LENGTH/FM INDEX audibly change the output — covering KICK-01/02/12 and FNDTN-01/06/07 offline.</done>
</task>

</tasks>

<verification>
- `make test` exits 0, prints ALL TESTS PASSED, and writes a non-empty `tests/output/fm2_kick.wav`.
- FM2 renders audible, deterministic, parameter-responsive output; both operators read the shared `.rodata` sine table.
- dsp.c dispatches through the vtable with FPCR FTZ set and clamped int16 output; single calloc/free; no DSP math in dsp.c; no atof anywhere.
- `module.json` declares id omega / sound_generator / drums / api_version 2 (FNDTN-02).
- Cross-build: `make dsp.so` inside the Docker image should now succeed (A-01's CI job flips continue-on-error to false is handled by A-03/A-04 once ui.c lands; A-02 confirms the native harness).
</verification>

<success_criteria>
- KICK-01 (vtable dispatch), KICK-02 (FM2 with own FM-index env), KICK-12 (8 Page-1 params), FNDTN-01/05/07 all proven offline.
- No stubs in FM2 except FX TYPE/AMT passthrough (D-03 honored).
- Deterministic byte-stable render (no denormal/uninit nondeterminism).
</success_criteria>

<output>
After completion, create `.planning/phases/A-foundation-fm2-model/A-02-SUMMARY.md`
</output>
