# Master Source Index & References for Claude Code

> **Notebook:** Ohm Force Bohm: Stereo Eurorack Kick Synthesis System  
> **Total Indexed Sources:** 38 Sources  
> **Categories:** Schwung Framework Architecture | Drum Racks & Engines | Ohm Force Bohm System & Techno Rumble  

---

## Category 1: Schwung Framework & Host Architecture

### 1. Schwung Main Framework Repository
- **Title:** GitHub - charlesvestal/schwung: An open framework for additional synths, FX and tools for the Ableton Move
- **Type:** Source Code & Architecture
- **URL:** [https://github.com/charlesvestal/schwung](https://github.com/charlesvestal/schwung)
- **Summary:** The core C/C++ framework enabling custom instruments, effects, and overtake tools to run on Ableton Move alongside stock firmware. Contains shadow UI bindings, SPI audio callback code, and plugin host implementations.

### 2. Schwung Module Development Guide
- **Title:** schwung/docs/MODULES.md at main · charlesvestal/schwung · GitHub
- **Type:** Technical Specification
- **URL:** [https://github.com/charlesvestal/schwung/blob/main/docs/MODULES.md](https://github.com/charlesvestal/schwung/blob/main/docs/MODULES.md)
- **Summary:** Official developer documentation covering `module.json` manifest specs, C `plugin_api_v2_t` plugin initialization, dynamic `ui_hierarchy` schemas, parameter types, `pad_layout: "drums"`, `child_index_param`, and real-time audio thread constraints.

### 3. Schwung JavaScript & C API Reference
- **Title:** schwung/docs/API.md at main · charlesvestal/schwung · GitHub
- **Type:** Technical Specification
- **URL:** [https://github.com/charlesvestal/schwung/blob/main/docs/API.md](https://github.com/charlesvestal/schwung/blob/main/docs/API.md)
- **Summary:** API bindings for QuickJS host script execution, display drawing primitives (`draw_arc`, `draw_circle`), MIDI injection via cable-0/cable-2, parameter get/set hooks, and runtime modulation callbacks.

### 4. Schwung User & Reference Manual
- **Title:** Manual · Schwung
- **Type:** Documentation
- **URL:** [https://schwung.dev/manual.html](https://schwung.dev/manual.html)
- **Summary:** User guide describing Shadow UI navigation, four instrument slots + Master FX slot, module bypass logic, drum rack pad-following UI rules, and Schwung Manager web interface.

---

## Category 2: Multi-Voice Drum Racks, Sequencers & Engine Reference Implementations

### 5. DR32 (Drum Rack 32)
- **Title:** GitHub - legsmechanical/schwung-dr32: DR32 — a clone of Move's native Drum Rack extended to 32 pads, for Schwung
- **Type:** Host Module Source
- **URL:** [https://github.com/legsmechanical/schwung-dr32](https://github.com/legsmechanical/schwung-dr32)
- **Summary:** 32-pad drum rack host for Schwung that instantiates sub-engines (`9W9`, `6W6`, `8W8`, `CW-78`, `Simian`, `Urchin`, `ChowKick`, `FM`, and `Sample`) per pad slot. Features per-pad buses, sends, and `.ablpreset` kit loading.

### 6. Movy Sequencer & Host
- **Title:** GitHub - DimaDake/schwung-movy: Elektron-style knob UI + 16-track sequencer
- **Type:** Host Module Source
- **URL:** [https://github.com/DimaDake/schwung-movy](https://github.com/DimaDake/schwung-movy)
- **Summary:** Elektron-style 16-track step sequencer and parameter UI host for Schwung. Features per-track chain navigation, auto-detected ADSR/filter graphics, send FX buses, and multi-track summing.

### 7. Forge Drum Synthesizer
- **Title:** GitHub - filliformes/forge-move: 8-voice FM/subtractive hybrid drum synth for Ableton Move
- **Type:** C DSP Engine
- **URL:** [https://github.com/filliformes/forge-move](https://github.com/filliformes/forge-move)
- **Summary:** 8-voice FM/subtractive hybrid drum synth with Kit A<->B morphing across 16 pads. Features ZDF Moog 4-pole transistor ladder filters, enveloped FM indices, dedicated noise layer, and per-voice resonators.

### 8. 9W9 (TR-909 Engine)
- **Title:** GitHub - athousanddetails/schwung-9W9: Move Schwung's 909 with a twist
- **Type:** C DSP Engine
- **URL:** [https://github.com/athousanddetails/schwung-9W9](https://github.com/athousanddetails/schwung-9W9)
- **Summary:** Circuit-modeled TR-909 kick, snare, toms, rim, clap, and sampled hats/ride/crash. Serves as a primary reference for 909 pitch sweeps and transient shaping in C.

### 9. 8W8 (TR-808 Engine)
- **Title:** GitHub - athousanddetails/schwung-8W8
- **Type:** C DSP Engine
- **URL:** [https://github.com/athousanddetails/schwung-8W8](https://github.com/athousanddetails/schwung-8W8)
- **Summary:** Circuit-modeled TR-808 drum machine. Bridged-T op-amp feedback network for bass drum where decay is loop gain rather than a simple envelope. Includes master saturation and diode clipping.

### 10. 6W6 (TR-606 Engine)
- **Title:** GitHub - athousanddetails/schwung-6W6
- **Type:** C DSP Engine
- **URL:** [https://github.com/athousanddetails/schwung-6W6](https://github.com/athousanddetails/schwung-6W6)
- **Summary:** Circuit-modeled 606 drum synth based on AudioKit Pro's 606 DSP. Provides 8 analog drum voices and master bus compressor.

### 11. CW-78 (CR-78 Engine)
- **Title:** GitHub - athousanddetails/schwung-cw-78
- **Type:** C DSP Engine
- **URL:** [https://github.com/athousanddetails/schwung-cw-78](https://github.com/athousanddetails/schwung-cw-78)
- **Summary:** Roland CR-78 circuit models. Features 3-section RC phase-shift ladder bass drum at 62.5 Hz where strike click leaks through for small-speaker punch.

### 12. Weird Dreams Drum Machine
- **Title:** GitHub - filliformes/weird-dreams-move: 8-voice analog drum machine
- **Type:** C DSP Engine
- **URL:** [https://github.com/filliformes/weird-dreams-move](https://github.com/filliformes/weird-dreams-move)
- **Summary:** 8-voice drum machine port with continuous oscillator morphing (sine/tri/saw/square), SVF filtered white noise, and dynamic pad-selected voice editing.

### 13. OneTrick SIMIAN 2 Port
- **Title:** GitHub - legsmechanical/schwung-simian: Ten-voice drum synthesiser for Schwung
- **Type:** C DSP Engine
- **URL:** [https://github.com/legsmechanical/schwung-simian](https://github.com/legsmechanical/schwung-simian)
- **Summary:** 10-voice 80s Simmons-style drum synth. Tuned oscillator yanked downward by an exponential pitch envelope mixed against resonant low-pass filtered noise.

### 14. Sophie FM Percussion
- **Title:** GitHub - mestela/schwung-sophie: Sophie metallic FM percussion synthesizer
- **Type:** C DSP Engine
- **URL:** [https://github.com/mestela/schwung-sophie](https://github.com/mestela/schwung-sophie)
- **Summary:** 16-pad metallic FM percussion synth featuring 4 operator algorithms (Fuse, Stack, Split, Shard), inharmonic frequency ratios, per-pad comb/ring delay, and bit/rate crushing.

### 15. Maze Voice
- **Title:** GitHub - sd88me/schwung-maze: Moog-Labyrinth-inspired mono synth voice and generative MIDI sequencer
- **Type:** C DSP Engine
- **URL:** [https://github.com/sd88me/schwung-maze](https://github.com/sd88me/schwung-maze)
- **Summary:** Moog Labyrinth-inspired thru-zero FM (TZFM) voice with wavefolder, SVF filter, warm per-channel overdrive, and ADAA anti-aliased saturation.

---

## Category 3: Ohm Force Bohm Hardware System & Techno Rumble Synthesis

### 16. Bohm Eurorack Manual
- **Title:** Bohm Eurorack Manual - Read the Docs
- **Type:** Documentation
- **URL:** [https://bohm-eurorack-manual.readthedocs.io/en/latest/](https://bohm-eurorack-manual.readthedocs.io/en/latest/)
- **Summary:** Complete hardware documentation for Ohm Force Bohm, Groove, and Performer expanders. Covers core models, calibration, firmware updates, and system menu options.

### 17. Bohm System Settings Reference
- **Title:** System Settings — Bohm documentation - Read the Docs
- **Type:** Technical Specification
- **URL:** [https://bohm-eurorack-manual.readthedocs.io/en/latest/system/index.html](https://bohm-eurorack-manual.readthedocs.io/en/latest/system/index.html)
- **Summary:** Deep technical reference for Bohm firmware settings: `Post EQ` (3-band shelf/peak filter), `Perf Vol` (B+G vs Bohm only volume routing), `Taps Out` envelope selection (`GROOVE`, `I BOHM`, `PERF`, `BOHM`), `Grv Env` sustain/fall behavior, and panning.

### 18. Bohm Firmware Changelog
- **Title:** Changelog — Bohm documentation - Read the Docs
- **Type:** Technical Release Notes
- **URL:** [https://bohm-eurorack-manual.readthedocs.io/en/latest/changelog/index.html](https://bohm-eurorack-manual.readthedocs.io/en/latest/changelog/index.html)
- **Summary:** Release notes documenting the addition of end-of-chain soft-clipper (+4.6dB headroom), DJ filter resonance control (`DJ RESO`), `DUCK TIME`, `DUCK SMTH`, `DUCK BS`, and `TAPS OUT` system options.

### 19. Gearnews — HPN Model & Bohm System
- **Title:** Ohmforce Bohm: New HPN Model - Let's Get Ready to Rumble - Gearnews.com
- **Type:** Industry Article
- **URL:** [https://www.gearnews.com/ohmforce-bohm-synth/](https://www.gearnews.com/ohmforce-bohm-synth/)
- **Summary:** Overview of the Bohm, Groove, and Performer modules, plus the new HPN model co-designed with Marc Faenger for hypnotic generative rumble sequences.

### 20. Synth Anatomy — Bohm & HPN Model Release
- **Title:** Ohm Force Bohm: new HPN Model brings deep, hypnotic rumble in the kick synth module
- **Type:** Industry Article
- **URL:** [https://synthanatomy.com/2026/05/ohm-force-bohm-a-new-stereo-dual-voice-multi-engine-kick-synth-voice-module.html](https://synthanatomy.com/2026/05/ohm-force-bohm-a-new-stereo-dual-voice-multi-engine-kick-synth-voice-module.html)
- **Summary:** Detailed breakdown of all 10 Bohm kick models (`FM-2X`, `HZ-1`, `OLP-4`, `PM-K1`, `PX-3`, `SP-6`, `VX-T`, `WT-4`, `XT-88`, `HPN`), microSD card `.OIFF` architecture, and hardware specs.

### 21. Sound On Sound Review
- **Title:** Ohmforce Bohm System - Sound On Sound
- **Type:** In-Depth Review
- **URL:** [https://www.soundonsound.com/reviews/ohmforce-bohm-system](https://www.soundonsound.com/reviews/ohmforce-bohm-system)
- **Summary:** Critical evaluation of the 3-module Bohm ecosystem, live performance snapshot workflow, Groove ghost-kick rumble generator, and Performer sidechain ducking / beat slip rolls.

### 22. SchneidersLaden Specs — Bohm Core Module
- **Title:** Ohm Force - Bohm - SchneidersLaden Berlin
- **Type:** Retail Hardware Specification
- **URL:** [https://schneidersladen.de/en/ohm-force-bohm](https://schneidersladen.de/en/ohm-force-bohm)
- **Summary:** 18HP module specs, power draw (+12V 130mA, -12V 10mA), Producer mode parameter randomization, and Performer mode preset sequencing.

### 23. SchneidersLaden Specs — Groove Expander
- **Title:** Ohm Force - Bohm Groove (Expander for Bohm) - SchneidersLaden Berlin
- **Type:** Retail Hardware Specification
- **URL:** [https://schneidersladen.de/en/ohm-force-bohm-groove-expander-for-bohm](https://schneidersladen.de/en/ohm-force-bohm-groove-expander-for-bohm)
- **Summary:** 10HP expander specs, 4-tap 16th-note subdivision controls, `LENGTH` and `COLOR` parameters, and `TAPS CV` modulation input/output system.

### 24. YouTube — How To Make Techno Rumble Kicks (2025)
- **Title:** How To Make Techno Rumble Kicks (2025)
- **Type:** Video Tutorial Transcript
- **Summary:** Production breakdown of techno rumble creation: parallel kick track processing, warehouse reverb smearing, decapitator-style saturation, low-pass filtering at 150Hz, sidechain ducking, and 16th-note delay percussive mid-layers.

### 25. YouTube — Techno Rumble Mastery
- **Title:** Techno Rumble Mastery (Underdog Electronic Music School)
- **Type:** Video Tutorial Transcript
- **Summary:** Systematic 3-tier framework for techno rumble synthesis (Basic, Intermediate, Master). Details parallel dry/wet split, mono summing below 200Hz, low-mid EQ cleanup (200Hz dip), and parallel saturation racks.

### 26. YouTube — The Techno Rumble Formula That Actually Works
- **Title:** The Techno Rumble Formula That Actually Works (Mercurial Tones)
- **Type:** Video Tutorial Transcript
- **Summary:** Step-by-step breakdown using multi-tap tape delay for 16th-note groove, reverb smearing, heavy clipping/saturation, and final bus compression.

### 27. YouTube — Ohmforce Bohm Demo & Tutorial (Daniele)
- **Title:** BEST KICK DRUM? | Ohmforce Bohm - MAIN MODULE DEMO TUTORIAL
- **Type:** Video Tutorial Transcript
- **Summary:** Hands-on walkthrough of Bohm's 9 base models, transient shaping controls (`TRS DECAY`, `TRS TONE`), and pitch curve variations (808 CCW vs 909 CW).

### 28. YouTube — Ohmforce Bohm, Groove and Performer (SynthDad)
- **Title:** Ohmforce Bohm, Groove and Performer. All you need in a kick drum
- **Type:** Video Tutorial Transcript
- **Summary:** Complete system tutorial covering Bohm models, Groove 4-tap sub-bass generator, Performer sidechain ducking depth, DJ filter modes, and live snapshot sequencing.

### 29. Reddit Thread — Bohm New Firmware & HPN
- **Title:** Ohmforce Bohm new firmware : r/modular
- **Type:** Community Discussion
- **URL:** [https://www.reddit.com/r/modular/comments/1u3y4bv/ohmforce_bohm_new_firmware/](https://www.reddit.com/r/modular/comments/1u3y4bv/ohmforce_bohm_new_firmware/)
- **Summary:** User experiences with the HPN model update, using `TAPS OUT` into external granular samplers, and standalone Groove performance without the Bohm kick.

### 30. Reddit Thread — Question Bohm Firmware
- **Title:** Question Ohmforce Bohm new firmware : r/modular
- **Type:** Community Discussion
- **URL:** [https://www.reddit.com/r/modular/comments/1t74ltt/question_ohmforce_bohm_new_firmware/](https://www.reddit.com/r/modular/comments/1t74ltt/question_ohmforce_bohm_new_firmware/)
- **Summary:** Discussion of independent volume control settings for Bohm kick vs Groove rumble, and hard panning kick Left / Groove Right for mono routing.

### 31. Reddit Thread — Bohm Demo & Workflow
- **Title:** Bohm Ohmforce demo : r/modular
- **Type:** Community Discussion
- **URL:** [https://www.reddit.com/r/modular/comments/1mzsjhn/bohm_ohmforce_demo/](https://www.reddit.com/r/modular/comments/1mzsjhn/bohm_ohmforce_demo/)
- **Summary:** Live techno rack integration, minimal sub-140BPM performance tips, and pitch envelope tuning for minimal techno.

### 32. YouTube — Es wird einfach immer besser | Ohmforce Bohm
- **Title:** Es wird einfach immer besser | Ohmforce Bohm
- **Type:** Video Tutorial Transcript
- **Summary:** German-language deep dive into Bohm firmware updates, snapshot pot-position recall, Performer ducking cutoff adjustments, and bit-crush/rectifier post-FX.

### 33. YouTube — What are these things?? Bohm new model
- **Title:** What are these things?? Ohmforce Bohm new model feat. K-Accumulator Complex OSC
- **Type:** Video Tutorial Transcript
- **Summary:** Showcase of the HPN generative sequencer model combined with Ohm Force hardware modules.

### 34-38. Additional Schwung & Module GitHub Repositories
- **Titles:** `schwung-6W6`, `schwung-8W8`, `schwung-cw-78`, `schwung-maze`
- **Summary:** Complementary repository sources containing additional C code, CMake build configurations, and catalog entry definitions.
