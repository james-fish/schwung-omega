---
phase: quick-260930-vhr
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/groove.h
  - src/groove.c
  - src/params.c
  - src/ui.c
  - tests/test_taps_redesign.c
  - tests/test_readback.c
  - Makefile
autonomous: false
requirements:
  - TAPS-LENGTH-BIDIR
  - TAPS-EQUAL-TAPS
  - TAPS-CLEAN-COPIES
  - TAPS-DRONE-STABLE
  - TAPS-EQUAL-POWER
  - TAPS-COLOR-DARKNESS
  - TAPS-LOOP-HP
  - TAPS-FRACTIONAL-SLEW
  - TAPS-REVERB-BIDIR
  - TAPS-NO-BITCRUSH
  - TAPS-RT-SAFE
  - TAPS-GEN-UNCHANGED
user_setup: []

must_haves:
  truths:
    # Each maps a user-intent memo requirement to an observable behaviour.
    - "At LENGTH max with all TAP1-4 up (feedback=0, reverb at center/off, and the post-groove performer chain neutralised for the test: DUCK=0, DJ filter neutral, CLIP off, groove output LP bypassed via GRV_FILTYPE=OFF), the RAW kick is written to the ring, so the TAPS-added output equals the same UNFILTERED kick placed on every 16th note. The invariant is CLEAN COPIES AT EQUAL LEVEL: all 4 taps carry the identical kick waveform at the same amplitude (per-tap amplitude ratio == 1.0 and normalised cross-correlation == 1.0 within epsilon). Because equal-power normalisation divides the 4-tap sum by sqrt(4)=2 at LENGTH=1 with taps=1, the ABSOLUTE per-tap level = grv_vol * tap_trim(=1.0 at LENGTH=1) * 0.5 of the raw kick — the reference must apply this 0.5 scale (times grv_vol) when comparing, OR the gate is defined shape/ratio-exact. This honours the user intent ('sounds exactly like the kick copied on every 16th at max') as the equal-level clean-copy property; the equal-power 0.5 scale is stated explicitly, not hidden. The ~30 Hz HP lives only on the feedback path and does NOT touch the clean taps."  # memo: right extreme = clean tap copies
    - "At LENGTH max, all 4 taps are equal level (no per-tap decay)."                                                                                 # memo: EQUAL LEVEL on all 4 taps
    - "At LENGTH min the TAPS voice is a smeared, resonant, continuous feedback drone that stays bounded and stable for 30 s (the ~30 Hz feedback-path HP + in-loop damping keep DC/sub from accumulating)."  # memo: left extreme = feedback drone
    - "COLOR is the darkness control: sweeping COLOR down audibly darkens the rumble; no hidden fixed 1.5 kHz loop LP survives."                      # memo: COLOR = darkness tool, no buried constants
    - "Loudness stays roughly constant across the LENGTH sweep and TAPS level-matches the kick at VOL~0.8 with DRIVE=0 (not 'too quiet')."            # memo: fixes 'too quiet, must crank VOL + drive'
    - "Output is never bit-crushed: <0.1% of samples hit the int16 rail at default settings; no hardcoded x3.0 makeup into a saturator."             # memo: bit-crushed complaint root cause deleted
    - "REVERB MIX knob is bidirectional: left = reverb pre-smear into the tap ring, center = off, right = post reverb; reuses the existing Schroeder." # memo: bidirectional pre/post reverb
    - "Tap spacing is fractional and slewed so live BPM changes glide without clicks."                                                                # research E / A.6
    - "render_block performs ZERO allocations and produces no NaN/Inf, peak <= 1.0, across all TAPS states."                                          # CLAUDE.md RT-safety
    - "The GEN groove voice behaviour is unchanged (locked scope constraint 6)."                                                                       # constraint: GEN must not change
  artifacts:
    - path: "src/groove.h"
      provides: "Redesigned TAPS fields on groove_state_t: fractional slewed spq, single tap_trim, diffusion allpass buffers, feedback-path HP state, reverb pre/post routing amounts. tap_decay[4] and FB_LP_A removed."
      contains: "groove_state_t"
    - path: "src/groove.c"
      provides: "Redesigned TAPS branch of groove_tick + control-rate mappings in groove_set_param/groove_set_length; bidirectional LENGTH, equal-power taps, fractional slewed reads, feedback-path HP + COLOR 2-pole loop LP + diffusion, bidirectional pre/post reverb routing. RAW kick written to ring (HP only on feedback path). GEN branch byte-identical."
      contains: "groove_tick"
      min_lines: 400
    - path: "src/params.c"
      provides: "Updated GKI_GRV_RVMIX default to 0.5 (center = reverb off) so a bare create is neutral; LENGTH/other defaults reconciled with the new curve."
      contains: "GKI_GRV_RVMIX"
    - path: "tests/test_readback.c"
      provides: "Readback assertion updated for the new grv_rvmix center=off default (0.5), so a bare create is verified neutral."
      contains: "grv_rvmix"
    - path: "tests/test_taps_redesign.c"
      provides: "Native acceptance harness for the 10 DSP-research acceptance criteria (clean-copies EQUAL-LEVEL vs RAW kick with post-groove chain neutralised, equal taps, 30 s drone stability, constant loudness, no-rail, COLOR darkens, no hidden LP, NaN/peak, BPM-click-free, pre-reverb stability with energy floor) + zero-alloc-in-render. File-local helpers (select_model, prime_groove, render_driven, buf_rms, ZCR) are COPIED into this file, not included."
      exports: ["main"]
      min_lines: 250
    - path: "Makefile"
      provides: "TAPS_TEST_SRCS + test-taps-redesign target wired into the `test` aggregate and .PHONY."
      contains: "test-taps-redesign"
  key_links:
    - from: "src/groove.c groove_tick TAPS branch"
      to: "groove_state_t ring buf_l/buf_r via fractional read"
      via: "linear-interpolated read k*spq behind write_pos, two & GRV_DELAY_MASK indices"
      pattern: "GRV_DELAY_MASK"
    - from: "src/groove.c groove_set_length"
      to: "fb_amount + tap_trim (opposing functions of LENGTH)"
      via: "control-rate mapping fb=0.85*(1-v), tap_trim=0.6+0.4*v, diffuse_amt=1-v"
      pattern: "fb_amount"
    - from: "src/groove.c groove_set_param PK_GRV_RVMIX"
      to: "reverb pre_amt / post_amt with center deadzone (replaces old rv_mix=v)"
      via: "bidirectional split on v around 0.5"
      pattern: "PK_GRV_RVMIX"
    - from: "tests/test_taps_redesign.c"
      to: "the real plugin via move_plugin_init_v2 -> create_instance -> set_param -> render_block"
      via: "mock_host.c driven transport + RAW isolated-kick reference render (post-groove chain neutralised)"
      pattern: "render_block"
---

<objective>
Pick up and implement the TAPS groove (Omega groove rumble voice) redesign specified in the DSP research, replacing the current "bit-crushed & quiet" 4-tap echo with a single feedback delay line read at 4 fractional tap points and driven by a bidirectional LENGTH knob (right = sample-exact clean kick copies on every 16th; left = damped/diffused resonant drone). Add a bidirectional pre/post REVERB MIX reusing the existing Schroeder reverb. Prove every acceptance criterion in the native test harness.

Purpose: The current TAPS voice fails the user's core design intent (equal-level taps at LENGTH max, dial-in-able darkness, no buried constants) due to three stacked gain/nonlinearity bugs diagnosed in RESEARCH-DSP: per-tap `pow` decay, a hardcoded `x3.0` makeup into the saturator, and a hidden 1.5 kHz loop LP.
Output: A redesigned RT-safe TAPS engine (groove.c/groove.h), reconciled params/UI for the bidirectional REVERB knob, a new acceptance test suite, and an on-device listening checklist gate.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@CLAUDE.md
@.planning/quick/260930-vhr-pick-up-taps-rumble-redesign-with-full-d/260930-vhr-RESEARCH-DSP.md
@.planning/quick/260930-vhr-pick-up-taps-rumble-redesign-with-full-d/260930-vhr-RESEARCH-SCHWUNG.md

# Current implementation (redesign target)
@src/groove.h
@src/groove.c
@src/params.c
@src/ui.c

# Test harness patterns to mirror
@tests/test_groove.c
@tests/mock_host.h

<interfaces>
<!-- Contracts the executor needs. Extracted from the codebase — no exploration required. -->

Ring + wrap (src/groove.h):
```c
#define GRV_DELAY_LEN  131072u
#define GRV_DELAY_MASK (GRV_DELAY_LEN - 1u)   /* branch-free & mask wrap, never % */
```

TPT 1-pole primitive (src/dsp_primitives.h) — used by groove.c via a bare-float view:
```c
typedef struct { float s; } tpt1_t;
static inline float tpt1_lp(tpt1_t *f, float x, float g);   /* g = tanf(pi*fc/SR), control-rate */
/* OMEGA_SR is the fixed 44100 sample rate. */
```
A 2-pole COLOR LP is two cascaded tpt1_lp stages (CLAUDE.md blesses "cascade two TPT stages"; 12 dB/oct, no resonance) — this is the acceptable interim per RESEARCH-DSP §C.1.

Groove tick / control-rate entry points (src/groove.c, current signatures — KEEP them):
```c
void groove_init(groove_state_t *g);
void groove_update_tempo(groove_state_t *g, const struct host_api_v1 *host, int frames);
void groove_tick(groove_state_t *g, float kick_l, float kick_r, float *out_gl, float *out_gr);
void groove_set_param(groove_state_t *g, const char *key, const char *val);
```

Groove param key macros (src/omega.h) — REUSE, do not rename (param-key compatibility, constraint 3):
```
PK_GRV_TYPE PK_GRV_VOL PK_GRV_LENGTH PK_GRV_COLOR PK_GRV_TAP1..4 PK_GRV_MONO
PK_GRV_DRIVE PK_GRV_FILTYPE PK_GRV_LFOSPD PK_GRV_LFOAMT
PK_GRV_RVMIX PK_GRV_RVDECAY PK_GRV_RVTONE PK_GRV_RVTYPE
```

Existing PK_GRV_RVMIX handler to REPLACE (src/groove.c ~line 412) — the old direct
`g->rv_mix = v;` assignment MUST be replaced by the bidirectional pre/post mapping in Task 1:
```c
} else if (strcmp(key, PK_GRV_RVMIX) == 0) {
    g->rv_mix = v;      /* OLD: linear post-only mix — DELETE, replace with pre/post split */
}
```

Existing PK_GRV_COLOR handler (src/groove.c ~line 386) — note it ALWAYS forces the output
LP on (`g->filter_type = GRV_FILT_LP;`). This is why the clean-copies gate (#1) MUST set
GRV_FILTYPE=OFF (value "2") AFTER setting COLOR, to bypass the output LP for that test:
```c
} else if (strcmp(key, PK_GRV_COLOR) == 0) {
    float fc = 30.0f * powf(20000.0f / 30.0f, v);
    g->color_g = tpt_g_from_hz(fc);
    g->filter_type = GRV_FILT_LP;        /* COLOR forces LP on — set FILTYPE after COLOR to bypass */
}
```

POST-GROOVE PERFORMER CHAIN (src/dsp.c render_block) that the clean-copies gate MUST neutralise
(these run AFTER groove_tick and would break sample-exactness against a groove-free reference):
```c
/* Sidechain duck: kick note-on sets duck_env=1 (dsp.c ~377); DUCK default 0.5 in params.c
 * means duck_depth>0, so the groove lows are multiplied by a time-varying duck_gain_s<1.
 * The groove-free reference render has no groove, so nothing cancels -> gate #1 fails.
 * FIX for the test: set PK_DUCK="0.0" (duck_depth=0 -> dg=1, no attenuation). (dsp.c ~511-530) */
/* DJ filter: bypassed only when dj_mode==2 (neutral). PK_DJ_FILT default 0.5 = neutral,
 * but the gate MUST assert/set PK_DJ_FILT="0.5" to guarantee dj_mode==2. (dsp.c ~536-555) */
/* Soft clip: applied when clip_on. PK_CLIP default 0.0 = off, but the gate MUST set
 * PK_CLIP="0.0" explicitly. Also set PK_MASTER_VOL="1.0" so the known scale is 1.0. (dsp.c ~557-569) */
```

Test harness (tests/mock_host.h):
```c
host_api_v1_t make_mock_host(void);                 /* resets beat=0, bpm=120 */
host_api_v1_t make_mock_host_null_transport(void);  /* both callbacks NULL -> 120 fallback */
void mock_host_set_beat(double beat);
void mock_host_advance_beat(double dbeat);
void mock_host_set_bpm(float bpm);
```
test_groove.c / test_distinct.c define reusable helpers to MIRROR by COPYING (they are
file-local `static` — NOT header-exported, so they cannot be #included): `select_model`,
`prime_groove`, `render_driven`, `buf_rms`, ZCR, `samples_per_16th(bpm)`, `dbeat_for_bpm(bpm)`.

Fractional linear-interp read behind the write head (RESEARCH-DSP §A.6, target pattern):
```c
float d  = (float)k * g->spq;                 /* spq = fractional samples/16th, slewed */
float rp = (float)g->write_pos - d;
int   i0 = ((int)floorf(rp)) & GRV_DELAY_MASK;
int   i1 = (i0 + 1) & GRV_DELAY_MASK;
float fr = rp - floorf(rp);
float s  = buf[i0] + fr * (buf[i1] - buf[i0]);
```
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Redesign the TAPS engine (groove.h + groove.c)</name>
  <files>src/groove.h, src/groove.c</files>
  <behavior>
    Test expectations that Task 3 will encode (write these mentally before implementing):
    - LENGTH=1, TAP1-4=1, RVMIX=0.5(off), COLOR wide open BUT GRV_FILTYPE=OFF (bypass output LP), DRIVE=0, MONO=0, and the post-groove performer chain neutralised (DUCK=0, DJ filter neutral, CLIP off, MASTER_VOL=1.0), one kick: the TAPS-added output holds the SAME kick waveform on every 16th at EQUAL level across all 4 taps (per-tap amplitude ratio == 1.0, cross-correlation with the raw kick == 1.0 within epsilon). fb must be exactly 0 at LENGTH=1 so the ring holds the raw kick (the ~30 Hz HP is feedback-path only). The absolute per-tap amplitude = grv_vol * tap_trim(1.0) * (1/sqrt(4)=0.5) of the raw kick — the equal-power 0.5 scale is expected, not a bug; the invariant verified is EQUAL-LEVEL CLEAN COPIES, and the reference either applies the 0.5 scale or the gate is ratio/shape-exact.
    - Same setup: RMS around tap 1 window == RMS around tap 4 window within +/-0.5 dB (equal taps).
    - LENGTH=0, taps=1, kicks every 4 beats for 30 s: every sample isfinite and |x|<=1.0; tail energy does not grow unbounded.
    - Sweep LENGTH 0..1: output RMS varies < +/-3 dB (constant loudness / equal-power comp).
    - Defaults (LENGTH~0.7, taps~0.7, DRIVE=0): <0.1% of samples at the +/-32767 int16 rail.
    - COLOR low vs high at LENGTH=0.5: ZCR(high) > ZCR(low) by a clear margin.
    - Stepping mock BPM mid-render: no inter-sample jump exceeds a whole-sample-jump threshold (slew works).
  </behavior>
  <action>
    Implement the RESEARCH-DSP §F recommended redesign. Honor all locked scope decisions.

    groove.h — modify groove_state_t (all BY VALUE, single calloc; keep new RAM < ~150 KB, target < ~2 KB):
    - ADD: `float spq;` and `float spq_target;` (fractional slewed 16th length; replaces integer `samples_per_16th` for READS — keep the existing int field too if convenient for the GEN branch, but TAPS reads use spq).
    - ADD: `float tap_trim;` (single global trim; replaces `tap_decay[4]`).
    - ADD: `float diffuse_amt;` (0 clean .. 1 fully diffused).
    - ADD two short Schroeder allpass buffers for in-loop diffusion, mutually prime: `float ap1[241]; int ap1i; float ap2[113]; int ap2i;` (~1.4 KB).
    - ADD feedback-path HP state: `float loop_hp_l_s, loop_hp_r_s;` and a fixed HP coefficient field or compute it in groove_init at ~30 Hz. NOTE: this HP is applied ONLY inside the recirculation (feedback) path, NOT on the ring input.
    - ADD a second COLOR LP stage state for the 2-pole cascade: `float color_lp2_l_s, color_lp2_r_s;` (loop-darkening + output share the COLOR coefficient per §C.3 — start shared).
    - ADD reverb routing amounts: `float rv_pre_amt, rv_post_amt;` (bidirectional MIX, §B.2).
    - REMOVE: `float tap_decay[4];` and delete the `FB_LP_A` hidden 1.5 kHz constant entirely (it becomes the COLOR-linked loop LP, not a buried constant).
    - Keep `fb_amount`, `fb_lp_l_s/r_s`, `tap_level[4]`, `color_g`, `color_lp_l_s/r_s`, `mono`, `vol`, all GEN fields, and the reverb comb/allpass buffers.

    groove.c:
    - groove_set_length(g, v): DELETE the per-tap `pow` loop. Implement the opposing-knob morph (§A.2):
        `g->fb_amount = 0.85f * (1.0f - v);`   (0 at v=1 clean, 0.85 at v=0 drone; stability cap)
        `g->tap_trim  = 0.6f + 0.4f * v;`       (equal-power loudness comp: pull taps down as feedback rises)
        `g->diffuse_amt = 1.0f - v;`            (bypass diffusion at clean end)
      At v=1 fb_amount MUST be exactly 0 so clean-copies exactness (acceptance #1) holds.
    - groove_update_tempo: keep the guarded get_beat_position -> get_bpm -> 120 chain UNCHANGED (constraint 2: do NOT adopt move_info.h, do NOT extend host_api_v1_t; note move_info.h as a deferred follow-up in the SUMMARY). Set `g->spq_target = (60.0f/bpm)*OMEGA_SR/4.0f;` as a FLOAT (no rounding) whenever BPM re-locks. Keep the existing int samples_per_16th for the GEN branch. Seed `g->spq = g->spq_target` in groove_init.
    - groove_tick TAPS branch — rewrite per §F.1 signal flow. CRITICAL PLACEMENT: the ~30 Hz HP goes ONLY on the feedback signal (inside the recirculation loop), NOT on the ring input. The RAW, unfiltered kick is what gets written to the ring, so at fb=0 the taps are exact copies of the raw kick (acceptance #1 compares against a RAW isolated-kick reference; a 30 Hz HP on the input would change a sub-heavy kick far more than a few LSB and would fail that gate). DC/sub cleanup on the feedback path still keeps the drone stable.
        1. Per-sample slew: `g->spq += (g->spq_target - g->spq) * SPQ_SLEW;` with `#define SPQ_SLEW 0.0005f` (~30 ms; §A.6). Guard spq >= 1.0f and spq*4 within the ring.
        2. Feedback tap read at 1*spq behind write_pos via the fractional linear-interp read (see <interfaces>) — call this `fb_signal`.
        3. Apply the ~30 Hz HP to `fb_signal` ONLY (structural sub-cleaner + stability, §C.2/§A.4): `fb_hp = fb_signal - tpt1_lp_at_30Hz(fb_signal)` using loop_hp_*_s at a fixed ~30 Hz coefficient computed in groove_init. (Do NOT high-pass the kick/ring input.)
        4. Diffuse `fb_hp` through the two short allpasses, crossfaded by diffuse_amt (§A.5): `fb = ap_in + diffuse_amt*(ap_out - ap_in)` for each allpass in series.
        5. COLOR-linked 2-pole LP in the loop (darkens the drone as it decays; §C.3) using color_g cascaded twice — applied to the feedback signal.
        6. Ring input = RAW kick + rv_pre_amt*reverb(kick) [see reverb routing below] + fb_amount * fb_signal. NO ~30 Hz HP on this sum. NO x3.0. Optional VERY gentle safety limiter only (fb<=0.85 is small-signal stable) — keep it out of the tone path or omit (§F.3, Open Q5). At fb=0 and rv_pre_amt=0 the ring input is EXACTLY the raw kick (this is what makes acceptance #1 clean copies at equal level).
        7. Write to buf_l/buf_r[write_pos]; advance write_pos with & GRV_DELAY_MASK.
        8. Sum 4 EQUAL taps at k*spq (k=1..4) via fractional reads, each weighted by `tap_level[t] * tap_trim`. Normalize equal-power: divide the tap sum by `sqrtf(fmaxf(sum_of_active_tap_gains, 1.0f))` (§A.3). Precompute the sqrt divisor at control rate in groove_set_length / a tap-level setter (NOT per sample — no per-sample sqrt/transcendental; store e.g. `g->tap_norm`). NOTE FOR ACCEPTANCE #1: at LENGTH=1 with all 4 taps=1, the divisor is sqrt(4)=2, so each clean copy comes out at 0.5x the raw kick amplitude (times grv_vol). This is the equal-power scale the test's reference accounts for.
      DELETE the `gl *= 3.0f; gr *= 3.0f;` makeup.
    - COLOR output filter: keep applying the COLOR LP on the final tap-summed output, but make it 2-pole (cascade the two tpt1 stages using color_lp_*_s and color_lp2_*_s). Preserve GRV_FILT_LP/HP/OFF behaviour via grv_filtype. IMPORTANT: GRV_FILT_OFF must be a true bypass of the output filter (no attenuation, no phase shift) so acceptance #1 can neutralise the output LP by setting GRV_FILTYPE=OFF after COLOR.
    - REVERB routing (§B.2) — bidirectional, ONE reverb instance. In groove_set_param, the OLD `g->rv_mix = v;` in the PK_GRV_RVMIX handler (groove.c ~line 412) MUST be REPLACED with:
        center deadzone +/-0.03; `v<0.47`: pre mode, `rv_pre_amt=(0.5-v)*2`, `rv_post_amt=0`; `v>0.53`: post mode, `rv_post_amt=(v-0.5)*2`, `rv_pre_amt=0`; else both 0 (off). (You may keep `g->rv_mix` as the post-mix scalar driven by rv_post_amt if the reverb block reads it, or drive the block from rv_post_amt directly — but the linear post-only `rv_mix=v` semantics are gone.)
        PRE: run the existing Schroeder reverb on the KICK and add rv_pre_amt*wet to the ring INPUT (raw kick + pre*rev), attenuate reverb-into-ring so effective send <= ~0.5 (guardrail, §B.2). Raw dry kick still always enters the ring. The Schroeder reverb is MONO-in (it sums `0.5*(gl+gr)`); in PRE mode mono-sum the STEREO kick into the reverb input the same way, then add the mono wet equally to both ring channels (kick stays stereo on the dry path — do not collapse the dry kick to mono).
        POST: run the same reverb on the tap-summed output, mix rv_post_amt wet (current behaviour). Refactor the existing reverb block into a small helper so it can be called at the pre point OR the post point without duplicating buffers.
    - Keep RVDECAY/RVTONE mappings; OPTIONALLY upgrade RVDECAY to the Jot RT60 map `g=10^(-3M/(RT60*SR))` (§D.1) — allowed, not required.
    - Reconcile groove_init defaults with the new fields (spq, tap_trim, diffuse_amt, tap_norm, loop HP coeff, rv_pre/post_amt=0, color_lp2 states=0).
    - RT-SAFETY (CLAUDE.md, hard): groove_tick must have ZERO malloc/free, ZERO host->log, ZERO file I/O, and NO per-sample powf/tanf/expf/sqrtf/division. Only floorf (single aarch64 instruction) is allowed per sample for the fractional read. All powf/tanf/sqrtf stay in groove_set_param / groove_set_length / groove_update_tempo (control rate).
    - The GEN branch of groove_tick and ALL gen_* logic must be BYTE-IDENTICAL — do not touch it (constraint 6). This INCLUDES the GEN wavefolder line `float d = 1.0f + g->gen_fold * 3.0f;` (groove.c ~line 267) — that `* 3.0f` is GEN-internal and must stay verbatim; only the TAPS `gl *= 3.0f; gr *= 3.0f;` makeup is deleted. If spq replaces samples_per_16th anywhere the GEN branch reads, keep the int samples_per_16th field feeding GEN exactly as today.
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && make test-groove 2>&1 | tail -20 && ! grep -qE "FB_LP_A|gl \*= 3\.0f|gr \*= 3\.0f|tap_decay" src/groove.c</automated>
  </verify>
  <done>groove.c compiles; existing test-groove suite stays GREEN (GEN unchanged, tap/tempo assertions still pass); `FB_LP_A`, the TAPS `gl *= 3.0f`/`gr *= 3.0f` makeup, and `tap_decay` are gone from groove.c (the GEN `gen_fold * 3.0f` line is intentionally UNTOUCHED and does not match the guard); groove_tick contains no per-sample powf/tanf/sqrtf/division; the RAW kick (not high-passed) is written to the ring; GRV_FILT_OFF is a true output-filter bypass.</done>
</task>

<task type="auto">
  <name>Task 2: Reconcile params + UI for the bidirectional REVERB knob</name>
  <files>src/params.c, src/ui.c, tests/test_readback.c</files>
  <action>
    Keep every existing PK_GRV_* key (param-key compatibility, constraint 3) — this is defaults + labels only, no new keys.

    src/params.c (g_global_defaults):
    - Set `[GKI_GRV_RVMIX]=0.5f` (was 0.0f) so a bare create sits at the CENTER = reverb OFF deadzone of the new bidirectional knob. Confirm groove_init's rv_pre_amt/rv_post_amt start at 0 to match (they will, since 0.5 -> off).
    - Leave `[GKI_GRV_LENGTH]=0.5f` (still a valid mid-morph). Verify no other groove default assumes the old per-tap decay semantics; adjust only if a comment references tap_decay.
    - Do NOT change PKI (kick) defaults.

    tests/test_readback.c (REQUIRED — the default changed, so the readback test must reflect it):
    - Search test_readback.c for any assertion pinned to the OLD grv_rvmix default of 0.0. Currently the file does not assert grv_rvmix on bare create (it only round-trips grv_vol at ~line 84), so there is no stale 0.0 assertion to break — but you MUST ADD a positive assertion that a bare create reports grv_rvmix == 0.5 (center = off), so "center=off on bare create" is tested. Add, in the "Bare create reports musical defaults" block (near line 67-71, after the master_vol/fx_type asserts): `assert(approx(get_val(api, inst, PK_GRV_RVMIX), 0.5f));` with a comment `/* bidirectional reverb: center = OFF on bare create */`, and add PK_GRV_RVMIX to the printf line. If any other test file (grep tests/ for grv_rvmix / GRV_RVMIX / RVMIX) asserts the old 0.0 default, update it to 0.5 as well.

    src/ui.c:
    - Update the P_GROOVE_FX RV MIX slot label/short-name to communicate the bidirectional meaning, e.g. name "REVERB" short "REV" (or keep "RV MIX") — a cosmetic label change only; keep the key PK_GRV_RVMIX. Add/adjust the comment above P_GROOVE_FX to note center=off, left=pre-smear, right=post.
    - Update the comment block near P_GROOVE1 (LENGTH now = bidirectional clean<->drone morph, not per-tap decay) so the UI docs match the new behaviour.
    - Do NOT restructure the groove page emission (groove1 / groove2=Groove Effects for TAPS / groove3 for GEN) — layout is unchanged; only labels/comments.
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && make test-switch test-readback 2>&1 | tail -15</automated>
  </verify>
  <done>test-switch (ui_hierarchy still balanced/valid across model+groove-type switches) and test-readback (set->get echo AND the new bare-create grv_rvmix == 0.5 assertion) both GREEN; every PK_GRV_* key preserved; no test still pins the old grv_rvmix 0.0 default.</done>
</task>

<task type="auto">
  <name>Task 3: Native acceptance test suite for the 10 DSP-research criteria</name>
  <files>tests/test_taps_redesign.c, Makefile</files>
  <action>
    Create tests/test_taps_redesign.c mirroring test_groove.c's harness style (RESEARCH-DSP §F.5 "Native test approach"). Drive the REAL plugin: move_plugin_init_v2(&mock) -> register/select a kick model (FM2) -> create_instance -> set_param -> mock_host_advance_beat -> render_block -> analyze int16. Reuse mock_host.c (make_mock_host + make_mock_host_null_transport).

    HELPER COPYING (important): `select_model`, `prime_groove`, `render_driven`, `buf_rms`, the ZCR helper, `samples_per_16th(bpm)`, and `dbeat_for_bpm(bpm)` are file-local `static` functions defined inside tests/test_distinct.c and tests/test_groove.c. They are NOT declared in any shared header, so they CANNOT be #included. COPY the ones you need verbatim into tests/test_taps_redesign.c as file-local statics (do not add them to a header, do not link the other test TUs). Keep the copies byte-consistent with the originals so behaviour matches.

    POST-GROOVE CHAIN NEUTRALISATION (shared setup helper — CRITICAL): several gates compare against a groove-free reference or need a known linear scale. Before those gates, set the performer chain to unity/bypass so groove_tick's output passes through unmodified: `PK_DUCK="0.0"` (duck_depth=0 -> no groove-low attenuation; without this the note-on-triggered duck multiplies the groove lows and the groove-free reference can't cancel it), `PK_DJ_FILT="0.5"` (dj_mode==2 neutral bypass), `PK_CLIP="0.0"` (soft clip off), `PK_MASTER_VOL="1.0"` (known unity scale). Put this in a helper (e.g. `neutralise_chain(api, inst)`) called by gates #1, #2, #4, #5, #6, #7.

    Implement these gates (all should PASS once Task 1 lands; write them to genuinely exercise the behaviour, min-energy guarded so they cannot pass trivially silent):
    1. Clean-copies EQUAL-LEVEL: FIRST neutralise the post-groove chain (DUCK=0, DJ filter neutral, CLIP off, MASTER_VOL=1.0 — see the helper above). Capture an ISOLATED-kick RAW reference render (grv_vol=0 so groove is silent, read the dry kick — NOT high-passed, matching the raw kick written to the ring since the ~30 Hz HP is feedback-path-only). Then set LENGTH=1, TAP1-4=1, RVMIX=0.5 (reverb off), COLOR=1.0 and THEN GRV_FILTYPE="2" (OFF, to bypass the output LP — COLOR forces LP on, so FILTYPE must be set AFTER COLOR), DRIVE=0, MONO=0, grv_vol to a known level (e.g. 1.0). Trigger ONE kick. Because equal-power normalisation divides the 4-tap sum by sqrt(4)=2 at taps=1, each clean copy appears at scale = grv_vol * tap_trim(=1.0 at LENGTH=1) * 0.5 of the raw reference. VERIFY the EQUAL-LEVEL CLEAN-COPY invariant (do NOT require absolute equality to 4 summed raw copies): (a) at each tap window k=1..4, the groove-added output equals `expected_scale * RAWkick[n-k*S]` within a tight epsilon (a few int16 LSB for fractional-read interpolation) — pick a BPM whose S=(60/bpm)*44100/4 is near-integer (e.g. choose bpm so spq ~ whole) to keep alignment exact; AND (b) the per-tap amplitude ratio tap4/tap1 == 1.0 within epsilon (equal level) and the normalised cross-correlation of each tap window against the raw kick == 1.0 within epsilon (same waveform). Assert the reference itself is non-trivial (RMS above a floor) so the gate can't pass on silence. State expected_scale explicitly in a comment. This proves the user intent "kick copied on every 16th at max" as equal-level clean copies (the 0.5 equal-power scale is stated, not hidden).
    2. Equal tap levels: (neutralise chain) RMS of window [S,2S) vs [4S,5S) within +/-0.5 dB.
    3. Drone stability 30 s: LENGTH=0, taps=1, kick every 4 beats; assert all isfinite, |x|<=1.0, and last-second energy <= mid-second energy * 1.5. Assert mid-second energy is above a non-trivial floor so the gate can't pass silent.
    4. Constant loudness: (neutralise chain) sweep LENGTH {0,0.25,0.5,0.75,1.0}; assert output RMS spread < +/-3 dB and each RMS above a floor.
    5. No bit-crush: (neutralise chain except keep defaults sensible) defaults (LENGTH=0.7, taps=0.7, DRIVE=0); assert fraction of samples at exactly +/-32767 < 0.1% and total energy above a floor.
    6. COLOR darkens: (neutralise chain) LENGTH=0.5; ZCR at COLOR=0.9 > ZCR at COLOR=0.1 by a clear margin.
    7. No hidden 1.5 kHz LP: (neutralise chain) ZCR at COLOR=1.0 exceeds a floor the old FB_LP_A would have suppressed (HF energy retained when COLOR fully open).
    8. NaN/Inf + peak<=1.0 across every test (assert in the shared render helper).
    9. BPM-change click-free: render while stepping mock BPM 120->130 mid-render; assert no |out[n]-out[n-1]| exceeds a threshold a whole-sample delay jump would exceed.
    10. Pre-reverb stability: RVMIX=0.0 (full pre), LENGTH=0 (max feedback), 30 s; isfinite + bounded (same as #3). ALSO add the same NON-TRIVIAL-ENERGY FLOOR as gate #3: assert the pre-path tail energy (last-second RMS) EXCEEDS a minimum so the gate cannot pass on a silent/near-silent pre path. State in a comment that the effective reverb-into-ring send is CAPPED at <= ~0.5 (the §B.2 guardrail in Task 1), so the reference floor is set with that cap in mind (i.e. the floor is a modest positive value, not the full-send level).
    Plus: zero-allocations-in-render — link tests/malloc_trap.c and assert no alloc during render_block (the trap aborts on Linux CI; on Darwin it is compiled out, so also keep the finite/bounded asserts as the portable guard, matching STATE.md A-01 note).

    Makefile:
    - Add `TAPS_TEST_SRCS = tests/test_taps_redesign.c tests/mock_host.c tests/wav.c tests/malloc_trap.c src/dsp.c src/groove.c src/ui.c src/params.c $(wildcard src/models/*.c) src/dsp_primitives.c` (mirror GROOVE_TEST_SRCS). Do NOT add test_groove.c or test_distinct.c to this list — the helpers are copied into test_taps_redesign.c, not linked.
    - Add a `test-taps-redesign` target (mirror `test-groove`: compile to build/test_taps_redesign, run it) with the `| src/wavetables.h` order-only prereq.
    - Add `test-taps-redesign` to the `.PHONY` line and to the `test:` aggregate prerequisites.
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && make test-taps-redesign 2>&1 | tail -25</automated>
  </verify>
  <done>build/test_taps_redesign runs and all 10 acceptance gates + zero-alloc guard PASS; gate #1 verifies equal-level clean copies with the post-groove chain neutralised and the equal-power 0.5 scale accounted for; gate #10 has a non-trivial-energy floor on the pre path; `make test` includes and passes test-taps-redesign; full suite stays GREEN.</done>
</task>

<task type="checkpoint:human-verify" gate="blocking">
  <name>Task 4: On-device TAPS listening checklist (human-verify)</name>
  <action>Blocking manual listening gate (no on-device testing is possible in this environment). Execute the on-device audition steps in <how-to-verify> on Move hardware and record PASS or issues per item. See <what-built> for what was implemented and <resume-signal> for how to resume.</action>
  <what-built>
    Redesigned TAPS groove voice: bidirectional LENGTH (right = clean kick copies at equal level on every 16th; left = damped/diffused resonant drone), equal-power tap normalisation (no x3.0), COLOR as a 2-pole darkness control, feedback-path ~30 Hz HP (raw kick still written to the ring), fractional slewed tap reads for click-free BPM changes, and a bidirectional pre/off/post REVERB MIX reusing the existing Schroeder reverb. All 10 DSP-research acceptance criteria pass offline; render is allocation-free and bounded.
  </what-built>
  <how-to-verify>
    On-device testing is NOT possible in this environment, so this is the required manual listening gate (mirrors the A-04 / B-09 on-device debt pattern). When Move hardware is available:
    1. Build a gate-passing dsp.so (CI artifact, or Docker `make dsp.so` + `scripts/glibc_gate.sh`) and `scripts/deploy.sh` to the Move.
    2. Load Omega, select a kick model (e.g. FM2), raise groove VOL, set groove TYPE = TAPS.
    3. LENGTH full RIGHT + all TAP1-4 full: confirm it sounds like the same kick placed on every 16th (distinct, equal-level, pummeling) — NOT decaying/bit-crushed.
    4. LENGTH full LEFT: confirm a smeared, resonant, continuous drone that stays stable (no runaway, no speaker-flap).
    5. Sweep COLOR: confirm it audibly dials darkness across the range (no stuck 1.5 kHz veil).
    6. Confirm level: at VOL ~0.8, DRIVE=0, the rumble level-matches the kick (not "too quiet").
    7. REVERB knob: LEFT = pre-smear into the taps, CENTER = off, RIGHT = post reverb; confirm distinct and stable.
    8. Automate a live BPM change and confirm no clicks.
    9. Confirm the GEN groove type still behaves exactly as before.
    Record PASS or the issue per item; re-voice constants (fb cap, tap_trim curve, COLOR range) in groove.c if any item fails. Note in the SUMMARY that adopting Schwung's move_info.h for authoritative BPM is a DEFERRED follow-up (constraint 2).
  </how-to-verify>
  <resume-signal>Type "approved" once on-device listening passes, or describe the issues to re-voice.</resume-signal>
</task>

</tasks>

<verification>
- `make test` is fully GREEN including the new test-taps-redesign and the existing test-groove (GEN unchanged).
- groove.c contains no `FB_LP_A`, no TAPS `gl *= 3.0f`/`gr *= 3.0f` makeup, no `tap_decay` (the GEN `gen_fold * 3.0f` line is intentionally preserved).
- groove_tick has no per-sample powf/tanf/expf/sqrtf/division (grep the TAPS branch; only floorf allowed).
- The RAW kick (not high-passed) is written to the ring; the ~30 Hz HP is applied only on the feedback path. GRV_FILT_OFF is a true output-filter bypass.
- All PK_GRV_* param keys preserved; the old `rv_mix = v` handler is replaced by the bidirectional pre/post mapping; grv_rvmix default is 0.5 (center = off) and test_readback asserts it on bare create.
- Gate #1 neutralises the post-groove chain (DUCK=0, DJ filter neutral, CLIP off, MASTER_VOL=1.0) and verifies equal-level clean copies accounting for the equal-power 0.5 scale, not absolute equality to 4 raw copies. Gate #10 has a non-trivial-energy floor on the pre path.
- New per-instance RAM added is < ~150 KB (in practice < ~2 KB: two small allpass buffers + a handful of floats); groove_state_t still inside the single calloc; the instance-size _Static_assert still holds (rebuild confirms).
</verification>

<success_criteria>
- Every user-intent memo requirement maps to a passing acceptance gate: bidirectional LENGTH (#1/#3), equal taps at max (#2), equal-level clean copies vs RAW kick (#1), COLOR = darkness (#6/#7), not-too-quiet (#4/#5), bidirectional pre/post reverb (#10 + routing), no buried constants (#7).
- Offline tests prove equal-level clean copies (against the RAW isolated-kick reference with the post-groove chain neutralised and equal-power scale accounted for, reverb off), equal tap levels, 30 s drone + pre-reverb stability (both with energy floors), constant loudness, no int16-rail abuse, COLOR darkening, BPM-click-free, NaN/peak safety, and zero render allocations.
- GEN groove voice behaviour is unchanged.
- On-device listening checklist is captured as a blocking human-verify checkpoint (hardware UAT debt), with move_info.h flagged as a deferred follow-up.
</success_criteria>

<output>
After completion, create `.planning/quick/260930-vhr-pick-up-taps-rumble-redesign-with-full-d/260930-vhr-SUMMARY.md`.
In the SUMMARY, explicitly note: (a) any tuning-constant deviations from the research starting values (fb cap 0.85, tap_trim 0.6+0.4v, SPQ_SLEW, COLOR pole count) and why; (b) that Schwung's move_info.h authoritative-BPM adoption was deliberately DEFERRED (constraint 2), keeping the guarded get_beat_position/get_bpm/120 chain; (c) the on-device listening checklist as outstanding hardware UAT debt; (d) that gate #1's clean-copies invariant is EQUAL-LEVEL (equal-power 0.5 scale accounted for), verified with the post-groove performer chain neutralised (DUCK=0, DJ filter neutral, CLIP off).
</output>
