---
phase: quick-261001-wft
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - module.json
  - release.json
  - src/ui.c
  - src/params.c
  - src/dsp.c
  - src/groove.c
  - src/groove.h
autonomous: true
requirements: [PLOCK-01, GEN-PITCH-02, GEN-SEQLEN-06, GEN-SUB-03, GRV-DRIVE-07, GEN-FILTENV-04, GEN-DECAY-05]
must_haves:
  truths:
    - "Holding a step and turning any Omega knob locks the value (no 'can't automate this')"
    - "Switching GEN scales does not jump the root pitch (root stays stable/low)"
    - "GEN SEQ LEN knob shows whole integers 1..64, no decimals"
    - "GEN voice has audible low-end weight (built-in sub-octave) with no new control"
    - "GEN/TAPS DRIVE at full-right is noticeably aggressive, matching the kick FX drive character"
    - "GEN filter opens with a short envelope and is resonant (18 dB/oct), filter decay tracks ~65% of note decay"
    - "GEN DECAY knob spreads the short/plucky range across most of its travel"
  artifacts:
    - path: "module.json"
      provides: "Static chain_params array enabling host p-lock/automation/modulation"
      contains: "chain_params"
    - path: "src/groove.c"
      provides: "GEN sub-octave, filter envelope, resonant filter, DECAY curve, matched drive"
    - path: "src/groove.h"
      provides: "New GEN filter-env + sub-osc state fields"
    - path: "src/ui.c"
      provides: "Stable normalized ROOT descriptor + integer SEQ LEN descriptor"
  key_links:
    - from: "host chain param table"
      to: "module.json chain_params"
      via: "parse_chain_params reads module.json on disk"
      pattern: "chain_params"
    - from: "get_param readback"
      to: "integer SEQ LEN / Hz ROOT display"
      via: "key-specific formatting branch in omega_get_param"
      pattern: "PK_GRV_GSEQLEN"
---

<objective>
Fix a batch of 7 diagnosed Omega issues and enable p-locking in the new Schwung
release. The headline fix is a static `chain_params` array in `module.json` that
unblocks the host's automation/modulation/p-lock parameter table (the host reads
this file on disk, not the dynamic `ui_hierarchy`). The remaining 6 are GEN/TAPS
DSP and param-domain fixes already root-caused in RESEARCH.md.

Purpose: Make every Omega knob automatable on-device and tighten the GEN groove
voice (pitch stability, low-end weight, filter character, usable DECAY range) plus
beef up GEN/TAPS drive to match the kick.

Output: Updated module.json (+ version bump), release.json, and targeted edits to
src/ui.c, src/params.c, src/dsp.c, src/groove.c, src/groove.h.
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/quick/261001-wft-fix-batch-of-omega-gen-taps-issues-plus-/261001-wft-RESEARCH.md
@CLAUDE.md

<interfaces>
Canonical param-key macros (src/omega.h) for chain_params entries:
  Root:    PK_MODEL="model", PK_MASTER_VOL="master_vol"
  Kick P1: PK_PITCH="pitch", PK_LENGTH="length", PK_CURVE="curve"
  Kick P2: PK_ATTACK="attack", PK_TRS_DEC="trs_dec", PK_TRS_TNE="trs_tne",
           PK_COLOR="color", PK_FILTER_ROUTE="filter_route", PK_FX_TYPE="fx_type",
           PK_FX_AMT="fx_amt", PK_FX_TONE="fx_tone"
  Per-model P2: fm_ratio/fm_index/op2_wave; fm4_algo/fm4_opratio/fm4_opindex/
           fm4_opamp/fm4_feedback/fm4_algo2; wtr_wave/wtr_bodypitch/wtr_transdec/
           wtr_transcol; phy_beater/phy_shell/phy_headtens/phy_damping;
           hrd_sample/hrd_mix/hrd_drive/hrd_crush; dig_waveidx/dig_sample/
           dig_bitdepth/dig_pitchenv; trs_tone/trs_tdec/trs_wtcol/trs_curve;
           ana_morph/ana_sublvl/ana_subdec/ana_sample; usr_sample/usr_wtmorph/
           usr_layervol/usr_pitchenv; gen_seed/gen_scale/gen_density/gen_seqlen/
           gen_lpffreq/gen_lpfpole
  Groove:  grv_type, grv_vol, grv_length, grv_color, grv_tap1..4, grv_mono,
           grv_drive, grv_filtype, grv_lfospd, grv_lfoamt, grv_rvmix, grv_rvdecay,
           grv_rvtone, grv_rvtype, grv_route, grv_gscale, grv_gseed, grv_gseqlen,
           grv_gdensity, grv_grotate, grv_gswing(=DECAY), grv_gwave, grv_gfold,
           grv_gretrig, grv_groot, grv_grange
  Performer: duck, duck_rel, duck_smt, duck_bs, dj_filt, dj_reso, cmpdr(=PK_CLIP)

Host chain_params parser (RESEARCH Finding 1): each entry requires "key", "name"
  (or "label"), "type" in {"float","int","enum"}. Optional "min","max","default",
  "step","unit","display_format". Enum: "options":[...]; host derives max from
  option count. Capacity 256, key[32], name[64]. module.json read capped at 65536 B.

Defaults source: match min/max/default to src/ui.c uiparam_t tables + src/params.c
  g_global_defaults/g_kick_defaults. PITCH Hz [30,200] default 50. Normalized/percent
  knobs [0,1]. Enum option lists live in ui.c as OPT_* arrays (model 9, fx_type 5:
  Diode/Clip/SAT/Fold/Crush, filter_route 3: Synth/Transient/Both, grv_type 2:
  Taps/Gen, grv_filtype 3: LP/HP/Off, grv_rvtype 3: Room/Hall/Plate, grv_route 4,
  grv_gscale 13: Unquantized..Diminished, grv_gretrig 6: None/1 Bar/2 Bar/4 Bar/
  8 Bar/On Note, gen_lpfpole 2: 2-pole/4-pole).

groove.c control-rate handlers (ALL powf/expf/tanf HERE, NEVER in groove_tick):
  groove_set_param(): PK_GRV_GSWING (DECAY): tau_s = 0.005*powf(30,v);
    gen_env_coef = expf(-1/(tau_s*SR)). PK_GRV_GROOT / PK_GRV_GSCALE:
    gen_base_hz = 20*powf(100,v).
  groove_tick() GEN branch (~line 348): per-step note trigger sets gen_freq/gen_env;
    wavetable read via wt_read_bl(wa,0,phase) + fold; gen_env *= gen_env_coef;
    gen_osc_phase += gen_freq/OMEGA_SR. COLOR filter block (~line 500 for GEN) uses
    tpt1_lp(&t, x, cg) 1-pole; cg from tpt_g_from_hz(fc) at control rate.
  groove_drive_block() (~line 122): hand-rolled diode k=1+drive*4, makeup
    mk=1/(1+drive*0.7), dry/wet by drive. This is the one to strengthen.

Kick FX drive to match (src/dsp_primitives.c): fx_config(st,mode,amt) sets
  st->out_gain=1/(1+amt*comp) (Diode comp=.4); fx_process(FX_DIODE,x,amt,st)=
  copysignf(diode_shape(|x|*k),x) dry/wet blended by amt, bounded [-1,1].

get_param display (src/dsp.c omega_get_param ~line 538): generic
  pk_format_value(cache, 4, buf, buf_len) for ALL keys. pk_format_value(v,decimals,
  buf,len) is locale-independent. Add a key-specific branch BEFORE the generic one.

groove_state_t (src/groove.h ~line 98-122): has gen_* fields incl. gen_env,
  gen_env_coef, gen_freq, gen_osc_phase, gen_decay, color_lp_l_s/r_s,
  color_lp2_l_s/r_s, color_g. Struct closes at line 123 — add NEW fields before the
  closing brace.
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Add static chain_params to module.json + bump versions (Finding 1 headline p-lock fix)</name>
  <files>module.json, release.json</files>
  <action>
    Add a top-level "chain_params" array to module.json (ADDITIVE; do NOT add a
    "ui_hierarchy" key — the host uses chain_params only when no ui_hierarchy key is
    in the file, which is our case per RESEARCH Finding 1). The dynamic on-screen
    get_param("ui_hierarchy") display path is untouched; this array feeds ONLY the
    host automation/p-lock table.

    Emit one entry per automatable key = the UNION of all keys in the <interfaces>
    block (root + kick P1 + kick P2 shared + every per-model P2 + all groove globals
    incl. the grv_g* GEN set + performer). Use canonical PK_* string spellings.
    Mirror type/min/max/default from src/ui.c uiparam_t tables and src/params.c so
    automation ranges equal the knobs:
      - Floats: {"key","name","type":"float","min","max","default","step"} plus
        "unit" where ui.c sets one. PITCH min 30 max 200 default 50 unit "Hz".
        Percent/normalized knobs min 0 max 1.
      - grv_gseqlen: type "int", min 1, max 64, step 1, default 16 (whole value).
      - Enums: type "enum", "options":[...] matching ui.c OPT_* arrays listed in
        <interfaces>; "default" is the option index.
    Keep valid JSON, well under 65536 bytes (~50 entries).

    Bump "version" in module.json 0.2.1 -> 0.3.0. Bump "version" in release.json
    0.2.1 -> 0.3.0 and change the download_url "/v0.2.1/" segment to "/v0.3.0/".
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && python3 tools/check_chain_params.py</automated>
  </verify>
  <done>module.json has a valid chain_params array (>=45 entries, each with key/name/type, grv_gseqlen is int, enums carry options), no ui_hierarchy key added, versions are 0.3.0 in both files, both parse as JSON. (Executor may write the one-shot checker tools/check_chain_params.py as part of this task, or inline an equivalent python3 -c assertion.)</done>
</task>

<task type="auto">
  <name>Task 2: GEN root pitch stability (Finding 2) + SEQ LEN integer display (Finding 6)</name>
  <files>src/ui.c, src/dsp.c, src/params.c</files>
  <action>
    FINDING 2 (ui.c ui_emit_gen_groove1 ~line 304): remove the unquantized Hz-domain
    ROOT descriptor swap. Make p_root ALWAYS the normalized 0..1 descriptor in BOTH
    modes — delete the `unq ? {..20..2000 Hz..} : {..}` ternary and emit a single:
      { PK_GRV_GROOT, "ROOT", "ROOT", UP_FLOAT, "", "0.012", NULL, NULL, NULL }
    (NULL min/max defaults to 0.0/1.0). This stops the host sending raw Hz that
    clamps to 1.0 and pins gen_base_hz to the top. Remove the now-unused `bool unq`
    if nothing else references it. groove.c's gen_base_hz = 20*powf(100,v) log map is
    CORRECT and unchanged; the scale handler stays pitch-stable because v is 0..1.

    FINDING 6 part 1 (ui.c P_GROOVE_GEN_SEQ ~line 224): the SEQ LEN descriptor is
    UP_FLOAT so the host shows decimals. Introduce UP_INT in the up_type_t enum
    (ui.c ~line 39) and handle it in ui_emit_param (~line 63): emit "type":"int" with
    integer min/max/step and a 0-decimal default. Change the PK_GRV_GSEQLEN
    descriptor to UP_INT min "1" max "64" step "1". Leave all other descriptors
    UP_FLOAT.

    FINDING 2+6 display (dsp.c omega_get_param ~line 538, BEFORE the generic 4-decimal
    pk_format_value fallthrough): add a key-specific branch:
      - PK_GRV_GSEQLEN: v = 1 + (int)(cache*63 + 0.5) (mirrors groove.c map); format
        0 decimals (pk_format_value(v,0,buf,buf_len)).
      - PK_GRV_GROOT: OPTIONAL derived Hz readout 20.0f*powf(100.0f,cache), 0 decimals.
        (Display-only; powf is fine here — get_param is UI/control rate, not render.
        Leaving ROOT as the normalized readback is also acceptable; the REQUIRED
        Finding-2 behaviour — ROOT no longer explodes — is delivered by the ui.c fix.)
    Keep the generic 4-decimal fallthrough for every other key.

    params.c: do NOT change defaults unless an existing readback assertion breaks.
    (GKI_GRV_GSEQLEN currently 1.0 normalized maps to seqlen 64 — out of scope to
    retune; the integer DISPLAY fix is what Finding 6 requires.)
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && grep -q UP_INT src/ui.c && ! grep -q "ROOT HZ" src/ui.c && make test-groove test-switch test-readback</automated>
  </verify>
  <done>ui.c has no "ROOT HZ"/2000-Hz descriptor (single normalized ROOT in both modes), UP_INT exists and grv_gseqlen uses it, dsp.c formats grv_gseqlen as a 1..64 integer; make test-groove/test-switch/test-readback all GREEN.</done>
</task>

<task type="auto">
  <name>Task 3: GEN built-in sub-octave (Finding 3) + stronger matched GEN/TAPS drive (Finding 7)</name>
  <files>src/groove.c, src/groove.h</files>
  <action>
    FINDING 3 (sub-octave, NO new UI param): in the GEN oscillator in groove_tick
    (~line 383-404), sum a second wavetable reader one octave below the fundamental
    at a fixed blend (~0.45 of the fundamental) to add low-end weight. Add a
    `float gen_sub_phase;` field to groove_state_t (src/groove.h, before the closing
    brace ~line 122) and zero it in groove_init and on each note trigger (where
    gen_osc_phase is reset ~line 362). Advance it at HALF the fundamental increment:
    gen_sub_phase += 0.5f*gen_freq/OMEGA_SR (wrap at 1.0). Read the SAME morphed
    wavetable position as the fundamental (reuse wa/wf) at gen_sub_phase, then
    osc = osc + 0.45f*sub BEFORE the wavefolder/env so existing fold headroom and
    gen_env apply to the summed signal. Keep RT-safe (no transcendental added; the
    division gen_freq/OMEGA_SR already exists per-sample for the fundamental — reuse
    the computed increment, do not add a second divide: compute
    `float inc = gen_freq / OMEGA_SR;` once and use inc and 0.5f*inc). Confirm no
    added clipping (the x/(1+|x|)-style fold + bounded env keep output in range) and
    no aliasing concern at the top of ROOT (sub is an octave LOWER, so safe).

    FINDING 7 (match the kick drive): strengthen groove_drive_block (~line 122) so
    full-right DRIVE is noticeably more aggressive and matches the kick FX diode
    character. Increase the pre-gain curve (e.g. k = 1 + drive*8..12 instead of
    drive*4) and keep output-gain compensation (makeup mk) so raising DRIVE changes
    CHARACTER not just level — mirror the kick's out_gain = 1/(1+amt*comp) form. Keep
    the asymmetric diode transfer (even-harmonic grit). All math stays inside
    groove_drive_block which is called from groove_tick's route loop — but it uses
    only algebraic ops (no powf/expf), so it is RT-safe as-is; do NOT introduce any
    transcendental. Preserve bounded output (the x/(1+x) forms self-limit).
    Applies to BOTH GEN and TAPS (same drive block, same route loop).
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && grep -q gen_sub_phase src/groove.h && make test-groove test-taps-redesign test-gen</automated>
  </verify>
  <done>groove.h has gen_sub_phase; GEN osc sums a 0.45-blend sub-octave (reads at half increment) before fold/env; groove_drive_block pre-gain strengthened (k>=1+drive*8) with makeup compensation and no new transcendental; make test-groove/test-taps-redesign/test-gen all GREEN (bounded, finite, GEN determinism preserved).</done>
</task>

<task type="auto" tdd="true">
  <name>Task 4: GEN filter envelope + 18 dB/oct resonant filter + filter-decay 65% of amp decay (Finding 4) + DECAY knob curve (Finding 5)</name>
  <files>src/groove.c, src/groove.h</files>
  <behavior>
    - DECAY curve (Finding 5): at the same knob value v, the new tau_s is SHORTER than
      the old 0.005*powf(30,v) for mid-knob (e.g. v=0.5), proving the musical short
      range is spread across more travel; endpoints stay sane (v=0 ~a few ms, v=1
      still reaches the long tail).
    - Filter decay (Finding 4): the GEN filter envelope coefficient decays FASTER than
      the amplitude env (filter tau ~= 0.65 * amp tau), so the filter closes before the
      note fully decays.
    - Resonant 18 dB/oct (Finding 4): a GEN DECAY sweep / note render stays finite and
      bounded in [-2,2] pre-clamp at the resonant corner (no runaway from the added
      resonance), and the filtered output differs measurably from the non-resonant
      1-pole baseline (the extra poles + resonance change the spectrum).
    - Filter opens ~20%: the per-note filter env adds a modest positive cutoff offset
      (~0.2 of range) at note onset, decaying back — verified by a non-zero early-vs-late
      brightness difference within a single note.
  </behavior>
  <action>
    All coefficient math at CONTROL rate (set_param); per-sample render stays
    transcendental-free (TPT SVF idiom per CLAUDE.md; tanf only in tpt_g_from_hz at
    control rate).

    FINDING 5 (DECAY curve) in groove_set_param PK_GRV_GSWING (~line 646): apply an
    input curve before the tau map so the short/plucky region spreads across the knob.
    Remap v' = v*v (expo), then tau_s = 0.005f*powf(30.0f, v') (keep the same 5 ms..
    ~150 ms endpoints; v'=0 -> 5 ms, v'=1 -> 150 ms, but mid-knob now lands much
    shorter). Recompute gen_env_coef = expf(-1/(tau_s*SR)) as today (control rate).

    FINDING 4 (filter envelope + resonance + 65% filter decay):
    1. Add GEN filter state to groove_state_t (src/groove.h before closing brace):
       `float gen_filt_env;` (per-note filter env, 0..1), `float gen_filt_env_coef;`
       (filter decay coef), and a THIRD TPT state pair for the 3rd pole:
       `float gen_filt_lp3_l_s, gen_filt_lp3_r_s;` (reuse color_lp_*_s + color_lp2_*_s
       for the first two poles). Zero all in groove_init.
    2. Filter decay = 65% of amp decay: in PK_GRV_GSWING, after computing the amp
       tau_s, derive the filter tau: filt_tau_s = 0.65f * tau_s; and
       gen_filt_env_coef = expf(-1/(filt_tau_s*SR)) (control-rate expf, alongside the
       amp coef). The filter env is thus SHORTER than the amp env.
    3. Filter env trigger: where the GEN note fires (gen_env=1.0 at ~line 361), also
       set gen_filt_env = 1.0f. Decay it per sample next to gen_env:
       gen_filt_env *= gen_filt_env_coef (RT-safe multiply).
    4. 18 dB/oct resonant filter for the GEN path (replace the current 1-pole GEN
       COLOR block ~line 500-509): cascade THREE TPT 1-pole LP stages (3-pole = 18 dB/
       oct) sharing the COLOR coefficient cg, and add resonance via a bounded feedback
       term. Resonance ~20%: feed back the 3rd-stage output into the first stage input
       scaled by a fixed k (reso ~0.2 -> modest Q); clamp the feedback so the stage
       stays stable/bounded (hard-limit the pre-stage sum, e.g. x = clampf(in -
       k*lp3_prev, -2, 2)). Modulate the effective cutoff by the filter env: open the
       cutoff ~20% at onset — e.g. cg_eff = cg scaled up by (1 + 0.2*gen_filt_env)
       clamped to the stable tpt coefficient range (as the existing LFO path clamps cg
       to [1e-4, 0.99]). Keep TAPS (2-pole cascade) and the GRV_FILT_OFF bypass paths
       BYTE-IDENTICAL — only the GEN branch changes. The HP variant for GEN = input -
       LP as today.

    RT-SAFETY recheck: the new per-sample work is adds/mults + one clampf + the TPT
    stages (all algebraic). expf/powf/tanf remain exclusively in set_param /
    tpt_g_from_hz (control rate). No malloc, no I/O. Output bounded by the clamped
    feedback + the downstream MONO/VOL + the final omega_to_i16 clamp.

    Extend tests/test_groove.c with a GEN filter-env + DECAY-curve case that renders a
    GEN note and asserts: finite/bounded output at the resonant corner; the new mid-knob
    DECAY tau is shorter than the old map (compute both, compare); the resonant GEN
    filter output differs from a 1-pole baseline render. Wire nothing new into the
    Makefile (test_groove already linked).
  </action>
  <verify>
    <automated>cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && grep -q gen_filt_env src/groove.h && make test-groove test-gen test-switch</automated>
  </verify>
  <done>groove.h has gen_filt_env + gen_filt_env_coef + the 3rd-pole state; GEN DECAY uses an expo input curve (shorter mid-knob tau); GEN filter is a 3-pole (18 dB/oct) resonant TPT cascade with a ~20% onset env opening and filter decay = 0.65*amp decay; render stays transcendental-free and bounded; test_groove gains the filter-env/DECAY-curve case; make test-groove/test-gen/test-switch all GREEN.</done>
</task>

</tasks>

<verification>
Run the full native suite to confirm no regressions across all harnesses:

    cd "/Users/jamesfish/Vibecoding Projects/Schwung/Omega" && make test

Manual / on-device (cannot be automated here — aarch64 build is Docker/CI, device
listening required):
- Hold a step on Move + turn any Omega knob -> value locks, NO "can't automate this".
- GEN: switch scales repeatedly -> root pitch stays stable/low (no jump to the top).
- GEN SEQ LEN knob -> shows whole integers 1..64, no decimals.
- GEN voice -> audibly weightier low end (built-in sub) with no new control.
- GEN/TAPS DRIVE full-right -> aggressive, comparable to the kick FX drive.
- GEN filter -> opens then closes within the note (short env), resonant character,
  filter tail shorter than the note tail.
- GEN DECAY knob -> useful short/plucky range across most of the travel.

aarch64 gate (CI authoritative): `scripts/glibc_gate.sh` on the Docker-built dsp.so;
confirm module.json is still valid JSON and <= 64 KB.
</verification>

<success_criteria>
- module.json carries a valid static chain_params array (no ui_hierarchy key added),
  versions bumped to 0.3.0 in module.json + release.json.
- GEN root is pitch-stable across scale switches (normalized ROOT descriptor in both
  modes); SEQ LEN reads as integers 1..64.
- GEN voice has a built-in sub-octave (no new param); GEN/TAPS drive matches the kick.
- GEN filter is a 3-pole resonant TPT with a ~20% onset envelope and 65%-of-amp filter
  decay; DECAY knob curve spreads the short range.
- `make test` fully GREEN; all audio-thread edits stay malloc/transcendental-free
  (powf/expf/tanf only at control rate), bounded output.
</success_criteria>

<output>
After completion, create `.planning/quick/261001-wft-fix-batch-of-omega-gen-taps-issues-plus-/261001-wft-SUMMARY.md`
</output>
