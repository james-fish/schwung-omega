---
phase: A
slug: foundation-fm2-model
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-09-29
---

# Phase A — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Plain C `assert` + tiny custom runner (native `cc`); no third-party framework |
| **Config file** | none — the Makefile `test` target IS the config (installed in Wave 0) |
| **Quick run command** | `make test` (compiles `tests/test_render.c` natively w/ `-DOMEGA_MALLOC_TRAP`, runs, writes WAV) |
| **Full suite command** | `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` |
| **Estimated runtime** | ~5 seconds (native harness); +cross-build + gate for full suite |

---

## Sampling Rate

- **After every task commit:** Run `make test` (native harness; < 5 s)
- **After every plan wave:** Run `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so`
- **Before `/gsd:verify-work`:** Full suite green in GitHub Actions (D-11) + on-device manual checklist (SC1, SC5)
- **Max feedback latency:** ~5 seconds (per-task); ~2 min (full suite w/ cross-build)

---

## Per-Task Verification Map

| Req ID | Behavior | Test Type | Automated Command | File Exists | Status |
|--------|----------|-----------|-------------------|-------------|--------|
| FNDTN-01 | vtable populated, `create/render/destroy` run without crash | smoke | `make test` (lifecycle in test_render.c) | ❌ W0 | ⬜ pending |
| FNDTN-02 | module.json parses; required fields present | unit | `make test` (json field assertions) / CI `jq` check | ❌ W0 | ⬜ pending |
| FNDTN-03 | zero alloc during render | unit | `make test` w/ `-DOMEGA_MALLOC_TRAP` (abort = fail) | ❌ W0 | ⬜ pending |
| FNDTN-04 | dsp.so builds; no GLIBC>2.35, no libmvec, 1 export | integration | `./scripts/glibc_gate.sh build/dsp.so` | ❌ W0 | ⬜ pending |
| FNDTN-05 | FTZ set (compiles on aarch64; no-op native) | unit | `make dsp.so` (compile) + review of FZ snippet | ❌ W0 | ⬜ pending |
| FNDTN-06 | mock host + WAV render produces file | smoke | `make test` → `tests/output/fm2_kick.wav` nonzero | ❌ W0 | ⬜ pending |
| FNDTN-07 | output finite + clamped ≤1.0 pre-int16 | unit | `make test` (isfinite/clamp asserts + int16 range) | ❌ W0 | ⬜ pending |
| KICK-01 | model dispatch through registry; FM2=id 0 | unit | `make test` (assert g_models[0]->name=="FM2") | ❌ W0 | ⬜ pending |
| KICK-02 | FM2 renders non-silent, deterministic kick | unit | `make test` (energy > threshold; fixed-seed hash stable) | ❌ W0 | ⬜ pending |
| KICK-12 | 8 Page-1 params change output audibly | unit | `make test` (PITCH lo/hi → differing buffers) | ❌ W0 | ⬜ pending |
| KICK-15 | sine table 2049 len, guard t[2048]==t[0] | unit | `make test` (static assert + runtime equality) | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/mock_host.c` / `.h` — mock `host_api_v1_t` (FNDTN-06)
- [ ] `tests/malloc_trap.c` — interposition + `g_audio_thread_active` flag (FNDTN-03 / D-13)
- [ ] `tests/wav.c` / `.h` — 44-byte PCM WAV writer
- [ ] `tests/test_render.c` — lifecycle + 512-block render + assertions (FNDTN-01/06/07, KICK-01/02/12/15)
- [ ] `scripts/glibc_gate.sh` — objdump GLIBC/libmvec/export gate (FNDTN-04)
- [ ] `Makefile` — `dsp.so` (aarch64) + `test` (native, `-DOMEGA_MALLOC_TRAP`) + `clean` + `deploy`
- [ ] `.github/workflows/ci.yml` — docker cross-build + gate + native test (D-11)
- [ ] Framework install: none — plain C, uses system `cc`

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Same `dsp.so` loads + sounds in 3 hosts | SC1 / D-14 | Requires Move hardware + host UIs | Deploy via `scripts/deploy.sh`; trigger + listen in Schwung slot, DR32 pad, Movy track |
| `ui_hierarchy` buf_len logged per host | SC5 / D-10 | On-device host-supplied buffer size | Inspect `/data/UserData/schwung/debug.log` after first `get_param("ui_hierarchy")` in each host |
| FM2 kick sounds correct | SC2 | Subjective audio quality | Listen to `tests/output/fm2_kick.wav` offline + on-device trigger |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s (per-task)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
