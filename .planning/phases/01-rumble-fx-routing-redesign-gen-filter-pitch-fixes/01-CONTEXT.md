# Phase 1: Rumble/FX Routing Redesign + GEN Filter & Pitch Fixes - Context

**Gathered:** 2026-10-01
**Status:** Ready for planning
**Source:** User on-device listening feedback (post-v1.1) + orchestrator code investigation

<domain>
## Phase Boundary

This phase fixes the groove/rumble subsystem based on on-device listening after v1.1 completion. Three bodies of work:

1. **Redesign the TAPS "smeared" rumble from first principles of techno rumble sound design.** The current C1/vhr implementation (a `fb_amount` resonant feedback recirculation loop up to 0.85, with in-loop allpass diffusion + 2-pole LP + a Schroeder reverb whose wet is fed *back into the same feedback ring*) sounds "bit-crushed and terrible" and spikes CPU into audible crackle, especially when reverb is added. Tear the resonant-feedback drone architecture out entirely and replace it with a cleaner, CPU-cheap, unconditionally-stable rumble.

2. **Add a user-selectable signal-routing order for the groove FX chain.** e.g. `REVERB → TAPS → DRIVE`, `TAPS → DRIVE → REVERB`, etc. The user explicitly likes "being able to change routing of our sources and effects."

3. **GEN groove fixes:** (a) GEN has no filter at all — add the single continuous log LP sweep 30 Hz–20 kHz (ONDEVICE_FEEDBACK #14) shared by TAPS and GEN; (b) GEN unquantized lowest pitch is far too high and the ROOT control does nothing audible when scale = Unquantized (#20/#21) — fix root-pitch-in-Hz behavior so the root fundamental is actually heard and reaches a true sub-bass register.

**Out of scope this phase** (do NOT touch unless trivially required): kick models, the Performer chain (duck/DJ filter/clip), sample infrastructure, and the legacy `MODEL_GEN` kick voice. GEN-removed-from-kick-list (#13) and other ONDEVICE items are NOT part of this phase.

**Hard constraints (global, apply to every task):**
- RT-safe `render_block`: zero malloc/free, zero file I/O, zero blocking, zero mutex, zero per-sample division or transcendentals (`sinf/expf/powf/tanf/floorf-ok`). All `powf/tanf/expf` at control rate only (`groove_set_param` / `groove_update_tempo` / a control-rate config fn).
- All buffers pre-allocated by value on `groove_state_t` inside the single `bohm_instance` calloc. No new allocations. Respect the instance-size `_Static_assert`.
- Host ABI LOCKED: `host_api_v1_t` / `plugin_api_v2_t` struct layouts + their `_Static_assert`s must not change. `groove_state_t` may grow (it's inside the instance); keep the size assert satisfied.
- Fixed 44.1 kHz, aarch64 Cortex-A53 target, cross-compiled in `ghcr.io/charlesvestal/schwung-builder:latest`; `objdump -T` gate: no GLIBC symbol newer than 2.35.
- `get_param`/`ui_hierarchy` served no-alloc, locale-independent (never libc `%f`).
- CPU budget: full chain ≤ ~10–15% on-device. The redesign MUST be cheaper than (or equal to) the current feedback+diffusion+reverb path, and MUST NOT be able to run away / build unbounded energy under any knob combination.

</domain>

<decisions>
## Implementation Decisions

### Rumble core redesign (LOCKED intent — DSP specifics are Claude's discretion, see research)
- **Remove the resonant-feedback drone entirely.** Delete the `fb_amount` recirculation path, the in-loop Schroeder allpass diffusion (`ap1/ap2`), the in-loop 2-pole feedback LP, the ~30 Hz feedback-path HP, and the reverb-send-into-the-ring (`rv_pre_amt` feeding `wl/wr`). No signal path may write its own (possibly-reverberated) output back into the delay ring. This is the specific thing that "clearly doesn't work."
- **New TAPS model = delay-tap ghost-kick with per-tap/per-16th shaper envelopes where LENGTH is the envelope decay time.** The user's words: "taps (16th shaper envelopes where len is decay of envelope)." Concretely: the kick is written to the delay ring; rumble is produced by reading ghost copies at 16th-note tap offsets, each copy shaped by an amplitude decay envelope whose decay length is set by LENGTH. Short LENGTH → distinct, separated ghost-kick plucks; long LENGTH → overlapping, smeared rumble. This is the classic feedback-delay techno rumble but realized with *bounded, retriggered decay envelopes* instead of an unbounded recirculating feedback loop — so it cannot run away.
- **LENGTH is bidirectional/decay-length** consistent with the existing "smear ↔ distinct" intent already recorded in memory (feedback_taps_redesign). Preserve the "clean distinct copies at one extreme, smeared continuous rumble at the other" musical range, but achieve it via envelope decay + tap overlap, NOT feedback resonance.
- **Loudness:** the rumble must be level-competitive with the kick without cranking VOL to 100 + drive (ONDEVICE #3 "way too quiet"). Use equal-power / makeup normalization at control rate so a musical default VOL produces an audible rumble.
- **Stability is non-negotiable:** with ANY combination of LENGTH, taps, drive, reverb, and routing, output must stay finite and bounded (no NaN/Inf, no unbounded growth). This is the CPU-crackle fix.

### FX routing selector (LOCKED)
- Add a discrete **ROUTING** enum param (new `grv_route` / `PK_GRV_ROUTE`, new `GKI_GRV_ROUTE`, UP_ENUM) on the shared "Groove Effects" page selecting the order the three groove blocks are applied. Blocks: **RUMBLE core (taps or gen)**, **DRIVE (+ its FX: LFO)**, **REVERB**. At minimum offer the two the user named plus sensible others, e.g.:
  - `RUMBLE→DRIVE→REVERB` (default — reverb last, classic)
  - `REVERB→RUMBLE→DRIVE`
  - `RUMBLE→REVERB→DRIVE`
  - `DRIVE→RUMBLE→REVERB`
  (Final option list is Claude's discretion; expose the two explicitly named — reverb-first and reverb-last — at minimum.)
- The reverb becomes a normal in-line block in the selected order — NEVER fed back into the rumble delay ring. One Schroeder reverb instance is fine; keep RV DECAY/TONE/TYPE controls. The bidirectional PRE/POST "RV MIX" hack (`rv_pre_amt`/`rv_post_amt`) is REPLACED by: a plain reverb MIX (0..1) + the ROUTING selector deciding order. Reverb comb feedback must be clamped so decay stays bounded (< 1.0, no runaway).
- Routing must be sample-accurate-safe: selecting order is a control-rate decision; the per-sample loop dispatches on the stored order (small switch / function-pointer-free branch), no per-sample transcendental.

### GEN filter (LOCKED — ONDEVICE #14)
- Both TAPS and GEN must expose a single continuous **LP sweep 30 Hz → 20 kHz with a log/exp curve** (lots of usable range in lows/low-mids). TAPS already has this via `PK_GRV_COLOR` on its `groove1` page (30 Hz..20 kHz log LP). GEN currently exposes NO filter knob anywhere (`Gen Groove` page = TYPE/VOL/SCALE/ROOT/RANGE/RETRIG; shared `Groove Effects` page = DRIVE/LFO/REVERB). Fix by exposing the same COLOR/FILTER LP sweep to GEN — preferred: add a FILTER knob to the shared "Groove Effects" page (`P_GROOVE_FX`) so BOTH types get it in a consistent location; reconcile with TAPS's existing COLOR so there is exactly one filter control per type with no duplicate/conflicting knob. The DSP LP already exists (`color_g` + `tpt1_lp`); this is primarily a UI-exposure + param-wiring fix, keeping it RT-safe.

### GEN unquantized pitch/root (LOCKED — ONDEVICE #20/#21)
- Root cause (confirmed in code): in `groove_tick`'s GEN branch the per-step offset `semi = gen_seq[step]` is `0..(range-1)` — strictly **additive above** the root, so the sequence never plays at or below the root and the fundamental floats too high (default root ≈122 Hz + up to +23 semitones). Fix so:
  - The **ROOT fundamental is actually audible** — make the generated offsets centered/bidirectional around the root (e.g. roughly ±range/2) OR otherwise ensure the root note is heard, so ROOT-in-Hz genuinely controls perceived pitch in unquantized mode.
  - The **lowest reachable pitch is a true sub-bass** — the user says the current lowest is "way too high." Extend the unquantized ROOT HZ range downward (candidate floor ~20–30 Hz) and ensure the actual sounding pitch can sit in the 30–80 Hz rumble register, not 120 Hz+.
  - Keep the quantized-scale path working as before; this fix is specific to musical low-register behavior and the unquantized ROOT HZ mapping.
- Verify ROOT is exposed and functional on the GEN groove page in BOTH modes (it already swaps label ROOT↔ROOT HZ via `ui_emit_gen_groove1`); confirm the value actually changes perceived pitch after the offset fix.

### Testing (LOCKED — native offline harness, no hardware)
- Extend `tests/test_groove.c` / add cases proving: (1) NO runaway — feed a kick + max LENGTH + max drive + max reverb + every routing order, render many blocks, assert finite + bounded (no growth over time); (2) rumble is audible at a musical default VOL (energy above a floor, level-competitive — not near-silent); (3) LENGTH morphs distinct↔smeared (measurable); (4) routing order actually changes output (orders produce different buffers); (5) GEN exposes a filter that sweeps (LP audibly attenuates highs as it closes); (6) GEN unquantized ROOT HZ changes perceived pitch and reaches a low fundamental (spectral/zero-crossing or autocorrelation-pitch check at low vs high ROOT HZ). Keep the malloc-trap + isfinite + int16-clamp guards green. Full `make test` suite must stay GREEN.
- Determinism: GEN seeded paths remain byte-stable for a fixed seed.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents (researcher, planner, executor) MUST read these before planning or implementing.**

### Current groove/rumble implementation (the code being redesigned)
- `src/groove.c` — `groove_tick` (per-sample rumble: TAPS feedback loop @343-451, GEN voice @292-342, filter @459-498, drive @501-508, reverb @510-519, LFO @521-529), `groove_set_param` (control-rate mappings @538-655), `groove_reverb_mono` (@56-71), `groove_set_length` (@95-99), `groove_update_tempo` (@224-282), `groove_init` (@145-218). The `fb_amount`/`rv_pre_amt`/`ap1`/`ap2`/in-loop LP+HP are what to remove.
- `src/groove.h` — `groove_state_t` layout (grow here, respect size assert), enums `GROOVE_TYPE_*`, `GRV_FILT_*`, `GRV_RETRIG_*` (@125-128).
- `src/params.h` — `pk_global_index_t` GKI_* enum (@61-78, add `GKI_GRV_ROUTE` here), `PK_GRV_*` string macros (@263-294), `g_global_defaults`.
- `src/params.c` — global key↔index tables + defaults (keep in sync with any new key).
- `src/ui.c` — `P_GROOVE1` (@176), `P_GROOVE_FX` shared effects page (@195, add FILTER + ROUTE here), `ui_emit_gen_groove1` (@278-309, GEN page 1), groove page dispatch (@350-378). Emit new enum via the `UP_ENUM` + `OPT_*` pattern (see `OPT_SCALE`/`OPT_GRVTYPE`/`OPT_RETRIG`).
- `src/dsp_primitives.h` / `.c` — `tpt1_lp`/`tpt1_t` TPT one-pole, `OMEGA_SR`, `wt_read_bl`, `scale_quantize`, wavetable/scale tables. Reuse; do not reinvent filters.
- `src/dsp.c` — how `groove_tick`/`groove_update_tempo` are called from `render_block`; `on_midi` (GEN On-Note retrig); the `is_groove_key` routing of `grv_*` keys.

### Reference DSP modules (study for algorithm + param shapes; do NOT link/copy binaries)
- `audiofx/module.json`, `audiofx/tapedelay.so` — TapeDelay: feedback/time/tone/width param design for a musical delay (tempo-sync divisions, tone LP on repeats, ping-pong). Good reference for a bounded, musical delay-feedback control taper.
- `audiofx/freeverb.so`, `audiofx/psxverb.so` + their `module (N).json` descriptors — reverb param ranges (mix/decay/damping/tone) and comb/allpass tunings. Reference for a stable in-line reverb block and its control taper.
- `audiofx/ducker.so`, `audiofx/palette.so`, `audiofx/ui_chain.js` — chaining/ui-hierarchy conventions (how routable FX blocks present params).

### Project docs
- `.planning/ONDEVICE_FEEDBACK_v1_1.md` — items #3 (TAPS quiet/bit-crushed), #14 (TAPS+GEN single LP sweep), #20/#21 (GEN unquantized scale + root-pitch-in-Hz), #23/#24 (GEN page layout).
- `.planning/STATE.md` — "[C1] Groove redesign" decision (@line 83) documents the feedback loop being removed; memory `feedback_taps_redesign.md` (bidirectional LENGTH smear↔distinct, pre/post reverb routing intent, duck fix).
- `CLAUDE.md` — tech-stack + RT-safety + cross-compile gotchas (authoritative constraints).

</canonical_refs>

<specifics>
## Specific Ideas

- Techno rumble first principles (for the researcher to expand with web sources): a rumble is a kick fed into a tempo-synced feedback delay (often 1/16 or dotted), the repeats pitched low and low-pass filtered so successive ghost-kicks blur into a continuous sub-bass drone that "pumps" against the kick. Classic recipes: (1) kick → short 1/16 delay with moderate feedback + LP → rumble; (2) kick → reverb → gate/duck → the reverb tail IS the rumble; (3) resonant/pitched delay. The user wants ours realized as **decay-enveloped ghost-kick taps** (bounded) plus a **routable reverb**, so both the "delay rumble" and "reverb rumble" recipes are reachable via the ROUTING selector without any unbounded feedback.
- The user is explicitly OK moving away from strictly copying Bohm+Groove toward "our own flexible yet good-sounding yet simple enough for Move's limited CPU" design.
- Suspected CPU crackle mechanism (state for the researcher to confirm): resonant feedback ring (fb≤0.85) + reverb comb feedback (≤0.99) + reverb wet summed back into the ring input = a compounding feedback network that can accumulate energy and drive values toward denormal/large ranges, inflating per-sample cost and clipping — audible crackle. Removing the into-ring reverb send and the recirculating feedback removes the mechanism.

</specifics>

<deferred>
## Deferred Ideas

- ONDEVICE_FEEDBACK #13 (remove GEN from the kick-model selector), #23/#24 fine-grained GEN page relabeling beyond adding the filter, and any non-groove items (#1,#2,#4-#12,#15-#17,#25-#33) — NOT this phase.
- Multi-tap independent per-tap routing, additional reverb algorithms (plate/hall variants beyond RV TYPE), and NEON vectorization — only if profiling later demands it.

</deferred>

---

*Phase: 01-rumble-fx-routing-redesign-gen-filter-pitch-fixes*
*Context gathered: 2026-10-01 by orchestrator from user feedback + code investigation*
