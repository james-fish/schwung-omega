# Phase 1: Rumble/FX Routing Redesign + GEN Filter & Pitch Fixes - Research

**Researched:** 2026-10-01
**Domain:** Real-time C DSP — techno rumble synthesis, feedback-free delay networks, in-line reverb routing, generative pitch mapping (Ableton Move / Schwung, aarch64)
**Confidence:** HIGH (code grounded + DSP recipes cross-verified with production + academic sources)

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Rumble core redesign (LOCKED intent — DSP specifics are Claude's discretion):**
- **Remove the resonant-feedback drone entirely.** Delete the `fb_amount` recirculation path, the in-loop Schroeder allpass diffusion (`ap1/ap2`), the in-loop 2-pole feedback LP, the ~30 Hz feedback-path HP, and the reverb-send-into-the-ring (`rv_pre_amt` feeding `wl/wr`). No signal path may write its own (possibly-reverberated) output back into the delay ring.
- **New TAPS model = delay-tap ghost-kick with per-tap/per-16th shaper envelopes where LENGTH is the envelope decay time.** Kick written to the ring; rumble = ghost copies read at 16th-note tap offsets, each shaped by an amplitude decay envelope whose decay length = LENGTH. Short LENGTH → distinct separated ghost-kick plucks; long LENGTH → overlapping smeared rumble. Bounded, retriggered decay envelopes, NOT unbounded recirculating feedback.
- **LENGTH is bidirectional/decay-length** (clean distinct copies ↔ smeared continuous rumble) achieved via envelope decay + tap overlap, NOT feedback resonance.
- **Loudness:** rumble must be level-competitive with the kick at a musical default VOL (equal-power / makeup normalization at control rate). (ONDEVICE #3.)
- **Stability non-negotiable:** with ANY combination of LENGTH/taps/drive/reverb/routing, output stays finite + bounded (no NaN/Inf, no unbounded growth).

**FX routing selector (LOCKED):**
- Add a discrete **ROUTING** enum (new `grv_route` / `PK_GRV_ROUTE` / `GKI_GRV_ROUTE`, UP_ENUM) on the shared "Groove Effects" page selecting the order of {RUMBLE core, DRIVE (+its FX: LFO), REVERB}. Must expose reverb-first AND reverb-last at minimum. Suggested set: `RUMBLE→DRIVE→REVERB` (default), `REVERB→RUMBLE→DRIVE`, `RUMBLE→REVERB→DRIVE`, `DRIVE→RUMBLE→REVERB`. Final list is Claude's discretion.
- The reverb becomes a normal in-line block in the selected order — NEVER fed back into the rumble ring. One Schroeder instance. Keep RV DECAY/TONE/TYPE. The bidirectional PRE/POST "RV MIX" hack (`rv_pre_amt`/`rv_post_amt`) is REPLACED by a plain reverb MIX (0..1) + the ROUTING selector deciding order. Reverb comb feedback clamped < 1.0.
- Routing is a control-rate decision; per-sample loop dispatches on stored order (small switch / branch), no per-sample transcendental.

**GEN filter (LOCKED — ONDEVICE #14):**
- Both TAPS and GEN expose ONE continuous LP sweep 30 Hz→20 kHz, log/exp curve. TAPS already has this via `PK_GRV_COLOR`. GEN currently exposes NO filter knob. Add the same COLOR/FILTER LP sweep to GEN — preferred: add a FILTER knob to the shared "Groove Effects" page so BOTH types get it in a consistent location; reconcile with TAPS's COLOR so there is exactly one filter control per type, no duplicate. DSP LP already exists (`color_g` + `tpt1_lp`); primarily a UI-exposure + param-wiring fix, RT-safe.

**GEN unquantized pitch/root (LOCKED — ONDEVICE #20/#21):**
- Root cause confirmed in code: per-step offset `semi = gen_seq[step]` is `0..(range-1)` — strictly additive above the root; sequence never plays at or below the root; fundamental floats too high.
- Make generated offsets centered/bidirectional around the root (~±range/2) so the ROOT fundamental is audible and ROOT-in-Hz genuinely controls perceived pitch in unquantized mode.
- Extend unquantized ROOT HZ range downward (candidate floor ~20–30 Hz); actual sounding pitch must reach 30–80 Hz rumble register, not 120 Hz+.
- Keep quantized-scale path working. Confirm ROOT is exposed + functional in BOTH modes.

**Testing (LOCKED — native offline harness, no hardware):**
- Extend `tests/test_groove.c` / add cases proving: (1) NO runaway (kick + max LENGTH + max drive + max reverb + every routing order, render many blocks, finite + bounded, no growth); (2) rumble audible at musical default VOL; (3) LENGTH morphs distinct↔smeared (measurable); (4) routing order changes output; (5) GEN filter sweeps (LP audibly attenuates highs); (6) GEN unquantized ROOT HZ changes perceived pitch + reaches a low fundamental. Keep malloc-trap + isfinite + int16-clamp guards green. Full `make test` GREEN. GEN seeded paths byte-stable for a fixed seed.

**Hard constraints (global):** RT-safe render (zero malloc/free/IO/blocking/mutex, zero per-sample division/transcendental — all powf/tanf/expf at control rate); all buffers pre-allocated by value in the single calloc; respect the instance-size `_Static_assert`; host ABI (`host_api_v1_t`/`plugin_api_v2_t` layouts + asserts) LOCKED; fixed 44.1 kHz aarch64 Cortex-A53, cross-compiled in `schwung-builder:latest`, `objdump -T` gate (no GLIBC > 2.35); `get_param`/`ui_hierarchy` no-alloc + locale-independent; CPU ≤ ~10–15%, new path MUST be cheaper-or-equal and cannot run away.

### Claude's Discretion
- Exact rumble DSP math (envelope shape, tap count/offsets, normalization scheme).
- Final routing option list (beyond the two named).
- Precise Hz floor/mapping for the GEN unquantized root.

### Deferred Ideas (OUT OF SCOPE)
- ONDEVICE #13 (remove GEN from kick-model selector), #23/#24 fine-grained GEN page relabeling beyond adding the filter, all non-groove items (#1,#2,#4–#12,#15–#17,#25–#33).
- Multi-tap independent per-tap routing, additional reverb algorithms beyond RV TYPE, NEON vectorization — only if profiling later demands.
- Kick models, Performer chain (duck/DJ filter/clip), sample infra, legacy `MODEL_GEN` kick voice — do NOT touch.
</user_constraints>

<phase_requirements>
## Phase Requirements

No IDs pre-assigned. The four LOCKED deliverables from CONTEXT.md `<decisions>` are the requirement set:

| ID (working) | Description | Research Support |
|--------------|-------------|------------------|
| RUMBLE-CORE | Replace the resonant-feedback TAPS drone with bounded decay-enveloped ghost-kick taps (LENGTH = decay) | §Rumble Core Algorithm — exact per-sample math, bounded-by-construction proof, field-by-field mapping onto `groove_state_t` |
| FX-ROUTE | User-selectable {RUMBLE, DRIVE, REVERB} order; reverb an in-line block; plain MIX replaces PRE/POST hack | §FX Routing Selector — control-rate order table, per-sample dispatch, new GKI/PK/OPT, ui.c placement |
| GEN-FILTER | Expose the 30 Hz–20 kHz log LP sweep to GEN (reuse `color_g`/`tpt1_lp`), one filter control per type | §GEN Filter Exposure — reuse existing COLOR DSP, move FILTER to shared FX page, reconcile with TAPS COLOR |
| GEN-PITCH | Center offsets around the root (root audible) + extend unquantized ROOT HZ to true sub-bass | §GEN Unquantized Pitch/Root Fix — exact `gen_seq` centering + Hz mapping change, both modes verified |
</phase_requirements>

---

## Summary

The current TAPS rumble is an **unbounded recirculating feedback delay** (`fb_amount` up to 0.85) stacked with **in-loop Schroeder allpass diffusion**, an **in-loop 2-pole LP**, a **~30 Hz feedback HP**, and — the killer — a **reverb wet signal summed back into the same delay ring** (`rv_pre_amt` → `wl/wr`). Two nested feedback loops (ring fb ≤ 0.85 + reverb comb fb ≤ 0.99) sharing energy is a compounding feedback network. On a Cortex-A53 the decaying tails fall into denormal range, and even with FPCR flush-to-zero set, the interaction of two high-feedback loops can accumulate energy toward large/oscillatory values — the "bit-crushed hot mess" + CPU crackle the user hears. (The CPU-crackle mechanism is independently confirmed: stacked feedback effects that decay to denormals cost 10–100× per op on ARM64 without FTZ, and energy buildup in coupled loops pushes values out of the musical range — see Sources.)

The fix is a **feedback-free architecture**. Write only the RAW kick into the ring. Produce rumble as a **sum of ghost-kick taps** read at successive 16th-note offsets, each tap weighted by a **decay envelope sampled at its own delay age**. LENGTH sets the decay time: short → the older taps are near-silent so you hear distinct separated copies; long → the older taps are still loud so copies overlap into a smeared continuous drone. There is no recirculation: every output sample is a finite weighted sum of a **finite number of past ring samples**, each weight in [0,1], so the output is bounded by `(kick_peak) × Σweights × makeup` — **bounded by construction, provably cannot run away**. The reverb becomes a **plain in-line block** (dry + MIX·wet) placed by a **control-rate ROUTING enum** that reorders {RUMBLE, DRIVE, REVERB} in the per-sample loop via a small precomputed order array. GEN's filter is a pure UI/param wiring change (the LP DSP already exists as `color_g`+`tpt1_lp`). GEN pitch is fixed by centering the per-step offset around the root and mapping the unquantized ROOT to a sub-bass Hz range.

**Primary recommendation:** Rip out the entire TAPS feedback/diffusion/reverb-into-ring block (`groove.c` lines ~343–431 plus the `groove_reverb_mono` PRE call and the `set_length` feedback mapping). Replace with a ring of RAW kick + a 4-to-8 tap decay-enveloped sum. Make the reverb a dry/wet in-line block driven by a plain `PK_GRV_RVMIX` 0..1 and inserted via a `PK_GRV_ROUTE` enum-ordered chain. Wire GEN's FILTER on the shared FX page and center the GEN offset.

---

## Project Constraints (from CLAUDE.md)

Directives the planner MUST enforce (same authority as locked decisions):

- **C11 (`-std=gnu11`), C only** — no C++, no STL, no `<threads.h>`. `_Static_assert`/`_Alignas`/anonymous unions allowed.
- **Audio thread**: zero malloc/free/new/delete, zero file I/O, zero blocking, zero mutex, **zero per-sample transcendental or division**. `powf/tanf/expf/sinf/cosf` at control rate ONLY (`groove_set_param` / `groove_update_tempo` / a control-rate config fn). `floorf`/`roundf` per-sample are OK (single aarch64 instruction).
- **Memory**: all buffers pre-allocated by value on `groove_state_t` inside the ONE `bohm_instance` calloc. No new allocations. Respect `_Static_assert(sizeof(struct bohm_instance) < 2200000)`.
- **Denormals**: FPCR FZ bit already set in `render_block` (`omega_set_ftz`, `dsp.c:470`). Keep relying on it; do NOT add a math-flag that toggles it globally.
- **Numeric**: 32-bit `float` throughout. int16 ↔ float only at the I/O boundary (`omega_to_i16`).
- **Filters**: TPT one-pole (`tpt1_lp`) for sweeps — never a biquad for a modulated cutoff. Reuse `dsp_primitives.h`, don't reinvent.
- **PRNG**: xorshift only (`grv_xorshift` / `prng_t`), never `rand()`.
- **Build**: single `dsp.so`, GNU Make in Docker `ghcr.io/charlesvestal/schwung-builder:latest`. Granular fast-math subset (`-fno-math-errno -ffp-contract=fast`), NOT blanket `-ffast-math`. `-Wl,--no-undefined`, `-fvisibility=hidden`. **objdump `-T` gate: no GLIBC symbol > 2.35, no `_ZGV*`/libmvec.**
- **Host ABI LOCKED**: `host_api_v1_t`/`plugin_api_v2_t` layouts + their `_Static_assert`s (offset 120 / 56) must not change. `groove_state_t` may grow (it's inside the instance).
- **`get_param`/`ui_hierarchy`**: no-alloc, locale-independent (never libc `%f`; use `pk_format_value`). Note: `get_param` runs on the SPI audio callback — **no `host->log` of any kind** (device-wide dropouts).

---

## Current Implementation — What to Remove vs Keep

Grounded in `src/groove.c` / `src/groove.h` as they exist today (the vhr "260930-vhr" redesign is already merged; this phase removes the feedback core it still contains).

### REMOVE (the resonant-feedback drone — the "clearly doesn't work" path)

| Location | Code | Why remove |
|----------|------|------------|
| `groove.c:95–99` `groove_set_length` | `g->fb_amount = 0.85f*(1-v)` + `diffuse_amt` | LENGTH must drive **envelope decay**, not feedback gain |
| `groove.c:358–365` step (2) | feedback tap read at 1·spq | feeds the recirculation |
| `groove.c:367–379` step (3) | ~30 Hz feedback-path HP (`loop_hp_*`) | part of the feedback loop |
| `groove.c:381–401` step (4) | in-loop Schroeder allpass diffusion (`ap1/ap2`) | part of the feedback loop |
| `groove.c:403–414` step (5) | COLOR-linked 2-pole in-loop LP (`fb_lp_*`) | part of the feedback loop |
| `groove.c:416–428` step (6) | `wl = kick + fb_amount*fb_l` **+ reverb-into-ring** (`rv_pre_amt`) | THE runaway: writes (reverberated) output back into the ring |
| `groove.c:422–428` + `groove.c:510–519` + `set_param` RVMIX `:576–591` | bidirectional PRE/POST reverb hack (`rv_pre_amt`/`rv_post_amt`) | replaced by plain MIX + ROUTE enum |
| `groove.h:69–80` | `fb_amount`, `diffuse_amt`, `fb_lp*`, `loop_hp*`, `loop_hp_g`, `ap1[241]/ap2[113]`, `rv_pre_amt`, `rv_post_amt` fields | dead after removal — frees ~1.4 KB of the instance |

### KEEP (verified sound, reused by the redesign)

| Location | Code | Role in redesign |
|----------|------|------------------|
| `groove.c:220–282` `groove_update_tempo` | transport-locked BPM → `samples_per_16th` + `spq_target` (EMA, control-rate re-lock) | UNCHANGED — the tap offsets still come from here |
| `groove.h:46–47` `spq`/`spq_target` + `groove.c:351–356` slew | fractional slewed 16th spacing | KEEP — tap k reads at `k·spq` fractional |
| `groove.c:436–450` step (7) | 4 fractional taps at `k·spq`, linear interp, `& GRV_DELAY_MASK` wrap, `tap_norm` | KEEP the read structure; add per-tap decay envelope weight |
| `groove.c:83–88` `groove_update_tap_norm` | equal-power `1/sqrt(Σgains)` | KEEP + extend to include decay energy for makeup |
| `groove.c:56–71` `groove_reverb_mono` | 2-comb + 1-allpass Schroeder | KEEP — but call it as a plain in-line block (dry+MIX·wet), never into the ring |
| `groove.c:459–498` COLOR filter (2-pole TAPS / 1-pole GEN) | `tpt1_lp` LP/HP/Off | KEEP — this is the shared 30 Hz–20 kHz sweep; expose it to GEN |
| `groove.c:501–508` DRIVE | saturation + makeup, dry/wet | KEEP — becomes a routable block |
| `groove.c:521–529` LFO tremolo | triangle amplitude mod | KEEP (rides with DRIVE block per CONTEXT: "DRIVE +its FX: LFO") |
| `groove.c:531` MONO + `:532` VOL | force-sum + level | KEEP (final stage, after routing) |
| `groove.c:288–342` entire GEN branch | transport-clocked sequencer → osc → fold → env | KEEP; fix pitch centering + expose filter |

**Net memory change:** removing `ap1[241]+ap2[113]` (354 floats ≈ 1.4 KB) and ~10 scalar feedback-state floats. Adding the redesign needs **at most a few scalars** (see §Rumble Core) — so the instance SHRINKS. The `_Static_assert(sizeof < 2200000)` has ~960 KB of headroom (true sizeof ≈ 1.24 MB); no risk.

---

## Rumble Core Algorithm (RUMBLE-CORE)

### The two production recipes (from techno sound-design research)

1. **Delay rumble** (what we build directly): delay the kick, distort/filter it, and you get a "galloping, pulsating, tempo-synced" rumble because you're delaying + filtering copies of the kick — NOT smearing with reverb. Low-pass the repeats to blur successive ghost-kicks into a continuous sub drone. (MusicRadar, Studio Brootle.)
2. **Reverb rumble**: copy the kick, add reverb, EQ out the highs, saturate → a low sub tail between kicks; sidechain/duck it so it pumps. (mastrng.com, Elektronauts.)

Our design reaches **recipe 1 via the tap engine + COLOR LP**, and **recipe 2 via the routable in-line reverb** (§FX Routing). Both without any unbounded feedback.

### Design: decay-enveloped ghost-kick taps

Write **only the raw kick** into the ring (no feedback term). Each render sample, sum `NTAPS` ghost copies read at `k·spq` behind the write head (`k = 1..NTAPS`). Weight tap `k` by a **decay envelope evaluated at that tap's age in seconds** `t_k = (k·spq)/SR`:

```
weight_k = tap_level[k-1] * exp(-t_k / tau)          // tau from LENGTH
rumble  += ring_read_fractional(k·spq) * weight_k
```

`exp` is NOT called per sample — see below. `tau` (decay time constant) is the LENGTH control:

- **LENGTH = 1 (right, "distinct")**: `tau` short (e.g. ~40 ms). At 174 BPM a 16th ≈ 86 ms, so `weight_2 = exp(-172/40) ≈ 0.014` — tap 1 dominates, later taps near-silent → clean separated ghost-kick plucks.
- **LENGTH = 0 (left, "smeared")**: `tau` long (e.g. ~1200 ms). `weight_k` stays high across all taps → heavily overlapping copies → continuous smeared rumble.

Map: `tau_ms = 40 * (1200/40)^(1-v)` = `40 * 30^(1-v)` (exp map, computed at control rate in `groove_set_length`). This is the bidirectional "clean↔smear" LENGTH the user wants, achieved with envelope decay + tap overlap, exactly as specified.

### Keeping it RT-safe (no per-sample `exp`)

The tap weights depend only on control-rate quantities (`spq`, `tau`, `tap_level`). Precompute the whole weight vector **once per block** (or on `set_param`/tempo re-lock) into `g->tap_w[NTAPS]`:

```c
// control-rate (groove_update_tempo tail, or a groove_config called per block)
static void groove_update_tap_weights(groove_state_t *g) {
    float inv_tau = 1.0f / (g->tap_tau_s > 1e-4f ? g->tap_tau_s : 1e-4f);
    float energy = 0.0f;
    for (int k = 1; k <= NTAPS; k++) {
        float age_s = (float)k * g->spq_target / OMEGA_SR;   // seconds
        float w = g->tap_level[k-1] * expf(-age_s * inv_tau); // expf: CONTROL rate
        g->tap_w[k-1] = w;
        energy += w * w;                                      // for equal-power makeup
    }
    // Makeup so a long tail is not louder than a short one, and a short tail is
    // still audible: normalise by sqrt(sum of squared weights), floor the divisor.
    float norm = sqrtf(energy > 0.25f ? energy : 0.25f);
    g->tap_norm = g->rumble_makeup / norm;                   // rumble_makeup ~ tuned gain
}
```

Per-sample render is then pure MACs (no transcendental, no division):

```c
// PER SAMPLE (groove_tick TAPS branch, replacing steps 2-7)
g->spq += (g->spq_target - g->spq) * SPQ_SLEW;               // existing slew
float spq = clampf(g->spq, 1.0f, (float)((int)GRV_DELAY_MASK / NTAPS));
float acc_l = 0.0f, acc_r = 0.0f;
for (int k = 1; k <= NTAPS; k++) {
    float dk  = (float)k * spq;
    float rpk = (float)g->write_pos - dk;
    float fk  = rpk - floorf(rpk);                           // floorf: single instr
    int   a0  = ((int)floorf(rpk)) & GRV_DELAY_MASK;
    int   a1  = (a0 + 1) & GRV_DELAY_MASK;
    float sl  = g->buf_l[a0] + fk * (g->buf_l[a1] - g->buf_l[a0]);
    float sr  = g->buf_r[a0] + fk * (g->buf_r[a1] - g->buf_r[a0]);
    acc_l += sl * g->tap_w[k-1];
    acc_r += sr * g->tap_w[k-1];
}
g->buf_l[g->write_pos] = kick_l;                             // RAW kick only — NO feedback
g->buf_r[g->write_pos] = kick_r;
g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;
gl = acc_l * g->tap_norm;
gr = acc_r * g->tap_norm;
```

### Tap count

CONTEXT keeps four TAP knobs (TAP1..4) on Groove Page 1, so **`NTAPS = 4`** reuses `tap_level[4]` verbatim and keeps the UI unchanged. For a deeper smear the 4 taps at `1·spq..4·spq` cover one beat; the decay tail beyond tap 4 is inaudible at musical `tau` because `weight_4` at `tau=1200ms`, spq≈86ms is `exp(-344/1200)≈0.75` and tap 5 would be `≈0.68` — acceptable to stop at 4 (keeps CPU and the existing knob mapping). **Recommendation: keep NTAPS=4.** If smear feels too gapped at long LENGTH on-device, bump to 6–8 with a fixed `0.6` level for taps 5..8 (discretion; adds only MACs).

### Why it is bounded by construction (the stability proof the phase demands)

Each output sample `= tap_norm · Σ_{k=1..4} w_k · ring[age_k]`, where:
- `ring[·]` only ever holds a RAW kick sample: `|ring| ≤ kick_peak ≤ 1.0` (the kick engines self-limit to [-1,1]).
- `w_k = tap_level_k · exp(-age·/tau) ∈ [0, 1]` (both factors in [0,1]).
- `tap_norm = makeup / sqrt(Σ w_k²)` is finite (divisor floored at 0.5).
- So `|output| ≤ tap_norm · Σ w_k · 1.0`, a **finite constant per block**, independent of time.

There is **no state that feeds its own output back** — the ring is written with the external kick, never with `acc`. Therefore the system is a finite-impulse-response (FIR) comb: **unconditionally stable, cannot accumulate energy, cannot run away**, for any LENGTH/tap/drive/reverb/routing combination. This is the structural fix for both the "bit-crushed" sound and the CPU crackle. (FIR-by-construction is the standard way to get a stable multi-tap rumble; feedback matrices must be < 1 in magnitude to decay, and coupling two such loops — as the current code does — is exactly what an FIR design avoids. See Sources: FDN stability, ARM denormal.)

### New/changed `groove_state_t` fields

| Field | Type | Cost | Purpose |
|-------|------|------|---------|
| `tap_w[4]` | `float[4]` | 16 B | precomputed per-tap decay weights (control rate) |
| `tap_tau_s` | `float` | 4 B | LENGTH-derived decay time constant (seconds) |
| `rumble_makeup` | `float` | 4 B | tuned makeup gain for level-competitiveness (ONDEVICE #3) |
| *(reuse)* `tap_norm` | existing | — | now = makeup/sqrt(Σw²) |
| *(reuse)* `tap_level[4]` | existing | — | per-tap base level, unchanged |

**Removed** (net negative): `fb_amount`, `diffuse_amt`, `fb_lp_l/r_s`, `fb_lp2_l/r_s`, `loop_hp_l/r_s`, `loop_hp_g`, `ap1[241]`, `ap1i`, `ap2[113]`, `ap2i`, `rv_pre_amt`, `rv_post_amt` → frees ~1.44 KB. **Instance shrinks; size assert safe.**

### Loudness (ONDEVICE #3 — "way too quiet")

Set `rumble_makeup` so that at default VOL (currently `GKI_GRV_VOL` default 1.0 in `params.c`, though `groove_init` sets `vol=0.0` — see Open Questions) and a default LENGTH, the rumble RMS is within a few dB of the kick RMS without DRIVE. Because `tap_norm` already equal-power-normalizes, a `rumble_makeup` of ~1.5–2.5 is the tuning knob. The audible-at-default test (§Validation) is the acceptance gate.

---

## FX Routing Selector (FX-ROUTE)

### The three blocks

Per CONTEXT: **RUMBLE core** (taps OR gen output), **DRIVE** (+ its FX: LFO tremolo), **REVERB**. FILTER/COLOR is NOT one of the three reorderable blocks — it is the per-type tone control (§GEN Filter). MONO + VOL are the fixed final stage.

### Reverb becomes a plain in-line block

Replace `rv_pre_amt`/`rv_post_amt` and the `groove_reverb_mono` PRE call inside the TAPS branch with ONE plain block applied wherever ROUTE places it:

```c
static inline void groove_reverb_block(groove_state_t *g, float *l, float *r) {
    float in  = 0.5f * (*l + *r);          // mono-in (rumble is near-mono sub)
    float wet = groove_reverb_mono(g, in); // existing 2-comb+1-allpass
    *l = *l + g->rv_mix * (wet - *l);      // dry/wet by MIX 0..1
    *r = *r + g->rv_mix * (wet - *r);
}
```

`rv_fb` (comb feedback from RV DECAY) is already clamped `0.5..0.99` in `set_param:592` — keep that clamp (< 1.0, decays, cannot run away). The reverb is now driven by a **plain `PK_GRV_RVMIX` 0..1** (change `set_param` RVMIX case from the bidirectional deadzone to `g->rv_mix = v;`).

### Control-rate order, per-sample dispatch (no branches-in-hot-path cost)

Store the selected order as a 3-entry array of block IDs, set once at control rate:

```c
enum { BLK_RUMBLE = 0, BLK_DRIVE = 1, BLK_REVERB = 2 };
// in groove_state_t: unsigned char route_order[3];
// control-rate (set_param PK_GRV_ROUTE): fill route_order from the enum choice.
```

The per-sample tail walks the array and applies each block in order. The rumble/gen core produces `gl,gr` first (it must — it is the source); DRIVE and REVERB are post-source transforms whose ORDER relative to each other and to any FILTER placement is what ROUTE controls. Concretely:

```c
// after the source (taps or gen) fills gl,gr and the COLOR filter runs:
for (int i = 0; i < 3; i++) {
    switch (g->route_order[i]) {
        case BLK_RUMBLE:  break;                         // already the source; no-op slot
        case BLK_DRIVE:   groove_drive_block(g, &gl, &gr);   // saturation + LFO
        case BLK_REVERB:  groove_reverb_block(g, &gl, &gr);  // dry/wet
    }
}
```

The `switch` on a 3-element `unsigned char` array is branch-predictable and transcendental-free; the drive/reverb math is the existing code. **No per-sample `powf`/`tanf`.** (The `BLK_RUMBLE` slot is a no-op because the source is produced before the loop; its position in the array still defines whether DRIVE/REVERB come "before" or "after" it conceptually — for the named orders this reduces to ordering DRIVE vs REVERB, which is what matters audibly. If a true "reverb before the rumble source" is wanted, that is impossible in a single-source-per-sample model; the meaningful reorderings are DRIVE↔REVERB order, which this delivers. See Open Questions.)

### Enum option set (expose reverb-first and reverb-last at minimum)

```c
static const char OPT_ROUTE[] =
  "[\"Rumble>Drive>Reverb\",\"Rumble>Reverb>Drive\","
   "\"Reverb>Rumble>Drive\",\"Drive>Rumble>Reverb\"]";
```

Map each index → a `route_order[3]` permutation in `set_param`. Default = index 0 (`RUMBLE→DRIVE→REVERB`, reverb last, classic). Index 2 satisfies "reverb first".

### Param / UI wiring

| File | Change |
|------|--------|
| `omega.h` | Add `#define PK_GRV_ROUTE "grv_route"` near the other `PK_GRV_*` (line ~281). Bump `OMEGA_GKI_COUNT` 37→38. |
| `params.h` | Add `GKI_GRV_ROUTE` to `pk_global_index_t` (append, e.g. after `GKI_GRV_RVTYPE`). |
| `params.c` | Add `[GKI_GRV_ROUTE]=PK_GRV_ROUTE` to `k_global_keys` and `[GKI_GRV_ROUTE]=0.0f` to `g_global_defaults`. |
| `groove.h` | Add `float rv_mix; unsigned char route_order[3];` (rename/replace the `rv_pre_amt`/`rv_post_amt` fields). |
| `groove.c` | `set_param`: new `PK_GRV_ROUTE` case → fill `route_order`; change `PK_GRV_RVMIX` to plain `rv_mix=v`; `groove_init` seed `route_order = {RUMBLE,DRIVE,REVERB}`, `rv_mix=0`. |
| `ui.c` | Add a ROUTE enum + a plain REVERB(MIX) param to `P_GROOVE_FX` (the shared effects page used by BOTH TAPS and GEN). Add `OPT_ROUTE`. Extend `KN_GROOVE_FX`. **Page has 7 params now; adding FILTER (GEN) + ROUTE = 9 → exceeds 8 encoders.** See §GEN Filter for the page-budget resolution. |

**Enum-as-UP_ENUM pattern** (verbatim from existing `OPT_RVTYPE`/`OPT_SCALE`): `{ PK_GRV_ROUTE, "ROUTE", "ROUTE", UP_ENUM, "", "0", OPT_ROUTE }`. `set_param` parses the integer index (like `PK_GRV_RVTYPE`/`PK_GRV_GWAVE` do: `(int)(parse_f(val)+0.5f)`), NOT the 0..1 clamp at the top of `groove_set_param`.

---

## GEN Filter Exposure (GEN-FILTER)

### The DSP already exists — this is wiring only

`PK_GRV_COLOR` (`set_param:550`) maps a 0..1 knob to a **30 Hz→20 kHz log LP** (`fc = 30*pow(20000/30, v)`) into `color_g`, applied by the `tpt1_lp` COLOR filter in `groove_tick` (`:459–498`). The TAPS branch uses a 2-pole cascade; the GEN branch uses a 1-pole. **GEN already runs the COLOR filter** — the ONLY problem is that **GEN's UI never exposes a knob that writes `PK_GRV_COLOR`**. (ONDEVICE #14.)

### Cleanest fix: one FILTER control per type, on the shared FX page

- TAPS Groove Page 1 already has COLOR (writes `PK_GRV_COLOR`). Leave it there for TAPS.
- GEN Groove Page 1 has no COLOR (its params are TYPE/VOL/SCALE/ROOT/RANGE/RETRIG).
- **Add a FILTER param that writes `PK_GRV_COLOR` to the shared `P_GROOVE_FX` page** (used as `groove2` for TAPS, `groove3` for GEN). This gives GEN the sweep in a consistent location AND keeps TAPS's Page-1 COLOR as the single TAPS filter control.

**Duplicate-knob risk:** if FILTER is added to the shared FX page for BOTH types, TAPS would have COLOR (Page 1) AND FILTER (FX page) — both writing `PK_GRV_COLOR` = a duplicate control. Two clean options (planner picks one; both satisfy "exactly one filter control per type"):

1. **Preferred:** Emit FILTER on the shared FX page ONLY for GEN (conditional, like the existing GEN-vs-TAPS page branching in `ui.c:355–377`). TAPS keeps COLOR on Page 1; GEN gets FILTER on its FX page (groove3). One filter control each, no duplicate. Minimal UI churn.
2. Move COLOR off TAPS Page 1 onto the shared FX page as FILTER for both, freeing a Page-1 slot. Larger UI reorg; touches TAPS Page 1 layout (out of appetite — CONTEXT says GEN page relabeling beyond the filter is deferred).

**Recommendation: Option 1.** In `ui.c`, when emitting `groove3`/`groove2` "Groove Effects" for GEN, include a `{ PK_GRV_COLOR, "FILTER", "FILT", UP_FLOAT, "%", "0.01", NULL }` param; for TAPS, do not (it has Page-1 COLOR).

### Page-budget resolution (FX page slot count)

The shared `P_GROOVE_FX` currently has 7 params (DRIVE, LFO SPD, LFO AMT, REVERB, RV DECAY, RV TONE, RV TYPE). Adding ROUTE (§FX-ROUTE) → 8. Adding FILTER for GEN → 9 > 8 encoders. Resolution options (discretion):

- **RV TYPE is "reserved" and does nothing** (`set_param:596` is a no-op stub). Drop RV TYPE from the FX page to reclaim a slot → DRIVE, LFO SPD, LFO AMT, REVERB, RV DECAY, RV TONE, ROUTE = 7, + FILTER (GEN only) = 8. **Recommended.** (RV TYPE can stay cached/defaulted; CONTEXT says "keep RV DECAY/TONE/TYPE controls" — if TYPE must stay visible, instead drop RV TONE or fold LFO SPD+AMT, planner's call.)
- Or keep the FX page at ≤8 for TAPS (no FILTER) and ≤8 for GEN (FILTER replaces RV TYPE), since the page is emitted per-type anyway.

---

## GEN Unquantized Pitch/Root Fix (GEN-PITCH)

### Root cause (confirmed in `groove.c`)

`groove_gen_rebuild` (`:110–132`) fills `gen_seq[i] = xorshift % rng` where `rng = gen_range ∈ [1,24]` → **offsets are `0..(range-1)`, strictly ≥ 0**. In `groove_tick` (`:301–304`):
```c
int semi = gen_unquantized ? (int)gen_seq[step] : scale_quantize(gen_scale, gen_seq[step]);
gen_freq = gen_base_hz * powf(2, semi/12);   // semi >= 0 → freq >= base, never below
```
So the sequence never sounds at or below the root; with the default root and +offsets the fundamental floats high. And the unquantized ROOT maps to `30 + v*170` Hz (`:611,619`) — default `gen_root_param=0.542` → `30+92 = 122 Hz`, matching ONDEVICE #20/#21 "lowest is way too high."

### Fix 1 — center the offsets (root audible, bidirectional)

Make `gen_seq` centered around 0 so the root fundamental is played and the sequence spreads ±range/2:

```c
// groove_gen_rebuild, replacing the strictly-additive line:
int span = g->gen_range;                       // 1..24
for (int i = 0; i < 32; i++) {
    int raw = (int)(grv_xorshift(&g->gen_rng) % (unsigned)span); // 0..span-1
    g->gen_seq[i] = (signed char)(raw - span / 2);               // center: ±span/2
}
```
Now degree 0 (the root) is reachable and the most common value; negative offsets play BELOW the root. `scale_quantize` already handles negative degrees (`:59` wraps idx and decrements oct), so the quantized path stays correct. Byte-stable for a fixed seed (same PRNG draw, deterministic shift) — determinism test preserved.

### Fix 2 — extend unquantized ROOT HZ down to sub-bass

Change the unquantized root mapping (`set_param:611` and `:619`) from `30 + v*170` (linear, 30–200 Hz, default 122 Hz) to a **log map with a sub-bass floor**, and lower the default so the sounding pitch lands in 30–80 Hz:

```c
// unquantized ROOT HZ: 20 Hz floor .. 200 Hz, exponential, default lands ~45-55 Hz
g->gen_base_hz = 20.0f * powf(200.0f / 20.0f, v);   // v=0 -> 20 Hz, v=1 -> 200 Hz
// default gen_root_param: pick v so base ~ 45 Hz: v = log(45/20)/log(10) ≈ 0.352
```
With centered offsets, a root of 45 Hz and a modest range, the sequence sits in the 30–80 Hz rumble register (ONDEVICE #21). Update `GKI_GRV_GROOT` default in `params.c` (currently `0.542f`) and the `ui.c` `ROOT HZ` param min/max (currently `"30"`/`"200"` at `ui.c:291`) to `"20"`/`"200"`.

`powf` runs at control rate only (set_param) — RT-safe. Keep the quantized mapping (`8.175 * 2^(semi/12)`) untouched.

### Verify ROOT works in both modes

`ui_emit_gen_groove1` (`ui.c:289–292`) already swaps the label ROOT↔ROOT HZ on `gen_unquantized`. After Fix 1+2, the value genuinely changes perceived pitch in both modes. The §Validation pitch test is the gate.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Fractional delay read | custom interpolator | existing `buf[a0] + f*(buf[a1]-buf[a0])` + `& GRV_DELAY_MASK` (`groove.c:436–450`) | already correct, branch-free, proven |
| Swept LP filter | biquad | `tpt1_lp` (`dsp_primitives.h:157`) | biquad zippers on modulation; TPT is the project standard |
| Reverb | new algorithm | existing `groove_reverb_mono` (2-comb+1-allpass, `groove.c:56`) | already tuned, "reverb sounds good — keep it" (ONDEVICE What Works) |
| PRNG for the sequence | `rand()` | existing `grv_xorshift` (`groove.c:102`) | deterministic, reentrant, byte-stable |
| Scale quantize | new table math | `scale_quantize` (`dsp_primitives.c:55`) — handles negative degrees | reused, correct for centered offsets |
| Tempo clock | new beat math | `groove_update_tempo` (`groove.c:224`) — UNCHANGED | transport-locked, EMA-smoothed, proven |
| Denormal protection | per-sample epsilon injection | rely on FPCR FZ already set in render (`dsp.c:470`) | free, global for the callback; FIR design avoids denormal tails anyway |
| int16 output clamp | manual cast | `omega_to_i16` (`dsp_primitives.h:167`) | single clamp+isfinite+lrintf point (FNDTN-07) |

**Key insight:** this phase is ~90% deletion + rewiring of existing, proven primitives. The only genuinely new DSP is the control-rate tap-weight precompute (a ~10-line loop). Do not introduce new buffers, filters, or a new reverb.

---

## Common Pitfalls

### Pitfall 1: Leaving any feedback term in the ring write
**What goes wrong:** re-introducing the crackle/bit-crush. **Why:** even a small `fb_amount` coupled with the reverb re-establishes a two-loop network. **Avoid:** the ring write is `buf[wp] = kick` — literally nothing else. Grep-assert `fb_amount` is gone. **Warning sign:** output RMS grows over a long render (the §Validation no-runaway test catches it).

### Pitfall 2: Calling `expf`/`powf` per sample for the tap decay
**What goes wrong:** blows the CPU budget / defeats the "cheaper than before" requirement. **Avoid:** precompute `tap_w[]` at control rate (per block or on set_param/tempo re-lock); per-sample is pure MAC. **Warning sign:** `objdump`/grep shows `expf`/`powf` reachable from `groove_tick`.

### Pitfall 3: Denormals in the reverb tail
**What goes wrong:** on ARM64 without FTZ, decaying reverb/comb tails hit denormals → 10–100× per-op cost → dropouts (independently confirmed, Mixxx issue #16126). **Avoid:** FZ is already set in `render_block` — keep it; do NOT add a math flag that changes it. The FIR rumble has no decaying tail of its own; only the reverb comb does, and FZ covers it. **Warning sign:** CPU spikes only when REVERB is up and the kick stops.

### Pitfall 4: Page slot overflow (>8 encoders)
**What goes wrong:** the shared FX page gains ROUTE + FILTER and exceeds 8 knobs; the host can't map them. **Avoid:** drop the no-op RV TYPE (or fold an LFO control) to stay ≤8; emit FILTER on the FX page for GEN only. **Warning sign:** `KN_GROOVE_FX` has >8 keys; UI JSON knob array length > 8.

### Pitfall 5: GKI count / cache drift
**What goes wrong:** adding `GKI_GRV_ROUTE` without bumping `OMEGA_GKI_COUNT` trips `_Static_assert(GKI_COUNT == OMEGA_GKI_COUNT)` (`params.c:9`). **Avoid:** bump `OMEGA_GKI_COUNT` 37→38 in `omega.h:43` in the same change. **Warning sign:** compile-time assert failure (good — it catches you).

### Pitfall 6: Breaking GEN byte-determinism
**What goes wrong:** the centered-offset change alters the PRNG draw pattern and a determinism test that hard-codes old sample values fails. **Avoid:** the shift is deterministic (same seed → same `gen_seq` → same audio); update any test that asserts *specific* old sample values, keep the "same seed → identical buffer" invariant. **Warning sign:** `test_gen` byte-identical assertion fails after the pitch fix — expected; re-baseline the golden, keep the determinism property.

### Pitfall 7: objdump/glibc gate regression
**What goes wrong:** using a new libm entry point (e.g. under a stray fast-math flag) pulls a `>2.35` or `_ZGV*` symbol. **Avoid:** only `expf`/`powf`/`tanf`/`sqrtf` already used; keep the granular fast-math subset; run `scripts/glibc_gate.sh` on the cross build. **Warning sign:** CI gate red.

---

## Code Examples

### Control-rate LENGTH → decay-time (replaces `groove_set_length`)
```c
// groove.c — CONTROL RATE. Maps LENGTH v in [0,1] to a decay time constant.
// v=1 (right) -> ~40 ms (distinct copies); v=0 (left) -> ~1200 ms (smeared drone).
static void groove_set_length(groove_state_t *g, float v) {
    g->tap_tau_s = (40.0f * powf(30.0f, 1.0f - v)) * 0.001f;  // powf at control rate
    groove_update_tap_weights(g);                            // recompute tap_w[]
}
```

### Plain in-line reverb block (replaces PRE/POST hack)
```c
// groove.c — dry/wet in-line, driven by rv_mix (0..1). NEVER writes to the ring.
static inline void groove_reverb_block(groove_state_t *g, float *l, float *r) {
    float wet = groove_reverb_mono(g, 0.5f * (*l + *r));  // existing Schroeder
    *l += g->rv_mix * (wet - *l);
    *r += g->rv_mix * (wet - *r);
}
```

### ROUTE enum parse (control rate, like `PK_GRV_RVTYPE`)
```c
// groove_set_param: parse the integer index, fill the order table.
} else if (strcmp(key, PK_GRV_ROUTE) == 0) {
    int idx = (int)(parse_f(val) + 0.5f);
    static const unsigned char orders[4][3] = {
        { BLK_RUMBLE, BLK_DRIVE,  BLK_REVERB }, // 0 Rumble>Drive>Reverb (default)
        { BLK_RUMBLE, BLK_REVERB, BLK_DRIVE  }, // 1 Rumble>Reverb>Drive
        { BLK_REVERB, BLK_RUMBLE, BLK_DRIVE  }, // 2 Reverb first
        { BLK_DRIVE,  BLK_RUMBLE, BLK_REVERB }, // 3 Drive first
    };
    if (idx < 0) idx = 0; if (idx > 3) idx = 3;
    for (int i = 0; i < 3; i++) g->route_order[i] = orders[idx][i];
}
```

---

## State of the Art

| Old Approach | Current Approach (this phase) | Why |
|--------------|-------------------------------|-----|
| Recirculating IIR feedback rumble (`fb_amount` ≤ 0.85 + reverb-into-ring) | FIR decay-enveloped ghost-kick taps (bounded sum of raw-kick copies) | Unconditionally stable; no denormal-tail CPU spikes; no compounding two-loop energy buildup |
| Bidirectional PRE/POST reverb hack (`rv_pre_amt`/`rv_post_amt`, deadzone) | Plain dry/wet MIX + explicit ROUTE enum | User-legible, one Schroeder instance, reverb never re-enters the ring |
| GEN offsets strictly additive above root | Offsets centered ±range/2 around root | Root fundamental audible; unquantized ROOT HZ meaningful |
| Unquantized root linear 30–200 Hz (default 122 Hz) | Log 20–200 Hz (default ~45 Hz) | Reaches true sub-bass (ONDEVICE #21) |

**Deprecated/outdated after this phase:** `fb_amount`, `diffuse_amt`, in-loop `fb_lp*`/`loop_hp*`, `ap1`/`ap2`, `rv_pre_amt`/`rv_post_amt` — all removed.

---

## Runtime State Inventory

This is a **code-only** change (DSP + params + UI in a single `dsp.so`). No datastores, services, OS registrations, secrets, or build artifacts hold any renamed/persisted state.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | None — the groove state is transient per-instance RAM in the single calloc; no persisted DB/collection. | None |
| Live service config | None — no external service; the module is a single `dsp.so`. | None |
| OS-registered state | None. | None |
| Secrets/env vars | None. | None |
| Build artifacts | `src/wavetables.h` / `sine_table.h` are generated but UNAFFECTED (no wavetable change). Preset/state JSON (`get_param("state")`) is **not implemented yet** (PRST-* is future Phase G), so no persisted preset references the removed `grv_rvmix` PRE/POST semantics or old field names. | None this phase |

**Note:** because presets are not yet persisted, changing `PK_GRV_RVMIX` semantics (bidirectional → plain 0..1) and adding `PK_GRV_ROUTE` breaks no saved state. If PRST lands later, the default (`rv_mix=0`, `route=0`) is a safe migration target.

---

## Validation Architecture

> nyquist_validation: no `.planning/config.json` key found asserting `false`; treat as ENABLED. This section is consumed to build VALIDATION.md.

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Plain C asserts + a WAV/RMS/ZCR harness (no third-party framework); native `cc`, `-DOMEGA_MALLOC_TRAP` on Linux |
| Config file | `Makefile` (`GROOVE_TEST_SRCS`, `TAPS_TEST_SRCS`; `test:` aggregates all suites) |
| Quick run command | `make test-groove` (drives the real plugin through the drivable mock host) |
| Full suite command | `make test` |

The existing pattern: `tests/test_groove.c` + `tests/test_taps_redesign.c` drive `move_plugin_init_v2 → create_instance → set_param → on_midi → render_block` through `tests/mock_host.c` (tempo-drivable: `mock_host_set_bpm`/`mock_host_advance_beat`/`make_mock_host_null_transport`). Helpers: `buf_rms`, ZCR, `samples_per_16th`, `dbeat_for_bpm`, `neutralise_chain` (DUCK=0/DJ=0.5/CLIP=0/MVOL=1 to isolate the groove). WAV output via `tests/wav.h`. Guards: malloc-trap abort, `isfinite`, int16 clamp.

### Phase Requirements → Test Map
| Req | Behavior | Test Type | Automated Command | File Exists? |
|-----|----------|-----------|-------------------|--------------|
| RUMBLE-CORE | No runaway: kick + max LENGTH + max DRIVE + max REVERB (`rv_mix=1`) + EACH of the 4 ROUTE orders, render 200+ blocks → every sample finite + in [-1,1] AND late-window RMS ≤ early-window RMS × 1.2 (no growth) | integration | `make test-groove` (new `test_no_runaway`) | ❌ Wave 0 |
| RUMBLE-CORE | Audible at default: default VOL + default LENGTH, RMS above a floor and within ~6 dB of the kick RMS (level-competitive, no DRIVE) | integration | `make test-groove` (new `test_rumble_audible`) | ❌ Wave 0 |
| RUMBLE-CORE | LENGTH morphs distinct↔smeared: at LENGTH=1 the inter-tap gaps are near-silent (low RMS between 16ths) vs LENGTH=0 continuous (high inter-tap RMS); metric = ratio of gap-RMS to peak-RMS rises with LENGTH | integration | `make test-groove` (new `test_length_morph`) | ❌ Wave 0 (extend existing `test_taps_redesign.c` LENGTH gate) |
| FX-ROUTE | Routing changes output: render the same input under order 0 vs order 2 (reverb first vs last) with DRIVE+REVERB up → buffers differ beyond epsilon | integration | `make test-groove` (new `test_route_changes_output`) | ❌ Wave 0 |
| FX-ROUTE | Reverb bounded: `rv_mix=1` + max RV DECAY, long render → finite + bounded | integration | covered by `test_no_runaway` | ❌ Wave 0 |
| GEN-FILTER | GEN LP sweeps: GEN type, high-frequency content (ZCR or high-band energy) drops as `PK_GRV_COLOR` goes 1→0 | integration | `make test-groove` (new `test_gen_filter_sweep`) | ❌ Wave 0 |
| GEN-PITCH | Unquantized ROOT tracks: low ROOT HZ → low measured fundamental (autocorrelation or zero-crossing pitch), high ROOT HZ → higher; low value lands in 30–80 Hz | integration | `make test-groove` (new `test_gen_root_pitch`) | ❌ Wave 0 |
| GEN-PITCH | Root audible / centered: with centered offsets the mean sounding pitch ≈ root (not root + range) | integration | part of `test_gen_root_pitch` | ❌ Wave 0 |
| ALL | Determinism: GEN fixed seed → byte-identical buffer across two runs | integration | `make test-gen` (re-baseline golden) | ✅ exists (update values) |
| ALL | RT-safety: zero allocations in render; no NaN/Inf; int16 in range | guard | malloc-trap + isfinite + clamp in every suite | ✅ exists |

### Sampling Rate
- **Per task commit:** `make test-groove` (+ `make test-gen` if GEN touched)
- **Per wave merge:** `make test`
- **Phase gate:** full `make test` GREEN + cross build `make dsp.so` + `scripts/glibc_gate.sh` (objdump gate) before `/gsd:verify-work`.

### Wave 0 Gaps
- [ ] `tests/test_groove.c` — add `test_no_runaway`, `test_rumble_audible`, `test_length_morph`, `test_route_changes_output`, `test_gen_filter_sweep`, `test_gen_root_pitch` (all covering the 4 deliverables). Reuse existing helpers; add a simple autocorrelation-pitch helper for GEN-PITCH.
- [ ] `tests/test_taps_redesign.c` — update/repurpose the existing LENGTH + reverb gates to the new feedback-free semantics (the old gates assumed `fb_amount`/PRE-POST; re-point them).
- [ ] `tests/test_gen.c` — re-baseline the determinism golden after the centered-offset change (keep the "same seed → identical" property; update the specific expected values).
- [ ] No new framework install — plain C asserts + existing harness cover everything.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| native `cc`/clang | `make test` (offline harness) | ✓ (macOS host) | system | — |
| `aarch64-linux-gnu-gcc` (Docker image) | `make dsp.so` cross build + glibc gate | ✗ locally (per STATE.md: Docker unavailable on this host) | — | CI (GitHub Actions) is the authoritative cross-build + `objdump -T` gate |
| `objdump` (glibc gate) | `scripts/glibc_gate.sh` | ✗ locally (inside the Docker image only) | — | CI runs the gate |
| `powf`/`expf`/`tanf`/`sqrtf` (libm) | control-rate mappings | ✓ | glibc 2.35 (device) | — (already used; no new symbols) |

**Missing dependencies with no fallback:** none — all required libm symbols are already linked and in-gate.
**Missing dependencies with fallback:** local aarch64 cross-build + objdump gate → rely on CI (unchanged from every prior phase; STATE.md confirms this is the standing arrangement).

---

## Open Questions

1. **Groove VOL default mismatch.**
   - What we know: `params.c` sets `[GKI_GRV_VOL]=1.0f` but `groove_init` sets `g->vol=0.0f` (silent opt-in, a C-02 deviation to protect the kick voicing battery). The cache default and the DSP init disagree.
   - What's unclear: which wins on-device (the host likely pushes the cached 1.0 on load, so VOL≈1.0 in practice).
   - Recommendation: the "audible at default" test should set VOL to the cache default (1.0) explicitly; if the rumble is still too quiet, `rumble_makeup` is the tuning knob, not VOL. Planner should reconcile the two defaults to one value.

2. **"Reverb before the rumble source" semantics.**
   - What we know: the rumble is the per-sample source; DRIVE/REVERB are post-source transforms. A literal "reverb → rumble" (reverb the dry kick, then tap it) would require reverbing the ring input — which is exactly the forbidden reverb-into-ring pattern.
   - What's unclear: whether the user's "reverb first" means (a) DRIVE-vs-REVERB order = REVERB before DRIVE (achievable, index 2), or (b) reverb the kick before it enters the tap ring (forbidden by the locked decision).
   - Recommendation: implement (a) — REVERB before DRIVE in the post-source chain. This is the only interpretation consistent with "no reverb into the ring." Document the option labels clearly so the ordering is unambiguous on the OLED.

3. **NTAPS = 4 vs 6–8 for smear depth.**
   - What we know: 4 taps cover one beat; at long LENGTH the smear may feel slightly gapped at slow tempos.
   - Recommendation: ship NTAPS=4 (reuses TAP1..4 knobs exactly). If on-device the long-LENGTH smear is gappy, add fixed-level taps 5..8 (cheap MACs, no new knobs). Flag for the on-device listening pass.

---

## Sources

### Primary (HIGH confidence)
- Project source, read in full: `src/groove.c`, `src/groove.h`, `src/params.c/.h`, `src/ui.c`, `src/dsp_primitives.c/.h`, `src/dsp.c` (groove wiring), `src/omega.h` (ABI + PK macros + size assert), `Makefile` (flags, gate, test targets), `tests/test_groove.c` / `test_taps_redesign.c` (harness pattern), `audiofx/module.json` (TapeDelay) / `module (3).json` (Freeverb) reference param tapers — grounds every removal/keep and field mapping.
- CONTEXT.md / REQUIREMENTS.md / STATE.md / ONDEVICE_FEEDBACK_v1_1.md — locked decisions, [C1] feedback-loop history, defects #3/#14/#20/#21.
- CLAUDE.md — RT-safety, TPT-over-biquad, single-calloc, objdump/glibc gate, granular fast-math.

### Secondary (MEDIUM confidence — cross-verified production/academic)
- [MusicRadar — Create a rumbling techno kick in 10 easy steps](https://www.musicradar.com/how-to/rumbling-techno-kick) — delay-rumble recipe (delay → distort → LP, tempo-synced pulsating).
- [Studio Brootle — Delay Rumble Sub Bass Rack](https://www.studiobrootle.com/delay-rumble-sub-bass-rack/) — delay vs reverb rumble distinction (galloping synced delay rumble vs smeared reverb rumble).
- [mastrng.com — Techno Rumble: How to make a Techno Kick that slams](https://www.mastrng.com/techno-rumble/) — reverb-tail rumble recipe (copy kick → reverb → EQ highs out → saturate → sub rumble).
- [Elektronauts — Techno rumble reverb](https://www.elektronauts.com/t/techno-rumble-reverb/170447) — filtered reverb tail sidechained to the kick.
- [Wikipedia — Gated reverb](https://en.wikipedia.org/wiki/Gated_reverb) — gated-reverb tail definition.
- [arXiv 2402.11216 — Optimizing tiny colorless feedback delay networks](https://arxiv.org/pdf/2402.11216) — FDN feedback matrix magnitude < 1 for a stable decaying response (stability-by-construction principle).
- [Mixxx issue #16126 — ARM64 audio thread missing FPCR FZ bit causes noise with stacked effects](https://github.com/mixxxdj/mixxx/issues/16126) — confirms: stacked feedback effects decay to denormals → 10–100× CPU on ARM64 without FTZ → dropouts (the CPU-crackle mechanism).
- [JUCE forum — Resolving denormal floats once and for all](https://forum.juce.com/t/resolving-denormal-floats-once-and-for-all/8241) — FTZ/DAZ as the standard fix for feedback-loop denormals.

### Tertiary (LOW confidence)
- [Gearspace — Kick Delay in Techno](https://gearspace.com/board/electronic-music-instruments-and-electronic-music-production/1085895-kick-delay-techno.html) — community feedback-delay routing discussion (unverified, corroborates recipe 1 only).

---

## Metadata

**Confidence breakdown:**
- Rumble core algorithm: HIGH — FIR-by-construction stability is provable + matches the documented delay-rumble recipe; per-sample math grounded in the existing tap-read code.
- FX routing: HIGH — control-rate order table + existing drive/reverb blocks; only ambiguity is the "reverb first" semantics (Open Q2).
- GEN filter: HIGH — DSP already exists; pure UI/param wiring.
- GEN pitch: HIGH — root cause confirmed in code; fix is a centered offset + log Hz map, both control-rate.
- Pitfalls/stability: HIGH — CPU-crackle mechanism independently confirmed (ARM denormal + coupled-loop energy).

**Research date:** 2026-10-01
**Valid until:** 2026-10-31 (stable domain; the source code is the authority and is versioned in-repo).
