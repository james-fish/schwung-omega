# Requirements: Omega

**Defined:** 2026-09-28
**Core Value:** A techno producer on Ableton Move should be able to dial in a full Bohm-style kick + rumble + performer system from one module, with immediate access to the most expressive performance controls on the root page.

---

## v1 Requirements

### Foundation

- [x] **FNDTN-01**: Module implements `plugin_api_v2_t` C plugin interface — loads and runs unmodified in Schwung instrument slots, DR32 pad slots, and Movy tracks
- [x] **FNDTN-02**: `module.json` manifest present with `component_type: sound_generator`, `pad_layout: drums`, `api_version: 2`, id `omega`
- [x] **FNDTN-03**: Zero dynamic allocation on audio thread — all buffers (groove delay, wavetable scratch, model state) pre-allocated in `create_instance`; malloc-trap debug build `abort()`s on any audio-thread heap call
- [x] **FNDTN-04**: `dsp.so` cross-compiled for Linux ARM64 (aarch64, glibc 2.35) via Docker `schwung-builder:latest` image pinned by digest; CI `objdump -T` gate enforces no GLIBC symbols newer than 2.35
- [x] **FNDTN-05**: FPCR flush-to-zero bit set explicitly in `render_block` — prevents denormal stalls on ARM64 Cortex-A53 (FPCR is per-thread, not inherited from host)
- [x] **FNDTN-06**: Offline test harness: mock `host_api_v1_t` stub + render-to-WAV output for DSP verification and listening without Move hardware
- [x] **FNDTN-07**: Output stage uses `clamp + isfinite + lrintf` before int16 cast — no unclamped float-to-int16 conversions anywhere in the signal path

### Kick Engine

- [x] **KICK-01**: Model dispatcher vtable — 10+ models selectable via MODEL parameter; each model implements `trigger`, `render`, `set_p2`, and `p2_slot_desc` function pointers; model IDs are permanent (append-only, never renumber)
- [x] **KICK-02**: FM2 model — 2-operator wavetable FM kick; carrier and modulator are both wavetable oscillators; FM index has its own decay envelope; model-specific Kick Page 2: FM RATIO, FM INDEX, OP2 WAVE
- [ ] **KICK-03**: FM4 model — 4-operator FM with 4 selectable routing algorithms (OPL3-inspired); per-operator AM envelopes; model-specific Kick Page 2: ALGORITHM, OP RATIO, OP INDEX, OP AMP, FEEDBACK, ALGO
- [ ] **KICK-04**: WTR model — wavetable body oscillator + dedicated transient impulse synth; body and click are independently enveloped; model-specific Kick Page 2: WAVE SELECT, BODY PITCH, TRANS DECAY, TRANS COLOR
- [ ] **KICK-05**: PHY model — damped resonator physical model using 2–3 resonant modes (modal synthesis); represents shell/head/beater interaction without full waveguide; model-specific Kick Page 2: BEATER, SHELL SIZE, HEAD TENS, DAMPING
- [ ] **KICK-06**: HRD model — hard techno: wavetable body + sample layer + post-distortion; drive and bit-crush available; model-specific Kick Page 2: SAMPLE LAYER, MIX, DRIVE, CRUSH
- [ ] **KICK-07**: DIG model — digital wavetable (chip/additive/bit-reduced waveforms) + sample playback; bit-depth control for retro character; model-specific Kick Page 2: WAVE IDX, SAMPLE LAYER, BIT DEPTH, PITCH ENV
- [ ] **KICK-08**: TRS model — advanced wavetable + transient synthesizer modeling stick/beater clicks and noise bursts; 909-style attack clarity; model-specific Kick Page 2: TRANS TONE, TRANS DECAY, WT COLOR, CURVE
- [ ] **KICK-09**: ANA model — analog wavetable morphing (sampled vintage waveforms) + sub-oscillator + sample layer; 808 sub-boom character; model-specific Kick Page 2: WAVE MORPH, SUB LEVEL, SUB DECAY, SAMPLE
- [ ] **KICK-10**: USR model — user-defined: load a custom WAV sample and/or 2048-sample wavetable from device storage (`module_dir/user/`); file I/O done off audio thread at `create_instance`; model-specific Kick Page 2: SAMPLE SELECT, WT MORPH, LAYER VOL, PITCH ENV
- [ ] **KICK-11**: GEN model — generative rumble: PRNG pitch/velocity sequence generator; seed for repeatable sequences; scale-quantized or free-frequency mode; Euclidean density gating for rhythmic feel; model-specific Kick Page 2 parameters (see GRV-04)
- [x] **KICK-12**: Universal Kick Page 1 present for all models (8 encoders): PITCH, LENGTH, SUSTAIN, CURVE (808↔909 pitch sweep), ATTACK, TRS DEC, TRS TNE, COLOR
- [x] **KICK-13**: Context-sensitive Kick Page 2: FX TYPE (Diode/Clip/SAT/Fold/Crush), FX AMT, + 6 model-specific parameter slots assembled dynamically from active model's `p2_slot_desc`
- [x] **KICK-14**: Post-kick FX modes: Diode (back-to-back diode rounding), Clip (asymmetric soft clip), SAT (warm parallel saturation), Fold (wavefolder), Crush (bit-depth/sample-rate reduction)
- [x] **KICK-15**: Wavetables stored as `static const float` arrays in `.rodata` (shared across all instances); linear interpolation with 2048+1 guard sample; pre-band-limited source tables

### Groove Rumble

- [ ] **GRV-01**: 4-tap 16th-note multi-tap delay engine fed from kick signal; circular delay buffer pre-allocated at `create_instance` (88,200 frame max, 2s at 44.1kHz)
- [ ] **GRV-02**: BPM derived from `get_beat_position()` beat-delta across blocks — NOT hardcoded 120 BPM; `samples_per_16th = (60/bpm) × sr / 4`; integer tap positions for v1
- [ ] **GRV-03**: Groove Page 1 (8 encoders): VOL, LENGTH (tap decay), COLOR (TPT SVF low-pass timbre), TAP1, TAP2, TAP3, TAP4, MONO toggle
- [ ] **GRV-04**: Groove Page 2 (GEN model only, hidden for all other models): SEED, SCALE (key/scale selector or free-freq mode), SEQ LEN, LPF FREQ, LPF POLE (2-pole / 4-pole toggle), DENSITY
- [ ] **GRV-05**: MONO toggle force-sums L+R channels to mono for sub-bass club routing (mono sum below 150 Hz is standard techno production practice)

### Performer

- [ ] **PERF-01**: Sidechain ducking triggered from MIDI note-on event (not amplitude threshold — amplitude trigger chatters on the kick waveform); parameterized attack (~1ms) and release (10–500ms) using per-sample coefficient derived from ms value
- [ ] **PERF-02**: Duck smoothing (DUCK SMT): one-pole slew on ducking envelope to eliminate low-frequency pops; high-pass detection threshold (DUCK BS) to set the frequency below which ducking activates
- [ ] **PERF-03**: DJ filter: TPT state-variable filter with bidirectional LP↔HP sweep; single knob crossfades LP/HP outputs; resonance via SVF damping term; cutoff clamped to `[20Hz, 0.45×sr]` to prevent coefficient blowup
- [ ] **PERF-04**: End-of-chain soft clipper: `y = x / (1 + |x|)` (symmetric, bounded, no positive-side divergence); on/off toggle; applied as final stage before int16 conversion
- [ ] **PERF-05**: Performer Page 1 (8 encoders): MSTR VOL, DUCK, DUCK REL, DUCK SMT, DUCK BS, DJ FILT, DJ RESO, CLIP

### UI & Navigation

- [ ] **UI-01**: `ui_hierarchy` JSON served from `get_param("ui_hierarchy", buf, buf_len)` — pre-serialized static string fragments; Kick Page 2 dynamically assembled from active model's `p2_slot_desc` into pre-allocated `ui_scratch[8192]`; no allocation in `get_param`; locale-independent numeric formatting throughout
- [ ] **UI-02**: Root performance page with 8 bidirectional macros: MODEL, MASTER VOL, PITCH, LENGTH, RUMBLE VOL, TAP1, DUCK, DJ FILT — these update bidirectionally with their corresponding sub-page parameters
- [ ] **UI-03**: Bidirectional macro sync implemented via shared `set_canonical(inst, param_id, v, updating_macro)` — single write path that updates both canonical param and macro mirror; no recursive `set_param` loop
- [ ] **UI-04**: Preset Load page (first page in hierarchy): lists all saved preset names; select to apply full module state
- [ ] **UI-05**: Preset Save page (last page in hierarchy): Save (overwrite current preset) + Save As (new preset name from encoder input)
- [ ] **UI-06**: Page navigation hierarchy: Root → Kick Page 1 → Kick Page 2 → Groove Page 1 → Groove Page 2 (GEN only) → Performer Page 1 → Preset Load → Preset Save

### Presets & State

- [ ] **PRST-01**: Host `state` blob served via `get_param("state")` — compact JSON, only non-default params, no whitespace/pretty-print; fits within host buffer cap (cap measured in Phase A spike)
- [ ] **PRST-02**: Named preset files in `module_dir/presets/*.json` — enumerated and parsed at `create_instance` (off audio thread), stored in preset bank array in instance struct
- [ ] **PRST-03**: Preset apply = staged double-buffer swap at block boundary — full state `memcpy` plus model engine re-init; no mid-block partial application
- [ ] **PRST-04**: Preset save = deferred via `volatile save_request` flag; low-priority writer pthread (created at init) drains flag and writes JSON to `module_dir/presets/`
- [ ] **PRST-05**: Preset saves full module state: active model, all Kick/Groove/Performer parameters

---

## v2 Requirements

### EXT Voice Hosting

- **EXT-01**: EXT model: enumerate installed Schwung `sound_generator` modules from device filesystem at init
- **EXT-02**: Load selected external module's `dsp.so` via `dlopen` as inner kick engine; call its `move_plugin_init_v2` and `create_instance`
- **EXT-03**: EXT Kick pages: read external engine's `ui_hierarchy` and display those pages instead of Omega's Kick pages
- **EXT-04**: Forward Kick page parameter changes to external engine via its `set_param`/`get_param`

### Performance FX

- **FX-01**: Beat Roll: subdivided loop-repeat effect; captures 1/2/1/4 beat and loops until release
- **FX-02**: Slip Roll: freeze+slip effect; freezes buffer and drifts playback position; beat-sync'd
- **FX-03**: Post-EQ 3-band shelf: low shelf, peak mid, high shelf on Performer output

### Bass Voice

- **BASS-01**: MIDI-pitched bass voice: Omega responds to MIDI pitch notes to generate melodic bass hits (distinct from generative GEN rumble)

---

## Out of Scope

| Feature | Reason |
|---------|--------|
| CV jack routing (TAPS CV, TAPS OUT) | Hardware-only Eurorack feature; not applicable to Schwung software architecture |
| Beat Roll / Slip Roll (v1) | Requires beat-sync'd buffer capture loop; deferred to v2 |
| EXT voice hosting (v1) | Requires `dlopen` + module enumeration; deferred to v2 |
| MIDI-pitched bass voice (v1) | GEN model covers generative basslines; MIDI melodic bass is a distinct use case; deferred to v2 |
| Post-EQ 3-band shelf (v1) | Performer fits in 1 page without it; deferred to v2 |
| Full ZDF Moog 4-pole ladder filter | TPT SVF chosen instead (~3-5% CPU saving, near-identical at bass frequencies) |
| Full waveguide physical modeling | Modal damped resonator is the functional equivalent chosen; full waveguide is over CPU budget |
| Per-model wavetable user uploads beyond USR | USR model handles user content; other models use static factory wavetables |
| Snapshot pot-position recall (Bohm hardware feature) | Hardware-specific recall mechanism for physical encoders; not applicable |

---

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| FNDTN-01 | Phase A | Complete |
| FNDTN-02 | Phase A | Complete |
| FNDTN-03 | Phase A | Complete |
| FNDTN-04 | Phase A | Complete |
| FNDTN-05 | Phase A | Complete |
| FNDTN-06 | Phase A | Complete |
| FNDTN-07 | Phase A | Complete |
| KICK-01 | Phase A | Complete |
| KICK-02 | Phase A | Complete |
| KICK-03 | Phase B | Pending |
| KICK-04 | Phase B | Pending |
| KICK-05 | Phase B | Pending |
| KICK-06 | Phase B | Pending |
| KICK-07 | Phase B | Pending |
| KICK-08 | Phase B | Pending |
| KICK-09 | Phase B | Pending |
| KICK-10 | Phase B | Pending |
| KICK-11 | Phase B | Pending |
| KICK-12 | Phase A | Complete |
| KICK-13 | Phase B | Complete |
| KICK-14 | Phase B | Complete |
| KICK-15 | Phase A | Complete |
| GRV-01 | Phase C | Pending |
| GRV-02 | Phase C | Pending |
| GRV-03 | Phase C | Pending |
| GRV-04 | Phase C | Pending |
| GRV-05 | Phase C | Pending |
| PERF-01 | Phase D | Pending |
| PERF-02 | Phase D | Pending |
| PERF-03 | Phase D | Pending |
| PERF-04 | Phase D | Pending |
| PERF-05 | Phase D | Pending |
| UI-01 | Phase E | Pending |
| UI-02 | Phase F | Pending |
| UI-03 | Phase F | Pending |
| UI-04 | Phase G | Pending |
| UI-05 | Phase G | Pending |
| UI-06 | Phase E | Pending |
| PRST-01 | Phase G | Pending |
| PRST-02 | Phase G | Pending |
| PRST-03 | Phase G | Pending |
| PRST-04 | Phase G | Pending |
| PRST-05 | Phase G | Pending |

**Coverage:**
- v1 requirements: 42 total
- Mapped to phases: 42 ✓
- Unmapped: 0

---
*Requirements defined: 2026-09-28*
*Last updated: 2026-09-28 after roadmap creation*
