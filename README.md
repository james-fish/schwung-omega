# Omega

A multi-engine techno kick synthesizer for [Schwung](https://schwung.dev) on the Ableton Move. Omega combines ten kick-synthesis engines, a feedback-free groove rumble generator, a generative bass/bleep sequencer, and a one-knob performer glue stage — all in a single native module you can load in a Schwung instrument slot or a Movy track.

Omega is inspired by the Ohm Force Bohm / Groove / Performer workflow, but it is its own instrument: the goal is a full techno kick-and-rumble voice that is fast to dial in, musical by default, and light enough to run comfortably on the Move's CPU.

---

## Features

**10 kick engines** — pick one with the `MODEL` selector:

| Model | Character |
|-------|-----------|
| FM2   | 2-op FM — clean, punchy, the default voice |
| FM4   | 4-op FM with selectable OPL-style algorithms |
| WTR   | Wavetable body + separate transient synth |
| PHY   | Physical/modal kick (beater + head + shell) |
| HRD   | Hard sample-layer kick |
| DIG   | Digital/bit-reduced wave kick |
| TRS   | Transistor/909-style blend |
| ANA   | Analog 808-style with sub + sample layers |
| USR   | Load your own WAV/wavetable from the module's `samples/` folder |
| GEN   | Generative kick voice |

**Feedback-free groove rumble (TAPS)** — the classic techno delay-rumble, rebuilt as a bounded FIR tap engine. The kick is written into a tempo-synced delay ring and read back as decay-enveloped ghost copies on the 16th-note grid:

- `LENGTH` sets the tap decay time — short = distinct ghost-kicks, long = a smeared continuous sub-rumble.
- `LPF`, `TAP1–4`, `VOL`, and `MONO` shape the rumble.
- It cannot run away: there is no recirculating feedback, so it stays bounded and clean under any settings.

**Generative sequencer (GEN groove)** — a transport-locked, scale-quantized step sequencer for techno melodies in the bass and bleep range:

- 13 scales plus a free **Unquantized** mode, a continuous **ROOT** (20 Hz–2 kHz), and a **RANGE** span.
- `SEQ LEN` 1–64, `DENSITY` (Euclidean), `ROTATE` ±32, stepped `SEED`, `DECAY`, a scannable `WAVE` (sine → saw → square → wavefolded/analog), `FOLD`, and its own `FILTER`.
- `RETRIG` modes sync it to 1/2/4/8-bar windows or free-run.

**Groove FX** — shared across TAPS and GEN:

- Diode `DRIVE`, an `LFO` that sweeps the filter cutoff and reverb tone together, a plain dry/wet `REVERB` with `RV DECAY` / `RV TONE` / `RV TYPE` (Room/Hall/Plate), and a `ROUTE` selector that reorders rumble → drive → reverb.

**Performer glue** — master stage with sidechain `DUCK` (ducks the rumble's lows against the kick), a bidirectional `DJ FILT` sweep with resonance, and `CMPDR`, a one-knob compressor + diode drive for bus glue.

**Presets** — Omega saves and restores its full state through Schwung's standard preset system.

---

## Pages

- **Root** — `MODEL`, `MSTR VOL`
- **Kick 1 / Kick 2** — pitch, length, curve, the model's own voice controls, transients, filter, and post-kick FX
- **Groove 1** — `TYPE` (Taps/Gen), `VOL`, `LENGTH`, `LPF`, `TAP1–4`
- **Groove Effects** — `DRIVE`, `LFO SPD/AMT`, `REVERB`, `RV DECAY/TONE`, `ROUTE`, `RV TYPE`
- **Gen Groove / Gen Seq** (GEN type) — `SCALE`, `ROOT`, `RANGE`, `RETRIG`, and the full sequence page (`SEED`, `SEQ LEN`, `DENSITY`, `ROTATE`, `DECAY`, `WAVE`, `FOLD`, `FILTER`)
- **Performer** — `DUCK`, `DUCK REL/SLEW/FREQ`, `DJ FILT`, `DJ RESO`, `CMPDR`

---

## Install

**From the Schwung module library** (once listed): browse the library on-device and install Omega like any other module.

**Manual install:** copy the module folder to your Move:

```
scp -r omega ableton@move.local:/data/UserData/schwung/modules/sound_generators/omega
```

The folder needs `module.json` and the aarch64 `dsp.so`.

---

## Build

Omega is a single `dsp.so` cross-compiled for the Move's aarch64 / glibc 2.35 target inside the pinned Schwung builder image.

```bash
# Cross-compile the module (inside the Schwung builder image)
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/charlesvestal/schwung-builder:latest make dsp.so

# Verify no glibc symbol newer than 2.35 (on-device load gate)
./scripts/glibc_gate.sh

# Package the release asset (omega-module.tar.gz)
make dist

# Run the native offline test suite (render-to-WAV, RT-safety, determinism)
make test

# Deploy to a Move on the network
OMEGA_DEVICE_HOST=ableton@move.local \
OMEGA_DEVICE_DIR=/data/UserData/schwung/modules/sound_generators/omega \
  ./scripts/deploy.sh
```

CI (GitHub Actions) runs `make test`, the aarch64 cross-build, and the glibc gate on every push.

---

## Constraints

- C11, no C++. The audio thread does zero allocation, zero file I/O, and zero per-sample transcendentals.
- Fixed 44.1 kHz. All buffers are pre-allocated in one block at instance creation.
- Targets Linux ARM64 (Cortex-A53), glibc 2.35.

---

## License

MIT.

---

## Credits

Built by James Fish. Inspired by the Ohm Force Bohm / Groove / Performer system. Runs on [Schwung](https://github.com/charlesvestal/schwung) by Charles Vestal.
