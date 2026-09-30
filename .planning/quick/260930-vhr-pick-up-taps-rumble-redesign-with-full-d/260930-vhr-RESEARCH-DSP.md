# TAPS Groove Redesign — DSP Research

**Researched:** 2026-09-30
**Domain:** Multitap / feedback delay DSP + techno rumble sound design (C11, RT-safe, 44.1 kHz, aarch64)
**Confidence:** HIGH on DSP mechanics and root-cause diagnosis; MEDIUM on exact "genre-correct" numeric ranges (production practice varies).
**Scope:** Research + concrete implementable redesign only. Does NOT modify `src/` or `tests/`.

---

## Summary

The current TAPS voice is not "bit-crushed" from a bitcrusher — it is bit-crushed from **three stacked gain/nonlinearity errors** that a grep audit of `src/groove.c` confirms:

1. **Per-tap `pow` decay is a loudness bug, not a length control.** `tap_decay[t] = pow(0.3+0.65*LENGTH, t+1)` raises `base` to the power `t+1`. At LENGTH=0.5, `base=0.625`, so tap weights are `0.625, 0.39, 0.24, 0.15` — tap 4 is **~12 dB quieter** than tap 1. The user's stated requirement is EQUAL-level taps at LENGTH max. This curve can never produce that (max `base=0.95` still gives `0.95^4=0.81`, and it also can't hit clean copies because feedback never reaches 0). This is the single most important thing to delete.
2. **A fixed `x3.0` makeup slammed into the saturator + int16 clamp.** The 4-tap weighted sum is multiplied by a hardcoded 3.0 (`gl *= 3.0f`), then the summed dry+feedback ring write goes through `x/(1+0.25|x|)`. With feedback recirculating and taps summing, the pre-clamp signal routinely exceeds 1.0 and gets clamped/saturated on nearly every sample → the crunchy, distorted, yet paradoxically "quiet-feeling" (heavily compressed) character the user reports.
3. **A buried fixed 1-pole LP at ~1.5 kHz inside the feedback loop** (`FB_LP_A=0.20`) darkens the tone with a constant the user can't reach. The user explicitly wants COLOR to be the darkness control, not a hidden constant.

**Primary recommendation:** Replace the whole TAPS engine with **one 16th-note feedback delay line read at 4 fractional tap points**, where a single **bidirectional LENGTH knob** cross-fades between two regimes: (right) *feedback = 0, all four taps equal-level → exact clean kick copies on every 16th*, and (left) *feedback high + in-loop diffusion + in-loop damping → smeared resonant drone*. Remove all per-tap `pow` decay, remove the `x3.0` makeup (replace with equal-power gain compensation), and move the loop LP onto the COLOR knob. Add a fractional (linear-interp) tap read with a slewed delay length so BPM changes don't click. Keep the existing Schroeder reverb but make its routing **bidirectional pre/post** via a MIX knob with a center-off deadzone. This is cheap (~30–40 float ops/sample), fits the existing `groove_state_t` with < 30 KB of new RAM, and every acceptance criterion below is unit-testable in the existing `tests/` harness.

---

## A. Multitap / Feedback Delay DSP — the LENGTH morph

### A.1 The core insight: one delay line, N taps + feedback are the SAME line

The current code already uses one power-of-two ring (`buf_l/buf_r`, 131072 frames). Keep that. The redesign reads **4 taps** at `k · samples_per_16th` behind the write head (k=1..4), and feeds back **one tap** (at `1 · samples_per_16th`) into the write head. Taps and feedback share the ring; that is correct and standard. The subtlety the user's question (E) raises — *"tap n is also fed back, describe how taps + feedback interact and double-count energy"* — is real and is answered by the LENGTH design below: **feedback and tap-weight are driven by ONE knob in opposition**, so at the "clean copies" end feedback is exactly 0 (no double counting), and at the "drone" end the taps matter less because the feedback drone dominates.

**Signal identity that anchors the whole design (right extreme):**
With feedback `fb=0`, the ring holds exactly the kick history. Reading tap k at `k·S` behind the write head (S = samples_per_16th) returns `kick[n − k·S]`. So

```
out[n] = kick[n − S] + kick[n − 2S] + kick[n − 3S] + kick[n − 4S]      (all taps = 1.0)
```

That is *sample-exactly* the same kick placed on the next four 16th notes. This is the acceptance-testable "clean copies" requirement. (HIGH confidence — it is arithmetic.)

### A.2 The LENGTH morph curve (the heart of the redesign)

LENGTH is `v ∈ [0,1]`, control-rate. Define it so **v=1 → clean equal taps, v=0 → smeared drone**, with equal-perceived-loudness across the sweep. Chosen alternative among the four the brief listed: **(ii) tap-weight vs feedback-gain as opposing functions of one knob, plus (iii) in-loop diffusion + damping for smear, and (iv) feedback→0 at the right extreme.** This is simpler and cheaper than crossfading two parallel delay networks (option i) and gives a continuous, musical morph.

```c
/* control-rate, in groove_set_length(g, v):  v in [0,1] */
/* RIGHT (v=1): distinct clean copies.  LEFT (v=0): smeared resonant drone. */

float smear = 1.0f - v;                 /* 1 at left, 0 at right */

/* 1) FEEDBACK: 0 at right (clean copies), high but stable at left.
 *    Cap strictly < 1 AFTER the in-loop saturator's worst-case gain (see A.4).
 *    0.85 max keeps a long resonant tail that still decays. */
g->fb_amount = 0.85f * smear;           /* 0 .. 0.85 */

/* 2) TAP WEIGHTS: EQUAL across all 4 taps (NO per-tap pow decay).
 *    A single global tap trim keeps loudness ~constant as feedback rises:
 *    when feedback adds energy (left), pull the direct taps down a touch. */
float tap_trim = 0.6f + 0.4f * v;       /* 0.6 (drone) .. 1.0 (clean) */
for (int t = 0; t < 4; t++) g->tap_gain[t] = tap_trim;   /* times user TAP1..4 */

/* 3) IN-LOOP DIFFUSION amount: allpass smear only when we want drone. */
g->diffuse_amt = smear;                 /* 0 clean .. 1 fully diffused */
```

Note: **per-tap `tap_decay[]` is deleted entirely.** The per-tap knobs `TAP1..4` remain as independent user level trims (0..1) multiplied by the single global `tap_gain` — so the user can still shape the rhythmic pattern (mute tap 3, etc.), but the *default at LENGTH=1 with all taps up is equal level*, exactly as required.

Confidence: HIGH on structure. The specific constants (0.85, 0.6/0.4 trim) are MEDIUM — tune by ear/RMS test; see acceptance criteria for the constant-loudness gate.

### A.3 Equal-power / loudness compensation (kills "too quiet")

The user reports TAPS is "too quiet — must crank VOL to 100 + add drive." Root cause is #2 above: the `x3.0` makeup collides with the saturator so peaks clamp while RMS stays low (heavy limiting = quiet + crunchy). Fix:

- **Delete `gl *= 3.0f`.** With 4 equal taps at unity and no feedback the summed peak is already up to ~4× a single kick copy on transient alignment; you need *attenuation*, not 3× gain.
- **Normalize the 4-tap sum by an active-tap compensation.** Sum of tap gains `G = Σ tap_gain[t]·TAPk`. Divide the tap-summed output by `sqrt(max(G,1))` (equal-power) rather than by `G` (equal-amplitude) — equal-power keeps perceived loudness roughly flat whether 1 or 4 taps are active without going dull when taps are removed. (MEDIUM — equal-power vs equal-amplitude is a taste call; equal-power is the standard crossfade/summing choice, see Delay-Line Interpolation ref.)
- **Feedback branch:** the drone end adds energy; the `tap_trim` in A.2 plus the in-loop damping (A.5) already compensate. Verify with the constant-loudness acceptance test.

Result: no makeup gain hitting a clamp, so no "bit-crushed" artifact, and VOL=0.8 level-matches the kick without DRIVE.

### A.4 Stability, DC, denormals

- **Feedback stability:** total loop gain must stay < 1 *after* any in-loop nonlinearity. Keep `fb_amount ≤ 0.85`. If you keep a gentle in-loop saturator, note `x/(1+a|x|)` has **unity slope at x=0** (gain 1 near zero), so it does NOT reduce small-signal loop gain — a self-oscillating tail is possible if `fb_amount·(loop filter gain) ≥ 1`. With a 1-pole LP in the loop (DC gain 1) and `fb ≤ 0.85`, small-signal loop gain ≤ 0.85 < 1 → stable. (HIGH.)
- **DC buildup:** a pure feedback delay accumulates DC/sub-30 Hz mud (the "speaker-flapping" the rumble docs warn about). Add a **one-pole HP at ~25–35 Hz inside the loop** (see C). This is both a genre requirement and a stability aid. (HIGH — matches rumble production docs: "Cut frequencies below 30Hz with a steep High-Pass Filter.")
- **Denormals:** FPCR FTZ/DAZ is already set per CLAUDE.md (`create_instance` sets the FZ bit), so decaying feedback tails flush to zero without the ~100× denormal penalty. No per-sample `+1e-20` dither needed. (HIGH — given the documented FPCR setup.) Belt-and-suspenders: the in-loop HP and LP states also naturally avoid sustained denormals.

### A.5 Adding smear: in-loop diffusion (allpass) + damping

To make the left extreme "smeared, resonant, diffuse" rather than a clean single echo, insert a **short allpass (or two) inside the feedback path**, scaled by `diffuse_amt`:

```c
/* 1-sample-cheap Schroeder allpass, g_ap ~0.5, short delay (e.g. 113, 241 frames).
 * Blend by diffuse_amt so RIGHT extreme = bypass (clean), LEFT = full smear. */
float ap_in  = fb_signal;
float ap_out = -g_ap*ap_in + apbuf[api];
apbuf[api]   = ap_in + g_ap*ap_out;
fb_signal    = ap_in + diffuse_amt * (ap_out - ap_in);   /* crossfade dry->diffused */
```

Two mutually-prime short allpasses (e.g. 113 and 241 frames, ~2.6 ms / 5.5 ms) give a convincing smear for ~8 float ops/sample and < 1.5 KB RAM. Optionally add a slow (~0.1–0.3 Hz) modulation of one allpass delay for a living drone — but that needs fractional reads and is optional. (MEDIUM — allpass diffusion in a feedback loop is textbook; the exact lengths are tunable.)

**In-loop damping = the COLOR-linked LP** (see C). Do NOT hardcode it.

### A.6 Fractional tap reads + slewed delay length (kills BPM zipper/clicks)

Current code reads integer `k·samples_per_16th` behind the write head and jumps `samples_per_16th` whenever BPM re-locks (control-rate). On a live BPM change (or DAW tempo automation) that jump moves every tap by whole samples at once → a click, and integer rounding means the taps drift off the true 16th grid. Fix with **linear-interpolated fractional reads** + a **slewed delay length**:

```c
/* control-rate target (float), slewed per-sample toward it: */
g->spq_target = (60.0f / bpm) * OMEGA_SR / 4.0f;         /* fractional samples/16th */
/* per-sample: one-pole slew, ~20–50 ms time constant */
g->spq += (g->spq_target - g->spq) * SPQ_SLEW;           /* SPQ_SLEW ~ 0.0005 */

/* fractional read k*spq behind write head: */
float d   = (float)k * g->spq;
float rp  = (float)g->write_pos - d;
int   i0  = ((int)rp) & GRV_DELAY_MASK;
int   i1  = (i0 + 1)  & GRV_DELAY_MASK;   /* note: read is BEHIND, so +1 is toward newer */
float fr  = rp - floorf(rp);
float s   = buf[i0] + fr * (buf[i1] - buf[i0]);          /* linear interp */
```

Use **linear interpolation for the taps** (random-access, robust under time variation — the reference explicitly notes linear "sounds very good when signal bandwidth is small compared with half the sampling rate," which is exactly true for sub-200 Hz rumble). Use **first-order allpass interpolation only if you later modulate the in-loop allpass**, and be aware allpass interp is recursive and can click on fast changes — so for the main taps, stick with linear + slew. (HIGH — directly supported by dsprelated Delay-Line Interpolation.)

The slew means BPM changes glide instead of jumping. Time-constant ~30 ms is inaudible as pitch-bend on a rumble yet kills the click. (MEDIUM on exact constant.)

### A.7 Rejected alternatives (A)

- **Crossfade two parallel networks (clean multitap ⨉ feedback comb).** Works, but doubles the ring reads and RAM and needs a click-free crossfader. The single-line opposing-knob design (A.2) achieves the same audible morph for half the cost. Rejected on CPU/RAM.
- **Keeping per-tap `pow` decay "but gentler."** Rejected outright — it structurally cannot produce equal-level taps and it is the primary user complaint.
- **Bucket-brigade / all-pass-only diffusor for the taps.** Overkill; the taps must stay *discrete* at the clean end, which an allpass chain destroys.

---

## B. Techno Rumble Sound Design (genre-correct targets)

### B.1 The canonical rumble recipe (from production sources + the project's own Context doc)

The rumble technique is: **kick → smear (reverb and/or 16th delay) → mono → saturate → steep LP (and HP) → sidechain duck.** The project's `Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` already encodes this exactly (5 stages), and it matches independent producer sources. (HIGH — two independent agreeing sources.)

Concrete numeric targets (MEDIUM — production practice varies, but these are representative and internally consistent):

| Parameter | Genre target | Maps to Omega control |
|---|---|---|
| Rumble band (sub layer) | HP ~30 Hz, LP ~120 Hz (steep) | in-loop HP (fixed ~30 Hz) + COLOR LP |
| Rumble band (mid/percussive layer) | 100–250 Hz up to ~600–800 Hz | COLOR opened up |
| Reverb decay | ~1.0–4.0 s, high/100% wet | RVDECAY → feedback→RT60 (D) |
| Reverb size/diffusion | size ~50%, diffusion ~80% | fixed in the cheap reverb |
| Saturation | "~10 dB drive," colour ~50% | DRIVE knob (already exists) |
| Sidechain duck | duck 4–8 dB, release ~80–150 ms | **deferred to Phase D** (per REFINEMENT-FEEDBACK) |
| 16th delay | 1/16 (and dotted 3/16) taps | the tap grid (this redesign) |

Sources: `Context/03_...md`; mastrng.com techno-rumble guide; theproducerschool / studiobrootle. The Bohm/Groove spec (`Context/02_...md §4`) confirms the intended control set: TAP1–4 (16th subdivisions), LENGTH (staccato tap ↔ long sub drone), COLOR (timbre/cutoff), VOL — the redesign preserves all four and makes LENGTH behave as the spec's "staccato tappings vs long sub-bass drones" describes.

### B.2 Pre-reverb-into-taps vs post-reverb (the bidirectional MIX knob)

The user's idea — reverb *before* the tap ring for a "pre-smear" — is a real and well-known technique. Chain-order sources confirm: **delay-before-reverb** is the conventional/"natural" order (each echo gets its own tail), while **reverb-before-delay** produces the "washed-out, larger soundscape, ambient" texture — exactly the pre-smeared rumble the user wants at the LEFT of the MIX knob. (HIGH that both orders are valid and sound distinct; MEDIUM that "reverb-before" is *the* rumble idiom — it is *a* recognized idiom.)

**Feasibility of pre-reverb-into-taps: YES, with two guardrails.**

1. **Level buildup / feedback-tail × reverb-tail interaction.** If a long reverb tail is fed into a high-feedback tap ring, energy compounds and can run away. Guardrails: (a) the tap-ring feedback is already capped < 1 and HP/LP-damped; (b) when MIX is in "pre" mode, **attenuate the reverb send into the ring** (e.g. reverb-into-ring gain ≤ 0.5) and rely on the ring's own damping; (c) put the reverb *pre*-tap but *post*-input-HP so you never feed sub-DC into the loop. Net: stable if reverb-send ≤ ~0.5 and `fb ≤ 0.85`. (MEDIUM — needs the 30 s stability acceptance test to confirm.)
2. **The ring's dry path.** In "pre" mode you still want the dry kick transient to punch. Recommended: the **dry kick always goes into the ring** (so clean taps still exist), and the reverb is *added* to the ring input scaled by the pre-amount — i.e. ring input = `kick + pre_amt·reverb(kick)`. This preserves rhythmic tap definition while adding the smear, rather than replacing the kick with a mush. (Recommendation, MEDIUM.)

**One-knob bidirectional REVERB MIX design (reuses existing `PK_GRV_RVMIX`):**

```
knob v in [0,1], center = 0.5:
  LEFT  (v < 0.5): PRE.  pre_amt  = (0.5 - v) * 2       // 0..1
                         reverb runs on kick, its output * pre_amt
                         is added to the tap-ring INPUT (kick + pre_amt*rev).
  CENTER(v ≈ 0.5): reverb effectively off (deadzone ±0.03 to avoid chatter).
  RIGHT (v > 0.5): POST. post_amt = (v - 0.5) * 2       // 0..1
                         reverb runs on the tap-ring OUTPUT, mixed post_amt wet.
```

Only ONE reverb instance is needed — its *input tap point* (kick vs ring-output) and *output mix point* switch with the knob sign. That keeps CPU/RAM flat. A small crossfade or the center deadzone prevents a click when crossing center. (Recommendation, HIGH feasibility / MEDIUM on exact deadzone width.)

---

## C. Filter Topology (COLOR + in-loop HP)

The codebase already ships a TPT 1-pole LP (`tpt1_lp` in `dsp_primitives.h`) and CLAUDE.md's stack decision is **TPT SVF** for swept filters. Recommendations:

1. **COLOR = TPT SVF 2-pole LP**, not the current 1-pole. A 2-pole (12 dB/oct) gives the "dial in darkness" authority the user wants; the current 1-pole (6 dB/oct) is too gentle to make a rumble genuinely dark. The rumble docs actually call for 24 dB/oct — you can **cascade two TPT 2-pole SVF LPs** if the user wants 4-pole, but start with one 2-pole and expose it as COLOR (30 Hz–20 kHz log sweep, which the code already computes). CLAUDE.md explicitly blesses "cascade two TPT stages" for a 2/4-pole toggle. (HIGH — matches CLAUDE.md filter decision.)
   - *Note:* `dsp_primitives.h` header comment says "Phase D upgrades to full SVF." A 2-pole SVF is a ~15-line addition per CLAUDE.md; if a full SVF primitive isn't landed yet, cascading two existing `tpt1_lp` calls is an acceptable interim 2-pole (two 1-poles = 12 dB/oct, no resonance). (HIGH.)

2. **In-loop HP at ~25–35 Hz.** Place a fixed one-pole HP (`hp = x − tpt1_lp(x, g@30Hz)`) **inside the feedback path** and on the ring input. This removes DC/sub buildup (stability + genre correctness) and is *not* user-exposed — it is a structural sub-cleaner, not a "buried tone constant." This is the legitimate fixed filter (unlike the illegitimate 1.5 kHz `FB_LP_A`). (HIGH.)

3. **Signal-flow placement.** Put the *damping LP* (COLOR-linked or a fraction of COLOR) **inside the feedback loop** so successive recirculations get progressively darker (natural rumble decay), and put the *COLOR LP* again (or the same coefficient) on the **final output** so the overall brightness tracks the knob. Simplest correct arrangement: one COLOR-controlled 2-pole LP on the **loop** (darkens the drone as it decays) + the final output LP is the same COLOR. The old fixed 1.5 kHz loop LP is **deleted**. (Recommendation, MEDIUM on whether to share one coefficient or split; sharing is simpler and passes the "COLOR controls darkness" requirement.)

---

## D. Reverb: cheap, good, long, dark

### D.1 Recommendation: KEEP and lightly extend the existing Schroeder reverb

On-device feedback (`ONDEVICE_FEEDBACK_v1_1.md`: "Reverb sounds good — keep it") and REFINEMENT-FEEDBACK both say the current 2-comb + 1-allpass Schroeder **already sounds good**. Do not replace it with an FDN for v1. The right move is:

- **Reuse the existing `rv_comb1[1557]`, `rv_comb2[1617]`, `rv_ap[556]`** (already mutually-prime-ish, already damped, already in `groove_state_t`).
- **Map RVDECAY to a proper RT60** using the standard Jot feedback-gain formula so a long dark warehouse tail is reachable:

```
g_comb = 10^( -3 · M / (RT60 · SR) )         // M = comb length in samples, SR = 44100
```

For `M=1557`, SR=44100: RT60=1.0 s → g≈0.769; RT60=2.0 s → g≈0.877; RT60=4.0 s → g≈0.936. Map RVDECAY 0..1 → RT60 ~0.3..4.0 s and compute `rv_fb` from the formula (currently it's a linear 0.5..0.99 which is fine but the RT60 mapping is more musical and matches the genre "1–4 s" target). Cap `g_comb ≤ 0.97` for stability. (HIGH — Jot RT60 formula is textbook, confirmed by CCRMA FDN delay-length page and standard reverb literature.)

- **Darkness:** the existing `rv_damp` (comb LP) already provides dark tails; drive it from RVTONE. For genuinely dark rumble reverb, allow `rv_damp` up to ~0.85 (heavy HF loss). Add a fixed HP ~30 Hz on the reverb output too. (MEDIUM.)
- **Pre-delay:** add a short pre-delay (e.g. 20–60 ms, one small ring) before the combs for the classic "space before the tail." Optional, ~1 KB RAM. Genre docs mention pre-delay for separation. (MEDIUM — nice-to-have.)
- **Stereo out from mono in:** read the two combs with a slight L/R decorrelation (e.g. swap comb1/comb2 dominance per channel, or add a second short allpass on one side) to widen. Cheap. (MEDIUM.)

### D.2 Cost / RAM

| Item | Per-sample cost | RAM |
|---|---|---|
| Existing Schroeder (2 comb + 1 AP) | ~12–16 float ops | (1557+1617+556)·4 B ≈ **15 KB** (already allocated) |
| + pre-delay (optional, ~2600 frames) | ~2 ops | ~10 KB |
| + stereo decorrelation AP (optional) | ~6 ops | ~1 KB |

### D.3 Why NOT an FDN (rejected for v1)

A 4-line FDN with Householder mixing + damping is the "better" reverb (Signalsmith: 8 channels, Householder in the feedback loop because "we don't want too much mixing... can lock the delays together"; delay lines mutually prime per Stanford CCRMA). But: (a) the current reverb already sounds good per on-device test; (b) an FDN is ~2–4× the ops and more RAM; (c) the CPU budget (10–15% total, many instances) favors the cheaper Schroeder. **Revisit an FDN only if the Schroeder proves too metallic for long 4 s tails.** If you do, use 4 lines, mutually-prime lengths in the ~1200–2400 sample range, a normalized Householder matrix `H = I − (2/N)·1·1ᵀ` (one dot product + N subtracts, no per-element multiply), and a one-pole damping LP per line. (Signalsmith / CCRMA, HIGH on the design if pursued; MEDIUM that it's needed.)

---

## E. Tempo Sync (answered inline in A.6)

- Tap positions must be **fractional** (`k · spq`, spq a float) and read with linear interpolation, and `spq` must be **slewed** toward its BPM-derived target, not jumped. This fixes both the off-grid integer rounding and the BPM-change click. (HIGH.)
- **Single feedback delay of 1×16th vs reading 4 taps:** you need BOTH. The **4 discrete taps** give the rhythmic "kick on every 16th" definition (required for the clean-copies extreme); the **1×16th feedback** gives the sustained drone. They coexist on one ring. Energy double-counting is avoided because feedback is 0 at the clean-tap extreme and the taps are trimmed down (equal-power) as feedback rises (A.2/A.3). The interaction is: at intermediate LENGTH, tap k hears both the original kick AND the decaying feedback of earlier kicks → this is *desirable* (it's what makes the mid-LENGTH sound "rolling"), and the `tap_trim` + in-loop damping keep it bounded. (HIGH on mechanism.)

---

## F. RECOMMENDED REDESIGN

### F.1 Signal flow (ASCII)

```
                         kick_l/r (this sample)
                              │
                   ┌──────────┴───────────┐
                   │  in-loop HP ~30 Hz    │  (structural sub-cleaner, fixed)
                   └──────────┬───────────┘
                              │
   REVERB MIX "PRE" (v<0.5):  + pre_amt · reverb(kick)   ──┐
                              │                            │ (reverb reused, D)
                     ring input = kick + fb_signal + pre   │
                              │                            │
                    ┌─────────▼─────────┐                  │
                    │  write buf[wp]     │                  │
                    │  power-of-2 ring   │                  │
                    └─────────┬─────────┘                  │
             ┌────────────────┼───────────────┐            │
        tap1 (1·spq)   tap2 (2·spq) ... tap4 (4·spq)       │  fractional
             │  ×TAP1        │ ×TAP2        │ ×TAP4          │  linear-interp
             └──────┬────────┴──────────────┘               │  (A.6)
                    │  Σ · tap_trim / sqrt(active gain)      │  (equal-power, A.3)
                    │                                        │
      feedback tap (1·spq) ─► in-loop allpass diffuse ─► in-loop LP(COLOR) ─► ×fb_amount ─┐
                    │                                                                     │
                    │ (fb_signal, fed back to ring input above) ◄─────────────────────────┘
                    ▼
            tap sum (gl, gr)
                    │
          ┌─────────▼─────────┐
          │ COLOR 2-pole LP    │  (SVF or 2×tpt1)  — the darkness control (C)
          └─────────┬─────────┘
                    │
          ┌─────────▼─────────┐
          │ DRIVE (existing)   │  saturation + auto-makeup
          └─────────┬─────────┘
                    │
   REVERB MIX "POST" (v>0.5): + post_amt · reverb(this) ──► (same reverb instance, D)
                    │
          ┌─────────▼─────────┐
          │ LFO tremolo (exist)│
          └─────────┬─────────┘
                    │
             MONO sum (exist) ─► × VOL ─► out_gl/out_gr
```

### F.2 Control mapping

| Control | Range / curve | Effect |
|---|---|---|
| **LENGTH** | 0..1, bidirectional in meaning (right=clean, left=drone) | v=1: `fb=0`, equal taps → exact clean copies. v=0: `fb=0.85`, full diffusion, damped drone. (A.2) |
| **REVERB MIX** | 0..1, center 0.5 = off (±0.03 deadzone) | <0.5: reverb PRE (into ring input, `pre_amt=(0.5−v)·2`). >0.5: reverb POST (`post_amt=(v−0.5)·2`). (B.2) |
| **COLOR** | 30 Hz–20 kHz log (code already computes) | 2-pole LP on loop + output; the darkness control. (C) |
| **TAP1..4** | 0..1 each | independent per-tap level trim × global `tap_trim`. Equal at defaults. (A.2) |
| **DRIVE** | 0..1 | existing saturation + auto-makeup (keep). |
| **RVDECAY** | 0..1 → RT60 ~0.3–4.0 s | Jot `g=10^(−3M/(RT60·SR))`. (D.1) |
| **RVTONE** | 0..1 → `rv_damp` | dark↔bright reverb tail. (D.1) |
| **LFO SPD / AMT** | existing | tremolo on output (keep). |
| **MONO** | toggle | sub-bass mono sum (keep, genre-correct). |
| **VOL** | 0..1 | master. Should level-match kick at ~0.8 with defaults after fixes. |

### F.3 Gain staging (numbers)

- Ring input: `kick + fb_signal(+pre)`; peak bounded by `fb ≤ 0.85` and in-loop HP/LP. No saturator on the write path required if `fb ≤ 0.85` (small-signal stable); keep a *gentle* limiter `tanh`-ish only as a safety net, NOT in the tone path.
- Tap sum: `Σ(buf·TAPk)·tap_trim / sqrt(max(Σ tap_gain,1))`. With 4 equal unity taps → divide by 2 (`sqrt(4)`), so aligned peaks land near unity, not 4×. **No `x3.0`.**
- Target: with VOL=0.8, LENGTH=1, all taps=1, the clean-copy output peak ≈ the kick peak (±3 dB). Verified by acceptance test F.5.

### F.4 Cost & memory

| Stage | Ops/sample (stereo) | RAM added |
|---|---|---|
| Ring write + 4 fractional taps (linear) | ~24 | 0 (existing ring) |
| Feedback tap + 2 in-loop allpass + in-loop LP + HP | ~20 | ~1.5 KB (allpass bufs) + a few floats |
| COLOR 2-pole (SVF or 2×tpt1) | ~10 | few floats |
| DRIVE / LFO / MONO / VOL (existing) | ~12 | 0 |
| Reverb (existing Schroeder, reused for pre OR post) | ~14 | 0 (existing) + optional ~11 KB pre-delay/stereo |
| **Total** | **~80 ops/sample stereo** (~40/mono-eq) | **< ~15 KB new** (well under the 150 KB/instance budget) |

At 44.1 kHz that is ~3.5 M ops/s per instance — comfortably inside the 10–15% A53 budget even with many instances; the 700 KB+ ring dominates RAM, not the new FX. (MEDIUM on absolute CPU — verify on-device, but the op count is small.)

### F.5 Acceptance criteria (unit-testable in existing `tests/` harness)

Use the existing pattern (`move_plugin_init_v2` → `create_instance` → `set_param` → drive mock transport via `mock_host_advance_beat` → `render_block` → analyze int16). All are RED-until-implemented gates:

1. **Clean-copies exactness (the headline requirement).** Set LENGTH=1, TAP1–4=1, REVERB MIX=0.5 (off), COLOR wide open, DRIVE=0, MONO=0. Trigger ONE kick, no further triggers. Assert the groove output at sample `n` equals (within a tight epsilon, e.g. 1 LSB int16) `kick[n−S] + kick[n−2S] + kick[n−3S] + kick[n−4S]` where `S=samples_per_16th` for the fixed test BPM. (Render the isolated kick separately to get the reference `kick[]`.) This proves equal-level clean copies and the exact-16th placement. (Derived in A.1.)
2. **Equal tap levels.** With the setup above, the RMS of the window around tap 1 (`[S, 2S)`) equals the RMS around tap 4 (`[4S, 5S)`) within ±0.5 dB. Directly refutes the current 12 dB decay bug.
3. **Drone stability (30 s).** LENGTH=0, all taps=1, feed kicks every 4 beats for 30 s of blocks. Assert every output sample `isfinite` and `|x| ≤ 1.0` (peak bound), and that the tail energy does not monotonically grow (energy of last second ≤ energy of a mid second × 1.5). Refutes runaway feedback.
4. **Constant loudness across LENGTH.** Sweep LENGTH 0→1 in 5 steps, same kick pattern; assert output RMS varies by < ±3 dB across the sweep. Refutes "too quiet" and validates the equal-power compensation (A.3).
5. **No bit-crush / no clamp abuse.** With defaults (LENGTH=0.7, taps=0.7, DRIVE=0), assert the fraction of output samples at exactly ±32767 (int16 rail) is < 0.1%. The current build would fail this (the `x3.0`+saturator rails constantly).
6. **COLOR audibly darkens.** Compare spectral-centroid proxy (ZCR, as `test_distinct.c` already computes) at COLOR=0.1 vs COLOR=0.9 with LENGTH=0.5; assert ZCR(0.9) > ZCR(0.1) by a clear margin. Proves COLOR is the darkness tool.
7. **No hidden 1.5 kHz constant.** Structural: assert removing input energy above ~2 kHz is achievable ONLY via COLOR (i.e. at COLOR fully open the output retains HF energy). Practically: ZCR at COLOR=1.0 must exceed a floor that the old fixed `FB_LP_A` would have suppressed.
8. **NaN/Inf guard + peak ≤ 1.0** across every test (already the harness default per `render_driven`).
9. **BPM-change click-free.** Render while stepping the mock BPM 120→130 mid-render; assert no single inter-sample jump exceeds a threshold (e.g. `|out[n]−out[n−1]| < 0.5`) that a whole-sample delay jump would exceed. Validates the slew (A.6).
10. **Pre-reverb stability.** REVERB MIX=0.0 (full pre), LENGTH=0 (max feedback), 30 s: `isfinite` + bounded, same as #3. Validates the pre-reverb×feedback guardrail (B.2).

**Native test approach:** mirror `test_groove.c` / `test_distinct.c`. Add a `test_taps_redesign.c` that (a) captures an isolated-kick reference render (groove VOL=0), then (b) captures the groove render, and asserts the criteria above via RMS-windowing and the existing ZCR proxy. Use `make_mock_host()` for driven-transport tests and `make_mock_host_null_transport()` for the 120-constant fallback. Reuse `omega_to_i16` bounds already asserted in `render_driven`. The malloc-trap (`tests/malloc_trap.c`) must still show ZERO allocations during `render_block` — the redesign adds no new allocations (all buffers stay by-value in `groove_state_t`).

### F.6 State-struct impact (informational, not a code change)

New fields fit inside `groove_state_t` (all by value, one calloc — no new allocation):
- `float spq, spq_target;` (fractional slewed 16th length) — replaces reliance on int `samples_per_16th` for reads.
- `float tap_gain[4];` (or a single `tap_trim`) — replaces `tap_decay[4]`.
- `float diffuse_amt;` and two small allpass buffers `float ap1[241], ap2[113]; int ap1i, ap2i;` (~1.4 KB).
- `float loop_hp_l_s, loop_hp_r_s;` (in-loop HP state).
- Optional pre-delay ring for the reverb (~10 KB) if pre-delay is added.
- Delete `tap_decay[4]` and the `FB_LP_A` usage / the fixed loop LP.

Total new RAM: **< ~15 KB/instance** (or ~1.5 KB without optional pre-delay), vs the existing ~1 MB ring — negligible.

---

## Project Constraints (from CLAUDE.md) — compliance check

| Constraint | Redesign compliance |
|---|---|
| C11, no C++/STL | All pseudocode is plain C, `static inline`-friendly. ✓ |
| Zero malloc/free/IO on audio thread | All new buffers by-value in `groove_state_t`, one calloc. No IO. ✓ |
| No per-sample transcendental | All `pow/tanf/expf` at control-rate (LENGTH curve, COLOR coeff, RT60 gain). Per-sample = mul/add + one `floorf` for interp. ✓ (floorf compiles to a single instruction on aarch64.) |
| float throughout, 44.1 kHz fixed | Yes; `OMEGA_SR` used. ✓ |
| TPT filters for swept | COLOR = TPT SVF/2×tpt1; matches CLAUDE.md filter decision. ✓ |
| Pre-allocated, branch-free ring `& mask` | Ring stays power-of-two `& GRV_DELAY_MASK`; fractional read uses two masked indices. ✓ |
| FPCR FTZ/DAZ set → denormals handled | Relied upon for feedback-tail denormal flush (A.4). ✓ |
| RAM budget (many instances) | < 15 KB new/instance. ✓ |

---

## Open Questions

1. **Exact `fb_amount` cap and `tap_trim` curve** — 0.85 / (0.6+0.4v) are principled starting points; final values are an ear+RMS-test tuning pass (acceptance criteria 3 & 4 are the gate). Confidence MEDIUM.
2. **Share one COLOR coefficient for both loop-LP and output-LP, or split?** Sharing is simpler and passes requirements; splitting (loop darker than output) could sound better for drones. Recommend start shared. MEDIUM.
3. **Pre-delay & stereo decorrelation on the reverb** — nice-to-have, adds ~11 KB. Ship without if RAM/time is tight; the existing reverb already "sounds good." LOW urgency.
4. **Is a 2-pole COLOR enough, or does the user want 4-pole (cascade two)?** Rumble docs say 24 dB/oct. Recommend expose 2-pole first; add a pole-toggle later if requested. MEDIUM.
5. **Whether to keep any in-loop safety saturator at all** — with `fb ≤ 0.85` and HP/LP the loop is small-signal stable, so a saturator is optional. If kept, make it *very* gentle and out of the audible tone path (it was part of the "bit-crushed" problem). MEDIUM.

---

## Sources

### Primary (HIGH)
- `Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` (project) — 5-stage rumble formula, HP<30 Hz, LP<150 Hz, mono sum, saturation, duck timing. Corroborated by external producer sources below.
- `Context/02_OHMFORCE_BOHM_SYSTEM_SPEC.md` (project) — Groove expander control intent: TAP1–4 (16th subdivisions), LENGTH (staccato↔sub drone), COLOR (cutoff), VOL.
- `src/groove.c` / `src/groove.h` / `src/dsp_primitives.h` (project) — confirmed root-cause bugs (per-tap pow, x3.0 makeup, fixed 1.5 kHz loop LP) by direct code read; confirmed available TPT primitive and ring layout.
- [DSPRelated / JOS — Delay-Line Interpolation](https://www.dsprelated.com/freebooks/pasp/Delay_Line_Interpolation.html) — linear vs allpass interpolation, "sounds very good when bandwidth small vs Nyquist" (true for rumble); allpass recursive → clicks on fast change; linear = random access. (fractional tap design, A.6)
- [Stanford CCRMA — Choice of Delay Lengths (JOS, PASP)](https://ccrma.stanford.edu/~jos/pasp/Choice_Delay_Lengths.html) — mutually-prime rule, mode density. (reverb design, D)
- Jot RT60↔feedback-gain formula `g = 10^(−3·M/(RT60·SR))` — standard reverb literature (JOS PASP), applied in D.1.

### Secondary (MEDIUM — cross-verified where possible)
- [Signalsmith Audio — Let's Write A Reverb](https://signalsmith-audio.co.uk/writing/2021/lets-write-a-reverb/) — 8-channel diffuser (20/40/80/160 ms Hadamard) + Householder feedback (100–200 ms), "not too much mixing or delays lock together," ~85% feedback. (FDN rejected-alternative rationale, D.3)
- [mastrng.com — Techno Rumble](https://www.mastrng.com/techno-rumble/) — reverb ~4 s / 100% wet / size 50% / diffusion 80%, LP ~120 Hz, drive ~10 dB, duck 4–8 dB. (genre targets, B.1)
- [The Producer School — Techno Rumble Kicks 2025](https://theproducerschool.com/blogs/featured-blogs/how-to-create-techno-rumble-kicks-for-2025) — reverb→distortion→filter→sidechain recipe. (B.1)
- [Studio Brootle — Techno Rumble Kick rack](https://www.studiobrootle.com/techno-rumble-kick-ableton-rack-mk2/) — rumble chain corroboration. (B.1)
- [Fader & Knob — Reverb Before or After Delay](https://faderandknob.com/blog/reverb-before-or-after-delay-chain-order) & [ADSR — Reverb/Delay order](https://www.adsrsounds.com/mixing-tutorials/reverb-and-delay-in-series-which-comes-first/) — delay-before-reverb = natural; reverb-before-delay = washed-out ambient (the pre-smear the user wants). (B.2)
- [USPTO 5,781,461 — multitap delay line with crossfader](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/5781461) & [Analog Devices — Multi-Tap VC Delay](https://wiki.analog.com/resources/tools-software/sigmastudio/toolbox/basicdsp/multitapvoltagecontrolleddelay) — multitap taps are independent reads of one line; crossfade 10–30 ms for tap transitions. (A.1)

### Project feedback docs (requirements source of truth)
- `.planning/REFINEMENT-FEEDBACK.md` — "bit-crushed & quiet... dry 4-tap echo, needs feedback/resonance"; reverb "sounds good, keep it"; COLOR must actually filter; TAPS Page 2 FX set.
- `.planning/ONDEVICE_FEEDBACK_v1_1.md` — bug #3 (TAPS bit-crushed/quiet), "Reverb sounds good — keep it," filter should be a continuous LP sweep 30 Hz–20 kHz.
- `MEMORY.md / feedback_taps_redesign.md` — bidirectional LENGTH, pre/post reverb, equal-level taps at max, COLOR = darkness tool.

---

## Metadata

**Confidence breakdown:**
- Root-cause diagnosis (why it's bit-crushed/quiet): HIGH — read directly from `src/groove.c`.
- LENGTH morph design (opposing tap/feedback + diffusion): HIGH on structure, MEDIUM on constants.
- Clean-copies exactness (acceptance #1): HIGH — arithmetic.
- Genre numeric targets: MEDIUM — production practice varies; internally consistent across 3 sources + project doc.
- Reverb keep-Schroeder + RT60 mapping: HIGH (formula) / MEDIUM (keep-vs-FDN is a judgment backed by on-device feedback).
- Pre-reverb-into-taps feasibility: MEDIUM — feasible with the stated guardrails; must pass the 30 s stability test.
- Fractional taps + slew: HIGH — standard technique, directly sourced.

**Research date:** 2026-09-30
**Valid until:** ~2026-10-30 (DSP fundamentals are stable; only genre-taste numbers drift).
