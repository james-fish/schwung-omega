---
phase: C
slug: groove-rumble-engine
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-09-29
---

# Phase C — Validation Strategy

> Per-phase validation contract. The high-risk item is tempo derivation (GRV-02):
> the mock host must become tempo-drivable (Wave 0) so tap positions can be
> asserted against driven BPM offline. On-device tempo-lock "feel" folds into the
> later deferred voicing round.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Plain C `assert` + custom runners (native `cc`); no third-party framework |
| **Config file** | none — the Makefile `test` target IS the config |
| **Quick run command** | `make test` (aggregates test-fm2/fx/switch/params/distinct/gen/groove; < ~12 s) |
| **Full suite command** | `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` |
| **Estimated runtime** | ~12 s native; +cross-build + gate for full suite |

---

## Sampling Rate

- **After every task commit:** `make test` (native; < ~12 s) — must stay green
- **After every plan wave:** `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so`
- **Before `/gsd:verify-work`:** full suite green in CI; on-device tempo-lock feel folded into the deferred voicing round
- **Max feedback latency:** ~12 s (per-task)

---

## Per-Task Verification Map

| Req ID | Behavior | Test Type | Automated Command | File Exists | Status |
|--------|----------|-----------|-------------------|-------------|--------|
| GRV-01 | 4-tap kick-fed delay: taps at N/2N/3N/4N behind write head, each scaled by TAP level; finite/bounded | unit | `./build/test_groove` | ❌ W0 | ⬜ |
| GRV-02 | Tap positions track driven BPM (120/128/174 → `(60/bpm)*sr/4 ±1`); NULL callback → get_bpm → 120-constant fallback each exercised; never live-hardcoded 120 | unit | `./build/test_groove` (parametric BPM + fallback cases) | ❌ W0 | ⬜ |
| GRV-03 | Page-1 VOL/LENGTH/COLOR/TAP1-4 lo-vs-hi differ measurably; bounded at extremes | unit | `./build/test_groove` (assert_param_responsive style) | ❌ W0 | ⬜ |
| GRV-04 | GEN Page-2 keys present in ui_hierarchy iff model==GEN, hidden otherwise; GEN seq clocks to driven BPM; SEQ LEN/DENSITY/SCALE/SEED change output; LPF POLE 2 vs 4 differ; same SEED → byte-identical | unit | `./build/test_groove` + extend `test_switch.c` (JSON gating) + `test_gen.c` (transport determinism) | ❌ W0 | ⬜ |
| GRV-05 | MONO on → groove L==R; MONO off + asymmetric taps → L!=R | unit | `./build/test_groove` | ❌ W0 | ⬜ |

Cross-cutting (every render path): no NaN/Inf (`isfinite` sweep), `|x| ≤ 1.0` pre-int16, zero audio-thread alloc (malloc trap, Linux CI), determinism where seeded.

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/mock_host.c` — **extend** to be tempo-drivable: settable `double g_mock_beat` + setter (advance per block), wire a `mock_get_bpm` stub onto `h.get_bpm` (currently NULL), and add a "NULL transport" host variant (both callbacks NULL) to exercise the last-resort 120 path. **Enabling gap for the entire GRV-02 test.**
- [ ] `tests/test_groove.c` — new harness: drive beat at 120/128/174 BPM assert tap offsets track; MONO channel equality; Page-1 param responsiveness; bounded/finite. Add `GROOVE_TEST_SRCS` + `test-groove` target to Makefile + include in `test`.
- [ ] extend `tests/test_switch.c` — assert `groove2`/SEQ LEN/LPF FREQ/LPF POLE keys present in ui_hierarchy iff model==MODEL_GEN, absent otherwise; hierarchy stays brace/bracket-balanced + null-terminated.
- [ ] extend `tests/test_gen.c` — GEN determinism holds with the transport clock (same driven BPM + SEED → byte-identical); SEQ LEN changes the pattern.
- [ ] Framework install: none — plain C, system `cc`.

*(WAV writer, malloc trap, mock-host skeleton, param-responsiveness battery reused; mock-host beat drivability is the one real new piece.)*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Rumble tempo-lock *feel* across live BPM changes | GRV-02 / SC1 | Perceptual timing on hardware | On-device: change project tempo, confirm rumble re-locks with no drift/glitch — folded into the deferred voicing round |
| Groove Page-1 shapes the rumble musically | GRV-03 / SC2 | Subjective | On-device audition (deferred voicing round) |
| GEN generative rumble sounds hypnotic/rolling | GRV-04 | Subjective | On-device audition (deferred voicing round) |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (esp. mock-host beat drivability)
- [ ] No watch-mode flags
- [ ] Feedback latency < 12s (per-task)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
