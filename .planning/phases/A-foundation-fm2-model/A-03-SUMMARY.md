---
phase: A-foundation-fm2-model
plan: 03
subsystem: ui-hierarchy
tags: [c11, ui-hierarchy, json-serialization, zero-alloc, get-param-contract, buflen-spike, ci-gate]

# Dependency graph
requires:
  - "A-01: src/omega.h (PK_* param keys, bohm_instance with logged_buflen flag, g_models[]); Makefile + glibc gate + CI scaffold"
  - "A-02: src/dsp.c (omega_get_param dispatch, temp omega_build_ui fallback behind #ifndef OMEGA_HAS_UI); src/models/fm2.c (fm2_p2_slot_desc emitting the 3 FM2 Page-2 slots as a JSON array); src/models/model_registry.c (g_models[MODEL_FM2])"
provides:
  - "src/ui.c: real minimal ui_hierarchy — static UI_PAGE1 (8 Bohm-keyed slots) + zero-alloc omega_build_ui that splices the active model's p2_slot_desc into Kick Page 2 with FX TYPE/AMT placeholders, bounded to buf_len, null-terminated, returns bytes written"
  - "src/dsp.c: get_param(ui_hierarchy) wired to the real omega_build_ui; temp fallback removed; D-10 one-shot flag-guarded buf_len log via locale-independent omega_itoa_msg"
  - "src/omega.h: OMEGA_HAS_UI defined + omega_build_ui declared (dsp.c drops its fallback)"
  - ".github/workflows/ci.yml: cross-build + glibc-gate job flipped to continue-on-error: false (blocking)"
  - "tests/test_render.c: ui_hierarchy contract assertions (correct keys, bytes-written, -1 for unknown, truncation canary)"
affects: [A-04-on-device, phase-E-full-nav-tree]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "pre-serialized static .rodata JSON fragments (UI_PAGE1/UI_FX_SLOTS/wrappers) assembled at runtime — no JSON library, zero allocation (D-09)"
    - "bounded ui_append helper: memcpy of min(len, remaining) reserving the terminator byte — never overruns buf_len (Pitfall 3)"
    - "dynamic Page 2: splice the interior of the model's p2_slot_desc JSON array (drop outer [ ]) into ui.c's own slots wrapper so Page 2 tracks the active model"
    - "locale-independent int->string (omega_itoa_msg): manual digit extraction, no snprintf/atof (UI-01)"
    - "OMEGA_HAS_UI compile gate resolved: ui.c owns omega_build_ui; dsp.c's A-02 fallback removed"

key-files:
  created:
    - src/ui.c
  modified:
    - src/omega.h
    - src/dsp.c
    - tests/test_render.c
    - Makefile
    - .github/workflows/ci.yml

key-decisions:
  - "Page 2 assembled by splicing the INTERIOR of fm2_p2_slot_desc's JSON array (indices 1..n-2, dropping outer brackets) into ui.c's own slots wrapper, then appending FX TYPE/AMT — keeps the model as the single source of its slot list while ui.c owns page structure"
  - "omega_build_ui returns bytes written EXCLUDING the null terminator (matches A-RESEARCH get_param contract 311-314 and fm2_p2_slot_desc's own convention); on overflow it returns what fit and still null-terminates, never overrunning"
  - "D-10 buf_len log kept in dsp.c (not ui.c): dsp.c owns the g_host handle and the get_param entry point; ui.c stays pure (no logging on the audio thread). omega_itoa_msg formats without snprintf/locale"
  - "src/ui.c added to Makefile TEST_SRCS (dsp.so already covers it via the src/*.c wildcard); required because dsp.c no longer provides omega_build_ui, so the native test link needs ui.c"

requirements-completed: [KICK-02, KICK-12]

# Metrics
duration: 3min
completed: 2026-09-29
---

# Phase A Plan 03: UI Hierarchy and Buflen Spike Summary

**A real, allocation-free `ui_hierarchy` JSON (Kick Page 1's 8 Bohm-keyed encoder slots + a dynamically-assembled FM2 Kick Page 2 spliced from the model's own `p2_slot_desc` plus FX TYPE/AMT placeholders) served from `get_param` bounded to the host `buf_len` and null-terminated, with the A-02 fallback removed, a one-shot locale-independent D-10 buf_len measurement spike wired for Phase E, and the CI cross-build + glibc gate flipped to blocking.**

## Performance

- **Duration:** ~3 min
- **Tasks:** 3
- **Files:** 1 created, 5 modified

## Accomplishments
- `src/ui.c` serves the real minimal `ui_hierarchy` (D-08/D-09) with **zero allocation**: a `static const UI_PAGE1` string carries all 8 Kick Page-1 slots (PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR), each keyed with the exact `PK_*` string so JSON keys match the `set_param`/`get_param` dispatch. Kick Page 2 is assembled at runtime by calling `g_models[inst->model]->p2_slot_desc` and splicing its array interior (the 3 FM2 slots: FM RATIO/FM INDEX/OP2 WAVE) followed by FX TYPE/AMT placeholder slots.
- Every write is bounded by a `ui_append` helper that copies `min(len, remaining)` reserving a byte for the terminator, so the builder **never writes past `buf_len`** and always null-terminates (Pitfall 3). It returns bytes written excluding the terminator (get_param contract, Pitfall 4).
- `src/dsp.c` drops the A-02 `#ifndef OMEGA_HAS_UI` fallback entirely; `omega_build_ui` is now declared in `omega.h` (which defines `OMEGA_HAS_UI`) and owned by `ui.c`. `get_param(ui_hierarchy)` performs the **D-10 one-shot buf_len log** guarded by `inst->logged_buflen`, formatting `ui_buflen=<v>` via `omega_itoa_msg` (manual digit extraction — no `snprintf`/`atof`/locale dependency), with a prominent `SPIKE (D-10)` comment scheduling its removal/gating before ship.
- `.github/workflows/ci.yml`'s cross-build + glibc-gate job is now `continue-on-error: false` (blocking) — all entry points and `ui.c` exist, so `make dsp.so` + `scripts/glibc_gate.sh` must pass.
- `tests/test_render.c` proves the contract offline: `ui_hierarchy` returns bytes written in-bounds and null-terminated; contains `pitch` + all 3 FM2 Page-2 keys (`fm_ratio`/`fm_index`/`op2_wave`) + `fx_type`/`fx_amt`; unknown keys return `-1`; and a tiny 16-byte `buf_len` neither overruns (canary byte intact) nor drops the null terminator.

## Task Commits

Each task committed atomically:

1. **Task 1: Implement ui.c — real ui_hierarchy + dynamic FM2 Page 2** — `98e62a0` (feat)
2. **Task 2: Wire dsp.c to ui.c, add D-10 buf_len spike, flip CI to blocking** — `3ac9e54` (feat)
3. **Task 3: ui_hierarchy contract assertions in the harness** — `e8802b7` (test)
4. **Docs: SUMMARY + STATE + ROADMAP** — see final metadata commit

## Files Created/Modified
- `src/ui.c` (created) — `UI_PAGE1` (8 keyed slots), `UI_FX_SLOTS`, structural wrappers, bounded `ui_append`, `omega_build_ui` (zero-alloc, model-aware Page 2, buf_len-bounded, null-terminated)
- `src/omega.h` (modified) — `#define OMEGA_HAS_UI 1` + `int omega_build_ui(bohm_instance_t*, char*, int)` prototype
- `src/dsp.c` (modified) — removed the `#ifndef OMEGA_HAS_UI` fallback body; added `omega_itoa_msg` (locale-independent); wired the D-10 one-shot buf_len log into `get_param(ui_hierarchy)`
- `tests/test_render.c` (modified) — ui_hierarchy contract assertions (keys, bytes-written, -1 for unknown, truncation canary)
- `Makefile` (modified) — added `src/ui.c` to `TEST_SRCS`; noted the `src/*.c` wildcard already covers ui.c in the `dsp.so` build
- `.github/workflows/ci.yml` (modified) — cross-build + glibc gate job flipped to `continue-on-error: false`

## Decisions Made
- **Page 2 splicing:** ui.c drops the outer `[` `]` of the model's `p2_slot_desc` array and injects the interior into its own `"slots":[ ... ]`, then appends FX TYPE/AMT. The model owns its slot list; ui.c owns page structure — clean separation for Phase B models.
- **Bytes-written excludes terminator:** matches the A-RESEARCH get_param contract and `fm2_p2_slot_desc`'s convention; the test asserts `buf[n]=='\0'`.
- **D-10 log stays in dsp.c:** dsp.c owns `g_host` and the entry point; ui.c stays log-free (audio-thread purity, Pitfall 1). Formatting is locale-independent.

## Deviations from Plan

None — plan executed as written. One dependency was pulled forward within scope: the plan lists the Makefile `TEST_SRCS` edit under Task 3, but it is a hard link dependency for Task 2's `make test` verify (dsp.c no longer defines `omega_build_ui`), so it was applied and committed in Task 2. No behavior or scope change.

## Issues Encountered
- **aarch64 cross-build (`make dsp.so`) cannot run locally:** `aarch64-linux-gnu-gcc` / Docker are unavailable on the macOS host (by design, D-11). CI (Linux + Docker) is the authoritative cross-build + glibc gate, now flipped to blocking. Native `make test` is fully green locally and is the per-task gate — both `test_fm2` and `test_render` print `ALL TESTS PASSED`, and `[host] ui_buflen=4096` confirms the D-10 spike fires once.

## Known Stubs
- **FX TYPE / FX AMT slots in the UI (intentional, per D-08 / Claude's Discretion):** the two FX slots appear in Kick Page 2 with the correct `PK_FX_TYPE`/`PK_FX_AMT` keys, but the 5 real FX modes (Diode/Clip/SAT/Fold/Crush) are KICK-14 / Phase B. The keys are wired end-to-end (UI + `fm2_set_param` stores them); only the DSP processing is deferred. This is the explicitly-allowed placeholder from D-08; it does not block the plan's goal (every FM2 param is reachable by a real key string matching dispatch).
- **D-10 buf_len log (intentional instrumentation, gated for removal):** the one-shot `host->log` on the audio thread is a deliberate spike diagnostic to capture the host-supplied `buf_len` for Phase E hierarchy sizing (A-04 captures it on-device across the 3 hosts). It is flag-guarded (`logged_buflen`), fires at most once per instance, and carries a prominent `SPIKE (D-10)` comment scheduling its removal or debug-flag gating before ship. Not a functional stub — it is required by D-10.

## Next Phase Readiness
- A-04 (on-device validation) can now deploy `dsp.so`, load in all 3 host contexts, and read the `[host] ui_buflen=<v>` log to capture the real per-host `buf_len` (resolves STATE.md open question / A-RESEARCH Open Q1) and `/proc/cpuinfo` for the `-mcpu` decision (D-15).
- CI now blocks on a successful aarch64 cross-build + glibc/libmvec/export gate, so any regression in the shipped module fails the pipeline.
- Phase E (full nav tree) inherits the zero-alloc, buf_len-bounded `omega_build_ui` pattern and the measured buf_len ceiling for sizing the complete hierarchy.

## Self-Check: PASSED
