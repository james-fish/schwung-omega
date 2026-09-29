---
phase: C-groove-rumble-engine
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - tests/mock_host.c
  - tests/mock_host.h
  - tests/test_groove.c
  - Makefile
autonomous: true
requirements: [GRV-01, GRV-02, GRV-03, GRV-05]
must_haves:
  truths:
    - "The mock host can be driven to advance beat position and report a BPM, and can present a NULL-transport variant"
    - "A test_groove harness exists, is wired into `make test`, and fails (RED) until the groove engine lands in C-02"
  artifacts:
    - path: "tests/mock_host.c"
      provides: "Drivable get_beat_position (settable module-static beat) + mock_get_bpm stub + NULL-transport host variant"
      contains: "make_mock_host_null_transport"
    - path: "tests/mock_host.h"
      provides: "Declarations for the beat setter, advance helper, and NULL-transport host constructor"
      contains: "mock_host_set_beat"
    - path: "tests/test_groove.c"
      provides: "Groove test harness: parametric BPM sweep, MONO channel equality, Page-1 responsiveness, finite/bounded"
      contains: "120"
    - path: "Makefile"
      provides: "GROOVE_TEST_SRCS + test-groove target folded into the aggregate test target"
      contains: "test-groove"
  key_links:
    - from: "tests/test_groove.c"
      to: "tests/mock_host.c"
      via: "mock_host_set_beat / mock_host_advance_beat before each render block"
      pattern: "mock_host_set_beat|mock_host_advance_beat"
    - from: "Makefile test target"
      to: "build/test_groove"
      via: "test-groove prerequisite"
      pattern: "test-groove"
---

<objective>
Wave 0 enabling work: make the offline mock host tempo-drivable and stand up the `test_groove.c` harness (wired into `make test`) BEFORE the tempo clock and groove engine are written in C-02. This retires the phase's value-at-risk (GRV-02 live tempo derivation) by making it testable offline first.

Purpose: The single genuinely-new piece of test infrastructure for Phase C is a drivable `get_beat_position()`. Without it, GRV-02 cannot be asserted offline. This plan closes that gap and writes the (initially RED) harness so C-02/C-03 execute against real assertions.
Output: An extended `tests/mock_host.{c,h}` (drivable beat + BPM stub + NULL-transport variant) and a new `tests/test_groove.c` plus a `test-groove` Makefile target folded into `make test`.
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
<!-- Host ABI transport callbacks the mock must drive (src/omega.h, LOCKED — do NOT alter). -->
From src/omega.h (host_api_v1_t):
```c
float  (*get_bpm)(void);            /* may be NULL on older hosts */
double (*get_beat_position)(void);  /* returns < 0 when transport stopped; may be NULL */
int sample_rate;                    /* 44100 in the mock */
int frames_per_block;               /* 128 in the mock */
```

Current tests/mock_host.c (what you are extending):
```c
static double mock_beat(void) { return 0.0; }   /* becomes drivable */
host_api_v1_t make_mock_host(void) {
    host_api_v1_t h = {0};
    h.api_version = 1; h.sample_rate = 44100; h.frames_per_block = 128;
    h.log = mock_log; h.midi_send_internal = mock_midi; h.midi_send_external = mock_midi;
    h.get_clock_status = mock_clock; h.get_beat_position = mock_beat;
    /* NOTE: h.get_bpm is currently left NULL. */
    return h;
}
```

Current tests/mock_host.h:
```c
host_api_v1_t make_mock_host(void);
```

Plugin ABI entry points the harness drives (src/omega.h plugin_api_v2_t):
```c
void* (*create_instance)(const char *module_dir, const char *json_defaults);
void  (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
void  (*set_param)(void *instance, const char *key, const char *val);
void  (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);
```

Groove param keys the harness references (added in C-02 — see acceptance note below):
`PK_GRV_VOL`, `PK_GRV_LENGTH`, `PK_GRV_COLOR`, `PK_GRV_TAP1..4`, `PK_GRV_MONO`, `PK_MODEL`, `PK_MASTER_VOL`.
</interfaces>

<test_patterns>
<!-- Reuse the existing harness idioms. -->
- test_switch.c: `move_plugin_init_v2(&host)` -> `api->create_instance("/tmp/omega","{}")` -> `set_param(PK_MODEL, "0")` -> `on_midi({0x90,36,100},3,0)` -> `render_block(out, 128)`; asserts `out[i]` in `[INT16_MIN,INT16_MAX]`; sums `fabs((double)out[i])` for non-silence (`> 1000.0`).
- test_gen.c: `snprintf(idx,...,"%d",MODEL_GEN)`; primes Page-1 keys to "0.5"; renders NBLOCKS of 128 into an int16 buffer.
- Makefile: each `*_TEST_SRCS` uses `$(wildcard src/models/*.c)` + `src/dsp.c src/ui.c src/dsp_primitives.c tests/mock_host.c tests/wav.c tests/malloc_trap.c`; each `test-*` target compiles with `$(CC) $(TEST_FLAGS) ... $(LDLIBS)` then runs the binary; `test:` lists the sub-targets as prerequisites.
</test_patterns>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Make the mock host tempo-drivable + add a NULL-transport variant</name>
  <files>tests/mock_host.c, tests/mock_host.h</files>
  <read_first>
    - tests/mock_host.c (current stub — mock_beat returns 0.0, get_bpm left NULL)
    - tests/mock_host.h (current single declaration)
    - src/omega.h lines 43-71 (host_api_v1_t: get_bpm at +..., get_beat_position, reserved[8] at +120 — do NOT alter the struct)
    - C-RESEARCH.md §Wave 0 Gaps + §Pattern 2 (the fallback chain the mock must exercise: beat-delta → get_bpm → 120 constant)
    - C-VALIDATION.md §Wave 0 Requirements (drivable g_mock_beat + setter, mock_get_bpm stub, NULL-transport variant)
  </read_first>
  <action>
    Extend tests/mock_host.c and tests/mock_host.h so the harness can drive transport.

    In tests/mock_host.c, replace the fixed `mock_beat` with a drivable one backed by a module-static double:
    ```c
    static double g_mock_beat = 0.0;   /* current beat position (quarter notes) */
    static float  g_mock_bpm  = 120.0f;/* BPM reported by mock_get_bpm */
    static double mock_beat(void) { return g_mock_beat; }
    static float  mock_get_bpm(void) { return g_mock_bpm; }
    ```
    Add public setters/advancers (definitions in .c, declarations in .h):
    ```c
    void  mock_host_set_beat(double beat)     { g_mock_beat = beat; }
    void  mock_host_advance_beat(double dbeat){ g_mock_beat += dbeat; }
    void  mock_host_set_bpm(float bpm)        { g_mock_bpm = bpm; }
    ```
    In `make_mock_host()`, wire `h.get_bpm = mock_get_bpm;` (currently NULL) alongside the existing `h.get_beat_position = mock_beat;`.

    Add a SECOND constructor `make_mock_host_null_transport()` that returns a host with BOTH `get_beat_position` and `get_bpm` set to NULL (leave the other stubs — log/midi/clock/sample_rate/frames_per_block — populated exactly as make_mock_host). This exercises C-02's last-resort 120 constant path (C-RESEARCH Pitfall 2).

    Reset semantics: `make_mock_host()` MUST also reset `g_mock_beat = 0.0` and `g_mock_bpm = 120.0f` so each test starts from a known transport state.

    In tests/mock_host.h, declare all four new functions:
    ```c
    host_api_v1_t make_mock_host(void);
    host_api_v1_t make_mock_host_null_transport(void);
    void mock_host_set_beat(double beat);
    void mock_host_advance_beat(double dbeat);
    void mock_host_set_bpm(float bpm);
    ```
    Do NOT touch src/omega.h or the host ABI struct. Do NOT add host->log calls anywhere new.
  </action>
  <acceptance_criteria>
    - `grep -q "mock_host_set_beat" tests/mock_host.h` AND `grep -q "mock_host_set_beat" tests/mock_host.c`
    - `grep -q "mock_host_advance_beat" tests/mock_host.c`
    - `grep -q "make_mock_host_null_transport" tests/mock_host.h` AND in tests/mock_host.c
    - `grep -q "mock_get_bpm" tests/mock_host.c` AND `grep -q "h.get_bpm" tests/mock_host.c`
    - The NULL-transport variant sets both callbacks NULL: `grep -A20 "make_mock_host_null_transport" tests/mock_host.c` shows `get_beat_position = NULL` (or `= 0`) and `get_bpm = NULL` (or leaves them unset via `{0}`).
    - src/omega.h is byte-identical to before this task: `git diff --quiet src/omega.h` (no ABI drift).
    - Existing suite still compiles + passes: `make test-switch` exits 0 (mock_host.c change does not break existing callers).
  </acceptance_criteria>
  <verify>
    <automated>make test-switch</automated>
  </verify>
  <done>The mock host advances beat position on demand, reports a settable BPM, and offers a both-callbacks-NULL variant; existing tests still green; omega.h untouched.</done>
</task>

<task type="auto">
  <name>Task 2: Write the RED test_groove.c harness + wire the Makefile target</name>
  <files>tests/test_groove.c, Makefile</files>
  <read_first>
    - tests/test_switch.c (harness idiom: init → create → set_param → on_midi → render_block; int16 range asserts; non-silence via summed fabs)
    - tests/test_gen.c lines 40-60 (Page-1 priming to "0.5"; MODEL selection via snprintf index)
    - Makefile lines 40-83 (the *_TEST_SRCS + test-* target pattern) and lines 124-127 (the aggregate `test:` target)
    - C-VALIDATION.md §Per-Task Verification Map (the exact GRV-01/02/03/05 assertions this harness must make)
    - C-RESEARCH.md §Pattern 2 (samples_per_16th = (60/bpm)*sr/4) and §Validation Architecture (BPM sweep 120/128/174)
  </read_first>
  <action>
    Create tests/test_groove.c — the offline harness for GRV-01/02/03/05. It drives the REAL plugin (move_plugin_init_v2 → create_instance → set_param → on_midi → render_block) exactly like test_switch.c. Because the groove engine does not exist until C-02, this harness is EXPECTED to fail to link/compile now — that is the intended RED state. Guard the not-yet-existing groove keys so the file still COMPILES against C-02's omega.h additions (the PK_GRV_* macros land in C-02); to keep this plan's own `make test-groove` from erroring on undefined macros, reference the groove keys as string literals matching the C-02 naming (`"grv_vol"`, `"grv_length"`, `"grv_color"`, `"grv_tap1".."grv_tap4"`, `"grv_mono"`) so the file compiles independently, and add a top-of-file comment noting these mirror the PK_GRV_* macros defined in C-02.

    The harness must implement these assertions (write them now; they go GREEN when C-02 lands):

    1. GRV-02 parametric BPM sweep — the crux. For each BPM in {120, 128, 174}:
       - Reset the mock (`make_mock_host`), init plugin, create instance, select a NON-GEN model (MODEL_FM2 = "0"), prime Page-1 to "0.5", trigger a note-on.
       - Simulate transport: before EACH 128-frame render block, advance the mock beat by `dbeat = bpm/60.0 * (128/44100.0)` quarter-notes via `mock_host_advance_beat(dbeat)` (this is the inverse of C-02's `bpm = dbeat*60*sr/frames`). Render ~64 blocks so the EMA in C-02 settles.
       - Assert the observed period differs across BPMs: capture the groove output's autocorrelation-peak lag OR simply assert that the expected `samples_per_16th = (int)((60.0/bpm)*44100.0/4.0 + 0.5)` values are DISTINCT (120→~5513, 128→~5168, 174→~3802) and that rendered output is non-silent + finite/bounded at each BPM. (v1: assert distinctness of the computed interval + non-silent tempo-driven output; the on-device "feel" is deferred.)
       - Fallback chain: (a) with `make_mock_host_null_transport()` (both callbacks NULL) render is still finite/bounded/non-silent (last-resort 120 constant); (b) with beat_position returning a negative value (call `mock_host_set_beat(-1.0)`) the get_bpm fallback path is exercised — output still finite/bounded.

    2. GRV-01 4-tap delay presence: trigger a kick, render enough blocks that tap 1 (at samples_per_16th behind the write head) would have fired; assert the buffer has energy AFTER the initial kick attack window (delayed taps produce later energy), and every sample is finite and `|x| <= 1.0` (reconstruct float from int16 or assert int16 range as test_switch does).

    3. GRV-03 Page-1 responsiveness (reuse the assert_param_responsive STYLE from test_params.c): for each of grv_vol/grv_length/grv_color/grv_tap1..4, render with the key at "0.0" vs "1.0" and assert the summed-abs energy (or RMS) DIFFERS measurably; bounded at both extremes.

    4. GRV-05 MONO: with `"grv_mono"` = "1" and asymmetric taps (e.g. tap1=1.0, tap3=0.0 vs a setting that would differ L/R), assert rendered L channel == R channel sample-for-sample (out[2n] == out[2n+1]); with `"grv_mono"` = "0" and an asymmetric configuration assert at least one frame has L != R. (Note: the base kick is mono L==R; MONO equality is meaningful once the groove voice can produce L!=R — assert MONO forces equality unconditionally, and document that the off-case L!=R depends on C-02 producing channel-asymmetric taps; if C-02's groove is symmetric, keep the MONO-on equality assertion as the load-bearing GRV-05 check.)

    Cross-cutting: every rendered buffer asserted finite + `|x|<=1.0` (int16 range) and at least one non-silent (summed fabs > 1000.0), mirroring test_switch.

    Then wire the Makefile:
    - Add `GROOVE_TEST_SRCS = tests/test_groove.c tests/mock_host.c tests/wav.c tests/malloc_trap.c src/dsp.c src/ui.c $(wildcard src/models/*.c) src/dsp_primitives.c`.
    - Add a `test-groove` target mirroring `test-gen` (mkdir build tests/output; `$(CC) $(TEST_FLAGS) $(GROOVE_TEST_SRCS) -o build/test_groove $(LDLIBS)`; `./build/test_groove`).
    - Add `test-groove` to the `.PHONY` line and as a prerequisite of the aggregate `test:` target.
  </action>
  <acceptance_criteria>
    - `test -f tests/test_groove.c`
    - `grep -q "174" tests/test_groove.c` AND `grep -q "128" tests/test_groove.c` AND `grep -q "120" tests/test_groove.c` (parametric BPM sweep present)
    - `grep -q "mock_host_advance_beat\|mock_host_set_beat" tests/test_groove.c` (drives the mock transport)
    - `grep -q "make_mock_host_null_transport" tests/test_groove.c` (last-resort fallback exercised)
    - `grep -q "grv_mono" tests/test_groove.c` AND `grep -q "grv_vol" tests/test_groove.c`
    - Makefile: `grep -q "test-groove" Makefile` (target + .PHONY + test: prerequisite) AND `grep -q "GROOVE_TEST_SRCS" Makefile`
    - `grep -q "test:.*test-groove\|test-groove" Makefile` confirms test-groove is a prerequisite of `test:`.
    - RED confirmation is acceptable: `make test-groove` is EXPECTED to fail now (undefined groove behavior / no groove sum yet). Prove the harness is real, not a stub, by confirming it references render_block and the BPM math: `grep -q "render_block\|api->render_block" tests/test_groove.c` and `grep -q "60.0\|60.0f\|/ 4" tests/test_groove.c`.
    - The rest of the suite is unaffected: `make test-switch` still exits 0.
  </acceptance_criteria>
  <verify>
    <automated>make test-switch && test -f tests/test_groove.c && grep -q "test-groove" Makefile</automated>
  </verify>
  <done>test_groove.c exists with real (RED) GRV-01/02/03/05 assertions driving the plugin through the tempo-drivable mock; the test-groove Makefile target is wired into `make test`; existing tests remain green. The harness goes GREEN when C-02 lands the groove engine.</done>
</task>

</tasks>

<verification>
- `make test-switch` exits 0 (existing suite unaffected by the mock_host extension).
- `git diff --quiet src/omega.h` (no host ABI drift in Wave 0).
- `tests/test_groove.c` exists and references the tempo-drivable mock + the 120/128/174 BPM sweep + grv_* keys.
- `test-groove` is a Makefile target and a prerequisite of `test:`.
- `make test-groove` fails RED now (groove engine absent) — this is the intended enabling state; it goes green in C-02.
</verification>

<success_criteria>
- The mock host can advance beat position, report a settable BPM, and present a both-callbacks-NULL variant.
- The RED test_groove harness encodes every GRV-01/02/03/05 offline assertion and is wired into `make test`.
- No changes to src/omega.h or the locked host/plugin ABI.
</success_criteria>

<output>
After completion, create `.planning/phases/C-groove-rumble-engine/C-01-SUMMARY.md`
</output>
