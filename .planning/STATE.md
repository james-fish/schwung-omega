---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
stopped_at: Completed A-03-ui-hierarchy-and-buflen-spike-PLAN.md
last_updated: "2026-09-28T22:39:52.000Z"
progress:
  total_phases: 7
  completed_phases: 0
  total_plans: 4
  completed_plans: 3
  percent: 0
---

# Project State: Omega

**Last updated:** 2026-09-28

---

## Project Reference

**Core value:** A techno producer on Ableton Move should be able to dial in a full Bohm-style kick + rumble + performer system from one module, with immediate access to the most expressive performance controls on the root page.

**What it is:** A native C Schwung module for Ableton Move — a multi-engine kick synthesizer (10 models) + 4-tap groove rumble generator + live performer mixer, in a single `dsp.so` loadable in Schwung slots, DR32 pads, and Movy tracks.

**Current focus:** Phase A — Foundation + FM2 Model

---

## Current Position

Phase: A (Foundation + FM2 Model) — EXECUTING
Plan: 4 of 4 (A-01, A-02, A-03 complete)
**Phase:** A — Foundation + FM2 Model
**Plan:** A-04 (on-device validation) — next
**Status:** Executing Phase A
**Progress:** Phase 0 of 7 complete; Plan 3 of 4 in Phase A complete

```
[◐○○○○○○] 0/7 phases (A: 3/4 plans)
```

---

## Performance Metrics

*(Populated as phases complete)*

| Metric | Value |
|--------|-------|
| Phases complete | 0/7 |
| Requirements delivered | 11/42 (FNDTN-01/02/03/04/05/06/07, KICK-01/02/12/15) |
| On-device CPU (full chain) | Not yet measured (Phase D target: 10-15%) |

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase A-foundation-fm2-model P01 | 6min | 3 tasks | 17 files |
| Phase A-foundation-fm2-model P02 | 6min | 3 tasks | 8 files |
| Phase A-foundation-fm2-model P03 | 3min | 3 tasks | 6 files |
| Phase A-foundation-fm2-model P03 | 3min | 3 tasks | 6 files |

## Accumulated Context

### Key Decisions

- **Single module** (not split kick + rumble) — inter-pad routing may not be supported in DR32/Movy; single module is certain to work
- **Hybrid DSP fidelity** — accurate FM/wavetable/transient engines; modal damped resonator for PHY; TPT SVF instead of ZDF Moog ladder (~3-5% CPU saving)
- **Synthesis-method model IDs** (FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN) — IP avoidance + user clarity
- **Root page = 8 bidirectional performance macros** — Move's 8 encoders are prime real estate; no dedicated nav page
- **Float-only internal signal path** — int16→float once at input, float→int16 once at output; never round-trip through int16 mid-chain
- **vtable dispatch** per model (trigger, render, set_p2, p2_slot_desc); registry array indexed by model_id_t; no giant switch
- **Single instance struct, one calloc** (~730-780 KB, dominated by 705 KB groove delay buffer); wavetables in `.rodata`, shared across instances
- **[A-01] Sine table literals use `%.9e`** — `%.9g` printed bare `0`/`1`/`-1` which with an `f` suffix are invalid C float literals; `%.9e` guarantees a decimal point + exponent
- **[A-01] `omega_to_i16` int16 boundary lives in `dsp_primitives.h`** — shared by the test harness (Wave 0) and A-02's `render_block`, single clamp+isfinite+lrintf point
- **[A-01] Malloc trap compiled out on Darwin** — `__libc_*` interposition is Linux-only; the Makefile drops `-DOMEGA_MALLOC_TRAP` on macOS, so Linux CI is the authoritative FNDTN-03 gate
- **[A-01] No `-mcpu` pinning** — per D-15, the Cortex core flag is deferred to on-device `/proc/cpuinfo` confirmation (A-04); baseline ARMv8-A only
- **[A-02] FM2 output scaled 0.6 body / 0.4 click** — the summed carrier+click can exceed 1.0; the engine self-limits so the float output stays within [-1,1] before int16 conversion (FNDTN-07/D-12), rather than relying on the clamp to mask overflow
- **[A-02] `omega_build_ui` stub behind `#ifndef OMEGA_HAS_UI`** — dsp.c ships a minimal `{"pages":[]}` so A-02 links standalone; A-03's ui.c defines OMEGA_HAS_UI and owns the real `ui_hierarchy` (D-08) + the D-10 buf_len log
- **[A-03] Kick Page 2 spliced from the model** — ui.c drops the outer `[` `]` of the active model's `p2_slot_desc` array and injects the interior into its own `"slots":[...]`, then appends FX TYPE/AMT; the model owns its slot list, ui.c owns page structure (clean for Phase B models)
- **[A-03] `omega_build_ui` returns bytes-written excluding terminator** — bounded `ui_append` copies `min(len, remaining)` reserving the terminator byte so it never overruns `buf_len` and always null-terminates (Pitfall 3/4); matches `fm2_p2_slot_desc` convention
- **[A-03] D-10 buf_len log stays in dsp.c, formatted locale-independently** — dsp.c owns `g_host`; `omega_itoa_msg` uses manual digit extraction (no `snprintf`/`atof`); one-shot flag-guarded, `SPIKE (D-10)` comment schedules removal/gating before ship; native harness measured `ui_buflen=4096`
- **[A-03] CI cross-build + glibc gate flipped to blocking** — `continue-on-error: false` now that dsp.c/ui.c/registry/fm2.c all exist; `make dsp.so` + glibc/libmvec/export gate must pass

### Todos / Watchpoints

- **Five reference-code bugs to NOT carry forward**: hardcoded 120 BPM tap interval; unbounded asymmetric fast_tanh; amplitude-threshold ducking; missing int16 clamp/isfinite; zero-margin scratch buffer
- Set FPCR flush-to-zero bit explicitly in `render_block` (per-thread, not inherited)
- Use granular fast-math subset, NOT blanket `-ffast-math` (avoids libmvec symbols)
- Add `MAX_BLOCK` margin to scratch buffers

### Open Questions (from research)

| Question | Resolve in Phase |
|----------|------------------|
| What `buf_len` does each host pass to `get_param("ui_hierarchy")`? | A (spike) → E |
| Are `set_param`/`get_param` guaranteed single-threaded? | A |
| Move per-process RAM ceiling (groove buffer sizing)? | C |
| Exact Move SoC / Cortex core (build `-mcpu` flag)? | A |
| Does Schwung re-query `ui_hierarchy` on model change or cache? | E |
| Host state-blob byte cap for Movy/Schwung slots? | G |

### Blockers

None.

---

## Session Continuity

**Next action:** Execute Plan A-04 (on-device validation) — deploy `dsp.so` to Move, load in all 3 host contexts (Schwung slot, DR32 pad, Movy track), read the `[host] ui_buflen=<v>` log per host (resolves the buf_len open question, unblocks Phase E sizing), read `/proc/cpuinfo` for the `-mcpu` decision (D-15), confirm FM2 sounds on-device.

**Stopped at:** Completed A-03-ui-hierarchy-and-buflen-spike-PLAN.md

**Recent activity:**

- 2026-09-29: A-03 complete — real `ui_hierarchy` (ui.c, D-08/D-09): static Page 1 (8 keyed slots) + dynamic FM2 Page 2 spliced from `p2_slot_desc` + FX TYPE/AMT; dsp.c wired to ui.c (fallback removed), D-10 one-shot locale-independent buf_len log; CI cross-build flipped to blocking; harness proves the get_param contract; `make test` green (commits 98e62a0, 3ac9e54, e8802b7)
- 2026-09-29: A-02 complete — FM2 engine (fm2.c), plugin entry points (dsp.c), model registry, module.json; real move_plugin_init_v2 lifecycle harness; `make test` green (commits bccf4e9, c4d028c, 3cac29b)
- 2026-09-29: A-01 complete — shared contracts (omega.h), .rodata sine table, dsp_primitives, full offline test harness + glibc gate + CI; `make test` green (commits 205e0c4, 0ac3a38, 07a33ed)
- 2026-09-28: PROJECT.md, REQUIREMENTS.md (42 v1 reqs), research/SUMMARY.md created
- 2026-09-28: ROADMAP.md created — 7 phases (A-G), 42/42 requirements mapped, coverage validated

---
*State initialized: 2026-09-28*
