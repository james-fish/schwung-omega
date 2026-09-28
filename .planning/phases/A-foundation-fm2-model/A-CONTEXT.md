# Phase A: Foundation + FM2 Model - Context

**Gathered:** 2026-09-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Prove the entire pipeline end-to-end: `dsp.so` cross-compiled in the pinned Docker image, loads in all 3 host contexts (Schwung slot, DR32 pad, Movy track), FM2 kick sounds on-device with fully wired Kick Page 1 + FM2 Page 2 parameters, all RT-safety primitives verified, and CI gates enforced. This phase establishes the foundation architecture that all 9 remaining phases build on — source layout, vtable dispatch, shared DSP primitives, and the test harness are all set here.

**In scope:** FNDTN-01 through FNDTN-07, KICK-01, KICK-02, KICK-12, KICK-15  
**Out of scope:** Any model beyond FM2; full nav tree (Phase E); Groove, Performer, Presets.

</domain>

<decisions>
## Implementation Decisions

### Source Code Organization

- **D-01:** Modular split from day one — do not start with a single file. File layout:
  - `src/dsp.c` — plugin entry point (`move_plugin_init_v2`), `create_instance`, `destroy_instance`, `render_block`, `set_param`, `get_param`, `on_midi`; no DSP math here
  - `src/models/fm2.c` — FM2 engine: trigger, render, set_p2, p2_slot_desc
  - `src/models/model_registry.c` — vtable registry array (model_id_t → kick_model_vtable_t); Phase B just adds entries here
  - `src/dsp_primitives.c` + `src/dsp_primitives.h` — all shared DSP building blocks (wavetable oscillator, exponential decay envelope, TPT SVF filter stub)
  - `src/ui.c` — ui_hierarchy JSON string fragments
  - `src/omega.h` — shared types: `bohm_instance_t`, `kick_model_vtable_t`, `model_id_t`, param key macros

- **D-02:** All headers are `src/*.h` (flat); source files under `src/` and `src/models/`. No deeper nesting in Phase A.

### FM2 Engine

- **D-03:** FM2 must be **fully complete** in Phase A — all 8 Kick Page 1 parameters (PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR) and all 3 FM2-specific Kick Page 2 parameters (FM RATIO, FM INDEX, OP2 WAVE) fully wired and audibly functional. No stubs.

- **D-04:** Carrier and modulator oscillators read from a **shared 2048+1-sample `static const float` sine wavetable** in `.rodata` (defined in `dsp_primitives.c`). Guard sample at index 2048 equals index 0 — linear interpolation at wrap needs no branch. Both FM2 oscillators, and all future models, read the same table. Do not call `sinf()` per sample.

- **D-05:** Envelopes (pitch sweep, FM index decay, amplitude) use a **shared one-pole exponential decay struct** defined in `dsp_primitives.h`:
  ```c
  typedef struct { float value; float coeff; } env_t;
  ```
  `env_trigger(e, target, decay_coeff)` and `env_tick(e)` live in `dsp_primitives.c`. All FM2 envelopes and all Phase B+ model envelopes use this same struct.

- **D-06:** CURVE parameter maps to the 808↔909 pitch sweep shape (as per KICK-12). Implement as a blend between two pitch envelope curves (fast exponential vs. slow linear/log decay) rather than a single fixed shape.

- **D-07:** COLOR parameter drives a TPT low-pass filter on the FM2 output (cutoff mapped from COLOR value). Phase A can use a simplified single-pole TPT for COLOR — the full SVF is Phase D. Define the TPT 1-pole in `dsp_primitives.c` to be reused by all models and Groove.

### UI Hierarchy

- **D-08:** Phase A delivers a **real minimal `ui_hierarchy` JSON** — not a stub. Must include:
  - Kick Page 1: 8 encoder slots for PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR
  - FM2 Kick Page 2: 3 model-specific slots (FM RATIO, FM INDEX, OP2 WAVE) + FX TYPE + FX AMT placeholders
  - Use real param key strings matching `set_param`/`get_param` dispatch

- **D-09:** UI hierarchy stored as a **pre-serialized static C string** in `src/ui.c`. No JSON library. Zero allocation in `get_param`. Served from `get_param("ui_hierarchy", buf, buf_len)` via `strncpy` (or `memcpy` + null-terminate) into the provided buffer.

- **D-10:** Phase A also measures and logs the actual `buf_len` passed by each host to `get_param("ui_hierarchy")` on first call — write the value to the host's log function. This measurement unblocks Phase E's full hierarchy sizing.

### CI and Testing

- **D-11:** **GitHub Actions** CI pipeline. On push: (1) Docker cross-compile step runs `make` inside `ghcr.io/charlesvestal/schwung-builder:latest`, (2) `objdump -T build/dsp.so | grep GLIBC` gate fails the build if any symbol references GLIBC > 2.35, (3) native `make test` runs the offline harness.

- **D-12:** Offline test harness (`tests/test_render.c`, compiled natively with `cc`): creates instance via mock `host_api_v1_t`, triggers FM2, renders 512 blocks (≈1.5s), writes to `tests/output/fm2_kick.wav` (44-byte PCM header + int16 data). Asserts: every output sample passes `isfinite()`, magnitude stays ≤ 1.0f before int16 conversion, and zero `malloc`/`free`/`calloc` called during `render_block`.

- **D-13:** Malloc trap: in the test build (`-DOMEGA_MALLOC_TRAP`), `malloc`/`calloc`/`realloc`/`free` are replaced via interposition with functions that call `abort()` after `render_block` initialization is complete. A global `bool g_audio_thread_active` flag gates the trap (false during `create_instance`, true during `render_block`).

- **D-14:** Deployment target: Move hardware is available. Deploy script `scripts/deploy.sh` uses scp + atomic rename (upload to `dsp.so.new`, then `mv dsp.so.new dsp.so` on device). Phase A success includes loading and triggering on-device in all 3 host contexts.

- **D-15:** Identify the exact Move SoC / Cortex core in Phase A (via `/proc/cpuinfo` on-device or Schwung docs). If confirmed Cortex-A53, document for potential `-mcpu=cortex-a53` flag — do not bake it in until confirmed.

### Claude's Discretion

- Phase A FX chain: FX TYPE and FX AMT appear in the FM2 Page 2 UI but the 5 FX modes (Diode/Clip/SAT/Fold/Crush) are Phase B scope (KICK-14). Phase A can wire FX TYPE/AMT to a passthrough (identity, no processing) with the correct param keys in place.
- Exact `env_t` coefficient formula (time-constant-based vs. sample-count-based) — choose whichever gives cleaner `set_param` mapping to the LENGTH/ATTACK/TRS DEC parameters.
- Makefile structure: Phased targets (`dsp.so`, `test`, `clean`, `deploy`) with separate aarch64 and native compiler variables. No additional constraints beyond what's in CLAUDE.md.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Schwung Plugin API
- `Context/01_SCHWUNG_DEV_ARCHITECTURE.md` — `plugin_api_v2_t` C interface, `host_api_v1_t` structure, `render_block` calling convention, `set_param`/`get_param` contract, host context differences (Schwung slot vs DR32 pad vs Movy track), MIDI trigger path

### Source Reference Implementations
- `Context/05_SOURCE_INDEX_AND_REFERENCES.md` — Index of reference DSP engines (9W9, 8W8, Forge, Sophie, Maze) available to study; which patterns are safe to port to C

### Bohm Parameter Spec
- `Context/02_OHMFORCE_BOHM_SYSTEM_SPEC.md` — Bohm hardware parameter definitions including PITCH, LENGTH, SUSTAIN, CURVE (808↔909 pitch sweep), ATTACK, TRS DEC, TRS TNE, COLOR semantics
- `Context/04_BOHM_SCHWUNG_MODULE_DESIGN.md` — Schwung-specific module design patterns, ui_hierarchy JSON schema examples, parameter naming conventions

### Build & Cross-Compilation
- `CLAUDE.md` §"Build System", §"Cross-Compilation Gotchas", §"Memory Layout Recommendations" — granular fast-math flags, FPCR FTZ, glibc symbol versioning gate, single-calloc instance struct pattern

### Techno Kick Synthesis Reference
- `Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` — FM kick synthesis techniques, transient/envelope design, COLOR filter role

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- None yet — this is Phase A, greenfield.

### Established Patterns
- None yet — Phase A establishes all patterns.

### Integration Points
- `dsp.c` exports exactly one symbol: `move_plugin_init_v2` (marked `__attribute__((visibility("default")))`); all other symbols hidden via `-fvisibility=hidden`
- `model_registry.c` is the only file that knows the full model list — `dsp.c` calls through the registry, never directly into `fm2.c`
- `ui.c` reads the active model ID from instance state to assemble the correct Page 2 content

</code_context>

<specifics>
## Specific Ideas

- The `env_t` one-pole decay struct (D-05) should be designed so that LENGTH, ATTACK, TRS DEC all map cleanly to the same struct — likely a `time_ms → coeff` helper that converts milliseconds to a per-sample decay coefficient: `coeff = expf(-1.0f / (time_ms * 0.001f * SAMPLE_RATE))`.
- The CURVE 808↔909 blend (D-06): at CURVE=0 → slow log decay (808 character); at CURVE=1 → fast linear/exponential decay (909 character); intermediate values blend the two target curves.
- The `g_audio_thread_active` malloc trap flag (D-13) should be set to `true` at the START of `render_block` and back to `false` at the END — not just once after `create_instance` — to allow any future post-init lazy allocation paths to be caught.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: A-foundation-fm2-model*
*Context gathered: 2026-09-28*
