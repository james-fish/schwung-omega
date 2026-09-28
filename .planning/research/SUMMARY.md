# Research Summary — Omega

**Synthesized:** 2026-09-28

---

## Executive Summary

Omega is a single-module techno production system for Ableton Move: 10 kick synthesis engines, a 4-tap 16th-note rumble generator, and a live performance mixer with sidechain ducking and DJ filter. Its closest prior art is the Ohm Force Bohm hardware — a 3-module Eurorack system — which Omega collapses into one `dsp.so`.

The stack is almost entirely platform-dictated (C11, aarch64, glibc 2.35, 44.1kHz/128-frame, `SCHED_FIFO 70` audio thread), leaving only DSP algorithm selection and code organization as open decisions. Research resolves both: TPT state-variable filter, float throughout, wavetables in `.rodata`, modal damped oscillators for physical modeling, and a function-pointer vtable dispatch copied from the DR32 reference implementation.

**Critical finding:** The reference code in `04_BOHM_SCHWUNG_MODULE_DESIGN.md` contains five directly shippable bugs that must not be carried into Omega:
1. Hardcoded 120 BPM tap interval (`sample_rate * 0.125f`)
2. Unbounded/asymmetric `fast_tanh` that diverges on positive values
3. Ducking triggered off amplitude threshold (chatters on waveform) instead of note event
4. Missing int16 clamp + `isfinite` guard before output cast
5. Scratch buffer exactly sized with no margin for block-size variance

**Second critical finding:** Schwung uses a two-layer persistence model — a host-managed `state` JSON blob (primary, has a size cap that DR32 already hit) plus module-owned `presets/*.json` files in `module_dir`. Both must be implemented. BPM must be derived from `get_beat_position()`, not hardcoded.

---

## Stack

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Language | C11 (`-std=gnu11`) | C ABI requirement; `_Static_assert`, `_Alignas`, anonymous unions for model-specific param overlay |
| Numeric format | `float` throughout | aarch64 mandates hardware FPU; fixed-point is a legacy no-FPU pattern; use `double`/`uint32` phase accumulators only for long-note oscillator drift |
| Filter topology | TPT state-variable filter (Zavalishin) | Simultaneous LP/BP/HP, stable under fast modulation, ~3–5% cheaper than ZDF Moog ladder |
| Wavetable storage | `static const float` in `.rodata`, `_Alignas(16)` | Zero heap, zero load-time I/O, OS maps once across all instances; 2048+1 guard sample for branch-free linear wrap |
| Physical modeling | Modal damped resonator (2–3 modes) | Exact, stable, vectorizes across modes. Full waveguide is over CPU budget. |
| Build system | GNU Makefile + Docker `schwung-builder:latest` (pinned by digest) | Single artifact; CMake adds no value |
| Denormals | Explicit FPCR FZ bit set in `render_block` | FPCR is per-thread, not inherited; set `fpcr |= (1u << 24)` on first `render_block` entry |
| Compiler flags | Granular fast-math subset, NOT `-ffast-math` | Blanket `-ffast-math` can pull libmvec symbols absent on device; use `-ffp-contract=fast -fno-math-errno -ffinite-math-only -fno-signed-zeros` |
| glibc gate | `objdump -T build/dsp.so | grep GLIBC` CI check | Every symbol must be `<= 2.35` — catch before SCP to device |

---

## Table Stakes Features

| Feature | Notes |
|---------|-------|
| Multi-engine MODEL selector with 10 models | Product identity |
| Universal Kick Page 1 (8 encoders: PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR) | Every kick synth has these |
| Context-sensitive Kick Page 2 (6 model-specific slots) | Move's encoder system expects this |
| 4-tap tempo-synced rumble (Groove) | Core product feature |
| Sidechain ducking triggered from note event | Triggering off amplitude is a bug |
| DJ filter: bidirectional LP/HP + resonance | Live performance staple |
| End-of-chain bounded soft clipper | Prevents hard digital clipping |
| Two-layer state persistence (compact `state` blob + named presets) | Without it, Omega loses state on reload |
| Zero-allocation, zero-I/O audio thread | Hard platform requirement |
| Loads unmodified in Schwung slot, DR32 pad, Movy track | Ecosystem requirement |

---

## Key Differentiators

| Feature | Value |
|---------|-------|
| All-in-one kick + rumble + performer | Hardware Bohm needs 3 Eurorack modules; nothing in Schwung ecosystem combines these |
| 10 synthesis models | 808 sub-boom (ANA) through generative (GEN) under one roof |
| Bidirectional performance-macro root page | 8 curated macros updating bidirectionally with sub-pages; no existing Schwung module does this |
| GEN model: hypnotic generative rumble | Scale-quantized PRNG pitch sequences, Euclidean density gating, SEED for repeatability |
| USR model: custom WAV/wavetable from device storage | Loaded off audio thread at init; bounded buffer in instance struct |

---

## Architecture Highlights

**vtable dispatch:** `model_vtable_t` per engine (trigger, render, set_p2, p2_slot descriptors). One C file per model, registry array indexed by `model_id_t`. Mirrors DR32's `dr32_engine_ops` — verified from source. No giant switch in `render_block`.

**Single instance struct, one `calloc`:** `omega_instance_t` ~730–780 KB, dominated by 705 KB groove delay buffer. One `calloc` in `create_instance`, freed once in `destroy_instance`.

**Wavetables NOT per-instance:** `static const float g_wavetables[NUM_MODELS][BANDS][2049]` in `.rodata`. Shared across all instances by the OS. Never copy into instance struct.

**Float-only internal signal path:** Convert `int16 → float` once at input; `float → int16` once at output with `clamp + isfinite + lrintf`. Reference code round-trips through int16 mid-chain — Omega must not.

**Render pipeline:** `kick_render → groove_delay_taps → duck_env → dj_filter → soft_clip → int16_out`

**UI hierarchy:** Static string fragments for fixed pages; Kick Page 2 assembled dynamically from model's `p2_slot_desc_t` into pre-allocated `ui_scratch[8192]`. No allocations in `get_param`.

**Bidirectional macros:** `set_canonical(inst, param_id, v, updating_macro)` is the only place that mutates canonical values. Re-entrancy prevented by construction.

**Preset threading:** Load-at-init (enumerate + parse all `presets/*.json` in `create_instance`). Apply = `memcpy`, RT-safe. Save = `volatile save_request` flag drained by dedicated low-priority writer pthread.

**BPM sync:** Compute `bpm = (Δbeats / Δseconds) × 60` across blocks; derive `samples_per_16th = (60/bpm) × sr/4`. Never hardcode 120 BPM.

---

## Critical Pitfalls

**1. Hidden allocation on audio thread** — `snprintf`, `atof`, `strtod` can allocate or touch locale state inside `set_param`/`get_param`. Hand-roll a locale-independent `parse_float` and fixed-format numeric printer. Add malloc-trap debug build that `abort()`s on any heap call from audio thread.

**2. FPCR FZ bit not set — denormal CPU stalls** — FPCR is per-thread, not inherited from host process. Set explicitly in `render_block`. Verified cause of real-world audio dropouts on ARM64 (Mixxx issue #16126).

**3. Five bugs in reference code** — listed in executive summary. Do not copy the reference render_block, tap_interval, fast_tanh, ducking trigger, or int16 cast code without fixing them.

**4. Preset load / model switch tearing** — Applying preset field-by-field mid-block renders split between old/new model state. Apply staged params atomically at block boundaries; zero/re-init engine state on every model switch.

**5. glibc 2.35 symbol versioning** — Any build outside the pinned Docker image can embed `GLIBC_2.36+` symbols that fail at `dlopen`. Gate every release on `objdump -T` check.

**Additional moderate pitfalls:**
- `ui_hierarchy` buffer size is unknown — measure actual `buf_len` Schwung passes on-device in Phase A (hard unknown, no documented limit)
- Scratch buffer margin: add `MAX_BLOCK` guard (reference has zero margin)
- Wavefolder aliasing on HRD/DIG drive: consider ADAA or 2–4× oversampling, budgeted against 10–15% CPU ceiling
- Filter coefficient blowup: clamp DJ filter cutoff to `[20Hz, 0.45×sr]` before computing `g = tan(πfc/sr)`

---

## Phase Sequence Recommendation

| Phase | Focus | Key Milestone |
|-------|-------|---------------|
| A — Foundation + FM2 | Plugin skeleton, RT primitives, CI gates, FM2 model | FM2 kick sounds on-device in all 3 hosts |
| B — 9 remaining models | Breadth expansion behind vtable; parallelizable | All 10 models playable |
| C — Groove rumble | BPM-synced 4-tap delay, COLOR LPF, MONO, GEN hooks | Rumble audibly syncs to project tempo |
| D — Performer chain | Note-event ducking, DJ filter, soft clipper | Full kick→rumble→duck→filter→clip chain CPU-measured |
| E — UI hierarchy | All params reachable from encoders; requires Phase A `buf_len` | Every page navigable on-device |
| F — Bidirectional macros | Root performance page with 8 bidirectional macros | Root page live-updates sub-pages |
| G — Presets | Compact state blob + named preset files + save/load UI | Full preset round-trip tested |

---

## Open Questions

| Question | Phase | Resolution |
|----------|-------|------------|
| What `buf_len` does Schwung/DR32/Movy pass to `get_param("ui_hierarchy")`? | A spike → E | Log via `host->log` on first `get_param` call |
| Are `set_param`/`get_param` guaranteed single-threaded? | A (staging design) | Inspect `MODULES.md`; verify with thread-ID assert |
| Move per-process RAM ceiling? | C (groove buffer size) | Measure `/proc/<pid>/status` VmRSS on-device at 1, 4, 16 instances |
| Exact Move SoC / Cortex core? | A (build flags) | Check `/proc/cpuinfo`; determines `-mcpu` flag |
| Does Schwung re-query `ui_hierarchy` on model change or cache? | E | Verify from `MODULES.md` or on-device |
| Host state-blob byte cap for Movy/Schwung slots? | G | Measure empirically (DR32 hit it — Omega will too without care) |
