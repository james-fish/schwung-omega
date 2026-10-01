**Omega v0.3.0 is out** 🎛️🔊

Big one this release: **p-locking now works.** Hold a step + turn any Omega knob and it locks per-step — automation and modulation too. (Turns out the host reads a module's `module.json` on disk to know what's automatable, and Omega wasn't declaring its params there — fixed, all 92 knobs are lockable now.)

Also a solid GEN groove pass:
• 🎚️ **Stable root pitch** — switching scales no longer sends the pitch flying
• 🔢 **SEQ LEN** reads clean integers 1–64
• 🔊 **Built-in sub-octave** in the GEN voice — way more low-end weight, no extra knob
• 🪄 **New resonant 18 dB/oct filter** with a snappy per-note envelope (opens on attack, closes before the tail)
• ⏱️ **DECAY re-curved** so the short/plucky range actually spreads across the knob
• 🔥 **GEN + TAPS drive** beefed up to match the kick — full-right is proper aggressive now

Update straight from the Schwung module library (it auto-tracks the latest release), or grab `omega-module.tar.gz` from the release page.

Release: https://github.com/james-fish/schwung-omega/releases/tag/v0.3.0
