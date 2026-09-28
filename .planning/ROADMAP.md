# Roadmap: Omega

**Created:** 2026-09-28
**Granularity:** standard (7 phases)
**Core Value:** A techno producer on Ableton Move should be able to dial in a full Bohm-style kick + rumble + performer system from one module, with immediate access to the most expressive performance controls on the root page.

---

## Phases

- [ ] **Phase A: Foundation + FM2 Model** - Prove the entire pipeline end-to-end: plugin loads in all 3 hosts, FM2 kick sounds on-device, all RT-safety primitives and CI gates in place
- [ ] **Phase B: Remaining 9 Kick Models** - All 10 models playable, each with correct context-sensitive Kick Page 2 params and FX chain
- [ ] **Phase C: Groove Rumble Engine** - Rumble audibly syncs to project tempo; GEN model hooks connected to Groove Page 2
- [ ] **Phase D: Performer Chain** - Full kick to rumble to duck to filter to clip chain complete and CPU-measured on-device
- [ ] **Phase E: UI Hierarchy** - Every parameter reachable from Move's 8 encoders via ui_hierarchy
- [ ] **Phase F: Bidirectional Macros** - Root page live-updates sub-page parameters bidirectionally
- [ ] **Phase G: Presets** - Full preset round-trip: named presets save/load; host state blob persists all params

---

## Phase Details

### Phase A: Foundation + FM2 Model
**Goal**: A developer can build `dsp.so` in the pinned Docker image, load Omega in Schwung slots / DR32 pads / Movy tracks, and hear an FM2 kick on-device — with every real-time-safety primitive and CI gate proven before any breadth work begins.
**Depends on**: Nothing (first phase)
**Requirements**: FNDTN-01, FNDTN-02, FNDTN-03, FNDTN-04, FNDTN-05, FNDTN-06, FNDTN-07, KICK-01, KICK-02, KICK-12, KICK-15
**Success Criteria** (what must be TRUE):
  1. The same unmodified `dsp.so` + `module.json` loads and produces audio in all three host contexts (Schwung slot, DR32 pad, Movy track)
  2. Triggering FM2 produces an audible kick with responsive Kick Page 1 params (PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR) and FM2 Page 2 params (FM RATIO, FM INDEX, OP2 WAVE)
  3. The malloc-trap debug build runs a full render session without `abort()`, and the `objdump -T` CI gate passes with no GLIBC symbols newer than 2.35
  4. The offline WAV harness renders FM2 output to a file with no NaN/Inf and correct int16 clamping (verified via `clamp + isfinite + lrintf`)
  5. The actual `ui_hierarchy` `buf_len` passed by each host is measured and logged on-device (unblocks Phase E)
**Plans**: 4 plans
Plans:
- [x] A-01-scaffolding-and-contracts-PLAN.md — build/test scaffolding, shared contracts (omega.h vtable + primitives), .rodata sine table, mock host, malloc trap, WAV writer, glibc gate, CI
- [x] A-02-fm2-engine-and-entry-points-PLAN.md — full FM2 engine, plugin entry points (dsp.c dispatch), model registry, module.json, real lifecycle harness
- [ ] A-03-ui-hierarchy-and-buflen-spike-PLAN.md — real minimal ui_hierarchy (Page 1 + FM2 Page 2), D-10 buf_len one-shot spike, CI cross-build flipped to blocking
- [ ] A-04-on-device-validation-PLAN.md — cross-build + deploy, 3-host load/audio verification (SC1), per-host buf_len capture (SC5), SoC identification (D-15)
**UI hint**: yes

### Phase B: Remaining 9 Kick Models
**Goal**: A producer can select any of the 10 models via the MODEL parameter and hear a distinct, correctly-voiced kick, each exposing its own 6-slot context-sensitive Kick Page 2 and the shared post-kick FX chain.
**Depends on**: Phase A (vtable dispatch, shared DSP primitives, wavetable storage)
**Requirements**: KICK-03, KICK-04, KICK-05, KICK-06, KICK-07, KICK-08, KICK-09, KICK-10, KICK-11, KICK-13, KICK-14
**Success Criteria** (what must be TRUE):
  1. Selecting each of FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN produces an audibly distinct, correctly-voiced kick
  2. Switching the MODEL parameter re-initializes engine state cleanly with no tearing or stale-state artifacts
  3. Each model's Kick Page 2 shows the correct 6 model-specific parameter slots assembled from its `p2_slot_desc`
  4. All five post-kick FX modes (Diode, Clip, SAT, Fold, Crush) are selectable and audibly alter the kick with no divergence or clipping artifacts
  5. USR loads a custom WAV/wavetable from `module_dir/user/` at `create_instance` with zero file I/O on the audio thread
**Plans**: TBD
**UI hint**: yes

### Phase C: Groove Rumble Engine
**Goal**: A producer hears a ghost-kick rumble derived from the kick signal that stays locked to the project tempo across BPM changes, controllable from Groove Page 1, with the GEN model's generative controls on Groove Page 2.
**Depends on**: Phase A (delay buffer pre-allocation, DSP primitives), Phase B (GEN model)
**Requirements**: GRV-01, GRV-02, GRV-03, GRV-04, GRV-05
**Success Criteria** (what must be TRUE):
  1. The 4-tap rumble stays rhythmically locked to the project tempo, tracking BPM changes via `get_beat_position()` (never hardcoded 120)
  2. Groove Page 1 controls (VOL, LENGTH, COLOR, TAP1-4, MONO) audibly shape the rumble
  3. The MONO toggle force-sums L+R to mono for sub-bass routing
  4. With the GEN model active, Groove Page 2 (SEED, SCALE, SEQ LEN, LPF FREQ, LPF POLE, DENSITY) generates repeatable scale-quantized sequences with Euclidean density gating; Page 2 is hidden for all other models
**Plans**: TBD
**UI hint**: yes

### Phase D: Performer Chain
**Goal**: A performer can play the complete `kick → groove → duck → dj_filter → soft_clip → int16_out` signal path with note-event-triggered ducking, a bidirectional DJ filter, and a bounded output clipper — with measured CPU headroom inside the 10-15% budget on-device.
**Depends on**: Phase C (full signal chain fed by kick + groove)
**Requirements**: PERF-01, PERF-02, PERF-03, PERF-04, PERF-05
**Success Criteria** (what must be TRUE):
  1. Ducking triggers cleanly off the MIDI note-on event (not amplitude) with no chatter, using ms-derived attack/release coefficients
  2. DUCK SMT and DUCK BS eliminate low-frequency pops via one-pole slew and high-pass detection threshold
  3. The DJ filter sweeps bidirectionally LP↔neutral↔HP from a single knob with resonance, no coefficient blowup at extremes (cutoff clamped to [20Hz, 0.45×sr])
  4. The end-of-chain soft clipper (`y = x / (1 + |x|)`) bounds output with no positive-side divergence, toggleable on/off
  5. Measured on-device CPU for the full chain stays within the 10-15% budget
**Plans**: TBD

### Phase E: UI Hierarchy
**Goal**: A user can navigate the full page hierarchy on Move's OLED and reach every parameter in REQUIREMENTS.md via the 8 encoders, with locale-independent numeric display and no allocation in `get_param`.
**Depends on**: Phase A (measured `buf_len`), Phases B/C/D (all params to expose)
**Requirements**: UI-01, UI-06
**Success Criteria** (what must be TRUE):
  1. Every parameter across Kick / Groove / Performer sections is reachable and editable from the 8 encoders on-device
  2. Kick Page 2 assembles dynamically from the active model's `p2_slot_desc` into `ui_scratch[8192]` with no allocation in `get_param`
  3. The full navigation hierarchy (Root → Kick 1 → Kick 2 → Groove 1 → Groove 2 [GEN only] → Performer 1 → Preset Load → Preset Save) works on-device
  4. Numeric values display and parse locale-independently (no `atof`/`strtod`/`snprintf` locale dependency)
**Plans**: TBD
**UI hint**: yes

### Phase F: Bidirectional Macros
**Goal**: A performer can drive the 8 curated root-page macros (MODEL, MASTER VOL, PITCH, LENGTH, RUMBLE VOL, TAP1, DUCK, DJ FILT) and see them stay in sync with their sub-page parameters in both directions — the headline performance differentiator.
**Depends on**: Phase E (complete set_param/get_param key dispatch)
**Requirements**: UI-02, UI-03
**Success Criteria** (what must be TRUE):
  1. Turning a root macro updates its corresponding sub-page parameter, and editing the sub-page parameter updates the root macro (bidirectional)
  2. Sync flows through a single `set_canonical()` write path with no recursive `set_param` loop
  3. Each of the 8 macros (MODEL, MASTER VOL, PITCH, LENGTH, RUMBLE VOL, TAP1, DUCK, DJ FILT) is live and controllable from the root page on-device
**Plans**: TBD
**UI hint**: yes

### Phase G: Presets
**Goal**: A producer can save and recall the full module state — active model plus every kick/groove/performer parameter — via named preset files and the host state blob, surviving reload and staying within the host buffer cap.
**Depends on**: Phase F (complete param set including macros to persist), Phase E (Preset Load/Save UI pages)
**Requirements**: UI-04, UI-05, PRST-01, PRST-02, PRST-03, PRST-04, PRST-05
**Success Criteria** (what must be TRUE):
  1. Saving then reloading (host state blob) restores active model and all parameters exactly, with the compact blob fitting within the measured host buffer cap
  2. Named presets in `module_dir/presets/*.json` are enumerated/parsed at `create_instance` and listed on the Preset Load page for selection
  3. Applying a preset performs an atomic block-boundary swap with model re-init and no mid-block tearing
  4. Saving a preset (Save / Save As) defers file writing to the low-priority writer pthread with zero file I/O on the audio thread
**Plans**: TBD
**UI hint**: yes

---

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| A. Foundation + FM2 | 2/4 | In Progress|  |
| B. 9 Kick Models | 0/? | Not started | - |
| C. Groove Rumble | 0/? | Not started | - |
| D. Performer Chain | 0/? | Not started | - |
| E. UI Hierarchy | 0/? | Not started | - |
| F. Bidirectional Macros | 0/? | Not started | - |
| G. Presets | 0/? | Not started | - |

---

## Coverage

- v1 requirements: 42 total
- Mapped to phases: 42
- Unmapped: 0

Every v1 requirement maps to exactly one phase. No orphans, no duplicates.

---
*Roadmap created: 2026-09-28*
