# Phase E2: Discrete Controls & UI Cleanup — Research

**Researched:** 2026-09-30
**Domain:** C DSP UI schema, wavetable waveform dispatch, filter topology
**Confidence:** HIGH (all findings from direct source-code reading)

---

## Project Constraints (from CLAUDE.md)

- C11, no C++, no STL
- Zero malloc/free on the audio thread; all allocations in `create_instance`
- `get_param` is on the audio/UI thread: no allocation, no file I/O
- `ui_hierarchy` JSON served from pre-allocated scratch via `omega_build_ui` in `src/ui.c`
- 128x64 OLED, 8 rotary encoders — UI is purely JSON-schema-driven
- Fixed 44.1 kHz sample rate
- Must build inside `ghcr.io/charlesvestal/schwung-builder:latest` (aarch64, glibc 2.35)

---

## Current State Per Bug

### Bug #8 — FM4 ALGORITHM: continuous knob instead of discrete selector

**File:** `src/models/fm4.c`, `src/ui.c`, `src/params.h`

**Current DSP dispatch** (`fm4_set_param`, line 184–188 of `fm4.c`):
```c
} else if (strcmp(key, PK_FM4_ALGO) == 0) {
    int a = (int)(v * (float)(FM4_NALGO - 1) + 0.5f);
    f->algo = a < 0 ? 0 : (a >= FM4_NALGO ? FM4_NALGO - 1 : a);
```
The DSP side already quantizes `v` to 0..3 and uses it as an index into the static routing tables `g_fm4_algo[4][]` and `g_fm4_carrier[4][]`. The four algorithms are:
- ALGO0: chain 3→2→1→0 (deep, evolving)
- ALGO1: two stacks (3→2) + (1→0) summed
- ALGO2: 3→{2,1,0} (bright, 3 carriers)
- ALGO3: (3→0) + 1 + 2 additive (hollow/woody)

**Current UI schema** (`fm4_p2_slot_desc`, line 357–361 of `fm4.c`):
```c
"{\"key\":\"" PK_FM4_ALGO "\",\"name\":\"ALGORITHM\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
```
Declared as `type:"float"` — no `options` array — so the host renders a continuous knob.

**What needs to change:** The `p2_slot_desc` entry must change `"type":"float"` to `"type":"enum"` and add `"options":["Chain","Stacks","Bright","Hollow"]`. The DSP mapping in `fm4_set_param` already works correctly with integer indices; no DSP change required.

**Cache index:** `PKI_FM4_ALGO` in `params.h`. The cache stores the raw 0..1 float. The enum host sends integer-as-string (e.g. "2"); `fm4_set_param` already does `(int)(parse_f(val) + 0.5f)` on PK_FM4_ALGO, which is correct for enum dispatch. No change needed in the DSP handler.

**UI schema emitter path:** `fm4_p2_slot_desc` → spliced by `omega_build_ui` in `ui.c` into kick1 page. Only the static JSON string inside `fm4_p2_slot_desc` needs to be updated.

---

### Bug #9 — FM2 OP2 WAVE: continuous knob, should be discrete + draw waveform

**File:** `src/models/fm2.c`, `src/ui.c`

**Current DSP dispatch** (`fm2_set_param`, line 144):
```c
} else if (strcmp(key, PK_OP2_WAVE) == 0) {
    fm->op2_wave = v;
```
`op2_wave` is used in `fm2_render` (line 217–219) as a BLEND factor between the raw sine and a folded variant (`fabsf(mod_out)*2-1`):
```c
float folded = fabsf(mod_out) * 2.0f - 1.0f;
mod_out = mod_out + fm->op2_wave * (folded - mod_out);
```
This is a continuous morph, not a discrete step. Bug #9 says it should be a discrete selector showing the waveform.

**How many options:** The morph has two extremes: pure sine (op2_wave=0) and rectified/folded sine (op2_wave=1). Logical discrete options: `["Sine","Fold"]` — or optionally also "Tri" by reading from g_wavetables[1]. Three to four options suffice.

**Waveform drawing:** The OLED UI is entirely driven by the `ui_hierarchy` JSON schema. There is no separate "draw waveform" API in the Schwung OLED system. The "draws the waveform" described in Bug #9 refers to: the UI host already renders a small waveform preview for enum options when the option name matches a known shape (or, more likely, the GEN wave selector from Phase C1 was the "good" model being referenced). Looking at the GEN wave selector in `ui.c` line 114:
```c
static const char OPT_GWAVE[] = "[\"Sine\",\"Tri\",\"Saw\",\"Square\",\"Digital\",\"Analog\"]";
```
And in `groove.c` `groove_set_param` (line 432):
```c
g->gen_wave = (int)(v * 5.99f);  /* 0..5 factory waves */
```
The GEN wave list picker dispatches directly into `wt_read_bl(g->gen_wave, ...)`. The "good" model is simply: an enum list-picker where each option name matches a waveform. The OLED host **may** render a waveform glyph when the option array matches the standard `["Sine","Tri","Saw","Square","Digital","Analog"]` names. This is not guaranteed — but using the standard names is the correct approach.

**Fix strategy for OP2 WAVE:** Convert from a continuous 0..1 blend to a 3-step discrete selector: `["Sine","Fold","Tri"]`. The DSP dispatch must map index 0→pure sine, index 1→fold morph, index 2→triangle table. The `fm2_state.op2_wave` field can remain a float, with the enum dispatch performing `op2_wave = (float)index / 2.0f` or storing the integer index directly.

**p2_slot_desc change:** Change `"type":"float","min":0.0,"max":1.0` to `"type":"enum","options":["Sine","Fold","Tri"]`.

---

### Bug #10 — HRD SAMPLE LAYER: continuous knob, should be list picker

**File:** `src/models/hrd.c`

**Current state:** `PK_HRD_SAMPLE` is declared in `hrd_p2_slot_desc` as `"type":"float"`. In `hrd_set_param` (line 160–165):
```c
} else if (strcmp(key, PK_HRD_SAMPLE) == 0) {
    int span = HRD_WAVE_MAX - HRD_WAVE_MIN;
    int idx  = HRD_WAVE_MIN + (int)(v * (float)span + 0.5f);
    idx = idx < HRD_WAVE_MIN ? HRD_WAVE_MIN : (idx > HRD_WAVE_MAX ? HRD_WAVE_MAX : idx);
    h->body_wave = idx;
    h->samp_wave = idx;
```
The wave range is `HRD_WAVE_MIN=2` (saw-ish) to `HRD_WAVE_MAX=4` (digital chip) — 3 discrete options: `g_wavetables[2]`, `[3]`, `[4]` = Saw, Square, Digital.

**Correct options array:** `["Saw","Square","Digital"]` — 3 items.

**DSP fix:** The handler already quantizes. Change dispatch to accept 0/1/2 integer index instead of 0..1 float, mapping to wave indices 2/3/4. Or keep `HRD_WAVE_MIN + index`. Either way trivial.

**p2_slot_desc change:** `"type":"float"` → `"type":"enum","options":["Saw","Square","Digital"]`.

---

### Bug #11 — DIG WAVE IDX: continuous knob, should be discrete selector

**File:** `src/models/dig.c`

**Current state:** `PK_DIG_WAVEIDX` is `"type":"float"` in `dig_p2_slot_desc`. In `dig_set_param` (line 147–152):
```c
} else if (strcmp(key, PK_DIG_WAVEIDX) == 0) {
    int span = DIG_WAVE_MAX - DIG_WAVE_MIN;
    int idx  = DIG_WAVE_MIN + (int)(v * (float)span + 0.5f);
    d->waveidx = idx < DIG_WAVE_MIN ? DIG_WAVE_MIN
               : (idx > DIG_WAVE_MAX ? DIG_WAVE_MAX : idx);
```
`DIG_WAVE_MIN=2` (saw), `DIG_WAVE_MAX=4` (digital) — same 3 options as HRD: `g_wavetables[2]`=Saw, `[3]`=Square, `[4]`=Digital.

**Correct options array:** `["Saw","Square","Digital"]` — 3 items.

**p2_slot_desc change:** `"type":"float"` → `"type":"enum","options":["Saw","Square","Digital"]`.

---

### Bug #12 — GEN wave audio: square/saw/digital/analog sound like triangle/sine

**File:** `src/models/gen.c`, `src/groove.c`

**Root cause — GEN kick model (`gen.c`):**
In `gen_set_param` (line 167–169):
```c
} else if (strcmp(key, PK_ATTACK) == 0) {
    /* ATTACK biases the body wave brighter (more attack presence). */
    g->wave = (int)(v * (float)(NUM_WAVES - 1) + 0.5f);
```
The GEN kick body wave is driven by the `PK_ATTACK` key, not a dedicated wave key. `NUM_WAVES=6`, so this maps ATTACK's 0..1 value to waves 0..5. BUT there is no dedicated wave selector on GEN's Page 2 exposed to the user as a list picker — wave index is a side-effect of ATTACK. The GEN model has no separate `PK_GEN_WAVE`-style key. The kick GEN model is being **removed** from the kick list (Bug #13), so this specific code path becomes irrelevant.

**Root cause — GEN groove voice (`groove.c`):**
In `groove_set_param` (line 432):
```c
} else if (strcmp(key, PK_GRV_GWAVE) == 0) {
    g->gen_wave = (int)(v * 5.99f);  /* 0..5 factory waves */
```
And in `groove_tick` (line 258):
```c
float osc = wt_read_bl(g->gen_wave, 0, g->gen_osc_phase);
```
The groove GEN voice correctly calls `wt_read_bl(g->gen_wave, ...)` with `gen_wave` as the table index. The UI sends an integer enum index (0=Sine, 1=Tri, 2=Saw, 3=Square, 4=Digital, 5=Analog). The mapping `(int)(v * 5.99f)` works correctly when the host sends an integer string ("0","1","2"...).

**BUT** the OLED feedback said "square sounds like triangle, saw sounds like triangle." Looking at the wavetable data: wave 0=sine, wave 1=triangle, wave 2=saw-ish, wave 3=square, wave 4=digital, wave 5=analog. If the GEN groove UI shows this list and uses `wt_read_bl`, the audio should be correct.

**The actual bug:** The dispatch `(int)(v * 5.99f)` multiplies a normalized 0..1 float. If the host sends enum index as `"2"` for Saw, `parse_f("2")` returns `2.0`, then `clampf(2.0, 0.0, 1.0) = 1.0`, then `(int)(1.0 * 5.99) = 5` (Analog, not Saw). The standard `set_param` pattern in `groove_set_param` clamps the raw float to [0,1] at the top of the function (line 370: `float v = clampf(parse_f(val), 0.0f, 1.0f)`), so any enum index > 1 collapses to 1 (Triangle). This is the bug: all enum indices above 0 and 1 get clamped to 1.

**Fix:** The GEN wave handler must bypass the `clampf(v, 0.0, 1.0)` by parsing the raw value directly (same pattern as FX_TYPE dispatch: `(int)(parse_f(val) + 0.5f)`), not using the pre-clamped `v`. Compare with `PK_FX_TYPE` handling in `fm2_set_param` (line 150):
```c
fm->fx_type = (float)(int)(parse_f(val) + 0.5f);
```
This is identical to the Bug #1 fix pattern. The GEN groove WAVE key must do the same.

**All other enum-dispatched params that follow the clamped `v` pattern in `groove_set_param` are vulnerable to the same bug** — specifically `PK_GRV_GSCALE`, `PK_GRV_GRETRIG`. These must also bypass the clamp when parsing enum indices.

---

### Bug #13 — GEN in kick model list

**File:** `src/ui.c`, `src/omega.h`

**Current state:** In `ui.c` line 105:
```c
static const char OPT_MODEL[] =
    "[\"FM2\",\"FM4\",\"WTR\",\"PHY\",\"HRD\",\"DIG\",\"TRS\",\"ANA\",\"USR\",\"GEN\"]";
```
GEN is listed at index 9 (matching `MODEL_GEN = 9` in `model_id_t`).

**What needs to change:** Remove `"GEN"` from the options array — the list becomes 9 items. The host will no longer show GEN as a kick model choice. GEN as a groove TYPE (the separate `GROOVE_TYPE_GEN` path in `groove_state_t`) is unaffected.

**Critical constraint:** `MODEL_GEN` remains in the `model_id_t` enum at position 9. The model registry still has `g_gen_vtable` at `[MODEL_GEN]`. The `model_id_t` enum is permanent/append-only per CLAUDE.md. The removal is UI-only: just drop `"GEN"` from `OPT_MODEL`. The cache still allocates `MODEL_COUNT` rows including MODEL_GEN; nothing structural changes.

**Side effect on params cache:** `pk_kick_index` / `pk_global_index` are unaffected. The cache row for `MODEL_GEN` is still populated (the user just can't navigate to it via the kick model UI picker anymore).

**WARNING:** If the host uses the option count to bound a SET_PARAM value, removing GEN means 9 options (indices 0–8) but the DSP accepts indices 0–9. The DSP already bounds `m = clamp(0, m, MODEL_COUNT-1)`, so setting model=9 via a direct set_param call would still work — but the UI won't offer it. This is the correct behavior.

---

### Bug #14 — TAPS and GEN filters: should be a single continuous LP knob

**Files:** `src/groove.c`, `src/ui.c`

**Current state for TAPS:** `P_GROOVE2_TAPS` in `ui.c` (line 187–196) includes:
```c
{ PK_GRV_FILTYPE, "FILTER",  "FILT",  UP_ENUM,  "",  "0",    OPT_GFILT },
```
`OPT_GFILT = "[\"LP\",\"HP\",\"Off\"]"` — a discrete type selector.

`PK_GRV_COLOR` on Groove Page 1 is the current LP cutoff for the TAPS path (line 381–382 in `groove_set_param`):
```c
} else if (strcmp(key, PK_GRV_COLOR) == 0) {
    float fc = 200.0f + v * (18000.0f - 200.0f);
    g->color_g = tpt_g_from_hz(fc);
```
This is a sweep from 200 Hz to 18 kHz. Bug #14 asks for 30 Hz–20 kHz log sweep.

**Current state for GEN groove:** `P_GROOVE_GEN2` in `ui.c` (line 217–228) also includes `PK_GRV_FILTYPE` as a discrete enum selector.

**What "single continuous LP sweep" means:** Remove the `PK_GRV_FILTYPE` discrete type selector from both TAPS and GEN groove FX pages. Replace or repurpose the `PK_GRV_COLOR` control to cover the full range 30 Hz–20 kHz with a log (exponential) curve.

**Filter implementation in `groove_tick`** (line 307–317): the current filter reads `g->filter_type` and branches `GRV_FILT_LP / GRV_FILT_HP / GRV_FILT_OFF`. If the new design is LP-only, the branch simplifies to always-LP (always apply `tpt1_lp`). The `filter_type` field and the `GRV_FILT_*` enum in `groove.h` can remain in the struct for backwards compatibility but the UI will no longer expose a type selector.

**Knob placement:** `PK_GRV_COLOR` on Groove Page 1 currently serves as the LP sweep for TAPS. The cleaner approach is: remove `PK_GRV_FILTYPE` from the FX page (Groove Page 2) and change the range of `PK_GRV_COLOR` (already on Groove Page 1) to 30 Hz–20 kHz log. This preserves the page-1 sweep knob position. No new param key needed.

**Log curve formula:** `fc = 30.0f * powf(20000.0f / 30.0f, v)` (exponential ramp, 30 Hz at v=0, 20 kHz at v=1). This is control-rate (`groove_set_param`), so `powf` is fine here.

**UI change:** Remove `PK_GRV_FILTYPE` from `P_GROOVE2_TAPS[]` and `P_GROOVE_GEN2[]`, update `KN_GROOVE2_TAPS` and `KN_GROOVE_GEN2` accordingly. Update `PK_GRV_COLOR` range comment in `groove_set_param`.

---

### Bug #15 — USR LAYER VOL: rename to BLEND, make bidirectional from center

**File:** `src/models/usr.c`, `src/ui.c` (indirectly via the `p2_slot_desc`)

**Current state:** `PK_USR_LAYERVOL` is declared in `usr.c`'s `p2_slot_desc` as a `"type":"float","min":0.0,"max":1.0` continuous knob. In `usr_set_param`:
```c
} else if (strcmp(key, PK_USR_LAYERVOL) == 0) {
    u->layer_vol = v;
```
And in `usr_render`, `layer_vol` is used as a straight 0..1 lerp between body and sample:
```c
s = s_body + u->layer_vol * (s_samp - s_body);  /* lerp body->sample */
```
(v=0 = pure body/wavetable; v=1 = pure sample)

**What "bidirectional from center" means:** 0.5 = equal blend. The control should be renamed BLEND. The semantics of the raw value can remain 0..1 (the host's bidirectional centering is a UI display convention, not a DSP inversion). The host renders a centered/bipolar display when the param schema carries a `"default":0.5` or when it's explicitly a bidirectional type.

**Minimal fix:** Rename display name in `usr_p2_slot_desc` from `"LAYER VOL"` to `"BLEND"` and set a `"default":0.5`. The raw 0..1 mapping stays the same; the DSP lerp is already correct for this interpretation (0.5 = equal mix). The `PK_USR_LAYERVOL` key string stays unchanged (key rename requires a cache migration — avoid).

**Cache key:** `PKI_USR_LAYERVOL` in `params.h`. Default is already 0.5 in `g_kick_defaults`. Only the UI display name and possibly the `ui.c` default annotation change.

**Bidirectional center in JSON schema:** Adding `"default":0.5` to the `p2_slot_desc` JSON object is sufficient; the host will center the knob at 50% and show a bipolar display.

---

### Bug #16 — Performer DUCK SMT and DUCK BS: opaque labels

**File:** `src/ui.c`, `src/omega.h`

**Current state** (`P_PERF` in `ui.c`, lines 121–128):
```c
{ PK_DUCK_SMT,   "DUCK SMT", "DKSMT", UP_FLOAT, "%", "0.01", NULL },
{ PK_DUCK_BS,    "DUCK BS",  "DKBS",  UP_FLOAT, "%", "0.01", NULL },
```

**What they actually do** (from `dsp.c` performer section and STATE.md decision [D]):
- `DUCK SMT` = duck envelope smoothing — a one-pole gain slew coefficient that prevents clicks when the duck gain changes rapidly. Conceptually: "smoothing speed" or "duck slew".
- `DUCK BS` = duck bass focus — a TPT highpass crossover: only the groove frequencies BELOW the crossover cutoff are ducked. Higher = duck more lows, lower = duck the whole signal. Conceptually: "focus" or "low cut".

**Better labels:**
- `DUCK SMT` → `DUCK SLEW` (short: `SLEW`) — describes the one-pole smoothing of the gain change
- `DUCK BS` → `DUCK FREQ` (short: `FREQ`) — the crossover frequency below which ducking applies

**What to change:** Only `P_PERF` table strings in `ui.c` (the `name` and `shortn` fields). The `PK_DUCK_SMT` / `PK_DUCK_BS` key strings in `omega.h` do NOT change (key rename would break the cache and any existing presets/host state). Only the display names in the UI table change.

---

## UI/OLED Capabilities

**Architecture:** The OLED UI is 100% schema-driven. `omega_build_ui` emits a JSON `ui_hierarchy` blob consumed by the host's UI engine. The host renders controls based on `"type"` in the schema:
- `"type":"float"` → continuous knob
- `"type":"enum"` with `"options":[...]` → list picker / string selector

**Waveform visualization:** There is no "draw waveform" API distinct from the list picker. The GEN wave selector is described as "good" because it uses `"type":"enum","options":["Sine","Tri","Saw","Square","Digital","Analog"]`. The Schwung host very likely renders a small waveform glyph for these standard names. Using the exact same option names for FM2 OP2 WAVE, HRD SAMPLE, and DIG WAVE IDX will provide the same visual behavior (HIGH confidence: this is the only enum+waveform-named selector that works).

**No allocation on the UI path:** All JSON emitters use `ui_append` / `snprintf` into the caller's pre-allocated buffer. The `p2_slot_desc` functions write into a `char scratch[1024]` on the stack in `omega_build_ui`. Changes to `p2_slot_desc` strings must stay within this 1024-byte budget.

**Enum dispatch pattern in `groove_set_param`:** The key lesson from Bug #12 is that any enum key in `groove_set_param` MUST bypass the top-of-function `v = clampf(parse_f(val), 0.0f, 1.0f)` and instead use the raw `parse_f(val)` directly, clamped only to the valid enum range. The pattern from `fm2_set_param`'s `PK_FX_TYPE` handler is the standard:
```c
int idx = (int)(parse_f(val) + 0.5f);
if (idx < 0) idx = 0;
if (idx >= N_OPTIONS) idx = N_OPTIONS - 1;
```

---

## Proposed Fixes

### Fix order and change scope

| Bug | File(s) | Change type | Isolated? |
|-----|---------|-------------|-----------|
| #13 | `src/ui.c` | Remove "GEN" from OPT_MODEL string | Yes |
| #16 | `src/ui.c` | Rename DUCK SMT / DUCK BS in P_PERF | Yes |
| #15 | `src/models/usr.c` | Rename p2_slot_desc name to BLEND + default 0.5 | Yes |
| #8  | `src/models/fm4.c` | Change p2_slot_desc ALGORITHM to enum | Yes |
| #10 | `src/models/hrd.c` | Change p2_slot_desc SAMPLE LAYER to enum | Yes |
| #11 | `src/models/dig.c` | Change p2_slot_desc WAVE IDX to enum | Yes |
| #9  | `src/models/fm2.c` | Change p2_slot_desc OP2 WAVE to enum + DSP dispatch | Mostly |
| #12 | `src/groove.c` | Fix enum clamping bug in groove_set_param | Yes |
| #14 | `src/groove.c`, `src/ui.c` | Remove filter type selector, update COLOR range | Both files |

### Fix #8 — FM4 ALGORITHM (fm4.c only)

In `fm4_p2_slot_desc`, replace the ALGORITHM slot with:
```c
"{\"key\":\"" PK_FM4_ALGO "\",\"name\":\"ALGORITHM\",\"type\":\"enum\","
"\"options\":[\"Chain\",\"Stacks\",\"Bright\",\"Hollow\"]},"
```
DSP handler in `fm4_set_param` already works correctly — `(int)(v * 3 + 0.5f)` handles 0..1 floats, but for enum the host sends "0","1","2","3". The handler needs to parse the integer directly:
```c
int a = (int)(parse_f(val) + 0.5f);  /* same pattern — works for both float and integer strings */
f->algo = a < 0 ? 0 : (a >= FM4_NALGO ? FM4_NALGO - 1 : a);
```
This already exists and is correct.

### Fix #9 — FM2 OP2 WAVE (fm2.c only, plus fm2_p2_slot_desc)

**Option design:** Use `["Sine","Fold","Tri"]` (3 options, indices 0/1/2).

In `fm2_set_param` PK_OP2_WAVE handler:
```c
} else if (strcmp(key, PK_OP2_WAVE) == 0) {
    int idx = (int)(parse_f(val) + 0.5f);
    if (idx < 0) idx = 0; if (idx > 2) idx = 2;
    fm->op2_wave = (float)idx;  /* 0=sine, 1=fold, 2=tri */
```

In `fm2_render`, branch on the integer:
```c
int wave_idx = (int)(fm->op2_wave + 0.5f);
if (wave_idx == 0) {
    /* pure sine: mod_out unchanged */
} else if (wave_idx == 1) {
    float folded = fabsf(mod_out) * 2.0f - 1.0f;
    mod_out = folded;
} else {
    mod_out = wt_read(g_wavetables[1][0], fm->mod_phase);  /* triangle table */
}
```

In `fm2_p2_slot_desc`, change OP2 WAVE slot to:
```c
"{\"key\":\"" PK_OP2_WAVE "\",\"name\":\"OP2 WAVE\",\"type\":\"enum\","
"\"options\":[\"Sine\",\"Fold\",\"Tri\"]},"
```

### Fix #10 — HRD SAMPLE LAYER (hrd.c only)

In `hrd_p2_slot_desc`, change SAMPLE LAYER slot:
```c
"{\"key\":\"" PK_HRD_SAMPLE "\",\"name\":\"SAMPLE LAYER\",\"type\":\"enum\","
"\"options\":[\"Saw\",\"Square\",\"Digital\"]},"
```

In `hrd_set_param` PK_HRD_SAMPLE handler, change to parse the integer index:
```c
int idx = (int)(parse_f(val) + 0.5f);
if (idx < 0) idx = 0; if (idx > 2) idx = 2;
h->body_wave = HRD_WAVE_MIN + idx;  /* maps 0->2, 1->3, 2->4 */
h->samp_wave = h->body_wave;
```

### Fix #11 — DIG WAVE IDX (dig.c only)

In `dig_p2_slot_desc`, change WAVE IDX slot:
```c
"{\"key\":\"" PK_DIG_WAVEIDX "\",\"name\":\"WAVE IDX\",\"type\":\"enum\","
"\"options\":[\"Saw\",\"Square\",\"Digital\"]},"
```

In `dig_set_param` PK_DIG_WAVEIDX handler:
```c
int idx = (int)(parse_f(val) + 0.5f);
if (idx < 0) idx = 0; if (idx > 2) idx = 2;
d->waveidx = DIG_WAVE_MIN + idx;  /* maps 0->2, 1->3, 2->4 */
```

### Fix #12 — GEN groove wave dispatch (groove.c)

In `groove_set_param`, the `PK_GRV_GWAVE` handler currently uses the pre-clamped `v`. Fix:
```c
} else if (strcmp(key, PK_GRV_GWAVE) == 0) {
    int idx = (int)(parse_f(val) + 0.5f);  /* bypass the 0..1 clamp */
    g->gen_wave = idx < 0 ? 0 : (idx >= NUM_WAVES ? NUM_WAVES - 1 : idx);
```
Apply the same pattern to `PK_GRV_GSCALE` and `PK_GRV_GRETRIG` which are also enum dispatch in `groove_set_param` but use the pre-clamped `v`.

For `PK_GRV_GSCALE` (currently `(int)(v * 3.99f)` — maps 0..1 to 0..3):
```c
} else if (strcmp(key, PK_GRV_GSCALE) == 0) {
    int idx = (int)(parse_f(val) + 0.5f);
    g->gen_scale = idx < 0 ? 0 : (idx > 3 ? 3 : idx);
```
For `PK_GRV_GRETRIG`:
```c
} else if (strcmp(key, PK_GRV_GRETRIG) == 0) {
    int m = (int)(parse_f(val) + 0.5f);
    g->gen_retrig = (m < 0) ? 0 : (m > 5 ? 5 : m);
```
(The GRETRIG handler already calls `parse_f(val)` directly — the existing code is `(int)(parse_f(val) + 0.5f)`. Check if `v` is used instead of `parse_f(val)` there.)

Looking at groove.c line 437: `int m = (int)(parse_f(val) + 0.5f);` — GRETRIG already uses `parse_f(val)` directly. Only GWAVE and GSCALE need fixing.

### Fix #13 — Remove GEN from kick model list (ui.c only)

In `ui.c` line 105, change:
```c
static const char OPT_MODEL[] =
    "[\"FM2\",\"FM4\",\"WTR\",\"PHY\",\"HRD\",\"DIG\",\"TRS\",\"ANA\",\"USR\",\"GEN\"]";
```
to:
```c
static const char OPT_MODEL[] =
    "[\"FM2\",\"FM4\",\"WTR\",\"PHY\",\"HRD\",\"DIG\",\"TRS\",\"ANA\",\"USR\"]";
```
9 options. The DSP dispatch still accepts MODEL=9 (MODEL_GEN) via set_param; it just won't appear in the list picker.

**Test impact:** `test_switch.c` iterates all `MODEL_COUNT` (10) models directly. The test sets model by integer, not by name, so it is unaffected. The test for `OPT_MODEL` count in `test_switch.c` (if any) would need updating.

### Fix #14 — TAPS and GEN filter → single LP sweep (groove.c + ui.c)

**DSP change in `groove_set_param`:** Change `PK_GRV_COLOR` range from `200..18000 Hz` to `30..20000 Hz` log:
```c
} else if (strcmp(key, PK_GRV_COLOR) == 0) {
    float fc = 30.0f * powf(20000.0f / 30.0f, v);  /* 30 Hz..20 kHz log */
    g->color_g = tpt_g_from_hz(fc);
    g->filter_type = GRV_FILT_LP;  /* always LP now */
```
Force `filter_type` to LP permanently — the type selector is no longer exposed.

**UI change in `ui.c`:** Remove `PK_GRV_FILTYPE` from `P_GROOVE2_TAPS[]` and `P_GROOVE_GEN2[]`. Update the `KN_GROOVE2_TAPS` and `KN_GROOVE_GEN2` knob arrays to remove that key. The `PK_GRV_COLOR` control on Groove Page 1 already serves as the LP knob — its name can stay "COLOR" or be renamed "LP FREQ". The feedback says "single continuous LP sweep knob" so it makes sense to rename the Groove Page 1 COLOR control to "LP FILT" or "FILTER".

**Filter behavior in `groove_tick`:** The existing filter code checks `g->filter_type`. Since it will always be `GRV_FILT_LP` after this change, the HP/Off branches become dead code. They can remain for future use without overhead (the branch is control-rate-driven from the state, so it adds one comparison per sample — negligible). No DSP simplification required, but it is clean to leave the comment.

### Fix #15 — USR BLEND rename (usr.c p2_slot_desc only)

In `usr_p2_slot_desc`, change:
```c
"{\"key\":\"" PK_USR_LAYERVOL "\",\"name\":\"LAYER VOL\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
```
to:
```c
"{\"key\":\"" PK_USR_LAYERVOL "\",\"name\":\"BLEND\",\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"default\":0.5},"
```
The DSP lerp semantics at 0.5 already produce equal mix. No render-path change.

### Fix #16 — Performer labels (ui.c only)

In `P_PERF[]` in `ui.c`:
```c
{ PK_DUCK_SMT, "DUCK SLEW", "SLEW",  UP_FLOAT, "%", "0.01", NULL },
{ PK_DUCK_BS,  "DUCK FREQ", "FREQ",  UP_FLOAT, "%", "0.01", NULL },
```
Update `KN_PERF` if short names affect knob mapping (they don't — knob arrays use key strings, not display names).

---

## Implementation Order

Safest sequence (each step is independently testable):

**Wave 1 — Pure UI/label changes (zero DSP risk):**
1. Bug #13: Remove GEN from OPT_MODEL in `ui.c`
2. Bug #16: Rename DUCK SMT → DUCK SLEW, DUCK BS → DUCK FREQ in `P_PERF` in `ui.c`
3. Bug #15: Rename LAYER VOL → BLEND + add default:0.5 in `usr.c` p2_slot_desc
4. Bug #14 (UI only): Remove PK_GRV_FILTYPE from P_GROOVE2_TAPS and P_GROOVE_GEN2, update knob arrays in `ui.c`

Verify: `make test` — test_switch checks JSON validity; test_readback checks schema. Both should pass.

**Wave 2 — p2_slot_desc enum conversions (DSP handler already quantizes):**
5. Bug #8: ALGORITHM enum in fm4.c p2_slot_desc (DSP handler unchanged)
6. Bug #10: SAMPLE LAYER enum in hrd.c p2_slot_desc + DSP handler integer index
7. Bug #11: WAVE IDX enum in dig.c p2_slot_desc + DSP handler integer index

Verify: `make test` — test_switch p2 JSON validity + test_params responsiveness.

**Wave 3 — FM2 OP2 WAVE (DSP change + schema change):**
8. Bug #9: OP2 WAVE enum in fm2.c p2_slot_desc + DSP handler + render dispatch

Verify: `make test` — test_params FM2 op2_wave responsiveness.

**Wave 4 — Groove enum clamping + LP filter DSP:**
9. Bug #12: Fix GEN wave (+ GSCALE) enum dispatch in `groove_set_param` in `groove.c`
10. Bug #14 (DSP): Update PK_GRV_COLOR range to 30–20kHz log + always-LP in `groove_set_param`

Verify: `make test` — test_groove wave responsiveness, test_switch groove pages.

---

## Common Pitfalls

### Pitfall 1: p2_slot_desc buffer overflow
The model's `p2_slot_desc` writes into `char scratch[1024]` in `omega_build_ui`. Adding `"options":["Saw","Square","Digital"]` to a slot adds ~35 bytes. FM4's 5-slot descriptor grows by ~160 bytes total. At current sizes this is safe (FM4 is ~290 bytes; with enum additions ~450 bytes), but count carefully.

**Check:** Each `p2_slot_desc` checks `if (buf_len <= len) return 0` before copying. The `buf_len` passed is 1024. Verify the total length of the new JSON stays under 1023 bytes.

### Pitfall 2: Enum clamping (the Bug #12 root cause everywhere)
Any `groove_set_param` key that receives an enum integer must use `parse_f(val)` directly (NOT the pre-clamped `v`). The function-top `v = clampf(parse_f(val), 0.0f, 1.0f)` is correct for float controls but silently truncates any enum index > 1 to 1.

This same issue does NOT affect the per-model `set_param` functions in `fm2.c` etc. — those functions clamp `v` at the top too, but their enum handlers (`PK_FM4_ALGO`, `PK_FX_TYPE`) already call `parse_f(val)` directly in the handler body, bypassing `v`. Verify each new handler follows this pattern.

### Pitfall 3: test_switch bracket balance assertion
`test_switch.c` verifies JSON is bracket-balanced and has no outer `[]`. Changing a slot from `"type":"float"` to `"type":"enum","options":[...]` adds bracket content inside the params array. The test checks for brackets in the splice but the `assert_p2_json_valid` function only checks outer brackets — inner `[]` inside `"options"` values are allowed. Confirm this is the case before assuming tests will pass.

### Pitfall 4: GEN model still silently available
After removing GEN from OPT_MODEL, a set_param("model","9") still activates MODEL_GEN. Any test that does a full model sweep (test_distinct, test_params) and includes GEN by integer index will still exercise it. This is fine — just be aware the on-device UI no longer reaches it.

### Pitfall 5: USR layer_vol DSP semantics
The current `layer_vol` lerp is `body + layer_vol * (sample - body)`. At v=0.0: pure body. At v=1.0: pure sample. At v=0.5: equal mix. This is correct for the renamed BLEND behavior without any DSP change. No inversion needed.

---

## Sources

- `src/models/fm4.c` — FM4 ALGORITHM dispatch (lines 184–188), algo tables (lines 51–66), p2_slot_desc (lines 354–371)
- `src/models/fm2.c` — OP2 WAVE dispatch (line 144), render blend (lines 217–219), p2_slot_desc (lines 260–270)
- `src/models/hrd.c` — SAMPLE LAYER dispatch (lines 160–165), HRD_WAVE_MIN/MAX constants (lines 42–43)
- `src/models/dig.c` — WAVE IDX dispatch (lines 147–152), DIG_WAVE_MIN/MAX constants (lines 37–38)
- `src/models/gen.c` — GEN kick model ATTACK/wave dispatch (lines 167–169)
- `src/groove.c` — GEN groove GWAVE dispatch (line 432), clamp pattern (line 370), filter dispatch (lines 307–317), COLOR range (lines 381–382)
- `src/ui.c` — OPT_MODEL (line 105), P_GROOVE2_TAPS (lines 187–199), P_GROOVE_GEN2 (lines 217–229), P_PERF (lines 120–129)
- `src/omega.h` — model_id_t enum, PK_* macro definitions
- `src/params.h` — cache index enums
- `tools/gen_wavetables.c` — wavetable wave palette (wave 0=Sine, 1=Tri, 2=Saw, 3=Square, 4=Digital, 5=Analog)
- `.planning/ONDEVICE_FEEDBACK_v1_1.md` — bug descriptions #8–#16

**Confidence:** HIGH for all findings — sourced directly from the codebase with no inference required.
