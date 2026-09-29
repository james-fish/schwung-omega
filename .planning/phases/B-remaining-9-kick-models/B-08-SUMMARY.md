---
phase: B-remaining-9-kick-models
plan: 08
subsystem: usr-gen-userload-generative
tags: [KICK-10, KICK-11, user-sample, user-wavetable, off-render-load, single-calloc, rt-safety, generative, xorshift, scale-quantize, euclidean-density, determinism, complete-registry, D-B02, model-registry, fx-chain]
requires:
  - "B-01: kick_model_vtable_t.set_param dispatch, MODEL_COUNT=10 designated-initializer registry with NULL slots (MODEL_USR/MODEL_GEN the LAST two), clean model-switch re-init (PK_MODEL memset + re-prime), NULL-slot silence; create_instance receives (module_dir, json_defaults)"
  - "B-02: prng_t xorshift64 (prng_seed/prng_next_f, nonzero-seed forced, deterministic), scale_quantize over g_scales + NUM_SCALES, wt_read_bl band-limited read + g_wavetables .rodata banks, env_t + env_coeff_from_ms, fx_config/fx_process + fx_state_t"
  - "B-03: reusable assert_param_responsive voicing battery + pairwise distinctness metric, fm2.c reference-bar structure (parse_f, clampf, tpt_g_from_hz, dual-env CURVE blend, control-rate/render-rate split)"
  - "B-04..B-07: WTR/TRS (B-04), ANA/DIG (B-05), HRD/FM4 (B-06), PHY (B-07) each replaced their NULL slot — the other seven of the ten had to already be non-NULL for this plan's complete-registry gate to hold"
provides:
  - "USR (KICK-10, MODEL_USR): user-content playback engine. Loads a custom PCM16 WAV (user/kick.wav) and/or a raw single-cycle wavetable (user/wavetable.raw) OFF the render loop at create_instance into a pre-sized region of the SINGLE instance calloc; when no user file is present it synthesises a built-in wavetable fallback so USR is never silent. g_usr_vtable; 4 Page-2 slots (SAMPLE SEL / WT MORPH / LAYER VOL / PITCH ENV); state <=4096 (big buffer lives in bohm_instance); render loop transcendental-free"
  - "GEN (KICK-11, MODEL_GEN): deterministic self-clocking generative kick. xorshift64 prng_t seeded from SEED drives a repeatable pitch sequence, scale-quantized via scale_quantize (SCALE), Euclidean DENSITY gating selects which self-clocked steps fire; a WTR/ANA-style wavetable body renders each hit. g_gen_vtable; 3 Page-2 slots (SEED / SCALE / DENSITY); state <=4096; render loop transcendental-free"
  - "RT-safety contract proven: fopen/fread/fclose appear ONLY in omega_create (create_instance); render_block/set_param/on_midi/get_param are file-I/O-free (the malloc trap guards render, and create is off the hot path per B-RESEARCH Open Question 2). No host->log added"
  - "Single-calloc growth (Pitfall 4): bohm_instance grew by usr_wavetable[OMEGA_WT_GUARD] (~8 KB) + usr_sample[44100] (~176 KB) + usr_sample_len/usr_wt_loaded/usr_loaded; _Static_assert(sizeof(struct bohm_instance) < 800000) still passes"
  - "Registry FULLY POPULATED: [MODEL_USR]=&g_usr_vtable and [MODEL_GEN]=&g_gen_vtable replace the last two NULL slots — all 10 slots non-NULL. Array-length _Static_assert(==MODEL_COUNT) intact + a NEW runtime no-NULL-slot gate in test_gen.c (initializer NULL-ness is not compile-time inspectable)"
  - "GEN Phase-B scope is the generative ENGINE ONLY (PRNG + scale-quantize + Euclidean density + self-clocking). Transport-sync via get_beat_position + the full Groove Page 2 UI (SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY) are explicitly deferred to Phase C (GRV-02/GRV-04), commented at the top of gen.c"
  - "src/ui.c root Model enum lists all 10 names in enum order (FM2,FM4,WTR,PHY,HRD,DIG,TRS,ANA,USR,GEN); the per-model Page-2 splice from p2_slot_desc is B-09"
  - "test_gen.c (KICK-11 determinism: same seed -> byte-identical / diff seed -> differs / DENSITY gates hits) + KICK-10 USR fixture-load + fallback both non-silent + the complete-registry runtime gate; test_distinct now a live 10-registered / 45-pairs gate; test_switch 100 pairs"
affects:
  - "src/omega.h (bohm_instance grown by usr_ buffer fields; committed in 69d11a1)"
  - "src/dsp.c (omega_create USR off-render file load: usr_load_wav/usr_load_wavetable/usr_join_path, module_dir/user/; committed in 69d11a1)"
  - "src/models/usr.c (new; committed in 3d2a5da)"
  - "src/models/gen.c (new; engine in 3d2a5da, non-silent-default + deterministic-downbeat fix in e6ba5f0)"
  - "src/models/model_registry.c (last two NULL slots replaced; befb75b)"
  - "src/ui.c (root Model enum options -> all 10; befb75b)"
  - "tests/test_gen.c (new; befb75b)"
  - "tests/fixtures/user_kick.wav (new PCM16 mono fixture; committed in 3d2a5da)"
  - "Makefile (test-gen target + GEN_TEST_SRCS, wired as a test prerequisite; befb75b)"
tech-stack:
  added: []
  patterns:
    - "Bounded one-time file load in create_instance ONLY (RT-safety, B-RESEARCH Open Q2): usr_load_wav validates the canonical 44-byte PCM16 header (fmt==1, 16-bit, 1..8 ch), mono-downmixes, and reads at most sizeof(usr_sample) frames; usr_load_wavetable reads exactly OMEGA_WT_LEN floats + writes the guard sample. On any absence/malformed/short read the usr_ buffers stay zeroed and usr_loaded stays false -> usr.c falls back to a built-in wavetable body so USR is non-silent. This is the module's ONLY file I/O and it never touches the render path."
    - "Single-calloc growth for the big user buffer (Pitfall 4): the ~184 KB usr_wavetable + usr_sample live BY VALUE inside bohm_instance (one calloc, freed once), NOT in model_state[4096] (which stays <=4096 for the per-model overlay) and NOT via a per-file malloc. usr_state holds only playback cursors/coeffs + its fx_state."
    - "GEN determinism via xorshift64 + full trigger reset (KICK-11): prng_seed(SEED) reseeds on SEED change so the same seed reproduces the pitch sequence; on trigger the amp/pitch envelopes + body phase are fully zeroed FIRST so a triggered render is byte-identical regardless of any tail from a prior render. NO rand() anywhere (B-02 prng_t only)."
    - "GEN self-clocking for Phase B: the sequencer free-runs at a fixed internal step rate (GEN_STEP_FRAMES) so GEN is audible/auditionable offline and on-device NOW; transport-sync to get_beat_position is Phase C. The downbeat ALWAYS fires on trigger (a kick must sound immediately); the Euclidean DENSITY pattern then gates the subsequent self-clocked steps."
    - "Render loops transcendental-free (CLAUDE.md): both engines confine powf/tanf to control-rate paths (gen_step_pitch pitch-quantize, set_param exp maps, tpt cutoff precompute); the per-sample render reads only wt_read_bl + precomputed coeffs + fx_process. Verified by inspection."
    - "Complete-registry defense-in-depth: the B-01 _Static_assert only guarantees ARRAY LENGTH == MODEL_COUNT, not that initializer values are non-NULL (not compile-time inspectable). test_gen.c adds a runtime loop asserting g_models[m] (and ->render/->trigger) non-NULL for all MODEL_COUNT so a forgotten slot fails the suite — appropriate now that B-08 is the final model plan."
key-files:
  created:
    - "src/models/usr.c"
    - "src/models/gen.c"
    - "tests/test_gen.c"
    - "tests/fixtures/user_kick.wav"
  modified:
    - "src/omega.h"
    - "src/dsp.c"
    - "src/models/model_registry.c"
    - "src/ui.c"
    - "Makefile"
decisions:
  - "USR realises user content via a bounded one-time read in create_instance (not on any hot path): the malloc trap guards render_block, and create is where the single calloc already happens, so fopen/fread/fclose here is RT-safe and compliant (B-RESEARCH Open Question 2, recommendation 1). On absence/failure USR falls back to a built-in wavetable rather than being silent (D-B02 non-silent default)."
  - "The ~184 KB user sample+wavetable buffer lives inside bohm_instance (grows the single calloc), NOT model_state[4096], because it cannot fit the per-model overlay and the project mandates one allocation per instance (Pitfall 4). usr_state keeps only cursors/coeffs so its <=4096 static_assert still holds."
  - "GEN is scoped to the generative ENGINE only for Phase B (PRNG + scale-quantize + Euclidean density + self-clocking) with an explicit top-of-file comment; transport-sync (get_beat_position) and the full Groove Page 2 UI are deferred to Phase C (GRV-02/GRV-04) to avoid pulling Phase-C work into a Phase-B model plan (Open Question 3)."
  - "The complete-registry no-NULL check is a RUNTIME test assertion (test_gen.c), not a _Static_assert, because designated-initializer values are not compile-time inspectable for NULL-ness; the length static_assert stays to guard the array size. This is the correct final-model-plan gate."
  - "GEN's trigger always fires the downbeat and fully zeroes envelopes before firing, trading a strictly-Euclidean step-0 for guaranteed immediate audibility + byte-identical determinism — the density pattern still gates every subsequent self-clocked step."
metrics:
  duration: 12min
  tasks: 3
  files: 9
  completed: 2026-09-29
---

# Phase B Plan 08: USR + GEN — User-Load & Generative Kicks Summary

**One-liner:** The final two models — USR (KICK-10), a user WAV/wavetable player that loads content OFF the render loop at create_instance into the single instance calloc with a non-silent built-in fallback, and GEN (KICK-11), a deterministic self-clocking generative kick (xorshift64 PRNG + scale-quantize + Euclidean density) scoped to the Phase-B engine only — registered into the last two NULL slots so all 10 models are now live.

## What Was Built

**USR (KICK-10)** is the user-content playback engine and the module's only file-touching model. At `create_instance` (via `module_dir`), `usr_load_wav` reads an optional `user/kick.wav` (canonical 44-byte PCM16, 1..8 ch mono-downmixed, bounded to the buffer cap) and `usr_load_wavetable` reads an optional `user/wavetable.raw` single cycle (+ guard sample). All of this is a bounded one-time read off the hot path (the malloc trap guards `render_block`, not create — B-RESEARCH Open Question 2). The ~184 KB user buffers (`usr_wavetable[OMEGA_WT_GUARD]` + `usr_sample[44100]`) live BY VALUE inside `bohm_instance` — the single calloc grew, not `model_state[4096]` and not a per-file malloc (Pitfall 4); `_Static_assert(sizeof(struct bohm_instance) < 800000)` still passes. When no user file is present (`usr_loaded == false`), `usr.c` synthesises a built-in wavetable body so USR is never silent. Page-2 exposes SAMPLE SEL / WT MORPH / LAYER VOL / PITCH ENV; output routes through COLOR LP + the shared `fx_process` chain; render is transcendental-free.

**GEN (KICK-11)** is a deterministic, self-clocking generative kick — the Phase-B ENGINE only. An xorshift64 `prng_t` seeded from SEED produces a repeatable pitch sequence, scale-quantized via `scale_quantize` (SCALE), with Euclidean DENSITY gating selecting which self-clocked steps fire; each hit renders a `wt_read_bl` body. The sequencer free-runs at a fixed internal step rate so GEN is auditionable offline/on-device now; **transport-sync (`get_beat_position`) and the full Groove Page 2 UI are deferred to Phase C (GRV-02/GRV-04)**, with an explicit scope comment at the top of `gen.c`. Reseeding on SEED change + fully zeroing envelopes/phase on trigger make the same-seed render byte-identical. NO `rand()` — B-02's `prng_t` only. Page-2 exposes SEED / SCALE / DENSITY.

**Registry finalized:** `[MODEL_USR]=&g_usr_vtable` and `[MODEL_GEN]=&g_gen_vtable` replace the last two NULL slots — all 10 slots are now non-NULL. The B-01 array-length `_Static_assert(==MODEL_COUNT)` stays, and a NEW runtime no-NULL-slot gate lives in `test_gen.c` (initializer NULL-ness isn't compile-time inspectable). `src/ui.c` root Model enum now lists all 10 names in enum order (the per-model Page-2 splice is B-09's job).

## Tasks Completed

| Task | Name | Commit | Files |
| ---- | ---- | ------ | ----- |
| 1 | Grow instance for USR + off-render file load in create_instance (KICK-10) | 69d11a1 | src/omega.h, src/dsp.c, tests/fixtures/user_kick.wav |
| 2 | USR engine (KICK-10) + GEN engine (KICK-11); non-silent default + deterministic downbeat | 3d2a5da, e6ba5f0 | src/models/usr.c, src/models/gen.c |
| 3 | Register USR + GEN (last two NULL slots); complete-registry gate; GEN determinism test; Model enum options | befb75b | src/models/model_registry.c, src/ui.c, tests/test_gen.c, Makefile |

## Verification

- `make test` exits 0 — all suites green:
  - `test_fm2`, `test_fx`, `test_render`: pass.
  - `test_switch`: **100 pairs** — clean model-switch re-init holds with all 10 models registered.
  - `test_params`: USR (4 P2 + FX + 8 Page-1) and GEN (3 P2 + FX + 8 Page-1) voicing batteries pass (non-silent default, each param measurably responsive, bounded across full lo->hi sweep).
  - `test_distinct`: **10 registered / 45 pairs** — USR and GEN distinct from all prior models and each other.
  - `test_gen`: GEN determinism OK (seed-stable byte-identical, seed-differ, DENSITY lo<hi gate) + USR load OK (fixture-loaded + fallback both non-silent) + complete-registry runtime gate (10/10 non-NULL, each with render/trigger).
- RT-safety confirmed: `fopen`/`fread`/`fclose` appear ONLY in `src/dsp.c` `omega_create` helpers; `omega_render_block`/`set_param`/`on_midi`/`get_param` are file-I/O-free. No `host->log` added.
- Single-calloc: `bohm_instance` grew by the hard-capped `usr_` fields; `_Static_assert(sizeof(struct bohm_instance) < 800000)` intact and passing (the build proves it).
- GEN uses `prng_t` xorshift64 (no `rand()`); both render loops confine `powf`/`tanf` to control-rate paths (verified by inspection).
- `[MODEL_USR] = &g_usr_vtable` and `[MODEL_GEN] = &g_gen_vtable` present; length `_Static_assert(==MODEL_COUNT)` intact; `src/ui.c` Model options list all 10 names.
- `tests/output/USR_kick.wav` (262188 bytes) and `tests/output/GEN_kick.wav` (262188 bytes) written non-empty.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] GEN silent by default + non-deterministic across triggers**
- **Found during:** Task 2/3 (GEN engine).
- **Issue:** (a) `dsp.c` primes only the shared Page-1 keys, so GEN's `npulses` (Euclidean density) was left uninitialised (`< 1`) unless Page-2 was explicitly set — a bare trigger could gate out the downbeat and render silence, failing the D-B02 non-silent-default criterion. (b) A trigger did not fully zero the amp/pitch envelopes + body phase first, so a tail from a prior render could leak into the next render and break byte-identical seed determinism (KICK-11).
- **Fix:** Default `npulses` to a moderate density (0.5) when Page-2 was never primed; fully `env_trigger(...,0,0)` all three envelopes + zero `body_phase` at the start of `gen_trigger`; make the downbeat ALWAYS fire (a triggered kick must sound immediately) and let the Euclidean DENSITY pattern gate only the subsequent self-clocked steps.
- **Files modified:** src/models/gen.c
- **Commit:** e6ba5f0

## Known Stubs

None. USR and GEN each render a full kick from real DSP. USR's fallback is a genuine synthesised wavetable body (not an empty/placeholder value) and is the documented behaviour when no user file is present; GEN is a complete generative engine (transport-sync + Groove Page 2 UI are a scoped Phase-C follow-up, not a stub — GEN self-clocks and is fully auditionable now).

## Deferred / Out-of-Scope

- **Cross-build + glibc gate** (`make dsp.so && ./scripts/glibc_gate.sh build/dsp.so`): no local `aarch64-linux-gnu-gcc` / Docker on the macOS host (confirmed absent). CI is the authoritative build/glibc gate — matching the B-04..B-07 precedent recorded in STATE.md. Both new render loops are transcendental-free by inspection, so no new libmvec `_ZGV*` risk is introduced.
- **GEN transport-sync + full Groove Page 2 UI** (SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY, `get_beat_position`): explicitly Phase C (GRV-02/GRV-04). Phase B ships the self-clocking engine only.
- **Per-model Page-2 splice from `p2_slot_desc`** into `ui.c` and the on-device manual voicing audit (`VOICING_AUDIT.md`): B-09.
- **On-device manual voicing sign-off** for USR/GEN (D-B02 manual / D-B04 audit): automated criteria are green; the ear round lands in B-09 + the on-device audit doc.

## Self-Check: PASSED

- src/models/usr.c FOUND
- src/models/gen.c FOUND
- tests/test_gen.c FOUND
- tests/fixtures/user_kick.wav FOUND
- src/models/model_registry.c ([MODEL_USR]/[MODEL_GEN]) FOUND
- src/ui.c (10-name Model enum) FOUND
- tests/output/USR_kick.wav FOUND
- tests/output/GEN_kick.wav FOUND
- commit 69d11a1 FOUND
- commit 3d2a5da FOUND
- commit befb75b FOUND
- commit e6ba5f0 FOUND
