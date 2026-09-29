---
phase: quick-260929-cjj
plan: 01
subsystem: foundation-abi-ui
tags: [abi, ui_hierarchy, manifest, crash-fix, phase-a]
requires:
  - Context/01_SCHWUNG_DEV_ARCHITECTURE.md (authoritative host/plugin ABI + JSON schemas)
provides:
  - Correct host_api_v1_t / plugin_api_v2_t ABI matching Context/01 byte-for-byte
  - module.json with nested capabilities block per manifest schema
  - omega_build_ui emitting the real levels-based ui_hierarchy
affects:
  - src/dsp.c (g_host->log now lands on the correct slot; no code change needed)
  - A-04 on-device validation (unblocked: crash root cause removed)
tech-stack:
  added: []
  patterns:
    - "ABI structs taken verbatim from Context/01, not invented"
    - "Levels-based ui_hierarchy assembled from static .rodata fragments, zero-alloc"
key-files:
  created: []
  modified:
    - src/omega.h
    - tests/mock_host.c
    - module.json
    - src/ui.c
    - tests/test_render.c
decisions:
  - "FM2 Page-2 params inlined directly into ui.c (FM2 is the only Phase A model) rather than splicing fm2.c's p2_slot_desc — simpler, and the schema shape changed anyway. fm2.c untouched; its p2_slot_desc vtable field stays populated so it keeps compiling."
metrics:
  duration: 3min
  completed: 2026-09-29
---

# Quick 260929-cjj: Fix Phase A On-Device Load Failure Summary

Corrected the host/plugin ABI struct layout, module manifest, and ui_hierarchy JSON to match `Context/01_SCHWUNG_DEV_ARCHITECTURE.md` verbatim — removing the on-device segfault where `g_host->log` landed on the host's `mapped_memory` data pointer during the first `get_param("ui_hierarchy")`.

## What Was Done

### Task 1 — Fix host/plugin ABI struct layout (the crash fix)
- Added the three Memory-Mapped Direct Access fields (`uint8_t *mapped_memory`, `int audio_out_offset`, `int audio_in_offset`) between `frames_per_block` and `log` in `host_api_v1_t`, matching Context/01 lines 30-50. This was the root cause: omitting them shifted `log` onto the host's `mapped_memory` offset, so the D-10 spike's `g_host->log()` call jumped through a data pointer → segfault → "nothing loads in".
- Changed `api_version` to `uint32_t` in both `host_api_v1_t` and `plugin_api_v2_t` (Context/01 lines 31, 53). Existing `= 1` / `= 2` int-literal assignments compile cleanly.
- Initialized the three new fields explicitly in `tests/mock_host.c make_mock_host()` (NULL / 0 / 0).
- `tests/mock_host.h` needed no change (declares only `make_mock_host()`; includes omega.h).
- `src/dsp.c` needed no change (`g_api.api_version = 2` assigns cleanly to `uint32_t`).
- Commit: `ce99134`

### Task 2 — Rewrite module.json to the Context/01 manifest schema
- Replaced the flat top-level `component_type` / `pad_layout` keys with a nested `capabilities` block (`chainable`, `component_type`, `audio_out`, `midi_in`, `pad_layout`).
- Added `abbrev` (`OMGA`), `author`, `description` per the spec values.
- Commit: `f10d3b5`

### Task 3 — Rewrite omega_build_ui to the real levels-based ui_hierarchy
- Replaced the invented `{"pages":[{"slots":[...]}]}` JSON with the host's real schema: top-level `pad_layout` + `child_index_param` + a `levels` map (`root`, `kick1`, `kick2`).
- `root`: Model enum + Volume float knobs, plus `kick1`/`kick2` sub-page links.
- `kick1`: the 8 Page-1 params (PITCH..COLOR) as 0..1 floats.
- `kick2`: the 5 FM2 Page-2 params (FM RATIO, FM INDEX, OP2 WAVE, FX TYPE, FX AMT) inlined.
- Preserved the zero-alloc mechanism exactly: bounded `ui_append`, `buf[off] = '\0'` null-termination, bytes-written return (get_param contract). Only the emitted JSON changed. All existing `PK_*` key strings retained so set_param/get_param dispatch still matches.
- Updated `tests/test_render.c` ui_hierarchy assertions to the levels schema (added `"levels"` and `fx_type`/`fx_amt` checks); kept every get_param contract check (bytes-written in-bounds, null-termination, canary/truncation safety, unknown-key → -1) unchanged.
- fm2.c untouched; `fm2_p2_slot_desc` stays assigned to `g_fm2_vtable.p2_slot_desc` so fm2.c keeps compiling with no `-Wunused` breakage — ui.c simply stops calling it.
- Commit: `5f799a7`

## Deviations from Plan

None - plan executed exactly as written.

## Verification

- `make test` printed `ALL TESTS PASSED` (both test_fm2 and test_render) after every task.
- The D-10 `[host] ui_buflen=4096` log line still fires — confirming `g_host->log` now lands on the correct slot (had it still been mislaid, the native harness would print but on-device would fault; the struct now matches the doc).
- `grep` confirmations: `mapped_memory` present between `frames_per_block` and `log`; `uint32_t api_version` count = 2; `module.json` nested-capabilities valid JSON with `component_type` gone from top level; `levels` + `child_index_param` present in ui.c; zero `"pages"` in ui.c; `levels` assertion present in test.
- The D-10 buf_len spike is unchanged (still one-shot, flag-guarded via `inst->logged_buflen`, still carrying its `SPIKE (D-10)` removal comment).
- No Makefile / CI / `.github/workflows` edits.

## Known Stubs

- `PK_FX_TYPE` / `PK_FX_AMT` remain placeholder params in kick2 (the 5 real FX modes are KICK-14 / Phase B). They are exposed with keys wired to `fm2_set_param` (identity passthrough today) and documented in fm2.c with a `TODO(Phase B)` comment. Intentional — the keys must exist now so the host UI and dispatch are stable; Phase B implements the FX behavior.

## Self-Check: PASSED

- Files: src/omega.h, module.json, src/ui.c, tests/mock_host.c, tests/test_render.c — all FOUND.
- Commits: ce99134, f10d3b5, 5f799a7 — all FOUND.
