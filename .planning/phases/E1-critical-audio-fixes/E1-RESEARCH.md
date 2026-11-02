# Phase E1: Critical Audio Fixes — Research

**Researched:** 2026-09-30
**Domain:** C audio DSP — FX chain dispatch, duck/sidechain, bit-crusher scaling, pitch envelope, soft clipper
**Confidence:** HIGH — all diagnoses are derived directly from reading the source code and tracing the exact numeric paths. No guessing.

---

## Summary

Eight bugs were reported from first on-device testing of v1.1. Code investigation shows they fall into three categories: (1) one root-cause dispatch error that cascades into three reported symptoms, (2) three independent parameter-scaling/direction mistakes, and (3) two gain-staging defaults that leave the duck and TAPS groove inaudible until the user takes explicit setup actions. The soft clipper has a fundamental algorithm issue that makes it actively harmful.

**Primary recommendation:** Fix the FX chain dispatch first (Bug #1). It is a one-line fix that also resolves the perceived "bit-crushed TAPS" (Bug #3) and the "FM2 ATTACK does nothing" (Bug #2). The remaining fixes are each isolated to a single parameter calculation.

---

## Bug Diagnoses

### Bug #1 — FX Chain: Only Diode Works; Clip/SAT/Fold/Crush All Sound Like Crush

**Root cause confirmed:** `src/models/fm2.c` line 106 (and identical code in every model):

```c
float v = clampf(parse_f(val), 0.0f, 1.0f);
...
fm->fx_type = v;   /* 0..1 -> mode 0..4 */
fx_config(&fm->fx, (int)(fm->fx_type * 4.0f + 0.5f), fm->fx_amt);
```

The UI defines `FX_TYPE` as an enum with 5 named options (`src/ui.c` line 106, line 161: `OPT_FX = ["Diode","Clip","SAT","Fold","Crush"]`). The host sends integer indices **0, 1, 2, 3, 4**. `parse_f("1")` returns `1.0f`. `clampf(1.0f, 0.0f, 1.0f)` returns `1.0f`. Then `(int)(1.0f * 4.0f + 0.5f) = 4` = `FX_CRUSH`.

**Full mapping of what the host sends vs what the model receives:**

| Host sends | parse_f | clampf | mode computed | Should be |
|-----------|---------|--------|---------------|-----------|
| "0" | 0.0 | 0.0 | 0 = FX_DIODE | FX_DIODE |
| "1" | 1.0 | 1.0 | 4 = FX_CRUSH | FX_CLIP |
| "2" | 2.0 | 1.0 | 4 = FX_CRUSH | FX_SAT |
| "3" | 3.0 | 1.0 | 4 = FX_CRUSH | FX_FOLD |
| "4" | 4.0 | 1.0 | 4 = FX_CRUSH | FX_CRUSH |

Diode works because index 0 maps correctly. Everything else is Crush.

**Affected files (identical pattern in every model):**
- `src/models/fm2.c` lines 147, 150, 153, 239
- `src/models/phy.c` lines 180, 181, 184, 307
- `src/models/hrd.c` lines 187, 188, 191, 276
- `src/models/dig.c` lines 166, 167, 170, 254
- `src/models/wtr.c` (same pattern)
- `src/models/trs.c` (same pattern)
- `src/models/ana.c` (same pattern)
- `src/models/fm4.c` (same pattern)
- `src/models/usr.c` (same pattern)

**Fix:** Remove the `0..1` clamp on the `fx_type` storage, OR do NOT use `clampf(parse_f(val), 0.0f, 1.0f)` for enum parameters. The correct approach: store the raw integer without the 0..1 clamp. In each model's `set_param`, for `PK_FX_TYPE`, use `parse_f(val)` directly (no clampf) before the `* 4.0f` multiplication, OR change the formula to `(int)(parse_f(val) + 0.5f)` which maps integer strings "0"–"4" directly to modes 0–4.

**Preferred single-line fix per model:**
```c
/* OLD (broken): */
fm->fx_type = v;
fx_config(&fm->fx, (int)(fm->fx_type * 4.0f + 0.5f), fm->fx_amt);

/* NEW (correct): integer enum index, no 0..1 clamp */
fm->fx_type = (float)(int)(parse_f(val) + 0.5f);   /* rounds "0"→0, "1"→1, etc. */
if (fm->fx_type < 0.0f) fm->fx_type = 0.0f;
if (fm->fx_type > 4.0f) fm->fx_type = 4.0f;
fx_config(&fm->fx, (int)fm->fx_type, fm->fx_amt);
```

Then in render: `int fx_mode = (int)fm->fx_type;` (remove the `* 4.0f` step entirely).

**Cascading effects:** This fix also resolves the Crush sample-and-hold that was smearing transients (Bug #2 partial) and makes the TAPS groove echo a clean kick instead of a Crush-processed one (Bug #3 partial).

---

### Bug #2 — FM2 ATTACK Does Nothing, TRS TNE Does Nothing

**Root cause:** Primarily a cascade of Bug #1. The FX_CRUSH sample-and-hold (hold up to 15 samples when `fx_amt > 0`) stretches/smears short transients. A click that lasts ~3–10 samples gets held at the last sample value, losing its character. After Bug #1 is fixed and `fx_type = FX_DIODE` at default, `fx_process` is largely transparent (`amt=0` → dry/wet blend returns the original signal).

**Secondary confirmation:** The `ATTACK` signal path in `fm2.c` render (line 224–226):
```c
float click = fm->trs_amp * env_tick(&fm->trs_env);
click = tpt1_lp(&fm->trs_tone_lp, click, fm->trs_tone_g);
float s = car_out * amp * 0.6f + click * 0.4f;
```
At default `trs_amp=0.5`, `trs_dec_ms=15ms`, `trs_tone_g` at fc=8250Hz (TRS_TNE=0.5): the click is clearly present (0.2 peak amplitude). The click IS generated and DOES affect output when measured in native tests (`test_params.c` passes). On-device, the Crush smearing was masking it.

**TRS TNE:** Sets the `trs_tone_lp` LP cutoff on the click (500–16000 Hz). This is a subtle spectral-brightness control on an already-short burst. It works, but the useful range is the upper half (higher TNE = brighter click). After Bug #1 fix, the spectral difference will be more noticeable since Crush is no longer smearing it.

**Additional standalone fix:** The `trs_tone_g` starts at 0.0f from calloc before `omega_prime_model` runs. If for any reason the prime does not set `PK_TRS_TNE` before the first trigger, `tpt1_lp(click, g=0)` returns `y = v + s = v + s` where g/(1+g) = 0 → the filter output is 0. Guard by initializing `trs_tone_g` in the model struct initializer or ensuring `trs_dec_ms` and `trs_tone_g` are defaulted to non-zero values that won't filter the click to silence.

**Fix in `fm2.c`:** The primary fix is Bug #1. A secondary defensive fix: initialize `fm->trs_tone_g` to a non-zero default in the state struct or guard it in render with `if (fm->trs_tone_g <= 0.0f) fm->trs_tone_g = 1.0f;`.

---

### Bug #3 — TAPS Bit-Crushed and Way Too Quiet

**Root cause:** Compound — two independent causes.

**Cause A (primary): Bug #1 cascade.** The TAPS groove tick (`groove.c groove_tick`) reads `kick_l/kick_r` from the model render output. If the kick was processed through FX_CRUSH (because of the enum clamp bug), the taps echo the crushed kick, which sounds bit-crushed. After Bug #1 fix, taps will echo a clean kick.

**Cause B (gain staging): Too-quiet default.** The TAPS groove volume chain:
- `grv_vol` defaults to `0.0f` (`groove_init` line 122 in `groove.c`, global default `GKI_GRV_VOL=0.0f`)
- Even at `grv_vol=1.0`, the tap output is: `sum += tap_level[t] * tap_decay[t] * buf_value` where `tap_decay[t]` for tap 1 at `LENGTH=0.5` = `(0.3 + 0.65*0.5)^1 = 0.625`, giving effective tap gain of `0.6 * 0.625 = 0.375`
- The total from 4 taps sums to roughly `0.375 + 0.234 + 0.146 + 0.092 = ~0.85` at full ring — audible but less than the kick

The user had to "crank VOL to 100 + add DRIVE" to level-match. This is a gain-staging issue: the tap path has insufficient output level to compete with the kick at default settings.

**Cause C (feedback buildup):** With `fb_amount=0.88` (high LENGTH) and kicks feeding the ring each bar, the gentle saturator `x/(1+0.25|x|)` is not strong enough to prevent energy accumulation. The ring can build up to where the saturator starts coloring the sound at each recirculation.

**Fix:** Increase the per-tap output gain. Changing `tap_decay[t]` scaling or adding a makeup gain after the tap sum will raise the TAPS output to level-match the kick more naturally. Consider changing the tap weight formula so the sum at default LENGTH=0.5 produces approximately 1.0x output.

---

### Bug #4 — Ducking Completely Inaudible

**Root cause:** The duck is working correctly mathematically, but requires an audible groove signal first. There is nothing to duck when `grv_vol=0.0` (the default). The user observed inaudible ducking while also struggling with Bug #3 (TAPS inaudible).

**Signal flow verification (`dsp.c` lines 519–529):**
```c
inst->duck_env *= inst->duck_rel_coef;
float target = 1.0f - inst->duck_depth * inst->duck_env;
inst->duck_gain_s += inst->duck_smt_a * (target - inst->duck_gain_s);
float dg = inst->duck_gain_s;
```
At `duck_depth=1.0`, `duck_env=1.0` (just triggered): `target = 0.0`. The gain slews to 0, ducking the groove lows to 0. Mathematically correct.

**Actual issue:** `duck_depth` defaults to `0.0` (`[GKI_DUCK]=0.0f` in `g_global_defaults`). The user must manually raise DUCK. Then the TAPS signal must also be audible. Because TAPS is near-silent (Bug #3), the duck appears to do nothing.

**Secondary issue worth verifying on-device:** The DUCK only attenuates the groove LOWS (`duck_bs_lp` crossover). If the groove content is mostly mids/highs (which depends on `grv_color`), the duck attenuation may be imperceptible even when active. The bass-split formula at `dsp.c` lines 527–528 attenuates only the low-pass component and passes the high-pass unchanged. If `grv_color` at default (~8 kHz LP) has already attenuated the lows, the duck's crossover may be acting on a nearly-empty low band.

**Fix:** Fix Bug #3 first (make TAPS audible at default settings). Duck will then be demonstrably audible. No code change needed in the duck logic itself, but consider lowering the `duck_bs_g` default crossover to around 150–200 Hz so the duck effect targets more of the groove's sub energy.

---

### Bug #5 — PHY CURVE Inverted / Blows Up Above 25%

**Root cause confirmed** in `src/models/phy.c` `phy_trigger` lines 239–240:

```c
float sweep_mult = 1.0f + (0.8f + 1.2f * p->curve);   /* 909 sweeps deeper */
float head_start = clampf(head_f * sweep_mult, 20.0f, 0.45f * OMEGA_SR);
```

At `curve=0`: `sweep_mult = 1.8`, so even with zero CURVE the head is always excited 1.8x the target frequency. At `curve=0.25`: `sweep_mult = 2.1`. At `curve=1.0`: `sweep_mult = 3.0`.

The `(0.8f + ...)` offset means CURVE can never reduce the sweep — it always starts at a minimum 1.8x. The user hears the initial burst at 1.8x the target pitch even at CURVE=0. Increasing CURVE beyond ~0.25 raises this starting pitch enough that the burst becomes piercing/high.

Note: The `modal_tick` resonator does NOT re-tune during playback. The pitch sweep comes entirely from the fact that the head is excited at `head_start` (higher freq) while the head MODE itself is set to `head_f` (lower freq) — the two interact through the beater burst. This produces a partial pitch drop effect, not a continuous sweep.

**Fix:** Change the formula so CURVE=0 means no sweep (excite at the target frequency), and CURVE=1 means full sweep:
```c
/* Fixed: curve=0 -> no sweep; curve=1 -> 3x sweep */
float sweep_mult = 1.0f + p->curve * 2.0f;
```
This makes the behavior: `curve=0` → excite at target Hz (thud, no drop), `curve=0.5` → excite at 2x target, `curve=1.0` → excite at 3x target (deep 909 pitch drop).

**HEAD TENS issue (linked):** The existing formula `head_f = pbase * (0.75 + 0.5 * p->head_tens)` caps detune at ±25% of PITCH. This is already fixed in B2. The on-device bug report confirms it: "HEAD TENS increases pitch to unusable range rather than lowering." With the corrected phy.c decoupling (PITCH sets base Hz, HEAD TENS shifts ±25%), this is fixed by the B2 commit. Verify on-device after the sweep_mult fix above.

---

### Bug #6 — DIG BIT DEPTH Only Audible in 0–0.1 Range

**Root cause confirmed** in `src/models/dig.c` `dig_set_param` lines 157–161:

```c
float bits = 6.0f + v * (14.0f - 6.0f);
d->bit_levels = bits_to_levels(bits);
```

The mapping direction is INVERTED relative to user expectation:
- `v=0` → `bits=6` → `levels=64` → MAXIMUM CRUSH (very audible)
- `v=0.5` → `bits=10` → `levels=1024` → subtle 10-bit crush
- `v=1.0` → `bits=14` → `levels=16384` → nearly clean

The user turns the knob from 0 upward expecting MORE crush, but finds less crush. The knob passes through the audible range (0–~0.15) then becomes inaudible.

**Fix:** Invert the mapping. Higher knob = more bit-crushing:
```c
/* Fixed: v=0 → clean (14-bit), v=1 → maximum crush (6-bit) */
float bits = 14.0f - v * (14.0f - 6.0f);
d->bit_levels = bits_to_levels(bits);
```

The default `[PKI_DIG_BITDEPTH]=0.5f` then maps to 10-bit (same audible starting point but now the user can turn UP for more crush, which is the expected direction).

---

### Bug #7 — HRD DRIVE Doesn't Engage Until 50%; CRUSH Does Nothing

**DRIVE root cause** in `src/models/hrd.c` lines 177–178:

```c
h->drive_mode = (v > 0.6f) ? FX_FOLD : FX_SAT;
fx_config(&h->drive_fx, h->drive_mode, h->drive_amt);
```

From v=0 to v≤0.6: uses FX_SAT mode. `fx_process(FX_SAT, x, v, st)` = `og * ((1-v)*x + v * x/(1+|x|))`. This is a gentle compression. At v=0.5, the signal is a 50% blend between dry and a soft-saturated version — barely audible on a signal with peak amplitude ~0.7 (the body wave after amplitude envelope). The SAT function `x/(1+|x|)` at x=0.5 gives 0.333, whereas the dry is 0.5 — only a 16% difference.

At v > 0.6 it switches to FX_FOLD which applies pre-gain `g = 1 + v*4`. At v=0.7: `g = 3.8`, which creates very obvious folded harmonics. This is the perceived "doesn't engage until 50%" — the SAT mode is too gentle from 0 to 0.59.

**Fix:** Lower the Fold threshold from 0.6 to 0.3, so the aggressive distortion starts earlier, OR add a pre-gain factor to the SAT drive so low DRIVE values are more audible:
```c
/* Option A: lower fold threshold */
h->drive_mode = (v > 0.3f) ? FX_FOLD : FX_SAT;

/* Option B: scale drive_amt to amplify the SAT effect at low values */
/* Apply a power curve: drive_amt_eff = v^0.5 (square root pushes more into SAT) */
float drive_eff = sqrtf(v);
fx_config(&h->drive_fx, h->drive_mode, drive_eff);
```

Option A is simpler and more predictable. With Fold starting at 30%, the character change is audible earlier.

**CRUSH root cause:** The default `[PKI_HRD_CRUSH]=0.5f` maps to `bits = 16 - 0.5*12 = 10`, `levels=1024`. 10-bit quantization on a kick that's already attenuated by the amplitude envelope is nearly inaudible. The user likely set CRUSH to 0.5 (default) and heard nothing different.

The mapping direction is correct for HRD (higher v = fewer bits = more crush): `bits = 16 - v*12`. But the default starting at 10-bit (subtle) is wrong — the default should be clean (v=0 = 16-bit = transparent). The user's complaint is "CRUSH does nothing" which means at v=0.5 it should be more audible.

**Fix for CRUSH:** Steepen the range so v=0.5 is more aggressive:
```c
/* Fixed range: v=0 → 16-bit (clean); v=1 → 2-bit (extreme) */
float bits = 16.0f - v * (16.0f - 2.0f);   /* was 16-4, now 16-2 */
```
Also change the default to `[PKI_HRD_CRUSH]=0.0f` so CRUSH starts transparent and the user explicitly engages it.

---

### Bug #17 / #33 — Soft Clipper Sounds Bad

**Root cause confirmed** in `src/dsp.c` render_block lines 563–565:

```c
if (inst->clip_on) {
    sl = sl / (1.0f + (sl < 0.0f ? -sl : sl));
    sr = sr / (1.0f + (sr < 0.0f ? -sr : sr));
}
```

The formula `x/(1+|x|)` is a compressive waveshaper, NOT a transparent limiter. It attenuates ALL signal levels including those well below clipping:
- At amplitude 0.1: 9% attenuation
- At amplitude 0.5: 33% attenuation  
- At amplitude 0.7: 41% attenuation
- At amplitude 1.0: 50% attenuation

The default `[GKI_CLIP]=1.0f` means clip is ON by default. Every kick and groove hit is run through this lossy compressor, making the output quieter and harmonically distorted even at safe signal levels.

The user correctly observes "everything is better with it off."

**Fix (immediate):** Change the default to OFF:
```c
/* In params.c g_global_defaults: */
[GKI_CLIP]=0.0f,   /* was 1.0f — soft clip OFF by default */
```

**Fix (algorithm improvement, Phase E4 scope):** Replace with a proper peak limiter that is transparent below a threshold T:
```c
/* Simple hard-limiter (bounded, transparent below 1.0): */
if (fabsf(sl) > 1.0f) sl = (sl > 0.0f) ? 1.0f : -1.0f;
```
This is equivalent to the existing `omega_to_i16` int16 clamp but at float stage. For Phase E1, just turning it off by default is sufficient (matches the Phase E1 success criterion SC-7: "either fixed to sound good or its default is OFF").

---

## Files to Modify

| File | Bug(s) | Change Summary |
|------|--------|----------------|
| `src/models/fm2.c` | #1, #2 | Fix FX_TYPE enum dispatch (remove 0..1 clamp for fx_type). Initialize `trs_tone_g` defensively. |
| `src/models/fm4.c` | #1 | Fix FX_TYPE enum dispatch (identical pattern) |
| `src/models/wtr.c` | #1 | Fix FX_TYPE enum dispatch |
| `src/models/phy.c` | #1, #5 | Fix FX_TYPE enum dispatch. Fix PHY CURVE sweep_mult formula. |
| `src/models/hrd.c` | #1, #7 | Fix FX_TYPE enum dispatch. Lower DRIVE fold threshold to 0.3. Steepen CRUSH range. |
| `src/models/dig.c` | #1, #6 | Fix FX_TYPE enum dispatch. Invert BIT DEPTH direction (14 - v*8). |
| `src/models/trs.c` | #1 | Fix FX_TYPE enum dispatch |
| `src/models/ana.c` | #1 | Fix FX_TYPE enum dispatch |
| `src/models/usr.c` | #1 | Fix FX_TYPE enum dispatch |
| `src/params.c` | #3, #17 | Change `[PKI_HRD_CRUSH]` default to 0.0f. Change `[GKI_CLIP]` default to 0.0f. Optionally raise TAPS tap gain defaults. |
| `src/groove.c` | #3 | (Optional) Increase tap output gain formula to level-match the kick at default vol. |
| `tests/test_fx.c` | #1 | Update test assertions for corrected FX mode dispatch. |

---

## Risk Assessment

### Low Risk (isolated math changes)
- **Bug #6 (DIG BIT DEPTH inversion):** Single-line change in one place (`dig.c` set_param). No cross-file effects. Test: `test_params.c` DIG BIT DEPTH responsiveness assertion should already cover this.
- **Bug #17 (SOFT CLIP default OFF):** One value in `params.c` `g_global_defaults`. No DSP logic change. Test: `test_perf.c` clip toggle test stays valid.
- **Bug #5 (PHY CURVE):** Change `sweep_mult` formula in `phy_trigger`. Isolated to PHY model. Test: `test_params.c` PHY CURVE responsiveness. Side effect: PHY default voicing at CURVE=0 now produces no pitch sweep — verify it still sounds like a kick (thud not a tone).

### Medium Risk (affects all models)
- **Bug #1 (FX chain dispatch):** Must be fixed in 9 model files. The pattern is identical in each. Risk: if any model stores `fx_type` as a normalized 0..1 value internally for some other purpose (unlikely — grepping confirms it's only used for the `* 4.0f` conversion), changing the storage semantics breaks it. Also update `test_fx.c` to test modes 0–4 directly.
- **Bug #7 (HRD DRIVE/CRUSH):** Changing the fold threshold and CRUSH range changes HRD's character. The test_params battery tests RMS responsiveness — these changes make DRIVE more aggressive sooner and CRUSH more audible at mid-knob, which will PASS the responsiveness test more easily. But verify HRD still sounds like a kick and not just noise.

### Higher Risk (interacting bugs)
- **Bugs #3 and #4 (TAPS gain + duck):** The TAPS gain issue could be addressed by changing `tap_decay` or adding a gain multiplier, but the feedback loop accumulation means raising gain could also over-saturate the ring. The feedback saturation formula `x/(1+0.25|x|)` is gentle — increasing it to `x/(1+0.5|x|)` would be a more aggressive soft limit, preventing accumulation. This change is in `groove.c groove_tick` and is isolated to the TAPS path.

### No Code Change Needed
- **Bug #4 (duck):** The duck logic is correct. After Bug #3 (TAPS gain) is improved and the user can hear the groove, duck will become audible. No code change to the duck itself is needed unless on-device retesting confirms a real issue.
- **Bug #2 (FM2 ATTACK/TRS TNE):** After Bug #1 fix, these become audible. The only defensive code change is a guard on `trs_tone_g > 0` which is optional.

---

## Implementation Order

**Do first (isolated, high impact):**
1. **Bug #1 (FX chain dispatch)** — highest impact, fixes cascading bugs. Change the `fx_type` set_param handling in all 9 model files. Remove the `* 4.0f` scaling from render loops (change `(int)(fm->fx_type * 4.0f + 0.5f)` to `(int)fm->fx_type`). This is a mechanical search-and-replace across 9 files.

2. **Bug #17 (soft clip default OFF)** — one-line change in `params.c`. Zero risk.

**Do second (independent scaling fixes):**
3. **Bug #6 (DIG BIT DEPTH direction)** — one-line inversion in `dig.c`.

4. **Bug #5 (PHY CURVE sweep_mult)** — one-line formula change in `phy.c`.

5. **Bug #7 (HRD DRIVE + CRUSH)** — two changes in `hrd.c`: lower fold threshold, steepen crush range. Also change `[PKI_HRD_CRUSH]` default in `params.c`.

**Do third (gain staging):**
6. **Bug #3 (TAPS gain staging)** — adjust tap output scaling in `groove.c` or `groove_init`. Requires auditioning on-device to confirm the correct target level.

**Verify last:**
7. **Bugs #2 and #4 (FM2 transients + duck)** — re-test on-device after fixes 1 and 6. If still inaudible, apply the defensive `trs_tone_g` guard for #2.

---

## Project Constraints Honored

From `CLAUDE.md`:
- All fixes are C11, no C++ or STL
- No malloc/free in the audio thread — all fixes are to `set_param` (control rate) or state struct initialization
- No new transcendentals in `render_block` — all fixes are algebraic
- Target: Linux ARM64, 44.1kHz fixed, 128-frame blocks — no platform-specific code changes needed

---

## Open Questions

1. **Bug #3 exact gain target:** How loud should TAPS be relative to the kick at default settings? The feedback says "level-match the kick." The kick peak is ~0.6–0.9 at velocity 1.0. The TAPS tap sum at default is ~0.85x the ring content, which at grv_vol=1.0 and typical ring accumulation may be 0.3–0.5 amplitude. Needs on-device calibration.

2. **PHY on-device verification after CURVE fix:** With `sweep_mult = 1 + curve*2`, PHY at CURVE=0 produces no pitch drop (purely modal resonance at excitation freq). This is technically correct (a pure struck drum) but may sound thin. Consider `sweep_mult = 1 + 0.3 + curve*1.7` (always a small sweep, more at higher curve) for a more musical minimum.

3. **FX_TYPE enum: does `fx_config` need to be called with the correct mode at init?** After the dispatch fix, `fx_config` will be called with mode 0–4. Verify `g_diode_lut_ready` initialization: it's a static, initialized on first call to `fx_config`. If the first call ever goes to `FX_CRUSH` (mode 4) before `FX_DIODE`, the LUT is never built. However, `FX_DIODE` is mode 0 = the default, so the default `fx_config(&fm->fx, 0, 0.0f)` call in `omega_prime_model` will build the LUT on first prime. This is fine.

4. **HRD DRIVE default value:** The default `[PKI_HRD_DRIVE]=0.5f` with the proposed fold threshold change (0.3) means HRD starts with FX_FOLD at default. The user will hear aggressive folded distortion on first load. Consider lowering the default to 0.2 to start in gentle SAT territory, or 0.0 for transparent default.
