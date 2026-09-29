# v1.1 "Refinement" — On-Device Test Feedback (source of truth)

Captured 2026-09-29 from on-device testing of Phases B (voicing) and C (groove).
This is the authoritative brief for the v1.1 milestone. Phase CONTEXT/PLAN docs
must trace back to the items here. Sidechain/duck is deferred to Phase D.

## Investigation findings (root causes, confirmed from code)

- **"Everything shows 0 / model box empty / vol resets / knobs look zeroed"** —
  `omega_get_param` (src/dsp.c) only answers `"ui_hierarchy"` and returns `-1`
  for every individual key. The host reads back current values PER KEY (reference
  `ui_chain.js:37-40`: `host_module_get_param(PARAMS[i].key)`). Omega answers
  nothing → every knob renders at 0. Also the schema (src/ui.c) has no `default`,
  no `type:"enum"`, no `unit`/`step`/`short_name` — all bare `float 0..1`.
  Reference modules (tapedelay/freeverb/psxverb `module.json`) carry all of that.
  No per-model value memory exists.
- **"Groove is a bit-crushed, quiet hot mess regardless of source"** — NOT CPU.
  `groove_tick` (src/groove.c) is a dry 4-tap 16th-note echo of the kick with NO
  feedback and NO resonance. Four discrete delayed copies at ~0.85 summed gain =
  gated thin echo-hits, not a continuous rumble. Real Bohm rumble needs
  feedback/resonance. Fix = the redesign below.

Reference modules to study: `/Users/jamesfish/Downloads/audiofx/`
(tapedelay.so, freeverb.so, psxverb.so, ducker.so, palette.so + their
module.json / help.json + ui_chain.js). Freeverb = cheap-reverb reference.

---

## Phase B.1 — Param/UI Infrastructure (FOUNDATIONAL, do first)

Pulls Phase E (UI hierarchy) work forward. Unblocks nearly every UI complaint.

- `get_param(key)` must return the CURRENT value as a string for every param key
  (locale-independent float formatting), not just `ui_hierarchy`. This is the
  knob-readback path the host uses.
- Rich param schema per control (match reference module.json): `type`
  (`enum`/`int`/`float`), `options` (string list for enums), `default`, `min`,
  `max`, `step`, `unit`, `short_name`, `display_format` where useful.
- **Per-model param state memory**: switching models must remember that model's
  last knob positions (do NOT reset visually or actually to 0/50%). Currently a
  model switch memsets state and re-primes to 0.5 but the UI shows 0.
- **Discrete string selectors** (same enum control type as Model): FX type,
  scale, groove type, retrigger mode, filter routing, reverb type, sample picker.
- Each control picks the RIGHT UI element + sweep type (not everything a pot):
  bidirectional/centered where it makes sense, discrete where it makes sense,
  proper units (e.g. PITCH in Hz).
- **Drive/auto-gain**: drive amount gets automatic output-gain compensation.
- Root page is too empty: later it should host the most-used params (duplicated)
  — this connects to Phase F macros; at minimum leave room and don't regress.

## Phase B.2 — Kick voicing + page reorg

Global voicing:
- PITCH starts too high and goes too high → LOWER floor and LOWER ceiling
  (most models sound like a kick around 700–900-ish only with curve maxed today;
  target: musical kick range with curve doing more of the work). Curve needs
  MORE curve to it.
- Merge **TRS TNE + COLOR** (they do basically the same thing — a filter/tone).
- Merge **LENGTH + SUS** (basically the same thing).
- **Page reorg** — Page 1 is where the fun is; transient is not the first reach:
  - **Kick Page 1**: PITCH, LENGTH, CURVE, then 4–5 MODEL-UNIQUE params.
  - **Kick Page 2**: the 3 transient controls, FX selector + FX amt + FX tone,
    plus 2 more: filter + filter routing (SYN / TRANSIENT / BOTH).
  - Alternative allowed if it's cleaner to squash FX onto Page 1 (but then we
    lose FX tone). Claude decides; bias to keeping Page 1 the fun one.
  - FM engines: only one FM engine has 6 controls — merge two into one to fit
    Page 1.
- FX: discrete selector + amount + a THIRD tone/mix control.

Per-model fixes:
- **WTR**: default sounds like just transient → increase WTSEL and BODYPIT
  defaults.
- **FM2/FM4**: voicing good, but FM2 default is piercing/shrill → LOWER default
  ratio and index.
- **PHY**: doesn't go low enough in pitch; HEAD TENS only sweeps pitch further
  UP (wrong) — fix direction / add real low-pitch range.
- **TRS**: interesting, but you can't tune the unique transient, only blend it
  with noise. The static transient params already use noise → redundant. Make
  TRS blend between DIFFERENT transient sounds/samples/synth transients instead.
- **ANA**: SUB LEVEL and SUB DECAY do nothing audible; SMP does nothing → fix so
  they're audible.
- **USR**: SMSEL should be a STRING LIST PICKER of samples from a folder (see
  B.3), not a knob. WMORP has weird jumps — smooth it. Both USR/ANA sound great
  at pitch ~700–900 + curve maxed, which shows the global voicing changes needed.
- **Sample selection anywhere**: never a knob — always a list picker showing a
  string name.

## Phase B.3 — Sample infrastructure

- A samples FOLDER inside the module hosting sample content.
- Optionally browse the SD card for samples too.
- String list-picker UI (from B.1) surfaces sample names for USR SMSEL and any
  sample-select control.

## Phase C.1 — Groove redesign (fixes the hot-mess + full redesign)

Architecture:
- Groove gets a **Type selector** (discrete string, like Model): **TAPS** vs
  **GEN**. This DECOUPLES generative groove from the base kick model selection —
  GEN is now a groove mode pairable with ANY kick model, exchangeable with TAPS.
  (Differs from Bohm/Groove.) Selecting GEN as groove type swaps the groove
  controls; selecting TAPS shows tap controls.
- Fix the "bit-crushed & quiet" — redesign to a proper feedback/resonant rumble,
  not a dry 4-tap echo.
- 1–2 pages per type.

TAPS type:
- Page 1: existing tap controls (COLOR is a filter — make it actually do
  something). VOL, LENGTH, taps.
- Page 2: FX incl. **reverb** (cheap model, 1–3 types, with MIX + DECAY + TONE =
  4 UI elements), **drive** (one knob), **filter type**, **LFO speed**, **LFO
  amount** on the taps (only add controls that don't overlap Page-1 taps).

GEN type (2 pages):
- Page 1: **SCALE** (discrete string selector — all scale types incl. unquantized
  / off), **SEED**, **SEQ LEN** (1–32, actually seeds a 1–32 × 16th-note
  sequence), **ROTATE** (bidirectional), maybe **SWING**.
- Page 2: **WAVE TYPE** (select waveform), **WAVEFOLDER**, **FILTER** (RES + ENV
  nice; probably drop LP POLE; the current filter is only usable near fully
  closed → needs a different curve), **LFO**, **MUTATE** button (mutate the
  sequence), **DELAY**, **REVERB**, **DRIVE**.
- **Retrigger mode** control: on-note, 1 bar, 2 bar, 4 bar, 8 bar, or NONE
  (starts with the sequencer, obeys length). Default = NONE (don't retrigger on
  every kick hit — current behavior is wrong).
- Sequence STOPS when the transport/sequence stops.
- More tone shaping so it's not just "a wavefolded kick" — broaden the range,
  make it musical.

General control conventions (apply across C.1 and reuse B.1):
- Where possible controls are bidirectional/centered, discrete, with proper
  values appropriate to the control (e.g. pitch in Hz).

## Deferred to Phase D (already acknowledged by user)

- Sidechain / ducking (no sidechaining yet — comes with the Performer chain).
