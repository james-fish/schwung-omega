# Ohm Force Bohm System Specification & Models Reference

> **Hardware Reference:** Ohm Force Bohm Stereo Dual-Voice Kick System  
> **Modules Included:** Bohm Core Module (18HP), Bohm:Groove Expander (10HP), Bohm:Performer Expander (8HP)  
> **Internal DSP Spec:** 48kHz audio sample rate, 24-bit Burr-Brown converters, 32-bit floating point processing  

---

## 1. System Overview

The Ohm Force Bohm system is a multi-engine Eurorack kick drum ecosystem designed for electronic music, live performance, and deep techno sound design. Rather than relying on a single analog or digital synthesis circuit, Bohm uses a multi-engine architecture where interchangeable kick "models" are loaded from memory/SD card.

```
+-------------------------------------------------------------------+
|                       OHM FORCE BOHM SYSTEM                       |
+---------------------------------+---------------------------------+
|  BOHM CORE MODULE (18HP)        |  GROOVE EXPANDER (10HP)        |
|  - Main Kick Engine (10 Models) |  - Secondary Ghost-Kick Voice   |
|  - Transient Synthesizer        |  - 4-Tap 16th-Note Subdivisions |
|  - Pitch Curve (808 / 909)      |  - Rhythmic Techno Rumble Gen   |
+---------------------------------+---------------------------------+
|  PERFORMER EXPANDER (8HP)                                         |
|  - Stereo Mixer & Volume Fader                                    |
|  - Sidechain Ducking Engine (Duck Depth, Release, Smooth, Low-Cut)|
|  - DJ Filter (LP/HP + Reso) & Performance Beat/Slip Rolls        |
|  - End-of-Chain Soft Clipper (+4.6dB Headroom)                   |
+-------------------------------------------------------------------+
```

---

## 2. Deep Breakdown of the 10 Kick Models

Bohm loads machine models via `.OIFF` (Ohm Force Interchange File Format) binary definition files.

### 1. `FM-2X` — 2-Operator Wavetable FM
- **Architecture:** Two-operator FM engine where Operator 1 (carrier) and Operator 2 (modulator) are both wavetable oscillators.
- **Sound Character:** Punchy, metallic, and modern FM kicks with continuous harmonic morphing via carrier/modulator ratio and wavetable sweeps.
- **Key Controls:** FM Ratio, FM Index Envelope, Wavetable Index, Attack Transient.

### 2. `HZ-1` — Wavetable + Transient Synth
- **Architecture:** Primary wavetable body oscillator paired with a dedicated transient impulse synthesizer.
- **Sound Character:** Clean, versatile, and precise. Excellent for tight 4-on-the-floor house and techno kicks where transient snap must be sculpted independently from low-end sub boom.
- **Key Controls:** Wavetable Select, Pitch Sweep, Transient Decay, Transient Color.

### 3. `OLP-4` — 4-Operator FM (OPL3 Inspired)
- **Architecture:** 4-operator FM engine inspired by classic 90s FM sound chips (Yamaha OPL3 / YMF262) with selectable operator routing algorithms.
- **Sound Character:** Aggressive, woody, hollow, and complex percussive thumps with rich enharmonic overtones.
- **Key Controls:** Algorithm Selector (1-4), Operator Ratios, FM Index Envelopes.

### 4. `PM-K1` — Physical Modeling Bass Drum
- **Architecture:** Physical modeling synth simulating a acoustic kick drum shell, resonant head, and beater impact.
- **Sound Character:** Organic, woody, warm acoustic bass drum sounds with natural head resonance and springy decay.
- **Key Controls:** Beater Material, Shell Size, Head Tension, Damping.

### 5. `PX-3` — Hard Techno Wavetable + Sampler
- **Architecture:** Hard techno wavetable oscillator combined with an internal digital sample layer and severe distortion/bit-crushing processing.
- **Sound Character:** Industrial, distorted, heavy-hitting rave and industrial techno kicks designed to cut through dense walls of sound.
- **Key Controls:** Sample Layer Select, Sample/Wavetable Mix, Drive/Crush, Transient Decay.

### 6. `SP-6` — Digital Wavetable + Sampler
- **Architecture:** Digital wavetable synth with digital waveforms (chip, additive, bit-reduced) combined with a sample playback engine.
- **Sound Character:** Electro, synthwave, and retro-futuristic digital kicks with crisp highs and precise low-end fundamental tracking.
- **Key Controls:** Digital Waveform Index, Sample Layering, Bit Depth, Pitch Envelope.

### 7. `VX-T` — Wavetable + Advanced Transient Synth
- **Architecture:** Advanced wavetable body generator coupled with a specialized transient synthesizer that models stick/beater clicks and white/pink noise bursts.
- **Sound Character:** Punchy 909-style kicks with extreme attack clarity and thick sub-bass tails.
- **Key Controls:** Transient Tone, Transient Decay, Wavetable Color, Pitch Sweep Curve.

### 8. `WT-4` — Analog Wavetable + Sampler
- **Architecture:** Wavetable oscillator initialized with warm analog synth waveforms (sampled from discrete vintage circuits) paired with a sub-oscillator and sample engine.
- **Sound Character:** Warm, fat, analog-sounding kicks (808-style sub-booms to punchy 909-style analog thumps).
- **Key Controls:** Analog Wave Morph, Pitch Curve (808 vs 909), Sub Level, Decay.

### 9. `XT-88` — User Wavetable & Sample Engine
- **Architecture:** Open model allowing users to load custom `.WAV` samples and custom 2048-sample wavetables directly from the microSD card.
- **Sound Character:** Custom user-defined kick sounds with controllable layer volume (`LAYER VOL`), pitch envelope, and transient shaping.
- **Key Controls:** User Sample Selector, User Wavetable Morph, Layer Vol, Pitch Sweep.

### 10. `HPN` — Hypnotic Generative Rumble Model (Marc Faenger Collaboration)
- **Architecture:** Co-developed with techno artist Marc Faenger. Combines a wavetable kick body with layered synthesized transients AND a generative note/pitch/velocity sequence generator that redefines the Groove circuit.
- **Sound Character:** Hypnotic, rolling, generative techno rumbles with evolving 16th-note sub-bass pitch variations.
- **Key Controls:** Sequence Seed, Scale Selector, Sub-bass Low-pass Filter (2-pole / 4-pole), Sequence Length, Color.

---

## 3. Bohm Core Module Panel Parameters

- `HIT`: Trigger button / CV input. Fires the primary kick voice.
- `LENGTH`: Sets total duration of the kick (from short 20ms clicks to multi-second sub booms).
- `SUSTAIN`: Controls the amplitude contour of the kick body tail after initial attack impact.
- `PITCH`: Fundamental frequency adjustment (calibrated from sub-C1 up to C3).
- `PITCH CURVE`: Controls the pitch envelope decay contour:
  - *Counter-Clockwise (CCW):* Slow, linear/exponential decay curve characteristic of the **TR-808** sub-boom.
  - *Clockwise (CW):* Fast, steep pitch sweep (500Hz -> 50Hz in 15ms) characteristic of the **TR-909** punch.
- `ATTACK`: Adjusts attack envelope start time and click amplitude.
- `TRS DECAY`: Transient Decay — shapes the length of the initial attack transient layer (0–30ms).
- `TRS TONE`: Transient Tone — controls high-frequency filtering and brightness of the transient click.
- `COLOR`: Timbral morph control (varies depending on active model: FM index, wavetable position, or harmonic drive).
- `FX`: Post-kick processor control:
  - *Diode:* Back-to-back diode rounding.
  - *Clip:* Asymmetric soft clipping.
  - *SAT:* Warm parallel saturation.
  - *Fold:* Wavefolder for metallic drive.
  - *Crush:* Bit-depth and sample-rate reduction.

---

## 4. Bohm:Groove Expander Specifications (10HP)

The Groove expander generates a secondary, clock-divided ghost-kick voice to create rhythmic techno rumbles in the wake of the main kick.

- `CLOCK`: Clock input synced to host tempo (16th-note subdivisions).
- `TAP 1, TAP 2, TAP 3, TAP 4`: Individual volume knobs for the four 16th-note subdivisions between primary kick hits.
- `LENGTH`: Adjusts sustain/decay duration of individual groove taps (short staccato tappings vs long sub-bass drones).
- `COLOR`: Shapes the timbre and filter cutoff of the groove voice.
- `VOL`: Master volume slider for the groove voice.
- `TAPS CV`: Single CV input controlling all 4 tap levels simultaneously.
- `TAPS OUT`: Output CV emitting the generated envelope (selectable in system settings: `GROOVE`, `I BOHM` inverted kick envelope, `PERF` ducking curve, or `BOHM` kick envelope).

---

## 5. Bohm:Performer Expander Specifications (8HP)

The Performer expander acts as a live performance mixer, sidechain ducker, and master FX unit.

- `VOL`: Fader controlling combined volume of Bohm + Groove or Bohm alone (selectable via `PERF VOL` system setting).
- `DUCK`: Sidechain ducking depth control. Automatically ducks external audio inputs whenever a kick hits.
- `DUCK TIME`: System/Model setting for ducking curve release time.
- `DUCK SMTH`: Ducking curve smoothing control to eliminate low-frequency pops/clicks.
- `DUCK BS`: High-pass threshold below which ducking operates.
- `DJ FILTER`: Dual LP/HP filter with `DJ RESO` control (0% to 100% resonance).
- `BEAT ROLL / SLIP ROLL`: Performance buffer repeat effects for live builds and drops.
- `END-OF-CHAIN SOFT CLIPPER`: Built-in limiter preventing hard digital clipping. Begins soft-clipping above 0dB (+-5V Eurorack) using +4.6dB codec headroom.
