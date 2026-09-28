---
phase: A-foundation-fm2-model
plan: 04
subsystem: on-device-validation
tags: [on-device, runbook, glibc-gate, deploy, buflen, soc-id, checkpoint, human-verify]

# Dependency graph
requires:
  - "A-02: dsp.so entry points + module.json (the artifact to deploy)"
  - "A-03: ui.c real ui_hierarchy + D-10 one-shot buf_len log; CI cross-build + glibc gate flipped to blocking"
  - "A-01: scripts/deploy.sh (atomic scp+rename, D-14); scripts/glibc_gate.sh (GLIBC/libmvec/export gate)"
provides:
  - "docs/SOC_IDENTIFICATION.md: /proc/cpuinfo runbook + CPU-part decode table; -mcpu recommendation deferred, explicitly NOT baked into Phase A (D-15)"
  - "docs/ON_DEVICE_VALIDATION.md: end-to-end build->deploy->3-host verify runbook; results table (loads/audible/buf_len per host) ready to fill; buf_len capture steps (SC1/SC5/D-10)"
affects: [phase-E-full-nav-tree, future-mcpu-build-flag]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "hardware-dependent validation captured as executable runbooks with PENDING (on-device) fields, not fabricated results — the human completes them at the Move device"
    - "authoritative aarch64 cross-build + glibc gate lives in GitHub Actions CI (D-11), since macOS host has no Docker / no aarch64-linux-gnu-gcc"

key-files:
  created:
    - docs/SOC_IDENTIFICATION.md
    - docs/ON_DEVICE_VALIDATION.md
  modified: []

key-decisions:
  - "Cross-build + deploy + device SSH cannot run on the Phase A execution host (macOS: no Docker, no aarch64 cross-gcc, no device link — all confirmed by probing). CI is the authoritative build/gate; the docs are runbooks the human completes on-device."
  - "-mcpu remains NOT baked into Phase A (D-15). SOC_IDENTIFICATION.md records the /proc/cpuinfo runbook + decode table and the deferred recommendation; baseline ARMv8-A ships until a core is confirmed on-device (avoids SIGILL)."
  - "Task 3 (human-verify checkpoint) STOPS here: the 3-host load/listen and buf_len capture require physical hardware + a human operator. On-device success criteria (SC1, SC5, D-15 SoC) are NOT marked verified."

requirements-completed: []
requirements-pending-on-device: [FNDTN-01, FNDTN-04]

# Metrics
duration: 2min
completed: 2026-09-29
---

# Phase A Plan 04: On-Device Validation Summary

**Automatable scaffolding for the on-device foundation validation: two executable runbook docs (SoC identification via `/proc/cpuinfo`, and the end-to-end build->deploy->3-host verify with a per-host loads/audible/buf_len results table) with every hardware-dependent field marked `PENDING (on-device)`. The build (Docker cross-compile), deploy (scp to Move), and device SSH could not run on the macOS execution host — CI is the authoritative build gate, and the human completes the runbooks at the device. Execution STOPS at the Task 3 human-verify checkpoint; SC1, SC5, and D-15 SoC ID are left unverified pending hardware.**

## Performance

- **Duration:** ~2 min
- **Tasks:** 2 of 3 automatable tasks completed; Task 3 is a blocking human-verify checkpoint (STOP)
- **Files:** 2 created, 0 modified

## Accomplishments

- **`docs/SOC_IDENTIFICATION.md`** (D-15): exact `ssh $OMEGA_DEVICE_HOST 'grep ... /proc/cpuinfo'` runbook (host var sourced from `deploy.sh`), a `CPU part` -> core decode table (`0xd03` = Cortex-A53), a PENDING result block, and a LOCKED recommendation — `-mcpu` is NOT baked into Phase A; baseline ARMv8-A ships until the core is confirmed on-device (avoids `SIGILL`, per A-RESEARCH Open Q2).
- **`docs/ON_DEVICE_VALIDATION.md`** (SC1/SC5/FNDTN-01/FNDTN-04/D-10/D-14): the full pipeline runbook — how to obtain a gate-passing `dsp.so` (CI artifact or local Docker build + `glibc_gate.sh`), the `deploy.sh` atomic scp+rename steps, log-clear prep, a 3-host results table (Schwung slot / DR32 pad / Movy track x loads/audible/params/ui_buflen), the `grep ui_buflen` capture command, and a same-binary (sha256/commit) confirmation for SC1. All device-observed fields are `PENDING (on-device)`.
- **Toolchain probe (factual):** confirmed on this host `docker ABSENT`, `aarch64-linux-gnu-gcc ABSENT`, `ssh present` but no device reachable — recorded rather than assumed, so the "cannot build/deploy here" claim is verified, not fabricated.

## Task Commits

1. **Task 1 (Cross-build, gate, deploy):** NOT PERFORMED on this host — Docker + aarch64 cross-gcc absent (confirmed by probe); no local artifact producible. Documented in `docs/ON_DEVICE_VALIDATION.md` as the CI-authoritative build path + `deploy.sh` runbook. No commit (no artifact).
2. **Task 2 (SoC-ID + buf_len prep docs):** `58362f8` (docs) — both runbook docs created.
3. **Task 3 (human-verify 3-host + buf_len):** CHECKPOINT — STOP, awaiting on-device human verification.
4. **Docs metadata (SUMMARY + STATE + ROADMAP):** see final commit.

## Deviations from Plan

### Environment-forced adjustments (not scope changes)

**1. [Rule 3 - Blocking] Task 1 cross-build + deploy cannot run on the execution host**
- **Found during:** Task 1 setup.
- **Issue:** Plan Task 1 calls `docker run ... make dsp.so`, `glibc_gate.sh`, and `deploy.sh`. The macOS execution host has no Docker, no `aarch64-linux-gnu-gcc`, and no connection to Move hardware (confirmed by probing `command -v`). This matches the known Phase A constraint (D-11: CI is the authoritative cross-build; A-01 decision "cross-build cannot run locally").
- **Resolution:** Documented the CI-authoritative build path and a local-Docker fallback inside `docs/ON_DEVICE_VALIDATION.md`, plus the exact `deploy.sh` runbook. Did NOT fabricate a `build/dsp.so`, gate output, or deploy result. The plan's own Task 1 fallback ("If the device is unreachable, record the failure clearly and pause") is honored.
- **Files:** docs/ON_DEVICE_VALIDATION.md
- **Commit:** 58362f8

**2. [Rule 3 - Blocking] Task 2 device SSH steps (cpuinfo read, log clear) deferred to runbook**
- **Found during:** Task 2.
- **Issue:** Task 2 steps 1 & 3 (`ssh <device> cat /proc/cpuinfo`, `ssh <device> ': > debug.log'`) require device access unavailable here.
- **Resolution:** Recorded as exact, ready-to-run runbook commands in the two docs; the doc-creation steps (2 & 4) that ARE automatable were completed. SoC/cpuinfo fields left `PENDING (on-device)`.
- **Files:** docs/SOC_IDENTIFICATION.md, docs/ON_DEVICE_VALIDATION.md
- **Commit:** 58362f8

## Checkpoint Status

**Task 3 is a blocking `checkpoint:human-verify`.** Execution STOPS here. The human must, at the Move device:
1. Obtain a gate-passing `dsp.so` (green CI run or local Docker build + `glibc_gate.sh`).
2. `./scripts/deploy.sh` (atomic scp + rename) + copy `module.json`.
3. Load + trigger + listen in all 3 hosts (Schwung slot, DR32 pad, Movy track) using the IDENTICAL binary.
4. `grep ui_buflen` the device log; transcribe results into `docs/ON_DEVICE_VALIDATION.md`.
5. `grep /proc/cpuinfo` and fill `docs/SOC_IDENTIFICATION.md`.

**On-device success criteria remain UNVERIFIED:** SC1 (3-host load+audio), SC5 (per-host buf_len), D-15 (SoC ID). These are NOT marked complete.

## Known Stubs

None new. (The FX TYPE/AMT UI placeholders and the D-10 buf_len spike log are pre-existing intentional items from A-02/A-03, documented in their SUMMARYs; A-04 adds no code.)

## Next Phase Readiness

- Phase A's foundation is code-complete and green in the native harness + (blocking) CI. The ONLY remaining Phase A gate is the on-device human verification captured by these runbooks.
- Once the human fills `docs/ON_DEVICE_VALIDATION.md` (SC1 + SC5) and `docs/SOC_IDENTIFICATION.md` (D-15), Phase A can proceed to `/gsd:verify-work`. The captured buf_len sizes Phase E's full nav tree; a confirmed Cortex-A53 unblocks an optional future `-mcpu=cortex-a53` build task.

## Self-Check: PASSED
