---
phase: quick-260929-cjj
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/omega.h
  - tests/mock_host.c
  - tests/mock_host.h
  - src/dsp.c
  - module.json
  - src/ui.c
  - tests/test_render.c
autonomous: true
requirements: [FNDTN-01, FNDTN-06, KICK-01]
gap_closure: true

must_haves:
  truths:
    - "host_api_v1_t matches Context/01 byte-for-byte, so g_host->log lands on the real log slot (no segfault on first get_param)"
    - "module.json validates against the Context/01 manifest schema (capabilities nested)"
    - "get_param(ui_hierarchy) emits the host's real schema (levels map, pad_layout, child_index_param) using the existing PK_* keys"
    - "make test stays green after each task"
    - "D-10 host->log buf_len spike remains flag-guarded / one-shot, still slated for removal before ship (unchanged by this plan)"
  artifacts:
    - path: "src/omega.h"
      provides: "Corrected host_api_v1_t + plugin_api_v2_t ABI (uint32_t api_version, mapped_memory/audio_out_offset/audio_in_offset)"
      contains: "mapped_memory"
    - path: "module.json"
      provides: "Manifest with nested capabilities block"
      contains: "capabilities"
    - path: "src/ui.c"
      provides: "omega_build_ui emitting the real levels-based ui_hierarchy"
      contains: "levels"
  key_links:
    - from: "src/dsp.c omega_get_param"
      to: "g_host->log"
      via: "correct struct offset (log after audio_in_offset)"
      pattern: "g_host->log"
    - from: "src/ui.c omega_build_ui"
      to: "set_param/get_param dispatch"
      via: "PK_* key strings unchanged in emitted JSON"
      pattern: "PK_PITCH|PK_FM_RATIO"
    - from: "tests/mock_host.c make_mock_host"
      to: "host_api_v1_t layout"
      via: "initializes mapped_memory/audio_out_offset/audio_in_offset"
      pattern: "mapped_memory"
---

<objective>
Fix the Phase A on-device load failure. The module instantiates but crashes/blanks because three fields of the host/plugin ABI and both JSON schemas were invented instead of taken verbatim from `Context/01_SCHWUNG_DEV_ARCHITECTURE.md` (the authoritative doc).

Root cause: `host_api_v1_t` is missing three fields (`mapped_memory`, `audio_out_offset`, `audio_in_offset`) that sit between `frames_per_block` and `log` in the canonical struct. On-device this shifts `g_host->log` onto the offset the host actually uses for `mapped_memory` (a data pointer). The D-10 spike calls `g_host->log()` during the first `get_param("ui_hierarchy")` right after instantiation, jumping through a data pointer -> segfault -> "nothing loads in". `module.json` and the `ui_hierarchy` JSON were also invented and must match the doc's schemas.

Purpose: unblock A-04 on-device validation (3-host load, buf_len capture) which is currently impossible because the module crashes on load.
Output: corrected `src/omega.h`, `tests/mock_host.c/.h`, `src/dsp.c`, `module.json`, `src/ui.c`, `tests/test_render.c`, with `make test` green throughout.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@CLAUDE.md

# AUTHORITATIVE spec — the fix is "make our code match this verbatim"
@Context/01_SCHWUNG_DEV_ARCHITECTURE.md

# Files being fixed
@src/omega.h
@src/dsp.c
@src/ui.c
@tests/mock_host.c
@tests/mock_host.h
@tests/test_render.c
@module.json

<interfaces>
<!-- Verbatim from Context/01 lines 30-66. Use these exactly. -->

Correct host_api_v1_t (Context/01 lines 30-50):

    typedef struct host_api_v1 {
        uint32_t api_version;
        int sample_rate;         /* Fixed at 44100 Hz */
        int frames_per_block;    /* Block size, typically 128 frames */

        /* Memory Mapped Direct Access */
        uint8_t *mapped_memory;
        int audio_out_offset;
        int audio_in_offset;

        /* Logging function */
        void (*log)(const char *msg);

        int (*midi_send_internal)(const uint8_t *msg, int len);
        int (*midi_send_external)(const uint8_t *msg, int len);

        int (*get_clock_status)(void);
        double (*get_beat_position)(void);
    } host_api_v1_t;

Correct plugin_api_v2_t api_version (Context/01 line 53): `uint32_t api_version; /* Must be set to 2 */`

Real ui_hierarchy schema shape (Context/01 lines 116-155): top-level object with
`"pad_layout":"drums"`, `"child_index_param":"current_pad"`, and a `"levels"` map whose
entries have `"name"`, a `"params"` array, and a `"knobs"` array. Params referencing a
sub-page use `{"level":"<id>","label":"<Label>"}`.
</interfaces>

<constraints>
- C11, no C++/STL (CLAUDE.md).
- ZERO allocation / file I/O / logging in `omega_build_ui` (audio thread). Only the emitted JSON changes — preserve the bounded `ui_append` helper, null-termination, and bytes-written return (get_param contract).
- No `atof`/`strtod`/locale-dependent parsing anywhere.
- Do NOT touch `.github/workflows/ci.yml` or the Makefile `-lm`/`LDLIBS` lines (already correct).
- Emitted JSON must keep the existing `PK_*` key strings so set_param/get_param dispatch still matches.
</constraints>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Fix host/plugin ABI struct layout (the crash fix)</name>
  <files>src/omega.h, tests/mock_host.c, tests/mock_host.h</files>
  <action>
Correct `host_api_v1_t` in src/omega.h to match Context/01 lines 30-50 VERBATIM. Two changes:

1. Change `int api_version;` to `uint32_t api_version;` (keep comment `/* = 1 */`).
2. Between `int frames_per_block;` and `void (*log)(const char *msg);` insert exactly these three fields (with the "Memory Mapped Direct Access" comment):

       /* Memory Mapped Direct Access */
       uint8_t *mapped_memory;
       int audio_out_offset;
       int audio_in_offset;

Leave the rest of the field order unchanged (log, midi_send_internal, midi_send_external, get_clock_status, get_beat_position). Update the struct's leading comment to reference Context/01 lines 30-50 and note the new mapped-memory fields.

In `plugin_api_v2_t` (src/omega.h) change `int api_version;` to `uint32_t api_version;` (keep comment `/* = 2 */`, Context/01 line 53). No other plugin field changes.

In tests/mock_host.c `make_mock_host()`: the `= {0}` initializer already zeroes everything, but add explicit assignments after `h.frames_per_block = 128;` to document the new fields:

       h.mapped_memory       = NULL;
       h.audio_out_offset    = 0;
       h.audio_in_offset     = 0;

`h.api_version = 1;` still assigns cleanly to uint32_t — leave it.

tests/mock_host.h does NOT declare the struct literal (it only declares `make_mock_host()` and includes omega.h), so no change is needed there. Update it ONLY if inspection shows it declares struct fields; otherwise leave it untouched.

In src/dsp.c, `g_api.api_version = 2;` assigns an int literal to a uint32_t field — this compiles cleanly, no change. Do not modify dsp.c in this task beyond confirming it still builds.

Reference: the wrong layout put `g_host->log` at the host's `mapped_memory` offset; the D-10 spike calls `g_host->log()` on the first `get_param("ui_hierarchy")` -> jump through a data pointer -> segfault. This struct fix removes the crash.
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && make test 2>&1 | tail -20</automated>
  </verify>
  <done>
- `make test` prints "ALL TESTS PASSED" and still logs a line matching `ui_buflen=` (D-10 spike still fires on the correct log slot).
- `grep -n "mapped_memory" src/omega.h` shows the field between frames_per_block and log.
- `grep -c "uint32_t api_version" src/omega.h` returns 2 (both structs).
- `grep -n "mapped_memory" tests/mock_host.c` shows the explicit init.
  </done>
</task>

<task type="auto">
  <name>Task 2: Rewrite module.json to the Context/01 manifest schema</name>
  <files>module.json</files>
  <action>
Replace the entire contents of module.json with the following (Context/01 lines 83-99 schema — capabilities NESTED, plus name/abbrev/author/description). Use these exact values:

    {
      "id": "omega",
      "name": "Omega",
      "abbrev": "OMGA",
      "version": "0.1.0",
      "author": "James Fish",
      "description": "Ohm Force Bohm-inspired FM2 techno kick for Schwung (Phase A)",
      "api_version": 2,
      "capabilities": {
        "chainable": true,
        "component_type": "sound_generator",
        "audio_out": true,
        "midi_in": true,
        "pad_layout": "drums"
      }
    }

The old flat `component_type`/`pad_layout` top-level keys are gone; they now live inside `capabilities`. This is a pure data file with no build dependency, so `make test` is unaffected but must still pass.
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && python3 -c "import json;d=json.load(open('module.json'));assert d['capabilities']['component_type']=='sound_generator';assert d['capabilities']['pad_layout']=='drums';assert d['abbrev']=='OMGA';assert 'component_type' not in d;print('module.json OK')"</automated>
  </verify>
  <done>
- `python3 -c "import json; json.load(open('module.json'))"` succeeds (valid JSON).
- `capabilities.component_type == "sound_generator"` and `capabilities.pad_layout == "drums"`.
- Top-level `component_type` key is GONE (nested only).
- `abbrev`, `author`, `description` present per the spec values above.
  </done>
</task>

<task type="auto">
  <name>Task 3: Rewrite omega_build_ui to the real levels-based ui_hierarchy + update test assertions</name>
  <files>src/ui.c, tests/test_render.c</files>
  <action>
Replace the invented `{"pages":[{"slots":[{key,label}]}]}` JSON in src/ui.c `omega_build_ui` with the host's real ui_hierarchy schema (Context/01 lines 109-162). PRESERVE the existing mechanism exactly: zero allocation, the bounded `ui_append` helper, `buf[off] = '\0'` null-termination, and returning bytes-written (get_param contract). ONLY the emitted JSON changes. Keep using the existing `PK_*` key macros so set_param/get_param dispatch still matches.

New emitted structure (top-level object with pad_layout, child_index_param, and a levels map):

    {
      "pad_layout":"drums",
      "child_index_param":"current_pad",
      "levels":{
        "root":{ ... },
        "kick1":{ ... },
        "kick2":{ ... }
      }
    }

Build it from `static const char[]` fragments the same way as today (replace UI_OPEN / UI_PAGE1_OPEN / UI_PAGE1 / UI_PAGE_MID / UI_PAGE2_OPEN / UI_FX_SLOTS / UI_CLOSE with new fragments). Emit these levels — FM2-only Phase A:

root level (`"root"`): name "Omega", params array =
  {"key":"model","name":"Model","type":"enum","options":["FM2"]},
  {"key":"master_vol","name":"Volume","type":"float","min":0.0,"max":1.0},
  {"level":"kick1","label":"Kick 1"},
  {"level":"kick2","label":"Kick 2"}
knobs = ["model","master_vol"]
Use PK_MODEL and PK_MASTER_VOL for the two keys.

kick1 level (`"kick1"`): name "Kick 1", params = the 8 Page-1 params, each
  {"key":<PK_*>,"name":<UPPER label>,"type":"float","min":0.0,"max":1.0}:
  pitch/PITCH, length/LENGTH, sustain/SUSTAIN, curve/CURVE, attack/ATTACK,
  trs_dec/"TRS DEC", trs_tne/"TRS TNE", color/COLOR (use PK_PITCH .. PK_COLOR).
knobs = ["pitch","length","sustain","curve","attack","trs_dec","trs_tne","color"]

kick2 level (`"kick2"`): name "Kick 2", params = the 5 FM2 Page-2 params, each
  {"key":<PK_*>,"name":<label>,"type":"float","min":0.0,"max":1.0}:
  fm_ratio/"FM RATIO", fm_index/"FM INDEX", op2_wave/"OP2 WAVE",
  fx_type/"FX TYPE", fx_amt/"FX AMT" (use PK_FM_RATIO .. PK_FX_AMT).
knobs = ["fm_ratio","fm_index","op2_wave","fx_type","fx_amt"]

Inlining the FM2 kick2 params directly into ui.c is fine (FM2 is the only Phase A model) — this is simpler than splicing `p2_slot_desc`, and the schema shape changed anyway. IMPORTANT: `fm2_p2_slot_desc` is a `static` function assigned to `g_fm2_vtable.p2_slot_desc` in fm2.c — leave that vtable field in place so fm2.c keeps compiling with no `-Wunused` breakage; ui.c simply stops calling it. Do NOT touch fm2.c.

Keep the leading file comment accurate: update the "Layout" block comment in src/ui.c to describe the new levels schema instead of the old pages/slots layout. Keep the audio-thread rules note (no alloc/log/file I/O) and the D-10-log-lives-in-dsp.c note.

Then update tests/test_render.c ui_hierarchy assertions (the block starting near line 110) to the new schema. Keep the get_param contract checks (bytes-written in-bounds, null-terminated, canary/truncation safety, unknown key returns -1) exactly as-is. Change only the substring assertions to:
  assert(strstr(uibuf, "levels"));
  assert(strstr(uibuf, "pitch"));
  assert(strstr(uibuf, "fm_ratio"));
  assert(strstr(uibuf, "fm_index"));
  assert(strstr(uibuf, "op2_wave"));
  assert(strstr(uibuf, "fx_type"));
  assert(strstr(uibuf, "fx_amt"));
The existing `assert(strstr(uibuf,"pitch"))` etc. survive; ADD the `"levels"` assertion. Update the comment above the block from "Page 1 key + all 3 FM2 Page-2 keys" to reflect the levels schema.
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && make test 2>&1 | tail -20</automated>
  </verify>
  <done>
- `make test` prints "ALL TESTS PASSED".
- `grep -n "levels" src/ui.c` and `grep -n "child_index_param" src/ui.c` both match.
- `grep -c "\"pages\"" src/ui.c` returns 0 (old schema gone).
- `grep -n "levels" tests/test_render.c` matches (new assertion present).
- All PK_* keys (pitch, master_vol, fm_ratio, fm_index, op2_wave, fx_type, fx_amt) still appear in the emitted JSON so dispatch is unchanged.
  </done>
</task>

</tasks>

<verification>
- `make test` green after each task (run it after Task 1, Task 2's JSON check, and Task 3).
- `objdump`/on-device load is NOT part of this plan (that is A-04, still gated on hardware) — but the crash root cause (struct layout) is removed here.
- The D-10 `g_host->log` buf_len spike is unchanged: still one-shot, flag-guarded via `inst->logged_buflen`, still carrying its `SPIKE (D-10)` removal comment. This plan does not remove or alter it.
- CI cross-build (`make dsp.so` + glibc gate) is unaffected: no new external symbols, no Makefile/CI edits.
</verification>

<success_criteria>
- src/omega.h `host_api_v1_t` matches Context/01 lines 30-50 field-for-field (uint32_t api_version + the three mapped-memory fields before `log`); `plugin_api_v2_t.api_version` is uint32_t.
- tests/mock_host.c initializes the three new fields.
- module.json matches the Context/01 nested-capabilities schema with the specified values.
- src/ui.c `omega_build_ui` emits the real levels-based ui_hierarchy (pad_layout + child_index_param + levels{root,kick1,kick2}) using existing PK_* keys, with the bounded/zero-alloc/null-terminated/bytes-written mechanism intact.
- tests/test_render.c asserts the new schema ("levels" + all keys), still enforcing the get_param contract.
- `make test` prints "ALL TESTS PASSED".
</success_criteria>

<output>
After completion, create `.planning/quick/260929-cjj-fix-phase-a-on-device-load-failure-host-/260929-cjj-SUMMARY.md`
</output>
