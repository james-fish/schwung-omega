<!-- GSD:project-start source:PROJECT.md -->
## Project

**Omega**

Omega is a native C Schwung module for Ableton Move that brings the complete Ohm Force Bohm/Groove/Performer techno kick synthesis system to the device. It provides 10 multi-engine kick synthesis models (2-op FM, 4-op FM, physical modeling, wavetable, generative, and more), a 4-tap rumble generator, and a live performance mixer with sidechain ducking and DJ filter. Omega runs as a single module loadable in Schwung's instrument slots, DR32 pad slots, and Movy tracks.

**Core Value:** A techno producer on Ableton Move should be able to dial in a full Bohm-style kick + rumble + performer system from one module, with immediate access to the most expressive performance controls on the root page.

### Constraints

- **Tech stack**: C (no C++, no STL) — Schwung plugin API v2 is C ABI
- **Audio thread**: Zero malloc/free/new/delete, zero file I/O, zero blocking, zero mutex on audio thread
- **Memory**: All voice buffers, delay lines, wavetables pre-allocated at `create_instance`. Groove delay buffer = 88200 frames × 2 channels × 4 bytes = ~700KB per instance
- **Target arch**: Linux ARM64 (aarch64), glibc 2.35, cross-compiled via `ghcr.io/charlesvestal/schwung-builder:latest`
- **UI**: 128×64 1-bit OLED, 8 rotary encoders, Move pads for navigation. `ui_hierarchy` JSON served from `get_param`
- **Sample rate**: Fixed 44.1kHz — no runtime negotiation
- **Compatibility**: Must load in Schwung slots, DR32 pad slots, and Movy tracks without modification
<!-- GSD:project-end -->

<!-- GSD:stack-start source:research/STACK.md -->
## Technology Stack

## Executive Summary
## Recommended Stack
### Core Language & Toolchain
| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| C | **C11** (`-std=gnu11`) | Implementation language | Schwung API is a C ABI; C++ is explicitly out of scope. C11 gives `<stdbool.h>`, `<stdint.h>`, `static_assert`, anonymous unions/structs, and `_Alignas` for cache/SIMD alignment — all useful, none costly. |
| aarch64-linux-gnu-gcc | Image default (GCC 10+) | Cross-compiler | Provided by `ghcr.io/charlesvestal/schwung-builder:latest`. GCC 10+ also enables libmvec auto-vectorization of scalar math under fast-math. |
| GNU Make | any | Build orchestration | Single shared object, fixed flags. Make is already the idiom in the reference `build.sh`. |
| libm | glibc 2.35 | `sinf`, `expf`, `tanhf`, `powf` | Link `-lm`. Precompute anything you can into tables to avoid per-sample transcendentals. |
- `_Static_assert` lets you assert struct sizes and buffer bounds at compile time (e.g. `_Static_assert(sizeof(groove_state_t) < 800000, ...)`) — invaluable when every byte is pre-allocated.
- `_Alignas(16)` on wavetable and scratch buffers guarantees NEON-friendly 16-byte alignment without linker tricks.
- Anonymous unions clean up the per-model parameter overlay (the 6 model-specific slots in Kick Page 2).
- C11 atomics are available if you ever need a lock-free param handoff, though for this API set/get and render run on the *same* thread so you likely won't.
- No downside on aarch64/GCC: C11 is fully supported. **Do not** use C11 threads (`<threads.h>`) — irrelevant here.
- Prefer the **granular fast-math subset** above over blanket `-ffast-math`. Blanket `-ffast-math` sets `-funsafe-math-optimizations` *and* links a startup that toggles FTZ/DAZ globally, and on some glibc builds pulls libmvec symbols that may not resolve on-device. The granular flags give you the speedups that matter for audio (no errno on `sqrtf`, allow FMA contraction) without the ABI risk. (MEDIUM confidence on the libmvec risk specifically — verify by inspecting `objdump -T build/dsp.so` for unexpected `@GLIBC` math symbols; see Cross-Compilation Gotchas.)
- `-fvisibility=hidden` keeps everything internal except your one exported `move_plugin_init_v2`. Mark it `__attribute__((visibility("default")))`.
- **Manually enable FTZ/DAZ** in `create_instance` by setting the `FZ` bit in FPCR (see Denormals below) rather than relying on a fast-math flag to do it.
### DSP: Numeric Format
| Decision | Choice | Why |
|----------|--------|-----|
| Sample/state representation | **32-bit `float` throughout** | aarch64 has a **mandatory** hardware FPU (VFP/NEON share one register file). Scalar `float` ops are single-cycle-throughput on Cortex-A53. Fixed-point offers no speed advantage on this class of core and adds scaling bugs. |
| I/O boundary only | int16 ↔ float conversion | Host `render_block` gives `int16_t *out_lr`. Convert to float, do all DSP in float, convert back. Reference code already does `x / 32768.0f`. |
| Intermediate precision | `float` (not `double`) | `double` on A53 is roughly half throughput and doubles memory traffic. Only use `double` for accumulator-sensitive spots like a slow phase accumulator over long delays if drift is ever observed (unlikely at 44.1k). |
### NEON SIMD — Relevance Assessment
| Aspect | Assessment |
|--------|------------|
| Do you *need* NEON? | **No.** Scalar float at 128 frames × a handful of voices is comfortably inside the 10–15% CPU budget on an A53 at 44.1kHz. |
| Should you *design against* it? | **Yes — keep it possible, don't force it.** Structure per-sample loops over contiguous `float[]` buffers, keep branches out of inner loops, `_Alignas(16)` your buffers. GCC `-O3` will auto-vectorize simple loops (mix, gain, tap sum) on its own. |
| Where NEON would pay off if profiling demands it | The 4-tap groove sum, block gain/mix, and the modal oscillator bank (4 modes = one `float32x4_t`). Write scalar first, vectorize only the proven hotspot. |
| Intrinsics header | `<arm_neon.h>` — but treat as a later optimization, gated behind profiling. |
### Wavetable Synthesis
| Decision | Choice | Why |
|----------|--------|-----|
| Storage | **`static const float` arrays compiled into the binary (`.rodata`)** | Zero load-time I/O, zero heap, deterministic, shared read-only across instances (the OS maps `.rodata` once). No audio-thread file access — satisfies the hard constraint. |
| Table length | **2048 samples/cycle** (as specified) | 2048 is the practical minimum for full-range single-cycle tables; fine for kick fundamentals which live at 30–200 Hz where phase increment per sample is tiny. |
| Interpolation | **Linear (2-point)** | For a kick's low fundamentals the read increment is small, so consecutive samples are very close and linear error is negligible. Verified community consensus: cubic "improves the low octaves a bit" but the bigger win is a larger/band-limited table, not a fancier interpolator. Linear is ~3 ops/sample vs ~10+ for cubic. |
| Anti-aliasing | **Pre-band-limit the stored tables** (mip-style: a few octave-band variants per model) rather than relying on the interpolator | Linear/cubic interpolation leaves imaging; band-limiting the *source* table is the correct fix. For kicks you rarely play high enough to alias, so 1–2 band variants per model usually suffice. |
| USR model (user WAV) | Load **in `create_instance` / via `set_param` off the audio thread**, into a pre-sized instance buffer | The constraint forbids file I/O on the audio thread. Enumerate/load at init time, as the PROJECT already plans ("init-time enumeration"). Cap the user table size and pre-allocate that cap. |
- Store as a single flat `static const float g_wavetables[NUM_MODELS][BANDS][2048]` in `.rodata`, `_Alignas(16)`. Flat + const = one shared mapping, no per-instance copy, cache-friendly sequential reads.
- Index by `model * stride + band * 2048 + phase_int`. Keep a guard sample (`table[2048] == table[0]`) so linear interp at the wrap point needs no branch — store `2049` and duplicate the first sample, or mask the `+1` read with `& 2047`.
- **Generate tables at build time**, not by hand: a small host-side C or Python generator emits a `wavetables.h` with the `const` arrays. Keeps the arrays reproducible and reviewable.
### FM Synthesis (FM2, FM4 models)
| Decision | Choice | Why |
|----------|--------|-----|
| Numeric format | **float** (per the format decision above) | FPU makes float phase accumulation and `sinf`/table lookup cheap. |
| Oscillator source | **Read the sine from a shared wavetable** (reuse the 2048 sine table) rather than calling `sinf` per sample | Table lookup + linear interp is faster and denormal-free vs `sinf`. Both carrier and modulator read the same `.rodata` sine table. |
| 2-op (FM2) | Modulator phase → scaled → added to carrier phase; single FM index envelope | Cheap; the classic punchy FM kick. |
| 4-op (FM4) | Fixed set of **selectable operator routing algorithms** (OPL3-inspired, as specified) encoded as a small static routing table | Precompute the algorithm graph as `static const` connection tables; the render loop just walks the table. Avoids per-sample branching on algorithm. |
| Phase representation | `float` phase in `[0,1)` × 2048, or a `uint32` phase accumulator with fixed-point fraction for exact wraparound | Either works; `uint32` phase accumulator is a nice trick for exact, branch-free wrap and cheap table indexing (top 11 bits index, low bits = interp fraction). This is *fixed-point phase, float amplitude* — a legitimate hybrid, not "fixed-point DSP." |
### Physical Modeling (PHY model)
| Aspect | Recommendation |
|--------|----------------|
| Model | **2–3 damped resonant modes** (exponentially-decaying sinusoids) excited by a short impulse/noise burst. Each mode = {frequency, decay coefficient, amplitude}. |
| Beater / head / shell mapping | Beater = the excitation impulse (short, bright, filtered noise burst). Head = the dominant pitched mode with a pitch envelope (the classic downward sweep). Shell = 1–2 lower-Q body modes adding thump/resonance. |
| Implementation | Each mode as a **2-pole resonator IIR** (biquad in "resonator" config) or a **complex state rotation** (per-sample `z *= e^{jω} · e^{-decay}` — exact, numerically stable, one complex multiply). The complex-rotation form is the modern, stable choice and vectorizes across modes. |
| Excitation | Filtered noise burst + click, envelope-gated. Reuse the transient/click infrastructure needed by WTR/TRS. |
| Cost | 3 modes ≈ 3 biquads/complex mults per sample — trivial. This is the "cheap physical model" the hybrid fidelity strategy depends on. |
### Filters
| Decision | Choice | Why |
|----------|--------|-----|
| Primary topology | **TPT state-variable filter** (Zavalishin / "The Art of VA Filter Design") | Already the logged project decision (TPT over ZDF Moog ladder, saves ~3–5% CPU). TPT SVF resolves the zero-delay feedback loop analytically, gives simultaneous LP/HP/BP outputs from one structure, and is stable and cheap. Ideal for the DJ filter (LP↔neutral↔HP sweep) and the groove COLOR/LPF (2/4-pole). |
| Reference implementation | Port from Zavalishin's SVF pseudocode (freely available in the VA Filter Design book, ch. on TPT SVF). JUCE `dsp::StateVariableTPTFilter` and `FirstOrderTPTFilter` are C++ references to study (do not depend on JUCE — reimplement the ~15 lines in C). | The math is short and unencumbered; a hand-written C SVF is a few multiplies per sample. |
| 4-pole / 2-pole toggle (groove LPF) | **Cascade two TPT 1-pole (or two SVF LP) stages**; toggle uses 1 or 2 stages | Matches the "LPF POLE (2/4-pole toggle)" requirement cleanly. |
| Biquad | **Fallback / secondary only** — use for fixed EQ, the modal resonators (RBJ resonator cookbook), and any static filtering where cutoff doesn't sweep fast | Biquads suffer coefficient-recalculation zipper artifacts and instability when modulated quickly; TPT is superior for the swept DJ filter. Keep biquads for the static/modal cases where they shine. |
### Generative Sequencer (GEN model)
| Decision | Choice | Why |
|----------|--------|-----|
| PRNG | **64-bit LCG or xorshift/PCG seeded from SEED param** | Deterministic, tiny, no allocation, reproducible sequences from a seed. A 64-bit LCG (`state = state*6364136223846793005 + 1442695040888963407`) or xorshift64 is a few instructions and perfectly adequate for musical randomness. **Do not** use `rand()` (non-reentrant, poor quality, implementation-defined). |
| Determinism | Reseed from `SEED` on change so the same seed → same pitch/velocity sequence | Matches "SEED (sequence mutation)" requirement — musicians expect repeatable results. |
| Scale quantization | **Static `const` lookup tables of semitone offsets per scale** (`static const int8_t g_scales[NUM_SCALES][12]`), plus a free-frequency mode | Quantize the PRNG-derived pitch to the nearest scale degree via table lookup — branch-light, no runtime math. Free-freq mode bypasses the table. |
| Sequence generation timing | Advance the sequence on beat/16th boundaries using `host->get_beat_position()` | Transport-synced; compute step indices from beat position, not wall clock. |
### Build System
| Decision | Choice | Why |
|----------|--------|-----|
| Build tool | **Plain GNU Makefile** invoked inside the Docker image | Single output artifact (`dsp.so`), fixed toolchain, fixed flags. CMake's value (multi-target, dependency graphs, find_package, cross-toolchain files) is wasted here and adds a layer of indirection over a 5-line compile. The reference `build.sh` already uses a bare gcc invocation. |
| When CMake *would* be justified | If you later split into a reusable DSP library consumed by multiple hosts, or add a large offline test tree with many targets | Not the case for v1. Revisit only if the test harness grows large. |
| Docker invocation | `docker run --rm -v "$PWD:/workspace" -w /workspace ghcr.io/charlesvestal/schwung-builder:latest make` | Reproducible, matches Schwung's documented pipeline. Keep a `scripts/build.sh` wrapper as documented. |
| Suggested Makefile targets | `dsp.so` (release aarch64), `test` (native host harness — see Testing), `clean`, `deploy` (scp atomic rename) | Two compilers in play: `aarch64-linux-gnu-gcc` for the module, native `cc`/`clang` for the offline tests. Keep them as separate targets/flag sets in one Makefile. |
### Testing — Offline DSP Verification (no Move hardware)
| Layer | Approach |
|-------|----------|
| Host mock | Write a **mock `host_api_v1_t`** in the test harness: `sample_rate=44100`, `frames_per_block=128`, a stub `log` that prints, stub MIDI senders returning 0, a `get_beat_position` you can drive to simulate transport. This lets you call `move_plugin_init_v2(&mock_host)` natively. |
| Render-to-WAV | A test driver that calls `create_instance`, sends `set_param` / `on_midi` (trigger a kick), loops `render_block` over N blocks into an `int16` buffer, and writes a **WAV file** (trivial 44-byte header + PCM). Listen to the results; this is your primary "does it sound right" loop. |
| Determinism / regression | For GEN and any seeded path, render with a fixed seed and **hash or checksum the output buffer**; assert byte-stable across runs. Catches accidental nondeterminism (denormals, uninitialized state). |
| Numeric sanity | Assert no NaN/Inf in output (`isfinite` sweep), check output stays within `[-1,1]` before int16 conversion, verify silence-in → silence-out for filters. |
| Allocation guard | In the test build, `#define`/interpose `malloc`/`free`/`calloc`/`realloc` to `abort()` (or count calls) and assert **zero allocations occur during `render_block`**. This mechanically enforces the hardest constraint. |
| Framework | Plain C assertions + a tiny `assert`-based runner is sufficient; **Unity** (ThrowTheSwitch) is a good lightweight option if you want structure. Avoid heavyweight C++ frameworks — stay in C. |
| Native build caveat | Native tests validate *logic and sound*, not aarch64 codegen. Still smoke-test the actual `dsp.so` on-device via scp before shipping. NEON/FPCR-specific paths won't exercise on x86 hosts — keep those paths simple and guarded. |
## Memory Layout Recommendations
| Structure | Size | Placement | Notes |
|-----------|------|-----------|-------|
| Groove delay buffer | 88200 frames × 2 ch × 4 B = **~700 KB** (`float delay_buffer_l/r[88200]`) | **Inside the instance struct**, allocated once via a single `calloc` (or `malloc`+memset) in `create_instance` | This is the big one. Allocate off the audio thread at instance creation only. Circular buffer with `write_pos` and modulo (or power-of-two mask if you round `MAX_DELAY_FRAMES` up to 131072 for a branch-free `& mask`). |
| Wavetable banks (10 models) | `NUM_MODELS × BANDS × 2048 × 4 B` (e.g. 10×2×2049×4 ≈ **164 KB**) | **`static const` in `.rodata`, shared across all instances** | Not per-instance. OS maps once, read-only. Do NOT copy into the instance. |
| Scale tables, routing tables | < 1 KB | `static const` `.rodata` | Shared, read-only. |
| Per-voice scratch buffers | 128–256 frames × float | Inside instance struct | Reference uses `int16_t scratch[256]`; prefer `float scratch[256]` (2 ch × 128) to avoid double int16↔float conversion between inner engine and processing. |
| Filter/oscillator/mode state | Bytes | Inside instance struct | Trivial. |
- **Single allocation strategy:** Make the instance struct contain the 700 KB delay arrays *by value* and allocate the whole `bohm_instance_t` in one `malloc`/`calloc`. One allocation, freed once in `destroy_instance`. Simplest and guarantees no audio-thread allocation.
- If instance count is high (16 Movy tracks × 700 KB ≈ 11 MB), that's fine on Move's RAM, but confirm the delay length is actually needed at full 2 s for every use — consider a shorter default if memory pressure appears.
- `_Alignas(16)` the delay and scratch buffers for clean auto-vectorization.
- Zero-initialize all state in `create_instance` (avoids denormal/NaN startup and makes tests deterministic).
## Cross-Compilation Gotchas (aarch64, glibc 2.35)
| Gotcha | Mitigation |
|--------|------------|
| **glibc symbol versioning** — building against a *newer* glibc than the device's 2.35 records `symbol@GLIBC_2.36+` references that fail to load on-device | Build **inside `ghcr.io/charlesvestal/schwung-builder:latest`** (it is pinned to 2.35). Verify with `objdump -T build/dsp.so \| grep GLIBC` — every version tag must be `<= 2.35`. This is the single most important on-device-load check. |
| **Blanket `-ffast-math` pulling libmvec** — some setups link vectorized math (`_ZGV*` symbols) that may be absent on the device | Use the granular fast-math subset (above), not `-ffast-math`. Re-inspect `objdump -T` for any `libmvec`/`_ZGV` symbols; if present, add `-fno-math-errno` only or `-fno-tree-loop-vectorize` on math-heavy files. |
| **Wrong `-mcpu`/`-march`** emitting instructions the SoC lacks → SIGILL on-device | Omit `-mcpu` (baseline ARMv8-A) unless the Cortex-A53 assumption is confirmed. |
| **Undefined symbols in a `-shared` lib load lazily** — a missing symbol may not surface until the code path runs on-device | Build/link with `-Wl,--no-undefined` so unresolved symbols fail at link time, not at runtime on Move. Link `-lm`. |
| **`-fPIC` required** for the shared object | Always pass `-fPIC` (reference build already does). |
| **Not exporting exactly one init symbol** | `-fvisibility=hidden` + `__attribute__((visibility("default")))` on `move_plugin_init_v2`. Verify the export list with `objdump -T`. |
| **Deployment race** — overwriting a live `dsp.so` | Use the documented atomic scp-then-rename pattern. |
## Alternatives Considered
| Category | Recommended | Alternative | Why Not |
|----------|-------------|-------------|---------|
| Language | C11 | C99 | C99 works but loses `_Static_assert`, `_Alignas`, anonymous unions — all useful, zero cost on GCC/aarch64. |
| Language | C11 | C++ (no exceptions/RTTI/STL) | Explicitly out of scope; API is C ABI. C++ adds name-mangling and ABI surface for no benefit here. |
| Numeric format | float | Fixed-point (Q15/Q31) | aarch64 has a mandatory FPU; fixed-point is a legacy no-FPU pattern that adds scaling bugs and saves nothing. |
| Numeric format | float | double | ~2× slower and 2× memory traffic on A53; unnecessary at 44.1k. |
| Wavetable interp | Linear (+ band-limited tables) | 4-point cubic/Hermite | Cubic ~3× the cost, marginal benefit at kick fundamentals; band-limiting the source table is the real fix. |
| Wavetable storage | static const `.rodata` | Load from files at runtime | File I/O on/near audio thread is forbidden; static arrays are shared, deterministic, zero-load. |
| Filter | TPT SVF | ZDF Moog ladder | ZDF nonlinear solver ~2× CPU; inaudible difference at bass freqs (project decision). |
| Filter (swept) | TPT SVF | Biquad | Biquad zipper/instability under fast modulation; biquad kept only for static/modal use. |
| PRNG | LCG/xorshift/PCG | `rand()` | `rand()` is low quality, non-reentrant, implementation-defined — nondeterministic across libc. |
| Build | Makefile | CMake | CMake's multi-target machinery is overkill for one `dsp.so`; adds indirection. |
| Test framework | Plain C asserts / Unity | GoogleTest / Catch2 | C++ frameworks pull you out of C; unnecessary weight. |
## Installation / Build Commands
# Cross-compile the module (inside the Schwung builder image)
# Verify glibc symbol versions are all <= 2.35 (critical on-device gate)
# Run native offline tests (render-to-WAV, allocation guard, determinism)
# Deploy (atomic)
## Confidence Assessment
| Area | Confidence | Notes |
|------|------------|-------|
| C11 vs C99 | HIGH | C11 fully supported on GCC/aarch64; recommendation is low-risk. |
| Float vs fixed / FPU | HIGH | aarch64 mandates a hardware FPU (VFP+NEON shared register file) — verified against ARM architecture docs. |
| Wavetable interp (linear) | HIGH | Strong community + literature consensus that cubic isn't worth it vs larger/band-limited tables; especially true for low kick fundamentals. |
| TPT filter | HIGH | Matches logged project decision; Zavalishin VA Filter Design is the authoritative reference. |
| Modal PM approach | HIGH | Standard modal synthesis (damped resonators) is the textbook efficient percussion method. |
| Build system (Makefile) | HIGH | Single-artifact build; matches Schwung's documented pipeline. |
| Offline testing | HIGH | Mock-host + render-to-WAV is a well-established DSP testing pattern; API cleanly supports it. |
| `-mcpu=cortex-a53` | MEDIUM | Move SoC is very likely i.MX8M / Cortex-A53 but confirm before pinning `-mcpu`. |
| libmvec fast-math ABI risk | MEDIUM | Documented as a real cross-compile pitfall; mitigated by granular flags + objdump check. |
## Gaps to Address (for phase-specific research later)
- **Exact Move SoC / Cortex core** — confirm before setting `-mcpu`. Check Schwung docs or `/proc/cpuinfo` on-device.
- **How `render_block` triggering works** — the reference nests an inner engine; for internal models you need the actual note-on trigger path (MIDI via `on_midi`, or DR32/Movy pad trigger semantics). Belongs in an architecture/API deep-dive.
- **Band-limiting depth** — how many octave-band table variants each model actually needs; determine empirically once tables are generated (listen for aliasing on high-pitch settings).
- **Per-instance memory at 16 Movy tracks** — confirm Move RAM headroom for 16 × ~700 KB delay buffers; may motivate a shorter default delay length.
- **libmvec presence on-device** — verify the granular-flag build produces no `_ZGV*`/libmvec references via `objdump -T`.
## Sources
- [Zavalishin, "The Art of VA Filter Design" (Native Instruments PDF)](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_1.1.1.pdf) — TPT SVF reference (HIGH)
- [Roost Audio — Topology-Preserving Transform](https://www.roostaudio.com/audio-dsp/tpt) — TPT overview (MEDIUM)
- [JUCE dsp::FirstOrderTPTFilter reference](https://docs.juce.com/master/classdsp_1_1FirstOrderTPTFilter.html) — C++ TPT reference to study (MEDIUM)
- [Improving the Chamberlin Digital State Variable Filter (arXiv)](https://arxiv.org/pdf/2111.05592) — SVF background (MEDIUM)
- [KVR — wavetable interpolation discussions](https://www.kvraudio.com/forum/viewtopic.php?t=552862) — linear vs cubic consensus (MEDIUM)
- [KVR — wavetable synthesis, interpolation, aliasing](https://www.kvraudio.com/forum/viewtopic.php?t=349360) — imaging / band-limiting (MEDIUM)
- [Wikipedia — Wavetable synthesis](https://en.wikipedia.org/wiki/Wavetable_synthesis) — general (MEDIUM)
- [ARMv8 FPU and SIMD Execution Units (systemonchips.com)](https://www.systemonchips.com/armv8-fpu-and-simd-execution-units-scalar-floating-point-operations-in-aarch64/) — shared V register file, scalar FP unit (HIGH)
- [Demystifying ARM Floating Point Compiler Options (Embedded Artistry)](https://embeddedartistry.com/blog/2017/10/11/demystifying-arm-floating-point-compiler-options/) — hard-float on aarch64 (HIGH)
- [Laird, "Physical Modelling of Drums using Digital Waveguides" (thesis)](https://slab.org/software/laird/joel_laird_thesis.pdf) — modal vs waveguide drum modeling (MEDIUM)
- [Nord Modular Book — Percussion Synthesis](https://cim.mcgill.ca/~clark/nordmodularbook/nm_percussion.html) — modal/damped-oscillator percussion (MEDIUM)
- [Red Hat — How glibc handles backward compatibility](https://developers.redhat.com/blog/2019/08/01/how-the-gnu-c-library-handles-backward-compatibility) — symbol versioning (HIGH)
- [glibc symbol versioning example (peeterjoot.com)](https://peeterjoot.com/2019/09/20/an-example-of-linux-glibc-symbol-versioning/) — `symbol@GLIBC_x.y` mechanics (HIGH)
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

Conventions not yet established. Will populate as patterns emerge during development.
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

Architecture not yet mapped. Follow existing patterns found in the codebase.
<!-- GSD:architecture-end -->

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd:quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd:debug` for investigation and bug fixing
- `/gsd:execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->



<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd:profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
