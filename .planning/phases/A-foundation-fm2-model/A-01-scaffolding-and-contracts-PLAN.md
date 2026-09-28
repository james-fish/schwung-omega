---
phase: A-foundation-fm2-model
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/omega.h
  - src/dsp_primitives.h
  - src/dsp_primitives.c
  - tools/gen_sine_table.c
  - src/sine_table.h
  - tests/mock_host.h
  - tests/mock_host.c
  - tests/malloc_trap.c
  - tests/wav.h
  - tests/wav.c
  - Makefile
  - scripts/glibc_gate.sh
  - .github/workflows/ci.yml
  - .gitignore
autonomous: true
requirements: [FNDTN-03, FNDTN-04, FNDTN-05, FNDTN-06, KICK-01, KICK-15]
must_haves:
  truths:
    - "make test compiles the native harness and exits 0 against a silent stub instance"
    - "The shared sine wavetable is 2049 floats in .rodata with guard sample t[2048]==t[0]"
    - "The kick_model_vtable_t contract and env_t/tpt1_t/wt_read primitives exist and are callable"
    - "glibc_gate.sh runs objdump checks (GLIBC<=2.35, no libmvec, exactly one export)"
    - "The malloc trap aborts on any heap call while g_audio_thread_active is true"
  artifacts:
    - path: "src/omega.h"
      provides: "kick_model_vtable_t, model_id_t (MODEL_FM2=0), bohm_instance_t, PARAM_KEY_* macros, OMEGA_SR/OMEGA_MAX_BLOCK"
      contains: "typedef struct"
    - path: "src/dsp_primitives.h"
      provides: "env_t, tpt1_t, wt_read, env_trigger/env_tick/env_coeff_from_ms, tpt1_lp, g_sine_table extern"
      contains: "env_t"
    - path: "src/dsp_primitives.c"
      provides: "g_sine_table[2049] .rodata definition + primitive implementations"
      contains: "g_sine_table"
    - path: "tests/mock_host.c"
      provides: "mock host_api_v1_t (44100 sr, 128 fpb)"
      contains: "make_mock_host"
    - path: "tests/malloc_trap.c"
      provides: "malloc/free/calloc/realloc interposition gated by g_audio_thread_active"
      contains: "g_audio_thread_active"
    - path: "tests/wav.c"
      provides: "44-byte PCM WAV writer"
      contains: "wav_open"
    - path: "Makefile"
      provides: "dsp.so (aarch64), test (native), clean, deploy targets"
      contains: "test:"
    - path: "scripts/glibc_gate.sh"
      provides: "objdump GLIBC/libmvec/export gate"
      contains: "GLIBC_"
    - path: ".github/workflows/ci.yml"
      provides: "docker cross-build + glibc gate + native test"
      contains: "schwung-builder"
  key_links:
    - from: "tests/test harness (stub)"
      to: "src/dsp_primitives.c wt_read + g_sine_table"
      via: "linked native compile"
      pattern: "wt_read"
    - from: "Makefile test target"
      to: "tests/malloc_trap.c"
      via: "-DOMEGA_MALLOC_TRAP compile + link"
      pattern: "OMEGA_MALLOC_TRAP"
---

<objective>
Establish the complete build/test scaffolding and shared contracts for Omega before any DSP is written. This is Wave 0 per A-VALIDATION.md: every subsequent task inherits an automated `make test` gate, the glibc CI gate, and the shared type/primitive contracts.

This plan defines the interfaces (omega.h vtable + instance struct, dsp_primitives), the shared `.rodata` sine wavetable (KICK-15), and the entire offline verification apparatus (mock host, malloc trap, WAV writer, glibc gate, GitHub Actions CI). It does NOT implement FM2 DSP — the harness renders against a temporary silent stub so the pipeline can go green end-to-end (create/render/destroy, malloc-trap clean, clamped int16) before DSP subjectivity enters.

Purpose: Retire the hardest RT-safety and toolchain risks first, and hand downstream plans concrete contracts so they build against blueprints rather than exploring.
Output: A compiling native test harness (`make test` exits 0), a cross-compilable `dsp.so` skeleton, the glibc gate script, CI workflow, and all shared headers/primitives.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/A-foundation-fm2-model/A-CONTEXT.md
@.planning/phases/A-foundation-fm2-model/A-RESEARCH.md
@.planning/phases/A-foundation-fm2-model/A-VALIDATION.md
@CLAUDE.md

<interfaces>
<!-- Verified API structs from Context/01_SCHWUNG_DEV_ARCHITECTURE.md lines 32-69. -->
<!-- Executor MUST replicate these EXACTLY — they are the host ABI. -->

host_api_v1_t (Context/01 lines 30-50):
```c
typedef struct {
    int api_version;             /* = 1 */
    int sample_rate;             /* Fixed at 44100 Hz */
    int frames_per_block;        /* typically 128 */
    void (*log)(const char *msg);
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);
    int (*get_clock_status)(void);
    double (*get_beat_position)(void); /* 24-PPQN synced */
} host_api_v1_t;
```

plugin_api_v2_t (Context/01 lines 54-66):
```c
typedef struct {
    int api_version;             /* = 2 */
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void  (*destroy_instance)(void *instance);
    void  (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void  (*set_param)(void *instance, const char *key, const char *val);
    int   (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    void  (*render_block)(void *instance, int16_t *out_lr, int frames);
} plugin_api_v2_t;

plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);
```

kick_model_vtable_t (A-RESEARCH.md §Pattern 1, lines 142-154 — LOCK these signatures):
```c
typedef struct bohm_instance bohm_instance_t;   /* fwd decl */
typedef struct {
    const char *name;                                   /* "FM2" */
    void (*trigger)(bohm_instance_t *inst, int note, int velocity);
    void (*render)(bohm_instance_t *inst, float *out_l, float *out_r, int frames);
    void (*set_p2)(bohm_instance_t *inst, const char *key, const char *val);
    int  (*p2_slot_desc)(bohm_instance_t *inst, char *buf, int buf_len);
} kick_model_vtable_t;
typedef enum { MODEL_FM2 = 0, MODEL_COUNT } model_id_t;  /* append-only, permanent */
```
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Define shared contracts (omega.h) and generate the .rodata sine table</name>
  <read_first>
    - src/ (confirm empty — greenfield)
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md §"Architecture Patterns" (Pattern 1 vtable lines 134-156, Pattern 2 wavetable lines 158-172)
    - .planning/phases/A-foundation-fm2-model/A-CONTEXT.md D-01, D-02, D-04, D-08, D-13
    - Context/01_SCHWUNG_DEV_ARCHITECTURE.md lines 30-69 (host/plugin API structs)
  </read_first>
  <action>
    Create `src/omega.h` (the single shared header, C11, include guard `OMEGA_H`):
    - `#include <stdint.h>`, `<stdbool.h>`, `<stddef.h>`.
    - `#define OMEGA_SR 44100.0f` and `#define OMEGA_MAX_BLOCK 256` (margin > 128 frames per CLAUDE.md "add MAX_BLOCK margin to scratch"; never size scratch to exactly 2x128).
    - Copy the host_api_v1_t and plugin_api_v2_t struct definitions VERBATIM from the <interfaces> block above (Context/01 ABI — do not alter field order or types).
    - Copy `kick_model_vtable_t`, `bohm_instance_t` fwd decl, and `typedef enum { MODEL_FM2 = 0, MODEL_COUNT } model_id_t;` VERBATIM from the <interfaces> block. Add a comment `/* model IDs are append-only and permanent — never renumber (KICK-01) */`.
    - Define the full `struct bohm_instance` here (so downstream plans share one definition). Include at minimum: `model_id_t model;` `float main_volume;` `bool logged_buflen;` (for D-10 one-shot flag) and an opaque `char model_state[4096];` placeholder region the FM2 plan will overlay with its own struct (comment: "Phase B overlays per-model state via union in a later plan; Phase A uses a fixed region"). Zero-init assumption documented.
    - Declare param key macros as string literals matching set_param/get_param dispatch (D-08): `#define PK_PITCH "pitch"`, `PK_LENGTH "length"`, `PK_SUSTAIN "sustain"`, `PK_CURVE "curve"`, `PK_ATTACK "attack"`, `PK_TRS_DEC "trs_dec"`, `PK_TRS_TNE "trs_tne"`, `PK_COLOR "color"`, `PK_FM_RATIO "fm_ratio"`, `PK_FM_INDEX "fm_index"`, `PK_OP2_WAVE "op2_wave"`, `PK_FX_TYPE "fx_type"`, `PK_FX_AMT "fx_amt"`, `PK_MODEL "model"`, `PK_MASTER_VOL "master_vol"`, `PK_UI_HIER "ui_hierarchy"`.
    - Declare `extern const kick_model_vtable_t *g_models[MODEL_COUNT];` (defined later in model_registry.c).
    - Add `_Static_assert(sizeof(struct bohm_instance) < 800000, "instance under 800KB");`.

    Create `tools/gen_sine_table.c` (host program, C, uses libm/`sinf`): a `main()` that prints to stdout a header `src/sine_table.h` body containing `static const float _Alignas(16) g_sine_table[2049] = { ... };` where element i = `sinf(2.0f*3.14159265358979323846f*(float)i/2048.0f)` for i=0..2047 and element 2048 = element 0 (guard sample, D-04). Emit `#include <stdalign.h>` at the top of the generated header. (Generating via a build-time tool avoids hand-writing 2049 constants and keeps them reproducible per A-RESEARCH.md line 161-163.)

    Run the generator to produce `src/sine_table.h` and commit the generated file (so CI does not require regeneration).
  </action>
  <verify>
    <automated>cc -std=gnu11 -o /tmp/gen_sine tools/gen_sine_table.c -lm && /tmp/gen_sine > /tmp/sine_check.h && grep -c "g_sine_table\[2049\]" /tmp/sine_check.h</automated>
  </verify>
  <acceptance_criteria>
    - `src/omega.h` contains `typedef struct` for both `host_api_v1_t` and `plugin_api_v2_t` (grep: `host_api_v1_t` and `plugin_api_v2_t`)
    - `src/omega.h` contains `typedef struct` `kick_model_vtable_t` with four function-pointer members `trigger`, `render`, `set_p2`, `p2_slot_desc` (grep all four)
    - `src/omega.h` contains `MODEL_FM2 = 0` and `MODEL_COUNT`
    - `src/omega.h` contains `#define OMEGA_SR 44100.0f` and `#define OMEGA_MAX_BLOCK 256`
    - `src/omega.h` contains all 16 PK_* macros listed above (grep `#define PK_PITCH` … `#define PK_UI_HIER`)
    - `src/omega.h` contains `_Static_assert(sizeof(struct bohm_instance) < 800000`
    - `src/sine_table.h` contains `g_sine_table[2049]` and `_Alignas(16)`
    - Generator compiles and emits exactly one `g_sine_table[2049]` declaration (verify command prints `1`)
  </acceptance_criteria>
  <done>omega.h defines the full host/plugin ABI, vtable contract, instance struct, and param keys; the sine table generator produces a 2049-element guard-terminated .rodata table.</done>
</task>

<task type="auto">
  <name>Task 2: Implement dsp_primitives (env_t, wt_read, tpt1) with a runtime guard-sample check</name>
  <read_first>
    - src/omega.h (just created — for OMEGA_SR and includes)
    - src/sine_table.h (just generated — the const table this file will #include)
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md §Pattern 2/3/4 (lines 158-201), FM2 recipe env_coeff formula lines 261-265
    - .planning/phases/A-foundation-fm2-model/A-CONTEXT.md D-04, D-05, D-07 and <specifics> lines 120-123
  </read_first>
  <action>
    Create `src/dsp_primitives.h` (guard `DSP_PRIMITIVES_H`, `#include "omega.h"`):
    - `typedef struct { float value; float coeff; } env_t;` (D-05 exact struct).
    - `typedef struct { float s; } tpt1_t;` (D-07 single integrator state).
    - `extern const float g_sine_table[2049];` (declared extern here; defined in the .c via the generated header).
    - Declare (as `static inline` in the header OR non-inline prototypes + defs in .c — choose static inline in the header so the native test and dsp.so both get them, per A-RESEARCH):
      - `static inline void env_trigger(env_t *e, float start, float coeff){ e->value=start; e->coeff=coeff; }`
      - `static inline float env_tick(env_t *e){ float v=e->value; e->value*=e->coeff; return v; }`
      - `static inline float env_coeff_from_ms(float time_ms){ return expf(-1.0f/(time_ms*0.001f*OMEGA_SR)); }` (VERBATIM from A-RESEARCH line 263; add `#include <math.h>`).
      - `static inline float wt_read(const float *t, float phase01){ float fp=phase01*2048.0f; int i=(int)fp; float fr=fp-(float)i; return t[i]+fr*(t[i+1]-t[i]); }` (VERBATIM A-RESEARCH lines 165-170; relies on t[2048]==t[0], no branch).
      - `static inline float tpt1_lp(tpt1_t *f, float x, float g){ float v=(x-f->s)*(g/(1.0f+g)); float y=v+f->s; f->s=y+v; return y; }` (VERBATIM A-RESEARCH lines 195-200).

    Create `src/dsp_primitives.c` (`#include "dsp_primitives.h"`, `#include "sine_table.h"`):
    - The `#include "sine_table.h"` brings in `static const float g_sine_table[...]`. Because dsp_primitives.h declares it `extern`, resolve by defining a non-static alias: define `const float g_sine_table[2049]` here by copying is not possible for const-init; instead have the GENERATOR emit the definition without `static` (change Task 1 generator to emit `const float _Alignas(16) g_sine_table[2049] = {...};`, no `static`) so this .c owns the single definition and other TUs use the extern decl. Update sine_table.h accordingly (regenerate).
    - Add one exported (internal-linkage-ok) function `void omega_primitives_selfcheck(void)` that runtime-asserts `g_sine_table[2048] == g_sine_table[0]` via `assert()` — this is what the test harness calls to verify KICK-15's guard sample at runtime. Guard the `assert` include with `#include <assert.h>`.
  </action>
  <verify>
    <automated>cc -std=gnu11 -Isrc -c src/dsp_primitives.c -o /tmp/dsp_prim.o -lm && echo "compiles"</automated>
  </verify>
  <acceptance_criteria>
    - `src/dsp_primitives.h` contains `typedef struct { float value; float coeff; } env_t;`
    - `src/dsp_primitives.h` contains `typedef struct { float s; } tpt1_t;`
    - `src/dsp_primitives.h` contains `env_coeff_from_ms` with body `expf(-1.0f/(time_ms*0.001f*OMEGA_SR))` (grep `env_coeff_from_ms`)
    - `src/dsp_primitives.h` contains `wt_read` returning `t[i]+fr*(t[i+1]-t[i])`
    - `src/dsp_primitives.h` contains `tpt1_lp`
    - `src/sine_table.h` defines `g_sine_table[2049]` WITHOUT the `static` keyword (grep `const float` and confirm no `static const float g_sine_table`)
    - `src/dsp_primitives.c` contains `omega_primitives_selfcheck` and asserts `g_sine_table[2048] == g_sine_table[0]`
    - The .c compiles to an object file (verify command prints `compiles`)
  </acceptance_criteria>
  <done>All shared DSP primitives compile; the sine table has a single non-static const definition owned by dsp_primitives.c; a runtime guard-sample self-check exists for KICK-15.</done>
</task>

<task type="auto">
  <name>Task 3: Build the test harness, malloc trap, WAV writer, glibc gate, Makefile, and CI</name>
  <read_first>
    - src/omega.h, src/dsp_primitives.h (contracts the harness links against)
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md §"Test Harness" (lines 449-498), §"Cross-Compilation" flags + glibc gate (lines 403-447)
    - .planning/phases/A-foundation-fm2-model/A-CONTEXT.md D-11, D-12, D-13, D-14
    - CLAUDE.md §"Build System", §"Cross-Compilation Gotchas"
  </read_first>
  <action>
    Create `tests/mock_host.h` + `tests/mock_host.c` — `host_api_v1_t make_mock_host(void)` returning `{api_version=1, sample_rate=44100, frames_per_block=128, log=mock_log (fprintf stderr), midi_send_*=stub returning 0, get_clock_status=stub 0, get_beat_position=stub 0.0}` (VERBATIM structure from A-RESEARCH lines 452-464).

    Create `tests/malloc_trap.c` — guarded by `#ifdef OMEGA_MALLOC_TRAP`: define `bool g_audio_thread_active = false;` and interpose `malloc/free/calloc/realloc` using `__libc_*` (Linux). Each aborts if `g_audio_thread_active` is true (A-RESEARCH lines 470-476). Add a top-of-file comment documenting the macOS caveat (A-RESEARCH line 478): "On macOS host, interposition via __libc_* is unavailable; the trap is authoritative only in the Linux CI native build. On macOS `make test` runs without the trap (compile flag omitted) — CI is the gate for FNDTN-03." Provide the flag as a Makefile-controlled define so macOS builds skip it.

    Create `tests/wav.h` + `tests/wav.c` — `FILE* wav_open(const char*, int sr, int ch)`, `void wav_write(FILE*, const int16_t*, int nsamples)`, `void wav_close(FILE*)`. `wav_open` writes a 44-byte canonical RIFF/WAVE/fmt/data PCM16 header with placeholder sizes; `wav_close` seeks back and patches the RIFF and data chunk sizes.

    Create `tests/test_render.c` — TEMPORARY stub-driven harness for Wave 0 (FM2 arrives in Plan A-02, which will extend this file):
    - `#include "omega.h"`, mock_host, wav.
    - Call `omega_primitives_selfcheck()` (KICK-15 runtime guard check).
    - Since no plugin entry exists yet, define a local silent-render path: allocate a `struct bohm_instance` on the stack, set `g_audio_thread_active=true`, loop 512 blocks of 128 frames producing silence through the SAME `to_i16` clamp path (copy the `to_i16` from A-RESEARCH lines 269-275 into a shared `src/dsp_primitives.h` inline `omega_to_i16` so A-02 reuses it — add it now: `static inline int16_t omega_to_i16(float x){ if(!isfinite(x)) x=0.0f; if(x>1.0f)x=1.0f; if(x<-1.0f)x=-1.0f; return (int16_t)lrintf(x*32767.0f);}`), assert each int16 in range, write to `tests/output/fm2_kick.wav`, set `g_audio_thread_active=false`.
    - Assert `g_models`… is NOT referenced yet (registry is A-02). Add a clearly-marked `/* TODO(A-02): replace stub render with move_plugin_init_v2 lifecycle + FM2 trigger */` block.
    - Assert the WAV file exists and is > 44 bytes at the end.

    Create `scripts/glibc_gate.sh` — VERBATIM logic from A-RESEARCH lines 424-432: fail if any `GLIBC_x.y > 2.35`, fail if any `_ZGV`/libmvec symbol, and assert exactly one `move_plugin_init_v2` default-visibility export (`objdump -T "$1" | grep ' g ' | grep -c move_plugin_init_v2` equals 1). `chmod +x`. Accept the .so path as `$1`.

    Create `scripts/deploy.sh` — scp `build/dsp.so` to device as `dsp.so.new` then `ssh <host> mv dsp.so.new dsp.so` atomic rename (D-14). Parameterize device host/path via env vars with sensible placeholder defaults and a comment.

    Create `Makefile` with separate compilers (D-11, discretion on structure):
    - Vars: `CC ?= cc` (native), `XCC = aarch64-linux-gnu-gcc`, `AARCH_FLAGS = -std=gnu11 -O3 -shared -fPIC -Isrc -fno-math-errno -ffp-contract=fast -fvisibility=hidden -Wl,--no-undefined -lm` (VERBATIM A-RESEARCH lines 412-418; NO -mcpu per D-15), `TEST_FLAGS = -std=gnu11 -O2 -Isrc -Itests -DOMEGA_MALLOC_TRAP -lm` on Linux only (detect `uname`: on Darwin drop `-DOMEGA_MALLOC_TRAP`).
    - `dsp.so` target: compiles src/*.c + src/models/*.c into `build/dsp.so` using `$(XCC) $(AARCH_FLAGS)`. In Phase A the model sources may not all exist yet — use a wildcard over `src/*.c src/models/*.c` and tolerate that models/ is populated by A-02 (this target is expected to fail until A-02 adds dsp.c/registry/fm2.c; that is acceptable for this plan — `make test` is the Wave 0 gate).
    - `test` target: `mkdir -p tests/output && $(CC) $(TEST_FLAGS) tests/test_render.c tests/mock_host.c tests/wav.c tests/malloc_trap.c src/dsp_primitives.c -o build/test_render && ./build/test_render`.
    - `clean`: `rm -rf build tests/output`.
    - `deploy`: `./scripts/deploy.sh`.

    Create `.github/workflows/ci.yml` (D-11): on push/PR — job 1 native `make test`; job 2 docker `docker run --rm -v "$PWD:/workspace" -w /workspace ghcr.io/charlesvestal/schwung-builder:latest make dsp.so` then `./scripts/glibc_gate.sh build/dsp.so`. Note in a comment that job 2 (dsp.so) may be allowed to fail until Plan A-02 lands the entry points — mark it `continue-on-error: true` with a TODO to flip to false after A-02.

    Create `.gitignore` — `build/`, `tests/output/`, `*.o`, `*.so`.

    Create `tests/output/.gitkeep` so the dir exists.
  </action>
  <verify>
    <automated>cd "$PWD" && make test && test -s tests/output/fm2_kick.wav && bash -n scripts/glibc_gate.sh && echo "GATE_OK"</automated>
  </verify>
  <acceptance_criteria>
    - `make test` exits 0 (verify command reaches `GATE_OK`)
    - `tests/output/fm2_kick.wav` exists and is non-empty (`test -s` passes)
    - `tests/malloc_trap.c` contains `g_audio_thread_active` and `abort()`
    - `tests/wav.c` contains `wav_open`, `wav_write`, `wav_close`
    - `src/dsp_primitives.h` now contains `omega_to_i16` with `isfinite` and `lrintf` (grep both)
    - `scripts/glibc_gate.sh` contains `GLIBC_`, `_ZGV`, and `move_plugin_init_v2`; passes `bash -n` syntax check
    - `Makefile` contains targets `dsp.so`, `test:`, `clean`, `deploy`, uses `aarch64-linux-gnu-gcc`, contains `-fno-math-errno -ffp-contract=fast`, `-fvisibility=hidden`, `-Wl,--no-undefined`, and does NOT contain `-mcpu`
    - `.github/workflows/ci.yml` contains `ghcr.io/charlesvestal/schwung-builder` and `glibc_gate.sh`
    - `test_render.c` contains a `TODO(A-02)` marker and calls `omega_primitives_selfcheck()`
  </acceptance_criteria>
  <done>The full offline verification apparatus is in place: `make test` compiles and runs green against a silent stub, produces a valid WAV, the malloc trap is wired (Linux), the glibc gate script and CI workflow exist, and the int16 clamp path (FNDTN-07) is shared for A-02 to reuse.</done>
</task>

</tasks>

<verification>
- `make test` exits 0; `tests/output/fm2_kick.wav` is a valid non-empty PCM16 WAV.
- `src/omega.h` locks the vtable contract, instance struct, ABI structs, and param keys downstream plans depend on.
- `src/sine_table.h` provides the single 2049-element guard-terminated `.rodata` sine table (KICK-15); runtime self-check passes.
- `scripts/glibc_gate.sh` is syntactically valid and encodes the GLIBC<=2.35 / libmvec / single-export checks (FNDTN-04).
- Malloc trap (FNDTN-03) and int16 clamp (FNDTN-07) mechanisms exist and are exercised by the stub harness.
</verification>

<success_criteria>
- Downstream plans (A-02, A-03) can `#include "omega.h"` / `"dsp_primitives.h"` and build against fixed contracts with zero codebase exploration.
- `make test` is a working per-task gate (< 5s) for all subsequent Phase A tasks.
- No `-mcpu` baked in (D-15 deferred to on-device confirmation in A-04).
</success_criteria>

<output>
After completion, create `.planning/phases/A-foundation-fm2-model/A-01-SUMMARY.md`
</output>
