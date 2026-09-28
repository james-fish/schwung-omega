# Phase A: Foundation + FM2 Model - Research

**Researched:** 2026-09-28
**Domain:** Native C11 Schwung `plugin_api_v2_t` sound-generator module for Ableton Move (aarch64/glibc 2.35); 2-op wavetable FM kick DSP; Docker cross-compilation; offline WAV/malloc-trap test harness
**Confidence:** HIGH (API contract, DSP, toolchain all verified against Context/ files + official Schwung docs; only `-mcpu` core ID and exact `ui_hierarchy` buf_len remain empirical unknowns, both explicitly deferred to on-device spike per D-10/D-15)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Source Code Organization**
- **D-01:** Modular split from day one — do not start with a single file. File layout:
  - `src/dsp.c` — plugin entry point (`move_plugin_init_v2`), `create_instance`, `destroy_instance`, `render_block`, `set_param`, `get_param`, `on_midi`; no DSP math here
  - `src/models/fm2.c` — FM2 engine: trigger, render, set_p2, p2_slot_desc
  - `src/models/model_registry.c` — vtable registry array (model_id_t → kick_model_vtable_t); Phase B just adds entries here
  - `src/dsp_primitives.c` + `src/dsp_primitives.h` — all shared DSP building blocks (wavetable oscillator, exponential decay envelope, TPT SVF filter stub)
  - `src/ui.c` — ui_hierarchy JSON string fragments
  - `src/omega.h` — shared types: `bohm_instance_t`, `kick_model_vtable_t`, `model_id_t`, param key macros
- **D-02:** All headers are `src/*.h` (flat); source files under `src/` and `src/models/`. No deeper nesting in Phase A.

**FM2 Engine**
- **D-03:** FM2 must be **fully complete** in Phase A — all 8 Kick Page 1 parameters (PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR) and all 3 FM2-specific Kick Page 2 parameters (FM RATIO, FM INDEX, OP2 WAVE) fully wired and audibly functional. No stubs.
- **D-04:** Carrier and modulator oscillators read from a **shared 2048+1-sample `static const float` sine wavetable** in `.rodata` (defined in `dsp_primitives.c`). Guard sample at index 2048 equals index 0 — linear interpolation at wrap needs no branch. Both FM2 oscillators, and all future models, read the same table. Do not call `sinf()` per sample.
- **D-05:** Envelopes (pitch sweep, FM index decay, amplitude) use a **shared one-pole exponential decay struct** defined in `dsp_primitives.h`: `typedef struct { float value; float coeff; } env_t;`. `env_trigger(e, target, decay_coeff)` and `env_tick(e)` live in `dsp_primitives.c`. All FM2 envelopes and all Phase B+ model envelopes use this same struct.
- **D-06:** CURVE parameter maps to the 808↔909 pitch sweep shape (per KICK-12). Implement as a blend between two pitch envelope curves (fast exponential vs. slow linear/log decay) rather than a single fixed shape.
- **D-07:** COLOR parameter drives a TPT low-pass filter on the FM2 output (cutoff mapped from COLOR value). Phase A can use a simplified single-pole TPT for COLOR — the full SVF is Phase D. Define the TPT 1-pole in `dsp_primitives.c` to be reused by all models and Groove.

**UI Hierarchy**
- **D-08:** Phase A delivers a **real minimal `ui_hierarchy` JSON** — not a stub. Must include Kick Page 1 (8 encoder slots: PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR) and FM2 Kick Page 2 (3 model-specific slots: FM RATIO, FM INDEX, OP2 WAVE + FX TYPE + FX AMT placeholders). Use real param key strings matching `set_param`/`get_param` dispatch.
- **D-09:** UI hierarchy stored as a **pre-serialized static C string** in `src/ui.c`. No JSON library. Zero allocation in `get_param`. Served from `get_param("ui_hierarchy", buf, buf_len)` via `strncpy` (or `memcpy` + null-terminate) into the provided buffer.
- **D-10:** Phase A also measures and logs the actual `buf_len` passed by each host to `get_param("ui_hierarchy")` on first call — write the value to the host's log function. This measurement unblocks Phase E's full hierarchy sizing.

**CI and Testing**
- **D-11:** **GitHub Actions** CI pipeline. On push: (1) Docker cross-compile step runs `make` inside `ghcr.io/charlesvestal/schwung-builder:latest`, (2) `objdump -T build/dsp.so | grep GLIBC` gate fails the build if any symbol references GLIBC > 2.35, (3) native `make test` runs the offline harness.
- **D-12:** Offline test harness (`tests/test_render.c`, compiled natively with `cc`): creates instance via mock `host_api_v1_t`, triggers FM2, renders 512 blocks (≈1.5s), writes to `tests/output/fm2_kick.wav` (44-byte PCM header + int16 data). Asserts: every output sample passes `isfinite()`, magnitude stays ≤ 1.0f before int16 conversion, and zero `malloc`/`free`/`calloc` called during `render_block`.
- **D-13:** Malloc trap: in the test build (`-DOMEGA_MALLOC_TRAP`), `malloc`/`calloc`/`realloc`/`free` are replaced via interposition with functions that call `abort()` after `render_block` initialization is complete. A global `bool g_audio_thread_active` flag gates the trap (false during `create_instance`, true during `render_block`).
- **D-14:** Deployment target: Move hardware is available. Deploy script `scripts/deploy.sh` uses scp + atomic rename (upload to `dsp.so.new`, then `mv dsp.so.new dsp.so` on device). Phase A success includes loading and triggering on-device in all 3 host contexts.
- **D-15:** Identify the exact Move SoC / Cortex core in Phase A (via `/proc/cpuinfo` on-device or Schwung docs). If confirmed Cortex-A53, document for potential `-mcpu=cortex-a53` flag — do not bake it in until confirmed.

### Claude's Discretion
- Phase A FX chain: FX TYPE and FX AMT appear in the FM2 Page 2 UI but the 5 FX modes are Phase B scope (KICK-14). Phase A can wire FX TYPE/AMT to a passthrough (identity, no processing) with the correct param keys in place.
- Exact `env_t` coefficient formula (time-constant-based vs. sample-count-based) — choose whichever gives cleaner `set_param` mapping to LENGTH/ATTACK/TRS DEC.
- Makefile structure: Phased targets (`dsp.so`, `test`, `clean`, `deploy`) with separate aarch64 and native compiler variables. No additional constraints beyond CLAUDE.md.

### Deferred Ideas (OUT OF SCOPE)
None — discussion stayed within phase scope. (Out of scope for the phase overall: any model beyond FM2; full nav tree [Phase E]; Groove/Performer; Presets; the 5 real FX modes [Phase B].)
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| FNDTN-01 | Implements `plugin_api_v2_t`; loads unmodified in Schwung slot, DR32 pad, Movy track | §API Contract — one exported `move_plugin_init_v2`, identical vtable across all three hosts; host differences are trigger/param routing only |
| FNDTN-02 | `module.json` manifest: `component_type: sound_generator`, `pad_layout: drums`, `api_version: 2`, id `omega` | §module.json — verified required + capability fields |
| FNDTN-03 | Zero audio-thread allocation; malloc-trap debug build aborts | §Test Harness (malloc-trap), §Memory Layout — single calloc in `create_instance` |
| FNDTN-04 | Cross-compiled aarch64/glibc 2.35 via pinned Docker; `objdump -T` GLIBC gate | §Cross-Compilation — exact flags + gate command |
| FNDTN-05 | FPCR flush-to-zero set explicitly in `render_block` | §Cross-Compilation (FPCR FTZ/DAZ snippet, bit 24) |
| FNDTN-06 | Offline mock-host + render-to-WAV harness | §Test Harness |
| FNDTN-07 | Output stage: clamp + isfinite + lrintf before int16 | §DSP (output stage), §Common Pitfalls (missing clamp) |
| KICK-01 | Model dispatcher vtable; append-only permanent model IDs | §Architecture Pattern 1 (vtable registry) |
| KICK-02 | FM2 model: 2-op wavetable FM, FM index decay env, Page 2 = FM RATIO/FM INDEX/OP2 WAVE | §FM2 DSP Recipe |
| KICK-12 | Universal Kick Page 1 (8 encoders) present | §Bohm Parameter Map, §UI Hierarchy |
| KICK-15 | Wavetables `static const float` in `.rodata`; linear interp with 2048+1 guard | §Architecture Pattern 2 (wavetable), D-04 |
</phase_requirements>

## Summary

Phase A is a full vertical slice: it establishes the entire C11 architecture (modular file layout, vtable model dispatch, shared DSP primitives, `.rodata` wavetable), a complete FM2 kick engine, a real minimal `ui_hierarchy`, the cross-compilation toolchain with its glibc gate, all RT-safety primitives (single-calloc instance, FPCR flush-to-zero, clamped int16 output, no audio-thread allocation), and an offline WAV/malloc-trap test harness. Everything downstream (9 more models, Groove, Performer, UI, macros, presets) plugs into the scaffolding proven here.

The Schwung API is small and fully specified. `move_plugin_init_v2(const host_api_v1_t*)` returns a static `plugin_api_v2_t*` vtable; the host then drives `create_instance` → `set_param`/`on_midi`/`get_param`/`render_block` → `destroy_instance`. **Critically, every one of these calls runs on the SCHED_FIFO-70 SPI audio thread** (verified against official Schwung MODULES.md), so the "no allocation / no I/O / no blocking" rule applies to *all* of them, not just `render_block` — including `create_instance`, where the CLAUDE.md single-calloc pattern is nonetheless permitted (the one allocation happens once, off the hot path). The three host contexts (Schwung slot, DR32 pad, Movy track) all consume the identical `dsp.so` + `module.json`; they differ only in how they route triggers and parameters, never in the ABI. FM2 is a textbook 2-op FM kick: modulator phase → scaled by FM-index envelope → offsets carrier phase; carrier reads the shared sine table; a pitch envelope (CURVE blends 808 slow-decay vs 909 fast-sweep) drives the carrier frequency; an amplitude envelope shapes the tail; a TPT 1-pole lowpass (COLOR) rolls off the top. All oscillators read one shared 2049-sample `static const float` sine table in `.rodata`.

**Primary recommendation:** Build the scaffolding first (omega.h types → dsp_primitives → vtable registry → dsp.c entry points → module.json → Makefile → mock-host harness), get silence-clean end-to-end (create/render/destroy with malloc-trap green and clamped int16), *then* fill in the FM2 DSP and the real `ui_hierarchy`. This ordering means the hardest RT-safety and toolchain risks are retired before any DSP subjectivity enters.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C | C11 (`-std=gnu11`) | Implementation language | Schwung API is a C ABI; C++ out of scope. C11 gives `_Static_assert`, `_Alignas`, anonymous unions, `<stdbool.h>`, `<stdint.h>` (CLAUDE.md, HIGH) |
| aarch64-linux-gnu-gcc | schwung-builder image default (GCC 10+) | Cross-compiler | Provided by pinned Docker image; matches on-device glibc 2.35 |
| GNU Make | any | Build orchestration | Single `dsp.so` + native test target; matches reference `build.sh` |
| libm | glibc 2.35 | `expf`/`lrintf`/`isfinite`/`sinf` (build-time table gen) | Link `-lm`. Per-sample transcendentals avoided by wavetable + precomputed coeffs |

### Supporting (test/build only)
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| native cc/clang (host) | system | Compile `tests/test_render.c` natively | Offline WAV render + malloc-trap; validates logic/sound, not aarch64 codegen |
| GitHub Actions | current | CI: Docker build + glibc gate + native test | D-11 |
| Docker | current | Reproducible cross-compile | `ghcr.io/charlesvestal/schwung-builder:latest` |

**No third-party runtime libraries.** No JSON library (D-09: pre-serialized static string). No test framework (plain C `assert` + tiny runner is sufficient — CLAUDE.md).

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| C11 | C99 | Loses `_Static_assert`/`_Alignas`/anonymous unions; zero cost to keep C11 on GCC/aarch64 (CLAUDE.md) |
| Wavetable sine lookup | per-sample `sinf()` | `sinf` slower + can produce denormals; D-04 mandates shared table |
| Makefile | CMake | Overkill for one artifact; adds indirection (CLAUDE.md) |
| Plain C asserts | Unity/GoogleTest | C++ frameworks pull out of C; unnecessary weight |

**Installation / build (verify image digest before pinning in CI per FNDTN-04):**
```bash
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/charlesvestal/schwung-builder:latest make dsp.so
```

## Architecture Patterns

### Recommended Project Structure (matches D-01/D-02 exactly)
```
src/
├── omega.h              # bohm_instance_t, kick_model_vtable_t, model_id_t, PARAM_KEY_* macros
├── dsp.c                # move_plugin_init_v2 + 6 vtable fns; dispatch only, no DSP math
├── dsp_primitives.h     # env_t, osc phase helpers, tpt1_t; sine table extern decl
├── dsp_primitives.c     # g_sine_table[2049] .rodata, env_trigger/tick, wt_read, tpt1_process
├── ui.c                 # pre-serialized ui_hierarchy static string(s)
└── models/
    ├── model_registry.c # g_models[] : model_id_t -> kick_model_vtable_t
    └── fm2.c            # fm2_trigger / fm2_render / fm2_set_p2 / fm2_p2_slot_desc
module.json              # manifest (id "omega", sound_generator, drums, api_version 2)
Makefile                 # targets: dsp.so (aarch64), test (native), clean, deploy
scripts/
├── build.sh             # docker wrapper
└── deploy.sh            # scp .new + atomic rename (D-14)
tests/
├── test_render.c        # mock host, trigger, 512-block render, WAV, assertions
├── mock_host.c/.h       # host_api_v1_t stub
└── output/              # fm2_kick.wav (gitignored)
.github/workflows/ci.yml # docker build + objdump gate + native test
```

### Pattern 1: vtable model dispatch (KICK-01)
**What:** A `kick_model_vtable_t` of four function pointers per model; a registry array indexed by `model_id_t`. `dsp.c` calls through the registry, never directly into `fm2.c`.
**When to use:** All model calls (trigger, render, set_p2, p2_slot_desc).
**Model IDs are append-only and permanent** — never renumber (KICK-01). Assign `FM2 = 0` now; Phase B appends `FM4=1, WTR=2, …`.
```c
// omega.h  — signatures the planner should lock; adjust arg lists to taste but keep stable
typedef struct bohm_instance bohm_instance_t;   // fwd decl

typedef struct {
    const char *name;                                   // "FM2"
    void (*trigger)(bohm_instance_t *inst, int note, int velocity);
    void (*render)(bohm_instance_t *inst, float *out_l, float *out_r, int frames);
    void (*set_p2)(bohm_instance_t *inst, const char *key, const char *val);
    int  (*p2_slot_desc)(bohm_instance_t *inst, char *buf, int buf_len); // JSON fragment for Page 2
} kick_model_vtable_t;

typedef enum { MODEL_FM2 = 0, MODEL_COUNT } model_id_t;  // append-only

// model_registry.c
extern const kick_model_vtable_t g_fm2_vtable;           // defined in fm2.c
const kick_model_vtable_t *g_models[MODEL_COUNT] = { &g_fm2_vtable };
```
Note: engine renders into **float** L/R scratch (not int16). CLAUDE.md warns against the reference module's int16 scratch (double conversion). The int16 boundary lives only at `render_block`'s final write.

### Pattern 2: shared `.rodata` wavetable (KICK-15, D-04)
**What:** One flat `static const float _Alignas(16) g_sine_table[2049]` in `dsp_primitives.c`; index 2048 duplicates index 0 (guard sample) so linear interp at wrap needs no branch/mask on the `+1` read.
```c
// dsp_primitives.c — generate at build time or with a designated initializer/loop is NOT allowed
// for const; use a generated header. Simplest Phase-A path: a small generator writes sine_table.h.
static const float _Alignas(16) g_sine_table[2049] = { /* generated: sinf(2*PI*i/2048), i=0..2048 */ };

static inline float wt_read(const float *t, float phase01) {   // phase01 in [0,1)
    float fp = phase01 * 2048.0f;
    int   i  = (int)fp;                 // 0..2047
    float fr = fp - (float)i;
    return t[i] + fr * (t[i+1] - t[i]); // t[2048]==t[0], no branch
}
```
Phase representation: keep a `float phase` in `[0,1)` per operator, increment by `freq/SR`, wrap with `if (phase >= 1.f) phase -= 1.f;`. (The `uint32` phase-accumulator trick from CLAUDE.md is a valid alternative but unnecessary at kick fundamentals.)

### Pattern 3: shared one-pole envelope (D-05)
```c
// dsp_primitives.h
typedef struct { float value; float coeff; } env_t;

// dsp_primitives.c
static inline void  env_trigger(env_t *e, float start, float coeff){ e->value=start; e->coeff=coeff; }
static inline float env_tick(env_t *e){ float v=e->value; e->value*=e->coeff; return v; }
// time_ms -> per-sample decay coeff (discretion D: time-constant form, matches CONTEXT specifics)
static inline float env_coeff_from_ms(float time_ms){
    return expf(-1.0f / (time_ms * 0.001f * OMEGA_SR)); // OMEGA_SR = 44100.0f
}
```
This one struct serves the pitch-sweep, FM-index-decay, and amplitude envelopes, and every Phase B+ model. For an attack (rising) segment, either invert (`value` ramps up toward target = 1 − decaying) or run a second env; keep it simple in Phase A.

### Pattern 4: TPT 1-pole lowpass (D-07, COLOR)
**What:** Zavalishin topology-preserving 1-pole LP; ~a few multiplies/sample; the reusable stub that Phase D upgrades to a full SVF.
```c
// dsp_primitives.h
typedef struct { float s; } tpt1_t;   // single integrator state
// cutoff_hz mapped from COLOR; g = tanf(pi*fc/SR) precomputed per set_param, not per sample
static inline float tpt1_lp(tpt1_t *f, float x, float g){
    float v = (x - f->s) * (g / (1.0f + g));
    float y = v + f->s;
    f->s = y + v;
    return y;
}
```

### Anti-Patterns to Avoid (all from CLAUDE.md "five reference bugs" + Context 04)
- **int16 scratch between engine and mixer:** double int16↔float conversion. Use float scratch; int16 only at final output.
- **Unclamped `(int16_t)(x * 32767.0f)`:** the reference `bohm_render_block` does exactly this (Context 04 lines 152–153) — wraps on overflow. Use clamp + `isfinite` + `lrintf` (FNDTN-07).
- **`atof`/`atoi` for params:** the reference `bohm_set_param` uses `atof` (Context 04 line 165) — locale-dependent. Use a locale-independent parser (UI-01 requires it project-wide; establish the habit in Phase A).
- **Zero-margin scratch buffers:** reference uses `int16_t scratch[256]` (exactly 2×128). Add a `MAX_BLOCK` margin; size float scratch generously.
- **Relying on fast-math flags to enable FTZ:** set FPCR FZ bit explicitly in `render_block` (FNDTN-05).

## FM2 DSP Recipe (KICK-02, KICK-12)

Concrete per-note signal flow. All frequencies in Hz, phases in `[0,1)`, `SR = 44100`.

**Parameters (Kick Page 1 + FM2 Page 2):**
- PITCH → fundamental `f0` (map to ~30–200 Hz; Bohm spec sub-C1..C3, Context 02).
- LENGTH → amplitude envelope decay time_ms (long sub-boom ↔ short click).
- SUSTAIN → tail amplitude contour after attack (Context 02: "amplitude contour of body tail").
- CURVE → 808↔909 pitch-sweep blend (see below; KICK-12, D-06).
- ATTACK → attack start time / click amplitude.
- TRS DEC → transient decay 0–30 ms (short attack transient layer).
- TRS TNE → transient brightness (HF filter on the click).
- COLOR → TPT 1-pole LP cutoff on FM2 output (D-07).
- FM RATIO → modulator freq = `ratio * carrier freq` (typical 0.5–8; integer ratios = harmonic, non-integer = metallic).
- FM INDEX → peak modulation depth, scaled by its own decay envelope.
- OP2 WAVE → modulator wave select (Phase A: at minimum sine; can expose a couple of table variants or a shaping factor — keep within the shared sine table + simple transforms).

**Per-sample render (carrier + modulator both read `g_sine_table`):**
```c
// pseudo-C, inside fm2_render loop over frames
float pitch  = env_tick(&fm->pitch_env);          // 0..1 pitch-sweep amount (see CURVE blend)
float fcar   = fm->f0 + pitch * fm->sweep_hz;      // carrier freq sweeps down to f0
float fmod   = fm->ratio * fcar;                   // modulator tracks carrier
float idx    = env_tick(&fm->index_env) * fm->fm_index; // FM index has its OWN decay env (KICK-02)

fm->mod_phase += fmod / OMEGA_SR;  if (fm->mod_phase >= 1.f) fm->mod_phase -= 1.f;
float mod_out  = wt_read(g_sine_table, fm->mod_phase);      // OP2 wave

float car_ph   = fm->car_phase + idx * mod_out;             // phase-modulate carrier
car_ph -= floorf(car_ph);                                   // wrap into [0,1)
float car_out  = wt_read(g_sine_table, car_ph);

fm->car_phase += fcar / OMEGA_SR;  if (fm->car_phase >= 1.f) fm->car_phase -= 1.f;

float amp      = env_tick(&fm->amp_env);                    // LENGTH/SUSTAIN shaped
float click    = fm->trs_amp * env_tick(&fm->trs_env);      // ATTACK/TRS transient layer
float sample   = car_out * amp + click;                     // (click optionally HP/bright per TRS TNE)
sample = tpt1_lp(&fm->color_lp, sample, fm->color_g);       // COLOR
out_l[n] = out_r[n] = sample;                               // mono kick, duplicated
```

**CURVE 808↔909 blend (D-06, Context 03 §4.1):** the 909 sweep is exponential (`f(t)=f_start·e^{-t/τ}+f_fund`, τ≈15 ms, Context 03) — fast and steep; the 808 is a slow, near-linear/log decay. Implement two pitch envelopes (or two decay coeffs) and crossfade by CURVE:
```c
// at trigger: derive two coeffs, blend the resulting per-sample pitch value
float c909 = env_coeff_from_ms(15.0f);        // fast
float c808 = env_coeff_from_ms(300.0f);       // slow boom (tune by ear)
// CURVE in [0,1]: 0 = 808 (slow), 1 = 909 (fast)  (Context 02: CCW=808, CW=909)
// simplest: pick blended coeff = mix(c808, c909, curve); or run both envs and lerp their outputs
```
Run both envelopes and lerp their outputs for a smoother morph; a single blended coefficient is the cheaper acceptable v1.

**`env_t` coefficient formula (discretion D, from CONTEXT specifics):**
```c
coeff = expf(-1.0f / (time_ms * 0.001f * 44100.0f));
```
Time-constant form chosen because LENGTH/ATTACK/TRS DEC are all naturally expressed in ms and map cleanly in `set_param`.

**Output stage (FNDTN-07) — the single int16 boundary:**
```c
static inline int16_t to_i16(float x){
    if (!isfinite(x)) x = 0.0f;
    if (x >  1.0f) x =  1.0f;
    if (x < -1.0f) x = -1.0f;
    return (int16_t)lrintf(x * 32767.0f);
}
```

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Sine generation | per-sample `sinf` calls | shared `.rodata` table + linear interp (D-04) | Faster, denormal-free, deterministic |
| Denormal handling | manual `if (fabsf(x)<1e-15)` flushes everywhere | FPCR FZ bit once per `render_block` (FNDTN-05) | Hardware flush is free and total |
| Swept lowpass | naive biquad w/ per-sample coeff recompute | TPT 1-pole (D-07) | Biquad zippers/instability under modulation |
| Float→int16 | `(int16_t)(x*32767)` | clamp+isfinite+lrintf (FNDTN-07) | Overflow wraps to opposite polarity → clicks |
| Param string parse | `atof`/`strtod` | locale-independent parser | Locale-dependent decimal separators (UI-01) |
| JSON serialization | a JSON library | pre-serialized static string (D-09) | Zero alloc in `get_param`; string is fixed |
| Instance allocation | many mallocs | single calloc of whole struct (CLAUDE.md) | One alloc, freed once; no fragmentation |

**Key insight:** In this domain the RT-safety constraints (no alloc, no denormals, bounded output) matter more than DSP sophistication. Retire them with standard mechanisms before touching sound design.

## Common Pitfalls

### Pitfall 1: Assuming only `render_block` is realtime-constrained
**What goes wrong:** Allocating/logging/doing file I/O in `set_param`, `get_param`, `on_midi`, or `create_instance`.
**Why:** Official Schwung MODULES.md states *all* six entry points run on the SCHED_FIFO-70 SPI callback and forbids "file I/O, allocation or free, locks, fork/exec, logging, unbounded work" in *any* of them.
**How to avoid:** Single calloc in `create_instance` (permitted once, but note it is on the audio thread — keep it to exactly one). No per-call allocation. **Conflict to flag:** D-10 requires calling `host->log` to record `buf_len`; the official doc lists logging among forbidden audio-thread operations. Do the log **once**, guarded by a `bool logged_buflen` flag, and treat it as a deliberate one-shot diagnostic (acceptable for a spike; remove or gate behind a debug flag before shipping). Planner should call this out.
**Warning signs:** Device-wide audio dropout; malloc-trap abort during param/midi handling.

### Pitfall 2: Missing FPCR flush-to-zero on the audio thread
**What goes wrong:** Denormal floats in envelope tails / filter states stall the A53 → buffer underruns, or (per the Mixxx bug) full-volume noise with stacked effects.
**Why:** FPCR is per-thread and NOT inherited from the host; the module's `render_block` runs on a thread whose FPCR you must set.
**How to avoid:** Set FZ (bit 24) at the top of `render_block` (snippet in §Cross-Compilation). Also zero-init all state in `create_instance`.
**Warning signs:** CPU spikes correlated with quiet tails; nondeterministic test output.

### Pitfall 3: `ui_hierarchy` buffer overrun / truncation
**What goes wrong:** Writing more than `buf_len` bytes into `get_param`'s buffer, or not null-terminating.
**Why:** Host-chosen `buf_len` is not documented (official doc only says "keep it reasonably small, 8KB cap").
**How to avoid:** Use `memcpy` of `min(json_len+1, buf_len)` and force a null terminator; return byte count. D-10 measures the real cap on-device to size Phase E.
**Warning signs:** Garbled UI, crash on model change.

### Pitfall 4: get_param return-value contract
**What goes wrong:** Returning wrong value; host misinterprets.
**Why:** Official doc: `get_param` returns **bytes written**, or **-1 if key unhandled**.
**How to avoid:** Return `-1` for unknown keys (do NOT return 0 or write garbage); return exact byte count for `ui_hierarchy`/param reads.

### Pitfall 5: on_midi trigger path assumptions
**What goes wrong:** Ignoring running status, or triggering on note-off, or mishandling `source`.
**Why:** `on_midi(inst, msg, len, source)`: `msg[0]`=status, `msg[1]`=note, `msg[2]`=velocity; `source` 0=internal(Move hw)/1=external(USB). Note-on with velocity 0 is a note-off.
**How to avoid:** Trigger FM2 only on `(msg[0]&0xF0)==0x90 && msg[2]>0`. In `pad_layout: drums`, pad hits arrive as note-ons; map (or ignore) note number for the single kick voice in Phase A.

## Code Examples

### Plugin entry point + vtable (dsp.c)
```c
// Source: Context/01_SCHWUNG_DEV_ARCHITECTURE.md §2 (verified vs official MODULES.md)
static plugin_api_v2_t g_api;   // static storage, returned to host

plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) __attribute__((visibility("default")));
plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;                    // stash for sample_rate, log, get_beat_position
    g_api.api_version    = 2;
    g_api.create_instance  = omega_create;
    g_api.destroy_instance = omega_destroy;
    g_api.on_midi          = omega_on_midi;
    g_api.set_param        = omega_set_param;
    g_api.get_param        = omega_get_param;
    g_api.render_block     = omega_render_block;
    return &g_api;
}
```

### render_block skeleton with FPCR + clamped output
```c
void omega_render_block(void *instance, int16_t *out_lr, int frames) {
    g_audio_thread_active = true;              // D-13 malloc-trap gate
    omega_set_ftz();                           // FPCR FZ (FNDTN-05)
    bohm_instance_t *inst = instance;

    float l[OMEGA_MAX_BLOCK], r[OMEGA_MAX_BLOCK];   // float scratch, margin > frames
    const kick_model_vtable_t *m = g_models[inst->model];
    m->render(inst, l, r, frames);             // dispatch (KICK-01)

    for (int n = 0; n < frames; n++) {
        out_lr[n*2]   = to_i16(l[n] * inst->main_volume);   // FNDTN-07
        out_lr[n*2+1] = to_i16(r[n] * inst->main_volume);
    }
    g_audio_thread_active = false;
}
```

### get_param("ui_hierarchy") (ui.c / dsp.c)
```c
// Source: D-09/D-10; official contract: return bytes written, -1 if unhandled
int omega_get_param(void *instance, const char *key, char *buf, int buf_len) {
    bohm_instance_t *inst = instance;
    if (strcmp(key, "ui_hierarchy") == 0) {
        if (!inst->logged_buflen && g_host && g_host->log) {   // D-10 one-shot; see Pitfall 1
            char m[64]; /* format buf_len without locale-dependent printf if possible */
            g_host->log(m);  inst->logged_buflen = true;
        }
        int n = omega_build_ui(inst, buf, buf_len);   // memcpy + null-term, model-aware Page 2
        return n;
    }
    /* ... individual param reads ... */
    return -1;   // unhandled key
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| int16 scratch buffers between DSP stages (reference Context 04) | float-only internal path, int16 at boundary | Project decision (STATE.md) | Avoids double conversion + rounding loss |
| Blanket `-ffast-math` | Granular fast-math subset + explicit FPCR FTZ | CLAUDE.md | Avoids libmvec `_ZGV*` symbols that may be absent on-device |
| ZDF Moog ladder | TPT SVF / 1-pole (Zavalishin) | Project decision | ~3–5% CPU saving, near-identical at bass freqs |
| `atof`/`atoi` param parse (reference) | locale-independent parser | Project decision (UI-01) | Locale-safe numeric handling |

**Deprecated/outdated:**
- Reference `bohm_render_block` (Context 04) is a *learning* reference, not to be copied — it carries the hardcoded-120-BPM tap interval, unbounded int16 cast, amplitude-threshold ducking, and zero-margin scratch (all five bugs in STATE.md watchpoints).

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Docker + schwung-builder image | FNDTN-04 cross-compile | Assumed (host has Docker) — VERIFY | pinned by digest in CI | none — blocking; must pull image |
| aarch64-linux-gnu-gcc | dsp.so build | ✓ (inside image) | GCC 10+ | none |
| native cc/clang | offline tests (FNDTN-06) | ✓ (macOS host) | system | none |
| Move hardware + scp access | D-14 on-device validation | ✓ (D-14: "Move hardware is available") | — | deploy defers if unreachable |
| GitHub Actions runner | D-11 CI | ✓ (repo is git) | — | run steps locally |

**Missing dependencies with no fallback:** Docker image must be pullable in CI and locally — first build step should confirm the digest. Actual `-mcpu` core (D-15) and `ui_hierarchy` buf_len (D-10) are on-device measurements, not blocking for the build.

## Cross-Compilation (FNDTN-04, FNDTN-05)

**Docker invocation:**
```bash
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/charlesvestal/schwung-builder:latest make dsp.so
```

**Recommended aarch64 compile flags (CLAUDE.md granular fast-math subset):**
```
-std=gnu11 -O3 -shared -fPIC -Isrc
-fno-math-errno -ffp-contract=fast        # granular fast-math (NOT blanket -ffast-math)
-fvisibility=hidden                        # only move_plugin_init_v2 exported
-Wl,--no-undefined                         # unresolved symbols fail at link, not on-device
-lm
# Do NOT add -mcpu=cortex-a53 until D-15 confirms the core (avoids SIGILL on wrong -march)
```
Mark the export: `__attribute__((visibility("default")))` on `move_plugin_init_v2`.

**glibc ≤ 2.35 gate (FNDTN-04, D-11) — fail CI if any newer symbol:**
```bash
# fails (nonzero) if any GLIBC_x.y > 2.35 symbol reference exists
if objdump -T build/dsp.so | grep -oE 'GLIBC_[0-9]+\.[0-9]+' \
   | sort -uV | awk -F_ '{split($2,v,"."); if (v[1]>2 || (v[1]==2 && v[2]>35)) exit 1}'; then
  echo "glibc gate OK (<=2.35)"; else echo "GLIBC symbol > 2.35 found"; exit 1; fi
# Also confirm no libmvec vectorized-math symbols leaked in:
objdump -T build/dsp.so | grep -E '_ZGV|libmvec' && { echo "libmvec leak"; exit 1; } || true
# Confirm exactly one exported symbol:
objdump -T build/dsp.so | grep ' g ' | grep -c move_plugin_init_v2
```

**FPCR flush-to-zero / DAZ enable (FNDTN-05) — call at top of `render_block`; FZ = bit 24:**
```c
// Source: ARM FPCR docs + verified community references (Mixxx #16126, zenn arm64-fpcr)
static inline void omega_set_ftz(void) {
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1u << 24);            // FZ: flush-to-zero (subnormals -> 0)
    // bit 25 (FZ16) only relevant for fp16; leave alone. AArch64 has no separate DAZ — FZ covers both.
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
#endif
}
```
On non-aarch64 (native test host) the function is a no-op, so the harness still compiles. GCC 11+ alternative: `__builtin_aarch64_get_fpcr64()/set_fpcr64()`.

## Test Harness (FNDTN-06, D-12, D-13)

**Mock `host_api_v1_t` (native build):**
```c
// tests/mock_host.c
static void mock_log(const char *m){ fprintf(stderr, "[host] %s\n", m); }
static int  mock_midi(const uint8_t *b, int n){ (void)b;(void)n; return 0; }
static double mock_beat(void){ return 0.0; }   // drivable for transport tests later
static int  mock_clock(void){ return 0; }
host_api_v1_t make_mock_host(void){
    host_api_v1_t h = {0};
    h.api_version = 1; h.sample_rate = 44100; h.frames_per_block = 128;
    h.log = mock_log; h.midi_send_internal = mock_midi; h.midi_send_external = mock_midi;
    h.get_clock_status = mock_clock; h.get_beat_position = mock_beat;
    return h;
}
```

**Malloc-trap interposition (D-13, `-DOMEGA_MALLOC_TRAP`):**
```c
// tests/malloc_trap.c — compiled into the test binary; g_audio_thread_active set in render_block
#ifdef OMEGA_MALLOC_TRAP
bool g_audio_thread_active = false;
void *malloc(size_t n){ if (g_audio_thread_active) abort(); return __libc_malloc(n); }
void  free(void *p){ if (g_audio_thread_active) abort(); __libc_free(p); }
void *calloc(size_t a,size_t b){ if (g_audio_thread_active) abort(); return __libc_calloc(a,b); }
void *realloc(void *p,size_t n){ if (g_audio_thread_active) abort(); return __libc_realloc(p,n); }
#endif
```
Notes: on Linux use `__libc_malloc` etc. (or `dlsym(RTLD_NEXT,...)`). On macOS host, symbol interposition differs — planner should either build the trap only under Linux CI, or use a malloc-zone/`-Wl,-interpose` approach on macOS. Flag: the trap is most reliable in the Linux CI native build; document the macOS caveat. The `g_audio_thread_active` flag is set `true` at the START and `false` at the END of `render_block` (CONTEXT specifics), catching any post-init lazy alloc.

**Render-to-WAV + assertions (D-12):**
```c
// tests/test_render.c
host_api_v1_t host = make_mock_host();
plugin_api_v2_t *api = move_plugin_init_v2(&host);
void *inst = api->create_instance("/tmp/omega", "{}");
uint8_t noteon[3] = {0x90, 36, 100};                 // trigger FM2 (kick note 36)
api->on_midi(inst, noteon, 3, 0);
int16_t out[128*2];
FILE *wav = wav_open("tests/output/fm2_kick.wav", 44100, 2);
for (int b = 0; b < 512; b++) {                       // ~1.5 s
    api->render_block(inst, out, 128);
    for (int i = 0; i < 128*2; i++) assert(out[i] >= INT16_MIN && out[i] <= INT16_MAX);
    wav_write(wav, out, 128*2);
}
wav_close(wav);
api->destroy_instance(inst);
```
The `isfinite`/`|x|<=1.0f` assertions belong inside `to_i16` (or a debug variant) so they check the *float* before conversion (D-12: "magnitude ≤ 1.0f before int16 conversion"). WAV writer = 44-byte canonical PCM header (RIFF/WAVE/fmt /data), 16-bit, 2ch, 44100.

## Validation Architecture

> nyquist_validation is enabled (config.json `workflow.nyquist_validation: true`).

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Plain C `assert` + tiny custom runner (native `cc`); no third-party framework |
| Config file | none — see Wave 0 (Makefile `test` target IS the config) |
| Quick run command | `make test` (compiles `tests/test_render.c` natively, runs, writes WAV) |
| Full suite command | `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` (native tests + cross-build + glibc/libmvec/export gate) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| FNDTN-01 | vtable populated, `create/render/destroy` run without crash | smoke | `make test` (lifecycle in test_render.c) | ❌ Wave 0 |
| FNDTN-02 | module.json parses; required fields present | unit | `make test` (json field assertions) or `jq` check in CI | ❌ Wave 0 |
| FNDTN-03 | zero alloc during render | unit | `make test` w/ `-DOMEGA_MALLOC_TRAP` (abort = fail) | ❌ Wave 0 |
| FNDTN-04 | dsp.so builds; no GLIBC>2.35, no libmvec, 1 export | integration | `./scripts/glibc_gate.sh build/dsp.so` | ❌ Wave 0 |
| FNDTN-05 | FTZ set (compiles on aarch64; no-op native) | unit | `make dsp.so` (compile) + code review of FZ snippet | ❌ Wave 0 |
| FNDTN-06 | mock host + WAV render produces file | smoke | `make test` → `tests/output/fm2_kick.wav` exists, nonzero | ❌ Wave 0 |
| FNDTN-07 | output finite + clamped ≤1.0 pre-int16 | unit | `make test` (isfinite/clamp asserts in to_i16 + int16 range) | ❌ Wave 0 |
| KICK-01 | model dispatch through registry; FM2=id 0 | unit | `make test` (assert g_models[0]->name=="FM2") | ❌ Wave 0 |
| KICK-02 | FM2 renders non-silent, deterministic kick | unit | `make test` (energy > threshold; fixed-seed byte hash stable) | ❌ Wave 0 |
| KICK-12 | 8 Page-1 params change output audibly | unit | `make test` (render with PITCH lo/hi → differing buffers) | ❌ Wave 0 |
| KICK-15 | sine table 2049 len, guard sample t[2048]==t[0] | unit | `make test` (static assert + runtime equality check) | ❌ Wave 0 |
| D-10 / SC5 | buf_len logged per host | manual (on-device) | inspect `/data/UserData/schwung/debug.log` in each host | manual |
| SC1 / D-14 | same dsp.so loads + sounds in 3 hosts | manual (on-device) | deploy + trigger in Schwung slot, DR32 pad, Movy track | manual |

Success-criteria mapping: SC1 (3-host load) = manual on-device; SC2 (audible params) = KICK-02/KICK-12 offline + manual on-device listen; SC3 (malloc-trap + glibc gate) = FNDTN-03/FNDTN-04 automated; SC4 (WAV, no NaN/Inf, clamp) = FNDTN-06/FNDTN-07 automated; SC5 (buf_len logged) = manual on-device.

### Sampling Rate
- **Per task commit:** `make test` (native harness; < 5 s)
- **Per wave merge:** `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so`
- **Phase gate:** full suite green in GitHub Actions (D-11) + on-device manual checklist (SC1, SC5) before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/mock_host.c` / `.h` — mock `host_api_v1_t` (FNDTN-06)
- [ ] `tests/malloc_trap.c` — interposition + `g_audio_thread_active` (FNDTN-03/D-13)
- [ ] `tests/wav.c` / `.h` — 44-byte PCM WAV writer
- [ ] `tests/test_render.c` — lifecycle + 512-block render + assertions (covers FNDTN-01/06/07, KICK-01/02/12/15)
- [ ] `scripts/glibc_gate.sh` — objdump GLIBC/libmvec/export gate (FNDTN-04)
- [ ] `Makefile` — `dsp.so` (aarch64) + `test` (native, `-DOMEGA_MALLOC_TRAP`) + `clean` + `deploy` targets
- [ ] `.github/workflows/ci.yml` — docker build + gate + native test (D-11)
- [ ] Framework install: none — plain C, uses system `cc`

## Open Questions

1. **Exact `ui_hierarchy` buf_len each host passes**
   - What we know: official doc says "keep reasonably small, 8KB cap"; not a hard contract.
   - What's unclear: the real per-host cap (Schwung slot vs DR32 vs Movy).
   - Recommendation: implement D-10 one-shot log; size Phase A JSON conservatively (< 2 KB); this is explicitly a Phase A→E spike, not a blocker.

2. **Exact Move SoC / Cortex core (`-mcpu`)**
   - What we know: strongly suspected i.MX8M / Cortex-A53 (CLAUDE.md MEDIUM).
   - What's unclear: unconfirmed.
   - Recommendation: D-15 — read `/proc/cpuinfo` on-device; ship baseline ARMv8-A (no `-mcpu`) until confirmed to avoid SIGILL.

3. **Are `set_param`/`get_param` guaranteed single-threaded relative to `render_block`?** (STATE.md open Q)
   - What we know: official doc says all six run on the same SPI callback thread → effectively serialized on one thread.
   - What's unclear: whether the host ever interleaves them from another thread (doc implies not).
   - Recommendation: assume single-threaded (same thread); no atomics needed in Phase A. Confirm on-device if any tearing observed.

4. **`host->log` on the audio thread (D-10 vs official "no logging" rule)**
   - What we know: official MODULES.md forbids logging in audio-thread calls; D-10 requires one log call.
   - Recommendation: one-shot, flag-guarded, treat as temporary spike diagnostic; planner should schedule its removal/gating before ship.

## Sources

### Primary (HIGH confidence)
- `Context/01_SCHWUNG_DEV_ARCHITECTURE.md` — `plugin_api_v2_t`/`host_api_v1_t` structs, render_block int16 convention, module.json, DR32/Movy hosting, Docker build template
- `Context/02_OHMFORCE_BOHM_SYSTEM_SPEC.md` — PITCH/LENGTH/SUSTAIN/CURVE(808↔909)/ATTACK/TRS DEC/TRS TNE/COLOR semantics; FM-2X architecture
- `Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` — exponential pitch-envelope formula (τ≈15 ms), soft-clip forms
- `Context/04_BOHM_SCHWUNG_MODULE_DESIGN.md` — instance struct pattern, set_param dispatch, render pipeline (reference — bugs noted)
- `Context/05_SOURCE_INDEX_AND_REFERENCES.md` — reference engines (9W9 909 sweeps, 8W8, Sophie 4-op FM, Forge, Maze) to study
- `CLAUDE.md` — full stack decisions (C11, float, wavetable, TPT, Makefile, cross-compile gotchas, memory layout)
- Official Schwung MODULES.md (raw.githubusercontent.com/charlesvestal/schwung/main/docs/MODULES.md) — get_param returns bytes-written/-1; on_midi byte layout + source 0/1; render_block int16 interleaved not mapped; ALL entry points on SCHED_FIFO-70; no-alloc/no-log rule; 8KB module.json cap

### Secondary (MEDIUM confidence)
- ARM FPCR docs + Mixxx issue #16126 + zenn arm64-fpcr — FZ = bit 24, `mrs/msr fpcr`, per-thread, `__builtin_aarch64_*_fpcr64` (GCC 11+)

### Tertiary (LOW confidence)
- `-mcpu=cortex-a53` applicability — pending D-15 on-device `/proc/cpuinfo` confirmation

## Metadata

**Confidence breakdown:**
- API contract: HIGH — Context 01 + official MODULES.md agree; return-value and on_midi layout confirmed by official doc
- FM2 DSP: HIGH — standard 2-op FM; formulas from Context 03; matches KICK-02
- Cross-compilation / FPCR: HIGH for flags/gate/FZ-bit; MEDIUM for libmvec risk and `-mcpu` (both mitigated by objdump check + omitting -mcpu)
- Test harness: HIGH for design; MEDIUM for malloc interposition on macOS host (Linux CI is reliable)

**Research date:** 2026-09-28
**Valid until:** ~2026-10-28 (stable C/DSP domain; re-check schwung-builder image + MODULES.md if Schwung API revs)
