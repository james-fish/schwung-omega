---
phase: E1-critical-audio-fixes
plan: 03
type: execute
wave: 2
depends_on: ["E1-01"]
files_modified:
  - src/groove.c
  - src/params.c
autonomous: true
requirements: ["#3", "#17"]

must_haves:
  truths:
    - "TAPS groove output is audible at grv_vol=1.0 without requiring extra DRIVE"
    - "Soft clipper is OFF by default (new instances start with clip_on = false)"
    - "HRD CRUSH default is 0.0 (transparent at startup, not 0.5 = 10-bit)"
    - "make test passes with no regressions"
  artifacts:
    - path: "src/groove.c"
      provides: "Increased tap output gain so TAPS level-matches the kick at default settings"
      contains: "tap_gain\|TAP_GAIN\|3.0f\|makeup"
    - path: "src/params.c"
      provides: "GKI_CLIP default 0.0f, PKI_HRD_CRUSH default 0.0f"
      contains: "GKI_CLIP]=0.0f"
  key_links:
    - from: "src/params.c"
      to: "src/dsp.c omega_create"
      via: "g_global_defaults seeds global_cache which seeds clip_on at startup"
      pattern: "GKI_CLIP\\]=0\\.0f"
    - from: "src/groove.c"
      to: "src/dsp.c render_block"
      via: "groove_tick output gl/gr summed into output buffer"
      pattern: "gl \\*= g->vol"
---

<objective>
Fix Bug #3 (TAPS groove too quiet) by applying a tap makeup gain multiplier in groove.c, and fix Bug #17 (soft clipper sounds bad) + HRD CRUSH wrong default by changing params.c defaults.

Purpose: These two files are orthogonal to E1-02. Both can be applied in parallel with E1-02 since they touch different source files.
Output: groove.c with corrected tap gain; params.c with CLIP=0 and HRD_CRUSH=0 defaults.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/phases/E1-critical-audio-fixes/E1-RESEARCH.md

<interfaces>
<!-- TAPS gain path in groove.c groove_tick (around lines 290-354): -->
/*
 * Current tap sum (lines 290-294):
 *   for (int t = 0; t < 4; t++) {
 *       float w = g->tap_level[t] * g->tap_decay[t];
 *       gl += g->buf_l[rp] * w;
 *       gr += g->buf_r[rp] * w;
 *   }
 * Then (line 354): gl *= g->vol; gr *= g->vol;
 *
 * At default tap_level=0.6, tap_decay[0]=0.625 (LENGTH=0.5, base^1):
 *   4-tap sum ≈ 0.85 × ring-content × vol
 * This is 40-50% quieter than the kick at typical vol=1.0.
 * Fix: add a constant tap makeup gain of 3.0 AFTER the tap loop, BEFORE
 * the filter/drive/reverb chain and BEFORE the vol scaling.
 * This raises the tap sum to ~2.5x ring-content, closer to kick level.
 */

<!-- params.c relevant defaults (lines 38, 73): -->
/*
 * Current defaults (to change):
 *   [PKI_HRD_CRUSH]=0.5f,           line 38  -> change to 0.0f
 *   [GKI_CLIP]=1.0f,                line 73  -> change to 0.0f
 *
 * These are in two separate arrays:
 *   g_kick_defaults[MODEL_COUNT][PKI_COUNT]  (line 38 is inside this array)
 *   g_global_defaults[GKI_COUNT]             (line 73 is inside this array)
 *
 * The UI schema default string in src/ui.c may also reference the CLIP
 * default as "1" — check and update to "0" if present (search: PK_CLIP.*"1").
 */
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Add tap makeup gain in groove.c to level-match the kick</name>
  <read_first>
    src/groove.c
  </read_first>
  <files>src/groove.c</files>
  <action>
In `groove_tick()`, after the 4-tap sum loop (the `for (int t = 0; t < 4; t++)` block that accumulates `gl` and `gr`), add a tap makeup gain multiplier. The tap loop ends around line 295 with the write_pos increment.

Find the block after the tap loop (before the FILTER section):
```c
        g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;
    }

    /* FILTER (C1-02): the COLOR one-pole ... */
```

Insert after `g->write_pos = ...` and before `/* FILTER */`:
```c
    /* TAP MAKEUP GAIN: the 4-tap weighted sum at default settings (tap_level=0.6,
     * LENGTH=0.5) produces ~0.85 of ring content. Multiply by 3.0 so the TAPS
     * voice level-matches the kick at grv_vol=1.0 without the user needing to
     * add DRIVE. This gain is before the filter/drive/reverb/vol chain, so those
     * controls retain their full range. (Bug #3 fix.) */
    gl *= 3.0f;
    gr *= 3.0f;
```

This multiplication happens only in the TAPS path (the `if (g->type == GRV_TYPE_TAPS)` block — check that the tap loop is inside that branch). If the tap loop is inside a conditional block for TAPS type, confirm the `gl *= 3.0f` is also inside that block.

After the edit, confirm the multiplication is placed AFTER the tap sum but BEFORE the `/* FILTER */` section (so the filter, drive, reverb, and vol all operate on the boosted signal).
  </action>
  <verify>
    <automated>grep -n "3\.0f" "/Users/jamesfish/Vibecoding Projects/Schwung/Omega/src/groove.c"</automated>
  </verify>
  <acceptance_criteria>
    - `grep -n "gl \*= 3\.0f" src/groove.c` shows a match after the tap loop
    - `grep -n "gr \*= 3\.0f" src/groove.c` shows a match
    - The `gl *= 3.0f` line appears BEFORE `/* FILTER */` in the file (verify with grep line numbers that the makeup gain line number is less than the FILTER comment line number)
    - The `gl *= 3.0f` line appears AFTER the tap for-loop's closing brace (tap loop line < makeup gain line)
    - `grep -n "TAP MAKEUP GAIN" src/groove.c` shows the comment
  </acceptance_criteria>
  <done>TAPS groove tap output is multiplied by 3.0 after accumulation, before filter/drive/vol, making it audible at default settings without extra DRIVE.</done>
</task>

<task type="auto">
  <name>Task 2: Fix params.c defaults (CLIP=OFF, HRD_CRUSH=0) and run make test</name>
  <read_first>
    src/params.c
    src/ui.c
  </read_first>
  <files>src/params.c, src/ui.c</files>
  <action>
**params.c — Fix two default values:**

Change 1 — HRD CRUSH default (around line 38, inside `g_kick_defaults`):
Find:
```c
[PKI_HRD_CRUSH]=0.5f,
```
Replace with:
```c
[PKI_HRD_CRUSH]=0.0f,
```
Rationale: CRUSH was transparent at 0.0f (16-bit) but the default 0.5f was placing the user at 9-bit immediately on load, which sounds bad without intent. Default transparent means CRUSH is an opt-in control.

Change 2 — Soft clip default (around line 73, inside `g_global_defaults`):
Find:
```c
[GKI_DJ_FILT]=0.5f, [GKI_DJ_RESO]=0.0f, [GKI_CLIP]=1.0f,
```
Replace with:
```c
[GKI_DJ_FILT]=0.5f, [GKI_DJ_RESO]=0.0f, [GKI_CLIP]=0.0f,
```
Rationale: The `x/(1+|x|)` soft clipper attenuates even moderate levels (33% at amplitude 0.5). Default OFF lets the user explicitly engage it. Matches Phase E1 success criterion SC-7.

**ui.c — Check if CLIP default schema string needs updating:**

Search ui.c for any hardcoded `"1"` default value for PK_CLIP. Run:
```
grep -n "PK_CLIP\|CLIP.*\"1\"\|\"1\".*CLIP\|clip.*default" src/ui.c
```

If ui.c has an explicit `"default":"1"` string for the CLIP param, change it to `"default":"0"`. The `ui_default_for()` function reads from `g_global_defaults` at runtime, so the schema string should stay in sync. If `ui_default_for` is the only path and there's no hardcoded default string, no change is needed in ui.c.

After both changes, run `make test`.
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && grep -n "PKI_HRD_CRUSH\]=0\.0f\|GKI_CLIP\]=0\.0f" src/params.c && make test 2>&1 | tail -20</automated>
  </verify>
  <acceptance_criteria>
    - `grep -n "PKI_HRD_CRUSH\]=0\.0f" src/params.c` shows a match
    - `grep -n "PKI_HRD_CRUSH\]=0\.5f" src/params.c` returns no matches (old value gone)
    - `grep -n "GKI_CLIP\]=0\.0f" src/params.c` shows a match
    - `grep -n "GKI_CLIP\]=1\.0f" src/params.c` returns no matches (old value gone)
    - `make test` exits 0 with all suites reporting PASSED
    - `make test` output contains `test_perf: ALL TESTS PASSED` (confirms the CLIP toggle test still works with new OFF default)
  </acceptance_criteria>
  <done>HRD CRUSH starts at 0.0 (transparent / off). Soft clipper starts at 0.0 (off). make test GREEN including test_perf which exercises the CLIP toggle.</done>
</task>

</tasks>

<verification>
After both tasks:
1. `grep -n "GKI_CLIP\]=0\.0f" src/params.c` — shows match
2. `grep -n "PKI_HRD_CRUSH\]=0\.0f" src/params.c` — shows match
3. `grep -n "gl \*= 3\.0f" src/groove.c` — shows match
4. `make test` exits 0
</verification>

<success_criteria>
TAPS groove is audible at default VOL=1.0 without extra drive. Soft clip default is OFF. HRD CRUSH default is transparent. All tests GREEN.
</success_criteria>

<output>
After completion, create `.planning/phases/E1-critical-audio-fixes/E1-03-SUMMARY.md`
</output>
