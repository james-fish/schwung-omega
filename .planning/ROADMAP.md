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

- [x] **Phase B1: Param/UI Infrastructure** - Per-key value readback + rich enum/int/float schema + per-model state memory + correct control types + drive auto-gain; foundational, unblocks every UI complaint (completed 2026-09-30, native tests GREEN)
- [x] **Phase B2: Kick Voicing & Page Reorg** - Lower/deeper pitch + stronger CURVE, merge redundant params, model-unique params on Page 1 / transients on Page 2, per-model voicing fixes (completed 2026-09-30, native tests GREEN; on-device voicing audit + TRS transient-source redesign + FILTER ROUTE syn/trans split pending — docs/VOICING_AUDIT_v1_1.md)
- [x] **Phase B3: Sample Infrastructure** - Module samples folder + optional SD browse, enumerated off-thread at create_instance; string list-picker for sample selection (completed 2026-09-30, native tests GREEN; SD mount path is a best-guess to confirm on-device)
- [x] **Phase C1: Groove Redesign** - TAPS/GEN type selector decoupled from kick model; feedback/resonant rumble (fixes "bit-crushed & quiet"); per-type pages with reverb/drive/filter/LFO; GEN retrigger modes + seqlen 1–32 + tone shaping (completed 2026-09-30, native tests GREEN)
- [x] **Phase D: Performer Chain** - Full kick→groove→duck→DJ filter→soft clip chain (completed 2026-09-30, native tests GREEN; on-device CPU measurement pending hardware)

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

## Milestone: v1.2 On-Device Fixes

**Goal:** Fix every audio defect and UI bug found in the first full on-device test of v1.1. All broken FX modes work, ducking is audible, TAPS sounds musical, discrete controls show correct UI, GEN groove has a full scale/root/range set, and all voicing defaults land in a musical sweet spot.
**Authoritative brief:** `.planning/ONDEVICE_FEEDBACK_v1_1.md`
**Granularity:** standard (4 phases)

### Phases (v1.2)

- [x] **Phase E1: Critical Audio Fixes** - Fix everything that is broken or silent: FX chain (4 broken modes), ducking, TAPS bit-crush/gain, FM2 attack/transient, PHY curve inversion, DIG bit-depth range, soft clipper (completed 2026-09-30)
- [ ] **Phase E2: Discrete Controls & UI Cleanup** - Convert all disguised-continuous controls to proper list pickers/enum selectors: FM4 ALG, FM2 OP WAVE (+ draw waveform), HRD sample layer, DIG wave index; fix GEN wave audio (square/saw broken); convert TAPS+GEN filters to continuous LP sweep; remove GEN from kick model list; relabel opaque performer controls; rename USR blend
- [ ] **Phase E3: GEN Groove Enhancements** - Add 8+ more scales (Dorian, Phrygian, Mixolydian, Hirajoshi, Hungarian, Whole Tone, Blues, Diminished); add root note selector (scale mode C-3→C3) + unquantized mode with root pitch; add seq range control; rework GEN groove page 1 layout (SCALE/ROOT/RANGE/RETRIG replacing taps controls); rename+align gen tone page to match TAPS effects layout
- [ ] **Phase E4: Voicing Defaults & Curve Polish** - Default pitch 50 Hz all models, length 50%, FM4 op defaults ~0.1; fix CURVE to be more aggressive earlier; fix PHY HEAD TENS direction; fix TRS wavetable color second half; improve ANA wave morph audibility; add DUCK CURVE parameter

### Phase Details (v1.2)

### Phase E1: Critical Audio Fixes
**Goal:** Every audio control that was broken or silent in v1.1 on-device testing now works: all 5 FX modes are audibly distinct, ducking triggers and attenuates the signal, TAPS sounds like a musical rumble at usable levels, FM2 ATTACK/TRS TNE shape the sound, PHY CURVE works in the right direction, DIG BIT DEPTH crushes across the full range, soft clipper is retested and either fixed or disabled by default.
**Depends on:** Phase D (full chain exists)
**Bugs addressed:** #1 (FX chain), #2 (FM2 attack/trs), #3 (TAPS), #4 (duck), #5 (PHY curve), #6 (DIG bit depth), #7 (HRD drive/crush), #17 (soft clip), #33 (duck+clip)
**Success Criteria:**
  1. Clip, SAT, Fold, and Crush all sound audibly different from each other and from Diode — FX type selector routes to correct DSP for all 5 modes
  2. DUCK attenuates the groove signal on every kick MIDI note-on; DUCK REL and DUCK amount are audible
  3. TAPS sounds like a resonant multi-tap rumble at default VOL (no extra drive needed to level-match the kick)
  4. FM2 ATTACK shapes the attack transient; TRS TNE shapes transient tone — both audibly affect output
  5. PHY CURVE modulates pitch in a musical direction (not blowing up to high pitch at >25%); HEAD TENS adjusts tuning without screaming
  6. DIG BIT DEPTH crushes audibly across the 0–1 range (not just 0–0.1)
  7. Soft clipper tested: either fixed to sound good or its default is OFF
**Plans**: 3 plans (E1-01..E1-03)
Plans:
- [x] E1-01-PLAN.md — Fix FX type dispatch in all 9 model files (Bug #1)
- [x] E1-02-PLAN.md — Fix PHY CURVE, DIG BIT DEPTH direction, HRD DRIVE/CRUSH (Bugs #5, #6, #7)
- [x] E1-03-PLAN.md — Fix TAPS tap gain and soft clip + HRD CRUSH defaults (Bugs #3, #17)

### Phase E2: Discrete Controls & UI Cleanup
**Goal:** Every control that selects from a fixed set of options renders as a list picker or enum selector — never a bare continuous knob. All wave selectors draw the waveform. GEN wave audio matches the displayed waveform. TAPS and GEN use a single continuous LP filter knob. The kick model list no longer includes GEN.
**Depends on:** Phase B1 (enum/list-picker infrastructure), Phase E1 (audio working)
**Bugs addressed:** #8 (FM4 ALG), #9 (FM2 OP WAVE), #10 (HRD sample), #11 (DIG wave index), #12 (GEN wave audio), #13 (GEN in kick list), #14 (TAPS+GEN filters), #15 (USR blend), #16 (performer labels)
**Success Criteria:**
  1. FM4 ALGORITHM is a discrete selector with named options (ALG1–ALGn); FM2 OP WAVE is a discrete selector that draws the selected waveform on the OLED
  2. HRD sample layer and DIG wave index are list pickers matching the USR sample-select model
  3. GEN sequencer wave audio matches the displayed shape: square is square, saw is saw (not both triangle)
  4. TAPS and GEN each have a single continuous LP FILTER knob (log curve 30 Hz–20 kHz); filter type selector is gone
  5. GEN is absent from the kick model list; TAPS + GEN filter renaming + USR blend label + performer duck labels are all updated

### Phase E3: GEN Groove Enhancements
**Goal:** GEN groove has a rich, musical scale system with 12+ options including unquantized, a root note/pitch selector, and a range control. The GEN groove page 1 shows sequence controls (not stale TAPS controls). Both groove effect pages are consistently laid out and identically named.
**Depends on:** Phase C1 (GEN groove engine), Phase E2 (discrete selector infrastructure in place)
**Features addressed:** #18–24
**Success Criteria:**
  1. Scale list includes at minimum: Unquantized, Chromatic, Major, Minor, Pentatonic, Dorian, Phrygian, Mixolydian, Hirajoshi, Hungarian, Whole Tone, Blues, Diminished (13 options)
  2. When a scale is selected, ROOT NOTE selector covers C-3 to C3 (7 octaves × 12 = 84 choices or a continuous int control)
  3. When scale = Unquantized, the ROOT NOTE control becomes ROOT PITCH (Hz); RANGE control sets the semitone span of the generated sequence
  4. GEN groove page 1 shows: SCALE, ROOT (note or pitch), RANGE, RETRIG — TAPS controls (LENGTH/COLOR/TAP1–4) are hidden
  5. Both groove effect pages (TAPS and GEN) are named "Groove Effects" and share the same control layout (positions match)

### Phase E4: Voicing Defaults & Curve Polish
**Goal:** Every model loads into a musical starting point at 50 Hz / 50% length. CURVE does expressive work across a wider range. FM4 ops default to gentle values. PHY and TRS upper ranges are usable. ANA wave morph is audible. A DUCK CURVE parameter is added to the performer page.
**Depends on:** Phase E1 (audio working correctly before fine-tuning)
**Issues addressed:** #25–32
**Success Criteria:**
  1. Default PITCH = 50 Hz and default LENGTH = 50% for all models (or as close as the DSP range allows)
  2. FM4 OP RATIO, OP INDEX, OP AMP, FEEDBACK all default to ~0.1 normalized (7–8 o'clock on the dial)
  3. CURVE response is more aggressive: meaningful pitch sweep is audible across 0–100%, not just 75–100%
  4. PHY HEAD TENS direction is corrected; CURVE does not cause pitch explosion in the upper range
  5. TRS WAVETABLE COLOR upper 50% has a usable tone character (not just aggressive buzz); ANA WAVE MORPH is audible across a wider parameter range
  6. Performer page has a DUCK CURVE parameter that shapes the duck envelope (linear vs exponential)

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
| B1. Param/UI Infrastructure | 2/2 | Complete (UIX-01..06; native tests GREEN, on-device pending) | 2026-09-30 |
| B2. Kick Voicing & Page Reorg | 3/3 | Complete (VOICE-01..06; native GREEN, on-device audit pending) | 2026-09-30 |
| B3. Sample Infrastructure | 1/1 | Complete (SMPL-01..03; native GREEN, SD path on-device) | 2026-09-30 |
| C1. Groove Redesign | 3/3 | Complete (GRVX-01..05; native GREEN, on-device pending) | 2026-09-30 |
| D. Performer Chain | 1/1 | Complete (PERF-01..05; native GREEN, on-device CPU pending) | 2026-09-30 |

### v1.2 On-Device Fixes

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| E1. Critical Audio Fixes | 3/3 | Complete    | 2026-09-30 |
| E2. Discrete Controls & UI Cleanup | 0/? | Not started | — |
| E3. GEN Groove Enhancements | 0/? | Not started | — |
| E4. Voicing Defaults & Curve Polish | 0/? | Not started | — |

### Future (post-v1.2)

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
*v1.2 E1 plans added: 2026-09-30*
