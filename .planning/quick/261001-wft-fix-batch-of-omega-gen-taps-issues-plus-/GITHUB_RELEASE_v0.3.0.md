## Omega v0.3.0 — P-locking + GEN groove glow-up

### 🔒 P-locking now works (the big one)
Hold a step and turn any Omega knob to lock a per-step value — automation and
modulation too. Previously the host refused every lock with *"can't automate this."*

**Why it was broken:** the new Schwung host builds its automation/p-lock parameter
table by reading a module's **static `module.json` on disk**, not the dynamic
`ui_hierarchy` Omega serves at runtime. Omega declared neither a `ui_hierarchy` nor a
`chain_params` key in that file, so the host registered **zero** parameters and
refused every lock as `unknown_param`. v0.3.0 adds a static `chain_params` array (all
92 automatable parameters) to `module.json` — purely additive; the on-screen UI is
unchanged.

### 🎛️ GEN groove fixes
- **Root pitch is stable** across scale changes (no more jumping to the top in
  Unquantized mode).
- **SEQ LEN** shows whole integers 1–64 (no decimals).
- **Built-in sub-octave** baked into the GEN voice — weightier low end, no new knob.
- **New GEN filter:** 18 dB/oct resonant filter with a per-note envelope that opens
  ~20% at attack and closes faster than the amp tail (filter decay ≈ 65% of note decay).
- **DECAY knob re-curved** so the short/plucky range spreads across most of the travel.

### 🔊 Drive
- GEN and TAPS **DRIVE** strengthened to match the kick's FX drive character —
  full-right is now genuinely aggressive, with makeup gain so it adds grit, not just level.

### Install
Update from the Schwung module library (catalog tracks the latest release
automatically), or drop the `omega/` folder from `omega-module.tar.gz` into
`/data/UserData/schwung/modules/sound_generators/`.

Native test suite green; aarch64 `dsp.so` glibc-2.35 gated via CI.
