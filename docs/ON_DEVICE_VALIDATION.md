# On-Device Validation — FM2 Foundation (SC1, SC5)

**Status:** PENDING (on-device) — build/deploy/verify runbook to be completed at the Move device.
**Requirements:** FNDTN-01 (loads in 3 hosts), FNDTN-04 (glibc gate), SC1 (same `dsp.so` sounds in 3 hosts), SC5 / D-10 (per-host `ui_hierarchy` buf_len).

This document is the runbook + results record for Phase A's headline on-device validation. The Phase A execution host (macOS, no Docker, no device link) could not perform the cross-build, the deploy, or the device SSH. The authoritative cross-build + glibc gate runs in GitHub Actions CI (D-11); the on-device load/listen is a human step at the Move. All hardware-dependent fields below are marked `PENDING (on-device)`.

---

## Prerequisite: a gate-passing aarch64 `dsp.so`

The `dsp.so` **cannot** be cross-built on macOS (no Docker, no `aarch64-linux-gnu-gcc`). Two authoritative paths produce it:

### Path A — GitHub Actions CI (authoritative, D-11)
CI (`.github/workflows/ci.yml`) runs the cross-build inside `ghcr.io/charlesvestal/schwung-builder:latest` and the glibc gate as a **blocking** job (flipped to `continue-on-error: false` in A-03). A green CI run on the current commit means `build/dsp.so` passed:
- GLIBC ≤ 2.35 (no `symbol@GLIBC_2.36+`)
- no libmvec `_ZGV*` leakage
- exactly one `move_plugin_init_v2` export

Download the CI build artifact (or rebuild locally on any Docker-capable Linux/macoS host, below) to get the `dsp.so` for deploy.

### Path B — local Docker build (any host with Docker)
```bash
# 1. Ensure the pinned builder image is present:
docker image inspect ghcr.io/charlesvestal/schwung-builder:latest >/dev/null 2>&1 \
  || docker pull ghcr.io/charlesvestal/schwung-builder:latest

# 2. Cross-build:
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/charlesvestal/schwung-builder:latest make dsp.so

# 3. Run the glibc/libmvec/export gate (must exit 0):
./scripts/glibc_gate.sh build/dsp.so
```
Expected gate output: `glibc gate OK (<=2.35)`, `libmvec gate OK`, `export gate OK (exactly one move_plugin_init_v2)`, `glibc_gate: ALL CHECKS PASSED`.

**Gate result:** `PENDING (on-device / CI)` — record the CI run URL or local gate output here.

---

## Deploy (D-14, atomic scp + rename)

```bash
# Override device host/dir if yours differs (defaults from scripts/deploy.sh):
export OMEGA_DEVICE_HOST="${OMEGA_DEVICE_HOST:-ableton@move.local}"
export OMEGA_DEVICE_DIR="${OMEGA_DEVICE_DIR:-/data/UserData/schwung/modules/omega}"

# Uploads build/dsp.so -> dsp.so.new, then atomic mv to dsp.so on-device:
./scripts/deploy.sh

# Also ensure module.json is on-device alongside dsp.so (one-time, same dir):
scp module.json "$OMEGA_DEVICE_HOST:$OMEGA_DEVICE_DIR/module.json"
```

**Deploy result:** `PENDING (on-device)`
**Deployed dsp.so identity (record for SC1 "same binary" assertion):**
- git commit: `PENDING (on-device)`
- sha256 (`shasum -a 256 build/dsp.so`): `PENDING (on-device)`

---

## Prep the buf_len capture (D-10 / SC5)

The D-10 spike logs `ui_buflen=<n>` to the device log on the **first** `get_param("ui_hierarchy")` per instance. Clear the log before verifying so each host's first-load line is unambiguous:

```bash
ssh "$OMEGA_DEVICE_HOST" ': > /data/UserData/schwung/debug.log' 2>/dev/null || true
# NOTE: if the log path differs on your device, record the actual path here:
```
**Log path used:** `/data/UserData/schwung/debug.log` (confirm on-device; note if different: `PENDING`)

---

## Human verification — load + listen in all 3 hosts

Use the **identical, unmodified** `dsp.so` for all three hosts (no rebuild between hosts — this is SC1's core assertion). For each host, in this order:

1. Load Omega into the slot/pad/track. Confirm it loads with no error (module appears, no crash).
2. Trigger a hit (pad hit or note-on, velocity > 0). Confirm you HEAR an FM2 kick.
3. Sweep a couple of Kick Page 1 encoders (e.g. PITCH, LENGTH) and confirm the sound changes audibly.
4. Record loads / audible below.

### Results table (SC1)

| Host context           | Loads (yes/no) | Audible FM2 kick (yes/no) | Params change sound (yes/no) | ui_buflen | Notes |
|------------------------|----------------|---------------------------|------------------------------|-----------|-------|
| Schwung instrument slot | PENDING        | PENDING                   | PENDING                      | PENDING (ui_buflen=?) | PENDING (on-device) |
| DR32 pad slot           | PENDING        | PENDING                   | PENDING                      | PENDING (ui_buflen=?) | PENDING (on-device) |
| Movy track              | PENDING        | PENDING                   | PENDING                      | PENDING (ui_buflen=?) | PENDING (on-device) |

> Table columns `loads` / `audible` / `buf_len` are the SC1 + SC5 record. `ui_buflen` values captured from the device log (below). If a host shows no `ui_buflen` line, note "not logged" (the host may cache the hierarchy and not re-query per load).

---

## Capture the per-host buf_len (SC5 / D-10)

After loading all three hosts, fetch the log and read the captured values:

```bash
ssh "$OMEGA_DEVICE_HOST" 'cat /data/UserData/schwung/debug.log' | grep ui_buflen
```

Expected line format (one per host that queried the hierarchy): `[host] ui_buflen=<n>`
(native harness measured `ui_buflen=4096` as the reference baseline — A-03.)

**Raw captured lines:**
```
PENDING (on-device) — paste the grep output here
```

Transcribe each host's `ui_buflen=<n>` into the results table above. These values size Phase E's full nav-tree hierarchy.

---

## Same-binary confirmation (SC1)

- [ ] `PENDING` — Confirm the identical `dsp.so` (same sha256 / commit above) was used in all three hosts, with NO rebuild between hosts.

---

## Sign-off

| Criterion | Status |
|-----------|--------|
| SC1 — same `dsp.so` loads + sounds in Schwung slot, DR32 pad, Movy track | PENDING (on-device) |
| SC5 / D-10 — per-host `ui_buflen` captured | PENDING (on-device) |
| FNDTN-04 — glibc gate green (CI or local) | PENDING (on-device / CI) |
| D-15 — SoC identified (see docs/SOC_IDENTIFICATION.md) | PENDING (on-device) |

---

*Doc created during A-04 (on-device validation). The build (Docker cross-compile), deploy (scp to Move), and device SSH could not run on the Phase A execution host (macOS, no Docker, no device connection). All hardware-dependent fields are `PENDING (on-device)`; the runbook above is exact and ready to complete at the device.*
