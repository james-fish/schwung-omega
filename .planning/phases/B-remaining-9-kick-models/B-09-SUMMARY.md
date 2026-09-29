---
phase: B-remaining-9-kick-models
plan: 09
subsystem: page2-splice-and-voicing-audit
tags: [KICK-13, page2-splice, p2_slot_desc, dynamic-ui, json-validity, voicing-audit, D-B02, D-B04, checkpoint, human-verify, on-device]
requires:
  - "B-01: kick_model_vtable_t (p2_slot_desc fn ptr), MODEL_COUNT=10 registry, clean model-switch re-init, ui.c root Model enum"
  - "B-02: fx_config/fx_process + fx_state_t (the FX chain the FX TYPE/AMT slots drive)"
  - "B-03: fm2_p2_slot_desc (reconciled here to the uniform full-object form), reusable voicing battery"
  - "B-04..B-08: all nine other models' p2_slot_desc emitting the uniform bare comma-separated {key,name,type,min,max} interior; fully-populated 10/10 registry"
  - "A-03/A-04: omega_build_ui bounded ui_append pattern, docs/ON_DEVICE_VALIDATION.md runbook template, scripts/deploy.sh + scripts/glibc_gate.sh"
provides:
  - "Dynamic Kick Page 2 (KICK-13 SC3): ui.c omega_build_ui splices the ACTIVE model's p2_slot_desc interior between a static kick2 prefix and the always-present FX TYPE/AMT suffix, via a fixed 1024-byte local scratch (no allocation, audio-thread-safe). Switching MODEL and re-querying ui_hierarchy shows that model's own slots — replaces the FM2-inlined UI_KICK2 block"
  - "Uniform Page-2 JSON across all 10 models: fm2_p2_slot_desc reconciled from the old A-03 [{key,label}] form to the {key,name,type,min,max} bare comma-separated interior used by B-04..B-08, so ui.c splices every model identically"
  - "p2_slot_desc validity gate (tests/test_switch.c assert_p2_json_valid): for every registered model asserts bounded (0<len<sizeof), null-terminated, brace-balanced, bracket-free, correct slot count (FM2=3/FM4=6/rest=4/GEN=3), too-small-buffer refusal (returns 0), and full-ui_hierarchy brace/bracket balance + null-termination after each model switch; plus get_param unknown-key -> -1"
  - "docs/VOICING_AUDIT.md (D-B04): the phase-completion gate — a 10-model x 5-item D-B02 manual voicing matrix (all cells PENDING on-device) + the exact deploy/audition runbook (CI artifact / Docker build + scripts/glibc_gate.sh -> scripts/deploy.sh -> select each model via root Model encoder -> audition), FM2 flagged as the D-B03 reference bar"
affects:
  - "src/ui.c (omega_build_ui dynamic kick2 splice; UI_KICK2 replaced by UI_KICK2_PREFIX + UI_KICK2_FX; commit 0c01954)"
  - "src/models/fm2.c (fm2_p2_slot_desc reconciled to the uniform full-object form; commit 0c01954)"
  - "tests/test_switch.c (assert_p2_json_valid + get_param unknown-key assertion; commit 74de7b0)"
  - "docs/VOICING_AUDIT.md (new; commit 26f89a2)"
tech-stack:
  added: []
  patterns:
    - "Dynamic per-model UI splice through a fixed local scratch (CLAUDE.md zero-alloc on the audio thread): omega_build_ui calls g_models[inst->model]->p2_slot_desc into a 1024-byte stack buffer, then wraps the returned bare interior with a static prefix + FX suffix. The FX suffix leads with a comma; when a model emits no interior (unimplemented slot / scratch too small -> slot_len 0) the comma is dropped (UI_KICK2_FX + 1) so the params array stays valid JSON. Every write stays bounded through the existing ui_append (reserves the terminator)."
    - "Uniform vtable JSON contract: all 10 p2_slot_desc emit a BARE comma-separated list of full {key,name,type,min,max} objects (no outer brackets) so ui.c is model-agnostic. B-09 reconciled the lone FM2 outlier (A-03 [{key,label}] form) to this contract."
    - "JSON-validity by structural counting (no parser): balanced braces/brackets via char counts, slot count via \"key\" substring count, bracket-free interior assertion, poisoned-buffer null-termination check — a lightweight plain-C gate over every registered model + the full spliced hierarchy."
    - "Manual voicing surfaced as a blocking human-verify checkpoint (D-B04), not an automated pass: the audit doc holds all cells PENDING (on-device) and no PASS is fabricated — mirrors A-04's hardware-checkpoint pattern (the ear is the authority, D-B01/D-B02)."
key-files:
  created:
    - "docs/VOICING_AUDIT.md"
  modified:
    - "src/ui.c"
    - "src/models/fm2.c"
    - "tests/test_switch.c"
decisions:
  - "The FX TYPE/AMT suffix is emitted with a leading comma and spliced AFTER the model interior; when the model produces no interior the leading comma is dropped (UI_KICK2_FX + 1, length - 2) so the kick2 params array is always valid JSON. This keeps FX TYPE/AMT constant across all 10 models while letting the model own its own slots."
  - "fm2_p2_slot_desc was reconciled to the uniform full-object bare-interior form (the other nine models already used it per B-04..B-08). This was required for a single generic splice path in ui.c — the A-03 note about ui.c 'stopping calling' fm2_p2_slot_desc is now reversed: ui.c calls it again, in the uniform format."
  - "The kick2 knobs array lists only the FX macros (PK_FX_TYPE/PK_FX_AMT) rather than per-model keys, because the model-specific keys vary and the params array is the authoritative slot source; the host encoder->key mapping for dynamic pages is Phase E nav-tree scope, not B-09. The emitted JSON stays valid and the model slots are fully present in params."
  - "Slot counts asserted per model (FM2=3, FM4=6, all others=4, GEN=3) from the actual model sources, not a blanket count — FM4 legitimately exposes 6 operator slots and FM2/GEN expose fewer, so a per-model expected-count table is the correct gate."
  - "Task 4 (on-device voicing) is a blocking human-verify checkpoint and CANNOT be automated or fabricated: it requires physical Move hardware + a human listener. Execution STOPS here with the audit doc ready and all cells PENDING, exactly like A-04."
metrics:
  duration: "partial (autonomous tasks only — stopped at human-verify checkpoint)"
  tasks: "3 of 4 (Task 4 is the blocking on-device voicing checkpoint)"
  files: 4
  completed: "2026-09-29"
---

# Phase B Plan 09: Page-2 Splice & Voicing Audit Summary

**One-liner:** Kick Page 2 now assembles dynamically per model — `ui.c` splices the active model's `p2_slot_desc` (all 10 reconciled to a uniform full-object JSON form) into the kick2 level with a bounded no-alloc scratch, a new `test_switch.c` gate proves every model's Page-2 JSON is valid/bounded/correctly-counted and the full hierarchy stays balanced across switches, and `docs/VOICING_AUDIT.md` stages the on-device D-B02 ear round as a blocking human-verify checkpoint (all cells PENDING, no fabricated PASS).

## What Was Built

**Task 1 — Dynamic Kick Page 2 splice (KICK-13 SC3, commit 0c01954):** `ui.c`'s `omega_build_ui` previously INLINED FM2's Page-2 params in a static `UI_KICK2` block. It now assembles kick2 dynamically: a static `UI_KICK2_PREFIX` (opens the level + params array), then the ACTIVE model's `p2_slot_desc` interior spliced through a fixed 1024-byte local scratch (no allocation — audio-thread-safe per CLAUDE.md), then the static `UI_KICK2_FX` suffix (the two always-present FX TYPE/AMT slots + a knobs array + level close). The FX suffix leads with a comma to separate it from the model slots; when a model emits no interior (unimplemented slot or scratch overflow -> `slot_len == 0`) the leading comma is dropped (`UI_KICK2_FX + 1`) so the params array stays valid JSON. To make the splice generic, `fm2_p2_slot_desc` was reconciled from its old A-03 `[{key,label}]` bracketed form to the uniform bare comma-separated `{key,name,type,min,max}` interior that B-04..B-08 already emit — so all 10 models now feed one code path. Every write stays bounded through the existing `ui_append` (reserves the terminator).

**Task 2 — Page-2 JSON-validity gate (KICK-13, commit 74de7b0):** `tests/test_switch.c` gained `assert_p2_json_valid`, which for every registered model asserts its `p2_slot_desc`: returns `0 < len < sizeof(buf)` (bounded, no overflow), is null-terminated at `len` with no embedded NUL, has balanced braces and zero brackets (bare interior), emits exactly the documented slot count (`"key"` count == FM2=3 / FM4=6 / rest=4 / GEN=3, one `{` per slot), and REFUSES a too-small buffer (returns 0 — the Pattern 3 overflow guard). It also switches the live instance to each model and asserts the FULL `ui_hierarchy` stays brace/bracket-balanced + null-terminated + honors the bytes-written contract, and that the model's slots survive the splice. A `get_param` unknown-key assertion (`-> -1`, Pitfall 4) was added in `main`.

**Task 3 — Voicing audit doc (D-B04, commit 26f89a2):** `docs/VOICING_AUDIT.md`, mirroring the A-04 runbook, is the phase-completion gate: a 10-model (FM2..GEN) x 5-item D-B02 manual voicing matrix with every cell `PENDING (on-device)`, the exact deploy+audition runbook (CI artifact / Docker `make dsp.so` + `./scripts/glibc_gate.sh` -> `./scripts/deploy.sh` -> select each model via the root Model encoder -> audition the 5 items), FM2 flagged as the D-B03 reference bar (sign off first), re-map guidance for on-device failures, and USR-fallback / GEN-Phase-B-scope notes. No PASS is fabricated.

## Tasks Completed

| Task | Name | Commit | Files |
| ---- | ---- | ------ | ----- |
| 1 | Dynamic Kick Page 2 splice from active model's p2_slot_desc (KICK-13 SC3) | 0c01954 | src/ui.c, src/models/fm2.c |
| 2 | p2_slot_desc JSON-validity + slot-count test for all 10 models (KICK-13) | 74de7b0 | tests/test_switch.c |
| 3 | Create docs/VOICING_AUDIT.md (D-B04) — 10-model x checklist matrix + runbook | 26f89a2 | docs/VOICING_AUDIT.md |
| 4 | On-device voicing sign-off (D-B02 / D-B04) — MANUAL human-verify | — | BLOCKING CHECKPOINT (on-device) |

## Verification

- `make test` exits 0 — all suites green:
  - `test_fm2`, `test_fx`: pass.
  - `test_switch`: **100 pairs** + `p2_slot_desc JSON valid for 10 models` (the new B-09 gate) + get_param unknown-key -> -1.
  - `test_params`: all 10 models' voicing batteries pass.
  - `test_distinct`: **10 registered / 45 pairs**.
  - `test_gen`: determinism + USR load/fallback + complete-registry all pass.
  - `test_render`: full lifecycle + per-model loop.
- Task 1 greps: `p2_slot_desc` present in `src/ui.c` (5), `FM RATIO` inline gone (0), `PK_FX_TYPE`/`PK_FX_AMT` present (2 each), no `malloc`/`calloc` in `ui.c` (0).
- Task 3 greps: `docs/VOICING_AUDIT.md` non-empty; all 10 model rows (FM2..GEN) present; `PENDING` on 20 lines (50 matrix cells); `scripts/deploy.sh` (3) + `scripts/glibc_gate.sh` (1) in the runbook.
- Cross-build + glibc gate: deferred to CI (no local Docker on macOS) — consistent with A-04/B-04..B-08. The ui.c splice adds no transcendentals and no new libmvec `_ZGV*` risk.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] FM2 Page-2 JSON format reconciled to match the other nine models**
- **Found during:** Task 1.
- **Issue:** The plan's generic splice requires all 10 `p2_slot_desc` to emit the same bare comma-separated `{key,name,type,min,max}` interior. B-04..B-08 already did, but `fm2_p2_slot_desc` still emitted the old A-03 `[{key,label}]` bracketed form — splicing it unmodified would have injected stray brackets and a wrong (label-only) slot shape into the kick2 params array.
- **Fix:** Reconciled `fm2_p2_slot_desc` to the uniform full-object bare-interior form (the plan explicitly directs this in Task 1 step 3). This reverses the A-03 note that ui.c "stops calling" fm2's descriptor — ui.c now calls it again, uniformly.
- **Files modified:** src/models/fm2.c
- **Commit:** 0c01954

## Known Stubs

None. The Page-2 splice is fully wired for all 10 models and proven valid by `test_switch`. `docs/VOICING_AUDIT.md`'s PENDING cells are NOT stubs — they are the intentional, un-fakeable on-device manual sign-off (D-B04), the same pattern as `docs/ON_DEVICE_VALIDATION.md` from A-04; they are resolved by a human at the Move, not by code.

## Checkpoint — Task 4 (blocking, on-device)

Execution STOPPED at Task 4, a `checkpoint:human-verify` (`autonomous: false`). All automatable work (Tasks 1-3) is complete and committed; `make test` is green. Task 4 is the D-B02 manual ear round for all 10 models, which requires physical Move hardware + a human listener and CANNOT be automated or fabricated (macOS host has no Docker / device link, per A-04). The audit doc is ready with all cells PENDING; the runbook is exact. Phase B is complete only when every cell in `docs/VOICING_AUDIT.md` reads PASS.

## Self-Check: PASSED

- src/ui.c FOUND (dynamic splice, p2_slot_desc x5, no malloc)
- src/models/fm2.c FOUND (reconciled p2_slot_desc)
- tests/test_switch.c FOUND (assert_p2_json_valid)
- docs/VOICING_AUDIT.md FOUND (10-model x 5-item matrix, all PENDING)
- commit 0c01954 FOUND (Task 1)
- commit 74de7b0 FOUND (Task 2)
- commit 26f89a2 FOUND (Task 3)
