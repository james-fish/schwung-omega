---
phase: E1-critical-audio-fixes
plan: 02
type: execute
wave: 2
depends_on: ["E1-01"]
files_modified:
  - src/models/phy.c
  - src/models/dig.c
  - src/models/hrd.c
autonomous: true
requirements: ["#5", "#6", "#7"]

must_haves:
  truths:
    - "PHY CURVE=0 produces no pitch sweep (excitation at target freq); CURVE=1 produces 3x freq sweep"
    - "DIG BIT DEPTH knob turned up = MORE crushing (lower bit count), not less"
    - "HRD DRIVE engages audibly before 50% (fold starts at 30%)"
    - "HRD CRUSH is audible across the full 0-1 range (2-bit extreme at max)"
    - "make test passes with no regressions"
  artifacts:
    - path: "src/models/phy.c"
      provides: "Corrected sweep_mult formula"
      contains: "1.0f + p->curve * 2.0f"
    - path: "src/models/dig.c"
      provides: "Inverted BIT DEPTH direction"
      contains: "14.0f - v * 8.0f"
    - path: "src/models/hrd.c"
      provides: "Lowered DRIVE fold threshold + steepened CRUSH range"
      contains: "v > 0.3f"
  key_links:
    - from: "src/models/phy.c"
      to: "modal_excite in src/dsp_primitives.c"
      via: "head_start computed from sweep_mult * head_f"
      pattern: "sweep_mult = 1.0f \\+ p->curve \\* 2.0f"
    - from: "src/models/dig.c"
      to: "crush() in src/dsp_primitives.c"
      via: "bits_to_levels applied in dig_set_param"
      pattern: "14\\.0f - v \\* 8\\.0f"
---

<objective>
Fix three independent parameter-scaling bugs in phy.c, dig.c, and hrd.c:
- Bug #5: PHY CURVE formula has a hardcoded +0.8 offset that forces a minimum 1.8x sweep even at CURVE=0
- Bug #6: DIG BIT DEPTH direction is inverted — higher knob means cleaner (fewer bits of crush)
- Bug #7: HRD DRIVE fold threshold is 0.6 (barely engages); CRUSH range only goes to 4-bit (not aggressive enough at mid-knob)

Purpose: Independent single-file math corrections that make three controls audible and musical across their full range.
Output: Three model files with corrected formulas.
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
<!-- PHY CURVE: phy_trigger() in src/models/phy.c lines 239-240 -->
<!-- Current (broken): -->
float sweep_mult = 1.0f + (0.8f + 1.2f * p->curve);   /* always >= 1.8x */
float head_start = clampf(head_f * sweep_mult, 20.0f, 0.45f * OMEGA_SR);

<!-- Fixed (per E1-RESEARCH.md Bug #5): -->
float sweep_mult = 1.0f + p->curve * 2.0f;             /* 0=no sweep, 1=3x sweep */
float head_start = clampf(head_f * sweep_mult, 20.0f, 0.45f * OMEGA_SR);

<!-- DIG BIT DEPTH: dig_set_param() in src/models/dig.c lines 158-161 -->
<!-- Current (broken — higher v = cleaner): -->
float bits = 6.0f + v * (14.0f - 6.0f);   /* v=0 -> 6-bit CRUSH, v=1 -> 14-bit clean */

<!-- Fixed (per E1-RESEARCH.md Bug #6 — higher v = more crush): -->
float bits = 14.0f - v * 8.0f;            /* v=0 -> 14-bit clean, v=1 -> 6-bit CRUSH */

<!-- HRD DRIVE: hrd_set_param() in src/models/hrd.c lines 176-178 -->
<!-- Current (broken — fold doesn't engage until 0.6): -->
h->drive_mode = (v > 0.6f) ? FX_FOLD : FX_SAT;

<!-- Fixed (per E1-RESEARCH.md Bug #7 Option A): -->
h->drive_mode = (v > 0.3f) ? FX_FOLD : FX_SAT;

<!-- HRD CRUSH: hrd_set_param() in src/models/hrd.c line 184 -->
<!-- Current (bits go 16..4, min 4-bit at max crush): -->
float bits = 16.0f - v * (16.0f - 4.0f);  /* v=1 -> 4-bit */

<!-- Fixed (bits go 16..2, more aggressive at max): -->
float bits = 16.0f - v * (16.0f - 2.0f);  /* v=1 -> 2-bit, v=0.5 -> 9-bit (more audible) */
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Fix PHY CURVE sweep formula and DIG BIT DEPTH direction</name>
  <read_first>
    src/models/phy.c
    src/models/dig.c
  </read_first>
  <files>src/models/phy.c, src/models/dig.c</files>
  <action>
**phy.c — Fix Bug #5 (PHY CURVE inverted / too aggressive):**

In `phy_trigger()` (around line 239), find:
```c
float sweep_mult = 1.0f + (0.8f + 1.2f * p->curve);   /* 909 sweeps deeper */
```
Replace with:
```c
float sweep_mult = 1.0f + p->curve * 2.0f;   /* curve=0: no sweep; curve=1: 3x sweep */
```
The `head_start = clampf(head_f * sweep_mult, ...)` line below stays unchanged.

This makes CURVE=0 excite at the target frequency (pure modal thud, no pitch drop). CURVE=0.5 excites at 2x target (a moderate 909-style sweep). CURVE=1.0 excites at 3x target (deep drop). Update the comment on the surrounding block to reflect the new semantics.

**dig.c — Fix Bug #6 (DIG BIT DEPTH direction inverted):**

In `dig_set_param()` where `PK_DIG_BITDEPTH` is handled (around line 158-161), find:
```c
float bits = 6.0f + v * (14.0f - 6.0f);
```
Replace with:
```c
float bits = 14.0f - v * 8.0f;   /* v=0 -> 14-bit clean, v=1 -> 6-bit maximum crush */
```
Update the comment above the line (currently "Higher v = MORE bits = cleaner") to read: "Higher v = FEWER bits = crunchier (v=0: 14-bit clean, v=1: 6-bit crush)."

The next line `d->bit_levels = bits_to_levels(bits);` stays unchanged.
  </action>
  <verify>
    <automated>grep -n "sweep_mult" "/Users/jamesfish/Vibecoding Projects/Schwung/Omega/src/models/phy.c" && grep -n "14\.0f - v \* 8" "/Users/jamesfish/Vibecoding Projects/Schwung/Omega/src/models/dig.c"</automated>
  </verify>
  <acceptance_criteria>
    - `grep -n "sweep_mult" src/models/phy.c` shows `float sweep_mult = 1.0f + p->curve * 2.0f;` and no `0.8f` constant
    - `grep -n "0\.8f + 1\.2f" src/models/phy.c` returns no matches
    - `grep -n "14\.0f - v \* 8" src/models/dig.c` shows a match
    - `grep -n "6\.0f + v \*" src/models/dig.c` returns no matches
  </acceptance_criteria>
  <done>PHY CURVE=0 no longer has a minimum 1.8x sweep. DIG BIT DEPTH knob direction is corrected: turning up increases crush.</done>
</task>

<task type="auto">
  <name>Task 2: Fix HRD DRIVE fold threshold and CRUSH range, then run make test</name>
  <read_first>
    src/models/hrd.c
  </read_first>
  <files>src/models/hrd.c</files>
  <action>
**hrd.c — Fix Bug #7 (DRIVE fold threshold + CRUSH range):**

Fix 1 — DRIVE fold threshold (around line 177):
Find:
```c
h->drive_mode = (v > 0.6f) ? FX_FOLD : FX_SAT;
```
Replace with:
```c
h->drive_mode = (v > 0.3f) ? FX_FOLD : FX_SAT;
```
This makes aggressive Fold distortion start at 30% DRIVE instead of 60%, so the user hears a character change much earlier in the knob sweep.

Fix 2 — CRUSH range steepening (around line 184):
Find:
```c
float bits = 16.0f - v * (16.0f - 4.0f);
```
Replace with:
```c
float bits = 16.0f - v * (16.0f - 2.0f);   /* v=0: 16-bit (clean), v=1: 2-bit (extreme) */
```
This extends the range from 16..4 bits to 16..2 bits. At v=0.5 (the previous default), bits go from 10-bit to 9-bit — more audible. At v=1.0, the crush is now extreme (2-bit = 4 quantization levels).

Update the comment (currently "v=0 -> 16 bits (clean/off), v=1 -> 4 bits") to: "v=0 -> 16-bit (clean), v=1 -> 2-bit (extreme crush)".

After editing hrd.c, run `make test` to verify no regressions.
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && grep -n "v > 0\.3f" src/models/hrd.c && grep -n "16\.0f - 2\.0f" src/models/hrd.c && make test 2>&1 | tail -20</automated>
  </verify>
  <acceptance_criteria>
    - `grep -n "v > 0\.3f" src/models/hrd.c` shows a match in the DRIVE branch
    - `grep -n "v > 0\.6f" src/models/hrd.c` returns no match (old threshold gone)
    - `grep -n "16\.0f - 2\.0f" src/models/hrd.c` shows a match in the CRUSH branch
    - `grep -n "16\.0f - 4\.0f" src/models/hrd.c` returns no match (old range gone)
    - `make test` exits 0 with all suites reporting PASSED
  </acceptance_criteria>
  <done>HRD DRIVE engages fold distortion at 30%. HRD CRUSH is audible across the full range (2-bit maximum at knob max). make test GREEN.</done>
</task>

</tasks>

<verification>
After both tasks complete:
1. `grep -n "0.8f + 1.2f" src/models/phy.c` — no matches
2. `grep -n "6.0f + v \*" src/models/dig.c` — no matches
3. `grep -n "v > 0.6f" src/models/hrd.c` — no matches
4. `make test` — exits 0
</verification>

<success_criteria>
PHY CURVE is linear from no-sweep (CURVE=0) to 3x-sweep (CURVE=1.0). DIG BIT DEPTH turns up = more crush. HRD DRIVE engages fold at 30%, CRUSH is extreme at 2-bit maximum. All tests green.
</success_criteria>

<output>
After completion, create `.planning/phases/E1-critical-audio-fixes/E1-02-SUMMARY.md`
</output>
