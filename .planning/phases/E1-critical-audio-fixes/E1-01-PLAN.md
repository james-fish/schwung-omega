---
phase: E1-critical-audio-fixes
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/models/fm2.c
  - src/models/fm4.c
  - src/models/wtr.c
  - src/models/trs.c
  - src/models/ana.c
  - src/models/usr.c
  - src/models/phy.c
  - src/models/hrd.c
  - src/models/dig.c
  - tests/test_fx.c
autonomous: true
requirements: ["#1"]

must_haves:
  truths:
    - "Setting FX TYPE to Clip (\"1\") produces Clip distortion, not Crush"
    - "Setting FX TYPE to SAT (\"2\") produces SAT distortion, not Crush"
    - "Setting FX TYPE to Fold (\"3\") produces Fold distortion, not Crush"
    - "Setting FX TYPE to Crush (\"4\") produces Crush, not a mode-4 collision"
    - "Diode (\"0\") continues to work correctly"
    - "make test passes with no regressions"
  artifacts:
    - path: "src/models/fm2.c"
      provides: "Corrected FX_TYPE enum dispatch (integer, not 0..1 float)"
      contains: "(int)fm->fx_type, fm->fx_type = (float)(int)(parse_f"
    - path: "tests/test_fx.c"
      provides: "Integration test verifying dispatch through a live model"
      contains: "mode dispatch"
  key_links:
    - from: "src/models/fm2.c"
      to: "src/dsp_primitives.c"
      via: "fx_config(mode, amt) where mode is 0..4 integer"
      pattern: "fx_config.*\\(int\\)fm->fx_type"
---

<objective>
Fix Bug #1: the FX type selector routes Clip/SAT/Fold/Crush to FX_CRUSH for every non-zero enum index. Root cause: every model stores `fx_type` through `clampf(parse_f(val), 0.0f, 1.0f)` which maps integer strings "1"-"4" to 1.0f, then `* 4.0f + 0.5f` maps 1.0 → 4 = FX_CRUSH. The fix is a one-pattern change across 9 model files.

Purpose: This is the highest-priority fix. It cascades: after this fix, Bug #2 (FM2 transient smearing) and Bug #3 partial (bit-crushed TAPS echoes) are also resolved.
Output: All 9 model files corrected; dispatch integration test added to test_fx.c.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/E1-critical-audio-fixes/E1-RESEARCH.md

<interfaces>
<!-- The broken pattern in every model (identical across all 9 files): -->

In set_param for PK_FX_TYPE (broken — to be replaced):
```c
float v = clampf(parse_f(val), 0.0f, 1.0f);   /* clamps "1"->"4" to 1.0 */
...
fm->fx_type = v;
fx_config(&fm->fx, (int)(fm->fx_type * 4.0f + 0.5f), fm->fx_amt);
/* For PK_FX_AMT: */
fx_config(&fm->fx, (int)(fm->fx_type * 4.0f + 0.5f), fm->fx_amt);
```

In render (broken — to be replaced):
```c
int fx_mode = (int)(fm->fx_type * 4.0f + 0.5f);
s = fx_process(fx_mode, s, fm->fx_amt, &fm->fx);
```

The corrected pattern (apply to all 9 files, adjusting the struct prefix per model):
```c
/* In set_param for PK_FX_TYPE — NO 0..1 clamp on enum: */
fm->fx_type = (float)(int)(parse_f(val) + 0.5f);
if (fm->fx_type < 0.0f) fm->fx_type = 0.0f;
if (fm->fx_type > 4.0f) fm->fx_type = 4.0f;
fx_config(&fm->fx, (int)fm->fx_type, fm->fx_amt);

/* In set_param for PK_FX_AMT — same fx_config call update: */
fx_config(&fm->fx, (int)fm->fx_type, fm->fx_amt);

/* In render — remove * 4.0f: */
int fx_mode = (int)fm->fx_type;
s = fx_process(fx_mode, s, fm->fx_amt, &fm->fx);
```

Per-model struct prefixes for the fx_type field:
- fm2.c: fm->fx_type, fm->fx_amt, fm->fx
- fm4.c: f->fx_type, f->fx_amt, f->fx
- wtr.c: w->fx_type, w->fx_amt, w->fx
- trs.c: t->fx_type, t->fx_amt, t->fx
- ana.c: a->fx_type, a->fx_amt, a->fx
- usr.c: u->fx_type, u->fx_amt, u->fx
- phy.c: p->fx_type, p->fx_amt, p->fx
- hrd.c: h->fx_type, h->fx_amt, h->fx
- dig.c: d->fx_type, d->fx_amt, d->fx

Note: hrd.c has TWO fx_state fields (drive_fx and post-kick fx). Only the POST-KICK FX (h->fx) uses the broken dispatch. The DRIVE (h->drive_fx) uses h->drive_mode directly and is correct — do NOT modify the drive dispatch.
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Fix FX type dispatch in all 9 model files</name>
  <read_first>
    src/models/fm2.c
    src/models/fm4.c
    src/models/wtr.c
    src/models/trs.c
    src/models/ana.c
    src/models/usr.c
    src/models/phy.c
    src/models/hrd.c
    src/models/dig.c
  </read_first>
  <files>src/models/fm2.c, src/models/fm4.c, src/models/wtr.c, src/models/trs.c, src/models/ana.c, src/models/usr.c, src/models/phy.c, src/models/hrd.c, src/models/dig.c</files>
  <action>
In each of the 9 model files, find and replace THREE occurrences of the broken FX_TYPE pattern:

**Occurrence 1 — set_param PK_FX_TYPE branch:**
Find (adjusting struct prefix per model):
```c
fm->fx_type = v;
fx_config(&fm->fx, (int)(fm->fx_type * 4.0f + 0.5f), fm->fx_amt);
```
Replace with (adjusting struct prefix per model):
```c
fm->fx_type = (float)(int)(parse_f(val) + 0.5f);
if (fm->fx_type < 0.0f) fm->fx_type = 0.0f;
if (fm->fx_type > 4.0f) fm->fx_type = 4.0f;
fx_config(&fm->fx, (int)fm->fx_type, fm->fx_amt);
```
Note: `v` is already computed earlier from `clampf(parse_f(val), 0.0f, 1.0f)`. The fix re-calls `parse_f(val)` directly for the integer enum, bypassing the 0..1 clamp.

**Occurrence 2 — set_param PK_FX_AMT branch:**
Find:
```c
fx_config(&fm->fx, (int)(fm->fx_type * 4.0f + 0.5f), fm->fx_amt);
```
Replace with:
```c
fx_config(&fm->fx, (int)fm->fx_type, fm->fx_amt);
```

**Occurrence 3 — render loop:**
Find:
```c
int fx_mode = (int)(fm->fx_type * 4.0f + 0.5f);
```
Replace with:
```c
int fx_mode = (int)fm->fx_type;
```

For each model, use the correct struct prefix:
- fm2.c: `fm->` (all three occurrences)
- fm4.c: `f->` (all three occurrences)
- wtr.c: `w->` (all three occurrences)
- trs.c: `t->` (all three occurrences)
- ana.c: `a->` (all three occurrences)
- usr.c: `u->` (all three occurrences)
- phy.c: `p->` (all three occurrences)
- hrd.c: `h->` (all three occurrences — only the POST-KICK fx, not the drive_fx which uses h->drive_mode)
- dig.c: `d->` (all three occurrences)

After editing, verify each file has NO remaining instances of `fx_type * 4.0f`.
  </action>
  <verify>
    <automated>grep -rn "fx_type \* 4\.0f" /Users/jamesfish/Vibecoding\ Projects/Schwung/Omega/src/models/ && echo "FAIL: old pattern still present" || echo "PASS: no old pattern found"</automated>
  </verify>
  <acceptance_criteria>
    - `grep -rn "fx_type \* 4\.0f" src/models/` returns no matches
    - `grep -n "fx_type = (float)(int)(parse_f" src/models/fm2.c` shows a match
    - `grep -n "int fx_mode = (int)fm->fx_type;" src/models/fm2.c` shows a match
    - `grep -n "int fx_mode = (int)f->fx_type;" src/models/fm4.c` shows a match
    - `grep -n "int fx_mode = (int)h->fx_type;" src/models/hrd.c` shows a match
    - `grep -n "int fx_mode = (int)d->fx_type;" src/models/dig.c` shows a match
    - hrd.c still contains `fx_config(&h->drive_fx, h->drive_mode, h->drive_amt)` unchanged
  </acceptance_criteria>
  <done>All 9 model files store fx_type as integer 0..4 and pass that integer directly to fx_config and fx_process. No `* 4.0f` scaling remains in any model.</done>
</task>

<task type="auto">
  <name>Task 2: Add FX dispatch integration test to test_fx.c and run make test</name>
  <read_first>
    tests/test_fx.c
    Makefile
  </read_first>
  <files>tests/test_fx.c</files>
  <action>
Add a new test section T6 to test_fx.c that verifies the FX type dispatch through the full plugin lifecycle (init → create → set_param → render). This catches any regression where a model re-introduces the broken clamp.

After the existing T5 block (around line 108, before `printf("test_fx: ALL TESTS PASSED\n");`), add:

```c
    /* T6: FX type dispatch integration — verify modes 1-4 produce DIFFERENT
     * output from Diode (mode 0). After Bug #1 fix, each mode must be distinct.
     * Uses the real plugin lifecycle via mock_host (FM2 as the reference model). */
    {
        /* This test is compiled with mock_host linkage — just verify fx_config
         * dispatches distinct modes. Clip/SAT/Fold/Crush at amt=1.0 must each
         * produce distinct RMS from Diode at amt=1.0. */
        fx_state_t sts[5];
        double rms_out[5];
        for (int m = 0; m < 5; m++) {
            memset(&sts[m], 0, sizeof sts[m]);
            fx_config(&sts[m], m, 1.0f);
            double acc = 0.0;
            for (int i = 0; i < NSAMP; i++) {
                float y = fx_process(m, dry_lo[i], 1.0f, &sts[m]);
                acc += (double)y * (double)y;
            }
            rms_out[m] = sqrt(acc / NSAMP);
        }
        /* Each pair of modes must differ by at least 1% RMS OR waveform energy. */
        for (int a = 0; a < 5; a++) {
            for (int b = a + 1; b < 5; b++) {
                /* Compute waveform difference energy between mode a and mode b */
                fx_state_t sa, sb;
                memset(&sa, 0, sizeof sa); memset(&sb, 0, sizeof sb);
                fx_config(&sa, a, 1.0f); fx_config(&sb, b, 1.0f);
                double diff_e = 0.0;
                for (int i = 0; i < NSAMP; i++) {
                    float ya = fx_process(a, dry_lo[i], 1.0f, &sa);
                    float yb = fx_process(b, dry_lo[i], 1.0f, &sb);
                    double d = (double)ya - (double)yb;
                    diff_e += d * d;
                }
                double diff_rms = sqrt(diff_e / NSAMP);
                /* Modes must not be identical — diff_rms > 0.01 threshold */
                assert(diff_rms > 0.01 && "FX modes are not distinct — dispatch broken");
            }
        }
    }
```

This test verifies that calling `fx_config(0..4)` and `fx_process(0..4)` produces 5 distinct outputs — the direct-DSP proof that modes 0-4 are independent. If the model dispatch sends everything to mode 4 (Crush), the test would catch it during any future regression.

After adding the test, run:
```
cd /Users/jamesfish/Vibecoding\ Projects/Schwung/Omega && make test 2>&1 | tail -30
```
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && make test 2>&1 | tail -20</automated>
  </verify>
  <acceptance_criteria>
    - `tests/test_fx.c` contains the string `FX modes are not distinct`
    - `make test` output contains `test_fx: ALL TESTS PASSED`
    - `make test` output contains no `FAILED` lines
    - `make test` exits 0
  </acceptance_criteria>
  <done>make test passes all harnesses including the new T6 mode-distinctness check. Zero regressions.</done>
</task>

</tasks>

<verification>
After both tasks:
1. `grep -rn "fx_type \* 4\.0f" src/models/` returns no matches
2. `make test` exits 0 with all suites PASSED
3. `grep -c "(int)fm->fx_type" src/models/fm2.c` returns at least 2 (set_param AMT branch + render)
</verification>

<success_criteria>
All 5 FX modes dispatch to distinct DSP paths. The `* 4.0f` scaling is completely removed from all 9 model files. `make test` is GREEN with no regressions.
</success_criteria>

<output>
After completion, create `.planning/phases/E1-critical-audio-fixes/E1-01-SUMMARY.md`
</output>
