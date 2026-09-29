---
phase: B-remaining-9-kick-models
plan: 09
type: execute
wave: 8
depends_on: ["B-01", "B-03", "B-04", "B-05", "B-06", "B-07", "B-08"]
files_modified:
  - src/ui.c
  - tests/test_switch.c
  - docs/VOICING_AUDIT.md
autonomous: false
requirements: [KICK-13]
must_haves:
  truths:
    - "Kick Page 2 assembles the ACTIVE model's 6-slot descriptor dynamically from its p2_slot_desc (spliced by ui.c) + FX TYPE/AMT — each model shows its own slots (KICK-13 SC3)"
    - "Every model's p2_slot_desc emits valid, bounded JSON with the correct slot count (parseable, no overflow)"
    - "docs/VOICING_AUDIT.md exists: a 10-model x D-B02-checklist matrix (FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN) with the exact on-device deploy+audition commands"
    - "The on-device manual voicing sign-off (D-B02 manual items) is surfaced as a MANUAL human-verify checkpoint, not an automated pass (D-B04)"
  artifacts:
    - path: "src/ui.c"
      provides: "kick2 level spliced from active model's p2_slot_desc + FX TYPE/AMT (replaces the FM2-inlined kick2)"
      contains: "p2_slot_desc"
    - path: "docs/VOICING_AUDIT.md"
      provides: "10-model x D-B02-checklist PASS/PENDING matrix + deploy/audition runbook (D-B04)"
      contains: "VOICING"
  key_links:
    - from: "src/ui.c omega_build_ui kick2 level"
      to: "g_models[inst->model]->p2_slot_desc(inst, ...)"
      via: "dynamic splice of the active model's slots"
      pattern: "p2_slot_desc"
    - from: "docs/VOICING_AUDIT.md"
      to: "on-device manual sign-off for all 10 models"
      via: "human-verify checkpoint (autonomous:false)"
      pattern: "PENDING"
---

<objective>
Complete KICK-13 by making Kick Page 2 assemble DYNAMICALLY from the active model's `p2_slot_desc` (ui.c currently INLINES FM2's Page-2 — it must splice the active model's slots so each of the 10 models shows its own 6 slots + FX TYPE/AMT). Then produce the phase's first-class voicing deliverable: `docs/VOICING_AUDIT.md`, a 10-model x D-B02-checklist matrix, and pause at a MANUAL on-device human-verify checkpoint for the D-B02 ear round (D-B04), mirroring the A-04 hardware checkpoint.

Purpose: The automated batteries (B-03..B-08) prove each model is distinct/bounded/param-responsive; this plan wires the per-model Page-2 UI and gates the phase on the on-device voicing sign-off (the ear is the authority, per D-B01/D-B02).
Output: Dynamic kick2 splice in ui.c, a p2_slot_desc JSON-validity test, docs/VOICING_AUDIT.md, and a blocking human-verify checkpoint.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/B-remaining-9-kick-models/B-RESEARCH.md
@.planning/phases/B-remaining-9-kick-models/B-VALIDATION.md
@.planning/phases/B-remaining-9-kick-models/B-CONTEXT.md
@src/ui.c
@src/omega.h
@src/models/fm2.c
@docs/ON_DEVICE_VALIDATION.md
@scripts/deploy.sh

<interfaces>
<!-- ui.c current kick2 (UI_KICK2) INLINES FM2's 5 Page-2 params (src/ui.c lines
     63-72). Replace with a dynamic splice: call g_models[inst->model]->p2_slot_desc
     into a bounded scratch, then append the FX TYPE/AMT slots + knobs. Each model's
     p2_slot_desc (B-03..B-08) now emits full {"key","name","type","min","max"} objects
     (research Pattern 3), so ui.c just wraps them into the kick2 level.
     omega_build_ui(inst, buf, buf_len) is bounded (ui_append reserves the terminator).
     docs/ON_DEVICE_VALIDATION.md is the A-04 runbook template to mirror. -->
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Dynamic Kick Page 2 splice from active model's p2_slot_desc (KICK-13 SC3)</name>
  <read_first>src/ui.c (UI_KICK2 inline, ui_append bounded-copy helper, omega_build_ui assembly), src/models/fm2.c (fm2_p2_slot_desc format), src/models/wtr.c + others (their p2_slot_desc full-object format from B-04..B-08), .planning/phases/B-remaining-9-kick-models/B-RESEARCH.md (Pattern 3 dynamic assembly + reconcile key,label vs key,name,type,min,max)</read_first>
  <files>src/ui.c</files>
  <action>
    Replace the FM2-inlined `UI_KICK2` with a DYNAMIC assembly of the active model's Page-2 (research Pattern 3):
    1. In `omega_build_ui`, build the kick2 level by: emitting the static prefix `"\"kick2\":{\"name\":\"Kick 2\",\"params\":["`, then calling `g_models[inst->model]->p2_slot_desc(inst, scratch, sizeof scratch)` into a bounded local scratch buffer (the models now emit the interior slot objects as full `{"key","name","type","min","max"}` -- verify the exact bracket convention each B-04..B-08 model used and reconcile: if models emit a bare comma-separated object list, append directly; if they wrap in `[...]`, strip the outer brackets like the A-03 pattern described in STATE.md `[A-03] Kick Page 2 spliced from the model`). Append the spliced interior, then append the FX TYPE/AMT slot objects (`PK_FX_TYPE`/`PK_FX_AMT`), close `"]"`, add a `"knobs"` array, close the level.
    2. Keep every write bounded through the existing `ui_append` helper (reserves the terminator; A-RESEARCH Pitfall 3). The scratch buffer for the splice must be a fixed local array (no allocation) sized conservatively (all 10 fragments are small; on-device buf_len cap still PENDING -- size the scratch e.g. 1024 and keep fragments well under it).
    3. Because model p2_slot_desc now returns full objects (B-03..B-08), FM2's `fm2_p2_slot_desc` must ALSO emit the full-object format to match (if B-03 left it in the old key,label form, update fm2_p2_slot_desc to the full key,name,type,min,max form -- reconcile so all 10 are uniform). Do this reconciliation here if not already done.
    4. Do NOT touch the ABI, dsp.c dispatch, or the DSP. No logging/alloc/file-IO in ui.c (audio thread).
    Result: switching MODEL and re-querying ui_hierarchy shows that model's own 6 slots + FX TYPE/AMT.
  </action>
  <acceptance_criteria>
    - `grep -q 'p2_slot_desc' src/ui.c` (ui.c now splices the active model's descriptor)
    - The static FM2-only `UI_KICK2` inline block is removed/replaced (`! grep -q 'FM RATIO' src/ui.c` OR the FM2 params are no longer hardcoded in ui.c)
    - `grep -q 'PK_FX_TYPE' src/ui.c && grep -q 'PK_FX_AMT' src/ui.c` (FX slots still appended)
    - ui.c uses a fixed local scratch (no malloc; `! grep -q 'malloc\|calloc' src/ui.c`)
    - `make test` exits 0 (get_param contract test still green)
  </acceptance_criteria>
  <verify>
    <automated>make test && grep -q 'p2_slot_desc' src/ui.c && echo SPLICE_OK</automated>
  </verify>
  <done>Kick Page 2 assembles dynamically from the active model's p2_slot_desc + FX TYPE/AMT; all 10 models emit uniform full-object slot JSON; bounded, no allocation; suite green.</done>
</task>

<task type="auto">
  <name>Task 2: p2_slot_desc JSON-validity + slot-count test for all 10 models (KICK-13)</name>
  <read_first>tests/test_switch.c (KICK-13 harness from B-01), src/ui.c (Task 1 splice), src/models/*.c (each p2_slot_desc), .planning/phases/B-remaining-9-kick-models/B-VALIDATION.md (KICK-13 "each p2_slot_desc emits valid bounded JSON with correct slot count")</read_first>
  <files>tests/test_switch.c</files>
  <action>
    Extend `tests/test_switch.c` (or add asserts) so that for EVERY registered model: call `g_models[m]->p2_slot_desc(inst, buf, sizeof buf)` and assert:
    - returns > 0 and <= sizeof buf (bounded, no overflow);
    - the emitted string is balanced JSON (equal counts of `{` and `}`, equal `[`/`]` if wrapped) and null-terminated;
    - the slot count (count of `"key"` occurrences) matches the model's expected count (FM4=6, others=4, GEN=3-ish -- assert the documented count per model);
    - calling with a too-small buf_len returns 0 (bounded refusal, no write past buffer -- the Pattern 3 overflow guard).
    Also assert `omega_build_ui` produces a balanced-brace, null-terminated string for each model after `set_param(PK_MODEL, ...)` (the full hierarchy stays valid across model switches).
    Plain C assert; no framework.
  </action>
  <acceptance_criteria>
    - `tests/test_switch.c` contains p2_slot_desc bounded/JSON-balance/slot-count assertions
    - too-small buf_len returns 0 assertion present
    - `make test` exits 0 covering all 10 registered models
  </acceptance_criteria>
  <verify>
    <automated>make test && echo P2JSON_OK</automated>
  </verify>
  <done>Every model's p2_slot_desc is proven to emit valid, bounded, correctly-counted JSON and to refuse overflow; the full ui_hierarchy stays balanced across model switches; suite green.</done>
</task>

<task type="auto">
  <name>Task 3: Create docs/VOICING_AUDIT.md (D-B04) — 10-model x checklist matrix + deploy runbook</name>
  <read_first>docs/ON_DEVICE_VALIDATION.md (A-04 runbook template to mirror), scripts/deploy.sh (the deploy loop), .planning/phases/B-remaining-9-kick-models/B-CONTEXT.md (D-B02 checklist verbatim + D-B04), B-VALIDATION.md (Manual-Only Verifications table)</read_first>
  <files>docs/VOICING_AUDIT.md</files>
  <action>
    Create `docs/VOICING_AUDIT.md` mirroring the A-04 runbook style (docs/ON_DEVICE_VALIDATION.md). Contents:
    1. A header stating this is the D-B04 phase-completion gate: Phase B is NOT complete until every model PASSES the D-B02 manual items on-device.
    2. The exact deploy+audition runbook (from B-CONTEXT specifics + A-04 loop): obtain a gate-passing dsp.so (CI artifact or Docker build + `scripts/glibc_gate.sh`), `scripts/deploy.sh` (atomic scp), load in a Schwung slot, select each MODEL via the root Model encoder, trigger, and audition. Bake the literal commands in.
    3. A 10-model x D-B02-manual-checklist MATRIX (rows = FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN; columns = the 5 D-B02 manual items VERBATIM from B-CONTEXT: (a) default sounds like a usable techno kick, (b) PITCH/CURVE 808-909 sounds musical, (c) each knob sweeps a musically useful range, (d) distinct character vs the others, (e) no live clip/zipper/artifacts). Each cell initialized to `PENDING (on-device)`.
    4. A note that FM2 (D-B03) is the reference bar -- sign it off first.
    5. A re-map guidance line: if a model fails an item on-device, re-map its param min/max or response curve in its model .c (D-B02 "re-map as needed"), rebuild, redeploy, re-audition.
    Do NOT fabricate PASS results -- all cells stay PENDING until a human fills them on-device (mirrors A-04, which does not fabricate hardware results).
  </action>
  <acceptance_criteria>
    - `docs/VOICING_AUDIT.md` exists and contains all 10 model names (FM2..GEN) as matrix rows
    - Contains the 5 D-B02 manual checklist items as columns/criteria
    - Contains `scripts/deploy.sh` and `scripts/glibc_gate.sh` in the runbook commands
    - Every matrix cell is `PENDING` (no fabricated PASS): `grep -c 'PENDING' docs/VOICING_AUDIT.md` >= 10
  </acceptance_criteria>
  <verify>
    <automated>test -s docs/VOICING_AUDIT.md && grep -q 'GEN' docs/VOICING_AUDIT.md && grep -q 'PENDING' docs/VOICING_AUDIT.md && echo AUDITDOC_OK</automated>
  </verify>
  <done>docs/VOICING_AUDIT.md is a 10-model x D-B02-checklist matrix (all PENDING) with the exact deploy/audition runbook; FM2 flagged as the reference bar; no fabricated results.</done>
</task>

<task type="checkpoint:human-verify" gate="blocking">
  <name>Task 4: On-device voicing sign-off (D-B02 manual / D-B04) — MANUAL human verification</name>
  <files>docs/VOICING_AUDIT.md</files>
  <action>
    This is a MANUAL human-verify checkpoint (autonomous:false), NOT an automated pass (D-B04). All prior automated work is complete; pause here for the on-device D-B02 ear round. The human deploys the gate-passing dsp.so to the Move, auditions each of the 10 models against the D-B02 manual checklist, records PASS or the specific issue in docs/VOICING_AUDIT.md, and re-voices any failing model (re-map param ranges/curves, rebuild, redeploy) until all cells read PASS. See <what-built> / <how-to-verify> below for the full runbook. Expected to pause pending Move hardware (mirrors A-04).
  </action>
  <what-built>
    All 10 kick models (FM2 re-voiced + FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN) are implemented, registered, and pass the full AUTOMATED D-B02 battery (non-silent, param-responsive, bounded-at-extremes, distinct, clean model-switch re-init). Kick Page 2 assembles dynamically per model. docs/VOICING_AUDIT.md is ready with all cells PENDING.
    This is the manual ear round: the automated tests prove the engines FUNCTION and are DISTINCT/BOUNDED; only a human on the Move can confirm they sound MUSICAL (D-B01: "Phase A proved the models function; Phase B must make them sound musical"). This checkpoint is surfaced as MANUAL, not an automated pass (D-B04).
  </what-built>
  <how-to-verify>
    1. Obtain a gate-passing dsp.so: use the CI artifact, or build in Docker (`docker run --rm -v "$PWD:/workspace" -w /workspace ghcr.io/charlesvestal/schwung-builder:latest make dsp.so`) then `./scripts/glibc_gate.sh build/dsp.so` (must pass -- glibc <=2.35, no libmvec/_ZGV, single export).
    2. Deploy: `./scripts/deploy.sh` (atomic scp to the Move).
    3. Load Omega in a Schwung slot. For EACH of the 10 models (select via the root Model encoder -- the enum now lists all 10):
       - Trigger at default (12 o'clock) settings; confirm it sounds like a usable techno kick out of the box.
       - Sweep PITCH and CURVE (808-909); confirm musical, not clicky/muddy, decay feels right.
       - Sweep each of the model's Kick Page 2 knobs end-to-end; confirm a musically useful range (no dead zones, no all-the-action-in-last-5%).
       - A/B against the other models; confirm a distinct sonic character.
       - Turn knobs live during a sustained trigger; confirm no clipping/zipper/artifacts.
       - Sign off FM2 FIRST (D-B03 reference bar), then the rest.
       - Record PASS or the specific issue in the corresponding cell of docs/VOICING_AUDIT.md.
    4. If any model fails an item: re-map its param min/max or response curve in its model .c, rebuild, redeploy, re-audition (D-B02 "re-map as needed"). Iterate until PASS.
    5. Phase B is complete only when every cell in docs/VOICING_AUDIT.md reads PASS.
    NOTE: The macOS host has no Docker/device link (per A-04 STATE.md); this step requires the Move hardware and is expected to pause here pending hardware, exactly like A-04.
  </how-to-verify>
  <resume-signal>Type "approved" once docs/VOICING_AUDIT.md is filled PASS for all 10 models on-device, or describe which models/items need re-voicing.</resume-signal>
</task>

</tasks>

<verification>
- `make test` exits 0 (dynamic splice + p2_slot_desc JSON-validity across all 10 models).
- docs/VOICING_AUDIT.md exists with the 10-model x D-B02 matrix (all PENDING) + deploy runbook.
- The on-device D-B02 manual round is a blocking human-verify checkpoint (D-B04), not an automated pass.
</verification>

<success_criteria>
- Kick Page 2 shows the correct 6 model-specific slots (+ FX TYPE/AMT) assembled dynamically from each active model's p2_slot_desc (KICK-13 SC3), proven valid/bounded for all 10.
- docs/VOICING_AUDIT.md gates phase completion on the on-device manual voicing sign-off for all 10 models incl. FM2 (D-B04); the ear is the authority (D-B01/D-B02).
</success_criteria>

<output>
After completion, create `.planning/phases/B-remaining-9-kick-models/B-09-SUMMARY.md`
</output>
