# Techno Rumble & Kick Synthesis Principles

> **Domain:** Electronic Music Production & Digital Signal Processing (DSP)  
> **Core Objective:** Principles, Signal Flow, and Mathematical/DSP Formulas for Generating Techno Rumble Kicks  

---

## 1. The Unified Techno Rumble Formula

A techno rumble kick is a production technique where the energy of a primary kick drum is split, processed through spatial/delay smearing, distorted, low-pass filtered, and dynamically ducked to fill the low-end frequency spectrum (30 Hz – 150 Hz) between kick hits.

```
                    +-----------------------+
                    |  PRIMARY KICK SOURCE  |
                    +-----------+-----------+
                                |
             +------------------+------------------+
             |                                     |
             v                                     v
     +---------------+                     +---------------+
     |  DRY KICK     |                     |  RUMBLE CHAIN |
     |  TRANSIENT    |                     +-------+-------+
     +-------+-------+                             |
             |                   1. SMEAR (Reverb / 16th Multi-Tap Delay)
             |                                     |
             |                   2. MONO ENFORCE (Sum L/R -> Mono)
             |                                     |
             |                   3. SATURATE / DRIVE (Wavefold / Soft-Clip)
             |                                     |
             |                   4. FILTER (Low-pass Cutoff < 150Hz)
             |                                     |
             |                   5. SIDECHAIN DUCK (Pump on Kick Trigger)
             |                                     |
             v                                     v
     +-------+-------------------------------------+-------+
     |                 MASTER BUS SUM                      |
     |           (Soft-Clip Limiter + Glue Comp)           |
     +-----------------------------------------------------+
```

---

## 2. The 4 Stages of Rumble Processing

### Stage 1: Smearing (Reverb / Delay)
- **Objective:** Convert a short, discrete kick impulse into a sustained acoustic/rhythmic tail.
- **Delay Method:** 1/16th note or 3/16th note dotted delays. Emphasizes upbeat ghost-kick Taps (like Bohm Groove's 4 taps).
- **Reverb Method:** Large room, hall, or warehouse reverb with 1.0s – 3.0s decay time. High wet mix (80%–100% wet).

### Stage 2: Mono Summing
- **Objective:** Club sound systems require clean, phase-aligned mono low end below 150 Hz.
- **DSP Rule:** Sum Left and Right channels: $Y_{mono}[n] = 0.5 \times (X_L[n] + X_R[n])$.

### Stage 3: Saturation & Harmonic Generation
- **Objective:** Low-frequency sine waves lack upper harmonics and sound quiet on smaller speakers. Saturation generates psychoacoustic upper harmonics.
- **Saturation Models:**
  - *Asymmetric Soft Clipping:* Introduces even and odd harmonics.
  - *Wavefolding:* Folds voltage peaks back over zero, adding metallic grit.
  - *Tube Saturation:* Smooth low-order polynomial distortion.

### Stage 4: Filtering & Low-Pass Roll-off
- **Objective:** Remove mid/high frequencies above 150 Hz so the rumble functions strictly as sub-bass.
- **Filter Topology:** 24dB/octave (4-pole) Low-Pass Filter at 120Hz – 180Hz. Cut frequencies below 30Hz with a steep High-Pass Filter to eliminate unhearable speaker-flapping DC mud.

### Stage 5: Sidechain Ducking
- **Objective:** Avoid low-frequency phase cancellation when the primary kick transient strikes.
- **Ducking Curve:** Inverse envelope trigger. When `HIT` fires, duck volume to zero within 1ms, then release smoothly over 80ms – 150ms before the next 16th note.

---

## 3. Multi-Layer Rumble Architecture

Advanced techno tracks utilize 3 distinct layers within the kick group:

| Layer | Frequency Range | Processing Chain | Function |
| :--- | :--- | :--- | :--- |
| **1. Primary Kick** | 30 Hz – 10 kHz | Pitch sweep (500Hz -> 50Hz) + High-frequency click | Direct transient punch |
| **2. Sub-Bass Rumble** | 30 Hz – 100 Hz | Reverb smear -> Mono -> 24dB LPF -> Heavy Ducking | Deep warehouse sub-bass |
| **3. Percussive Mid-Layer** | 100 Hz – 600 Hz | 16th-note multi-tap delay -> Wavefolder -> BPF | Rhythmic forward momentum |
| **4. Texture Air Layer** | 800 Hz – 4 kHz | Frozen reverb -> High-pass filter -> Micro-ducking | Atmospheric grit & width |

---

## 4. Mathematical & DSP Algorithms

### 1. Exponential Pitch Envelope (Kick Body)
A classic 909 kick pitch sweep is modeled using an exponential frequency decay:
$$f(t) = f_{start} \cdot e^{-t / \tau_p} + f_{fundamental}$$
Where $f_{start} \approx 400\text{ Hz}$, $f_{fundamental} \approx 50\text{ Hz}$, and $\tau_p \approx 0.015\text{ s}$ (15 ms pitch decay time).

### 2. Asymmetric Soft-Clipper (`fast_tanh`)
Soft-clipping saturation limits dynamic peaks while adding warm harmonics:
$$y(x) = \begin{cases} 
\tanh(x) & \text{if } x \ge 0 \\
\frac{x}{1 + |x|} & \text{if } x < 0 
\end{cases}$$

### 3. Wavefolder
Wavefolding folds signals exceeding threshold $V_{th}$:
$$y(x) = | (x + V_{th}) \pmod{4 V_{th}} - 2 V_{th} | - V_{th}$$

### 4. Zero-Delay Feedback (ZDF) Moog 4-Pole Transistor Ladder Filter
For resonant, self-oscillating percussion filtering without digital delay-free loop instability, use a ZDF topology solving the non-linear pole equations:
$$s_k[n] = s_k[n-1] + 2 \cdot v_k[n]$$
Where $v_k[n]$ represents the state updates across all 4 cascaded low-pass stages with feedback $G_{reso}$.
