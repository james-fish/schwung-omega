# Feature Landscape

**Domain:** Native C Schwung module for Ableton Move — multi-model techno kick synth + rumble generator + performance mixer (Ohm Force Bohm-inspired)
**Researched:** 2026-09-28
**Overall confidence:** HIGH for Schwung conventions and DSP patterns (verified against real repos: `charlesvestal/schwung`, `legsmechanical/schwung-dr32`); MEDIUM for generative-sequencer specifics (WebSearch + domain reasoning)

---

## Executive Summary

Omega sits at the intersection of three product categories: (1) **multi-engine drum synths** (Bohm, and Schwung's own DR32/Forge/Simian/Sophie), (2) **techno rumble generators** (multi-tap sub-bass delay tools), and (3) **performance mixer/FX strips** (sidechain + DJ filter + clipper). Each category has well-established table stakes. The differentiator for Omega is packaging all three into a *single* Schwung module with a curated bidirectional performance-macro root page — something no existing Schwung module does, and something the hardware Bohm only achieves across three separate Eurorack modules.

Critically, research against the real DR32 and Schwung source reveals a strong, battle-tested set of conventions Omega should follow rather than invent. The two most load-bearing findings: (a) the **`dr32_engine_ops` vtable pattern** is the canonical way to dispatch `render_block` across many synthesis engines in a single C module, and (b) Schwung persistence flows through a host-managed **`state` JSON blob** via `get_param`/`set_param`, *not* through modules writing their own files on the audio thread. The PROJECT.md "module_dir JSON preset files" requirement is a legitimate *second, module-owned* user-preset layer, but it must be reconciled with the host `state` mechanism — this is the single most important clarification for requirements.

---

## Table Stakes

Features users of this product category expect. Missing = the module feels incomplete or broken for its category.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Multiple selectable kick models with a single MODEL selector | Bohm's whole identity is multi-engine; every Schwung drum synth (Forge, Simian, Sophie) offers voice/algorithm selection | Med | Use engine-vtable dispatch (see Architecture); models are the product |
| Per-model context-sensitive parameter pages | Move has 8 encoders; each model exposes different controls. DR32 gates knob pages on `ui_family`/`ui_engine` via `visible_if` | Med | `ui_hierarchy` must swap Kick Page 2's 6 model slots on active model |
| Universal kick controls (PITCH, LENGTH, SUSTAIN, pitch CURVE 808↔909, ATTACK) | Every kick synth in this space has these; the 808/909 pitch-curve knob is a genre signature | Low | Exponential pitch envelope: `f(t)=f_start·e^(−t/τ)+f_fund`, τ≈15ms for 909 |
| Transient/click shaping independent of body (TRS DEC, TRS TNE) | Separating attack snap from sub boom is a defining technique for 4-on-floor kicks | Low | Noise/impulse burst + one-pole tone filter |
| Post-kick drive/saturation FX (soft-clip, sat, fold, crush) | Low-freq sines need harmonic generation to be audible on small speakers; universal in the category | Low | `fast_tanh` + wavefolder + bitcrush already specced in context docs |
| Multi-tap tempo-synced rumble (the Groove voice) | This IS the "rumble kick" technique; the reason the product exists | Med | 4× 16th-note taps from a circular delay buffer; tempo from `get_beat_position()` |
| Rumble mono-summing + low-pass color filter | Club sub-bass must be phase-aligned mono below ~150Hz; established rumble rule | Low | `y=0.5(L+R)`, then LPF at 120–180Hz |
| Sidechain ducking of rumble by the kick | Prevents low-end phase cancellation; core to the rumble sound | Med | Envelope follower; see implementation hints below |
| Master volume + end-of-chain soft clipper | Prevents hard digital clipping; every performance strip has this | Low | tanh limiter with headroom |
| Preset save/load of full module state | Users expect to recall their dialed-in kicks; every Schwung module persists via host `state` blob | Med | See preset-system finding — reconcile host state vs module-owned presets |
| Loads unmodified in Schwung slot, DR32 pad, and Movy track | Cross-host compatibility is a stated Schwung norm; a module that only works in one host is broken | Med | Single `dsp.so` + `module.json`; `ui_hierarchy` served via `get_param` |
| Zero-allocation, zero-I/O audio thread | Hard requirement of the platform, not optional | Med | All buffers pre-allocated in `create_instance` |

---

## Differentiators

Features that set Omega apart. Not strictly expected, but high-value.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| All-in-one kick + rumble + performer in ONE module | Hardware Bohm needs 3 modules + a case; no Schwung module combines these | High | The core value prop; single-module decision already made in PROJECT.md |
| Bidirectional performance-macro root page | 8 curated macros (MODEL, MASTER, PITCH, LENGTH, RUMBLE, TAP1, DUCK, DJ FILT) give immediate hands-on control; sub-pages update the macros and vice versa | Med-High | Bidirectional sync is the tricky part — macro edits must write through to owning param and reflect back |
| 10 distinct synthesis methods (FM2, FM4, PHY, WTR, HRD, DIG, TRS, ANA, USR, GEN) under one roof | Covers 808 sub-boom → 909 punch → hard-techno distortion → physical woody → generative in one instrument | High | Hybrid fidelity strategy already decided; PHY simplified to damped oscillators |
| GEN model: generative hypnotic rumble sequencer | The HPN-equivalent; turns the Groove circuit into an evolving 16th-note pitch sequencer — a genuine "wow" feature for hypnotic techno | High | See generative-sequencer patterns below |
| DJ filter as a single bidirectional performance knob (LP↔neutral↔HP) with resonance | One-knob sweep is a live-performance staple; pairs naturally with Move's encoders | Low-Med | TPT state-variable filter, crossfade LP/HP outputs |
| USR model: load custom WAV/wavetable from device storage | User-generated content dramatically extends sonic range; off-thread file load only | Med | File read at init/param-change on host thread, never audio thread |

---

## Anti-Features (Do NOT build in v1)

Features that are either hardware/CV-only, architecturally risky on Move, or out of scope. Several are already flagged in PROJECT.md; this section confirms and enumerates them with reasoning for the requirements consumer.

| Anti-Feature | Why Avoid in v1 | What to Do Instead |
|--------------|-----------------|--------------------|
| **CV jacks: `TAPS CV` input, `TAPS OUT` output** | Physically meaningless in software — there are no patch cables. Hardware-only. | Nothing. Expose tap levels as params; a shared macro can drive all 4 taps if wanted |
| **`TAPS OUT` envelope routing modes (GROOVE / I BOHM / PERF / BOHM)** | These select which envelope goes to a CV jack that doesn't exist | Skip entirely |
| **Beat Roll / Slip Roll (buffer-repeat performance FX)** | Requires beat-synced capture loop + significant complexity to feel good; hard to do well in a v1 | Defer to v2 (already in PROJECT.md Out of Scope) |
| **EXT voice hosting (dlopen an external kick module as inner engine)** | `dlopen` + runtime module enumeration is heavy; 10 internal models cover the range | Defer to v2 (already scoped out). Internal models only in v1 |
| **MIDI-pitched melodic bass voice** | Different use case; GEN covers generative basslines | Defer to v2 (already scoped out) |
| **Post-EQ 3-band shelf (Bohm system setting)** | Performer fits in one page without it; Move's screen real estate is scarce | Defer; rely on kick/rumble filters + drive |
| **Hardware "snapshot" pot-position morphing / Producer-mode randomization** | Bohm's live snapshot recall is a hardware performance workflow; the preset system covers recall | Preset load/save covers state recall. Consider param randomize as a stretch, not v1 |
| **Full ZDF Moog ladder filter** | ~2× CPU of TPT SVF; audible difference is minimal at bass freqs | TPT state-variable filter (decision already logged) |
| **Full physical-modeling waveguide (PHY)** | Waveguide is CPU-expensive; budget is tight (10–15% shared with 10+ tracks) | Damped-oscillator functional equivalent (decision already logged) |
| **Runtime sample-rate negotiation** | Move is fixed 44.1kHz | Hardcode 44100; precompute all rate-dependent constants once |
| **Module writing its own preset files on the audio thread** | Violates the zero-I/O rule and duplicates the host `state` mechanism | See preset finding — host `state` blob is primary; module-owned files only off-thread at init |

---

## Deep-Dive Findings (the 7 research questions)

### 1. Preset system design for Schwung modules — HIGH confidence

**Finding: Schwung's canonical persistence is a host-managed `state` JSON blob, not module-written files.**

Verified from `legsmechanical/schwung-dr32` `dsp/dr32_state.h`:
- The host persists a chain slot by asking the module to serialize itself into a **`state` blob** (a JSON string), and restores it by handing the blob back. In DR32 this is `dr32_state_write()` / `dr32_state_read()`, wired to `get_param`/`set_param`.
- **Critical constraint discovered in the field:** the host **caps the state-blob size it will read back**. DR32 hit this — a full pretty-printed dump exceeded the cap and silently restored at defaults (dated 2026-08-06 in the source comment). DR32's fix: write a *baseline* after load and then persist only params that differ. **Omega must keep its `state` blob compact** (omit params at default, no pretty-printing).
- `create_instance(module_dir, json_defaults)` receives `json_defaults` to bootstrap initial values without doing file I/O.
- Module directory files (`module.json` manifest, `presets.json`/factory presets, `config.json` for web-UI settings) are read on the **host thread**, never the audio thread. DR32 reads `.ablpreset` kits and samples via a two-phase `prepare` (any thread, slow, does I/O + decode) / `apply` (kit-owning thread, no I/O) split.

**Implication for Omega's PROJECT.md "module_dir JSON preset files" requirement:** This is a legitimate *second layer* (module-owned named user presets, like Bohm's SD-card presets) that sits *on top of* the host `state` mechanism. Reconcile as follows:
- **Layer 1 (mandatory):** Implement `get_param("state")`/`set_param("state")` returning a compact JSON blob of active model + all params. This is how Schwung/DR32/Movy actually persist a slot. Without it, Omega loses state on reload.
- **Layer 2 (the PROJECT.md preset pages):** Named user presets as JSON files in `module_dir`. Enumerate available preset names **at `create_instance`** (host thread) into a pre-allocated list. "Load preset" applies a cached in-memory param set (no file read on audio thread). "Save" must be deferred to a host-thread context — via a `set_param("save_preset", name)` that stages the write, executed off the audio callback. **Do not `fopen` inside `render_block`.**

**Convention summary (per real repos):** each module lives in its own dir with `module.json` (generated manifest) + `module.def.json` (authored source in some modules) + optional `presets.json` + `config.json` + `ui.js`/`ui_hierarchy` + `dsp/`.

### 2. Multi-model architecture — the dispatcher pattern — HIGH confidence

**Finding: use a vtable of function pointers per engine, selected by an integer ID.** Verified from DR32's `dr32_engine.h`.

The canonical Schwung pattern (`dr32_engine_ops`):
```c
typedef struct {
    int         id;          // OMEGA_MODEL_FM2, ...
    const char *slug, *name, *prefix;
    int         nparams;
    const dr32_eparam *params;      // param table (min/max/def/unit/page)
    void *(*create)(int sample_rate);
    void  (*destroy)(void *e);
    void  (*set)(void *e, int idx, float display);
    void  (*note_on)(void *e, float vel01, float tune_st);
    void  (*choke)(void *e);
    int   (*render)(void *e, float *out, int n);  // returns 0 when silent
} omega_engine_ops;
```
- Each model is one translation unit exporting a `const omega_engine_ops` (e.g. `extern const omega_engine_ops omega_engine_fm2;`).
- A registry function `omega_engine_get(id)` returns the ops pointer; the module's `render_block` calls `ops->render(engine_instance, buf, n)`.
- **Key discoveries to copy:**
  - `render()` **returns 0 when the voice has been silent long enough** so the host skips it until the next `note_on` — a free CPU optimization that matters given the 10–15% budget.
  - **Engine IDs are persisted** (they appear in the state blob). **Append new engines; never renumber** — renumbering breaks saved presets.
  - Params are stored/edited in **display units**; each engine converts internally.
  - `create`/`destroy` run on the host thread (`set_param`); everything else is audio-thread-safe (no alloc, no I/O).

**Adaptation for Omega:** Unlike DR32 (one voice = one mono drum), Omega's engine renders the *kick body* only; the Groove rumble, ducking, DJ filter, and clipper are Omega's own shared post-stage (analogous to DR32's per-pad stage being outside the engine). So Omega's `render_block` = `active_ops->render(kick)` → circular-buffer rumble taps → duck → sum → DJ filter → soft clip. Pre-allocate all 10 engine instances at `create_instance` OR allocate lazily on the host thread when the model changes (DR32 does create/destroy on model switch). Given only one active model at a time, **create-on-switch (host thread) with a small pre-warm** is the memory-lean choice; pre-allocating all 10 is simpler but heavier. Recommend create-on-switch since model changes happen off the audio thread via `set_param`.

### 3. Sidechain ducking envelope in C — HIGH confidence (DSP standard)

**Finding: use a triggered envelope follower with instant attack, exponential release, expressed as a per-sample coefficient derived from ms.**

- **Trigger, not signal-following:** duck on the kick's `note_on` (or a detected kick transient), not on continuous level — this gives a clean, predictable pump. The context doc's `fabsf(kick)>0.1` threshold works but is fragile; prefer triggering off the same event that fires the kick voice.
- **Attack:** near-instant (1ms or faster) — "duck to zero within 1ms." At 44.1kHz, 1ms ≈ 44 samples. A single-sample snap to `1.0 − depth` is acceptable and cheapest.
- **Release:** smooth exponential recovery over **80–150ms** (must complete before the next 16th note; at 130 BPM a 16th ≈ 115ms). Compute the coefficient once per param change, not per sample:
  ```c
  // release_coef in [0,1); larger = slower recovery
  float release_coef = expf(-1.0f / (release_ms * 0.001f * sample_rate));
  // per sample, recovering toward 1.0:
  duck_env = 1.0f - (1.0f - duck_env) * release_coef;
  ```
  This replaces the context doc's fixed `*0.005f` magic number with a proper ms-parameterized coefficient (DUCK REL knob).
- **Work in samples internally, expose ms to the user.** Precompute coef when DUCK REL changes.
- **DUCK SMT (smooth):** a one-pole slew on the envelope itself (or an S-curve) to remove sub-bass clicks/pops from the abrupt attack — the context doc explicitly calls out low-frequency pops. Implement as a second one-pole smoothing the `duck_env` before it's applied.
- **DUCK BS (bass/high-pass threshold):** duck only content above a cutoff (protect the lowest sub) OR gate the ducking trigger by a high-passed kick — a high-pass on the detection path.
- Apply `duck_env` (0=fully ducked, 1=dry) as a multiplier on the rumble/external signal only, not the kick.

### 4. DJ filter design — HIGH confidence (VA/TPT standard)

**Finding: use a TPT (topology-preserving transform) state-variable filter, crossfading LP and HP outputs from a single bidirectional knob. Not a classic bilinear biquad.**

- The TPT/ZDF **state-variable filter** (Zavalishin, *The Art of VA Filter Design*) simultaneously produces LP, BP, and HP from one structure with a single set of state variables — ideal because you need both LP and HP from one knob, and it's cheap and stable up to Nyquist (no bilinear high-frequency cramping, no ladder non-linear solver).
- **Single-knob mapping (DJ FILT, 0..127 → −1..+1):**
  - center (neutral) = bypass/flat, output = input
  - CCW (0..center): sweep an LP cutoff from ~20kHz (open) down to ~30–100Hz. Crossfade dry→LP or just use LP output with cutoff rising to Nyquist at center.
  - CW (center..127): sweep an HP cutoff from ~20Hz up to ~5–10kHz.
- **Resonance (DJ RESO):** the SVF's damping/`k` parameter (`k = 2 − 2·reso`, `reso` in 0..1). Standard TPT SVF resonance control; safe, self-oscillates near max.
- **Why not biquad:** a bilinear biquad needs coefficient recomputation per cutoff change and warps badly near Nyquist; the TPT SVF updates a single `g = tan(π·fc/fs)` and gives all outputs. Consistent with PROJECT.md's "TPT filter over ZDF Moog ladder" decision.
- Coefficients: `g = tan(π·fc/fs)`, `k = 1/Q`. Recompute `g` only when cutoff moves (per block is fine).

### 5. Groove tap-delay tempo sync from `get_beat_position()` — HIGH confidence

**Finding: derive the 16th-note interval in samples from BPM (itself derived from `get_beat_position()` over time), and use integer tap positions with fractional interpolation only if BPM is non-integer-sample.**

- `get_beat_position()` returns a **24-PPQN-synced transport beat position** (a `double` in beats). It gives you *phase*, not directly BPM. Two ways to get the tap interval:
  1. **Preferred — track BPM from beat delta:** sample beat position each block, `bpm = (Δbeats / Δseconds) * 60`. Then `samples_per_16th = (60.0 / bpm) * sample_rate / 4.0`. Smooth BPM to avoid jitter.
  2. Or read host tempo if a host-tempo accessor exists (not shown in the v1 host API; the API only exposes `get_beat_position` and `get_clock_status`).
- The context doc's `sample_rate * 0.125f` hardcodes 120 BPM — **wrong for tempo sync.** Replace with the BPM-derived interval.
- **Integer vs fractional taps:** `samples_per_16th` is almost always fractional (e.g. at 128 BPM, 16th = 5170.3 samples). Options:
  - **Integer truncation** (`(int)`): simplest, taps drift by <1 sample — inaudible for a rumble ghost-kick. This is the pragmatic v1 choice. DR32/context code uses integer positions.
  - **Fractional read with linear interpolation:** read `buffer[floor]` and `buffer[floor+1]` and lerp. Only needed if precise phase-locking to the grid matters (it mostly doesn't for smeared sub-bass). Recommend integer for v1, note fractional as a polish item.
- **Phase alignment:** to lock taps to the actual 16th grid rather than "N samples behind write head," compute each tap's read offset from beat phase: `frac = beat_position * 4` (16ths), `tap_phase = frac - tapindex`. For v1's "ghost kick trailing the main kick" behavior, trailing-delay (read = write − k·interval) is the intended and simpler model — the taps are *echoes of the kick*, which the context doc confirms.
- Buffer: `MAX_DELAY_FRAMES = 88200` (2s) already covers 4 taps down to ~30 BPM.

### 6. Generative sequencer patterns (GEN / HPN model) — MEDIUM confidence

**Finding: hypnotic techno sequences come from constrained, slowly-mutating, scale-quantized pitch sequences over a Euclidean/steady 16th grid — determinism + subtle variation is what makes them "hypnotic."**

Standard building blocks (combine them):
- **Deterministic seed → PRNG (LCG or LFSR):** a seed value drives a small linear-congruential generator or 15-bit LFSR to produce a *repeatable* pseudo-random sequence. SEED knob = re-seed → new-but-repeatable pattern. LFSR is cheap and integer-only (NES-APU-style, well established for generative pitch/noise). This maps directly to PROJECT.md's `SEED` param.
- **Scale quantization:** map raw PRNG output into a musical scale (minor, phrygian, etc.) via a small pitch-class table. `SCALE` param selects scale or "free frequency" mode. Quantizing to a scale (and a narrow octave range) is what keeps generative sequences musical rather than random.
- **Sequence length (SEQ LEN):** loop the pattern over N steps (e.g. 8/16/32). Shorter loops = more hypnotic/repetitive; longer = more evolving.
- **Euclidean rhythm for step gating (DENSITY):** Bjorklund's algorithm evenly distributes K active hits across N steps — `DENSITY` maps to K. This gives organic, non-uniform-but-balanced rhythmic placement that's a genre staple. Precompute the boolean pattern (integer/bit array) when DENSITY or SEQ LEN changes, on the host thread.
- **What makes it "hypnotic" (from techno production wisdom):**
  - **Narrow pitch range** (mostly sub-bass, ±a few semitones) so it reads as a rolling bassline, not a melody.
  - **Steady 16th pulse** with subtle velocity variation, not rhythmic chaos.
  - **Slow evolution:** small mutation per loop (occasionally flip one step) rather than re-randomizing — a "mutation amount" is more hypnotic than full randomness.
  - Heavy **LPF (2/4-pole toggle)** and ducking so successive notes blur into a continuous rumble (PROJECT.md's `LPF FREQ`, `LPF POLE`).
- **Implementation:** all sequence generation happens on the host thread (`set_param` or on beat boundaries computed in `render_block` but only reading precomputed tables). Advance the step on 16th-note boundaries derived from beat phase. Store the generated pitch/velocity arrays pre-allocated.

### 7. What NOT to build in v1

Covered in the **Anti-Features** table above. The clearly hardware/CV-only items (not applicable to software): `TAPS CV` in, `TAPS OUT` out, and all four `TAPS OUT` envelope routing modes. The performance/complexity deferrals: Beat/Slip Roll, EXT voice hosting, MIDI melodic bass, Post-EQ 3-band, hardware snapshot morphing, Producer-mode randomization.

---

## Feature Dependencies

```
module.json + plugin_api_v2 skeleton  → everything (foundation)
engine-vtable dispatcher              → all 10 models, MODEL selector, root macro
one working kick engine (e.g. FM2)    → Groove rumble (needs a kick to smear)
Groove rumble (circular buffer+taps)  → sidechain ducking (ducks the rumble)
BPM from get_beat_position            → tempo-synced taps AND GEN step advance
kick engine + rumble + performer      → root performance macros (bidirectional)
host `state` blob persistence         → module-owned named presets (Layer 2)
TPT SVF                               → DJ filter AND rumble COLOR/GEN LPF (shared)
Euclidean + PRNG + scale quantizer    → GEN model only
```

---

## MVP Recommendation

Ship the *whole three-section system* but with a **reduced model count first**, because the single-module integration (kick→rumble→duck→filter→clip) plus root-macro bidirectional sync is where the risk lives — not in having 10 models.

Prioritize:
1. **Foundation + cross-host load** (module.json, plugin_api_v2, ui_hierarchy, host `state` blob persistence) — nothing works without this and the state cap must be handled early.
2. **Engine-vtable dispatcher + 2–3 models** (FM2, ANA/808-sub, TRS/909) — proves the dispatch pattern and covers the essential sonic poles.
3. **Groove 4-tap rumble with real BPM sync + COLOR filter** — the reason the product exists.
4. **Performer: ducking + DJ filter (TPT SVF) + soft clipper** — completes the signature chain.
5. **Root performance-macro page (bidirectional)** — the headline differentiator.

Then expand: remaining kick models (FM4, WTR, PHY, HRD, DIG, USR), and finally the **GEN model** (highest-complexity, most-novel — best as a focused later phase with its own research pass).

Defer (already scoped): EXT hosting, Beat/Slip Roll, MIDI melodic bass, Post-EQ, all CV features.

---

## Sources

- Schwung framework repo (dispatch, module.json, ui_hierarchy, plugin_api_v2): https://github.com/charlesvestal/schwung — HIGH
- Schwung MODULES.md (preset/state persistence, json_defaults, config.json file rules): https://raw.githubusercontent.com/charlesvestal/schwung/main/docs/MODULES.md — HIGH
- DR32 engine dispatcher vtable (`dr32_engine.h`), preset (`dr32_preset.h`), state blob + host size cap (`dr32_state.h`): https://github.com/legsmechanical/schwung-dr32 — HIGH (read directly via GitHub API)
- Provided context: `01_SCHWUNG_DEV_ARCHITECTURE.md`, `02_OHMFORCE_BOHM_SYSTEM_SPEC.md`, `03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md`, `04_BOHM_SCHWUNG_MODULE_DESIGN.md`, `05_SOURCE_INDEX_AND_REFERENCES.md` — HIGH (project-authoritative)
- The Art of VA Filter Design (Zavalishin) — TPT SVF, ZDF, resonance: referenced via https://www.kvraudio.com/forum/viewtopic.php?t=350246 — HIGH (established DSP standard)
- State variable filter / bilinear transform background: https://en.wikipedia.org/wiki/State_variable_filter , https://arxiv.org/pdf/2111.05592 — MEDIUM
- Euclidean rhythm (Bjorklund) for DENSITY: https://en.wikipedia.org/wiki/Euclidean_rhythm , https://cgm.cs.mcgill.ca/~godfried/publications/banff.pdf — HIGH
- LFSR/LCG generative pitch + Euclidean in practice: https://github.com/dingvald/ProceduralMusicGenerator , https://blog.landr.com/euclidean-rhythms/ — MEDIUM
- Bohm hardware manual (CV features, system settings that are anti-features): https://bohm-eurorack-manual.readthedocs.io/en/latest/ — MEDIUM (per context index)
