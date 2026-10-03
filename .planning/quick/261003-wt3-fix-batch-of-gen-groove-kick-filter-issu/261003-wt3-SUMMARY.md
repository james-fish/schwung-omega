# Quick Task 261003-wt3 — SUMMARY (v0.4.1)

**Completed:** 2026-10-04
**Branch:** quick/gen-groove-kickfilter-batch
**Tests:** full native harness GREEN (`make test` — 11 suites: fm2, fx, switch, params,
distinct, gen, groove, readback, samples, perf, taps_redesign).

## What shipped

Root-caused and fixed a 14-item on-device feedback batch. Key architectural note: the
live GEN engine is in `src/groove.c` (`groove_tick`), not the legacy `src/models/gen.c`.

| # | Item | Fix |
|---|------|-----|
| 2 | SEQ LEN "snaps to 64" | `grv_gseqlen` is an `int` param — host sends the RAW integer (like the working `grv_grootnote`). Was mis-handled as normalized `v*63`→always 64. Now parsed raw (1..64); readback echoes raw; UP_INT default emits raw; cache default 16. |
| 3 | ROOT "1 Hz→20 Hz" + shows in both modes | Merged ROOT + ROOT NOTE into one Hz control (`grv_groot`) used in both modes (`gen_base_hz = gen_root_hz`). Defensive parse (raw Hz; stray 0..1 → log-mapped). Cache default 0.18→45. ROOT NOTE dropped from UI. |
| 4 | RETRIG→RESET, loop + silence | Renamed RESET; options None/1–8 Bar/On Note. Stepping rewritten: pattern **always modulo-loops**; reset to step 0 every N bars via a note-len-aware 16th accumulator → short seqs loop and reset cleanly. |
| 5 | GEN NOTE LEN | New enum `grv_gnotelen` (1/4d…1/32) scales the step duration. |
| 6a | ROUTE "Rmbl"→"Grv" | Renamed in OPT_FXROUTE + module.json. |
| 6b | Kick LPF floor ~50 Hz | All 9 models' PK_COLOR floor lowered to 50 Hz (fully closed). |
| 7 | SWING re-added | New bipolar `grv_gswingamt` (0.5=none), subtle ±8% max; lengthens even/shortens odd steps. |
| 8 | On-Note restart dead | `on_midi` gate was `type==GEN` (never true in v0.4). Now restarts on every kick note-on when RESET=On Note. |
| 9 | Filter env depth/reso +10% | GEN filter: depth 3.0→3.3, resonance 0.45→0.495. |
| 10 | GEN>TAPS send | New `grv_gentaps`; GEN voice summed into the TAPS delay-ring input → rumble/reverb act as a delay/echo on GEN. |
| 11 | Pluckier GEN env | Two-component amp env: fast pluck (DECAY-controlled) over a 50% body that releases to 0; filter env = 65% of amp env. |
| 12 | TAPS HPF + layout | New `grv_hpf` (bypassed at min). Taps page: TVOL / DECAY / HPF / LPF (row 1), TAP1–4 (row 2). |
| 13 | TVOL rename | TAPS VOL → name "TAP VOL", short "TVOL". |
| 1 | GEN VOL bar | Metadata kept parallel to TAP VOL; the OLED bar-widget trigger isn't knowable offline — **verify on device**. |

## Files changed
`src/omega.h`, `src/params.h`, `src/params.c`, `src/groove.h`, `src/groove.c`,
`src/dsp.c`, `src/ui.c`, `src/models/{fm2,fm4,wtr,phy,hrd,dig,trs,ana,usr}.c`,
`module.json`, `release.json`, `tests/test_switch.c`, `tests/test_groove.c`.
OMEGA_GKI_COUNT 41→45 (+grv_hpf/gnotelen/gswingamt/gentaps). Version 0.4.0→0.4.1.

## Deploy
No local Docker/cross-compiler, so the shippable aarch64 `dsp.so` builds in CI on push
(`.github/workflows/ci.yml` → artifact `omega-dsp-aarch64`). To land on the Move:
download that artifact and run `./scripts/deploy.sh`, or cut the v0.4.1 release so the
main-tracking catalog picks it up.

## Flag for on-device verification
- Item 1: confirm GEN VOL now renders as a vertical bar like TAP VOL.
- Item 3: confirm ROOT shows/accepts Hz cleanly (no 1→20 flicker).
- Item 11: confirm the pluck/50%-sustain feel is musical (DECAY sets pluck + body length).
