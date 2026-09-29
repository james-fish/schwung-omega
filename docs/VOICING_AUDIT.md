# Voicing Audit — All 10 Kick Models (D-B02 / D-B04)

**Status:** PENDING (on-device) — the manual per-model ear round has not run. Every matrix cell below is `PENDING (on-device)`.

**This document is the D-B04 phase-completion gate.** Phase B is **NOT complete** until every model PASSES every D-B02 manual voicing item on-device. The automated batteries (B-03..B-09: non-silent default, per-param responsiveness, bounded-at-extremes, pairwise distinctness, clean model-switch re-init, valid Page-2 JSON) prove each engine **functions** and is **distinct/bounded**. Only a human at the Move can confirm each engine sounds **musical** (D-B01: "Phase A proved the models function; Phase B must make them sound musical"). This is surfaced as a MANUAL sign-off, not an automated pass — no cell may be marked PASS without an on-device audition.

**Requirements:** KICK-13 (per-model Page 2 shows the active model's slots), and the D-B02 voicing checklist for all 10 models (FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN).

The Phase-B execution host (macOS, no Docker, no device link) cannot cross-build, deploy, or SSH the Move — identical to A-04. The authoritative cross-build + glibc gate runs in GitHub Actions CI (D-11); the on-device audition is a human step at the Move.

---

## Prerequisite: a gate-passing aarch64 `dsp.so`

The `dsp.so` **cannot** be cross-built on macOS (no Docker, no `aarch64-linux-gnu-gcc`). Two authoritative paths produce it (same as A-04):

### Path A — GitHub Actions CI (authoritative, D-11)
CI (`.github/workflows/ci.yml`) runs the cross-build inside `ghcr.io/charlesvestal/schwung-builder:latest` and the glibc gate as a **blocking** job. A green CI run on the current commit means `build/dsp.so` passed: GLIBC ≤ 2.35 (no `symbol@GLIBC_2.36+`), no libmvec `_ZGV*` leakage, exactly one `move_plugin_init_v2` export. Download the CI build artifact for deploy.

### Path B — local Docker build (any host with Docker)
```bash
# 1. Ensure the pinned builder image is present:
docker image inspect ghcr.io/charlesvestal/schwung-builder:latest >/dev/null 2>&1 \
  || docker pull ghcr.io/charlesvestal/schwung-builder:latest

# 2. Cross-build the module:
docker run --rm -v "$PWD:/workspace" -w /workspace \
  ghcr.io/charlesvestal/schwung-builder:latest make dsp.so

# 3. Run the glibc/libmvec/export gate (must exit 0):
./scripts/glibc_gate.sh build/dsp.so
```
Expected gate output: `glibc gate OK (<=2.35)`, `libmvec gate OK (no _ZGV/libmvec symbols)`, `export gate OK (exactly one move_plugin_init_v2)`, `glibc_gate: ALL CHECKS PASSED`.

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
**Deployed dsp.so identity (record for reproducibility):**
- git commit: `PENDING (on-device)`
- sha256 (`shasum -a 256 build/dsp.so`): `PENDING (on-device)`

---

## Audition procedure (per model)

Load Omega in a Schwung instrument slot. For **EACH** of the 10 models, select it via the **root Model encoder** (the enum lists all 10 in order: FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN — selecting a model re-queries the per-model Kick Page 2, which now shows that model's own slots + FX TYPE/AMT), then:

1. **(a) Default preset** — trigger at default (12-o'clock) settings; confirm it sounds like a usable techno kick out of the box (no obvious tuning required to be listenable).
2. **(b) PITCH / CURVE (808↔909)** — sweep PITCH and CURVE across their range; confirm musical, not clicky/muddy, decay feels right.
3. **(c) Each knob's range** — sweep each of the model's Kick Page 2 knobs end-to-end; confirm a musically useful range (no dead zones, no all-the-action-in-the-last-5%).
4. **(d) Distinct character** — A/B this model back-to-back against the others; confirm a distinct sonic character (each model earns its slot).
5. **(e) No live artifacts** — turn knobs live during a sustained trigger; confirm no clipping, zipper noise, or artifacts.

**Sign off FM2 FIRST** — it is the D-B03 reference bar. Audition the other nine against it.

Record `PASS` or the specific issue in each cell of the matrix below.

**If a model fails an item:** re-map its param min/max or response curve in its model `.c` (D-B02 "re-map as needed"), rebuild (Path A/B above), redeploy (`./scripts/deploy.sh`), and re-audition. Iterate until PASS.

---

## Voicing matrix (10 models × 5 D-B02 manual items)

D-B02 manual checklist items (columns):
- **(a) Usable default** — default/12-o'clock preset sounds like a usable techno kick out of the box.
- **(b) PITCH/CURVE musical** — PITCH sweep / CURVE (808↔909 character) sounds musical, not clicky/muddy, decay feels right.
- **(c) Knobs useful range** — each Kick Page 2 knob sweeps a musically useful range end-to-end (no dead zones, no all-in-last-5%).
- **(d) Distinct character** — distinct sonic character vs the other nine models.
- **(e) No live artifacts** — no clipping/zipper/artifacts when turning knobs live during a sustained trigger.

| Model | (a) Usable default | (b) PITCH/CURVE musical | (c) Knobs useful range | (d) Distinct character | (e) No live artifacts |
|-------|--------------------|-------------------------|------------------------|------------------------|-----------------------|
| **FM2** *(reference bar — sign off first)* | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |
| **FM4** | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |
| **WTR** | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |
| **PHY** | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |
| **HRD** | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |
| **DIG** | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |
| **TRS** | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |
| **ANA** | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |
| **USR** | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |
| **GEN** | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) | PENDING (on-device) |

> Note on **USR**: with no `user/kick.wav` / `user/wavetable.raw` present the engine plays its built-in wavetable fallback (D-B02 non-silent). Audition the fallback; optionally place a user WAV in `module_dir/user/` and re-trigger to audition user content.
> Note on **GEN**: Phase-B scope is the self-clocking generative engine (SEED/SCALE/DENSITY). Transport-sync + the full Groove Page 2 UI land in Phase C (GRV-02/GRV-04); audition the self-clocked engine now.

---

## Sign-off

Phase B is complete only when **every cell above reads PASS**.

| Criterion | Status |
|-----------|--------|
| FM2 re-voiced to reference bar (D-B03) — all 5 items PASS | PENDING (on-device) |
| All 10 models PASS the 5 D-B02 manual items (D-B04) | PENDING (on-device) |
| FNDTN-04 — glibc gate green (CI or local) | PENDING (on-device / CI) |
| Per-model Kick Page 2 shows the active model's slots (KICK-13 SC3) | Automated: PASS (`make test`); on-device visual confirm PENDING |

---

*Doc created during B-09 (Page-2 splice + voicing audit). The cross-build (Docker), deploy (scp to Move), and device audition could not run on the Phase-B execution host (macOS, no Docker, no device connection). All voicing results are `PENDING (on-device)`; the runbook above is exact and ready to complete at the device. No PASS results are fabricated — the ear is the authority (D-B01/D-B02).*
