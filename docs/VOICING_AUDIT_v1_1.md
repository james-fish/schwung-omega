# v1.1 Voicing Audit (on-device) — Phase B2

Offline tests can prove params are *responsive, bounded, distinct* — they cannot
prove a kick *sounds good*. This checklist is the on-device gate for the B2
voicing changes. Deploy (`make dsp.so` → glibc gate → `scripts/deploy.sh`),
audition each item, mark PASS/ADJUST, and feed adjustments back.

## Global changes to verify

- [ ] **PITCH is now direct Hz** (30–200 Hz, default ~50). Confirm the knob reads
      Hz and a musical kick fundamental is reachable low without maxing CURVE
      (VOICE-01). Range feels right? floor/ceiling ok?
- [ ] **CURVE is stronger** — sweep depth now `f0*(1.5 + curve*7)` capped 1000 Hz
      (was `2+curve*4` capped 480). Confirm CURVE gives a pronounced 909 drop and
      "more curve" without needing to max it (VOICE-02).
- [ ] **Page reorg**: Kick Page 1 = PITCH/LENGTH/CURVE + model-unique; Kick Page 2
      = ATTACK/TRS DEC/TRS TNE/FILTER/FILT RTE/FX TYPE/FX AMT/FX TONE (VOICE-04).
- [ ] **FX TONE** tilt (Page 2) — neutral at center, darker left, brighter right.
- [ ] **Drive auto-gain** (UIX-06) — raising FX AMT on SAT/Fold/Clip changes
      character, not just loudness.

## Per-model (defaults tuned; confirm they sound like a kick at default)

- [ ] **FM2** — default ratio/index lowered (0.22/0.30) to kill the shrill/piercing
      default (VOICE-06). Warm + punchy now?
- [ ] **FM4** — ALGO2 merged away; 5 unique params fit Page 1. Still distinct/usable?
- [ ] **WTR** — WAVE/BODY PITCH defaults raised (0.6/0.7) so it's not just the
      transient at default (VOICE-06). Body present now?
- [ ] **PHY** — PITCH is now the head-mode base Hz, decoupled from HEAD TENS
      (which only detunes ±25%). Confirm PHY reaches LOW pitch and HEAD TENS no
      longer just pushes up (VOICE-06). 
- [ ] **HRD / DIG** — Hz pitch + stronger curve; still aggressive/crunchy.
- [ ] **TRS** — ⚠️ PENDING REDESIGN: user wants the unique transient to blend
      between *distinct transient sources* (samples/synth clicks), not just noise.
      NOT yet implemented — needs the B3 sample infra + a transient-source morph.
- [ ] **ANA** — SUB LEVEL default raised (0.7) + sub weighted dominant in the mix
      (0.95). Confirm SUB LEVEL/DECAY are now clearly audible (VOICE-06). SMP
      (sample thump) audible?
- [ ] **USR** — WT MORPH now crossfades adjacent built-in waves (was discrete
      jumps) — confirm smooth morph (VOICE-06). SAMPLE SELECT picker lands in B3.

## Known follow-ups (tracked, not yet wired)

- [ ] **FILTER ROUTE (SYN/TRANS/BOTH)** — control exposed; DSP currently applies
      COLOR to the whole voice (= Both, the default). The Synth-only / Transient-
      only split needs each model to expose body vs transient to a shared filter —
      a per-model refactor scheduled as a B2 follow-up.
- [ ] **TRS transient-source morph** — see above; depends on B3 sample infra.
- [ ] **PITCH schema min/max** shows 30/200 but a couple of models still clamp
      internally via omega_pitch_hz — confirm no model ignores the low end.

Mark each PASS on device; report ADJUSTs for a tuning pass.
