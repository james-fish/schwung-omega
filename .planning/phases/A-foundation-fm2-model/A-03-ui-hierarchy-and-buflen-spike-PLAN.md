---
phase: A-foundation-fm2-model
plan: 03
type: execute
wave: 3
depends_on: ["01", "02"]
files_modified:
  - src/ui.c
  - src/dsp.c
  - src/omega.h
  - tests/test_render.c
  - .github/workflows/ci.yml
autonomous: true
requirements: [KICK-02, KICK-12]
must_haves:
  truths:
    - "get_param(\"ui_hierarchy\") returns a real minimal JSON string (not a stub) covering Kick Page 1 + FM2 Page 2"
    - "The JSON's param keys exactly match the set_param/get_param dispatch keys"
    - "get_param returns bytes written for ui_hierarchy and -1 for unknown keys, never overrunning buf_len"
    - "buf_len passed by the host is captured once via a flag-guarded one-shot host->log spike (D-10), gated for removal before ship"
    - "make dsp.so cross-build succeeds and the glibc gate passes (CI job flipped to blocking)"
  artifacts:
    - path: "src/ui.c"
      provides: "pre-serialized static ui_hierarchy string(s) + omega_build_ui() assembling Page 1 + FM2 Page 2 with zero allocation"
      contains: "omega_build_ui"
    - path: "src/dsp.c"
      provides: "get_param wired to omega_build_ui + D-10 one-shot buf_len log; temp fallback removed"
      contains: "logged_buflen"
  key_links:
    - from: "src/dsp.c get_param(ui_hierarchy)"
      to: "src/ui.c omega_build_ui"
      via: "direct call, memcpy into host buffer, null-terminated, bytes returned"
      pattern: "omega_build_ui"
    - from: "src/ui.c omega_build_ui"
      to: "src/models/fm2.c fm2_p2_slot_desc via g_models[inst->model]"
      via: "Page 2 assembled from active model p2_slot_desc"
      pattern: "p2_slot_desc"
---

<objective>
Deliver the real minimal `ui_hierarchy` JSON (D-08/D-09) and wire the D-10 buf_len measurement spike. `ui.c` stores pre-serialized static string fragments for Kick Page 1 (8 encoder slots) and assembles FM2 Kick Page 2 (3 model slots + FX TYPE/AMT placeholders) from the active model's `p2_slot_desc` into the caller's buffer with zero allocation. `dsp.c`'s temporary `omega_build_ui` fallback is replaced by the real one, and the one-shot flag-guarded host->log records the host-supplied buf_len (unblocking Phase E), scheduled for removal/gating before ship. Finally, the CI dsp.so job flips to blocking now that all entry points and ui.c exist.

Purpose: Complete the UI-facing half of the vertical slice so params are reachable by real key strings, and capture the buf_len unknown flagged in STATE.md / A-RESEARCH Open Question 1.
Output: A real `ui_hierarchy` served correctly, a wired D-10 spike, and a green blocking cross-build.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/phases/A-foundation-fm2-model/A-CONTEXT.md
@.planning/phases/A-foundation-fm2-model/A-RESEARCH.md
@CLAUDE.md
@.planning/phases/A-foundation-fm2-model/A-01-SUMMARY.md
@.planning/phases/A-foundation-fm2-model/A-02-SUMMARY.md

<interfaces>
<!-- Contracts from A-01/A-02. -->
From src/omega.h: PK_* param key macros (must match JSON exactly), bohm_instance_t (has `bool logged_buflen`), g_models[].
From src/models/fm2.c: `int fm2_p2_slot_desc(bohm_instance_t*, char*, int)` returns a JSON fragment for the 3 FM2 slots.
From src/dsp.c (A-02): `omega_get_param` currently calls a TEMP `omega_build_ui` fallback returning `{"pages":[]}` — this plan replaces the fallback with the real ui.c symbol.
get_param contract (A-RESEARCH 311-314): return bytes written, or -1 if key unhandled; never exceed buf_len; always null-terminate.
D-10 one-shot log pattern (A-RESEARCH 364-376, Pitfall 1 lines 293-296): guard with `inst->logged_buflen`; treat as temporary spike diagnostic; format buf_len WITHOUT locale-dependent printf if possible.
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Implement ui.c — real minimal ui_hierarchy with dynamic FM2 Page 2 assembly</name>
  <read_first>
    - src/omega.h (PK_* macros — JSON keys MUST match these exactly)
    - src/models/fm2.c fm2_p2_slot_desc (the Page 2 fragment source)
    - .planning/phases/A-foundation-fm2-model/A-CONTEXT.md D-08, D-09, D-10 and integration point (ui.c reads active model ID, lines 113-114)
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md §Pitfall 3 (buffer overrun, lines 305-309), get_param example (362-376)
    - Context/04_BOHM_SCHWUNG_MODULE_DESIGN.md (ui_hierarchy JSON schema examples, param naming conventions)
  </read_first>
  <action>
    Create `src/ui.c` (`#include "omega.h"`, `#include <string.h>`):
    - Define a `static const char UI_PAGE1[]` pre-serialized JSON fragment (D-09, no JSON library) for Kick Page 1 with 8 encoder slots in order: PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR. Each slot object MUST use the exact PK_ key string (e.g. `"key":"pitch"`) and a display label. Keep total well under 2KB (A-RESEARCH Open Q1 recommends conservative sizing).
    - Implement `int omega_build_ui(bohm_instance_t *inst, char *buf, int buf_len)` — ZERO allocation (D-09):
      1. Assemble the full hierarchy into the caller's `buf`: an opening JSON wrapper, the static Page 1 fragment, then Page 2 assembled by calling `g_models[inst->model]->p2_slot_desc(inst, tmp, sizeof tmp)` where `tmp` is a stack buffer, plus FX TYPE (PK_FX_TYPE) and FX AMT (PK_FX_AMT) placeholder slots, then the closing wrapper.
      2. Bound every write to remaining `buf_len` (Pitfall 3): track a running offset, use `memcpy` of `min(len, remaining)`, and ALWAYS null-terminate at `buf[min(total, buf_len-1)]`. Never write past buf_len.
      3. Return the number of bytes written (excluding or including terminator per get_param contract — return bytes written to match A-RESEARCH; document the choice in a comment).
    - Add `int omega_build_ui(bohm_instance_t*, char*, int);` prototype to `src/omega.h` and add `#define OMEGA_HAS_UI 1` there (so dsp.c drops its temporary fallback — see Task 2).
    - Keep ui.c free of any allocation, file I/O, or logging (all six entry points are audio-thread; A-RESEARCH Pitfall 1).
  </action>
  <verify>
    <automated>cc -std=gnu11 -Isrc -Itests -c src/ui.c -o /tmp/ui.o && echo "ui compiles"</automated>
  </verify>
  <acceptance_criteria>
    - `src/ui.c` contains `omega_build_ui`
    - `src/ui.c` UI_PAGE1 JSON contains all 8 Page-1 keys: `"pitch"`, `"length"`, `"sustain"`, `"curve"`, `"attack"`, `"trs_dec"`, `"trs_tne"`, `"color"` (grep each)
    - `src/ui.c` calls `g_models[inst->model]->p2_slot_desc` (dynamic Page 2 assembly)
    - `src/ui.c` includes FX TYPE and FX AMT placeholder slots using `PK_FX_TYPE`/`PK_FX_AMT` (grep `fx_type`, `fx_amt`)
    - `src/ui.c` bounds writes to `buf_len` and null-terminates (grep `buf_len` and a `memcpy`/terminator)
    - `src/ui.c` contains NO `malloc`/`calloc`/`realloc`/`printf` (grep -c returns 0)
    - `src/omega.h` now declares `omega_build_ui` and defines `OMEGA_HAS_UI`
    - ui.c compiles to an object file (verify prints `ui compiles`)
  </acceptance_criteria>
  <done>A real, allocation-free ui_hierarchy is served: static Page 1 (8 correctly-keyed slots) + dynamically assembled FM2 Page 2 (3 model slots + FX TYPE/AMT), bounded to buf_len and null-terminated.</done>
</task>

<task type="auto">
  <name>Task 2: Wire dsp.c get_param to real ui.c, add D-10 buf_len one-shot spike, flip CI to blocking</name>
  <read_first>
    - src/dsp.c (A-02 — the get_param with the TEMP omega_build_ui fallback to remove)
    - src/ui.c (Task 1 — the real omega_build_ui)
    - .github/workflows/ci.yml (A-01 — the dsp.so job with continue-on-error: true to flip)
    - .planning/phases/A-foundation-fm2-model/A-CONTEXT.md D-10, D-11
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md get_param example + D-10 one-shot (362-376), Pitfall 1 conflict note (293-296), Open Question 4 (563-565)
  </read_first>
  <action>
    Edit `src/dsp.c`:
    - Remove the temporary `#ifndef OMEGA_HAS_UI` fallback definition of `omega_build_ui` (ui.c now owns it; OMEGA_HAS_UI is defined in omega.h). Keep `extern int omega_build_ui(bohm_instance_t*, char*, int);` available via omega.h include.
    - In `omega_get_param`, for `PK_UI_HIER`:
      1. D-10 one-shot buf_len spike (guarded): `if (!inst->logged_buflen && g_host && g_host->log) { char m[64]; /* format "ui_buflen=%d" WITHOUT locale-dependent printf: build the integer string manually or with a fixed-radix itoa helper */ omega_itoa_msg(m, buf_len); g_host->log(m); inst->logged_buflen = true; }` Write a small `static void omega_itoa_msg(char *dst, int v)` that formats `ui_buflen=<v>` using manual digit extraction (no snprintf/locale). Add a prominent comment: `/* SPIKE (D-10): one-shot buf_len measurement to unblock Phase E. host->log on the audio thread violates the no-log rule (A-RESEARCH Pitfall 1 / Open Q4) — REMOVE or gate behind a debug build flag before ship. */`
      2. `return omega_build_ui(inst, buf, buf_len);`
    - Confirm unknown keys still `return -1`.

    Edit `.github/workflows/ci.yml`: flip the dsp.so cross-build job from `continue-on-error: true` to `continue-on-error: false` (now that dsp.c/ui.c/registry/fm2.c all exist, `make dsp.so` must succeed and `scripts/glibc_gate.sh build/dsp.so` must pass). Remove the "may fail until A-02" TODO comment.
  </action>
  <verify>
    <automated>make test && cc -std=gnu11 -Isrc -Itests -DOMEGA_MALLOC_TRAP -fsyntax-only src/dsp.c && grep -q "continue-on-error: false" .github/workflows/ci.yml && echo "WIRED_OK"</automated>
  </verify>
  <acceptance_criteria>
    - `src/dsp.c` no longer defines a fallback `omega_build_ui` body (grep: `omega_build_ui` appears only as a call, not `int omega_build_ui(...){`)
    - `src/dsp.c` get_param for ui_hierarchy calls `omega_build_ui(inst, buf, buf_len)`
    - `src/dsp.c` contains a `logged_buflen` one-shot guard and `omega_itoa_msg` (no `snprintf`/`sprintf`/`atof` — grep -c returns 0 for each)
    - `src/dsp.c` contains a `SPIKE (D-10)` comment noting removal/gating before ship
    - `.github/workflows/ci.yml` contains `continue-on-error: false` for the dsp.so job (no remaining `continue-on-error: true`)
    - `make test` exits 0 (verify reaches `WIRED_OK`)
  </acceptance_criteria>
  <done>get_param serves the real ui_hierarchy from ui.c; the D-10 buf_len spike logs once, locale-independently, with a documented removal note; CI now blocks on a successful cross-build + glibc gate.</done>
</task>

<task type="auto">
  <name>Task 3: Add ui.c to build + harness assertions for ui_hierarchy contract</name>
  <read_first>
    - Makefile (A-01 — test + dsp.so targets to add src/ui.c to)
    - tests/test_render.c (A-02 — add ui assertions)
    - src/ui.c, src/dsp.c (Tasks 1-2)
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md get_param contract (311-314), Pitfall 3 (305-309)
  </read_first>
  <action>
    Edit `Makefile`: add `src/ui.c` to BOTH the native `test` compile line and the `dsp.so` source set (or rely on the `src/*.c` wildcard already covering ui.c — verify ui.c is included in both). Ensure the native test link includes ui.c so `omega_build_ui` resolves (dsp.c no longer provides it).

    Edit `tests/test_render.c` — add ui_hierarchy assertions:
    - `char uibuf[4096]; int n = api->get_param(inst, "ui_hierarchy", uibuf, sizeof uibuf); assert(n > 0 && n < (int)sizeof uibuf); assert(uibuf[n-1]=='\0' || uibuf[n]=='\0');` (null-terminated, within bounds).
    - `assert(strstr(uibuf, "pitch") && strstr(uibuf, "fm_ratio") && strstr(uibuf, "fm_index") && strstr(uibuf, "op2_wave"));` (Page 1 key + all 3 FM2 Page-2 keys present).
    - Unknown-key contract: `char tmp[8]; assert(api->get_param(inst, "nonexistent_key_xyz", tmp, sizeof tmp) == -1);`
    - Truncation safety (Pitfall 3): call get_param with a deliberately tiny `buf_len` (e.g. 16) into a bounded buffer and assert it does NOT write past the buffer (use a canary byte after the buffer, assert unchanged) and still null-terminates.
    - Keep `ALL TESTS PASSED` print at the end.
  </action>
  <verify>
    <automated>make test && test -s tests/output/fm2_kick.wav</automated>
  </verify>
  <acceptance_criteria>
    - `Makefile` test and dsp.so builds include `src/ui.c` (grep `ui.c`, or a `src/*.c` wildcard that provably covers it)
    - `tests/test_render.c` calls `api->get_param(inst, "ui_hierarchy", ...)` and asserts `n > 0`
    - `tests/test_render.c` asserts presence of `"pitch"`, `"fm_ratio"`, `"fm_index"`, `"op2_wave"` in the JSON (grep `strstr`)
    - `tests/test_render.c` asserts unknown key returns `-1`
    - `tests/test_render.c` has a truncation/canary check for the tiny-buf_len case
    - `make test` exits 0 and prints `ALL TESTS PASSED`
  </acceptance_criteria>
  <done>ui.c is in both build paths; the harness proves the ui_hierarchy contract (correct keys, bytes-written return, -1 for unknown, no buffer overrun) offline.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 with ui_hierarchy contract assertions green (correct keys, -1 for unknown, bounded writes).
- `make dsp.so` cross-build succeeds and `scripts/glibc_gate.sh build/dsp.so` passes; CI job is now blocking.
- The real ui_hierarchy (D-08/D-09) serves Page 1 (8 keyed slots) + dynamic FM2 Page 2 with zero allocation in get_param.
- The D-10 buf_len spike is wired as a one-shot, flag-guarded, locale-independent log with a documented pre-ship removal/gating note.
</verification>

<success_criteria>
- Every FM2 parameter is reachable by a real key string that matches set_param dispatch.
- get_param obeys the return-value contract and never overruns the host buffer (Pitfall 3/4 retired).
- buf_len measurement (STATE.md open question / A-RESEARCH Open Q1) is instrumented for on-device capture in A-04.
</success_criteria>

<output>
After completion, create `.planning/phases/A-foundation-fm2-model/A-03-SUMMARY.md`
</output>
