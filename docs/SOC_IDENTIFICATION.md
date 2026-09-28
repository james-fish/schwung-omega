# Move SoC / Cortex Core Identification (D-15)

**Status:** PENDING (on-device) — this is a runbook to be completed at the Move device.
**Requirement:** D-15 — identify the exact Move SoC / Cortex core to decide whether `-mcpu=cortex-a53` is safe to add.
**Decision rule (locked, A-CONTEXT D-15 / A-RESEARCH Open Q2):** Do NOT bake in `-mcpu` in Phase A. Ship baseline ARMv8-A until the core is confirmed on-device — a wrong `-march`/`-mcpu` risks `SIGILL` on the device.

---

## Why this could not be filled automatically

This identification requires reading `/proc/cpuinfo` on the Move device over SSH. The execution host for Phase A (macOS, no Docker, no device connection) cannot reach the device. The commands below are exact and ready to run once you have SSH access to the Move.

The device host/user default comes from `scripts/deploy.sh`:
`OMEGA_DEVICE_HOST` (default `ableton@move.local`).

---

## Runbook — run at the device

```bash
# Use the same host var as deploy.sh (override if your device differs):
export OMEGA_DEVICE_HOST="${OMEGA_DEVICE_HOST:-ableton@move.local}"

# Read the CPU identity fields:
ssh "$OMEGA_DEVICE_HOST" 'grep -E "CPU part|CPU implementer|CPU architecture|Features|model name|Hardware" /proc/cpuinfo'
```

### How to decode the result

| `CPU part` value | Core | `-mcpu` if confirmed |
|------------------|------|----------------------|
| `0xd03` | Cortex-A53 | `-mcpu=cortex-a53` is safe to add later |
| `0xd04` | Cortex-A35 | use `-mcpu=cortex-a35` |
| `0xd05` | Cortex-A55 | use `-mcpu=cortex-a55` |
| other | decode via ARM part-number table | keep baseline ARMv8-A |

`CPU implementer : 0x41` = ARM Ltd. The i.MX8M family (strongly suspected for Move) is quad Cortex-A53 → expect `0xd03`.

---

## Recorded Result (fill on-device)

**Raw `/proc/cpuinfo` excerpt:**

```
PENDING (on-device) — paste the full grep output here
```

**Decoded core name:** `PENDING (on-device)`
**SoC (if identifiable from `Hardware`/model line):** `PENDING (on-device)`

---

## -mcpu Recommendation

- **Confirmed core:** `PENDING (on-device)`
- **Recommendation:** If `CPU part : 0xd03` (Cortex-A53) is confirmed, `-mcpu=cortex-a53` is safe to add in a **future** build task (it enables A53-tuned scheduling with no new instructions beyond the baseline the device already runs).
- **Phase A stance (LOCKED):** `-mcpu` is **NOT baked into Phase A**. The Makefile ships baseline ARMv8-A (no `-mcpu`/`-march` override) per D-15 and A-RESEARCH Open Q2. This avoids `SIGILL` on an unconfirmed core. Adding the flag is deferred until this doc records a confirmed core.

---

*Doc created during A-04 (on-device validation). Hardware-dependent fields left as `PENDING (on-device)` because the Phase A execution host had no device connection.*
