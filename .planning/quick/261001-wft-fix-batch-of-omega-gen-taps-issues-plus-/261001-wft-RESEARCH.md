# Quick 261001-wft — Research / Investigation Findings

**Gathered:** 2026-10-02
**Scope:** GEN groove + TAPS fixes, and the big one — p-locking support in new Schwung.

All GEN-related issues live in **`src/groove.c`** (the GEN *groove* voice), NOT `src/models/gen.c` (the legacy GEN kick model). Keys are the `PK_GRV_G*` globals, handled in `groove_set_param` (line ~535) and rendered in `groove_tick` (GEN voice ~349+).

---

## FINDING 1 (CRITICAL) — Why p-locking is refused: "not locked! can't automate this"

**Root cause: Omega declares ZERO automatable params to the host.**

The new Schwung chain host builds its automation/modulation/p-lock parameter table by
reading the module's **static `module.json` file on disk** — it does NOT use the
dynamic `get_param("ui_hierarchy")` for this. Verified in the host source
(`charlesvestal/schwung`):

- `src/modules/chain/dsp/chain_params.c` → `parse_chain_params(module_path, …)`:
  opens `"<module_dir>/module.json"`, then `strstr(json, "\"ui_hierarchy\"")`. If that
  key is present in the file it parses params from it; **else `*count = 0`** and it
  falls back to a top-level `"chain_params"` array. If neither is in the file → **0 params**.
- `src/modules/chain/dsp/chain_lanes.c` (p-lock write handler, ~line 1448):
  ```c
  inst->lanes_plock_refusal = LANE_PLOCK_UNKNOWN_PARAM;
  chain_param_info_t *pinfo = find_param_by_key(inst, target, param);
  if (!pinfo) return;        // <-- every Omega param hits this
  ```
- `chain_internal.h`: `LANE_PLOCK_UNKNOWN_PARAM /* the module has no such parameter */`
- `src/shared/param_pages/page_controller.mjs`: maps that refusal to the on-screen
  notice `"can't automate this"` / `"not locked, …"`.

Omega's `module.json` currently contains only `id/name/abbrev/version/author/description/api_version/capabilities`
— **no `ui_hierarchy`, no `chain_params`** — so the host registers nothing and refuses
every p-lock. Display pages still work because they come from the separate dynamic
`get_param("ui_hierarchy")` path (`page_controller.mjs` ~line 1227). That's why the UI
looks fine but nothing is automatable.

**Fix (chosen): add a static `"chain_params"` array to `module.json`** listing every
automatable key. This is the legacy path and is parsed when no `ui_hierarchy` key is in
the file (exactly our case). A flat superset array is ideal — it covers every param
regardless of which page/model is active, and keeps the dynamic on-screen hierarchy
untouched (no drift risk for the display).

Parser contract (`parse_param_object` in host `chain_params.c`) per entry:
- **Required:** `"key"`, `"name"` (or `"label"`), `"type"` ∈ {`"float"`,`"int"`,`"enum"`}.
- **Optional:** `"min"`,`"max"`,`"default"`,`"step"`,`"unit"`,`"display_format"`.
- **Enum:** `"options":[…]`; host sets `max_val = option_count-1` automatically.
- Capacity `MAX_CHAIN_PARAMS = 256` (we have ~50 — fine). `key[32]`, `name[64]`.

Entries must mirror the keys/types/min/max/defaults already defined in `src/ui.c`
(`uiparam_t` tables) and `src/params.c` defaults so automation ranges match the knobs.
Keys to include = the union of: root (`model`, `master_vol`), kick page-1
(`pitch`,`length`,`curve`), kick page-2 shared (`attack`,`trs_dec`,`trs_tne`,`color`,
`filter_route`,`fx_type`,`fx_amt`,`fx_tone`), every per-model page-2 key (FM2/FM4/WTR/
PHY/HRD/DIG/TRS/ANA/USR), all groove globals (`grv_*` incl. the GEN `grv_g*` set), and
the performer page (`duck*`,`dj_filt`,`dj_reso`,`clip`). Use `PK_*` string macros in
`src/omega.h` as the canonical key spellings.

**Acceptance:** on device, hold a step + turn any Omega knob → value locks (no
"can't automate this"). Also unlocks modulation/CC typing for the same reason.

---

## FINDING 2 — Unquantized GEN ROOT jumps "super high" after changing scales

`groove_set_param` top: `float v = clampf(parse_f(val), 0.0f, 1.0f);` — every value is
clamped to 0..1 and treated as normalized. But `src/ui.c` `ui_emit_gen_groove1` swaps the
ROOT param descriptor to a **Hz domain** when unquantized:
```c
unq ? {PK_GRV_GROOT,"ROOT HZ","RTHZ",UP_FLOAT,"Hz","1",NULL,"20","2000"}
    : {PK_GRV_GROOT,"ROOT","ROOT",UP_FLOAT,"","0.012",NULL,NULL,NULL};
```
A `min:20,max:2000` descriptor makes the host send the **raw Hz value** (e.g. 500) for
that knob. `v` then clamps to `1.0`, and the handler computes
`gen_base_hz = 20 * powf(100, v)` → `20*100^1 = 2000 Hz`. Switching into Unquantized (or
nudging ROOT there) pins it to the top → "super high". The `grv_gscale` handler has the
same `20*100^gen_root_param` line, so a scale change re-applies the mis-scaled root.

**Fix:** keep ROOT a normalized `0..1` param in BOTH modes (do not emit a 20..2000
descriptor). Keep the single `gen_base_hz = 20*100^v` (20 Hz..2 kHz log) map. Show Hz as
a **derived display string** in `get_param` formatting only (compute `20*100^param`),
never as the param's min/max. This makes scale switches pitch-stable (the dev's original
intent per the in-code comment) and removes the clamp explosion. Verify the kick `pitch`
Hz convention and mirror whatever is consistent.

---

## FINDING 3 — GEN wave needs a built-in sub (no new control)

GEN voice reads a wavetable morph (`gen_wave_pos`, `gen_fold`) in `groove_tick`
(~line 383). Requirement: bake a sub an octave below into the generated/added signal so
GEN has more low-end weight, with **no new UI param**. Cleanest: in the GEN oscillator,
sum a second reader one octave down (half phase increment) at a fixed blend (e.g. ~0.4–0.5
of the fundamental), or bake a two-partial (fundamental + sub-octave) table. Keep it
RT-safe (control-rate coeffs only), mono-summable, and bounded (no added clipping — fold
headroom already applies). Confirm no aliasing at the top of the ROOT range.

---

## FINDING 4 — GEN filter envelope + resonance

GEN filter is `PK_GRV_COLOR` (shared) rendered in the GEN path; today it's a plain
LP sweep. Requirements:
- A **filter envelope that opens ~20%** (modest positive env depth ≈ 0.2 of range).
- **18 dB/oct resonant filter** (3-pole) with resonance **~20%**.
- **Filter decay ≈ 65% of the (amp) note decay** (`gen_decay`/`gen_env_coef`,
  `grv_gswing` handler ~line 650). Derive the filter env coef from the same `tau_s` scaled
  by 0.65.
TPT SVF is the project's filter idiom (`dsp_primitives`). 18 dB/oct = cascade a TPT 1-pole
after a 2-pole SVF LP, or a 3-pole config. Resonance maps to SVF k. All coeffs at control
rate; env applied per-sample via a precomputed coef. Keep bounded/stable.

---

## FINDING 5 — GEN DECAY curve (usable range only ~0.0–0.3)

`grv_gswing` handler maps `tau_s = 0.005 * powf(30, v)` (5 ms..~150 ms). User says the
usable range is only the first ~0.3 of the knob. Apply an input curve so the musical
short range spreads across the whole travel — e.g. remap `v' = v*v` (or similar
expo/power curve) before the existing `tau_s` map, so 0..1 knob lands mostly in the
short/plucky region. Keep endpoints sane (still reach the long tail at full right).

---

## FINDING 6 — SEQ LENGTH must be discrete integers 1..64 (no decimals)

Handler already computes an int: `gen_seqlen = 1 + (int)(v*63+0.5)` → good. The problem is
**display**: the knob shows decimal places. Two parts:
1. **Display:** `get_param` for `grv_gseqlen` must format as an integer (0 decimals).
   Check `src/ui.c`/`params.c`/`dsp.c` get_param formatting for this key — likely using a
   fractional `pk_format_value`. Also the `ui.c` descriptor uses
   `type":"float","step":"0.0159","min":1,"max":64` — consider declaring it `int` /
   integer step so the host shows whole numbers.
2. **chain_params entry** (Finding 1) for `grv_gseqlen` should be `type:"int"`,
   `min:1,max:64,step:1` so automation is also integer-quantized.

---

## FINDING 7 — DRIVE on GEN/TAPS too tame; match the kick drive

Groove DRIVE is `PK_GRV_DRIVE` (`grv_drive`), a diode drive in the groove FX chain
(`groove.c`). The kick's FX drive (FX TYPE/AMT path, `src/dsp.c` / per-model, FX AMT) is
described as "rather nice" and stronger. Increase the groove drive's gain staging /
pre-gain curve so full-right is noticeably more aggressive — ideally reuse the same
drive transfer/pre-gain the kick FX uses (the diode/sat shaper + auto-gain comp per
UIX-06) so GEN/TAPS drive character matches the kick. Preserve output-gain compensation
so raising drive changes character, not just level.

---

## Build / verify / ship notes

- Native offline tests: `make test` (and the groove/taps targets) — see `Makefile`.
  Add/extend native asserts where cheap (root pitch stability across scale switches;
  seqlen integer formatting; chain_params JSON present & parseable).
- aarch64 build is Docker (`ghcr.io/charlesvestal/schwung-builder`) — may be CI-only on
  this host; `scripts/` has build/deploy/glibc_gate. CI is the authoritative build gate.
- Bump `version` in `module.json` + `release.json` for the new release.
- `module.json` is also what the host parses for chain_params — keep it valid JSON and
  ≤ 64 KB (host caps the read at 65536 bytes; our additions are well under).
