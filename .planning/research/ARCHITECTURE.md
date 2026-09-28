# Architecture Patterns

**Domain:** Native C Schwung sound-generator module (techno kick synthesis) for Ableton Move
**Researched:** 2026-09-28
**Overall confidence:** HIGH for the Schwung API surface and DR32/Forge dispatch precedent (Context docs + repo READMEs); MEDIUM for Move per-process memory ceiling (no hard figure published — estimated from constraints); MEDIUM for the specific bidirectional-macro pattern (derived from API semantics, no reference implementation observed).

---

## Recommended Architecture

Omega is a **single flat `dsp.so`** that implements `plugin_api_v2_t`. It does NOT nest an external Schwung module as its voice (EXT hosting is deferred to v2 per PROJECT.md), so the reference design in `04_BOHM_SCHWUNG_MODULE_DESIGN.md` — which uses `inner_kick_instance` / `inner_kick_api` pointers — is superseded. All 10 kick models are internal DSP written directly into the module.

```
                        move_plugin_init_v2(host)
                                  │  returns static plugin_api_v2_t vtable
                                  ▼
        ┌───────────────────────────────────────────────────────┐
        │                  omega_instance_t                       │
        │  (single heap block, allocated once in create_instance) │
        └───────────────────────────────────────────────────────┘
                                  │
     ┌───────────┬───────────────┼────────────────┬──────────────┐
     ▼           ▼               ▼                ▼              ▼
 param_store  kick_engine_t   groove_state_t  performer_state_t  preset_bank_t
 (canonical   ┌────────────┐  (4-tap delay,   (duck env, DJ     (N presets
  param       │ model[10]  │   ~705 KB buf,   filter, clipper)   loaded at
  values +    │ dispatch   │   color LPF)                        init from disk)
  macro       │ via        │
  mirror)     │ vtable ptr │   scratch buffers (kick[], groove[]) — RT-safe
              └────────────┘   ui_scratch[8 KB] for dynamic ui_hierarchy JSON
```

### Component Boundaries

| Component | Responsibility | Communicates With | Owns Memory |
|-----------|---------------|-------------------|-------------|
| `omega_module.c` (dispatch) | Implements the 6 vtable fns; routes `set_param`/`get_param` keys; owns lifecycle | Everything | The instance block |
| `param_store` (canonical state) | Single source of truth for every parameter value + 8-slot macro mirror | Dispatch, all DSP | Flat float/int arrays |
| `kick_engine_t` + `models/*.c` | 10 synthesis models behind a function-pointer table; renders dry kick into scratch | Dispatch (param push), render pipeline | Per-model wavetables, envelope state |
| `groove.c` (`groove_state_t`) | 4-tap 16th-note delay rumble + color LPF; GEN sequencer state | Render pipeline (reads kick scratch) | ~705 KB delay buffer |
| `performer.c` (`performer_state_t`) | Sidechain duck envelope, DJ filter (TPT SVF), soft clipper | Render pipeline | Filter state (tiny) |
| `ui.c` | Assembles `ui_hierarchy` JSON into a scratch buffer, model-context-sensitive | `get_param`, param_store | ui_scratch buffer |
| `presets.c` (`preset_bank_t`) | Enumerates + loads preset files at init; applies preset to param_store; flags save requests | Dispatch, param_store, host filesystem (init only) | Preset bank array |

### Data Flow (render_block, per 128-frame block)

```
render_block(inst, out_lr, frames):
  1. kick_engine.vtable->render(active_model_state, kick_scratch[frames], frames)
        → produces dry kick, float, into scratch (NOT int16 — keep float internally
          to avoid the double int16 round-trip the reference code does)
  2. for each frame:
        groove_out = groove_process(kick_scratch[f])   // write to delay ring, sum 4 taps, color LPF
        duck_env   = performer_duck(kick_scratch[f])    // kick amplitude drives duck
        groove_out *= duck_env
        mix        = (kick + groove_out)
        mix        = dj_filter(mix)                      // performer TPT filter on the sum
        mix        = soft_clip(mix * master_vol)         // +4.6 dB headroom
        out_lr[f*2], out_lr[f*2+1] = to_int16(mix_l, mix_r)
```

**Key deviation from reference design:** keep the internal signal path in `float`, convert to `int16` only once at the output. The reference `bohm_render_block` renders the inner voice as `int16` then divides by 32768 back to float — a lossy round-trip that Omega avoids because all models are internal float generators.

---

## 1. Instance Struct Layout

**Recommendation:** one master struct, one `calloc` in `create_instance`, sub-structs by concern. The struct is large (~730 KB, dominated by the groove delay buffer) so it MUST be heap-allocated — do NOT put it on the stack and do NOT embed it by value anywhere that could be stack-allocated.

```c
#define OMEGA_SR            44100
#define MAX_DELAY_FRAMES    88200        // 2 s at 44.1 kHz
#define NUM_TAPS            4
#define NUM_MODELS          10
#define NUM_MACROS          8
#define MAX_PRESETS         64
#define UI_SCRATCH_BYTES    8192
#define BLOCK_SCRATCH       256          // covers up to 128 frames stereo (float)

typedef enum { MDL_FM2=0, MDL_FM4, MDL_WTR, MDL_PHY, MDL_HRD,
               MDL_DIG, MDL_TRS, MDL_ANA, MDL_USR, MDL_GEN } model_id_t;

// Per-model DSP state lives in a union-or-tagged-array (see §2). Kept small.
typedef struct {
    model_id_t   active;
    kick_common_t common;              // pitch, length, sustain, curve, attack, trs_dec, trs_tne, color
    model_state_t state[NUM_MODELS];   // per-model private state (only `active` is advanced)
    // wavetable banks are SHARED (const, read-only) — see memory budget, kept in module-static
} kick_engine_t;

typedef struct {
    float delay_l[MAX_DELAY_FRAMES];   // 352,800 bytes
    float delay_r[MAX_DELAY_FRAMES];   // 352,800 bytes
    int   write_pos;
    float tap_vol[NUM_TAPS];
    float length, color_cut, color_z1_l, color_z1_r;
    // GEN model sequencer (Groove Page 2)
    uint32_t seed; int seq_len, seq_pos; float seq_freq[16]; int scale; float density;
    float lpf_z1, lpf_z2; int lpf_pole; // 2/4-pole toggle
    int   mono;
} groove_state_t;                       // ~705 KB

typedef struct {
    float duck_depth, duck_rel, duck_smooth, duck_bs, duck_env;
    float dj_filter_pos, dj_reso, dj_z1_l, dj_z2_l, dj_z1_r, dj_z2_r;
    float clip_amount;
} performer_state_t;                    // < 100 bytes

typedef struct {
    char  name[32];
    // full serialized param snapshot (all keys) — stored compactly
    float values[OMEGA_PARAM_COUNT];
    model_id_t model;
} preset_t;

typedef struct {
    preset_t slot[MAX_PRESETS];         // ~ 64 * (32 + ~200*4) ≈ 55 KB
    int      count;
    int      current;
    // deferred-save handshake (audio thread → main/init thread)
    volatile int   save_request;        // 0 = none, 1 = save, 2 = save-as
    char           save_name[32];
} preset_bank_t;

typedef struct omega_instance {
    const host_api_v1_t *host;
    char  module_dir[256];              // captured from create_instance for preset paths

    // Canonical parameter store + macro mirror (bidirectional sync — see §4)
    float macro_val[NUM_MACROS];        // last-known macro encoder positions

    kick_engine_t     kick;
    groove_state_t    groove;           // the 705 KB member
    performer_state_t performer;
    preset_bank_t     presets;

    // RT scratch — pre-allocated, never malloc'd on audio thread
    float kick_scratch[BLOCK_SCRATCH];      // 1 KB
    float groove_scratch[BLOCK_SCRATCH];    // 1 KB
    char  ui_scratch[UI_SCRATCH_BYTES];     // 8 KB
} omega_instance_t;
```

**Sizing:** `sizeof(omega_instance_t) ≈ 730 KB`, entirely dominated by `groove_state_t` (705 KB). Everything else is < 70 KB combined. Because Move runs many instances (16 DR32 pads, 16 Movy tracks), see §6 for the multi-instance budget flag.

**Rules:**
- Single `calloc(1, sizeof(omega_instance_t))` in `create_instance`. `calloc` zeroes state (env states, write_pos, filter z-states) for free.
- Wavetable banks are **read-only const data** — put them in module `static const` tables (in `.rodata`, shared across all instances, not per-instance). This is the single most important memory decision (§6).
- Never place `omega_instance_t` on the stack. Pass by pointer only.

---

## 2. Model Dispatch Table

**Recommendation:** function-pointer vtable per model, one C file per model, registered in a static array indexed by `model_id_t`. This mirrors both DR32 (`dsp/engines/` with a section-per-engine picker) and Forge (5 algorithms with "algorithm-dependent labels" and table-driven knob mapping) — the two closest reference implementations in the Schwung ecosystem.

Do **not** use a giant `switch` in `render_block`. A vtable keeps each model isolated, testable in isolation, and makes the build-order incremental (add models one at a time).

```c
// models/model_api.h
typedef struct {
    const char *id;                                    // "FM2", "FM4", ...
    void  (*trigger)(model_state_t *s, const kick_common_t *c, float velocity);
    float (*render) (model_state_t *s, const kick_common_t *c);   // one sample; or block variant
    void  (*set_p2) (model_state_t *s, int slot, float v);        // Kick Page 2 model-specific slots
    const p2_slot_desc_t *p2_slots;                    // 6 slot descriptors for dynamic UI (§3)
    int   p2_slot_count;
} model_vtable_t;

// models/registry.c
extern const model_vtable_t MODEL_FM2, MODEL_FM4, MODEL_WTR, MODEL_PHY, MODEL_HRD,
                            MODEL_DIG, MODEL_TRS, MODEL_ANA, MODEL_USR, MODEL_GEN;

const model_vtable_t *const MODEL_TABLE[NUM_MODELS] = {
    &MODEL_FM2, &MODEL_FM4, &MODEL_WTR, &MODEL_PHY, &MODEL_HRD,
    &MODEL_DIG, &MODEL_TRS, &MODEL_ANA, &MODEL_USR, &MODEL_GEN
};
```

- `model_state_t` should be a `union` of the per-model state structs (or a fixed-size opaque byte buffer sized to the largest) so the array `state[NUM_MODELS]` stays predictable. Only the active model advances its state each block; on model switch, `trigger` re-initializes.
- Each model file (`models/fm2.c`, etc.) exports exactly one `const model_vtable_t`. This is the same isolation pattern DR32 uses in `dsp/engines/`.
- Shared DSP primitives (wavetable oscillator read, envelope generators, `fast_tanh`, TPT SVF) live in `dsp/common.c` and are called by every model — avoid duplicating oscillator code across 10 files.

**Reference:** DR32 stores `plugin_api_v2_t *engine_api` + `void *engine_instance` per pad and loops calling `engine_api->render_block`. Omega uses the same *shape* of indirection but inward — a lightweight internal `model_vtable_t` instead of the full plugin API, because the models are compiled in, not `dlopen`ed.

---

## 3. UI Hierarchy: Dynamic JSON Assembly

**Recommendation:** hybrid — static string fragments composed into a pre-allocated scratch buffer (`ui_scratch[8192]`) at `get_param("ui_hierarchy")` time, with the Kick Page 2 section selected by active model.

Rationale: The hierarchy is 95% static (root macros, Kick Page 1, Groove Page 1, Performer, preset pages), but Kick Page 2's 6 model-specific slots and the GEN-only Groove Page 2 must change with the active model. A fully static string can't express context-sensitivity; full runtime JSON construction (per-key `snprintf` of every param) is error-prone and slow. The middle path:

```c
int omega_get_param(void *inst_v, const char *key, char *buf, int len) {
    omega_instance_t *i = inst_v;
    if (strcmp(key, "ui_hierarchy") == 0) {
        char *p = i->ui_scratch; int rem = UI_SCRATCH_BYTES;
        p += append(p, &rem, UI_HEADER_STATIC);        // root + macros + kick pg1 + groove pg1 + perf
        // Model-specific Kick Page 2 built from the active model's p2_slots descriptors:
        const model_vtable_t *m = MODEL_TABLE[i->kick.active];
        p += append_p2_page(p, &rem, m);               // FX TYPE, FX AMT + 6 slots from m->p2_slots
        if (i->kick.active == MDL_GEN)
            p += append(p, &rem, GROOVE_PAGE2_GEN);     // SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY
        p += append(p, &rem, UI_FOOTER_STATIC);        // preset load/save pages
        return (int)(p - i->ui_scratch);
    }
    ...
}
```

- `p2_slot_desc_t` (per-model, in each model's vtable) carries `{key, name, type, min, max}` so the JSON is generated from data, not hand-written per model. This keeps the 10 models' Page-2 layouts co-located with their DSP.
- `get_param` runs on the audio thread, so this must not allocate — hence the pre-allocated `ui_scratch`. Assembly is a handful of `memcpy`/`snprintf` into a fixed buffer, cheap and RT-safe. Schwung re-queries `ui_hierarchy` when the model enum changes, so returning model-dependent JSON is the correct mechanism to swap Page 2.

**Confidence:** MEDIUM — the exact re-query trigger cadence isn't documented in the Context files; verify against `schwung/docs/MODULES.md` during the UI phase. If Schwung caches `ui_hierarchy` aggressively, may need to signal a refresh via a param write.

---

## 4. Bidirectional Macros

**Recommendation:** treat the 8 macros as *aliases* over canonical parameters, with a single shared write path. Both the macro key and the underlying key funnel into one internal setter; a re-entrancy guard prevents loops.

Macro map (from PROJECT.md): MODEL, MASTER VOL, PITCH, LENGTH, RUMBLE VOL, TAP1, DUCK, DJ FILT.

```c
// One internal setter is the ONLY place that mutates a canonical value.
static void set_canonical(omega_instance_t *i, int param_id, float v, int updating_macro);

void omega_set_param(void *inst_v, const char *key, const char *val) {
    omega_instance_t *i = inst_v; float v = atof(val);
    int macro = macro_index_for_key(key);          // -1 if not a macro key
    if (macro >= 0) {
        i->macro_val[macro] = v;
        set_canonical(i, MACRO_TARGET[macro], v, /*updating_macro=*/1);  // macro → underlying
    } else {
        int pid = param_index_for_key(key);
        set_canonical(i, pid, v, /*updating_macro=*/0);
        int m = macro_for_param(pid);              // -1 if this param isn't a macro target
        if (m >= 0) i->macro_val[m] = v;           // underlying → macro mirror (no re-dispatch)
    }
}
```

- Store the macro mirror (`macro_val[8]`) as plain state; `get_param` on a macro key returns `macro_val[m]`, so the encoder shows the live value regardless of which path last moved it.
- The guard is the `updating_macro` flag (or simply: the underlying-→-macro branch updates the mirror *directly* without re-calling `set_param`, so there is no recursion by construction).
- Scaling: some macros need range remap (e.g. MODEL is an enum 0–9; DJ FILT is bipolar). Keep a small `macro_scale[8]` transform so the macro's 0–1 encoder maps correctly onto the target param's native range and back.
- `MODEL` macro is special: writing it changes `kick.active`, which changes the UI hierarchy (§3) — ensure the model-switch path also re-triggers the active model's state init.

**Confidence:** MEDIUM — no reference implementation of bidirectional macros observed in the Schwung repos; pattern derived from the `set_param`/`get_param` contract. Validate encoder round-trip behavior on-device early.

---

## 5. Preset I/O Threading

**Recommendation:** load-all-at-init + deferred-save-flag. All filesystem work happens off the audio thread.

`create_instance(module_dir, json_defaults)` is documented as running on the audio thread, BUT it runs once at load, before real-time streaming for this instance is critical, and it is the sanctioned place for setup. The safe pattern:

- **Load:** In `create_instance`, enumerate `<module_dir>/presets/*.json` (or a single `presets.dat` bank), parse each into `preset_bank_t.slot[]`. This is the "init-time enumeration" PROJECT.md calls for. After init, the audio thread only *reads* the in-memory bank — applying a preset = copying `preset_t.values` into the param_store, zero I/O.
- **Apply (runtime):** `set_param("preset_select", n)` → memcpy `presets.slot[n]` into canonical state + push to DSP. Pure memory, RT-safe.
- **Save (runtime):** The audio thread must NOT write files. Use a deferred handshake: `set_param("preset_save"/"preset_save_as", name)` sets `presets.save_request` + snapshots current param_store into a staging `preset_t`. The actual `fopen`/`fwrite` is performed **outside** the audio callback.

The open question is *who* performs the deferred write. Options, in order of preference:
1. If Schwung provides any non-RT callback or the host polls `get_param`, drain `save_request` there.
2. If not, the write can piggyback on the *next* `set_param`/`get_param` that Schwung issues from its UI thread — but only if those are guaranteed non-RT. **This must be verified** against `docs/MODULES.md`; the Context doc states `set_param`/`get_param` run on the audio thread, which would forbid it.
3. Worst case: spawn a dedicated low-priority writer thread in `create_instance` that sleeps on a condition/flag and does the file write. A pthread is allowed (it's created off the RT thread and never touched by it beyond an atomic flag store).

**Recommendation:** design around option 3 (init-time writer thread + atomic flag) as the safe default, since it doesn't depend on undocumented host thread guarantees. Confirm thread creation is acceptable on Move during the preset phase.

**Confidence:** MEDIUM — threading model for saves depends on undocumented host behavior. The load-at-init read path is HIGH confidence and directly supported.

---

## 6. Memory Budget

Per-instance `create_instance` heap:

| Allocation | Size | Notes |
|-----------|------|-------|
| Groove delay buffer L+R | 705,600 B (~689 KB) | 88,200 × 2 ch × 4 B float — the dominant cost |
| Preset bank (64 slots) | ~55 KB | 64 × (name + ~200 params × 4 B) |
| Per-model state array | ~5–20 KB | union-sized to largest model (PHY/FM4 hold most state) |
| Scratch (kick+groove) | 2 KB | float, 128-frame stereo |
| UI scratch | 8 KB | dynamic ui_hierarchy assembly |
| Misc (params, filters, macros) | < 5 KB | |
| **Per-instance total** | **≈ 760–780 KB** | |

**Wavetable banks are NOT counted per-instance** — they are `static const` in `.rodata`, loaded once for the whole `dsp.so`, shared by every instance. Estimate: 10 models × a few single-cycle tables (e.g. 8 tables × 2048 samples × 4 B ≈ 64 KB per model) ≈ **~500 KB–1 MB of shared const wavetable data**. If USR loads a user WAV, that buffer IS per-instance and must be bounded (recommend a fixed cap, e.g. 128 KB, allocated in the instance block, refused if larger).

**Multi-instance flag (IMPORTANT):**
- 1 instance: ~0.78 MB + ~1 MB shared tables ≈ under 2 MB. Fine.
- 16 DR32 pads all running Omega: 16 × 0.78 MB ≈ **12.5 MB** + 1 MB shared. Likely fine, but not free.
- 16 Movy tracks: same ~12.5 MB. If a user runs DR32 *and* Movy tracks with many Omega instances, RAM could climb toward ~25 MB.

Move has no published hard per-process ceiling in the Context docs, but it is a constrained embedded device shared with stock firmware. **Flag:** the 705 KB delay buffer is the multiplier that hurts at scale. Two mitigations to evaluate during the groove phase:
- Right-size the delay: 2 s at 44.1 kHz assumes very slow tempi. 4 taps at 16th notes down to ~60 BPM need ~1 s (44,100 frames) → halves the buffer to ~350 KB. Verify the slowest supported tempo before committing to 88,200.
- Mono groove option already exists (MONO toggle) — but the buffer must still be allocated for stereo unless mono is compile-time. Keep stereo buffer; MONO just sums on read.

**Recommendation:** allocate 88,200 for v1 correctness, but add a `TODO` to measure real RAM headroom on-device and consider trimming to 44,100 if multi-instance pressure appears. Confidence on the exact ceiling: MEDIUM (no published Move figure).

---

## 7. Build Order (Dependency Chain)

Build inner→outer, DSP-first, so each layer is audible/testable before the next depends on it. UI and presets come last because they orchestrate already-working DSP.

```
Phase A: Skeleton + one model (proves the whole pipeline end-to-end)
  ├─ module.json + move_plugin_init_v2 + vtable stubs
  ├─ omega_instance_t + create/destroy (single calloc)
  ├─ dsp/common.c: wavetable osc, envelopes, fast_tanh, TPT SVF primitive
  ├─ models/model_api.h + registry + FM2 only  (simplest accurate model)
  └─ render_block: FM2 → int16 out. Deploy, hear a kick.  ← first on-device milestone

Phase B: Model dispatch complete
  └─ Implement remaining 9 models behind the vtable, one file each.
     Order by reuse: FM4 (extends FM2) → WTR/HRD/DIG/TRS/ANA (wavetable+transient family,
     share common.c) → ANA sub-osc → USR (adds bounded user-WAV load, filesystem) →
     PHY (damped-oscillator model) → GEN (sequencer state, feeds Groove Pg2).
     Universal Kick Page 1 params wired through kick_common_t.

Phase C: Groove rumble
  └─ groove_state_t 4-tap delay + color LPF + MONO. Reads kick scratch from Phase A/B.
     GEN-model sequencer hooks (Groove Page 2 state) added here.

Phase D: Performer
  └─ Sidechain duck env (driven by kick amplitude), DJ filter (TPT SVF sweep + reso),
     end-of-chain soft clipper. Completes the render pipeline.

Phase E: UI hierarchy
  └─ ui.c: static fragments + model-context Page 2 assembly into ui_scratch.
     set_param/get_param full key dispatch. Now every param is reachable from encoders.

Phase F: Bidirectional macros
  └─ macro_val[8] mirror + shared set_canonical path + scale table. Depends on E's
     full key dispatch existing.

Phase G: Presets
  └─ preset_bank_t load-at-init enumeration; apply (RT memcpy); deferred-save flag +
     writer thread. Depends on the complete param_store from A–F to snapshot/restore.
```

**Rationale for ordering:**
- DSP before UI: you can drive params via `set_param` from a test harness / debug log before any encoder UI exists, so models are validated without the UI layer.
- One model before ten: the vtable + render pipeline + int16 output is the riskiest integration; prove it once (FM2), then the other 9 are parallelizable, low-risk fill-in.
- Groove/Performer before UI: they only need the kick scratch signal, not the UI. Get the full audio chain correct first.
- Macros after full key dispatch (E): macros alias canonical keys, so those keys must exist first.
- Presets last: a preset snapshots the *entire* finished param set — building it before the params are finalized means constant schema churn.

**Cross-host validation** (Schwung slot / DR32 pad / Movy track) should be exercised at the end of Phase A (skeleton loads everywhere) and re-checked after Phase E (UI hierarchy auto-renders in Movy). No code differs per host — the same `dsp.so` must load in all three, so test all three as soon as the skeleton exists.

---

## Anti-Patterns to Avoid

### Nesting an external module as the voice (v1)
**What:** The reference `04_BOHM_SCHWUNG_MODULE_DESIGN.md` holds `inner_kick_instance` + `inner_kick_api` and calls a sub-module's `render_block`.
**Why bad for v1:** requires `dlopen` + runtime module enumeration — explicitly deferred to v2 in PROJECT.md, and adds RT-thread and lifecycle hazards.
**Instead:** compile all 10 models in, dispatch via internal `model_vtable_t`.

### int16 round-trip inside the signal path
**What:** rendering the voice to int16, then dividing by 32768 back to float for further processing (as the reference render_block does).
**Why bad:** loses precision and adds pointless conversions on every sample.
**Instead:** keep the internal path float; convert to int16 exactly once at output.

### Giant switch in render_block for 10 models
**Why bad:** couples all models into one function, defeats isolated testing and incremental build order.
**Instead:** function-pointer vtable (matches DR32 `dsp/engines/` and Forge algorithm dispatch).

### Any malloc / file I/O on the audio thread
**What:** allocating scratch, building JSON with dynamic buffers, or writing preset files inside `render_block`/`set_param`/`get_param`.
**Why bad:** violates the hard Schwung RT rule; causes audio dropouts/xruns on SCHED_FIFO.
**Instead:** pre-allocate all buffers in `create_instance`; assemble UI JSON into fixed `ui_scratch`; defer preset writes to a non-RT thread via an atomic flag.

---

## Scalability Considerations

| Concern | 1 instance | 16 instances (DR32 or Movy) | 32 instances (DR32 + Movy heavy) |
|---------|-----------|-----------------------------|----------------------------------|
| RAM | ~0.78 MB + 1 MB shared tables | ~12.5 MB + 1 MB | ~25 MB + 1 MB — flag, verify headroom |
| CPU (10–15% budget each) | fine | must stay within total core-3 budget; PHY/GEN are the heaviest — keep hybrid-DSP simplifications | likely exceeds budget; user-managed |
| Wavetable data | shared `.rodata`, cost-once | cost-once (not multiplied) | cost-once |
| Groove delay buffer | 705 KB | multiplied 16× → the RAM driver | consider 44,100-frame trim |

---

## Sources

- `Context/01_SCHWUNG_DEV_ARCHITECTURE.md` — plugin_api_v2_t, RT rules, ui_hierarchy schema, DR32 `dr32_pad_t` dispatch, Movy auto-UI. (HIGH)
- `Context/04_BOHM_SCHWUNG_MODULE_DESIGN.md` — reference instance struct, render_block, set_param dispatch (used as baseline, EXT nesting superseded). (HIGH)
- `Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` — rumble signal chain, pitch envelope, soft-clip/wavefold formulas, ducking curve. (HIGH)
- `.planning/PROJECT.md` — 10-model scope, EXT deferral, memory constraint (~700 KB delay), macro set, cross-host requirement. (HIGH)
- DR32 repo README (github.com/legsmechanical/schwung-dr32) — per-pad engine instances, `dsp/engines/` section-per-engine layout. (MEDIUM — README-level, source not read)
- Forge repo README (github.com/filliformes/forge-move) — 5-algorithm table-driven dispatch, algorithm-dependent knob labels, binary kit persistence with magic header. (MEDIUM — README-level)
- Move per-process memory ceiling: no published figure; budget estimated from device constraints. (LOW — needs on-device measurement)
