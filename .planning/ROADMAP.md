# Roadmap: Omega

**Created:** 2026-09-28
**Granularity:** standard
**Core Value:** A techno producer on Ableton Move should be able to dial in a full Bohm-style kick + rumble + performer system from one module, with immediate access to the most expressive performance controls on the root page.

---

## Global Constraints (apply to every phase)

- **RT-safe audio thread**: zero malloc/free, zero file I/O, zero blocking, zero mutex in `render_block`; all buffers pre-allocated in `create_instance` (single `calloc` of `bohm_instance`).
- **`get_param` is on the audio/UI thread**: no logging, no allocation, no file I/O in `get_param`; `ui_hierarchy` and per-key value readback are served from pre-allocated scratch, locale-independent numeric formatting.
- **`ui_hierarchy` served from `get_param`**: pre-serialized static fragments + no-alloc splice into `ui_scratch`; Kick Page 2 assembled from the active model's `p2_slot_desc`.
- **Target arch**: Linux ARM64 (aarch64, glibc 2.35), cross-compiled via `ghcr.io/charlesvestal/schwung-builder:latest`; `objdump -T` gate enforces no GLIBC symbols newer than 2.35.
- **Off-audio-thread file access only**: sample/user-content enumeration and loading happen at `create_instance` (or a deferred low-priority writer), never in the render/`set_param`/`get_param` path.
- **CPU budget**: full chain must stay within ~10–15% on-device.
- **Host ABI is LOCKED**: `host_api_v1_t` / `plugin_api_v2_t` struct layouts and their `_Static_assert`s must not change; internal vtables may grow.

---

## Milestone: v1.0 (shipped — records preserved)

### Phases (v1.0)

- [ ] **Phase A: Foundation + FM2 Model** - Prove the entire pipeline end-to-end: plugin loads in all 3 hosts, FM2 kick sounds on-device, all RT-safety primitives and CI gates in place (code + CI complete; ON-DEVICE verification PENDING — SC1/SC5/D-15 await hardware, see docs/ON_DEVICE_VALIDATION.md)
- [x] **Phase B: Remaining 9 Kick Models** - All 10 models playable, each with correct context-sensitive Kick Page 2 params and FX chain (completed 2026-09-29)
- [x] **Phase C: Groove Rumble Engine** - Rumble audibly syncs to project tempo; GEN model hooks connected to Groove Page 2 (completed 2026-09-29)

> v1.0 Phases D–G are SUPERSEDED / RESCHEDULED. Phase D (Performer Chain) is carried into v1.1 as its final phase. Phases E (UI Hierarchy), F (Bidirectional Macros), G (Presets) are **FUTURE** — not in the v1.1 milestone (see "Future Phases" below).

### Phase Details (v1.0)

### Phase A: Foundation + FM2 Model
**Goal**: A developer can build `dsp.so` in the pinned Docker image, load Omega in Schwung slots / DR32 pads / Movy tracks, and hear an FM2 kick on-device — with every real-time-safety primitive and CI gate proven before any breadth work begins.
**Depends on**: Nothing (first phase)
**Requirements**: FNDTN-01, FNDTN-02, FNDTN-03, FNDTN-04, FNDTN-05, FNDTN-06, FNDTN-07, KICK-01, KICK-02, KICK-12, KICK-15
**Success Criteria** (what must be TRUE):
  1. The same unmodified `dsp.so` + `module.json` loads and produces audio in all three host contexts (Schwung slot, DR32 pad, Movy track)
  2. Triggering FM2 produces an audible kick with responsive Kick Page 1 params and FM2 Page 2 params (FM RATIO, FM INDEX, OP2 WAVE)
  3. The malloc-trap debug build runs a full render session without `abort()`, and the `objdump -T` CI gate passes with no GLIBC symbols newer than 2.35
  4. The offline WAV harness renders FM2 output to a file with no NaN/Inf and correct int16 clamping (verified via `clamp + isfinite + lrintf`)
  5. The actual `ui_hierarchy` `buf_len` passed by each host is measured and logged on-device (unblocks UI work)
**Plans**: 4 plans (A-01..A-04)
**UI hint**: yes

### Phase B: Remaining 9 Kick Models
**Goal**: A producer can select any of the 10 models via the MODEL parameter and hear a distinct, correctly-voiced kick, each exposing its own 6-slot context-sensitive Kick Page 2 and the shared post-kick FX chain.
**Depends on**: Phase A (vtable dispatch, shared DSP primitives, wavetable storage)
**Requirements**: KICK-03, KICK-04, KICK-05, KICK-06, KICK-07, KICK-08, KICK-09, KICK-10, KICK-11, KICK-13, KICK-14
**Success Criteria** (what must be TRUE):
  1. Selecting each of FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN produces an audibly distinct, correctly-voiced kick
  2. Switching the MODEL parameter re-initializes engine state cleanly with no tearing or stale-state artifacts
  3. Each model's Kick Page 2 shows the correct model-specific parameter slots assembled from its `p2_slot_desc`
  4. All five post-kick FX modes (Diode, Clip, SAT, Fold, Crush) are selectable and audibly alter the kick with no divergence or clipping artifacts
  5. USR loads a custom WAV/wavetable from `module_dir/user/` at `create_instance` with zero file I/O on the audio thread
**Plans**: 9 plans (B-01..B-09)
**UI hint**: yes

### Phase C: Groove Rumble Engine
**Goal**: A producer hears a ghost-kick rumble derived from the kick signal that stays locked to the project tempo across BPM changes, controllable from Groove Page 1, with the GEN model's generative controls on Groove Page 2.
**Depends on**: Phase A (delay buffer pre-allocation, DSP primitives), Phase B (GEN model)
**Requirements**: GRV-01, GRV-02, GRV-03, GRV-04, GRV-05
**Success Criteria** (what must be TRUE):
  1. The 4-tap rumble stays rhythmically locked to the project tempo, tracking BPM changes via `get_beat_position()` (never hardcoded 120)
  2. Groove Page 1 controls (VOL, LENGTH, COLOR, TAP1-4, MONO) audibly shape the rumble
  3. The MONO toggle force-sums L+R to mono for sub-bass routing
  4. With the GEN model active, Groove Page 2 generates repeatable scale-quantized sequences with Euclidean density gating; Page 2 is hidden for all other models
**Plans**: 3 plans (C-01..C-03)
**UI hint**: yes

---

## Milestone: v1.1 Refinement

**Goal:** Fix the on-device UX and audio defects found testing Phases B/C, complete the reworked kick/groove design, then build the Performer chain.
**Authoritative brief:** `.planning/REFINEMENT-FEEDBACK.md`
**Granularity:** standard (5 phases)

### Phases (v1.1)

- [ ] **Phase B1: Param/UI Infrastructure** - Per-key value readback + rich enum/int/float schema + per-model state memory + correct control types + drive auto-gain; foundational, unblocks every UI complaint
- [ ] **Phase B2: Kick Voicing & Page Reorg** - Lower/deeper pitch + stronger CURVE, merge redundant params, model-unique params on Page 1 / transients on Page 2, per-model voicing fixes
- [ ] **Phase B3: Sample Infrastructure** - Module samples folder + optional SD browse, enumerated off-thread at create_instance; string list-picker for sample selection
- [ ] **Phase C1: Groove Redesign** - TAPS/GEN type selector decoupled from kick model; feedback/resonant rumble (fixes "bit-crushed & quiet"); per-type pages with reverb/drive/filter/LFO; GEN retrigger modes + seqlen 1–32 + tone shaping
- [ ] **Phase D: Performer Chain** - Full kick→groove→duck→DJ filter→soft clip chain, CPU-measured on-device inside the 10–15% budget

### Phase Details (v1.1)

### Phase B1: Param/UI Infrastructure
**Goal**: Every parameter reports its real current value back to the host and declares rich, correct UI metadata — so knobs, the model box, and volume show their actual positions instead of 0, discrete params render as string selectors, and each model remembers its own settings.
**Depends on**: Phase B, Phase C (v1.0 kick + groove engines exist to expose)
**Requirements**: UIX-01, UIX-02, UIX-03, UIX-04, UIX-05, UIX-06
**Success Criteria** (what must be TRUE):
  1. After the host reads back a control, every knob, the model box, and the volume display show their real current values (not 0) — `get_param(key)` returns the current value string for every param key, and unknown keys still return -1
  2. Switching models restores that model's last-set knob positions — nothing visually or actually resets to 0 or 50% on a model switch
  3. Discrete named parameters (FX type, groove type, scale, retrigger mode, filter routing, reverb type, sample select) render as enum string selectors, the same control type as MODEL
  4. Controls declare appropriate elements and sweeps — bidirectional/centered where meaningful, discrete where meaningful, and real-world units where meaningful (e.g. PITCH in Hz) — via a schema carrying type/options/default/min/max/step/unit/short_name
  5. Raising drive/distortion amount changes character without simply making the signal louder (automatic output-gain compensation)
**Plans**: TBD
**UI hint**: yes

### Phase B2: Kick Voicing & Page Reorg
**Goal**: Every model sounds like a musical kick at its defaults, PITCH and CURVE do the expressive work in a lower/deeper range, redundant controls are merged, and the fun model-unique params sit on Page 1 with transients/FX/filter on Page 2.
**Depends on**: Phase B1 (enum selectors, units, filter-routing selector, per-model state memory)
**Requirements**: VOICE-01, VOICE-02, VOICE-03, VOICE-04, VOICE-05, VOICE-06
**Success Criteria** (what must be TRUE):
  1. A musical kick fundamental is reachable in the lower PITCH range (shown in Hz) without maxing CURVE, and CURVE now does more of the 808↔909 pitch-envelope work
  2. Redundant Page-1 controls are gone — TRS TNE and COLOR are one tone control, LENGTH and SUSTAIN are one length control
  3. Kick Page 1 leads with PITCH, LENGTH, CURVE plus 4–5 model-unique params; Kick Page 2 holds the 3 transient controls, FX selector + amount + tone, and filter + filter routing (SYN/TRANSIENT/BOTH)
  4. Each model is audibly kick-like at defaults with its voicing fixes applied (WTR body vs transient, FM2 not shrill, PHY reaches low + correct HEAD TENS direction, TRS blends distinct transient sources, ANA sub/sample audibly functional, USR smooth WMORP)
  5. Post-kick FX exposes a discrete type selector, an amount, and a third tone/mix control
**Plans**: TBD
**UI hint**: yes

### Phase B3: Sample Infrastructure
**Goal**: Sample content lives in a module folder (optionally browsable from the SD card), enumerated safely off the audio thread, and any sample-select control is a string list-picker showing sample names — never a knob.
**Depends on**: Phase B1 (enum string list-picker control type); relates to Phase B2 (USR/sample-select voicing)
**Requirements**: SMPL-01, SMPL-02, SMPL-03
**Success Criteria** (what must be TRUE):
  1. Sample content in the module's samples folder is discovered and available, enumerated off the audio thread at `create_instance` (no file I/O in render/`set_param`/`get_param`)
  2. USR SAMPLE SELECT and every sample-select control is a string list-picker showing sample names, never a knob
  3. Samples on the SD card can optionally be browsed (bounded, off the audio thread) and selected the same way
**Plans**: TBD
**UI hint**: yes

### Phase C1: Groove Redesign
**Goal**: The groove is a proper continuous resonant rumble (not gated bit-crushed echoes), with a Type selector that swaps between TAPS and a GEN groove decoupled from the kick model, each with its own musical control pages and sensible retrigger behavior.
**Depends on**: Phase B1 (Type/scale/retrigger/reverb-type selectors, value readback), Phase B3 (any groove sample content)
**Requirements**: GRVX-01, GRVX-02, GRVX-03, GRVX-04, GRVX-05
**Success Criteria** (what must be TRUE):
  1. The groove sounds like a continuous rumble at usable levels — resonant/feedback voice, not gated echoes and not bit-crushed/quiet
  2. A discrete Type selector swaps TAPS vs GEN control sets, and GEN groove pairs with any kick model (decoupled from the base model selection)
  3. TAPS type: Page 1 tap controls with an audibly effective COLOR filter; Page 2 FX (reverb with MIX/DECAY/TONE, drive, filter type, LFO speed, LFO amount)
  4. GEN type: Page 1 (SCALE enum incl. off/unquantized, SEED, SEQ LEN seeding a real 1–32 × 16th sequence, ROTATE bidirectional, SWING) and Page 2 (WAVE TYPE, WAVEFOLDER, FILTER with usable RES+ENV curve, LFO, MUTATE, DELAY, REVERB, DRIVE)
  5. GEN retrigger mode (on-note / 1 / 2 / 4 / 8 bars / NONE, default NONE) means the sequencer free-runs and obeys length instead of retriggering every kick hit, and the sequence stops when the transport stops
**Plans**: TBD
**UI hint**: yes

### Phase D: Performer Chain
**Goal**: A performer can play the complete `kick → groove → duck → dj_filter → soft_clip → int16_out` signal path with note-event-triggered ducking, a bidirectional DJ filter, and a bounded output clipper — with measured CPU headroom inside the 10–15% budget on-device.
**Depends on**: Phase C1 (full kick→groove chain feeding duck → DJ filter → soft clip)
**Requirements**: PERF-01, PERF-02, PERF-03, PERF-04, PERF-05
**Success Criteria** (what must be TRUE):
  1. Ducking triggers cleanly off the MIDI note-on event (not amplitude) with no chatter, using ms-derived attack/release coefficients
  2. DUCK SMT and DUCK BS eliminate low-frequency pops via one-pole slew and high-pass detection threshold
  3. The DJ filter sweeps bidirectionally LP↔neutral↔HP from a single knob with resonance, no coefficient blowup at extremes (cutoff clamped to [20Hz, 0.45×sr])
  4. The end-of-chain soft clipper (`y = x / (1 + |x|)`) bounds output with no positive-side divergence, toggleable on/off
  5. Measured on-device CPU for the full chain stays within the 10–15% budget
**Plans**: TBD

---

## Future Phases (not in v1.1)

These carry the v1.0 letter identifiers and remain FUTURE — scheduled after v1.1.

### Phase E: UI Hierarchy (FUTURE)
**Goal**: A user can navigate the full page hierarchy on Move's OLED and reach every parameter via the 8 encoders.
**Requirements**: UI-01, UI-06
**Status**: FUTURE (partially unblocked by B1's schema/readback work)

### Phase F: Bidirectional Macros (FUTURE)
**Goal**: The 8 curated root-page macros stay in sync with their sub-page parameters in both directions.
**Requirements**: UI-02, UI-03
**Status**: FUTURE

### Phase G: Presets (FUTURE)
**Goal**: Save and recall full module state via named preset files and the host state blob.
**Requirements**: UI-04, UI-05, PRST-01, PRST-02, PRST-03, PRST-04, PRST-05
**Status**: FUTURE

---

## Progress

### v1.0

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| A. Foundation + FM2 | 4/4 | Complete (on-device pending) | 2026-09-28 |
| B. 9 Kick Models | 9/9 | Complete | 2026-09-29 |
| C. Groove Rumble | 3/3 | Complete | 2026-09-29 |

### v1.1 Refinement

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| B1. Param/UI Infrastructure | 0/? | Not started | - |
| B2. Kick Voicing & Page Reorg | 0/? | Not started | - |
| B3. Sample Infrastructure | 0/? | Not started | - |
| C1. Groove Redesign | 0/? | Not started | - |
| D. Performer Chain | 0/? | Not started | - |

### Future (post-v1.1)

| Phase | Status |
|-------|--------|
| E. UI Hierarchy | Future |
| F. Bidirectional Macros | Future |
| G. Presets | Future |

---

## Coverage

**v1.1 milestone requirements (this milestone):**

| Requirement group | Phase | Count |
|-------------------|-------|-------|
| UIX-01..UIX-06 | B1 | 6 |
| VOICE-01..VOICE-06 | B2 | 6 |
| SMPL-01..SMPL-03 | B3 | 3 |
| GRVX-01..GRVX-05 | C1 | 5 |
| PERF-01..PERF-05 | D (carried from v1.0) | 5 |

- v1.1 refinement requirements (UIX/VOICE/SMPL/GRVX): 20 total → 20 mapped ✓
- PERF requirements carried into v1.1: 5 total → 5 mapped ✓
- v1.1 milestone coverage: 25/25 ✓ (no orphans, no duplicates)

**v1.0 requirements** remain mapped as originally: FNDTN/KICK/GRV → Phases A/B/C (complete); UI/PRST → Phases E/F/G (FUTURE).

---
*Roadmap created: 2026-09-28*
*v1.1 Refinement section added: 2026-09-30*
