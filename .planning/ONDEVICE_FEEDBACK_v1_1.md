# On-Device Feedback: v1.1 Build

**Date:** 2026-09-30
**Session:** First full on-device test of all 10 models + groove + performer

---

## Critical Audio Bugs (broken / silent)

| # | Area | Bug |
|---|------|-----|
| 1 | All models — FX chain | Only Diode works. Clip / SAT / Fold / Crush all sound identical — like Crush. FX type selector broken for 4 of 5 modes. |
| 2 | FM2 | ATTACK does nothing. TRS TNE (transient tone) does nothing. |
| 3 | TAPS | Sounds bit-crushed and terrible. Way too quiet — must crank VOL to 100 + add drive just to level-match the kick. |
| 4 | Performer — Duck | Ducking is completely inaudible. DUCK control does nothing. |
| 5 | PHY | CURVE appears inverted: usable range is 0–25%, above that pitch goes wildly high. HEAD TENS increases pitch to unusable range rather than lowering. |
| 6 | DIG | BIT DEPTH only audible in 0–0.1 range. Everything above sounds normal (no crushing). |
| 7 | HRD | CRUSH does nothing. DRIVE doesn't engage until 50%. |

---

## Discrete / UI Control Bugs

| # | Area | Bug |
|---|------|-----|
| 8 | FM4 | ALGORITHM is a continuous knob but clearly steps through discrete routing graphs (not morphing). Must be a discrete selector: ALG1, ALG2, ALG3 … etc. |
| 9 | FM2 | OP WAVE is a continuous knob but selects discrete waveforms. Must be discrete selector; draw the waveform in the UI. |
| 10 | HRD | Sample layer control is continuous but selects discrete samples. Must be a list picker (same model as USR sample select). |
| 11 | DIG | Wave index appears to have ~3–4 discrete options. Must be a discrete selector or list picker — show what is selected. |
| 12 | GEN | Wave selector UI is good (opens list). But audio is wrong: square sounds like triangle, saw sounds like triangle, digital/analog both show sine and sound like triangle. Only sine and triangle actually work. |
| 13 | GEN model in kick list | GEN should be removed from the kick model selector — GEN is a groove type, not a kick model. |
| 14 | TAPS + GEN filters | Filter should NOT be a type selector. Both TAPS and GEN need a single continuous LP sweep knob from ~30 Hz to 20 kHz with a log/exponential curve (lots of usable range in lows and low-mids). |
| 15 | USR layer volume | Unclear if it's blending wave layer vs sample layer. Rename to BLEND (or WAVE/SMPL BLEND). Make bidirectional from center (50% = equal). |
| 16 | Performer | DUCK SMT and DUCK BS labels are opaque — user doesn't know what they mean. Relabel clearly. |
| 17 | Soft clipper | Sounds bad — everything is better with it off. Needs retuning or better algorithm. |

---

## GEN Groove — Missing Features

| # | Feature | Detail |
|---|---------|--------|
| 18 | More scales | Chromatic/Major/Minor/Pentatonic not enough. Add: Dorian, Phrygian, Mixolydian, Hirajoshi, Hungarian, Whole Tone, Blues, Diminished |
| 19 | Root note selector | In any scale mode, user needs to set a root note (C-3 to C3 = about 6 octaves). |
| 20 | Unquantized scale option | "Unquantized" was not available as a scale. Must be added as an explicit option. |
| 21 | Root pitch in unquantized mode | When scale = Unquantized, the root note control becomes a root pitch (Hz) control. |
| 22 | Seq range | Need a control for the pitch range of the generated sequence. |
| 23 | GEN groove page layout | GEN mode groove page 1 currently shows TAPS controls (LENGTH, COLOR, TAP1–4). Replace these with: SCALE, ROOT (note or pitch), RANGE, RETRIG. Move RETRIG from gen tone page to here. |
| 24 | Gen tone page rename + layout | Rename "gen tone" to "Groove Effects". Layout should match TAPS effects page exactly (same positions for filter, LFO speed/amount, drive, reverb mix/decay/tone). |

---

## Voicing Defaults & Curve Polish

| # | Area | Issue |
|---|------|-------|
| 25 | All models — default pitch | Default pitch should be 50 Hz across all models. |
| 26 | All models — default length | Default length should be 50%. |
| 27 | All models — CURVE range | Usable range is compressed into 75–100%. Curve needs to do more work across the full range — more aggressive pitch sweep earlier. |
| 28 | FM4 defaults | OP RATIO, OP INDEX, OP AMP, FEEDBACK should default to ~0.1 (7–8 o'clock position). |
| 29 | PHY | Fix HEAD TENS direction: more tension should NOT increase pitch this aggressively (it blows up into unusable territory). |
| 30 | TRS | WAVETABLE COLOR only usable in first 50%. Second 50% becomes an aggressive buzz with no kick body. Fix upper range. |
| 31 | ANA | WAVE MORPH is inaudible except when CURVE is low (~30%). Needs more impact across the range. |
| 32 | Performer | Add a DUCK CURVE parameter (controls the shape of the duck envelope: linear vs exponential). DUCK itself is inaudible — fix first (#4 above). |
| 33 | Soft clipper | Tune or replace algorithm. Current one sounds bad; everything better with clip off. |

---

## What Works Well

- Models remember their last settings when switching — good.
- FM2 voicing sounds good.
- ANA sub-level and sub-decay now audible — good.
- GEN wave selector UI (list picker) is the correct model for ALL wave/sample selectors in the module.
- USR sample select "none" state works well.
- Reverb sounds good — keep it.
- GEN retrig works. LFO speed/amount work. Drive works.
- FX Diode works.
