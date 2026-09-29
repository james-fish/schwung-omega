---
phase: C-groove-rumble-engine
plan: 02
type: execute
wave: 2
depends_on: [C-01]
files_modified:
  - src/omega.h
  - src/groove.c
  - src/groove.h
  - src/dsp.c
  - Makefile
autonomous: true
requirements: [GRV-01, GRV-02, GRV-03, GRV-05]
must_haves:
  truths:
    - "The 4-tap rumble reads back the kick signal at 16th-note offsets and stays rhythmically locked to the driven BPM (never a live-hardcoded 120)"
    - "Groove Page 1 controls (VOL, LENGTH, COLOR, TAP1-4, MONO) audibly shape the rumble"
    - "The MONO toggle force-sums L+R to mono on the groove voice"
    - "The kick+groove sum is written to output with a clean Phase-D insertion point below it"
  artifacts:
    - path: "src/groove.h"
      provides: "groove_state_t (delay rings + tempo clock + Page-1 params + COLOR LP), plus groove API decls"
      contains: "groove_state_t"
      min_lines: 30
    - path: "src/groove.c"
      provides: "groove_update_tempo (guarded fallback chain), groove_tick (mask-wrapped 4-tap + COLOR + MONO), groove_set_param"
      contains: "get_beat_position"
      min_lines: 60
    - path: "src/omega.h"
      provides: "groove_state_t member on bohm_instance, raised _Static_assert, PK_GRV_* macros"
      contains: "PK_GRV_VOL"
    - path: "src/dsp.c"
      provides: "groove render stage (kick+groove sum) + groove_set_param dispatch branch + PHASE D insertion marker"
      contains: "groove_update_tempo"
  key_links:
    - from: "src/dsp.c omega_render_block"
      to: "src/groove.c groove_update_tempo + groove_tick"
      via: "once-per-block tempo update then per-sample tick, l[n]+=gl; r[n]+=gr"
      pattern: "groove_update_tempo|groove_tick"
    - from: "src/dsp.c omega_set_param"
      to: "src/groove.c groove_set_param"
      via: "PK_GRV_* key branch before the model-vtable fallback"
      pattern: "groove_set_param"
    - from: "src/groove.c groove_update_tempo"
      to: "host_api_v1_t get_beat_position / get_bpm"
      via: "NULL/negative-guarded beat-delta then get_bpm then 120 last resort"
      pattern: "get_beat_position|get_bpm"
---

<objective>
Build the non-GEN Groove rumble voice: the pre-allocated circular delay buffer, the transport-locked tempo clock (the phase's value-at-risk, GRV-02), the 16th-note 4-tap read, Groove Page 1 controls + MONO, and the `kick + groove` sum in render_block with a clean Phase-D insertion point. Turns C-01's RED test_groove GREEN for GRV-01/02/03/05.

Purpose: This is the crux of Phase C. The tempo clock replaces the forbidden hardcoded-120 reference bug with a guarded `get_beat_position()` beat-delta derivation. The delay buffer grows the single instance calloc (DC-01/DC-08) and the groove voice is model-independent for all non-GEN models.
Output: New `src/groove.{c,h}`, grown `bohm_instance` with a raised size assert + PK_GRV_* macros in omega.h, groove wiring in dsp.c, and a Makefile that compiles src/groove.c into every build.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/C-groove-rumble-engine/C-CONTEXT.md
@.planning/phases/C-groove-rumble-engine/C-RESEARCH.md
@.planning/phases/C-groove-rumble-engine/C-VALIDATION.md

<interfaces>
<!-- Shared primitives (src/dsp_primitives.h) — use directly, do NOT reimplement. -->
```c
typedef struct { float s; } tpt1_t;
static inline float tpt1_lp(tpt1_t *f, float x, float g);   /* g = tanf(pi*fc/SR) precomputed */
static inline int16_t omega_to_i16(float x);                /* clamp+isfinite+lrintf (FNDTN-07) */
#define OMEGA_SR 44100.0f
```

<!-- Host transport (src/omega.h host_api_v1_t) — LOCKED, do NOT alter the struct. -->
```c
float  (*get_bpm)(void);            /* may be NULL */
double (*get_beat_position)(void);  /* < 0 when stopped; may be NULL */
```

<!-- Current bohm_instance tail + the assert to RAISE (src/omega.h). -->
```c
struct bohm_instance {
    model_id_t model; float main_volume; bool logged_buflen;
    char model_state[4096];
    float usr_wavetable[OMEGA_WT_GUARD]; float usr_sample[44100];
    int usr_sample_len; bool usr_wt_loaded, usr_loaded;
    /* <-- ADD: groove_state_t groove; here */
};
_Static_assert(sizeof(struct bohm_instance) < 800000, "instance under 800KB");  /* RAISE */
```

<!-- dsp.c seams (src/dsp.c). -->
```c
static const host_api_v1_t *g_host;                 /* set in move_plugin_init_v2 */
static float dsp_parse_f(const char *s);            /* locale-independent parse */
static void omega_set_param(void *inst, const char *key, const char *val);  /* add PK_GRV_* branch */
static void omega_render_block(void *inst, int16_t *out_lr, int frames);     /* add groove sum */
/* render currently: model render into l[]/r[]; then out_lr[n*2]=omega_to_i16(l[n]*main_volume); */
```
</interfaces>

<tempo_algorithm>
<!-- VERBATIM tempo clock (C-RESEARCH §Pattern 2). Copy into groove.c; adapt field names to groove_state_t. -->
```c
/* Called ONCE per render block, before the per-sample loop. NEVER per sample. */
static void groove_update_tempo(groove_state_t *g, const host_api_v1_t *host, int frames) {
    float bpm = 0.0f;

    /* (1) PRIMARY: beat-delta from get_beat_position (GRV-02). */
    if (host && host->get_beat_position) {
        double beat = host->get_beat_position();
        if (beat >= 0.0) {                          /* >=0 => transport running */
            if (g->have_prev_beat) {
                double dbeat = beat - g->prev_beat;
                if (dbeat > 0.0 && dbeat < 4.0) {   /* sane per-block advance */
                    bpm = (float)(dbeat * 60.0 * (double)OMEGA_SR / (double)frames);
                }
            }
            g->prev_beat = beat;
            g->have_prev_beat = true;
        } else {
            g->have_prev_beat = false;              /* stopped: drop stale prev */
        }
    }

    /* (2) FALLBACK: get_bpm (NULL-guarded, sanity-clamped). */
    if (bpm <= 0.0f && host && host->get_bpm) {
        float b = host->get_bpm();
        if (b >= 20.0f && b <= 999.0f) bpm = b;
    }

    /* (3) LAST RESORT: 120 constant — ONLY when no transport info at all. */
    if (bpm <= 0.0f) bpm = g->last_bpm > 0.0f ? g->last_bpm : 120.0f;

    /* Smooth jitter; recompute the tap interval ONLY when BPM actually moves. */
    bpm = clampf(bpm, 20.0f, 300.0f);
    g->bpm_smooth += (bpm - g->bpm_smooth) * 0.20f;         /* one-pole EMA */
    if (fabsf(g->bpm_smooth - g->last_bpm) > 0.5f) {        /* control-rate re-lock */
        g->last_bpm = g->bpm_smooth;
        int spq = (int)((60.0f / g->bpm_smooth) * OMEGA_SR / 4.0f + 0.5f);  /* samples_per_16th */
        if (spq < 1) spq = 1;
        if (spq * 4 > (int)(GRV_DELAY_MASK)) spq = (int)(GRV_DELAY_MASK) / 4;  /* clamp reach */
        g->samples_per_16th = spq;
    }
}
```
NOTE: `bpm_smooth` and `last_bpm` MUST be initialised so the first block does not divide by zero. The calloc zero-inits them; guard `bpm_smooth <= 0` on first use by seeding `g->bpm_smooth = 120.0f` and `g->last_bpm = 120.0f` + `g->samples_per_16th = (int)((60.0f/120.0f)*OMEGA_SR/4.0f + 0.5f)` in a groove_init called from create_instance, so the very first block already has a valid interval.
</tempo_algorithm>

<delay_and_page1>
<!-- VERBATIM 4-tap read (C-RESEARCH §Pattern 1) + Page-1 tail (§Pattern 3). Copy into groove.c. -->
```c
#define GRV_DELAY_LEN  131072u          /* power-of-two ring (~3 s @44.1k) */
#define GRV_DELAY_MASK (GRV_DELAY_LEN - 1u)   /* branch-free & mask wrap (DC-01) */

/* per sample (inside groove_tick): kick_l/kick_r are this sample's kick output. */
g->buf_l[g->write_pos] = kick_l;
g->buf_r[g->write_pos] = kick_r;
float gl = 0.0f, gr = 0.0f;
for (int t = 0; t < 4; t++) {
    unsigned rp = (g->write_pos - (unsigned)((t + 1) * g->samples_per_16th)) & GRV_DELAY_MASK;
    float w = g->tap_level[t] * g->tap_decay[t];   /* TAP level * LENGTH decay weight */
    gl += g->buf_l[rp] * w;
    gr += g->buf_r[rp] * w;
}
g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;

/* Page-1 tail: COLOR LP + MONO + VOL (control-rate coeffs precomputed in set_param). */
gl = tpt1_lp(&g->color_lp_l, gl, g->color_g);
gr = tpt1_lp(&g->color_lp_r, gr, g->color_g);
if (g->mono) { float m = 0.5f * (gl + gr); gl = gr = m; }   /* GRV-05 sub-bass mono sum */
gl *= g->vol; gr *= g->vol;
*out_gl = gl; *out_gr = gr;
```
Page-1 param mappings (control-rate, in groove_set_param; v = clampf(parse_f(val),0,1)):
- PK_GRV_VOL     -> g->vol = v
- PK_GRV_LENGTH  -> per-tap decay weights: `g->tap_decay[t] = powf(v_shaped, (float)(t+1))` where a longer LENGTH keeps later taps louder — e.g. `float base = 0.3f + 0.65f*v; for t in 0..3: g->tap_decay[t] = powf(base, (float)(t+1));` (discretion: musical decay curve; powf at CONTROL rate only)
- PK_GRV_COLOR   -> `float fc = 200.0f + v*(18000.0f-200.0f); g->color_g = tanf((float)M_PI*fc/OMEGA_SR);`  (reuse the tpt_g_from_hz idiom from gen.c)
- PK_GRV_TAP1..4 -> g->tap_level[0..3] = v
- PK_GRV_MONO    -> g->mono = (v >= 0.5f)
</delay_and_page1>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Define groove_state_t + PK_GRV_* macros in omega.h, raise the instance-size assert</name>
  <files>src/groove.h, src/omega.h</files>
  <read_first>
    - src/omega.h lines 123-149 (bohm_instance layout + the `< 800000` _Static_assert) and lines 151-229 (PK_* macro block)
    - C-CONTEXT.md DC-01, DC-03, DC-08 (buffer sizing, Page-1 controls, raise the assert)
    - C-RESEARCH.md §Recommended state placement + §Pitfall 5 (mask buffer is ~1.0 MB; the DC-08 "1,100,000" is illustrative — size the assert to the TRUE mask footprint, ~1.3 MB)
    - C-RESEARCH.md Open Q1 + Open Q3 (mask sizing; new PK_GRV_* / PK_GEN_SEQLEN/LPFFREQ/LPFPOLE key naming — lowercase strings)
  </read_first>
  <action>
    Create src/groove.h defining the NEW `groove_state_t` (Phase C owns it; NOT part of the locked host ABI). It must contain, all by value (zero-init by the single calloc):
    ```c
    #include "dsp_primitives.h"   /* tpt1_t, OMEGA_SR */
    #include <stdbool.h>
    #define GRV_DELAY_LEN  131072u
    #define GRV_DELAY_MASK (GRV_DELAY_LEN - 1u)
    typedef struct groove_state {
        float buf_l[GRV_DELAY_LEN];     /* circular delay rings (~512 KB each) */
        float buf_r[GRV_DELAY_LEN];
        unsigned write_pos;
        /* tempo clock */
        double prev_beat; bool have_prev_beat;
        float  bpm_smooth, last_bpm; int samples_per_16th;
        /* Page-1 params */
        float vol; float tap_level[4]; float tap_decay[4];
        float color_g; tpt1_t color_lp_l, color_lp_r;
        bool  mono;
    } groove_state_t;
    ```
    Declare the groove API (implemented in C-02 Task 2):
    ```c
    struct bohm_instance;   /* fwd */
    void groove_init(groove_state_t *g);
    void groove_update_tempo(groove_state_t *g, const struct host_api_v1 *host, int frames);
    void groove_tick(groove_state_t *g, float kick_l, float kick_r, float *out_gl, float *out_gr);
    void groove_set_param(groove_state_t *g, const char *key, const char *val);
    ```
    (Use `const struct host_api_v1 *` to avoid a typedef ordering issue, or include omega.h — pick whichever compiles cleanly; omega.h already defines host_api_v1_t.)

    In src/omega.h:
    - Add `#include "groove.h"` is NOT allowed if it creates a cycle (groove.h includes dsp_primitives.h which includes omega.h). Instead: put the `groove_state_t groove;` member on bohm_instance by forward-declaring the struct in omega.h and defining it fully in groove.h, OR define groove_state_t in omega.h directly (simplest, mirrors how usr_* fields live in omega.h). RECOMMENDED: define `groove_state_t` in groove.h but forward-declare `typedef struct groove_state groove_state_t;` in omega.h and store a POINTER — NO: the buffer must be by value in the single calloc. Therefore define `groove_state_t` fully in groove.h and `#include "groove.h"` from omega.h AFTER the OMEGA_SR/OMEGA_WT_* defines but guard the include cycle: groove.h must NOT re-include omega.h (it only needs tpt1_t; forward-declare tpt1_t or include dsp_primitives.h guarded). CLEANEST PATH given the existing cycle: define `groove_state_t` INLINE in omega.h (right before struct bohm_instance), reusing the existing pattern where omega.h already declares the OMEGA_WT_* geometry to keep the instance layout self-contained without pulling dsp_primitives.h. Define GRV_DELAY_LEN/MASK and the tpt1 state as a bare `float color_lp_l_s, color_lp_r_s;` (store the tpt1 `.s` state as plain floats in omega.h to avoid the include), and have groove.c cast/wrap them. Choose the approach that keeps omega.h free of a dsp_primitives.h include; document the choice in a comment.
    - Add `groove_state_t groove;` as the last member of struct bohm_instance.
    - RAISE the assert: compute the true sizeof (two 131072-float rings = 1,048,576 B + model_state 4096 + usr buffers ~184 KB + small fields ≈ 1.24 MB) and set `_Static_assert(sizeof(struct bohm_instance) < 1300000, "instance under 1.3MB");`. If the build reports the real size differs, set the bound just above the reported value (never pad).
    - Add the PK_GRV_* macros in the PK_* block (lowercase strings, unique across the flat strcmp dispatch):
      ```c
      #define PK_GRV_VOL    "grv_vol"
      #define PK_GRV_LENGTH "grv_length"
      #define PK_GRV_COLOR  "grv_color"
      #define PK_GRV_TAP1   "grv_tap1"
      #define PK_GRV_TAP2   "grv_tap2"
      #define PK_GRV_TAP3   "grv_tap3"
      #define PK_GRV_TAP4   "grv_tap4"
      #define PK_GRV_MONO   "grv_mono"
      ```
    These string values MUST match the literals C-01's test_groove.c uses ("grv_vol","grv_length","grv_color","grv_tap1".."grv_tap4","grv_mono").
    Do NOT alter host_api_v1_t / plugin_api_v2_t / their +120/+56 asserts.
  </action>
  <acceptance_criteria>
    - `test -f src/groove.h` and `grep -q "groove_state_t" src/groove.h`
    - `grep -q "GRV_DELAY_MASK" src/groove.h`
    - omega.h: `grep -q "PK_GRV_VOL" src/omega.h` and all eight PK_GRV_* macros present (`grep -c "PK_GRV_" src/omega.h` >= 8)
    - omega.h: `grep -q "groove" src/omega.h` shows the new bohm_instance member
    - The raised assert exists and the old 800000 bound is gone as the instance guard: `grep -q "sizeof(struct bohm_instance) < 1" src/omega.h` (bound >= 1,300,000) and `! grep -q "sizeof(struct bohm_instance) < 800000" src/omega.h`
    - Host ABI asserts untouched: `grep -q "reserved) == 120" src/omega.h` and `grep -q "render_block) == 56" src/omega.h`
    - It compiles (the assert holds): `make test-switch` exits 0 after Task 2 & 3 land the groove.c symbols — if Task 1 is verified in isolation, at minimum `$(CC) -std=gnu11 -fsyntax-only -Isrc -Itests src/omega.h` reports no error (or defer full compile to Task 3's verify).
  </acceptance_criteria>
  <verify>
    <automated>cc -std=gnu11 -fsyntax-only -Isrc src/groove.h 2>&1; grep -q "PK_GRV_VOL" src/omega.h && grep -q "groove_state_t" src/groove.h</automated>
  </verify>
  <done>groove_state_t (delay rings + tempo clock + Page-1 params) is defined, bohm_instance carries it by value inside the single calloc, the size assert is raised to the true mask footprint, and all eight PK_GRV_* macros exist with the exact strings C-01's harness expects. Host ABI is untouched.</done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: Implement groove.c — tempo clock, mask-wrapped 4-tap read, COLOR/MONO/VOL, groove_set_param</name>
  <files>src/groove.c</files>
  <read_first>
    - src/groove.h (Task 1's struct + API decls)
    - The <tempo_algorithm> block above (VERBATIM groove_update_tempo — copy it, adapt field names)
    - The <delay_and_page1> block above (VERBATIM 4-tap read + Page-1 tail + param mappings)
    - src/dsp_primitives.h lines 151-160 (tpt1_lp) and 165-170 (omega_to_i16) — reuse, do not reimplement
    - src/models/gen.c lines 82-104 (parse_f + clampf + tpt_g_from_hz idioms to copy) and lines 38-40 (the GEN_STEP_FRAMES hardcode this phase kills — for context, NOT to touch here)
    - C-RESEARCH.md §Pitfall 1-4 (hardcoded BPM, NULL/negative transport, ring overflow clamp, jitter EMA)
  </read_first>
  <behavior>
    - groove_update_tempo at driven 120 BPM (dbeat advanced per block) settles samples_per_16th to (int)((60/120)*44100/4 + 0.5) == 5513 (±1 after EMA settles).
    - At 128 BPM → ~5168; at 174 BPM → ~3802 — the three values are DISTINCT (GRV-02 SC1).
    - With host==NULL or both callbacks NULL, samples_per_16th falls back to the 120-derived value (5513) via the last-resort constant; no NULL deref, output finite.
    - With get_beat_position returning < 0 (stopped), have_prev_beat resets and the get_bpm fallback (or 120) supplies the interval — no giant spurious delta on resume.
    - groove_tick writes kick into the ring and reads 4 taps behind the write head; a lone impulse at t=0 reappears (nonzero) at ~samples_per_16th, 2×, 3×, 4× later, scaled by tap_level*tap_decay.
    - MONO on → out_gl == out_gr exactly for every sample. VOL scales output linearly. COLOR at low cutoff attenuates high-frequency content.
    - Output is always finite and, for in-range taps, bounded (the kick+groove sum bound is enforced at the int16 boundary in dsp.c, not here — do NOT clamp inside groove).
  </behavior>
  <action>
    Create src/groove.c implementing the four API functions from groove.h.

    Copy the <tempo_algorithm> `groove_update_tempo` VERBATIM, adapting field names to groove_state_t. Add a static `clampf` (copy from gen.c) and `#include <math.h>` for fabsf. This is the DC-02/GRV-02 crux: the ONLY tempo source. There must be NO hardcoded live tap interval — the 120.0f constant may appear ONLY in the last-resort fallback branch.

    Implement `groove_init(g)`: seed `g->bpm_smooth = 120.0f; g->last_bpm = 120.0f; g->samples_per_16th = (int)((60.0f/120.0f)*OMEGA_SR/4.0f + 0.5f);` and default Page-1 params to musical middles (vol=0.7, tap_level[t]=0.6, tap_decay via LENGTH=0.5 curve, color_g from ~8 kHz, mono=false) so a bare create → trigger is audible. have_prev_beat=false, write_pos=0.

    Implement `groove_tick(g, kick_l, kick_r, out_gl, out_gr)`: copy the <delay_and_page1> per-sample body VERBATIM. If groove.h stored the tpt1 state as bare floats (per Task 1's include-cycle decision), wrap them in local `tpt1_t` views or operate on the `.s` fields directly using the tpt1_lp math inlined — either way reuse the exact tpt1_lp formula, never a biquad (CLAUDE.md/DC-06).

    Implement `groove_set_param(g, key, val)`: `float v = clampf(parse_f(val), 0.0f, 1.0f);` then the control-rate mappings from <delay_and_page1> (VOL/LENGTH/COLOR/TAP1-4/MONO). All powf/tanf are CONTROL-rate here, never in groove_tick. Unknown keys ignored.

    RT-safety: no malloc/free, no host->log, no file I/O, no per-sample division/transcendental in groove_tick. parse_f is the locale-independent parser (copy from gen.c/dsp.c), NOT atof.
  </action>
  <acceptance_criteria>
    - `test -f src/groove.c`
    - Tempo source is the host callback, not a constant: `grep -q "get_beat_position" src/groove.c` and `grep -q "get_bpm" src/groove.c`
    - The forbidden hardcode is absent as a LIVE interval: `! grep -qE "0\.125f|sample_rate \* 0\.125|\* 0\.125f" src/groove.c` and the only `120` is in the fallback (`grep -n "120" src/groove.c` shows it inside the last-resort branch / groove_init seed, never as a tap interval).
    - Mask wrap, not modulo, in the hot loop: `grep -q "& GRV_DELAY_MASK" src/groove.c` and `! grep -q "% GRV_DELAY_LEN\|% MAX_DELAY" src/groove.c`
    - MONO sum present: `grep -q "0.5f \* (gl + gr)\|0.5f\*(gl+gr)\|0.5f \* (gl+gr)" src/groove.c` (mono averaging) and `grep -q "g->mono" src/groove.c`
    - samples_per_16th recomputed via the DC-02 formula: `grep -q "60.0f / g->bpm_smooth\|60.0f/g->bpm_smooth" src/groove.c` and `grep -q "OMEGA_SR / 4.0f\|OMEGA_SR/4.0f" src/groove.c`
    - No per-sample transcendental in groove_tick: the function body between its `{` and matching `}` contains no `powf(`/`tanf(`/`sinf(`/`expf(` (verify by reading; the tempo/set_param functions may use them at control rate).
    - Compiles cleanly into the switch harness once Task 3 wires it: verified in Task 3.
  </acceptance_criteria>
  <verify>
    <automated>cc -std=gnu11 -O2 -Isrc -c src/groove.c -o /tmp/groove.o && grep -q "get_beat_position" src/groove.c && grep -q "& GRV_DELAY_MASK" src/groove.c && ! grep -qE "\* 0\.125f" src/groove.c</automated>
  </verify>
  <done>groove.c derives BPM only from the guarded host transport chain (never a live 120), reads 4 mask-wrapped 16th-note taps of the kick, applies COLOR/MONO/VOL at the correct rates, and compiles. The forbidden hardcoded-interval bug is provably absent.</done>
</task>

<task type="auto">
  <name>Task 3: Wire groove into dsp.c (kick+groove sum + set_param dispatch) and the Makefile; turn test_groove GREEN</name>
  <files>src/dsp.c, Makefile</files>
  <read_first>
    - src/dsp.c lines 156-194 (omega_create single calloc — add groove_init), 212-243 (omega_set_param dispatch — add the PK_GRV_* branch), 265-287 (omega_render_block — add the groove stage + sum + Phase-D marker)
    - src/groove.h (the API to call)
    - C-RESEARCH.md §Pattern 6 (signal path & Phase-D insertion point — VERBATIM below)
    - Makefile lines 40-83 (DSP_SRCS uses src/*.c wildcard which already picks up src/groove.c; each *_TEST_SRCS explicitly lists src files — groove.c must be added to the test src lists) and 116-119 (dsp.so target)
    - tests/test_groove.c (C-01's RED harness this task turns GREEN)
  </read_first>
  <action>
    Wire the groove voice into dsp.c following C-RESEARCH §Pattern 6:

    In `omega_create` (after the calloc + USR load, before priming kick params): call `groove_init(&inst->groove);`. (Include "groove.h" at the top of dsp.c.)

    In `omega_render_block`, after the model renders into `l[]`/`r[]` and BEFORE the int16 output loop, insert:
    ```c
    /* Groove rumble (Phase C). GEN's own output IS the rumble (DC-05, wired in C-03);
     * for now every model feeds the kick-fed multitap. */
    groove_update_tempo(&inst->groove, g_host, frames);   /* once per block (Pattern 2) */
    for (int n = 0; n < frames; n++) {
        float gl, gr;
        groove_tick(&inst->groove, l[n], r[n], &gl, &gr);
        l[n] += gl;  r[n] += gr;                           /* kick + groove sum */
    }
    /* <<< PHASE D INSERTION POINT: duck -> DJ filter -> soft clip go HERE, on l[]/r[] >>> */
    ```
    (The existing `out_lr[n*2] = omega_to_i16(l[n]*inst->main_volume)` loop stays as-is below the marker — it is the FNDTN-07 net for the sum, which may exceed 1.0; do NOT add a clamp inside the groove stage — that headroom management is Phase D.)

    In `omega_set_param`, add a groove-key branch BEFORE the model-vtable fallback (the final `else`). Match all eight PK_GRV_* keys and route to `groove_set_param(&inst->groove, key, val)`. Keep PK_MODEL/PK_MASTER_VOL handling first; the PK_GRV_* branch sits between PK_MASTER_VOL and the model-vtable else. Use a helper predicate (e.g. `is_groove_key(key)`) or an explicit `strcmp` chain — the groove keys are model-independent and must NOT go through the kick model vtable.

    Makefile: add `src/groove.c` to every test src list that currently lists individual src files — GROOVE_TEST_SRCS (C-01), TEST_SRCS, SWITCH_TEST_SRCS, PARAMS_TEST_SRCS, DISTINCT_TEST_SRCS, GEN_TEST_SRCS, FM2_TEST_SRCS(if it links dsp.c — it does not, skip), FX_TEST_SRCS(skip, dsp_primitives only). Any list that includes `src/dsp.c` MUST also include `src/groove.c` (dsp.c now calls groove_*). `DSP_SRCS = $(wildcard src/*.c)` already picks up src/groove.c for the cross-build — verify no change needed there.

    After wiring, C-01's `make test-groove` must go GREEN (GRV-01/02/03/05 assertions pass).
  </action>
  <acceptance_criteria>
    - dsp.c calls the groove API: `grep -q "groove_init" src/dsp.c` and `grep -q "groove_update_tempo" src/dsp.c` and `grep -q "groove_tick" src/dsp.c` and `grep -q "groove_set_param" src/dsp.c`
    - The kick+groove sum is present: `grep -q "l\[n\] += gl" src/dsp.c` and `grep -q "r\[n\] += gr" src/dsp.c`
    - The Phase-D insertion marker exists: `grep -q "PHASE D INSERTION POINT" src/dsp.c`
    - No clamp added inside the groove stage (the sum is bounded only at omega_to_i16): the groove loop does not call omega_to_i16 or clamp gl/gr.
    - Makefile: `grep -q "src/groove.c" Makefile` and every test src list that has `src/dsp.c` also has `src/groove.c` (SWITCH_TEST_SRCS, GEN_TEST_SRCS, PARAMS_TEST_SRCS, DISTINCT_TEST_SRCS, TEST_SRCS, GROOVE_TEST_SRCS).
    - The C-01 harness passes: `make test-groove` exits 0 (GRV-01/02/03/05 GREEN).
    - No regressions: `make test` exits 0 (all sub-suites green).
    - RT-safety intact: no `host->log` added to dsp.c; the render loop adds no malloc/file-I/O.
  </acceptance_criteria>
  <verify>
    <automated>make test</automated>
  </verify>
  <done>The groove voice is summed with the kick in render_block behind a labeled Phase-D insertion point, groove Page-1 keys dispatch through groove_set_param (not the model vtable), src/groove.c is compiled into every relevant build, and `make test` (including test-groove) is fully green — GRV-01/02/03/05 verified offline.</done>
</task>

</tasks>

<verification>
- `make test` exits 0 (test-groove GREEN plus all existing sub-suites).
- `cc -std=gnu11 -O2 -Isrc -c src/groove.c` compiles.
- `! grep -qE "\* 0\.125f" src/groove.c` (forbidden hardcoded interval absent) and `grep -q "get_beat_position" src/groove.c` (tempo from host).
- omega.h size assert raised to >= 1,300,000 and host ABI asserts (+120/+56) untouched.
- dsp.c contains the "PHASE D INSERTION POINT" marker and the kick+groove sum.
</verification>

<success_criteria>
- The 4-tap rumble tracks driven BPM (120/128/174 produce distinct samples_per_16th) via the guarded get_beat_position chain — never a live 120.
- Groove Page 1 (VOL/LENGTH/COLOR/TAP1-4) measurably shapes the rumble; MONO force-sums L+R.
- The kick+groove sum is written to output with a clean Phase-D insertion point and no premature clamp.
- The instance still allocates in a single calloc; the size assert reflects the true footprint.
</success_criteria>

<output>
After completion, create `.planning/phases/C-groove-rumble-engine/C-02-SUMMARY.md`
</output>
