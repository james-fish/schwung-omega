# Domain Pitfalls

**Domain:** Real-time C DSP plugin (kick synth + rumble + performer) for Ableton Move / Schwung
**Researched:** 2026-09-28
**Overall confidence:** HIGH for audio-thread + ARM64 + DSP numerics (verified against docs and real-world bug reports); MEDIUM for Schwung-specific buffer limits and multi-host semantics (only source is the project's own Context docs — flagged inline).

---

## Critical Pitfalls

Mistakes that cause rewrites, hard-to-debug audio glitches, or crashes on-device.

### Pitfall 1: Hidden allocation / blocking inside "innocent" C library calls on the audio thread
**What goes wrong:** `render_block`, `set_param`, `get_param`, `on_midi` all run on the `SCHED_FIFO 70` SPI audio thread (per `01_SCHWUNG_DEV_ARCHITECTURE.md` §2). The obvious offenders (`malloc`/`free`/`new`) are easy to avoid, but many stdlib functions allocate or take locks internally:
- `sprintf`/`snprintf`/`printf` family — glibc's formatted output pulls in locale data and can allocate internal buffers (especially with `%f` float formatting, wide chars, or large field widths). This is the single most likely sneak-in because building `ui_hierarchy` JSON and formatting `get_param` return values is naturally done with `snprintf`.
- `atof`/`strtod` — **locale-dependent** and can touch locale global state; `strtod` in particular has been observed allocating in some glibc paths for very long inputs. `atoi`/`strtol` are integer-only and effectively safe, but see Pitfall 12 (locale decimal separator).
- `pthread_mutex_lock`, `sleep`, `usleep`, `nanosleep`, any syscall.
- Hidden first-call initializers: `math.h` functions are fine, but anything touching `errno` via TLS on first use, `localtime`, `getenv`, `dlopen` (relevant to the deferred EXT-voice feature) all can block or allocate.

**Why it happens:** The API forces `get_param`/`set_param` onto the audio thread, and the natural way to serialize a JSON hierarchy or parse a parameter value is exactly the string code that is unsafe. Developers assume "no malloc" means "no explicit malloc."

**Consequences:** Intermittent buffer underruns → the exact "full-volume digital noise" / clicks class of bug, non-deterministic and nearly impossible to reproduce in a debugger. Priority inversion if a lock is contended.

**Prevention:**
- Serialize `ui_hierarchy` JSON **once at `create_instance`** into a pre-allocated `char` buffer in the instance struct; `get_param("ui_hierarchy", ...)` just `memcpy`s (bounded, no formatting). See Pitfall 5 for sizing.
- For numeric `get_param` returns, use a hand-rolled integer/fixed-point formatter (no `%f`, no locale). If floats must be formatted, do it with a small fixed-precision routine you own.
- Parse `set_param` values with a locale-independent hand-rolled `parse_float` (multiply-accumulate the mantissa; handle `-`, `.`), NOT `atof`/`strtod`. Cheap and provably RT-safe.
- Add a CI/dev guard: build a debug variant that `LD_PRELOAD`s a malloc that `abort()`s if called from the audio thread (or a simple thread-id assert in a wrapped allocator), and run a synthetic render loop.

**Detection:** Audible clicks under load that vanish at low CPU; `perf`/`ftrace` showing `render_block` occasionally exceeding the ~2.9ms block budget (128 frames @ 44.1kHz). A `malloc`-trap build firing.

**Confidence:** HIGH. Corroborated by Ross Bencina's "Real-time audio programming 101" and LMMS realtime conventions (both list `printf`/`malloc`/mutex as forbidden). The `atof`/locale hazard is documented glibc behavior.
**Phase:** Foundation phase (audio-thread contract + string handling utilities) — must be established before any DSP is written.

---

### Pitfall 2: ARM64 denormal floats not flushed to zero — CPU stalls in feedback/decay tails
**What goes wrong:** Every decaying signal in Omega (exponential pitch envelope, ducking release `duck_env += (1-duck_env)*0.005`, filter states, delay-line feedback, kick body tails) asymptotes toward zero and produces **denormal** (subnormal) floats. On ARM64/AArch64, if the per-thread FPCR flush-to-zero (FZ) bit is not set, denormal arithmetic is dramatically slower and can stall the pipeline.
**Why it happens:** Two ARM64-specific traps:
1. **FPCR is per-thread and NOT inherited** across `pthread_create` (unlike x86 MXCSR which IS inherited). So even if the host process sets FTZ, Schwung's audio thread may not have it — and Omega cannot assume the host did it.
2. `-ffast-math`/`crtfastmath.o` "sets" FTZ only in the thread that runs the CRT init; GCC can also optimize away runtime denormal checks under fast-math semantics, so a self-test can falsely pass while FPCR is unset on the real audio thread.

**Consequences:** Exactly the documented Mixxx bug (issue #16126): denormals in effects with feedback stall the CPU → underrun → full-volume digital noise. Under Omega's CPU budget (10-15%, alongside 10+ Movy tracks) this is the difference between shipping and constant crackle.

**Prevention:**
- On the very first entry of `render_block` (or a one-time flag checked there), set the FPCR FZ bit on the current thread:
  ```c
  uint64_t fpcr; __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
  fpcr |= (1u << 24); /* FZ */
  __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
  ```
  Do it in `render_block` (guaranteed to run on the audio thread), not `create_instance` — though `create_instance` also runs on the audio thread per the docs, setting it in both is belt-and-suspenders.
- Additionally add tiny DC/denormal-killing offsets or `x = x + 1e-20f - 1e-20f` guards in the hottest feedback loops (delay taps, ducking release) as defense in depth.
- Do NOT rely on `-ffast-math` alone. Prefer explicit FPCR set + targeted `-fno-math-errno` rather than global fast-math (which breaks `NaN`/`Inf` handling you may need for input sanitization).

**Detection:** Sudden CPU spikes only when signals are decaying to silence (i.e., after a kick, in the tail). Profiler shows time in FP instructions during "quiet" regions.

**Confidence:** HIGH. Directly corroborated by Mixxx issue #16126 (ARM64 audio thread missing FPCR FZ → full-volume noise) and ARM's own flush-to-zero documentation.
**Phase:** Foundation phase — the FPCR set must exist before any DSP is profiled, or all CPU numbers are wrong.

---

### Pitfall 3: Preset load / model switch tearing under concurrent render (state race)
**What goes wrong:** `set_param` and `render_block` both run on the audio thread in Schwung (per docs, they are all on the SPI thread). **If that is literally true, they are serialized and there is no data race** — but this must be verified, because:
1. In DR32/Movy hosting, UI parameter edits and audio rendering may be marshaled differently, and a host could deliver `set_param` from a UI thread.
2. A full preset load touches dozens of fields (active model, all kick/groove/performer params, tap volumes, filter coeffs). Even single-threaded, applying a preset field-by-field mid-block means the first half of a block renders with the old model and the second half with the new — audible zipper/discontinuity, and worse, a **model index change without re-initializing that model's DSP state** reads stale/garbage envelope and filter memory.

**Why it happens:** Naive implementations write directly into the live instance struct from `set_param`. Model switching is the sharp edge: changing `active_engine` mid-render without resetting the new engine's state, or while a voice is sounding, produces pops or NaNs.

**Prevention:**
- **Confirm the threading model first** (Pitfall 11). If truly single-threaded audio-thread-only, races are impossible but tearing-within-a-block still is.
- Use a **double-buffer + atomic pointer swap** for the full parameter set: `set_param` writes into a shadow/staging struct; a single `_Atomic` "generation" counter or a `stdatomic.h` `atomic_store` of a pointer signals `render_block` to adopt the new set **at a block boundary** (top of `render_block`), never mid-block. On ARM64, use C11 `_Atomic`/`atomic_load`/`atomic_store` (lock-free for pointer/int) — do NOT use a mutex.
- Model switching: on adopting a new `active_engine`, explicitly zero/re-init that engine's state (envelopes, filter memory, phase accumulators) and optionally crossfade one block to avoid a click.
- Individual live knob turns can be applied directly (they're single scalar writes and idempotent); only the bulk preset-load path needs the staging swap.

**Detection:** Clicks/pops precisely when switching models or loading presets; occasional NaN output (silence or full-scale noise) after a model change.

**Confidence:** HIGH on the tearing/state-init hazard; the "is there an actual cross-thread race" part is MEDIUM and depends on Schwung's real threading (Context docs say single audio thread — verify on-device).
**Phase:** Preset system phase, but the staging-struct pattern should be designed in the Foundation phase so all params flow through it.

---

### Pitfall 4: Groove delay buffer — hardcoded BPM, wrap-around, and cold-start garbage
**What goes wrong:** The reference in `04_BOHM_SCHWUNG_MODULE_DESIGN.md` has several latent bugs:
1. `int tap_interval = (int)(sample_rate * 0.125f);` is **hardcoded to a 16th note at 120 BPM** (0.125s = one 16th at 120BPM). The taps will not sync to the host tempo. Omega has `host->get_beat_position()` and clock — tap interval must be derived from live BPM, or the "groove" is only correct at 120.
2. **Cold start:** `delay_buffer_l/r` are large (`88200` frames each) and, if not explicitly zeroed in `create_instance`, contain garbage → loud noise on first taps. Even zeroed, for the first `4 * tap_interval` samples the read positions point at not-yet-written silence, which is correct (silence) — but only if zero-initialized.
3. **Wrap math:** `(write_pos - (t+1)*tap_interval + MAX_DELAY_FRAMES) % MAX_DELAY_FRAMES` is only safe while `(t+1)*tap_interval < MAX_DELAY_FRAMES`. Tap 4 at slow tempos: `4 * tap_interval` at, say, 60 BPM 16ths = `4 * 0.25s * 44100 = 44100` frames — fine. But 4 taps of **quarter notes** or dotted values, or a "TAP LENGTH" that scales interval, can exceed `88200`, making the modulo produce a wrong (aliased) read position rather than clamping. Adding a single `MAX_DELAY_FRAMES` only corrects for one wrap; large multiples underflow past it.
4. **Non-integer delay:** BPM-derived tap intervals are rarely integer samples. Rounding to `int` detunes the rumble pitch and causes slow phase drift between taps. Fractional (interpolated) read is usually needed for musical rumble.

**Prevention:**
- Derive `tap_interval` from `host->get_beat_position()`/clock each block (or on tempo change), not a constant.
- Clamp total delay: `max_tap_offset = MAX(1, MIN((t+1)*tap_interval, MAX_DELAY_FRAMES - 1))`. Guarantee the largest tap offset never reaches or exceeds buffer size; size the buffer for the slowest supported tempo × longest tap.
- Compute read index with a general modulo that handles any positive/negative magnitude: `read_pos = ((write_pos - offset) % N + N) % N` — but ensure `offset < N` first.
- `memset` both delay buffers to 0 in `create_instance` (the docs confirm all buffers pre-allocated there — make zeroing explicit).
- Use linear-interpolated fractional read for tap positions.

**Detection:** Rumble pitch changes with project tempo unexpectedly (or doesn't when it should); noise burst on first kick after load; garbage/aliased taps at extreme LENGTH/tempo settings.

**Confidence:** HIGH — bugs are directly visible in the reference code in `04_BOHM_SCHWUNG_MODULE_DESIGN.md`.
**Phase:** Groove rumble phase.

---

### Pitfall 5: `ui_hierarchy` JSON overflows `get_param` buffer — silent truncation
**What goes wrong:** `get_param(instance, "ui_hierarchy", buf, buf_len)` must return the full JSON describing every page and parameter. With 10 models, each contributing 6 model-specific slots on Kick Page 2, plus Groove Page 1/2, Performer, Root macros, and Preset pages, the JSON can be several KB. If `buf_len` is smaller than the serialized JSON, the schema is truncated → invalid JSON → the host either shows a broken/empty UI or (worse) crashes its parser.
**Why it happens:** The host allocates `buf` and passes `buf_len`; the plugin does not control it. The documented interface (`get_param(..., char *buf, int buf_len)`) gives a length but the max is host-defined and **not stated in the Context docs** — this is an unknown that must be measured on-device.
**Prevention:**
- **Measure the actual `buf_len` Schwung/DR32/Movy pass** early on-device (log it via `host->log`). This is a Phase-1 spike.
- Keep the hierarchy compact: use short keys, avoid whitespace, avoid repeating all 10 models' param definitions inline if a context-sensitive scheme (page 2 slots re-labeled per active model via `set_param` refresh) can keep the static JSON small. Model-specific labels can be delivered dynamically rather than enumerating a 10× cartesian product.
- If `get_param` is called with a `buf_len` smaller than the serialized size, return the **required length** (if the API convention allows) or at minimum `host->log` a loud warning and never write a truncated-but-valid-looking fragment.
- Pre-serialize once (Pitfall 1) and know its exact byte length as a compile-time-ish constant; assert it against observed `buf_len`.

**Detection:** UI shows fewer pages/knobs than expected, or blank; host log parse errors. Truncation appears only after adding the Nth model/page.

**Confidence:** MEDIUM — the truncation *mechanism* is certain from the API signature; the actual Schwung buffer limit is unverified (Context docs do not state it). Flag as an on-device measurement task.
**Phase:** UI hierarchy phase (with a measurement spike in Foundation).

---

### Pitfall 6: glibc 2.35 symbol versioning — binary won't load on Move
**What goes wrong:** The binary must run against **exactly glibc 2.35** on Move. Linking against a newer glibc (e.g. building outside the pinned Docker image) embeds symbol versions like `GLIBC_2.36`/`memcpy@GLIBC_2.38` that the device's loader can't resolve → `dsp.so` fails to `dlopen` with "version not found," or the whole Shadow UI refuses to load the module. Newer `math.h` symbols (`exp`, `pow`, `log` used heavily in pitch envelopes and filter coeffs) are common offenders because they gained new versioned symbols over glibc releases.
**Why it happens:** Building on a dev machine's native toolchain instead of `ghcr.io/charlesvestal/schwung-builder:latest`, or a CI job pulling a newer builder tag.
**Prevention:**
- Build **only** in the pinned Docker image (`ghcr.io/charlesvestal/schwung-builder:latest`) — verify it targets glibc 2.35 and pin by digest, not `:latest`, once confirmed (a `:latest` retag could silently bump glibc).
- After build, run `aarch64-linux-gnu-objdump -T build/dsp.so | grep GLIBC_` and assert no symbol requires `> 2.35`. Add this as a CI gate.
- Prefer static-linking libm if feasible, or use `-Wl,--wrap` only as a last resort.
- Avoid pulling in `__isoc23_*` / `__isoc99_*` variants (sscanf/strtod) — another reason to hand-roll parsing (Pitfall 1).

**Detection:** Module loads fine in the Docker/QEMU test but fails to appear or errors on the physical Move; `objdump -T` shows a `GLIBC_2.36+` requirement.
**Confidence:** HIGH — standard cross-compilation symbol-versioning behavior; the glibc 2.35 pin is explicit in the Context docs.
**Phase:** Foundation / build-pipeline phase.

---

### Pitfall 7: Sidechain ducking self-triggering off the kick's own amplitude
**What goes wrong:** The reference triggers ducking with `if (fabsf(kick_l) > 0.1f)`. Using the **kick's instantaneous sample amplitude** as the trigger means: (a) ducking fires on every sample above 0.1 during the entire kick body, not once per hit; (b) the kick's decaying tail crosses 0.1 repeatedly, causing the duck to re-trigger and chatter; (c) a low-velocity or sub-heavy kick whose peak sits below 0.1 never ducks; (d) it ducks on the kick oscillation zero-crossings oddly because `fabsf` of a sine dips below threshold every half-cycle.
**Why it happens:** Conflating "kick is playing" with "kick sample is loud right now." Sidechain ducking should be triggered by the **note/hit event** (the trigger that started the kick voice), not by reading the audio amplitude.
**Prevention:**
- Trigger ducking from the kick's **trigger event** (`on_midi` note-on or the internal voice trigger), setting `duck_env` to `1 - duck_depth` once at hit time, then releasing via the smoothing coefficient.
- If envelope-follower-style ducking is genuinely wanted, use a proper peak/RMS follower with attack/hold, not a bare threshold, and hold through the kick body.
- Make the release coefficient sample-rate- and DUCK-REL-parameter-derived, not the hardcoded `0.005`. `0.005` per sample @ 44.1kHz is a ~4.5ms time constant — far shorter than the 80-150ms the rumble doc specifies; the reference constant contradicts the design intent.

**Detection:** Ducking "flutters" or breathes during the kick body; rumble volume pumps at audio rate; duck depth feels inconsistent across kick models/velocities.
**Confidence:** HIGH — the bug is visible in the reference code and contradicts `03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` §Stage 5 (80-150ms release).
**Phase:** Performer/ducking phase; the trigger plumbing should be defined in Foundation (how a hit event is signaled).

---

## Moderate Pitfalls

### Pitfall 8: `fast_tanh` asymmetry and non-monotonic/overflow behavior
**What goes wrong:** The `03` doc defines an asymmetric clipper (`tanh(x)` for x≥0, `x/(1+|x|)` for x<0) but the `04` reference implements `x/(1+x)` for x≥0 and `x/(1-x)` for x<0 — **a different function, and a dangerous one**: `x/(1-x)` for `x<0` is the rational-tanh's *positive*-side formula reused, but `x/(1+x)` for `x≥0` **blows up / goes non-monotonic as x→ toward values where the denominator misbehaves** and does NOT saturate to ±1 the way `x/(1+|x|)` does. Specifically `x/(1+x)` → 1 only as x→∞ but exceeds sane range for moderate x and is asymmetric in an unmusical way. Also, feeding the sum of kick+rumble (which can exceed ±1 before clipping) into a clipper that isn't bounded to ±1 defeats the "+4.6dB headroom before hard clip" goal.
**Why it happens:** Two different formulas across two docs; the implemented one is a typo-level error. Asymmetric clippers are legitimate (they add even harmonics, per Stage 3) but must be *intentionally* asymmetric and always bounded.
**Prevention:**
- Use a single, tested, **bounded** soft-clip: the symmetric rational `x / (1 + |x|)` (always in (-1,1)) or a proper `tanh` approximation (e.g. a bounded Padé/`tanh` polynomial). If asymmetry is wanted, apply a small DC bias before a symmetric clipper, then remove DC after — controllable and always bounded.
- Guarantee `|y| < 1.0` for all finite inputs before the `int16` cast; then apply the `+4.6dB` (×1.7) makeup *inside* the headroom and a final hard clamp to [-1, 1] before scaling by 32767 (see Pitfall 9).
- Unit-test the clipper across `x ∈ [-10, 10]` asserting monotonic and bounded.

**Detection:** Distortion sounds harsh/gated rather than warm; output DC offset; asymmetric waveform where symmetry was intended.
**Confidence:** HIGH — the two docs disagree and the implemented formula is unbounded on the positive side.
**Phase:** Performer/soft-clip phase.

### Pitfall 9: `int16` output conversion — clipping, rounding, and asymmetric range
**What goes wrong:** `out_lr[f*2] = (int16_t)(mix_l * 32767.0f)` has three bugs: (a) **no clamp** — if `mix_l` exceeds ±1 (very possible before/without the soft clip, or with makeup gain), the cast **wraps/overflows** into full-scale opposite-polarity noise (the classic loud-glitch); (b) truncation toward zero instead of rounding adds a DC-ish bias and quantization asymmetry; (c) `int16` range is asymmetric ([-32768, +32767]) so scaling positive and negative by the same 32767 is fine, but multiplying by 32768 on the negative side would overflow +full-scale.
**Prevention:**
- Always clamp before cast: `float c = mix_l < -1.f ? -1.f : (mix_l > 1.f ? 1.f : mix_l); out = (int16_t)lrintf(c * 32767.0f);`
- Use `lrintf`/round-to-nearest (RT-safe, no allocation) not C truncation.
- Sanitize NaN/Inf before conversion (`if (!isfinite(c)) c = 0.f;`) — a single NaN from a filter blowup otherwise becomes full-scale noise.
**Detection:** Occasional loud pops at high levels; DC offset on scope; NaN → sustained noise.
**Confidence:** HIGH.
**Phase:** Foundation (output stage) — shared by all models.

### Pitfall 10: Kick pitch envelope & filter-coeff numerics at extreme parameters
**What goes wrong:** The pitch envelope `f(t) = f_start·e^(-t/τ) + f_fund`:
- As `f(t) → f_fund`, no precision problem in the frequency itself, but the **phase accumulator** `phase += 2π·f(t)/sr` accumulates error over long tails; if `phase` is a `float` it loses precision after seconds of running → detuning/beating. Use `double` phase or wrap phase every cycle.
- Very small `τ` (fast 909 sweep) with per-block (not per-sample) envelope updates causes zipper/stepping; very large `LENGTH` risks `exp` underflow to denormal (see Pitfall 2).
- **Filter coefficients** (TPT SVF chosen per Key Decisions) blow up at extreme cutoff: cutoff approaching Nyquist (`g = tan(π·fc/sr)` → ∞) or ≤0. The rumble LPF at 120-180Hz is safe, but a DJ filter sweeping the full range and any resonance near self-oscillation needs coefficient clamping. `tan` near π/2 → huge `g` → NaN.
- Resonance `Q` → ∞ at self-oscillation makes SVF denominator → 0.

**Prevention:**
- `double` phase accumulators (or fixed-point) for oscillators; wrap to [0, 2π).
- Clamp filter cutoff to `[20 Hz, 0.45·sr]` and `Q`/resonance to a safe max before computing coefficients.
- Precompute `exp` decay as a per-sample multiplier (`coeff = expf(-1/(τ·sr))`, then `f *= coeff` each sample) rather than calling `expf` per sample — cheaper and monotonic; guard `f` with a floor at `f_fund`.
- Update envelopes/coeffs per-sample or with parameter smoothing to avoid zipper.
**Detection:** Detuned/beating long kicks; NaN/silence at filter extremes; zipper noise on fast knob moves.
**Confidence:** HIGH for the general mechanisms; per-model specifics (PHY damped-oscillator stability, wavefolder aliasing) below.
**Phase:** Kick engine phase (per-model), with the phase/coeff-clamp utilities in Foundation.

### Pitfall 11: Wavefolder aliasing (HRD/DIG/TRS and rumble saturation)
**What goes wrong:** The wavefolder `|(x+Vth) mod 4Vth − 2Vth| − Vth` and any hard/asymmetric clipper generate high-order harmonics that exceed Nyquist and **alias** back down as inharmonic, metallic artifacts — most audible on the tonal kick body and sustained rumble. At 44.1kHz with no oversampling, aggressive fold/crush on a 50-150Hz fundamental folds energy well above Nyquist.
**Prevention:**
- Oversample the fold/clip stage 2-4× (upsample → fold → lowpass → downsample) for the models where drive is central (HRD, and rumble Stage-3 saturation). This costs CPU — budget it against the 10-15% ceiling; may only be affordable on the master soft-clip and the HRD drive, not everywhere.
- Alternatively use antialiased/bandlimited fold approximations (ADAA — antiderivative anti-aliasing) which is cheaper than brute oversampling.
- Keep fold amount musically bounded; expose "FX AMT" with a range that doesn't push extreme aliasing.
**Detection:** Metallic, gritty, pitch-inharmonic artifacts on heavy drive that get worse at higher fundamental pitch; spectrogram showing aliased reflections.
**Confidence:** MEDIUM-HIGH — aliasing from waveshaping is well-established DSP; the specific audibility depends on Omega's material (bass-heavy → somewhat forgiving, but drive-forward techno is exactly where it bites).
**Phase:** Kick FX phase; decide oversampling budget during CPU-budgeting spike.

---

## Minor Pitfalls

### Pitfall 12: Locale decimal separator breaks `atof` parsing
**What goes wrong:** If the module ever runs where the C locale uses `,` as the decimal separator, `atof("0.5")` returns `0`. Combined with Pitfall 1, this is another reason to hand-roll a locale-independent float parser that always treats `.` as the separator.
**Prevention:** Own parser; never `atof`/`strtod`.
**Confidence:** HIGH. **Phase:** Foundation.

### Pitfall 13: Scratch buffer exactly sized, no margin
**What goes wrong:** `int16_t scratch_kick_buf[256]` fits exactly 128 stereo frames (128×2). The docs say blocks are "typically 128 frames" — if a host ever passes >128 frames, the inner render overruns the scratch buffer → stack/struct corruption.
**Prevention:** Size scratch buffers to a known `MAX_BLOCK` (e.g. 512 stereo = 1024) and assert `frames*2 <= capacity` at the top of `render_block`; clamp or split if exceeded. Read `host->frames_per_block` at `create_instance` and size accordingly.
**Confidence:** HIGH (visible in reference struct). **Phase:** Foundation.

### Pitfall 14: NEON/auto-vectorization alignment and `-O3` assumptions
**What goes wrong:** `-O3` on aarch64 auto-vectorizes loops (the per-frame groove loop, filters). AArch64 NEON tolerates unaligned loads but generated code and any hand-written intrinsics can assume 16-byte alignment; misaligned SIMD on some paths is slower or (with intrinsics like `vld1q` on over-aligned assumptions) incorrect. Also `-O3 -ffast-math` reorders FP ops, which can change denormal/NaN behavior (ties into Pitfalls 2 and 10).
**Prevention:** Align hot arrays (delay buffers, wavetables) with `__attribute__((aligned(16)))`. Avoid global `-ffast-math`; if hand-writing NEON, use unaligned intrinsics explicitly. Verify audio output bit-for-bit between `-O0` and `-O3` builds on a test vector to catch fast-math-induced divergence.
**Confidence:** MEDIUM (general aarch64/GCC behavior; no Omega-specific evidence yet). **Phase:** Foundation build config; revisit if hand-optimizing.

---

## Multi-Host Compatibility Constraints (Schwung / DR32 / Movy)

Same `dsp.so` loaded three ways — assumptions that break:

| Assumption | Breaks because | Mitigation |
|---|---|---|
| "One instance exists" | DR32 calls `create_instance` per pad (up to 32); Movy up to 16 tracks. Omega's ~700KB delay buffer × N instances = up to ~22MB (DR32) — may exceed Move RAM budget. | Make delay-buffer size configurable / smaller default; verify total memory across max instance count; consider a shared read-only wavetable ROM (static const) not duplicated per instance. |
| "`render_block` called at a steady rate with fixed `frames`" | DR32 renders each pad into a pad buffer; Movy renders 16 chains. Block size and call frequency can differ; a pad may be rendered only when active. | Never assume wall-clock time from block count; derive time from `host->get_beat_position()` and accumulated frames. Handle `frames` variance (Pitfall 13). |
| "UI hierarchy interpreted identically" | Movy auto-renders Elektron-style pages from `ui_hierarchy`; DR32 exposes params at pad level; Schwung uses the 8-encoder page nav. `canvas` types and deep nesting may render differently or be ignored in DR32. | Keep hierarchy portable; don't rely on host-specific rendering of `canvas`; test all three hosts. Root-page bidirectional macros must degrade gracefully if a host flattens the hierarchy. |
| "`pad_layout: drums` + MIDI note = one voice" | In DR32, Omega is one pad's engine — its own internal pad-layout/model selection may conflict with DR32's pad semantics; note routing differs from a Schwung slot. | Define clearly whether Omega responds to a single trigger note (as a DR32 pad engine) vs. a keyboard; document per-host trigger behavior. |
| "State/preset persistence is the host's job" | Each host may persist instance state differently (or not call the same save/restore path). | Preset system uses module_dir JSON files enumerated at init (per PROJECT.md) — verify `module_dir` is valid and writable in all three hosts; file writes must happen off the audio thread. |
| "EXT voice `dlopen` works" (v2, deferred) | Nesting a `plugin_api_v2_t` engine inside Omega inside DR32 = double nesting; `dlopen` on the audio thread is forbidden, and recursive hosting may not be supported by all hosts. | Deferred to v2 (per PROJECT.md) — keep the inner-engine pointer/API abstraction but do all `dlopen` at init, never in `render_block`. |

**Confidence:** MEDIUM — instance/hosting mechanics come from `01_SCHWUNG_DEV_ARCHITECTURE.md` §5 (project's own docs); exact memory ceilings and per-host UI rendering must be verified on-device.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|---|---|---|
| Foundation / build pipeline | glibc 2.35 symbol mismatch (P6); FPCR FTZ unset (P2); no malloc-trap harness (P1) | Pin Docker by digest, `objdump -T` gate, set FPCR in `render_block`, add RT-safety test build |
| Foundation / param plumbing | Hand-rolled locale-free parse/format missing (P1, P12); staging struct for preset swap (P3) | Build parse/format utils + atomic staging swap before any DSP |
| Kick engine (per model) | Phase-accum precision, coeff blowup at extremes (P10); denormal tails (P2) | double phase, clamp coeffs, per-sample exp multiplier, denormal guards |
| Kick FX / drive | Wavefolder aliasing (P11); unbounded/typo soft-clip (P8) | Oversample or ADAA the drive stages; single bounded clipper |
| Groove rumble | Hardcoded 120BPM tap interval, wrap/cold-start bugs, integer tap detune (P4) | Tempo-derived fractional taps, clamp offsets < N, memset buffers |
| Performer / ducking | Amplitude self-trigger + wrong release constant (P7) | Trigger from hit event; parameterized release matching 80-150ms |
| Output stage | int16 overflow/no-clamp/NaN (P9) | Clamp + isfinite + lrintf |
| UI hierarchy | JSON truncation vs host `buf_len` (P5) | Measure buf_len on-device; pre-serialize; compact schema |
| Preset system | Mid-block tearing, model-switch without state reset (P3) | Adopt staged params at block boundary; re-init engine state on switch |
| Multi-host validation | Instance count RAM, block-size/rate variance, UI interpretation (table above) | Test on Schwung + DR32 + Movy; verify memory at max instances |

---

## Sources

- Ross Bencina, "Real-time audio programming 101: time waits for nothing" — http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing (HIGH — forbidden functions: malloc/new/printf/mutex/syscalls on audio thread)
- LMMS "Realtime Conventions" wiki — https://github.com/LMMS/lmms/wiki/Realtime-Conventions (MEDIUM — corroborates forbidden-function list)
- timur.audio, "Using locks in real-time audio processing, safely" — https://timur.audio/using-locks-in-real-time-audio-processing-safely (MEDIUM — atomics vs mutex, lock-free patterns)
- Mixxx issue #16126, "ARM64: Audio thread missing flush-to-zero (FPCR FZ bit) causes full-volume digital noise with stacked effects" — https://github.com/mixxxdj/mixxx/issues/16126 (HIGH — direct real-world corroboration of P2)
- ARM Developer, "Flush-to-zero" — https://developer.arm.com/documentation/ddi0406/c/Application-Level-Architecture/Application-Level-Programmers--Model/Floating-point-data-types-and-arithmetic/Flush-to-zero (HIGH — FZ semantics, Advanced SIMD always flushes)
- KVR Audio DSP forum, "Are denormals a problem on arm64/M1 systems?" — https://www.kvraudio.com/forum/viewtopic.php?t=573073 (MEDIUM — FPCR per-thread, not inherited)
- Project Context docs `01_SCHWUNG_DEV_ARCHITECTURE.md`, `03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md`, `04_BOHM_SCHWUNG_MODULE_DESIGN.md`, `PROJECT.md` (MEDIUM — authoritative for Schwung specifics; not independently verifiable, and reference code contains the bugs flagged in P4/P7/P8/P9/P13)
