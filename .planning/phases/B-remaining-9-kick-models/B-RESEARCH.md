# Phase B: Remaining 9 Kick Models - Research

**Researched:** 2026-09-29
**Domain:** Techno kick synthesis DSP in RT-safe C11 (aarch64) — 9 distinct kick engines behind the Phase A vtable + a 5-mode post-kick FX chain + per-model voicing
**Confidence:** HIGH on DSP technique, ABI, RT-safety, FX formulas (verified against src/ + Context/ + CLAUDE.md). MEDIUM on exact default voicing numbers (taste-dependent; defensible starting points given, tuned by ear on-device per D-B02).

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**D-B01: Per-model voicing round is a first-class deliverable (not optional polish).**
Every model — including FM2, which was built in Phase A but is NOT yet voiced (user reports it "sounds like FM but the parameter ranges and CURVE don't sound good yet") — must pass a voicing pass before the phase is done. Phase A proved the models *function*; Phase B must make them *sound musical*. Voicing is in scope for all 10 models: FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN.

**D-B02: The voicing checklist (per model).** Each model must satisfy this checklist. Split into automated (offline WAV render + asserts) and manual (on-device listening — the ear is the authority, mirroring the A-04 hardware checkpoint):

*Automated (offline, in `make test` / WAV harness):*
- Renders non-silent output for a default trigger; output finite and clamped ≤ 1.0 pre-int16.
- Each of the model's Kick Page 2 params measurably changes the output (lo vs hi render differs).
- No aliasing blow-up or divergence at extreme param settings (output stays bounded across the full param sweep).
- Model switch into/out of this model re-inits cleanly (no stale-state / NaN carried across a switch).

*Manual (on-device, per-model voicing sign-off):*
- Default/12-o'clock preset sounds like a usable techno kick out of the box (no obvious tuning required to be listenable).
- The PITCH sweep / CURVE (808↔909 character) sounds musical across its range — not clicky, not muddy, decay feels right.
- Each knob sweeps a musically useful range end-to-end (no dead zones, no all-the-action-in-the-last-5%). Re-map param min/max and response curves as needed.
- The model has a distinct sonic character vs the others (FM2≠FM4≠ANA≠HRD… each earns its slot).
- No clipping, zipper noise, or artifacts when turning knobs live.

**D-B03: FM2 re-voicing specifically.** Treat FM2 as the first voicing target and the reference bar. Revisit its parameter ranges (PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR, FM RATIO, FM INDEX, OP2 WAVE) and the CURVE 808↔909 envelope-blend shape. The FM2 DSP code lives in `src/models/fm2.c` (Phase A); voicing changes there are in-scope for Phase B.

**D-B04: Voicing is delivered as (a) per-model tasks + (b) a final voicing audit.** Each model's plan carries the automated voicing criteria in its `must_haves`. A dedicated voicing/audit step at the end of the phase produces a per-model checklist doc (like `docs/ON_DEVICE_VALIDATION.md`) listing the 10 models with PASS/PENDING for each checklist item, so the on-device manual sign-off is tracked, not skipped. Phase B is not "complete" until that doc is filled on-device (manual verification), same pattern as A-04.

### Claude's Discretion
- Model DSP recipes (modal PM for PHY, band-limited wavetables for WTR, FM4 operator algorithms, ANA analog-style, DIG digital/bitcrush character, TRS 808/909-ish transistor, HRD hard/distorted, GEN generative) — research picks the concrete approach per model; CLAUDE.md tech-stack guidance is the starting point.
- Default parameter values and exact min/max ranges per model — choose musically, refine in the voicing round.
- Order in which models are built/voiced within the phase.
- FX chain implementation details (the 5 modes) beyond "audibly alter the kick, no divergence/clipping."

### Deferred Ideas (OUT OF SCOPE)
- buf_len measurement (was the removed D-10 spike) — capture off-thread in Phase E when the full hierarchy is sized.
- Groove (Phase C), Performer (Phase D), full nav tree (Phase E), macros (F), presets (G).
</user_constraints>

<phase_requirements>
## Phase Requirements

Bohm-spec model names map cleanly to the project's synthesis-method IDs (STATE.md "Synthesis-method model IDs" decision; Context/02 §2). This mapping is authoritative for the phase:

| Req ID | Model (project ID / Bohm name) | Description | Research Support |
|--------|-------------------------------|-------------|------------------|
| KICK-03 | FM4 / `OLP-4` | 4-op FM, 4 selectable OPL3-style algorithms; per-op AM envelopes; P2: ALGORITHM, OP RATIO, OP INDEX, OP AMP, FEEDBACK, ALGO | §FM4 recipe; Context/02 §2.3; Sophie 4-op reference (Context/05 #14) |
| KICK-04 | WTR / `HZ-1` | wavetable body osc + dedicated transient impulse synth, independently enveloped; P2: WAVE SELECT, BODY PITCH, TRANS DECAY, TRANS COLOR | §WTR recipe; §Shared primitive `wt_read` + band-limited tables; Context/02 §2.2 |
| KICK-05 | PHY / `PM-K1` | 2–3 damped resonant modes (modal synth), shell/head/beater; P2: BEATER, SHELL SIZE, HEAD TENS, DAMPING | §PHY recipe; §New primitive `modal_t`; CLAUDE.md Physical Modeling; Context/02 §2.4 |
| KICK-06 | HRD / `PX-3` | hard-techno wavetable body + sample layer + post-distortion; drive + bit-crush; P2: SAMPLE LAYER, MIX, DRIVE, CRUSH | §HRD recipe; §FX chain (SAT/Crush reused); Context/02 §2.5 |
| KICK-07 | DIG / `SP-6` | digital wavetable (chip/additive/bit-reduced) + sample playback; bit-depth control; P2: WAVE IDX, SAMPLE LAYER, BIT DEPTH, PITCH ENV | §DIG recipe; §New primitive `crush()`; Context/02 §2.6 |
| KICK-08 | TRS / `VX-T` | advanced wavetable body + transient synth modeling stick/beater clicks + noise bursts; 909 attack clarity; P2: TRANS TONE, TRANS DECAY, WT COLOR, CURVE | §TRS recipe; §New primitive `noise_t`; Context/02 §2.7 |
| KICK-09 | ANA / `WT-4` | analog wavetable morph (sampled vintage waves) + sub-osc + sample layer; 808 sub-boom; P2: WAVE MORPH, SUB LEVEL, SUB DECAY, SAMPLE | §ANA recipe; §Band-limited analog tables; Context/02 §2.8 |
| KICK-10 | USR / `XT-88` | user-loaded WAV + 2048 wavetable from `module_dir/user/`; file I/O off audio thread at create_instance; P2: SAMPLE SELECT, WT MORPH, LAYER VOL, PITCH ENV | §USR recipe; §USR file-load pattern; Context/02 §2.9 |
| KICK-11 | GEN / `HPN` | generative rumble: PRNG pitch/velocity sequence, seed, scale-quantize or free-freq, Euclidean density; P2 params (Groove Page 2 in Phase C — see §GEN scope) | §GEN recipe; §New primitives `prng_t` + scale tables; Context/02 §2.10 |
| KICK-13 | (all) | Context-sensitive Kick Page 2: FX TYPE + FX AMT + 6 model-specific slots assembled from active model's `p2_slot_desc`; clean model-switch re-init | §Page 2 assembly; §Model-switch re-init; ui.c splice pattern |
| KICK-14 | (all) | Post-kick FX modes: Diode, Clip (asym soft clip), SAT (warm parallel sat), Fold (wavefolder), Crush (bit/SR reduction), bounded output | §FX chain formulas |
</phase_requirements>

## Summary

Phase B is breadth over an already-proven skeleton. Phase A locked the ABI (`omega.h`, do not touch — `_Static_assert`s enforce byte layout), the vtable dispatch contract (`kick_model_vtable_t` = trigger/render/set_p2/p2_slot_desc), the shared `.rodata` sine table, three shared primitives (`env_t`, `wt_read`, `tpt1_t`), the clamped int16 boundary (`omega_to_i16`), and the offline WAV/malloc-trap harness. Nine new models plug into that scaffold by (1) appending a `model_id_t` enum value + a vtable entry in `model_registry.c` (append-only; `MODEL_FM2=0` is permanent), (2) overlaying `bohm_instance.model_state[4096]` with a per-model state struct (each must `_Static_assert(sizeof(x) <= 4096)`), and (3) emitting a `p2_slot_desc` JSON fragment. Every model reuses the Phase A primitives; the phase adds five new shared primitives to `dsp_primitives.h` (band-limited wavetable read, modal resonator, xorshift PRNG, scale-quantize table, and the FX-chain functions) so no model hand-rolls them.

**The hard problem is voicing, not DSP correctness (D-B01/D-B02).** Every model — including the already-built FM2 (D-B03) — must sound like a usable techno kick at its default 12-o'clock settings and must sweep a musically useful range on every knob. This research gives the planner a concrete DSP recipe, defensible default parameter values, sensible min/max ranges, and a distinctness statement for each of the 10 models, grounded in Context/03 (pitch-sweep formula `f(t)=f_start·e^{-t/τ}+f_fund`, τ≈15 ms for 909; slow decay for 808), Context/02 (Bohm per-model semantics), and known 808/909/FM/analog kick norms (fundamental 40–60 Hz, sweep from 300–800 Hz, click 1–10 ms, body decay 100–1500 ms). Where a number is genuinely taste, this is stated and a default is given for the planner to encode; the on-device ear round (D-B02 manual / D-B04 audit doc) is the final authority.

**Primary recommendation:** Build in this order to front-load risk and de-risk voicing: (1) **FX chain first** (KICK-14) as shared primitives — it is used by HRD/DIG and is a self-contained, testable unit; (2) **re-voice FM2** (D-B03) to establish the voicing bar and the reusable voicing-test asserts; (3) **model-switch re-init** (KICK-13) via a mandatory `reset()` in each trigger + a clean-switch harness test; (4) the synthesis models grouped by shared primitive — the wavetable/transient family (WTR, TRS, ANA, DIG, HRD) which reuse `wt_read` + band-limited tables + noise + FX, then **FM4** (extends FM2's FM core), then **PHY** (new modal primitive), then **USR** (file-load, off-thread), then **GEN** (PRNG + scale tables; Phase B scope is the audible generative kick, full transport-synced groove is Phase C); (5) the **voicing audit doc** (D-B04) last.

## Standard Stack

No new libraries. Everything is C11 + libm, per CLAUDE.md and Phase A. New code is all first-party DSP.

### Core (unchanged from Phase A — verified in src/)
| Component | Where | Purpose | Why Standard |
|-----------|-------|---------|--------------|
| C11 (`-std=gnu11`) | Makefile | Implementation | Schwung C ABI; `_Static_assert` guards each model state size (CLAUDE.md, HIGH) |
| `env_t` (one-pole decay) | dsp_primitives.h | pitch/index/amp/transient envelopes | Shared, RT-safe, denormal-flushed by FPCR (D-05) |
| `wt_read` (linear interp, guard sample) | dsp_primitives.h | all wavetable oscillators | Branch-free wrap; every osc reads `.rodata` tables (D-04/KICK-15) |
| `tpt1_lp` (TPT 1-pole LP) | dsp_primitives.h | COLOR + per-model tone filters | Zavalishin TPT; stable under modulation, no biquad zipper (D-07) |
| `omega_to_i16` | dsp_primitives.h | the ONE int16 boundary | clamp+isfinite+lrintf (FNDTN-07) |
| `g_sine_table[2049]` | dsp_primitives.c (.rodata) | shared sine for all FM/osc | one mapping, shared read-only (KICK-15) |

### New shared primitives to ADD in `dsp_primitives.h/.c` (this phase)
| Primitive | Signature (proposed) | Used by | Notes |
|-----------|----------------------|---------|-------|
| Band-limited multi-wave tables | `static const float _Alignas(16) g_wavetables[NUM_WAVES][BANDS][2049]` + `wt_read_bl(wave, band, phase01)` | WTR, DIG, ANA, HRD, USR | Build-time generated header (like sine_table.h). Pick band by pitch to prevent aliasing. Start with 1 band (kicks rarely alias at 40–200 Hz); add band-limited variants only if a hi-pitch sweep aliases in the harness (CLAUDE.md). |
| Modal resonator | `typedef struct { float re, im, cos_w, sin_w, decay; } modal_t;` + `modal_excite(m, freq, decay_ms)` + `modal_tick(m)` | PHY | Complex-rotation form (CLAUDE.md "modern, stable choice"): per-sample `z *= e^{jω}·e^{-decay}`; one complex multiply. 2–3 modes per voice. Precompute `cos_w/sin_w/decay` in set_param/trigger, never per-sample. |
| PRNG (xorshift64) | `typedef struct { uint64_t s; } prng_t;` + `prng_seed(p, seed)` + `prng_next_f(p)` → [0,1) | GEN | xorshift64 (CLAUDE.md: NOT `rand()`). Deterministic from SEED → repeatable sequences. |
| Scale quantize | `static const int8_t g_scales[NUM_SCALES][12]` + `scale_quantize(scale, degree)` → semitone offset | GEN | `.rodata` table lookup; free-freq mode bypasses (CLAUDE.md). |
| Noise burst | `typedef struct { prng_t rng; } noise_t;` + `noise_tick(n)` → [-1,1] | TRS, WTR, PHY excite, HRD | Reuse `prng_t`; white noise = `2*prng_next_f-1`. Filter through `tpt1` for pink-ish / colored clicks. |
| FX chain (KICK-14) | `float fx_process(int mode, float x, float amt)` + small state for Crush | ALL models (post-kick), HRD/DIG drive | 5 modes; see §FX Chain. Pure functions except Crush (holds sample-hold + phase). All bounded ≤ 1.0. |

**Installation / build:** no package installs. New generated tables emit a header at build time (add a generator target to the Makefile, same pattern as `sine_table.h`). Wave generation runs on the host (native `cc`), not on device.

**Version verification:** N/A — no third-party packages added this phase. Toolchain (aarch64-linux-gnu-gcc in `ghcr.io/charlesvestal/schwung-builder:latest`, glibc 2.35) is unchanged and already gated by CI (FNDTN-04). The glibc/libmvec/single-export `objdump -T` gate from Phase A remains the authoritative build gate; new `expf`/`sinf`/`tanhf`/`floorf` uses must not introduce libmvec `_ZGV*` symbols (keep the granular fast-math subset, not blanket `-ffast-math`).

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Complex-rotation modal resonator (PHY) | 2-pole biquad resonator (RBJ) | Biquad valid but coeff recompute + can go unstable under fast pitch sweep; complex rotation is exact + stable + vectorizes across modes (CLAUDE.md). Use biquad only for a static body mode if simpler. |
| Union overlay of per-model state | separate malloc per model | Forbidden: single calloc only (FNDTN-03). Overlay `model_state[4096]`; each struct `_Static_assert`ed ≤ 4096. |
| xorshift64 PRNG (GEN) | PCG / LCG | All fine; xorshift64 is fewest instructions and adequate for musical randomness (CLAUDE.md). NOT `rand()` (nondeterministic across libc). |
| Band-limited wavetables now | mip-band on demand | Kicks live at 40–200 Hz where aliasing is negligible; ship 1 band, add bands only if the harness detects aliasing on a hi-pitch sweep. |

## Architecture Patterns

### Recommended file layout (extends D-01/D-02 — flat headers, models under src/models/)
```
src/
├── omega.h                 # LOCKED ABI. Append MODEL_* enum values here (append-only). Add PK_* keys for new P2 params.
├── dsp_primitives.h/.c     # ADD: band-limited wt_read_bl, modal_t, prng_t, scale tables, noise_t, fx_process
├── wavetables.h            # NEW generated: g_wavetables[NUM_WAVES][BANDS][2049] (build-time generator)
├── ui.c                    # EXTEND: enum "options" list + kick2 splice from active model's p2_slot_desc (see §Page 2)
└── models/
    ├── model_registry.c    # APPEND vtable pointers (append-only; FM2 stays index 0)
    ├── fm2.c               # RE-VOICE (D-B03): ranges + CURVE blend + FX chain call
    ├── fm4.c   (KICK-03)   # NEW
    ├── wtr.c   (KICK-04)   # NEW
    ├── phy.c   (KICK-05)   # NEW
    ├── hrd.c   (KICK-06)   # NEW
    ├── dig.c   (KICK-07)   # NEW
    ├── trs.c   (KICK-08)   # NEW
    ├── ana.c   (KICK-09)   # NEW
    ├── usr.c   (KICK-10)   # NEW
    └── gen.c   (KICK-11)   # NEW
docs/ON_DEVICE_VALIDATION.md  # EXTEND / new VOICING_AUDIT.md (D-B04): 10 models × checklist PASS/PENDING
```

### Pattern 1: New model = enum + state overlay + vtable entry (KICK-01, KICK-13)
**What:** Each model follows the exact `fm2.c` shape. Copy it as the template.
```c
// omega.h — APPEND ONLY. Never renumber. FM2=0 is permanent.
typedef enum {
    MODEL_FM2 = 0, MODEL_FM4, MODEL_WTR, MODEL_PHY, MODEL_HRD,
    MODEL_DIG, MODEL_TRS, MODEL_ANA, MODEL_USR, MODEL_GEN,
    MODEL_COUNT
} model_id_t;   // MODEL_COUNT becomes 10

// each model .c:
typedef struct fm4_state { /* ... */ } fm4_state;
_Static_assert(sizeof(fm4_state) <= 4096, "fm4_state fits model_state");  // MANDATORY
// fm4_state *st = (fm4_state *)inst->model_state;  // overlay

// model_registry.c — APPEND (order MUST match enum indices):
const kick_model_vtable_t *g_models[MODEL_COUNT] = {
    &g_fm2_vtable, &g_fm4_vtable, &g_wtr_vtable, &g_phy_vtable, &g_hrd_vtable,
    &g_dig_vtable, &g_trs_vtable, &g_ana_vtable, &g_usr_vtable, &g_gen_vtable,
};
```
**Warning:** `model_state[4096]` is a SINGLE shared region. When the user switches models, the new model's state struct reinterprets the SAME bytes the previous model left. This is the source of KICK-13's stale-state hazard — see Pattern 2.

### Pattern 2: Clean model-switch re-init (KICK-13, SC2, D-B02 automated criterion)
**What:** Switching MODEL must fully re-initialise the incoming model's state — no NaN, no stale phase/filter/envelope carried across the reinterpreted `model_state` bytes.
**Why it's a real hazard:** `omega_set_param(PK_MODEL)` in dsp.c currently just sets `inst->model = m`. The next `render` calls the new model over bytes the old model wrote. If the new model reads state it assumes was zeroed (filter integrators, phase, envelope `value`), it can emit garbage / NaN / a click.
**How to fix (recommend a contract, planner picks placement):**
- Add a `reset(inst)` responsibility to every model. Simplest: on `PK_MODEL` change in dsp.c, `memset(inst->model_state, 0, sizeof inst->model_state)` then re-apply the primed default kick params to the new model (mirroring `omega_create`'s prime loop), so the incoming model starts from a known-good, fully-parameterised, zeroed state.
- Every model's `trigger` must also fully (re)initialise its own oscillator phases + filter states (FM2 already does: `car_phase=0; color_lp.s=0; ...`). Triggering after a switch then gives a deterministic attack.
- Guard the switch at a block boundary: the swap happens in `set_param` which runs on the same audio thread as `render` and is serialized with it (A-RESEARCH open Q3: all entry points on one SPI thread), so a mid-`render` tear is not possible — but re-init must complete before the next `render`. The memset-on-switch satisfies this.
```c
// dsp.c omega_set_param, PK_MODEL branch (proposed):
if (strcmp(key, PK_MODEL) == 0) {
    int m = clamp_model((int)dsp_parse_f(val));
    if ((model_id_t)m != inst->model) {
        inst->model = (model_id_t)m;
        memset(inst->model_state, 0, sizeof inst->model_state);  // clear stale bytes
        for (i in k_kick_keys) g_models[m]->set_p2? / route defaults;  // re-prime
    }
}
```
**Note on the prime loop:** `omega_create` currently calls `fm2_set_param` directly for all keys. Phase B must route param defaults through the *active model's* `set_p2` (and Page-1 keys through a model-agnostic path), because different models interpret different P2 keys. Cleanest: give each model a `set_param(inst,key,val)` that handles both Page-1 and its own Page-2 keys (FM2 already does this — `fm2_set_param`), and dispatch to `g_models[inst->model]` instead of hardcoding `fm2_set_param` in dsp.c. **This is a required dsp.c refactor for KICK-13** — flag it for the planner. The current `omega_set_param` hardcodes `fm2_set_param` (dsp.c line 118); that must become a vtable call so other models receive their params.

### Pattern 3: Page 2 slot descriptor + dynamic assembly (KICK-13, SC3)
**What:** Each model owns a `p2_slot_desc` returning a JSON fragment of its 4–6 model-specific slots; ui.c wraps it + FX TYPE/AMT into the `kick2` level.
**Current state:** Phase A's `ui.c` INLINED FM2's page-2 params (it stopped calling `fm2_p2_slot_desc`, see ui.c lines 63–72 comment). For Phase B with 10 models, ui.c MUST switch to splicing the active model's `p2_slot_desc` (the A-03 mechanism described in STATE.md `[A-03] Kick Page 2 spliced from the model`) so each model shows its own slots. The `fm2_p2_slot_desc` format (array of `{key,label}`) is the template; note the *newer* ui.c schema uses `{key,name,type,min,max}` — reconcile the two: either have `p2_slot_desc` emit the full `{key,name,type,min,max}` objects, or have ui.c enrich the `{key,label}` fragment. **Recommend:** each model emits the full `{"key":..,"name":..,"type":"float","min":0.0,"max":1.0}` objects so ui.c stays generic. Also update the root-level Model enum `"options":["FM2",...]` to list all 10 names.
```c
// each model:
static int xxx_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    static const char json[] =
        "{\"key\":\"" PK_ALGO "\",\"name\":\"ALGO\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        /* ...4-6 slots... */ ;
    int len = (int)(sizeof(json)-1);
    if (buf_len <= len) return 0;         // bounded, no overflow (Pitfall)
    memcpy(buf, json, (size_t)len); buf[len] = '\0'; return len;
}
```
Keep each fragment well under the measured buf_len (Phase A native harness saw 4096; on-device cap still PENDING — size conservatively, all 10 fragments are small).

### Pattern 4: The universal kick skeleton (all models share this contour)
Every techno kick, regardless of engine, is: **pitch-swept body oscillator + amplitude envelope + short attack transient/click + optional COLOR lowpass + FX**. The 8 Kick Page 1 params (KICK-12, already wired) mean the same thing for every model:
- PITCH → fundamental `f0` (map ~30–200 Hz)
- LENGTH → amp decay time (ms)
- SUSTAIN → tail contour scalar
- CURVE → 808↔909 pitch-sweep blend (D-06)
- ATTACK → click amplitude / attack shape
- TRS DEC → transient decay (ms)
- TRS TNE → transient brightness (LP cutoff)
- COLOR → model-dependent timbre morph + output LP (Context/02 §3: "varies depending on active model: FM index, wavetable position, or harmonic drive")

Each model differs only in **how the body is generated** and **what COLOR + the 4–6 Page-2 slots morph**. This is why FM2's structure is the template and why the voicing bar (D-B03) transfers across models. **Reuse the FM2 dual-envelope CURVE blend verbatim** in every model unless the model has a reason to differ (PHY sweeps a modal frequency; GEN sequences pitch).

### Anti-Patterns to Avoid (carry forward Phase A's five reference bugs + new ones)
- **Per-sample transcendentals in render:** no `sinf/expf/tanf/tanhf/powf` in any render loop. Precompute all coeffs (`env_coeff_from_ms`, `tpt_g_from_hz`, modal `cos_w/sin_w/decay`, FM4 algorithm routing) in set_param/trigger. `tanhf` for SAT should be a table or the rational `x/(1+|x|)` approximation, not per-sample `tanhf`.
- **Unbounded FX output:** the reference `fast_tanh` (Context/04 line 91) is asymmetric AND unbounded on the negative side (`x/(1-x)` diverges as x→1). Do NOT copy it (STATE.md watchpoint). Use bounded forms only (§FX Chain).
- **Stale `model_state` across a switch:** see Pattern 2. memset on switch.
- **`atof`/`atoi`:** use the existing locale-independent `parse_f` (copy from fm2.c) in every model.
- **NaN from divide/log in modal or filter:** clamp cutoff/decay/freq to safe ranges in set_param (PHY decay > 0, freq in [20, 0.45*SR]).
- **All-the-action-in-last-5% param curves (D-B02):** map perceptual params (pitch, decay time, cutoff) exponentially/logarithmically, not linearly, so the knob sweep is musically even.

## Per-Model DSP Recipes + Voicing Starting Points

> Ranges below are defensible v1 defaults for the planner to encode (D-B02 says re-map as needed on-device; the ear is final). "Default" = value at knob 0.5 (12 o'clock). All params arrive normalized [0,1] via `set_param`; the model maps to real units. Frequencies Hz, times ms.

### FM2 re-voicing (D-B03, KICK-02 — already built, fix ranges + CURVE)
**Current issues (from user + code read):** sweep depth `sweep_hz = f0*4` is fixed and can be too much at high f0; CURVE lerps two *fixed* coeffs (15 ms / 300 ms) but the pitch envelope amount is not scaled by a musical sweep target; FM index env is a fixed 40 ms; body/click split fixed 0.6/0.4.
**Voicing fixes (recommend):**
- PITCH: keep 30–200 Hz but bias default lower — techno kicks sit ~45–55 Hz. Map `f0 = 35 + v*(120-35)` (exp map better); default ≈ 50 Hz.
- Sweep: make depth a musical `sweep_hz = clamp(f0 * (2..6), up to ~500 Hz start)`; Context/03 says 909 sweeps ~400–500 Hz → f_fund. Consider decoupling sweep start (≈300–500 Hz) from f0 rather than a pure multiple.
- CURVE (D-06): the dual-env blend is right; tune the two time constants — 909 fast ≈ 10–20 ms (Context/03 τ≈15 ms), 808 slow ≈ 150–400 ms. Blend the *outputs* (already does), not the coeff, for a smoother morph. Default CURVE 0.5 = balanced.
- LENGTH: 50–1500 ms is fine; default ≈ 300–400 ms. Use an exp map so mid-knob is musically centered.
- FM INDEX: 0–12 is wide; the tail can get buzzy. Default index ≈ 2–4 with its own fast decay (30–60 ms) so attack is bright, tail clean (KICK-02). Consider index range 0–8.
- OP2 WAVE: the fold blend is a good distinctness lever; keep.
- Body/click split 0.6/0.4: expose less; ATTACK should scale click amplitude so default is punchy but not clicky.
**Distinctness:** punchy, metallic, modern FM. The FM ratio + index sweep gives harmonic morphing no other model has.

### FM4 / OLP-4 (KICK-03) — 4-operator FM, OPL3-inspired
**DSP recipe:** Extend the FM2 core to 4 operators. Encode the 4 selectable algorithms as `static const` routing tables (which op modulates which; which ops sum to output) so the render loop walks a table, no per-sample branching (CLAUDE.md). All 4 ops read `g_sine_table`. Each operator has its own AM (amplitude) envelope + FM index envelope (reuse `env_t`). Feedback: operator 4 (or the first) feeds its own last output back into its phase (classic FM feedback), scaled by FEEDBACK param — clamp to avoid runaway.
```c
// 4 algorithms (OPL3-style): e.g.
// ALGO0: 4→3→2→1 (chain, one carrier)   — deep, evolving
// ALGO1: (4→3)+(2→1) (two stacks summed) — richer, two-voice
// ALGO2: 4→(3,2,1) (one mod, 3 carriers) — bright, additive-ish
// ALGO3: (4→1)+2+3 (one FM pair + 2 additive carriers) — hollow/woody
static const uint8_t g_fm4_algo[4][NUM_OPS] = { ... };  // routing per algorithm
```
**Voicing defaults:** default ALGORITHM = chain (0); OP RATIOS integer near {1, 1, 2, 3} for harmonic thump, allow non-integer for metallic; OP INDEX moderate with per-op decay; FEEDBACK ≈ 0.1–0.3 default (small). Body f0 same as FM2. **Distinctness (Context/02 §2.3):** aggressive, woody, hollow, complex enharmonic overtones — vs FM2's simpler 2-op punch. Reference: Sophie 4-op algorithms (Context/05 #14 — Fuse/Stack/Split/Shard).
**Page 2 (KICK-03):** ALGORITHM, OP RATIO, OP INDEX, OP AMP, FEEDBACK, ALGO (6 slots — matches REQUIREMENTS.md; note "ALGORITHM" and "ALGO" both listed — treat as algorithm-select + a second algo-morph/detune; planner to disambiguate, one can be per-op ratio spread).

### WTR / HZ-1 (KICK-04) — wavetable body + dedicated transient synth
**DSP recipe:** Body = `wt_read_bl` on a selectable wavetable (WAVE SELECT picks among a few band-limited factory waves: sine, tri-ish, a couple of harmonically richer tables). Body pitch-swept exactly like FM2 (reuse CURVE blend). A **dedicated transient impulse synth** runs in parallel and is *independently enveloped* (Context/02 §2.2 "transient snap sculpted independently from low-end sub boom"): a short filtered noise/click with its own TRANS DECAY + TRANS COLOR (LP via `tpt1`). Sum body + transient.
**Voicing defaults:** clean, tight 4-on-the-floor. Default WAVE = sine-ish body; BODY PITCH ≈ 50 Hz; TRANS DECAY ≈ 3–8 ms; TRANS COLOR ≈ bright (2–6 kHz LP). **Distinctness:** the cleanest, most "precise" kick — transient fully separable from body (its selling point vs FM engines).
**Page 2 (KICK-04):** WAVE SELECT, BODY PITCH, TRANS DECAY, TRANS COLOR (4 slots; pad to 6 with FX TYPE/AMT).

### PHY / PM-K1 (KICK-05) — modal physical model
**DSP recipe (CLAUDE.md Physical Modeling + new `modal_t` primitive):** 2–3 damped resonant modes = exponentially-decaying sinusoids via complex-rotation state (`z *= e^{jω}·e^{-decay}`, one complex mul/mode). Excite with a short filtered noise/click burst at trigger. Map:
- BEATER → excitation character (short bright filtered-noise burst; brighter/harder = more HF, shorter).
- HEAD TENS → the dominant pitched mode's frequency (+ the classic downward pitch envelope on it, reuse CURVE).
- SHELL SIZE → 1–2 lower-Q body modes' frequencies (bigger = lower).
- DAMPING → decay coefficient of all modes (more damping = shorter, more "dead" thud).
```c
// per trigger: excite modes; per sample: sum modal_tick(m) for each mode, apply amp env
modal_excite(&st->head, head_freq, head_decay_ms);   // precompute cos_w/sin_w/decay
// render: float s = amp*(a0*modal_tick(&st->head)+a1*modal_tick(&st->body1)+...);
```
**Voicing defaults:** organic, woody, warm (Context/02 §2.4). Head mode ≈ 55–80 Hz, one body mode ≈ 120–180 Hz, decays 150–400 ms; beater burst ≈ 2–5 ms. **Distinctness:** the only non-oscillator engine — natural resonance + springy decay, no FM/wavetable character. **Pitfall:** clamp mode freq to [20, 0.45*SR] and decay to (0,1) to avoid NaN/blowup.
**Page 2 (KICK-05):** BEATER, SHELL SIZE, HEAD TENS, DAMPING (4 slots).

### HRD / PX-3 (KICK-06) — hard techno: wavetable + sample layer + distortion
**DSP recipe:** Wavetable body (like WTR) + a sample layer (MIX blends a short punchy sampled kick transient — for Phase B a factory `.rodata` one-shot or a synthesized layer; USR handles user samples) + heavy post-distortion. DRIVE feeds the summed signal through SAT/Fold (reuse §FX chain SAT/Fold with high amt); CRUSH applies bit/SR reduction (reuse `crush()`). This is the loudest, most aggressive model.
**Voicing defaults:** industrial, distorted, cuts through a mix (Context/02 §2.5). Default DRIVE moderate (audible grit, not fizz); CRUSH low-to-off default; MIX ≈ 0.3 sample layer. **Distinctness:** the only intentionally distorted/crushed kick; DRIVE + CRUSH give rave/industrial character. **Pitfall:** DRIVE must stay bounded — the distortion is the §FX bounded forms, output still ≤ 1.0.
**Page 2 (KICK-06):** SAMPLE LAYER, MIX, DRIVE, CRUSH (4 slots).

### DIG / SP-6 (KICK-07) — digital wavetable + sample + bit-depth
**DSP recipe:** Body = digital-character wavetables (chip/square/additive/bit-reduced factory tables via `wt_read_bl`), WAVE IDX selects. BIT DEPTH applies `crush()` bit-reduction as a *timbral* control (retro digital character, always somewhat on, unlike HRD's aggressive CRUSH). PITCH ENV = dedicated pitch-sweep amount. Optional sample layer.
**Voicing defaults:** electro/synthwave, crisp highs, precise low-end tracking (Context/02 §2.6). Default WAVE = a chip/square-ish table; BIT DEPTH ≈ 10–12 bits (subtle crunch); PITCH ENV moderate. **Distinctness:** the "digital/retro" kick — bit-reduction as character, chip waveforms. Contrast with HRD (HRD = distortion/loud; DIG = lo-fi/digital-clean-crunch).
**Page 2 (KICK-07):** WAVE IDX, SAMPLE LAYER, BIT DEPTH, PITCH ENV (4 slots).

### TRS / VX-T (KICK-08) — advanced wavetable + transient synth (909 clarity)
**DSP recipe:** Like WTR but with a more advanced transient synth: TRANS TONE morphs the click spectrum (stick/beater click ↔ white/pink noise burst), TRANS DECAY its length. WT COLOR morphs the body wavetable. CURVE = its own pitch-sweep-curve slot (Context/02 §2.7 explicitly lists CURVE in P2). 909-style: emphasize a fast, bright, clear attack and a thick sub tail.
**Voicing defaults:** punchy 909, extreme attack clarity, thick sub (Context/02 §2.7). Default TRANS TONE ≈ mid (click+noise mix); TRANS DECAY ≈ 2–6 ms; strong 909-side CURVE default. **Distinctness vs WTR:** WTR = clean/neutral transient; TRS = the aggressive 909 attack specialist with noise-burst option. **New primitive:** `noise_t` (white) + `tpt1` for coloring.
**Page 2 (KICK-08):** TRANS TONE, TRANS DECAY, WT COLOR, CURVE (4 slots; note CURVE here is a P2 pitch-curve morph distinct from the Page-1 CURVE — planner to name uniquely, e.g. `PK_TRS_CURVE`).

### ANA / WT-4 (KICK-09) — analog wavetable morph + sub-osc + sample
**DSP recipe:** Body = morph across warm "analog" factory wavetables (sampled vintage-style waves: sine→tri→slightly-saturated) via WAVE MORPH (interpolate between two tables). A dedicated **sub-oscillator** (pure low sine, SUB LEVEL, SUB DECAY) adds the 808 boom under the body. Optional sample layer. Reuse CURVE for 808-vs-909 character.
**Voicing defaults:** warm, fat, 808-style sub-boom (Context/02 §2.8). Default sub prominent (SUB LEVEL ≈ 0.5, SUB DECAY long 400–800 ms for the boom), body warm, CURVE biased to 808 (slow). Default f0 ≈ 45 Hz. **Distinctness:** the 808 sub-boom king — dedicated sub-osc + long decay is unique; warmest/roundest model. **New:** analog-character band-limited tables + wavetable morph (2-table crossfade using `wt_read_bl`).
**Page 2 (KICK-09):** WAVE MORPH, SUB LEVEL, SUB DECAY, SAMPLE (4 slots).

### USR / XT-88 (KICK-10) — user WAV + user wavetable (off-thread load)
**DSP recipe:** Enumerate + load user files at `create_instance` from `module_dir/user/` — this is off the render loop but STILL on the audio thread per A-RESEARCH Pitfall 1 (all six entry points run on the SPI callback). Confirm with Phase A open Q: `create_instance` is where the single calloc is permitted; a bounded file read there is the least-bad option and matches KICK-10 ("file I/O done off audio thread at create_instance"). **Pre-size the buffers at the cap inside the instance struct** (do NOT malloc per-file). Because `model_state` is 4096 bytes and a WAV/wavetable is larger, USR needs its sample/wavetable storage in a dedicated pre-allocated region — **flag for planner: USR's user-sample buffer likely does NOT fit in `model_state[4096]`.** Options: (a) add a fixed USR sample buffer to `bohm_instance` (grows the single calloc — allowed, it's one alloc), cap e.g. a 2048-sample wavetable (8 KB) + a short one-shot sample (e.g. 1 s = 176 KB); (b) cap conservatively. Play: SAMPLE SELECT picks a loaded file, WT MORPH morphs the user wavetable, LAYER VOL mixes sample vs wavetable, PITCH ENV sweeps.
**Off-thread caveat:** the RT-safety rule forbids blocking file I/O on the audio thread; `create_instance` runs there. **This is a genuine constraint tension (flag for planner):** either (1) accept a bounded, one-time read in `create_instance` (matches the requirement text and Phase A's single-calloc-in-create precedent), or (2) defer the actual read to the Phase G writer-pthread pattern (PRST-04 already plans a low-priority pthread). Recommend (1) for Phase B with a hard size cap + graceful fallback to silence/synth if no file, since the requirement explicitly says "at create_instance". Zero file I/O in render/set_param/on_midi/get_param remains absolute.
**Voicing defaults:** whatever the user loads; provide a sensible built-in fallback wavetable so USR is non-silent with no user file (D-B02 non-silent default). **Distinctness:** user content — by definition distinct.
**Page 2 (KICK-10):** SAMPLE SELECT, WT MORPH, LAYER VOL, PITCH ENV (4 slots).

### GEN / HPN (KICK-11) — generative rumble (Phase B scope = the kick, groove is Phase C)
**DSP recipe:** A wavetable/synth kick body (reuse WTR/ANA body) driven by a **generative pitch/velocity sequence**. PRNG (`prng_t` xorshift64) seeded from SEED produces a repeatable sequence of pitches (scale-quantized via `g_scales` table, or free-freq) and velocities. Euclidean density gates which steps fire.
**Phase B vs Phase C scope (CRITICAL — clarify, per additional_context):** REQUIREMENTS.md maps GEN's *full* Page-2 controls to **GRV-04 (Groove Page 2, Phase C)** and transport-synced stepping to Phase C (GRV-02, `get_beat_position`). **Phase B GEN scope:** implement the generative *engine* — the model renders an audible generative kick with a working PRNG + scale-quantize + Euclidean-density gating + seed determinism, so it is selectable and sounds distinct (SC1). It can self-clock (free-running at a default internal step rate) for offline validation and on-device audition in Phase B. **Transport sync to project BPM and the Groove Page 2 UI (SEED/SCALE/SEQ LEN/LPF/DENSITY controls) are wired in Phase C.** Phase B provides the PRNG/scale/Euclidean primitives + the GEN vtable + a minimal set of P2 slots so it is voice-able now; Phase C connects it to `get_beat_position` and Groove Page 2. **Flag this scope split explicitly in the plan** so GEN isn't over-built or under-built.
**Voicing defaults:** hypnotic, rolling, evolving 16th-note sub-bass variations (Context/02 §2.10). Default SEED = fixed repeatable; scale = minor/free; density moderate. **Distinctness:** the only model whose pitch *changes per hit* — evolving, generative.
**Page 2 (KICK-11):** minimal Phase-B slots (e.g. SEED, SCALE, DENSITY, + body controls); full SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY set lands on Groove Page 2 in Phase C (GRV-04). Determinism: reseed from SEED on change → same sequence (CLAUDE.md, KICK-11).

## FX Chain (KICK-14, KICK-13) — the 5 post-kick modes

**Contract:** FX TYPE selects mode (0–4 → Diode/Clip/SAT/Fold/Crush), FX AMT scales intensity [0,1]. Applied as the final per-sample stage of each model's render (after COLOR, before the int16 boundary). **All outputs MUST be bounded ≤ 1.0** (FNDTN-07; the int16 clamp is a safety net, not the plan — engines self-limit per Phase A precedent). Implement as one shared `fx_process(mode, x, amt, state)` in `dsp_primitives`. Source formulas: Context/02 §3 (FX descriptions), Context/03 §4 (soft-clip, wavefolder), CLAUDE.md (bounded forms), STATE.md watchpoint (do NOT use the unbounded reference `fast_tanh`).

| Mode | Formula (bounded) | Notes |
|------|-------------------|-------|
| **Diode** | back-to-back diode rounding: `y = sign(x) * (1 - exp(-|x|*k))` where `k = 1 + amt*K` | Smooth, even+odd harmonics, asymptotes to ±1 → always bounded. Context/02 "back-to-back diode rounding". `exp` per-sample is costly → table or `x*(1.5 - 0.5*x*x)`-style cheap approx for small x; recommend a small `.rodata` shaping LUT indexed by `x`. |
| **Clip** | asymmetric soft clip, bounded both sides: pos `y = tanh(g*x)`, neg `y = g*x/(1+|g*x|)`, `g = 1 + amt*G` | Context/03 §4.2 exact form BUT note: Context uses `x/(1+|x|)` for neg (bounded, good) and `tanh` for pos (bounded, good) — this is the safe version. Do NOT use `x/(1-x)` (the reference's divergent bug). Approximate `tanh` with `x*(27+x*x)/(27+9*x*x)` (Padé) or a LUT — no per-sample `tanhf`. |
| **SAT** | warm parallel saturation: `y = (1-mix)*x + mix*softsat(x)`, `mix = amt`, `softsat(x)=x/(1+|x|)` (bounded) or cubic `x - x³/3` clamped | Parallel = blend dry + saturated (Context/02 "warm parallel saturation"). Low-order polynomial (Context/03 §4 "tube saturation: smooth low-order polynomial"). Bounded by construction. |
| **Fold** | wavefolder: `y = fold(g*x)` using triangle-fold `y = |((x+1) mod 4) - 2| - 1` (maps to [-1,1]), `g = 1 + amt*F` | Context/03 §4.3 wavefolder formula (adapted to unit range). Folds peaks back → metallic grit. Bounded to [-1,1] by the triangle fold. Watch DC — high fold can add offset; consider a leaky HP or center. |
| **Crush** | bit + sample-rate reduction: bit: `y = round(x * levels)/levels`, `levels = 2^bits`, `bits = 16 - amt*12`; SR: sample-and-hold, hold `y` for `hold = 1 + amt*H` samples | Context/02 "bit-depth and sample-rate reduction". Holds state (last sample + phase counter) → the ONLY stateful FX mode; `fx_process` needs a small state struct for Crush. Bounded (rounding a bounded input stays bounded). |

**Reused by HRD/DIG:** HRD's DRIVE = SAT/Fold at high amt; HRD/DIG CRUSH/BIT DEPTH = the Crush bit-reduction. Implement once, call from both the FX chain and those models.

**Voicing:** at FX AMT = 0 each mode should be ≈ transparent (or the model's clean sound); amt sweeps to a musically useful max without fizz/aliasing. Fold and Crush alias hardest — test at high pitch + high amt in the harness (D-B02 "no aliasing blow-up at extremes").

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Sine / any osc | per-sample `sinf` | shared `.rodata` table + `wt_read` (D-04) | denormal-free, faster, deterministic |
| Band-limited waves | runtime BLIT/BLEP synthesis | build-time generated band-limited tables | zero audio-thread cost; kicks rarely alias |
| Modal resonator | ad-hoc IIR per model | shared `modal_t` complex-rotation primitive | exact, stable, reusable across PHY + any resonant body |
| PRNG | `rand()` | shared `prng_t` xorshift64 | `rand()` nondeterministic across libc; seed must reproduce (KICK-11) |
| Scale quantize | runtime interval math | `.rodata` `g_scales` LUT | branch-light, no per-sample math |
| Soft clip / saturate | the reference `fast_tanh` (unbounded neg side) | bounded forms in §FX chain | `x/(1-x)` diverges → clicks/blowup (STATE.md bug #2) |
| `tanh`/`exp` in render | per-sample `tanhf`/`expf` | Padé/rational approx or `.rodata` LUT | transcendentals stall A53, may pull libmvec |
| Bit/SR crush | per-model bespoke crusher | shared `crush()` | HRD + DIG + FX Crush share one implementation |
| Param parse | `atof` | copy `parse_f` (locale-independent) | locale-safe (UI-01) |
| Per-model malloc | separate allocations | overlay `model_state[4096]` (single calloc) | FNDTN-03; USR sample buffer is the one exception (grow the single calloc) |
| FX output safety | rely on the int16 clamp | engines self-limit + bounded FX | clamp is a net, not a plan (Phase A D-12 precedent) |

**Key insight:** Phase B's value is 10 *distinct, musical* voices sharing a small set of correct, RT-safe primitives. Build the primitives (FX, modal, band-limited wt, prng, scale, noise, crush) once and well; each model becomes a thin recipe over them. Voicing (default values + range maps) is where the real work is — the DSP is mostly assembly of proven blocks.

## Common Pitfalls

### Pitfall 1: Stale `model_state` across a model switch (KICK-13)
**What goes wrong:** Switching MODEL reinterprets the same 4096 bytes; the new model reads the old model's leftover phase/filter/envelope state → click, NaN, or garbage until the next trigger.
**Why:** `model_state` is one shared region; there is no per-model zeroing on switch today (dsp.c just sets `inst->model`).
**How to avoid:** memset `model_state` to 0 on MODEL change + re-prime defaults + require every `trigger` to fully init phases/filters (Pattern 2). Add a harness test: switch A→B→A, trigger, assert finite + non-stale.
**Warning signs:** click on model change; NaN in harness after a switch sequence.

### Pitfall 2: dsp.c hardcodes `fm2_set_param` (blocks all other models)
**What goes wrong:** `omega_set_param` (dsp.c line 118) routes ALL kick keys to `fm2_set_param`. New models never receive their params.
**How to avoid:** Refactor the param path to dispatch through the active model. Give each model a `set_param(inst,key,val)` (handles Page-1 + its Page-2 keys) and call `g_models[inst->model]->...`. Requires either adding a `set_param` fn ptr to the vtable OR reusing `set_p2` for all keys. **Flag for planner:** the vtable (`omega.h`) is LOCKED (`_Static_assert` on `plugin_api_v2_t`, but the `kick_model_vtable_t` is NOT static-asserted — it CAN gain a field). Cleanest: add a `set_param` fn ptr to `kick_model_vtable_t`, or route all keys through `set_p2` (rename its role). Confirm the model vtable struct is safe to extend (it is — no static_assert binds its layout, and it's internal, not host ABI).

### Pitfall 3: Per-sample transcendentals sneaking into new models
**What goes wrong:** `tanhf`/`expf`/`sinf`/`powf` in a render loop → A53 stalls, CPU budget blown, possible libmvec `_ZGV*` symbols failing the glibc gate.
**How to avoid:** All coeff math in set_param/trigger; approximations/LUTs for FX shaping; the `objdump -T` gate catches libmvec leaks at CI.
**Warning signs:** CI glibc/libmvec gate fails; on-device CPU spike (Phase D measures, but keep budget in mind now).

### Pitfall 4: USR sample buffer doesn't fit in `model_state[4096]`
**What goes wrong:** A user WAV/2048-sample wavetable is larger than 4096 bytes; overlaying it in `model_state` overflows.
**How to avoid:** Add a dedicated pre-sized USR buffer to `bohm_instance` (still one calloc), hard-capped. Load off render (at create_instance), fall back to a built-in table if absent. See §USR recipe.

### Pitfall 5: Aliasing / divergence at extreme params (D-B02 automated criterion)
**What goes wrong:** High pitch + high FM index / high fold / high crush aliases or diverges; a wavefolder or FM index sweep can exceed [-1,1] transiently.
**How to avoid:** Bounded FX forms; clamp FM index and fold gain; sweep-test every param lo→hi in the harness asserting `isfinite` + `|x|≤1`. Add band-limited tables only where the harness shows aliasing.
**Warning signs:** harness assert fails at extreme settings; harsh digital fizz on high-pitch sweeps.

### Pitfall 6: Model enum / registry index mismatch
**What goes wrong:** `g_models[]` order doesn't match `model_id_t` enum indices → wrong model dispatched.
**How to avoid:** Keep the registry array in exact enum order; the `MODEL_COUNT`-sized array + a `_Static_assert(sizeof g_models/sizeof g_models[0] == MODEL_COUNT)` (add one) catches a missing entry at compile time.

## Code Examples

### Complex-rotation modal resonator (PHY, new primitive)
```c
// dsp_primitives.h — Source: CLAUDE.md Physical Modeling (complex-rotation form)
typedef struct { float re, im, cos_w, sin_w, decay; } modal_t;
static inline void modal_excite(modal_t *m, float freq_hz, float decay_per_sample, float amp) {
    float w = 2.0f * (float)M_PI * freq_hz / OMEGA_SR;   // precompute at trigger, not per sample
    m->cos_w = cosf(w); m->sin_w = sinf(w); m->decay = decay_per_sample;
    m->re = amp; m->im = 0.0f;                            // impulse-excite
}
static inline float modal_tick(modal_t *m) {
    float re = m->re * m->cos_w - m->im * m->sin_w;
    float im = m->re * m->sin_w + m->im * m->cos_w;
    m->re = re * m->decay; m->im = im * m->decay;         // e^{jw} * e^{-decay}
    return m->re;                                         // real part = decaying sinusoid
}
```

### Bounded FX modes (KICK-14)
```c
// dsp_primitives.c — all bounded to [-1,1]; NO per-sample tanhf/expf in the hot path
static inline float fx_clip(float x, float g) {          // asym soft clip (bounded both sides)
    float gx = g * x;
    return gx >= 0.0f ? gx / (1.0f + gx)                  // >=0: x/(1+x) -> +1
                      : gx / (1.0f - gx);                 // <0:  x/(1-|x|) -> -1  (|gx|<1 kept by g clamp)
}
static inline float fx_fold(float x, float g) {          // triangle wavefolder -> [-1,1]
    float v = g * x + 1.0f;
    v = v - 4.0f * floorf(v * 0.25f);                     // mod 4 into [0,4)
    return fabsf(v - 2.0f) - 1.0f;                        // triangle fold
}
// Crush is stateful (sample-hold + bit quantize): keep last + phase in a tiny struct.
```
Note: the `fx_clip` neg branch requires `|g*x| < 1` to stay bounded; clamp `g` and pre-limit `x` (or use `x/(1+|x|)` symmetric if asymmetry isn't needed). Prefer symmetric `x/(1+|x|)` where "always bounded" matters more than asymmetry (STATE.md bug #2). SAT uses the symmetric form parallel-blended.

### New model skeleton (copy fm2.c)
```c
// src/models/phy.c — Source: fm2.c template + CLAUDE.md modal PM
typedef struct phy_state { modal_t head, body1, body2; env_t amp; noise_t exc; /*...*/ } phy_state;
_Static_assert(sizeof(phy_state) <= 4096, "phy_state fits model_state");
static void phy_trigger(bohm_instance_t*, int, int);    // excite modes + amp env + reset
static void phy_render(bohm_instance_t*, float*, float*, int);  // sum modal_ticks * amp, FX
static void phy_set_p2(bohm_instance_t*, const char*, const char*);
static int  phy_p2_slot_desc(bohm_instance_t*, char*, int);
const kick_model_vtable_t g_phy_vtable = { "PHY", phy_trigger, phy_render, phy_set_p2, phy_p2_slot_desc };
```

## State of the Art

| Old Approach | Current Approach | When | Impact |
|--------------|------------------|------|--------|
| Reference `fast_tanh` asym unbounded (Context/04 line 91) | bounded `x/(1+|x|)` + Padé tanh + triangle fold | Project decision (STATE.md bug #2) | No divergence/clicks in FX |
| 2-pole biquad resonators for modal | complex-rotation modal (`z *= e^{jw}e^{-d}`) | CLAUDE.md | Stable under sweep, vectorizes, one mul/mode |
| `rand()` for generative | xorshift64 seeded PRNG | CLAUDE.md | Deterministic, reproducible sequences |
| Runtime file wavetable load | build-time `.rodata` band-limited tables | CLAUDE.md/KICK-15 | Zero audio-thread I/O (USR is the only file path, off render) |
| Blanket `-ffast-math` | granular subset + FPCR FTZ | Phase A | No libmvec symbols; gate stays green |

**Deprecated/outdated:** The entire Context/04 reference `bohm_render_block` is a learning artifact carrying all five STATE.md bugs — do not copy it. Its `fast_tanh`, `atof` params, and unclamped int16 cast are explicitly forbidden.

## Open Questions

1. **`kick_model_vtable_t` extension for per-model `set_param` (Pitfall 2).**
   - What we know: dsp.c hardcodes `fm2_set_param`; other models won't get params. The model vtable is internal (not host ABI) and has no `_Static_assert` binding its layout, so it can gain a field.
   - What's unclear: whether the planner adds a `set_param` fn ptr to the vtable vs. repurposing `set_p2` for all keys.
   - Recommendation: add `void (*set_param)(bohm_instance_t*, const char*, const char*)` to the vtable; dispatch all kick keys through the active model. Low risk (internal struct).

2. **USR file load on the audio thread (create_instance) vs. requirement text.**
   - What we know: KICK-10 says "file I/O done off audio thread at create_instance"; A-RESEARCH confirms create_instance runs on the SPI thread but is where the single calloc is permitted (one-time, off the hot render loop).
   - What's unclear: whether a bounded one-time read in create_instance is acceptable, or must defer to the Phase G writer-pthread.
   - Recommendation: bounded one-time read in create_instance with a hard size cap + silent/synth fallback (matches the requirement). Keep render/set_param/on_midi/get_param file-I/O-free absolutely. Confirm on-device that create_instance file read doesn't cause a load-time stall.

3. **GEN Phase B vs Phase C scope boundary (KICK-11 vs GRV-02/GRV-04).**
   - What we know: GEN's full controls + transport sync map to Phase C (Groove Page 2, `get_beat_position`).
   - Recommendation: Phase B ships the generative engine + PRNG/scale/Euclidean primitives + a minimal P2, self-clocking for audition; Phase C wires transport + Groove Page 2. Encode this split in the plan so GEN is neither over- nor under-built. (See §GEN recipe.)

4. **Exact default voicing numbers (all models).**
   - What we know: Context/02+03 give character + the pitch-sweep formula; specific defaults are taste.
   - Recommendation: encode the defensible defaults in this doc; the D-B02 manual round + D-B04 audit doc are the final authority. Not a blocker.

## Environment Availability

**SKIPPED (no NEW external dependencies).** Phase B is pure first-party C/DSP over the Phase A toolchain. The build depends only on the already-validated `ghcr.io/charlesvestal/schwung-builder:latest` (aarch64-linux-gnu-gcc, glibc 2.35, libm) and native `cc` for the harness — both confirmed in Phase A (A-RESEARCH §Environment Availability). No new tools, services, or runtimes. The on-device manual voicing round (D-B02/D-B04) reuses the A-04 deploy loop (CI artifact → `scripts/deploy.sh` → trigger on Move); Move hardware availability is the same PENDING item carried from A-04, not a new dependency.

## Validation Architecture

> nyquist_validation is enabled (config.json `workflow.nyquist_validation: true`). Section included.

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Plain C `assert` + tiny runner (native `cc`); no third-party framework — unchanged from Phase A |
| Config file | none — the Makefile `test` target is the config (extend it for new models) |
| Quick run command | `make test` (compiles `tests/test_render.c` + model TUs natively, runs asserts, writes per-model WAVs) |
| Full suite command | `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` (native asserts + cross-build + glibc/libmvec/single-export gate) |

### Phase Requirements → Test Map
Each of the 9 new models + FM2 re-voice + FX chain + model-switch gets the SAME automated D-B02 battery. "distinct" = render buffer differs measurably (e.g. spectral-centroid or RMS-envelope delta) from every other model's default render.

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| KICK-03 (FM4) | non-silent default; 6 P2 params each change output; bounded at extremes; distinct vs others | unit | `make test` (render + energy + lo/hi-differ + finite/≤1 sweep + distinctness delta) | ❌ Wave 0 |
| KICK-04 (WTR) | " | unit | `make test` | ❌ Wave 0 |
| KICK-05 (PHY) | " + modal freq/decay clamped (no NaN) | unit | `make test` | ❌ Wave 0 |
| KICK-06 (HRD) | " + DRIVE/CRUSH bounded, no divergence | unit | `make test` | ❌ Wave 0 |
| KICK-07 (DIG) | " + BIT DEPTH audibly changes, bounded | unit | `make test` | ❌ Wave 0 |
| KICK-08 (TRS) | " + transient distinct from body | unit | `make test` | ❌ Wave 0 |
| KICK-09 (ANA) | " + sub-osc present, long decay bounded | unit | `make test` | ❌ Wave 0 |
| KICK-10 (USR) | non-silent with fallback (no user file); loads a fixture WAV off-render; params change output | unit + smoke | `make test` (fixture WAV in tests/) | ❌ Wave 0 |
| KICK-11 (GEN) | non-silent; SAME seed → byte-identical render (determinism); different seed → different; density gates steps; bounded | unit | `make test` (fixed-seed hash stability + seed-differ) | ❌ Wave 0 |
| KICK-13 (switch) | A→B→A switch + trigger: finite, non-stale, no NaN; each model's p2_slot_desc emits valid bounded JSON with correct slot count | unit | `make test` (switch sequence + JSON parse/length asserts) | ❌ Wave 0 |
| KICK-14 (FX) | each of 5 modes audibly alters a test tone; amt=0 ≈ transparent; output ≤1 + finite at max amt + high pitch (anti-alias/divergence) | unit | `make test` (per-mode render + bound sweep) | ❌ Wave 0 |
| FM2 re-voice (D-B03) | re-voiced FM2 still non-silent, bounded, all 11 params change output; regression WAV | unit | `make test` | ✅ extend existing FM2 test |
| D-B02 manual (all 10) | usable default, musical CURVE, even knob sweep, distinct character, no live artifacts | manual (on-device) | deploy + audition each model per VOICING_AUDIT.md | manual |
| D-B04 audit doc | 10 models × checklist PASS/PENDING filled on-device | manual (on-device) | fill docs/VOICING_AUDIT.md | manual |

### Sampling Rate
- **Per task commit:** `make test` (native harness; renders the touched model's WAV + asserts; < 10 s).
- **Per wave merge:** `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` (native + cross-build + glibc/libmvec/export gate).
- **Phase gate:** full suite green in CI (D-11) + the on-device manual voicing audit (D-B04 doc filled: all 10 models PASS the D-B02 manual items) before `/gsd:verify-work`. Mirrors A-04's hardware-checkpoint pattern.

### Wave 0 Gaps
- [ ] `tests/test_render.c` — extend with a per-model loop: for each `model_id_t`, prime defaults → trigger → render → assert non-silent + finite + ≤1; write `tests/output/<model>_kick.wav`. (covers non-silent + bounded for all)
- [ ] `tests/test_params.c` (or extend) — per-model P2 param lo-vs-hi render-differ assertion (D-B02 "each param measurably changes output").
- [ ] `tests/test_switch.c` (or extend) — model-switch A→B→A + trigger, finite/non-stale asserts (KICK-13).
- [ ] `tests/test_fx.c` (or extend) — 5 FX modes on a test tone: audible-change + bounded-at-max asserts (KICK-14).
- [ ] `tests/test_distinct.c` (or extend) — pairwise distinctness metric across all 10 default renders (D-B02 "distinct character").
- [ ] `tests/fixtures/user_kick.wav` — small fixture WAV for USR load test (KICK-10).
- [ ] `tests/test_gen.c` (or extend) — GEN determinism: same seed → identical buffer hash; different seed → differs (KICK-11).
- [ ] `Makefile` — add all new model TUs to both the `dsp.so` (aarch64) and `test` (native) builds; add the wavetable-generator target (emits `wavetables.h`).
- [ ] `docs/VOICING_AUDIT.md` — 10-model × D-B02-checklist matrix (D-B04), filled on-device.
- [ ] Framework install: none — plain C, system `cc`.

*(No new test framework needed; the Phase A harness pattern extends cleanly.)*

## Sources

### Primary (HIGH confidence)
- `src/omega.h`, `src/dsp_primitives.h`, `src/models/fm2.c`, `src/models/model_registry.c`, `src/dsp.c`, `src/ui.c` — LOCKED ABI, vtable contract, existing primitives, FM2 template, dispatch path, Page-2 assembly (read directly this phase)
- `Context/02_OHMFORCE_BOHM_SYSTEM_SPEC.md` §2 — all 10 model architectures + sound character + key controls (the authoritative per-model semantics + name mapping); §3 — Kick Page 1 params + FX mode descriptions (Diode/Clip/SAT/Fold/Crush)
- `Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` §4 — exponential pitch envelope (`f(t)=f_start·e^{-t/τ}+f_fund`, τ≈15 ms), asymmetric soft-clipper, wavefolder formula; §2 saturation models
- `Context/04_BOHM_SCHWUNG_MODULE_DESIGN.md` — reference render pipeline + `fast_tanh` (studied as the ANTI-pattern; the five bugs to avoid)
- `Context/05_SOURCE_INDEX_AND_REFERENCES.md` — reference C DSP engines safe to study: Sophie (4-op FM algorithms, bit/rate crush) for FM4; 9W9 (909 sweeps/transients) for TRS; 8W8 (808 boom, diode clip) for ANA; Maze (wavefolder, ADAA sat) for FX; Simian/Weird Dreams (pitch-env + SVF noise) for PHY/WTR
- `CLAUDE.md` — DSP stack: modal complex-rotation PM, band-limited wavetables (linear interp + guard), TPT SVF, FM4 static routing tables, xorshift PRNG + scale LUTs, RT-safety (no alloc/log/file-IO on any entry point), float-only path, single calloc, granular fast-math + FPCR
- `.planning/phases/A-foundation-fm2-model/A-RESEARCH.md` — vtable pattern, wavetable + env + tpt1 primitives, output stage, all-entry-points-on-SPI-thread rule, five reference bugs, malloc-trap/WAV harness design

### Secondary (MEDIUM confidence)
- STATE.md Key Decisions / watchpoints — five reference bugs, single-calloc, synthesis-method IDs, TPT-over-ladder, FPCR
- REQUIREMENTS.md KICK-03..14 — exact Page-2 slot lists per model + FX mode list + traceability

### Tertiary (LOW confidence — taste-dependent, flagged)
- Specific default parameter values / min-max ranges per model — grounded in known 808/909/FM/analog norms + Context character notes, but final tuning is the on-device D-B02 ear round.

## Metadata

**Confidence breakdown:**
- ABI / vtable / RT-safety / file layout: HIGH — read directly from locked `src/` + Phase A research; append-only pattern proven
- Per-model DSP technique: HIGH — standard techniques (FM, modal, wavetable, PRNG) all specified in CLAUDE.md + Context; reference engines available to study
- FX chain formulas: HIGH for bounded correctness (Context/03 + CLAUDE.md), MEDIUM for exact "warm"/"diode" character (taste; approximations given)
- Voicing defaults: MEDIUM — defensible starting points from documented character + kick norms; explicitly the taste-dependent, ear-final part (D-B02)
- Validation architecture: HIGH — extends the proven Phase A harness pattern; every requirement mapped to an automated + (where relevant) manual check

**Research date:** 2026-09-29
**Valid until:** ~2026-10-29 (stable C/DSP domain; re-check only if the Schwung ABI revs or the schwung-builder image changes)
