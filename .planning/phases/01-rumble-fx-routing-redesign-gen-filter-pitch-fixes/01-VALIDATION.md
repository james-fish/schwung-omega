---
phase: 1
slug: rumble-fx-routing-redesign-gen-filter-pitch-fixes
status: complete
nyquist_compliant: true
wave_0_complete: true
created: 2026-10-01
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Derived from 01-RESEARCH.md "## Validation Architecture".

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Plain C asserts + WAV/RMS/ZCR harness (no third-party framework); native `cc`, `-DOMEGA_MALLOC_TRAP` on Linux |
| **Config file** | `Makefile` (`GROOVE_TEST_SRCS`, `TAPS_TEST_SRCS`; `test:` aggregates all suites) |
| **Quick run command** | `make test-groove` |
| **Full suite command** | `make test` |
| **Estimated runtime** | ~10 seconds |

Existing pattern: `tests/test_groove.c` + `tests/test_taps_redesign.c` drive the real plugin (`move_plugin_init_v2 → create_instance → set_param → on_midi → render_block`) through `tests/mock_host.c` (tempo-drivable: `mock_host_set_bpm` / `mock_host_advance_beat` / `make_mock_host_null_transport`). Helpers: `buf_rms`, ZCR, `samples_per_16th`, `dbeat_for_bpm`, `neutralise_chain`. WAV via `tests/wav.h`. Guards: malloc-trap abort, `isfinite`, int16 clamp.

---

## Sampling Rate

- **After every task commit:** Run `make test-groove` (+ `make test-gen` if GEN touched)
- **After every plan wave:** Run `make test`
- **Before `/gsd:verify-work`:** Full `make test` green + cross build `make dsp.so` + `scripts/glibc_gate.sh` (objdump gate)
- **Max feedback latency:** ~10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 1-00-01 | 00 | 0 | ALL (test scaffold) | integration | `make test-groove` | ❌ W0 | ⬜ pending |
| 1-01-01 | 01 | 1 | RUMBLE-CORE (no runaway) | integration | `make test-groove` (`test_no_runaway`) | ❌ W0 | ⬜ pending |
| 1-01-02 | 01 | 1 | RUMBLE-CORE (audible@default) | integration | `make test-groove` (`test_rumble_audible`) | ❌ W0 | ⬜ pending |
| 1-01-03 | 01 | 1 | RUMBLE-CORE (LENGTH morph) | integration | `make test-groove` (`test_length_morph`) | ❌ W0 | ⬜ pending |
| 1-02-01 | 02 | 2 | FX-ROUTE (order changes output) | integration | `make test-groove` (`test_route_changes_output`) | ❌ W0 | ⬜ pending |
| 1-02-02 | 02 | 2 | FX-ROUTE (reverb bounded) | integration | `make test-groove` (`test_no_runaway`) | ❌ W0 | ⬜ pending |
| 1-03-01 | 03 | 2 | GEN-FILTER (LP sweep attenuates) | integration | `make test-groove` (`test_gen_filter_sweep`) | ❌ W0 | ⬜ pending |
| 1-04-01 | 04 | 2 | GEN-PITCH (root tracks, sub-bass) | integration | `make test-groove` (`test_gen_root_pitch`) | ❌ W0 | ⬜ pending |
| 1-04-02 | 04 | 2 | GEN-PITCH (determinism golden) | integration | `make test-gen` | ✅ (update) | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_groove.c` — add `test_no_runaway`, `test_rumble_audible`, `test_length_morph`, `test_route_changes_output`, `test_gen_filter_sweep`, `test_gen_root_pitch`; add a simple autocorrelation-pitch helper for GEN-PITCH. Reuse existing helpers.
- [ ] `tests/test_taps_redesign.c` — re-point the existing LENGTH + reverb gates to the new feedback-free semantics (old gates assumed `fb_amount`/PRE-POST).
- [ ] `tests/test_gen.c` — re-baseline the determinism golden after the centered-offset change (keep "same seed → identical"; update expected values).
- [ ] No new framework — plain C asserts + existing harness cover everything.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Subjective "sounds like a good techno rumble, not bit-crushed" | RUMBLE-CORE | Perceptual quality is not fully capturable by RMS/ZCR | Deploy `dsp.so` to Move; TAPS groove, sweep LENGTH + routing + reverb; confirm smeared sub-rumble with no crackle |
| On-device CPU headroom (no crackle under full chain + reverb) | RUMBLE-CORE | Requires aarch64 hardware + real audio callback timing | Deploy; run full chain with reverb + max LENGTH; confirm no dropouts |

*Automated tests prove bounded/finite/level/pitch; the two rows above are the on-device confirmation gate.*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** approved 2026-10-01
