# B1-01 PLAN — Param value cache + readback + rich schema

**Phase:** B1 Param/UI Infrastructure (v1.1)
**Requirements:** UIX-01, UIX-02, UIX-03, UIX-04, UIX-05, UIX-06
**Brief:** `.planning/REFINEMENT-FEEDBACK.md`

## Root cause (confirmed)

- `omega_get_param` answers only `"ui_hierarchy"`, returns `-1` for every key. Host
  reads back per-key values (`ui_chain.js:37-40`) → all knobs render at 0.
- Models store *derived* values (Hz/ms/coeffs) in `model_state`, never the raw
  normalized value the host set → nothing to echo back.
- Model switch does `memset(model_state)` + reprime to `"0.5"` → per-model state lost;
  UI shows 0 regardless.
- Schema (ui.c) is bare `float 0..1` — no `default`/`enum`/`options`/`unit`/`step`/
  `short_name`, so no string selectors, no units, no correct knob start positions.

## Design

**Central raw-value cache on `bohm_instance`** (the single source of truth for readback
and per-model memory), decoupled from each model's derived DSP state:

- `float kick_cache[MODEL_COUNT][PKI_COUNT]` — every kick param (shared Page-1 keys +
  every model's Page-2 keys) indexed by a `pk_index` enum. Shared keys stored per-model
  → per-model memory (UIX-04). Model-unique keys are already unique strings; storing a
  full row per model is harmless (~10×N×4B, well under budget).
- `float global_cache[GKI_COUNT]` — master_vol, model, and the 8 groove keys (global,
  not per-model).
- `bool kick_cache_set[MODEL_COUNT][PKI_COUNT]` — tracks explicit sets vs defaults.

**Key→index + defaults tables** in a new `src/params.c`/`params.h`:
- `int pk_kick_index(const char *key)` → PKI_* or -1
- `int pk_global_index(const char *key)` → GKI_* or -1
- `static const float g_kick_defaults[PKI_COUNT]` and `g_global_defaults[GKI_COUNT]` —
  the schema `default` values (musical, not 0).
- `param_meta_t` per key: `type` (float/int/enum), `min`, `max`, `step`, `unit`,
  `short_name`, `options` (for enums) — consumed by ui.c to emit the rich schema.

**set_param** (dsp.c): parse+record into the right cache (`kick_cache[model][idx]` or
`global_cache[idx]`, set the `_set` flag), THEN dispatch to model/groove as today.

**get_param** (dsp.c): 
- `"ui_hierarchy"` → `omega_build_ui`
- kick key → format `kick_cache[model][idx]` (int/enum → `%d`, float → `%.4f`,
  locale-independent) into buf; return length
- global key → format `global_cache[idx]`
- else → -1

**Model switch** (dsp.c): `memset(model_state)`, then for each kick key re-prime the
model via `set_param` using the target model's cached value (or default if unset) —
restores that model's sound AND its knob positions.

**create_instance**: seed both caches from the defaults tables and prime the active
model, so a bare create shows musical defaults (not 0).

**Rich schema (UIX-02/03/05)** in ui.c: emit `default`, `step`, `unit`, `short_name`,
and `type:"enum"`+`options` for discrete params (fx_type first: Diode/Clip/SAT/Fold/
Crush). Value readback (UIX-01) is what actually positions knobs; schema `default` is
the fallback the host uses before first readback.

**Drive auto-gain (UIX-06)**: in `fx_config`/`fx_process` apply an output-gain
compensation for SAT/Fold/drive modes so raising amount changes character, not level.
(Full per-model drive lives in B2; here wire the compensation hook in the shared FX.)

## Tasks

1. `src/params.h` + `src/params.c`: pk_index/global_index enums, key↔index lookup,
   defaults tables, `param_meta_t` metadata table. `_Static_assert` counts.
2. `bohm_instance`: add `kick_cache`, `kick_cache_set`, `global_cache`; bump size
   assert if needed. Seed in `omega_create`.
3. `dsp.c`: record in `set_param`; implement value readback in `get_param`; rewrite
   model-switch to replay cached/default values (no more blind reprime-to-0.5).
4. `ui.c`: emit rich schema (type/options/default/min/max/step/unit/short_name),
   fx_type as enum. Keep no-alloc bounded appends.
5. `fx`: add output-gain compensation for drive-like modes (UIX-06).
6. Tests: extend `tests/test_switch.c` / add `tests/test_params_readback.c` — after
   set_param, get_param echoes value; after model switch, get_param returns the other
   model's stored values; enum keys round-trip; unknown key → -1; schema JSON stays
   valid + balanced with new fields.

## Verification

- `make test` green (readback + per-model memory + enum round-trip + valid schema).
- No allocation/transcendental added to render (only cache writes at control rate).
- Host ABI structs/asserts untouched.
