# Omega

## What This Is

Omega is a native C Schwung module for Ableton Move that brings the complete Ohm Force Bohm/Groove/Performer techno kick synthesis system to the device. It provides 10 multi-engine kick synthesis models (2-op FM, 4-op FM, physical modeling, wavetable, generative, and more), a 4-tap rumble generator, and a live performance mixer with sidechain ducking and DJ filter. Omega runs as a single module loadable in Schwung's instrument slots, DR32 pad slots, and Movy tracks.

## Core Value

A techno producer on Ableton Move should be able to dial in a full Bohm-style kick + rumble + performer system from one module, with immediate access to the most expressive performance controls on the root page.

## Current Milestone: v1.1 Refinement

**Goal:** Fix the on-device UX and audio defects found testing Phases B/C, and complete the reworked design, before building the Performer chain.

**Target features:**
- Param/UI infrastructure: per-key value readback, rich schema (enum/int/float, options, defaults, units, steps), per-model state memory, correct control types
- Kick voicing + page reorg: lower/deeper pitch+curve, merge redundant params, model-unique params on Page 1 / transients on Page 2, per-model voicing fixes
- Sample infrastructure: module sample folder + SD browse, string list-picker for sample selection
- Groove redesign: TAPS/GEN type selector decoupled from kick model, feedback rumble, per-type pages with reverb/drive/filter/LFO, GEN retrigger modes + seqlen 1–32 + tone shaping
- Performer chain (Phase D): duck → DJ filter → soft clip, on-device CPU measured

**Authoritative brief:** `.planning/REFINEMENT-FEEDBACK.md` (root-cause investigation + per-model/per-page detail from on-device testing).

## Requirements

### Validated

(None yet — ship to validate)

### Active

**Module Foundation**
- [ ] Implements Schwung `plugin_api_v2_t` C plugin interface — loadable in Schwung slots, DR32 pads, Movy tracks
- [ ] `module.json` manifest with `component_type: sound_generator`, `pad_layout: drums`, `api_version: 2`
- [ ] `ui_hierarchy` JSON schema served via `get_param` — drives Move's 8 encoder + OLED page system
- [ ] Zero dynamic allocation on audio thread — all buffers pre-allocated in `create_instance`
- [ ] Cross-compiled for Linux ARM64 (`aarch64`, glibc 2.35) via Docker Schwung builder image

**Kick Engine — 10 Synthesis Models**
- [ ] FM2 — 2-operator wavetable FM kick (accurate: carrier/modulator WT oscillators, FM index envelope)
- [ ] FM4 — 4-operator FM with selectable operator routing algorithms (OPL3-inspired)
- [ ] WTR — Wavetable + dedicated transient impulse synth (independent body + click control)
- [ ] PHY — Physical modeling kick: damped oscillator model for shell/head/beater (functional equivalent, not waveguide)
- [ ] HRD — Hard techno wavetable + sample layer + drive/bit-crush processing
- [ ] DIG — Digital wavetable + sample (chip/additive waveforms, bit-depth control)
- [ ] TRS — Advanced wavetable + transient synth (noise burst + click modeling, 909 character)
- [ ] ANA — Analog wavetable morph + sub-oscillator + sample layer (808 sub-boom character)
- [ ] USR — User wavetable + sample: load custom WAV/wavetable from device storage
- [ ] GEN — Generative rumble model: pitch/velocity sequence generator with key/scale or free-frequency mode

**Kick Engine — Universal Parameters (all models)**
- [ ] Kick Page 1: PITCH, LENGTH, SUSTAIN, CURVE (808↔909 pitch sweep), ATTACK, TRS DEC, TRS TNE, COLOR
- [ ] Kick Page 2: FX TYPE (Diode/Clip/SAT/Fold/Crush), FX AMT, + 6 model-specific parameter slots (context-sensitive, driven by active model)

**Groove Rumble Generator**
- [ ] 4-tap 16th-note multi-tap delay rumble engine — secondary ghost-kick voice derived from kick signal
- [ ] Groove Page 1: VOL, LENGTH, COLOR (timbre filter), TAP1, TAP2, TAP3, TAP4, MONO toggle
- [ ] Groove Page 2 (GEN model only): SEED (sequence mutation), SCALE (key/scale selector or free-freq), SEQ LEN, LPF FREQ, LPF POLE (2/4-pole toggle), DENSITY

**Performer Mixer & FX**
- [ ] Sidechain ducking engine: kick trigger ducks external groove signal automatically
- [ ] DJ filter: dual LP/HP with resonance, bidirectional sweep (LP↔neutral↔HP)
- [ ] End-of-chain soft clipper (+4.6dB headroom before hard clip)
- [ ] Performer Page 1: MSTR VOL, DUCK, DUCK REL, DUCK SMT, DUCK BS, DJ FILT, DJ RESO, CLIP

**Root Performance Page**
- [ ] Root page = 8 bidirectional performance macros (update bidirectionally with sub-pages)
- [ ] Proposed macro set: MODEL, MASTER VOL, PITCH, LENGTH, RUMBLE VOL, TAP1, DUCK, DJ FILT
- [ ] Navigation links to Kick / Groove / Performer section pages

**Preset System**
- [ ] Preset Load page (first page in hierarchy): lists saved presets, select to apply
- [ ] Preset Save page (last page): Save (overwrite) + Save As (new name)
- [ ] Preset saves full module state: active model, all kick/groove/performer params
- [ ] Preset I/O via filesystem (module_dir JSON files) — file access outside audio thread via init-time enumeration

### Out of Scope

- **EXT voice hosting (v2)** — Load an external Schwung kick module (e.g., 9W9) as Omega's inner engine. Requires `dlopen` + runtime module enumeration. Core value delivered without it; add post-v1.
- **Beat Roll / Slip Roll** — Live buffer-repeat performance effects from Bohm Performer. Requires beat-sync'd capture loop. Complex to implement well; defer to v2.
- **MIDI-pitched bass voice** — Omega responds to MIDI pitch notes for melodic bass hits. GEN model covers generative basslines; melodic MIDI bass is a different use case for v2.
- **CV-style modulation routing** — Hardware Bohm has `TAPS CV` and `TAPS OUT` CV jacks. Not applicable to Schwung software architecture.
- **Post-EQ 3-band shelf** — Bohm Performer system setting. Performer fits cleanly in 1 page without it; defer.

## Context

**Platform:** Ableton Move running Schwung Shadow UI framework. Linux ARM64, 44.1kHz, 128-frame blocks. Real-time audio on `SCHED_FIFO 70` core 3.

**CPU budget:** ~10–15% CPU for Omega. Move is expected to run Omega alongside 10+ Movy tracks, 16 DR32 pads, and polyphonic synths. Hybrid DSP fidelity strategy: accurate FM/wavetable/transient engines (cheap), simplified physical modeling (damped oscillators vs full waveguide), TPT filter topology instead of ZDF Moog ladder.

**Host compatibility:** Single `dsp.so` + `module.json` must work unmodified in 3 host contexts:
- **Schwung slots** — standard 4-track instrument slot
- **DR32** — loaded as a pad engine via `dr32_pad_t.engine_api`
- **Movy** — Elektron-style 16-track host with auto-detected parameter page UI

**Prior art / reference implementations:**
- Context docs: `01_SCHWUNG_DEV_ARCHITECTURE.md` through `05_SOURCE_INDEX_AND_REFERENCES.md`
- Reference DSP engines: 9W9 (TR-909), 8W8 (TR-808), Forge (FM+ZDF ladder), Sophie (4-op FM percussion), Maze (TZFM + wavefolder)
- Bohm hardware manual: https://bohm-eurorack-manual.readthedocs.io

**Naming rule:** No model or feature names that are identical to Bohm hardware names. All models use 2-3 char synthesis-method IDs (FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN).

## Constraints

- **Tech stack**: C (no C++, no STL) — Schwung plugin API v2 is C ABI
- **Audio thread**: Zero malloc/free/new/delete, zero file I/O, zero blocking, zero mutex on audio thread
- **Memory**: All voice buffers, delay lines, wavetables pre-allocated at `create_instance`. Groove delay buffer = 88200 frames × 2 channels × 4 bytes = ~700KB per instance
- **Target arch**: Linux ARM64 (aarch64), glibc 2.35, cross-compiled via `ghcr.io/charlesvestal/schwung-builder:latest`
- **UI**: 128×64 1-bit OLED, 8 rotary encoders, Move pads for navigation. `ui_hierarchy` JSON served from `get_param`
- **Sample rate**: Fixed 44.1kHz — no runtime negotiation
- **Compatibility**: Must load in Schwung slots, DR32 pad slots, and Movy tracks without modification

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Single module (not split kick + rumble) | Split kick/rumble across two pads requires inter-pad audio routing which may not be architecturally supported in DR32/Movy. Single module is certain to work. | — Pending |
| Hybrid DSP fidelity | CPU budget requires compromise. FM/wavetable/transient engines are cheap to implement accurately. PM-K1's physical model and the main resonant filter are where simplification saves meaningful CPU. | — Pending |
| Synthesis-method model IDs | IP avoidance (no Bohm name cloning) + user clarity about synthesis method. Bohm's abstract alphanumeric IDs provide no synthesis context. | — Pending |
| EXT voice hosting deferred to v2 | `dlopen` + module enumeration adds significant complexity. 10 internal synthesis models cover the sonic range. Value can ship without it. | — Pending |
| Root page = performance macros (bidirectional) | Move's 8 encoders are prime real estate. A dedicated nav page wastes them. Curated bidirectional macros give immediate hands-on control of the most expressive parameters across all three sections. | — Pending |
| TPT filter over ZDF Moog ladder | ZDF non-linear solver is ~2x the CPU cost of TPT state-variable filter. At bass frequencies, audible difference is minimal. Saves ~3-5% CPU. | — Pending |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd:transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd:complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-09-29 — milestone v1.1 Refinement started*
