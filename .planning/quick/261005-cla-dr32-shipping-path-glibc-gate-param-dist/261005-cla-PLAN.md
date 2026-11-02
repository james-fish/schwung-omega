---
phase: quick-261005-cla
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - scripts/glibc_gate.sh
  - Makefile
  - .github/workflows/ci.yml
  - scripts/deploy.sh
  - help.json
  - src/ui.c
  - src/models/fm2.c
  - src/models/fm4.c
  - src/models/wtr.c
  - src/models/phy.c
  - src/models/hrd.c
  - src/models/dig.c
  - src/models/trs.c
  - src/models/ana.c
  - src/models/usr.c
  - tests/test_readback.c
autonomous: true
requirements: [SHIP-DR32, A11Y-REBASE]
must_haves:
  truths:
    - "glibc_gate.sh passes for dr32_engine.so when its export symbol (dr32_engine_plugin) is given as the 2nd arg"
    - "make dist packages dsp.so + dr32_engine.so + module.json + help.json into the release tarball"
    - "CI cross-builds and gates BOTH dsp.so and dr32_engine.so and uploads both (+ help.json) as artifacts"
    - "deploy.sh atomically uploads BOTH dsp.so and dr32_engine.so"
    - "help.json exists at repo root with the six on-device Help topics"
    - "ui_hierarchy param names are spoken words (e.g. Compressor Drive, Wavetable) not ALL-CAPS codes; the OLED grid still draws the old short codes via short_name/short_options"
    - "root hierarchy object no longer carries child_index_param (current_pad)"
    - "every engine (model) param carries short_name, step, and unit"
    - "make test stays GREEN (all 12 suites incl. test-dr32 and test-readback)"
  artifacts:
    - path: "help.json"
      provides: "On-device Help viewer content (6 topics, lines <=20 chars)"
      contains: "\"title\": \"Omega\""
    - path: "scripts/glibc_gate.sh"
      provides: "Parameterized export-symbol gate (EXPECT 2nd arg, default move_plugin_init_v2)"
    - path: "src/ui.c"
      provides: "Accessible ui_hierarchy: spoken names, short_options on code enums, no current_pad"
  key_links:
    - from: "Makefile dist target"
      to: "build/dr32_engine.so + help.json"
      via: "cp into build/dist/omega/ before tar"
      pattern: "cp build/dr32_engine.so|cp help.json"
    - from: ".github/workflows/ci.yml"
      to: "glibc_gate.sh build/dr32_engine.so dr32_engine_plugin"
      via: "new gate step"
      pattern: "dr32_engine_plugin"
    - from: "src/ui.c ui_emit_param (UP_ENUM)"
      to: "short_options JSON field"
      via: "new uiparam_t.short_options emitted when non-NULL"
      pattern: "short_options"
---

<objective>
Ship the already-merged DR32 engine plugin (PR #2) end-to-end, and re-base PR #1's
accessibility layer onto current main (v0.4) without regressing the DSP main already
fixed.

Part 1 — DR32 shipping path: the build + test for dr32_engine.so are DONE on main. Only
packaging/CI/deploy remain: parameterize the export gate, add dr32_engine.so to
dist/CI/deploy, and ship help.json alongside.

Part 2 — Accessibility re-base: cherry-pick PR #1's help.json verbatim, and re-apply its
ui_hierarchy accessibility INTENT (spoken names, short_options for code enums,
short_name/step/unit on all engine params, two collision renames, drop current_pad) onto
main's CURRENT v0.4 param set. DISCARD PR #1's groove.c/groove.h/params.c Gen changes
entirely (superseded by main v0.4.1).

Purpose: make Omega shippable as a DR32 engine AND screen-reader / Help accessible.
Output: 4 updated ship scripts, new help.json, accessible ui.c + 9 model descriptors,
updated test_readback assertions.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@./CLAUDE.md
@.planning/STATE.md

<constraints>
- C11, no C++. This plan touches ONLY: build/CI/deploy scripts, ui.c (ui_hierarchy JSON
  string emitter served from get_param), the 9 model p2_slot_desc JSON string emitters, a
  new help.json, and tests/test_readback.c. NO audio-thread code, NO DSP math changes. All
  ui.c / model-descriptor changes are JSON metadata STRINGS only.
- ui_hierarchy changes are DISPLAY/accessibility metadata only — they do NOT affect p-lock
  automation (automation is driven by module.json chain_params, not the dynamic
  ui_hierarchy). Do NOT touch module.json.
- Cannot run the aarch64 Docker cross-compile or glibc gate on this macOS host (no Docker).
  Verify Part 1 script edits by INSPECTION + `bash -n` syntax check; treat the actual
  cross-compile/gate/deploy as CI-verified. Verify Part 2 via native `make test`.
- DISCARD PR #1's src/groove.c, src/groove.h, src/params.c Gen defaults, tests/
  test_groove.c — they reference struct fields that no longer exist on main and were
  independently fixed in v0.4.1. Do NOT git-merge pr1; re-apply intent only.
- Keep main's v0.4 Gen/dual-voice params (PK_GRV_G* in ui.c) — PR #1 never saw them. Only
  restyle their name/short_name/unit, never remove or regress them.
</constraints>

<ground_truth>
Verified exact locations (do NOT re-derive; re-confirm exact strings before each edit).

PART 1
- scripts/glibc_gate.sh line 37: `EXPORTS=$(objdump -T "$SO" | grep ' g ' | grep -c move_plugin_init_v2 || true)`
  and lines 38/41 messages hardcode "move_plugin_init_v2". dr32_engine.so exports
  `dr32_engine_plugin` (src/dr32_engine.c line ~265,
  `__attribute__((visibility("default"))) const dr32x_plugin *dr32_engine_plugin(...)`).
- Makefile `dist:` target lines 177-182: `dist: dsp.so` -> mkdir build/dist/omega,
  `cp build/dsp.so ...`, `cp module.json ...`, tar. (dr32_engine.so target already exists
  at lines 169-171 with -Wl,-Bsymbolic + DR32_SRCS; `make test` already runs test-dr32 per
  line 187.)
- .github/workflows/ci.yml cross-build-gate job lines 30-41: `make dsp.so` step, gate step
  `./scripts/glibc_gate.sh build/dsp.so`, upload artifact `path:` has `build/dsp.so` +
  `module.json`.
- scripts/deploy.sh lines 14-27: `SO="build/dsp.so"`, existence check, scp to dsp.so.new,
  ssh atomic `mv`.

PART 2
- help.json: retrieve verbatim with `git show pr1:help.json` (172 lines, 6 topics; pr1 =
  fetched ref, confirmed present at 6d65f13). Write to repo root `help.json`.
- src/ui.c ui_emit_param (lines 75-117): UP_ENUM branch (lines 78-83) emits `"options":%s`
  but has NO short_options. uiparam_t struct (lines 41-50) has no short_options field.
- src/ui.c enum option .rodata (lines 152-173): OPT_MODEL, OPT_FX, OPT_ROUTE, OPT_GRVTYPE,
  OPT_GFILT, OPT_RVTYPE, OPT_SCALE, OPT_GWAVE, etc. — these are the CODE arrays. P_PERF line
  185 `{ PK_CLIP, "CMPDR", "CMPDR", ... }` is the compressor-drive param (orchestrator: name
  -> "Compressor Drive").
- src/ui.c UI_OPEN line 302: contains `"child_index_param":"current_pad"` — REMOVE that
  key/value pair from the root object (get_param never serves current_pad).
- Model p2_slot_desc JSON emitters (static const char json[] in each): fm2.c 266-277 (FM
  RATIO/FM INDEX/OP2 WAVE enum Sine/Fold/Tri), wtr.c 255-267 (WAVE SEL/BODY PITCH/TRANS DEC/
  TRANS COL), trs.c 269-281 (CLICK/SNAP/TRANS DEC/WT COLOR/CURVE), plus fm4.c ~356, phy.c
  ~384, hrd.c ~292, dig.c ~268, ana.c ~264, usr.c ~327. These flat interior param objects
  currently carry ONLY key/name/type/min/max (+ enum options) — no short_name/step/unit.
- COLLISION RENAMES (both in trs.c): line 273 `TRANS DEC` (PK_TRS_TDEC) -> "Click Decay";
  line 275 `CURVE` (PK_TRS_CURVE) -> "Sweep Curve" (Kick 1 already has a shared CURVE).
- tests/test_readback.c hardcoded enum-array assertions that WILL break when codes move to
  short_options: line 118 `"options":["FM2"`, line 119 `"Diode","Clip","SAT"`, line 140
  `"Synth","Transient","Both"`. Also asserts presence of `"short_name":` (121), `"unit":`
  (122), `"step":` (123) — keep those satisfiable.
</ground_truth>
</context>

<tasks>

<task type="auto">
  <name>Task 1: DR32 shipping path — gate, dist, CI, deploy (Part 1)</name>
  <files>scripts/glibc_gate.sh, Makefile, .github/workflows/ci.yml, scripts/deploy.sh</files>
  <action>
Four mechanical edits so dr32_engine.so ships beside dsp.so. No DSP/code logic. Addresses
SHIP-DR32.

1) scripts/glibc_gate.sh — parameterize the export symbol:
   - After the `SO=` line add: `EXPECT="${2:-move_plugin_init_v2}"`.
   - Line 37: replace `grep -c move_plugin_init_v2` with `grep -c "$EXPECT"`.
   - Lines 38/41 messages: replace the literal `move_plugin_init_v2` with `$EXPECT` so the
     "exactly one <symbol>" / "found N <symbol> exports" messages stay correct.
   - Update the usage/comment header to document the optional 2nd arg.
   Result: `./scripts/glibc_gate.sh build/dr32_engine.so dr32_engine_plugin` passes; the
   existing `./scripts/glibc_gate.sh build/dsp.so` (no 2nd arg) still gates on
   move_plugin_init_v2 unchanged.

2) Makefile `dist:` target (lines 177-182) — add dr32_engine.so + help.json (help.json comes
   from Task 2; dist just copies it):
   - Change prereq `dist: dsp.so` -> `dist: dsp.so dr32_engine.so`.
   - After `cp build/dsp.so build/dist/omega/dsp.so` add
     `cp build/dr32_engine.so build/dist/omega/dr32_engine.so`.
   - After the module.json cp add `cp help.json build/dist/omega/help.json`.
   - Update the `dist:` comment to list dsp.so + dr32_engine.so + module.json + help.json.

3) .github/workflows/ci.yml cross-build-gate job (after the existing dsp.so build+gate):
   - Add step `- name: Cross-compile dr32_engine.so` running `make dr32_engine.so`.
   - Add step `- name: dr32 glibc / libmvec / export gate` running
     `./scripts/glibc_gate.sh build/dr32_engine.so dr32_engine_plugin`.
   - Extend the upload-artifact `path:` list to include `build/dr32_engine.so` and
     `help.json` (keep `build/dsp.so` and `module.json`).

4) scripts/deploy.sh — atomically deploy dr32_engine.so too:
   - Keep the dsp.so block. Add a parallel block for DR32: a `DR32SO="build/dr32_engine.so"`
     var, an existence check mirroring the dsp.so one (message: run `make dr32_engine.so`
     first), scp `$DR32SO` to `$DEVICE_DIR/dr32_engine.so.new`, then
     `ssh "$DEVICE_HOST" "mv '$DEVICE_DIR/dr32_engine.so.new' '$DEVICE_DIR/dr32_engine.so'"`.
     Echo progress like the dsp.so block.
  </action>
  <verify>
    <automated>bash -n scripts/glibc_gate.sh && bash -n scripts/deploy.sh && grep -q 'EXPECT="${2:-move_plugin_init_v2}"' scripts/glibc_gate.sh && grep -q 'dr32_engine_plugin' .github/workflows/ci.yml && grep -q 'cp build/dr32_engine.so' Makefile && grep -q 'cp help.json' Makefile && grep -q 'dr32_engine.so.new' scripts/deploy.sh && echo PART1_OK</automated>
  </verify>
  <done>
glibc_gate.sh takes an optional 2nd symbol arg (default move_plugin_init_v2) and gates on
it; `bash -n` clean. Makefile dist copies dsp.so + dr32_engine.so + module.json + help.json.
ci.yml builds+gates dr32_engine.so with the dr32_engine_plugin symbol and uploads it +
help.json. deploy.sh atomically uploads dr32_engine.so. (Cross-compile / gate / deploy
themselves are CI/hardware-verified — not runnable on this host.)
  </done>
</task>

<task type="auto">
  <name>Task 2: help.json + ui.c accessibility — short_options, spoken names, drop current_pad (Part 2a)</name>
  <files>help.json, src/ui.c</files>
  <action>
Re-apply PR #1's accessibility INTENT onto main's current ui.c. Do NOT git-merge pr1.
Addresses A11Y-REBASE.

1) help.json — cherry-pick verbatim:
   Run `git show pr1:help.json` and write its EXACT output to repo-root `help.json` (six
   topics: Overview, Pages, Engines, Groove, Performer, Own Samples; lines <=20 chars for the
   on-device Help viewer). No edits to the content.

2) src/ui.c — add a short_options field to the enum metadata path:
   - Add `const char *short_options;` to the `uiparam_t` struct — append it AFTER `mx`
     (last field) so existing positional `{...}` initializers that omit it default to NULL,
     avoiding touching every initializer.
   - In `ui_emit_param` UP_ENUM branch (lines 78-83): when `p->short_options != NULL`, emit
     an extra `,"short_options":%s` immediately after `"options":%s`. When NULL, emit nothing
     extra (back-compat). Keep buffer bounds (obj[320] — the short arrays are small, but
     confirm no truncation).

3) src/ui.c — spoken `name` fields + code `short_options` on the ui.c-owned CODE enums:
   For each ui.c-owned CODE enum, set `name` options to spoken words and move the OLD code
   array into a new `_SHORT` .rodata array wired via short_options so the grid still draws
   codes:
   - OPT_MODEL (line 153, codes FM2/FM4/WTR/PHY/HRD/DIG/TRS/ANA/USR): add OPT_MODEL_SHORT =
     the existing code array; set OPT_MODEL to SPOKEN names (e.g. "FM 2-Op","FM 4-Op",
     "Wavetable","Physical","Hard","Digital","Transient","Analog","User Sample"). Wire
     P_ROOT[PK_MODEL] (line 193) short_options=OPT_MODEL_SHORT.
   - OPT_FX (line 154, Diode/Clip/SAT/Fold/Crush): spoken where cryptic (SAT -> "Saturate",
     otherwise keep), with OPT_FX_SHORT holding the codes; wire P_KICK2[PK_FX_TYPE] (line 218)
     short_options.
   - OPT_ROUTE (line 157, Synth/Transient/Both) is already spoken — leave as-is, no
     short_options needed. Likewise leave the already-spoken OPT_SCALE/OPT_GWAVE/OPT_RESET/
     OPT_NOTELEN/OPT_MONO/OPT_POLE unchanged. Use judgment on OPT_FXROUTE / OPT_RVTYPE /
     OPT_GFILT — only convert if a PURE code; the priority CODE enum is OPT_MODEL.
   - P_PERF (line 185): `{ PK_CLIP, "CMPDR", "CMPDR", ... }` -> name "Compressor Drive" (keep
     short_name "CMPDR"). Give the other P_PERF floats spoken names where cryptic (e.g.
     "MSTR VOL"->"Master Volume", "DUCK REL"->"Duck Release", "DUCK SLEW"->"Duck Slew",
     "DUCK FREQ"->"Duck Frequency", "DJ FILT"->"DJ Filter", "DJ RESO"->"DJ Resonance"),
     keeping their existing short_name grid codes.

4) src/ui.c — drop current_pad (A11Y-REBASE): in UI_OPEN (line 302) remove the
   `"child_index_param":"current_pad",` key/value pair so the root object is
   `{"pad_layout":"drums","levels":{`. get_param never serves current_pad, so every read of
   it fails today.

Keep all existing v0.4 Gen params (P_GROOVE_GEN_SEQ, gengroove1, P_GROOVE1, P_GROOVE_FX)
present. You MAY restyle their `name` to spoken words (e.g. "LPF"->"Low-Pass", "HPF"->
"High-Pass") but MUST NOT remove or regress them or change their keys/short_name grid.
  </action>
  <verify>
    <automated>git show pr1:help.json | diff - help.json && grep -q 'short_options' src/ui.c && grep -q 'Compressor Drive' src/ui.c && ! grep -q 'current_pad' src/ui.c && echo UI_EDITS_OK</automated>
  </verify>
  <done>
help.json at repo root byte-identical to `git show pr1:help.json`. uiparam_t has a
short_options field; UP_ENUM emits "short_options" when present. MODEL (and other code)
enums show spoken option names with codes preserved in short_options; P_PERF CMPDR name is
"Compressor Drive". Root object no longer contains current_pad. All v0.4 Gen params still
present. (test_readback is updated in Task 3; it may still fail here on the old enum-array
asserts — that is expected until Task 3.)
  </done>
</task>

<task type="auto">
  <name>Task 3: Engine param metadata, collision renames, test_readback update (Part 2b)</name>
  <files>src/models/fm2.c, src/models/fm4.c, src/models/wtr.c, src/models/phy.c, src/models/hrd.c, src/models/dig.c, src/models/trs.c, src/models/ana.c, src/models/usr.c, tests/test_readback.c</files>
  <action>
Add accessibility metadata to every engine's p2_slot_desc JSON interior, rename the two
colliding names, and update test_readback for the enum-option changes from Task 2.
Addresses A11Y-REBASE.

1) For EACH of the 9 model p2_slot_desc `static const char json[]` interiors (fm2 266-277,
   fm4 ~356, wtr 255-267, phy ~384, hrd ~292, dig ~268, trs 269-281, ana ~264, usr ~327):
   - Add `"short_name":"<<=6 char code>"`, `"step":"<json literal>"`, and `"unit":"<...>"`
     to EVERY param object that lacks them. Float params: typically step "0.01", unit "%"
     (or "" / "Hz" / "ms" where the param is a real-domain control — use the param's
     existing comment/semantics to pick). Enum params (e.g. fm2 OP2 WAVE): short_name only
     (enums need no step/unit); if the enum options are codes, give spoken `name` options
     and keep codes — but these model enums (Sine/Fold/Tri etc.) are already spoken, so just
     add short_name.
   - Keep each `json[]` a single bounded static string; the `len = sizeof(json)-1` / bounds
     check pattern stays. Do NOT change keys, min, max, or the splice/gating contract
     (ui.c's ui_append_gated_interior still inserts visible_if before each '}').
   - Spoken `name` for cryptic abbreviations per orchestrator mapping (examples; apply the
     spirit across all 9): "TRS TNE"->"Transient Tone", "TRANS COL"->"Transient Color",
     "WAVE SEL"->"Wave Select", "BODY PITCH" stays, "CLICK/SNAP" stays or ->"Click / Snap".
     Keep names readable spoken English; the grid uses short_name.

2) COLLISION RENAMES in src/models/trs.c:
   - Line 273 `"name":"TRANS DEC"` (PK_TRS_TDEC) -> `"name":"Click Decay"`.
   - Line 275 `"name":"CURVE"` (PK_TRS_CURVE) -> `"name":"Sweep Curve"` (Kick 1 already owns
     the shared "CURVE"; this disambiguates TRS's own P2 curve).
   Give both a distinct short_name (e.g. "CLKDEC", "SWPCRV").

3) tests/test_readback.c — update assertions broken by Task 2's enum-option changes:
   - Line 118 `assert(strstr(ui, "\"options\":[\"FM2\"") != NULL);` — MODEL options are now
     spoken words. Change to assert the NEW spoken first option (e.g.
     `"\"options\":[\"FM 2-Op\""`) AND add an assertion that `"short_options":["FM2"` (or the
     chosen short array) is present, proving codes migrated to short_options.
   - Line 119 `"\"Diode\",\"Clip\",\"SAT\""` — if FX options changed (SAT->"Saturate"),
     update to match the new options string; if FX kept its codes, leave it. Match Task 2's
     actual choice.
   - Line 140 `"\"Synth\",\"Transient\",\"Both\""` — OPT_ROUTE is unchanged (already spoken),
     so this assertion should still pass; leave it unless Task 2 changed OPT_ROUTE.
   - Lines 121-123 assert presence of `"short_name":`, `"unit":`, `"step":` anywhere in the
     hierarchy — still satisfied (now also by engine params). Keep them.
   - Do not weaken unrelated assertions. The goal is: test_readback reflects the new accessible
     schema and passes.
  </action>
  <verify>
    <automated>make test 2>&1 | tail -5</automated>
  </verify>
  <done>
Every engine param in all 9 model p2_slot_desc interiors carries short_name + (for floats)
step + unit. trs.c TRANS DEC -> "Click Decay" and CURVE -> "Sweep Curve". test_readback.c
assertions updated for the new MODEL (and any changed) enum option arrays + short_options.
`make test` is GREEN across all 12 suites (test-dr32 included, unchanged). ui_hierarchy stays
valid JSON (test_switch/test_readback brace/bracket + null-termination checks pass).
  </done>
</task>

</tasks>

<verification>
- `make test` GREEN: all 12 suites, including test-dr32 (unchanged) and test-readback
  (updated). ui_hierarchy JSON stays brace/bracket-balanced and null-terminated across model
  switches (test_switch), and the rich-schema asserts (enum/options/short_options/default/
  short_name/unit/step) pass (test_readback).
- `bash -n scripts/glibc_gate.sh` and `bash -n scripts/deploy.sh` clean.
- help.json at repo root is byte-identical to `git show pr1:help.json`.
- `grep current_pad src/ui.c` returns nothing.
- CI/cross-compile/glibc-gate/deploy of dr32_engine.so are CI/hardware-verified (no Docker on
  this host) — verified here by inspection only.
</verification>

<success_criteria>
- Part 1: dr32_engine.so is gated (parameterized symbol), packaged by `make dist`, built +
  gated + uploaded by CI, and atomically deployed by deploy.sh — alongside dsp.so + help.json.
- Part 2: help.json shipped verbatim; ui_hierarchy uses spoken param/option names with codes
  preserved in short_name/short_options; every engine param has short_name/step/unit; the two
  TRS name collisions are resolved; current_pad is gone; no v0.4 Gen param regressed.
- No audio-thread / DSP math changed; module.json untouched (p-lock automation unaffected).
- `make test` GREEN.
</success_criteria>

<output>
After completion, create
`.planning/quick/261005-cla-dr32-shipping-path-glibc-gate-param-dist/261005-cla-SUMMARY.md`
</output>
