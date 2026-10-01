# Quick 261001-wft — Summary

**Completed:** 2026-10-02
**Status:** Code complete, all native tests GREEN. Release v0.3.0.

Fixed a batch of 7 Omega issues and — the headline — enabled **p-locking** in the
new Schwung release. Four atomic commits (one per task; Task 4 was TDD red→green).

## What shipped

### Finding 1 — p-locking now works (headline) — `f6f28b5`
**Root cause (investigated against the `charlesvestal/schwung` host source):** the
chain host builds its automation/modulation/p-lock parameter table by reading the
module's **static `module.json` on disk** (`parse_chain_params` → looks for a
`"ui_hierarchy"` or `"chain_params"` key in the file). Omega had neither — it only
served `ui_hierarchy` dynamically via `get_param` — so the host registered **zero**
params and every p-lock hit `find_param_by_key → NULL → LANE_PLOCK_UNKNOWN_PARAM`,
surfacing on-device as *"not locked! can't automate this."* The display pages worked
because they come from the separate dynamic `get_param("ui_hierarchy")` path.
**Fix:** added a static `"chain_params"` array to `module.json` (92 entries — the
full union of automatable keys, types/min/max/defaults mirrored from `src/ui.c` +
`src/params.c`; enums carry `options`; `grv_gseqlen` is `int`). Additive only — no
`ui_hierarchy` key added, so the dynamic on-screen hierarchy is untouched.

### Finding 2 — GEN root pitch stable across scale switches — `03ffbcb`
The unquantized ROOT descriptor in `ui.c` declared a Hz domain (`min:20,max:2000`),
so the host sent raw Hz that the handler clamped to `1.0` → `gen_base_hz = 20·100^1 =
2000 Hz` ("super high"). Now ROOT is a single normalized `0..1` descriptor in both
modes; the `20·100^v` log map (20 Hz–2 kHz) is unchanged, so scale switches no longer
move the root.

### Finding 6 — SEQ LEN shows whole integers 1..64 — `03ffbcb`
Added a `UP_INT` UI param type (emits `"type":"int"` with integer min/max/step) and a
key-specific `get_param` branch formatting `grv_gseqlen` with 0 decimals.

### Finding 3 — GEN built-in sub-octave (no new control) — `ccbea9a`
GEN oscillator now sums a second wavetable reader one octave down (half the phase
increment) at a fixed ~0.45 blend before the fold/env, adding low-end weight. New
`gen_sub_phase` state in `groove.h`, zeroed on init and each note trigger. RT-safe
(reuses the fundamental's computed increment, no added divide/transcendental).

### Finding 7 — stronger GEN/TAPS drive matched to the kick — `ccbea9a`
`groove_drive_block` pre-gain strengthened (`k = 1 + drive·9`, was `·4`) with a
diode-style asymmetric transfer and makeup compensation mirroring the kick FX
`out_gain = 1/(1+amt·comp)` form, so full-right DRIVE is aggressive and changes
character, not just level. Self-limiting `x/(1±x)` transfer → bounded, no new
transcendental. Applies to both GEN and TAPS (shared block).

### Finding 4 — GEN filter env + 18 dB/oct resonant filter + 65% filter decay — `d1c4bdd`→`7722499`
The GEN COLOR filter is now a **3-pole (18 dB/oct) resonant TPT cascade** with a
bounded feedback term (~20% resonance). A per-note **filter envelope** opens the
cutoff ~20% at onset and decays, with filter tau = **0.65 × amp tau** (closes before
the note tail). New state in `groove.h` (`gen_filt_env`, `gen_filt_env_coef`, 3rd-pole
pair). All coefficient math stays at control rate (`expf`/`tanf` only in
`set_param`/`tpt_g_from_hz`); per-sample render is algebraic + one `clampf` → bounded.
TAPS 2-pole + the OFF bypass paths left byte-identical.

### Finding 5 — GEN DECAY knob curve — `7722499`
DECAY input is pre-curved (`v' = v²`) before the `tau_s = 0.005·30^v'` map, so the
musical short/plucky range (old usable band ~0–0.3) now spreads across most of the
travel; endpoints unchanged (≈5 ms → ≈150 ms).

## Verification
- `make test` — **ALL GREEN** (fm2, fx, switch[100 pairs], params, distinct,
  gen[10/10], groove, readback, samples, perf, taps-redesign, render).
- `module.json` valid JSON, 92 unique keys, no `ui_hierarchy` key, < 64 KB
  (`tools/check_chain_params.py`).
- Zero audio-thread malloc/transcendentals (powf/expf/tanf only at control rate);
  output bounded.

## Manual / on-device (requires Move hardware + aarch64 build; CI is the build gate)
- Hold a step + turn any Omega knob → locks (no "can't automate this").
- GEN: switch scales repeatedly → root stays stable/low.
- GEN SEQ LEN → whole integers; GEN low-end weightier; GEN/TAPS DRIVE aggressive;
  GEN filter opens/closes within the note (resonant, short tail); DECAY plucky range
  across the knob.

## Version / release
- `module.json` + `release.json` bumped **0.2.1 → 0.3.0**.
- No new catalog PR needed: `charlesvestal/schwung` catalog entry (PR #585, merged)
  tracks `github_repo`/`main`/`asset_name` with no pinned version — publishing the
  v0.3.0 GitHub release + bumped `release.json` updates catalog users automatically.
