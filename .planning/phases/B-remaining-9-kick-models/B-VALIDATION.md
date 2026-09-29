---
phase: B
slug: remaining-9-kick-models
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-09-29
---

# Phase B — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Phase B's hard problem is VOICING: automated tests prove each engine is
> distinct/bounded/param-responsive; the D-B02 voicing checklist is signed off
> by ear on-device (D-B04 audit doc), mirroring the A-04 hardware checkpoint.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Plain C `assert` + tiny runner (native `cc`); no third-party framework — unchanged from Phase A |
| **Config file** | none — the Makefile `test` target is the config (extend for new models) |
| **Quick run command** | `make test` (compiles harness + model TUs natively, runs asserts, writes per-model WAVs) |
| **Full suite command** | `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` |
| **Estimated runtime** | ~10 s (native harness); +cross-build + gate for full suite |

---

## Sampling Rate

- **After every task commit:** `make test` (renders the touched model's WAV + asserts; < 10 s)
- **After every plan wave:** `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so`
- **Before `/gsd:verify-work`:** full suite green in CI (D-11) **AND** `docs/VOICING_AUDIT.md` filled on-device — all 10 models PASS the D-B02 manual items
- **Max feedback latency:** ~10 s (per-task); ~2 min (full suite w/ cross-build)

---

## Per-Task Verification Map

"distinct" = render buffer differs measurably (RMS-envelope / spectral-centroid delta) from every other model's default render.

| Req ID | Behavior | Test Type | Automated Command | File Exists | Status |
|--------|----------|-----------|-------------------|-------------|--------|
| KICK-03 (FM4) | non-silent default; 6 P2 params each change output; bounded at extremes; distinct | unit | `make test` | ❌ W0 | ⬜ |
| KICK-04 (WTR) | " | unit | `make test` | ❌ W0 | ⬜ |
| KICK-05 (PHY) | " + modal freq/decay clamped (no NaN) | unit | `make test` | ❌ W0 | ⬜ |
| KICK-06 (HRD) | " + DRIVE/CRUSH bounded, no divergence | unit | `make test` | ❌ W0 | ⬜ |
| KICK-07 (DIG) | " + BIT DEPTH audibly changes, bounded | unit | `make test` | ❌ W0 | ⬜ |
| KICK-08 (TRS) | " + transient distinct from body | unit | `make test` | ❌ W0 | ⬜ |
| KICK-09 (ANA) | " + sub-osc present, long decay bounded | unit | `make test` | ❌ W0 | ⬜ |
| KICK-10 (USR) | non-silent w/ fallback (no file); loads fixture WAV off-render; params change output | unit+smoke | `make test` (fixture WAV) | ❌ W0 | ⬜ |
| KICK-11 (GEN) | non-silent; same seed → byte-identical; diff seed → differs; density gates; bounded | unit | `make test` (seed hash) | ❌ W0 | ⬜ |
| KICK-13 (switch) | A→B→A + trigger: finite, non-stale, no NaN; each p2_slot_desc emits valid bounded JSON w/ correct slot count | unit | `make test` (switch seq + JSON asserts) | ❌ W0 | ⬜ |
| KICK-14 (FX) | 5 modes audibly alter test tone; amt=0 ≈ transparent; ≤1 + finite at max amt + high pitch | unit | `make test` (per-mode + bound sweep) | ❌ W0 | ⬜ |
| FM2 re-voice (D-B03) | re-voiced FM2 still non-silent, bounded, all 11 params change output | unit | `make test` | ✅ extend | ⬜ |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_render.c` — per-model loop: prime defaults → trigger → render → assert non-silent + finite + ≤1; write `tests/output/<model>_kick.wav`
- [ ] per-model P2 param lo-vs-hi render-differ assertion (D-B02 "each param changes output")
- [ ] model-switch A→B→A + trigger finite/non-stale asserts (KICK-13)
- [ ] 5 FX modes on a test tone: audible-change + bounded-at-max asserts (KICK-14)
- [ ] pairwise distinctness metric across all 10 default renders (D-B02 "distinct character")
- [ ] `tests/fixtures/user_kick.wav` — small fixture WAV for USR load test (KICK-10)
- [ ] GEN determinism test: same seed → identical hash; diff seed → differs (KICK-11)
- [ ] `Makefile` — add new model TUs to both `dsp.so` (aarch64) and `test` (native); add wavetable-generator target (`wavetables.h`)
- [ ] `docs/VOICING_AUDIT.md` — 10-model × D-B02-checklist matrix (D-B04), filled on-device
- [ ] Framework install: none — plain C, system `cc`

---

## Manual-Only Verifications (D-B02 voicing — on-device, per model)

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Default preset sounds like a usable techno kick | D-B02 / goal | Subjective audio quality | Deploy (CI artifact → `scripts/deploy.sh`); trigger each model at defaults; listen |
| PITCH sweep / CURVE (808↔909) sounds musical | D-B02 / D-B03 | Ear is the authority | Sweep CURVE full range per model; confirm not clicky/muddy |
| Each knob sweeps a musically useful range | D-B02 | No metric for "useful" | Sweep each P2 knob end-to-end; confirm no dead zones |
| Distinct character across all 10 models | D-B02 / goal | Perceptual | A/B each model back-to-back on-device |
| No live artifacts (clip/zipper) turning knobs | D-B02 | Real-time behavior | Turn knobs live during sustained trigger |
| FM2 specifically re-voiced to reference bar | D-B03 | Subjective | FM2 is the first voicing target; sign off before others |

All rows tracked in `docs/VOICING_AUDIT.md` (10 models × checklist); phase not complete until filled PASS on-device.

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s (per-task)
- [ ] `docs/VOICING_AUDIT.md` filled PASS for all 10 models on-device (D-B04)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
