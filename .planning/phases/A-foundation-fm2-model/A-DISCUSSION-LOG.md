# Phase A: Foundation + FM2 Model - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-09-28
**Phase:** A-foundation-fm2-model
**Areas discussed:** Source layout, FM2 completeness, UI stubs vs real hierarchy, CI and on-device testing

---

## Source Layout

| Option | Description | Selected |
|--------|-------------|----------|
| Modular split now | dsp.c + models/fm2.c + models/model_registry.c + dsp_primitives.c + ui.c from day one | ✓ |
| Single file to start | Everything in dsp.c; refactor to multi-file after Phase A | |
| Flat multi-file, no subdirs | dsp.c + fm2.c + primitives.c at top level, no models/ subdir | |

**User's choice:** Modular split now  
**Notes:** Clean separation from day one so Phase B is purely additive (just add models/fm4.c, etc.)

---

| Option | Description | Selected |
|--------|-------------|----------|
| dsp_primitives.c + dsp_primitives.h | One file for all shared DSP building blocks | ✓ |
| Separate files per primitive type | oscillator.c, envelope.c, filter.c etc. | |
| Inline in dsp.c for now | Keep primitives in dsp.c, extract later | |

**User's choice:** dsp_primitives.c + dsp_primitives.h  
**Notes:** Simple and flat; all models include the same header

---

| Option | Description | Selected |
|--------|-------------|----------|
| models/model_registry.c | Registry array lives next to the models | ✓ |
| In dsp.c | Registry array is small in Phase A (1 model) | |

**User's choice:** models/model_registry.c  
**Notes:** dsp.c stays thin — plugin glue only

---

## FM2 Completeness

| Option | Description | Selected |
|--------|-------------|----------|
| Full FM2 as specced | All 8 Kick Page 1 params + 3 FM2 Page 2 params fully wired | ✓ |
| Minimal viable FM2 | Trigger produces sound, PITCH and LENGTH only | |

**User's choice:** Full FM2 as specced  
**Notes:** Phase B is purely additive with no revisiting Phase A code

---

| Option | Description | Selected |
|--------|-------------|----------|
| Wavetable sine lookup | Read from shared 2048+1-sample static const sine table in .rodata | ✓ |
| sinf() per sample | Call sinf() directly; replace later if needed | |

**User's choice:** Wavetable sine lookup  
**Notes:** Faster on A53, no denormals, reusable across all 10 models

---

| Option | Description | Selected |
|--------|-------------|----------|
| Shared ADSR/decay primitive in dsp_primitives.c | Reusable one-pole exponential decay struct | ✓ |
| Inline per envelope in fm2.c | Each FM2 envelope hand-rolled inline | |

**User's choice:** Shared ADSR/decay primitive  
**Notes:** Pays off immediately in Phase B; avoids extract-under-pressure scenario

---

## UI Stubs vs Real Hierarchy

| Option | Description | Selected |
|--------|-------------|----------|
| Real minimal hierarchy | Actual ui_hierarchy JSON with Kick Page 1 + FM2 Kick Page 2, real param keys | ✓ |
| Placeholder stub + buf_len spike | Minimal stub JSON; real hierarchy in Phase E | |

**User's choice:** Real minimal hierarchy  
**Notes:** Phase E adds nav tree and remaining pages without touching Phase A pages

---

| Option | Description | Selected |
|--------|-------------|----------|
| Static C string in ui.c | Pre-serialized static string fragment; zero allocation in get_param | ✓ |
| Build-time code generation | Python/C script generates ui_hierarchy.h from declarative spec | |

**User's choice:** Static C string in ui.c  
**Notes:** Simple; full code-gen approach can be added in Phase E if the string gets unwieldy

---

## CI and On-Device Testing

| Option | Description | Selected |
|--------|-------------|----------|
| Yes — Move hardware available | On-device deploy and test in Phase A | ✓ |
| No — hardware comes later | Offline CI only; on-device deferred | |

**User's choice:** Move hardware available  
**Notes:** Success criteria 1 and 5 (loads in all 3 hosts, buf_len measurement) are fully verifiable

---

| Option | Description | Selected |
|--------|-------------|----------|
| GitHub Actions | Automated Docker build + objdump gate + native test run on push | ✓ |
| Local scripts only | scripts/build.sh + scripts/test.sh + scripts/deploy.sh; no cloud CI | |

**User's choice:** GitHub Actions  
**Notes:** Enforces FNDTN-04 CI gate mechanically; requires GitHub repo

---

| Option | Description | Selected |
|--------|-------------|----------|
| Render-to-WAV + assertions + malloc trap | WAV render + isfinite + magnitude check + zero-malloc during render_block | ✓ |
| Render-to-WAV only | WAV render + isfinite; malloc trap added later | |

**User's choice:** Full harness with malloc trap  
**Notes:** Catches allocation bugs immediately; the interposition pattern is straightforward in C

---

## Claude's Discretion

- FX TYPE/AMT wiring in Phase A (passthrough identity, correct param keys)
- `env_t` coefficient formula (time-constant-based vs. sample-count-based)
- Makefile structure and target naming

## Deferred Ideas

None — discussion stayed within phase scope.
