# Phase C: Groove Rumble Engine - Research

**Researched:** 2026-09-29
**Domain:** Real-time tempo-locked multi-tap delay DSP in native C11 for the Omega Schwung module (aarch64/glibc 2.35); host-transport BPM derivation; TPT SVF filtering; wiring the Phase-B GEN generative engine to a Groove Page 2; offline WAV/mock-host validation of tempo-lock.
**Confidence:** HIGH — every technical claim is grounded in the locked C-CONTEXT.md decisions, the in-tree source (omega.h / dsp.c / gen.c / dsp_primitives.h / ui.c), and the canonical Context/02–04 spec files. The one genuine unknown (live-BPM feel, tempo-derivation stability on real transport) is explicitly deferred to an on-device round and flagged below.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**DC-01: Groove = kick-fed 4-tap 16th-note multi-tap delay (GRV-01)**
Write the per-sample kick output into a pre-allocated circular delay buffer; read 4 taps at 16th-note offsets back out, each scaled by its TAP level. Buffer pre-allocated in `create_instance` (88,200 frames × 2ch = ~705 KB), by value in the single instance calloc. Planner's discretion: modulo wrap vs power-of-two mask (round MAX_DELAY_FRAMES up to 131072 for branch-free `& mask`) — prefer the mask.

**DC-02: Tempo from the host clock, NEVER hardcoded 120 (GRV-02)**
Derive BPM from `host->get_beat_position()` beat-delta across blocks; `samples_per_16th = (60/bpm) × sr / 4`, integer tap positions for v1. Guard the transport edge cases (this is the reference-code bug we must not repeat): `get_beat_position()` returns < 0 when no transport is running, and the pointer may be NULL on older hosts — fall back to `host->get_bpm()` (also NULL-guard), then to a 120 default ONLY as the last-resort constant, never as the live value. Recompute tap interval at control rate when BPM changes so the rumble re-locks across tempo changes.

**DC-03: Groove Page 1 — 8 encoders (GRV-03)**
VOL (groove master), LENGTH (per-tap decay), COLOR (TPT SVF low-pass timbre on the groove voice — reuse the existing `tpt1`/SVF primitive), TAP1, TAP2, TAP3, TAP4 (individual 16th-tap levels), MONO (toggle).

**DC-04: MONO force-sum (GRV-05)**
MONO toggle sums L+R to mono on the groove voice for sub-bass club routing (standard below ~150 Hz). Apply to the groove signal; the kick path is unaffected.

**DC-05: GEN ↔ Groove Page 2 integration (GRV-04) — behavior decision**
Groove Page 2 (SEED, SCALE, SEQ LEN, LPF FREQ, LPF POLE, DENSITY) is shown only when the active model == GEN (MODEL_GEN) and hidden for all other models (ui.c already assembles Page 2 dynamically; extend it to conditionally emit a Groove Page 2 for GEN). Behavior per the Bohm spec (Context/02: HPN "redefines the Groove circuit"): when GEN is active, its generative scale-quantized pitch sequence drives the Groove voice (the generative engine built in Phase B now connects to these Page-2 controls + the tempo clock), rather than the plain delayed-kick multitap. For all non-GEN models, Groove is the kick-fed multitap of DC-01. The GEN generative engine already exists (KICK-11); Phase C wires its SEED/SCALE/SEQ-LEN/DENSITY controls to Groove Page 2 and clocks it from the transport (DC-02), and adds the Groove LPF (LPF FREQ + LPF POLE 2/4-pole).

**DC-06: LPF POLE 2/4-pole toggle (GRV-04)**
Implement the Groove Page 2 LPF as cascaded TPT 1-pole low-pass stages: 1 stage = 2-pole equivalent path / 2 stages = 4-pole, toggled by LPF POLE. Reuse the shared TPT primitive; no biquad for the swept filter (CLAUDE.md).

**DC-07: NO reverb / no smear stage (v1 scope)**
Traditional techno rumble offers a reverb "smear" path; Bohm — and Omega — deliberately use the dry multi-tap delay approach only (confirmed with the user). No reverb, allpass, or diffusion anywhere in the Groove chain.

**DC-08: Instance-size static_assert must be raised**
Adding the ~705 KB groove delay buffer to `bohm_instance` (which already grew for USR's ~184 KB buffers) pushes `sizeof(struct bohm_instance)` past the current `_Static_assert(... < 800000)`. Raise the bound (e.g. to < 1,100,000) and keep the single-calloc strategy. Confirm Move RAM headroom is fine (16 Movy tracks × ~0.9 MB ≈ 14 MB — acceptable).

### Claude's Discretion
- Circular-buffer wrap strategy (mask vs modulo), exact COLOR/LPF cutoff mappings, per-tap decay curve shape, and how the GEN sequence phase maps onto the tempo grid — choose musically, refine in the (deferred) voicing round.
- Whether the groove voice reuses the shared FX chain or has its own COLOR-only path.

### Deferred Ideas (OUT OF SCOPE)
- Reverb / smear stage for a warehouse-rumble character (DC-07) — revisit if the user wants the reverb-smeared sound.
- On-device voicing round for Phase B (`docs/VOICING_AUDIT.md`) — deferred by user; still outstanding UAT debt. Groove voicing will fold into a later on-device round.
- Fractional/interpolated tap positions (v1 uses integer tap positions per GRV-02).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| GRV-01 | 4-tap 16th-note multi-tap delay engine fed from kick; circular delay buffer pre-allocated at `create_instance` (88,200 frame max, 2s @ 44.1kHz) | §Architecture Pattern 1 (circular delay), §Memory / Runtime State Inventory (buffer sizing + DC-08 assert), §Code Examples (write/read/mask) |
| GRV-02 | BPM derived from `get_beat_position()` beat-delta across blocks — NOT hardcoded 120; `samples_per_16th = (60/bpm) × sr / 4`; integer tap positions | §Architecture Pattern 2 (tempo derivation), §Common Pitfalls 1–3, §Code Examples (tempo clock) — the hard, de-risked problem |
| GRV-03 | Groove Page 1 (8 encoders): VOL, LENGTH (tap decay), COLOR (TPT SVF low-pass), TAP1, TAP2, TAP3, TAP4, MONO toggle | §Architecture Pattern 3 (groove state + Page-1 mapping), §Don't Hand-Roll (reuse tpt1) |
| GRV-04 | Groove Page 2 (GEN only, hidden otherwise): SEED, SCALE, SEQ LEN, LPF FREQ, LPF POLE (2/4-pole), DENSITY | §Architecture Pattern 4 (GEN↔Groove wiring), §Architecture Pattern 5 (conditional Page-2 in ui.c), §Open Questions (new PK_ keys) |
| GRV-05 | MONO toggle force-sums L+R to mono for sub-bass routing | §Architecture Pattern 3 (MONO), §Code Examples (mono sum) |
</phase_requirements>

## Summary

Phase C adds one new voice — the Groove rumble — that sits between the finished kick engine and the (out-of-scope) Phase-D performer chain. The mechanics are almost entirely already-proven in this codebase: a pre-allocated circular delay buffer (DC-01), the shared `tpt1_lp` TPT 1-pole for COLOR and the Page-2 LPF cascade (DC-03/DC-06), the shared `env_t` for per-tap decay (DC-03 LENGTH), and the Phase-B `gen.c` generative engine (SEED/SCALE/DENSITY + `prng_t`/`scale_quantize`/Euclidean gate) that Phase C re-clocks from the transport (DC-05). The genuinely new and highest-risk piece is **live tempo derivation from `host->get_beat_position()`** (DC-02, GRV-02) — the single reference-code bug (Context/04 line 118: `tap_interval = sample_rate * 0.125f` = a hardcoded 16th-at-120-BPM) that STATE.md explicitly forbids repeating.

The disciplined answer to tempo derivation: read `get_beat_position()` **once per block** (never per sample), track the previous beat position, and compute `bpm = (beat_now − beat_prev) × 60 × sr / frames`. Because a per-block beat delta is jittery, smooth it (one-pole EMA or a "snap when it moves > ε" latch) and recompute `samples_per_16th = (60/bpm) × sr / 4` only when BPM actually changes — at control rate, never per sample, never a per-sample division. Guard the three transport edges in a strict fallback chain: (1) if `get_beat_position` is non-NULL and returns ≥ 0, derive live BPM; (2) else if `get_bpm` is non-NULL and returns a sane value (20–999), use it; (3) else fall back to a 120.0f **constant** used only as a last resort, never presented as a live tempo. Integer tap positions for v1 (deferred: fractional/interpolated taps).

The Groove sums with the kick as `kick + groove` at the module output, leaving a clean, single insertion point for Phase D's duck→DJ-filter→clip. Everything stays RT-safe: the delay buffer and all groove/GEN state pre-allocate inside the existing single instance calloc (DC-08 raises the `_Static_assert` bound), zero audio-thread alloc/log/file-I/O, and transport is read only through NULL/negative-guarded host callbacks.

**Primary recommendation:** Build in this order — (Wave 0) extend the mock host with a drivable `get_beat_position`/`get_bpm` and add the tempo-clock test harness; (Wave 1) the tempo clock + circular delay + Page-1 controls for the non-GEN kick-fed multitap (GRV-01/02/03/05); (Wave 2) wire `gen.c` to the transport clock + the new Groove Page 2 controls and the conditional `ui.c` emission (GRV-04/DC-05/DC-06). Retire the tempo-lock risk first with an offline harness that drives the beat position at 120/128/174 BPM and asserts tap positions track — this is the whole phase's crux.

## Standard Stack

No new third-party libraries. Phase C is pure in-tree C11 over the existing primitives.

### Core (all already in the tree)
| Component | Location | Purpose in Phase C | Why Standard |
|-----------|----------|--------------------|--------------|
| `tpt1_t` / `tpt1_lp(f, x, g)` | `dsp_primitives.h:152-160` | COLOR (Page 1) + LPF cascade (Page 2, DC-06) | TPT SVF chosen over biquad for swept filters (CLAUDE.md, D-07); no zipper/instability under modulation |
| `env_t` / `env_trigger` / `env_tick` / `env_coeff_from_ms` | `dsp_primitives.h:41-58` | Per-tap LENGTH decay envelope (DC-03) | Shared one-pole exp decay; time-constant ms mapping already the project idiom (D-05) |
| `prng_t` / `prng_next_f` + `scale_quantize` + `g_scales[4][12]` | `dsp_primitives.h:113-149`, `dsp_primitives.c` | GEN sequence generation (already used by gen.c) | Deterministic xorshift64, libc-independent; NOT `rand()` |
| `gen.c` generative engine | `src/models/gen.c` | The GEN voice Phase C re-clocks from transport (DC-05) | Already ships PRNG + scale-quantize + Euclidean gate + self-clock; Phase C swaps self-clock → transport clock |
| `host->get_beat_position()` / `host->get_bpm()` | `omega.h:64,67` | Live tempo derivation (DC-02) | The ONLY sanctioned tempo source; both NULL-guarded |
| single instance calloc | `dsp.c:168` (`omega_create`) | Pre-allocate the ~705 KB delay buffer by value (DC-01/DC-08) | One alloc, off the render loop; CLAUDE.md single-alloc strategy |

### Alternatives Considered
| Instead of | Could Use | Tradeoff / Why Not |
|------------|-----------|--------------------|
| Power-of-two mask wrap (131072) | Modulo `% MAX_DELAY_FRAMES` (88200) | Modulo is a per-sample integer division (slow on A53) and the exact reference bug's neighbour. Mask is branch-free `& (N-1)`; DC-01 prefers it. Rounding 88200 → 131072 costs ~350 KB extra but stays well under the raised assert. |
| Deriving BPM from beat-delta | Only `get_bpm()` | `get_bpm` may be NULL (older hosts) and DC-02/GRV-02 explicitly require the beat-delta path as primary. Keep `get_bpm` as fallback #2 only. |
| Integer tap positions | Fractional/interpolated taps | Deferred (C-CONTEXT deferred ideas); integer is v1 and avoids a per-sample interpolation read on the delay line. |
| Groove reuses shared `fx_process` chain | COLOR-only path | Discretion (DC). Recommend a COLOR-only groove path in Phase C: the duck/DJ-filter/clip performer chain is Phase D and belongs after the `kick+groove` sum, not inside the groove voice. Keeps the Phase-D insertion point clean. |

**Installation:** none. `make test` (native) remains the gate; `make dsp.so` (Docker cross-build) + `scripts/glibc_gate.sh` remain the ship gate.

## Architecture Patterns

### Recommended state placement
```
struct bohm_instance {              // omega.h — grows in Phase C
    model_id_t model;
    float      main_volume;
    bool       logged_buflen;
    char       model_state[4096];   // per-model (GEN state overlays here)

    float usr_wavetable[OMEGA_WT_GUARD];   // existing USR (~8 KB)
    float usr_sample[44100];               // existing USR (~176 KB)
    int   usr_sample_len; bool usr_wt_loaded, usr_loaded;

    /* --- NEW Phase C: Groove voice (by value, inside the single calloc) --- */
    groove_state_t groove;          // holds the ~705 KB delay buffer + params + tempo clock
};
_Static_assert(sizeof(struct bohm_instance) < 1100000, "instance under 1.1MB");  // DC-08: raise from 800000
```

`groove_state_t` is a NEW struct (Phase C owns it; not part of the locked host ABI). Put it in `omega.h` alongside `bohm_instance` (like the reference `groove_state_t` in Context/04 lines 30-39, but with the fixed tempo clock and mask wrap). It contains: the two delay ring buffers, `write_pos`, tap levels, LENGTH/COLOR/VOL/MONO params, the tempo-clock fields, and the Page-2 LPF cascade state (2× `tpt1_t`). Zero-initialised by the existing calloc — deterministic, denormal-free start.

**Buffer sizing note (mask path):** With the 131072 power-of-two mask, the two float ring buffers are `2 × 131072 × 4 = 1,048,576` bytes (~1.0 MB) alone. That plus USR's ~184 KB and `model_state[4096]` pushes `sizeof(bohm_instance)` to ~1.24 MB — **over the DC-08 example bound of 1,100,000.** Two clean options for the planner:
- **Option A (mask, honest sizing):** keep the 131072 mask and raise the assert to e.g. `< 1300000`. DC-08's "1,100,000" was an example ("e.g."); the real number must cover the mask buffer. Recompute the exact `sizeof` and set the bound just above it.
- **Option B (modulo at 88200):** keep the literal 88200-frame buffer (`2 × 88200 × 4 = 705,600` B, matching DC-01's "~705 KB") and pay the modulo. Total ~900 KB, fits under 1,100,000.
- **Recommended:** Option A (mask) for RT performance, with the assert sized to the true mask footprint. Flag the DC-08 example number as needing this correction — see Open Questions Q1.

### Pattern 1: Circular delay buffer, kick-fed 4-tap read (GRV-01, DC-01)
**What:** Each sample, write the current kick output into the ring at `write_pos`, then read 4 taps at `1..4 × samples_per_16th` behind `write_pos`, each scaled by its TAP level, and sum. Advance `write_pos` with a branch-free mask.
**When:** The non-GEN groove voice (all models except MODEL_GEN).
```c
// Source: adapted from Context/04 lines 108-127 BUT with the mask wrap and the
// tempo clock (NOT the hardcoded 0.125f interval — that is the forbidden bug).
#define GRV_DELAY_MASK (131072u - 1u)   // power-of-two ring; & mask is branch-free
// per sample n, kick_l/kick_r already computed by the kick render:
g->buf_l[g->write_pos] = kick_l;
g->buf_r[g->write_pos] = kick_r;
float gl = 0.0f, gr = 0.0f;
for (int t = 0; t < 4; t++) {
    unsigned rp = (g->write_pos - (unsigned)((t + 1) * g->samples_per_16th)) & GRV_DELAY_MASK;
    float decay = g->tap_decay[t];      // per-tap LENGTH decay weight (control-rate)
    gl += g->buf_l[rp] * g->tap_level[t] * decay;
    gr += g->buf_r[rp] * g->tap_level[t] * decay;
}
g->write_pos = (g->write_pos + 1) & GRV_DELAY_MASK;
```
Note: `write_pos` is `unsigned`; subtracting `(t+1)*samples_per_16th` then masking gives the correct wrapped read index with no `+ MAX` term needed (unsigned wraparound + mask). `samples_per_16th` must be clamped so `4 × samples_per_16th < 131072` (at 20 BPM, `samples_per_16th ≈ 33075`, ×4 = 132300 > 131072 — so clamp min BPM or cap tap count reach; see Pitfall 3).

### Pattern 2: Tempo derivation — the DC-02/GRV-02 crux (READ ONCE PER BLOCK)
**What:** Derive live BPM from the per-block beat-position delta, smooth it, recompute `samples_per_16th` only on change. Guard NULL callback and negative (stopped) transport.
**When:** Once at the top of the groove render, before the per-sample loop. NEVER per sample.
```c
// Source: DC-02 formula + Context/04 (the bug it replaces) + omega.h host callbacks.
// Called ONCE per render block. g->host is g_host (stashed in move_plugin_init_v2).
static void groove_update_tempo(groove_state_t *g, const host_api_v1_t *host, int frames) {
    float bpm = 0.0f;

    // (1) PRIMARY: beat-delta from get_beat_position (GRV-02).
    if (host && host->get_beat_position) {
        double beat = host->get_beat_position();
        if (beat >= 0.0) {                          // >=0 => transport running
            if (g->have_prev_beat) {
                double dbeat = beat - g->prev_beat;
                if (dbeat > 0.0 && dbeat < 4.0) {   // sane per-block advance
                    bpm = (float)(dbeat * 60.0 * (double)OMEGA_SR / (double)frames);
                }
            }
            g->prev_beat = beat;
            g->have_prev_beat = true;
        } else {
            g->have_prev_beat = false;              // stopped: drop stale prev
        }
    }

    // (2) FALLBACK: get_bpm (NULL-guarded, sanity-clamped).
    if (bpm <= 0.0f && host && host->get_bpm) {
        float b = host->get_bpm();
        if (b >= 20.0f && b <= 999.0f) bpm = b;
    }

    // (3) LAST RESORT: 120 constant — ONLY when no transport info at all.
    if (bpm <= 0.0f) bpm = g->last_bpm > 0.0f ? g->last_bpm : 120.0f;

    // Smooth jitter; recompute the tap interval ONLY when BPM actually moves.
    bpm = clampf(bpm, 20.0f, 300.0f);
    g->bpm_smooth += (bpm - g->bpm_smooth) * 0.20f;         // one-pole EMA
    if (fabsf(g->bpm_smooth - g->last_bpm) > 0.5f) {        // control-rate re-lock
        g->last_bpm = g->bpm_smooth;
        // samples_per_16th = (60/bpm) * sr / 4  (DC-02, integer for v1)
        int spq = (int)((60.0f / g->bpm_smooth) * OMEGA_SR / 4.0f + 0.5f);
        if (spq < 1) spq = 1;
        if (spq * 4 > (int)(GRV_DELAY_MASK)) spq = (int)(GRV_DELAY_MASK) / 4;  // clamp reach
        g->samples_per_16th = spq;
    }
}
```
Key disciplines: **one division per block, not per sample** (`60/bpm`); the EMA + threshold latch means the ring read positions only shift when the tempo genuinely changes, so the rumble "re-locks" cleanly across BPM changes without zipper. `prev_beat` must be reset when transport stops (`beat < 0`) so a resume doesn't compute a giant spurious delta.

### Pattern 3: Groove Page 1 controls + MONO (GRV-03, GRV-05, DC-03/DC-04)
**What:** 8 encoders map to `groove_state_t` fields at control rate in a `groove_set_param`. COLOR is a `tpt1_lp` on the summed groove voice. MONO force-sums L+R.
```c
// control-rate (in groove_set_param), NOT per sample:
//   VOL     -> g->vol         (0..1 groove master)
//   LENGTH  -> per-tap decay weights (e.g. tap_decay[t] = powf(v_shaped, t+1) or env-based)
//   COLOR   -> g->color_g = tanf(pi*fc/SR) with fc mapped from COLOR (reuse tpt_g_from_hz)
//   TAP1..4 -> g->tap_level[0..3]  (0..1)
//   MONO    -> g->mono (bool: v >= 0.5)
// per-sample tail (after the 4-tap sum gl/gr):
gl = tpt1_lp(&g->color_lp_l, gl, g->color_g);   // COLOR timbre LP
gr = tpt1_lp(&g->color_lp_r, gr, g->color_g);
if (g->mono) { float m = 0.5f * (gl + gr); gl = gr = m; }   // GRV-05 sub-bass mono sum
gl *= g->vol; gr *= g->vol;
// then: out_l[n] += gl; out_r[n] += gr;   // kick + groove sum (Pattern 6)
```
Note the mono sum is `0.5*(L+R)` (Context/03 §2, `Y_mono = 0.5×(X_L+X_R)`) — averaging, not raw addition, to avoid a +6 dB jump on centered content. Applies to the groove voice only; the kick path is untouched (DC-04).

### Pattern 4: GEN ↔ Groove clock + Page-2 controls (GRV-04, DC-05/DC-06)
**What:** When `model == MODEL_GEN`, the GEN generative sequence *is* the groove voice (Context/02 HPN "redefines the Groove circuit"). Phase C swaps gen.c's fixed self-clock (`GEN_STEP_FRAMES 5088`, a hardcoded ~130-BPM 16th — see gen.c line 38-40) for the transport-derived `samples_per_16th`, and adds SEQ LEN, LPF FREQ, LPF POLE.

The Phase-B `gen.c` already has: `prng_t rng` (SEED), `scale` (SCALE), `density`/`npulses` (DENSITY → Euclidean), `step`/`step_ctr` self-clock, and a per-step `gen_step_pitch` + `gen_fire_step`. Phase C changes/adds:
- **Transport clock:** replace `g->step_ctr = GEN_STEP_FRAMES;` reloads with `g->step_ctr = samples_per_16th;` sourced from the groove tempo clock (Pattern 2). gen.c must read the tempo interval — pass it in (e.g. store `samples_per_16th` in a field the render loop reads, updated once per block by `groove_update_tempo`). This is the single line that kills gen.c's `GEN_STEP_FRAMES` hardcode (the GEN analogue of the DC-02 bug).
- **SEQ LEN:** replace the fixed `GEN_SEQ_LEN 16` with a runtime `g->seq_len` (clamp e.g. 1..16) so the Euclidean pattern length is a control. `euclid_hit(step, npulses, seq_len)` and `step = (step+1) % seq_len` use it.
- **LPF FREQ + LPF POLE (DC-06):** a cascade of 1 or 2 `tpt1_lp` stages on the GEN body output. `g->lpf_g = tanf(pi*fc/SR)` from LPF FREQ; `g->lpf_pole` toggles 1 stage (2-pole-equivalent path) vs 2 stages (4-pole). This is the sub-bass LPF the HPN spec calls for (Context/02 line 84: "Sub-bass Low-pass Filter (2-pole / 4-pole)").
```c
// GEN body render tail (per sample), replacing gen.c's single color_lp line:
float s = body * amp * 0.85f;
s = tpt1_lp(&g->lpf1, s, g->lpf_g);                 // stage 1 (always)
if (g->lpf_pole) s = tpt1_lp(&g->lpf2, s, g->lpf_g); // stage 2 => 4-pole (DC-06)
```
**Scope-boundary flag:** gen.c currently lives as a *kick model* (rendered into the kick L/R by the vtable dispatch in dsp.c). DC-05 says when GEN is active the generative sequence drives the *groove* voice. Two viable structures — see the explicit flag in Open Questions Q2; the cleaner split is to let GEN render as it does today (it IS the voice) and simply (a) feed it the transport `samples_per_16th`, (b) expose SEQ LEN / LPF FREQ / LPF POLE, and (c) have `ui.c` surface those on a *Groove Page 2* rather than duplicating them on Kick Page 2. The non-GEN groove multitap (Pattern 1) runs in the groove stage of `render_block`; for GEN, the groove-multitap stage is bypassed (the GEN model output already carries the rumble). This matches "GEN generative sequence drives the groove voice; non-GEN = kick-fed multitap" verbatim.

### Pattern 5: Conditional Groove Page 2 in ui.c (GRV-04, DC-05)
**What:** `ui.c` (`omega_build_ui`) currently emits fixed `root` + `kick1` + dynamic `kick2` levels. Phase C adds a `groove1` level (always present, 8 slots per Pattern 3) and a `groove2` level emitted **only when `inst->model == MODEL_GEN`** (6 slots: SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY).
**How:** Follow the existing bounded `ui_append` pattern (ui.c:89-95). Add static `.rodata` fragments `UI_GROOVE1` and `UI_GROOVE2`; gate the groove2 append on `inst && inst->model == MODEL_GEN`. Add level links in the root or navigation as the phase's UI hint allows (the full nav tree is Phase E — Phase C just needs the levels present and correctly gated).
```c
// in omega_build_ui, after the kick2 splice, before UI_CLOSE:
ui_append(buf, buf_len, &off, UI_GROOVE1, sizeof(UI_GROOVE1)-1);
if (inst && inst->model == MODEL_GEN)
    ui_append(buf, buf_len, &off, UI_GROOVE2, sizeof(UI_GROOVE2)-1);
```
**Buf_len watch:** adding groove1 (~8 slots) + conditional groove2 (~6 slots) grows the hierarchy. A-RESEARCH measured native `ui_buflen=4096`; the real on-device cap is still an Open Question (Phase A→E spike). Keep fragments compact and verify the full serialized string with groove2 present stays inside the smallest plausible cap; the bounded `ui_append` already truncates safely, but a truncated hierarchy is a UI bug. Flag for the Phase-E buf_len measurement.

### Pattern 6: Signal path & Phase-D insertion point
**What:** In `dsp.c omega_render_block`, after the kick renders into `l[]`/`r[]`, run the groove stage and sum. Leave ONE obvious insertion point for Phase D.
```c
// omega_render_block, after g_models[...]->render(inst, l, r, frames):
if (inst->model != MODEL_GEN) {          // GEN's own output IS the rumble (DC-05)
    groove_update_tempo(&inst->groove, g_host, frames);   // once per block (Pattern 2)
    for (int n = 0; n < frames; n++) {
        float gl, gr;
        groove_tick(&inst->groove, l[n], r[n], &gl, &gr); // Patterns 1+3
        l[n] += gl;  r[n] += gr;                          // kick + groove sum
    }
}
// <<< PHASE D INSERTION POINT: duck -> DJ filter -> soft clip go HERE, on l[]/r[] >>>
for (int n = 0; n < frames; n++) {
    out_lr[n*2]   = omega_to_i16(l[n] * inst->main_volume);   // FNDTN-07
    out_lr[n*2+1] = omega_to_i16(r[n] * inst->main_volume);
}
```
For GEN, the groove-multitap stage is skipped and `groove_update_tempo` still runs (to feed GEN's `samples_per_16th`); the GEN model render already produced the rumble into `l[]`/`r[]`. The `kick + groove` sum can exceed 1.0 — the existing `omega_to_i16` clamp is the net (Phase D's soft clip is the real bound). Do NOT hand-roll a clamp in the groove stage; leave headroom management to Phase D.

### Anti-Patterns to Avoid
- **Hardcoded tap interval** (`sample_rate * 0.125f`, Context/04 line 118) — the single forbidden reference bug. Tap interval MUST come from the tempo clock (Pattern 2).
- **Per-sample `get_beat_position()` / division** — read the beat once per block; compute `60/bpm` once per BPM change. A per-sample transcendental/division in the render loop violates the project's control-rate/render-rate split.
- **`% MAX_DELAY_FRAMES` per sample** — use the power-of-two mask (DC-01), not integer modulo, in the hot loop.
- **Unbounded `fast_tanh` / raw `(int16_t)(x*32767)`** (Context/04 lines 91-94, 152-153) — the groove sum uses the existing bounded `omega_to_i16`; no bespoke clipper in Phase C (that's Phase D).
- **`atof`/`atoi` in set_param** (Context/04 line 165-166) — use the in-tree locale-independent `parse_f` (as gen.c/dsp.c already do).
- **Logging on the audio thread** — no `host->log` in groove/render/set_param (all six entry points are on the SPI audio thread).
- **Reverb/allpass/diffusion** — explicitly out of scope (DC-07); dry multi-tap only.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Swept low-pass (COLOR, Page-2 LPF) | New biquad or ladder | `tpt1_lp` (dsp_primitives.h) cascaded for 4-pole | TPT chosen over biquad for swept filters (CLAUDE.md); zipper-free; DC-06 mandates the cascade |
| Per-tap decay envelope | New envelope math | `env_t` + `env_coeff_from_ms` or a precomputed decay weight | Shared one-pole exp decay is the project idiom (D-05) |
| Generative sequence | New PRNG/sequencer | The existing `gen.c` engine + `prng_t`/`scale_quantize`/Euclidean | Already built (KICK-11); Phase C only re-clocks it (DC-05) |
| Deterministic randomness | `rand()` | `prng_t` xorshift64 | libc-independent, reproducible (STATE.md forbids rand()) |
| Float→int16 at output | `(int16_t)(x*32767)` | `omega_to_i16` (clamp+isfinite+lrintf) | Reference bug (Context/04 152-153); FNDTN-07 |
| Param string parse | `atof`/`strtod` | in-tree `parse_f` | Locale-safe (UI-01); reference used atof (Context/04 165) |
| Instance allocation | per-buffer malloc | grow the single calloc struct by value | CLAUDE.md single-alloc; DC-01/DC-08 |
| Delay ring wrap | `% N` per sample | power-of-two `& mask` | Branch-free, no per-sample integer division (DC-01) |

**Key insight:** Phase C is 90% assembly of proven primitives. The only genuinely new code is the tempo clock (Pattern 2) and the groove struct plumbing. The value-at-risk is entirely in getting `get_beat_position()` derivation correct and RT-safe — retire that with the offline harness before touching sound design.

## Common Pitfalls

### Pitfall 1: Repeating the hardcoded-BPM reference bug (the whole reason DC-02 exists)
**What goes wrong:** Copying Context/04's `int tap_interval = (int)(inst->host->sample_rate * 0.125f);` — a 16th note locked to 120 BPM forever; the rumble drifts out of time at any other tempo. gen.c has the same latent bug in `GEN_STEP_FRAMES 5088` (~130 BPM).
**Why:** The reference module was written for a fixed demo tempo; the value looks like real code.
**How to avoid:** Tempo comes ONLY from Pattern 2's `groove_update_tempo`. Grep the final diff for `0.125f`, `* sample_rate *`, and `GEN_STEP_FRAMES` — none may set a live interval. The 120 constant may appear ONLY in the last-resort fallback branch.
**Warning signs:** Tap positions identical across the 120/128/174-BPM harness runs (they must differ); rumble audibly off-grid on-device at non-120 tempo.

### Pitfall 2: NULL / negative transport callbacks crash or produce garbage BPM
**What goes wrong:** `host->get_beat_position` is NULL on older hosts → NULL-deref; or returns `< 0` when transport is stopped → a negative/nonsense BPM; or the first block computes a huge delta because `prev_beat` was never seeded.
**Why:** The host ABI marks these optional (omega.h: callbacks may be NULL); stopped transport returns < 0 (DC-02).
**How to avoid:** The strict guarded chain in Pattern 2: NULL-check every callback; treat `beat < 0` as "stopped" and drop `prev_beat`; require `have_prev_beat` before computing a delta; sanity-clamp `dbeat` (0 < dbeat < 4) and BPM (20..300). Fall through to `get_bpm`, then to the 120 constant.
**Warning signs:** SIGSEGV on a host without transport callbacks; BPM spikes / silence on transport start-stop; first-block tempo glitch.

### Pitfall 3: Tap read reaches past the ring / min-BPM overflow
**What goes wrong:** At very low BPM, `4 × samples_per_16th` can exceed the ring length (at 20 BPM `samples_per_16th ≈ 33075`, ×4 ≈ 132300 > 131072), so tap 4 reads its own future / aliases the write head.
**Why:** The ring is finite (~2 s); four 16ths at very slow tempo exceed 2 s only below ~20 BPM for the mask size, but the clamp must be explicit.
**How to avoid:** Clamp `samples_per_16th` so `4 × spq ≤ ring_len` (Pattern 2 shows the clamp), and clamp min BPM to 20. The 88200-frame (2 s) literal covers 4 taps down to ~34 BPM; the 131072 mask covers down to ~20 BPM. Document the min musical tempo.
**Warning signs:** Tap 4 sounds like feedback/comb at slow tempo; buffer self-read artifacts.

### Pitfall 4: BPM jitter causes zipper as tap positions jump every block
**What goes wrong:** Raw per-block beat-delta is noisy (host may report beat position with quantization); recomputing `samples_per_16th` every block makes read positions jump each buffer → clicks/zipper on the rumble.
**Why:** Per-block delta has integer-frame and host-scheduling jitter.
**How to avoid:** Smooth BPM (one-pole EMA) and only recompute the interval when smoothed BPM moves past a threshold (Pattern 2). Integer tap positions still jump by whole frames when they do change, but a rare, thresholded jump is inaudible vs. a per-block jitter.
**Warning signs:** Grainy/zippering rumble at steady tempo; the determinism test failing (output not byte-stable at fixed driven BPM).

### Pitfall 5: DC-08 assert under-sized for the mask buffer
**What goes wrong:** Using the 131072 mask makes the two float ring buffers ~1.0 MB; `sizeof(bohm_instance)` exceeds DC-08's example bound of 1,100,000 and the `_Static_assert` fails the build.
**Why:** DC-01 says "88,200 frames × 2ch = ~705 KB" (that's the modulo/88200 sizing), but DC-01 *also* prefers the 131072 mask, which is larger (~1.0 MB).
**How to avoid:** Compute the true `sizeof` and set the assert just above it (Option A in §Recommended state placement). Treat DC-08's "1,100,000" as the illustrative "e.g." it is worded as. See Open Questions Q1.
**Warning signs:** Compile-time `_Static_assert` failure "instance under 1.1MB".

### Pitfall 6: Growing ui_hierarchy past the host buf_len cap
**What goes wrong:** Adding groove1 + conditional groove2 levels overflows the host's `get_param("ui_hierarchy")` buffer; the bounded `ui_append` truncates → malformed JSON / missing pages.
**Why:** The real per-host cap is unmeasured (A-RESEARCH Open Q1; native harness saw 4096, on-device TBD).
**How to avoid:** Keep fragments compact; add a test asserting the full serialized hierarchy WITH groove2 present is brace/bracket-balanced and null-terminated (extend `test_switch.c`'s `assert_p2_json_valid` style). Flag the on-device buf_len measurement (Phase E dependency).
**Warning signs:** Garbled Groove pages on-device; JSON-validity test failing when GEN is active.

## Code Examples

Concrete, in-tree-consistent snippets are inlined in Architecture Patterns 1–6 above (all grounded in `dsp_primitives.h`, `gen.c`, `dsp.c`, and Context/04's structure minus its bugs). The load-bearing ones:
- **Tempo clock (Pattern 2)** — the DC-02/GRV-02 crux; read once/block, guarded fallback chain, EMA + threshold re-lock.
- **Circular delay 4-tap read (Pattern 1)** — mask wrap, tempo-driven offsets.
- **Groove Page-1 tail + MONO (Pattern 3)** — `tpt1_lp` COLOR + `0.5*(L+R)` mono sum.
- **GEN LPF cascade (Pattern 4)** — 1/2 `tpt1_lp` stages for DC-06.
- **Signal sum + Phase-D insertion (Pattern 6)**.

## Runtime State Inventory

Phase C adds runtime state but is **not** a rename/refactor/migration; still, the state-placement discipline matters for RT-safety:

| Category | Items | Action |
|----------|-------|--------|
| Stored data | None (no databases/datastores) | None — verified: module is a single `dsp.so`, no persistence until Phase G presets |
| Live service config | None | None — no external services |
| OS-registered state | None | None |
| Secrets/env vars | None | None |
| Build artifacts | `groove_state_t` grows `bohm_instance`; raise DC-08 `_Static_assert`; new `PK_GRV_*` / `PK_GEN_SEQLEN`/`PK_GEN_LPFFREQ`/`PK_GEN_LPFPOLE` macros in omega.h | Code edit only (compile-time). No stale artifacts — native `make test` and Docker `make dsp.so` rebuild from source. |

## State of the Art

| Old Approach (in-tree / reference) | Current Approach (Phase C) | Impact |
|-----------------------------------|----------------------------|--------|
| Reference `tap_interval = sample_rate*0.125f` (120-BPM hardcode, Context/04) | Beat-delta tempo derivation, once/block, EMA-smoothed (Pattern 2) | Rumble tracks project tempo (GRV-02 SC1) |
| gen.c `GEN_STEP_FRAMES 5088` self-clock (Phase B, ~130 BPM) | Transport-driven `samples_per_16th` fed into gen.c (DC-05) | GEN sequence locks to host tempo |
| gen.c single `color_lp` (Phase B) | Groove Page-2 LPF FREQ + 2/4-pole cascade (DC-06) | Sub-bass LPF matches HPN spec (Context/02) |
| `% MAX_DELAY_FRAMES` (reference) | power-of-two `& mask` (DC-01) | Branch-free, no per-sample division |
| gen.c fixed `GEN_SEQ_LEN 16` | runtime SEQ LEN control (GRV-04) | Variable pattern length |

**Deprecated/outdated:** Context/04's `bohm_render_block` remains a *learning* reference only — it carries the hardcoded-BPM tap interval, amplitude-threshold ducking (Phase D will replace with note-event trigger per PERF-01), unbounded `fast_tanh`, and raw int16 cast. Do not copy it; extract only the circular-buffer *shape*.

## Environment Availability

No new external dependencies. The Phase-A toolchain covers Phase C.

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| native cc/clang | `make test` (offline harness) | ✓ (macOS host) | system | none |
| Docker + `schwung-builder` image | `make dsp.so` cross-build + glibc gate | via CI (no local Docker on this host, per A-04/B) | pinned in CI | CI is the authoritative build gate |
| Move hardware + scp | on-device tempo-lock feel confirmation | ✗ locally (A-04/B-09 pending) | — | offline harness validates logic; on-device feel deferred |
| `host->get_beat_position` / `get_bpm` | live tempo (GRV-02) | mock drivable in harness; real host on-device | — | guarded fallback chain (Pattern 2) covers absence |

**Missing with no fallback:** none blocking. **Missing with fallback:** on-device tempo-lock *feel* is deferred to a later on-device round (same posture as A-04/B-09); the offline harness fully validates the tempo *logic*.

## Validation Architecture

> nyquist_validation is enabled (config.json `workflow.nyquist_validation: true`).

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Plain C `assert` + custom runners (native `cc`); no third-party framework |
| Config file | none — the Makefile `test` target IS the config |
| Quick run command | `make test` (aggregates test-fm2/fx/switch/params/distinct/gen; < ~10 s) |
| Full suite command | `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so` (native + cross-build + glibc/libmvec/export gate) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| GRV-01 | 4-tap kick-fed delay: taps read at N/2N/3N/4N frames behind write head; each scaled by TAP level; bounded/finite output | unit | `./build/test_groove` (new) — trigger kick, render, assert tap-delayed energy present + finite/bounded | ❌ Wave 0 |
| GRV-02 | Tap positions track driven BPM: drive mock `get_beat_position` at 120/128/174 BPM, assert `samples_per_16th` (and thus tap read offsets) differ and equal `(60/bpm)*sr/4 ±1`; NULL callback → get_bpm fallback → 120 constant, each exercised | unit | `./build/test_groove` — parametric BPM sweep + fallback-chain cases | ❌ Wave 0 |
| GRV-03 | Page-1 controls change output: VOL/LENGTH/COLOR/TAP1-4 lo-vs-hi produce measurably different buffers (RMS or spectral delta); bounded at extremes | unit | `./build/test_groove` — reuse `assert_param_responsive` style over groove keys | ❌ Wave 0 |
| GRV-04 | GEN Page-2 present only for GEN: `ui_hierarchy` contains `groove2`/SEQ LEN/LPF keys iff model==GEN; hidden for FM2..USR. GEN seq clocks to driven BPM; SEQ LEN/DENSITY/SCALE/SEED change output; LPF POLE 2 vs 4-pole audibly differ; determinism (same SEED → byte-identical) | unit | `./build/test_groove` + extend `test_switch.c assert_*_json_valid`; extend `test_gen.c` for transport-clock determinism | ❌ Wave 0 |
| GRV-05 | MONO force-sums L+R: with MONO on, groove L==R; with MONO off and asymmetric taps, L!=R | unit | `./build/test_groove` — assert channel equality under MONO | ❌ Wave 0 |

Cross-cutting automated asserts (every render path): no NaN/Inf (`isfinite` sweep), `|x| ≤ 1.0` pre-int16, zero audio-thread alloc (malloc trap, Linux CI), determinism where seeded.

Success-criteria mapping (ROADMAP Phase C):
- SC1 (rumble tracks BPM, never 120-hardcoded) = GRV-02 offline harness (parametric BPM) + on-device feel (deferred).
- SC2 (Page-1 controls shape rumble) = GRV-03 offline + on-device listen.
- SC3 (MONO sub-bass sum) = GRV-05 offline.
- SC4 (GEN Page-2 generative + hidden for others) = GRV-04 offline (JSON gating + determinism + BPM-clock).

### Sampling Rate
- **Per task commit:** `make test` (native; < ~10 s) — must stay green.
- **Per wave merge:** `make test && make dsp.so && ./scripts/glibc_gate.sh build/dsp.so`.
- **Phase gate:** full suite green in GitHub Actions + the (deferred) on-device tempo-lock feel check folded into the later voicing round, before `/gsd:verify-work`.

### Wave 0 Gaps
- [ ] `tests/mock_host.c` — **extend** `mock_beat` to be drivable (a settable module-static `double g_mock_beat` + a setter, or a function-pointer the test advances per block) AND wire a `mock_get_bpm` stub onto `h.get_bpm` (currently NULL). Add a "NULL transport" host variant (both callbacks NULL) to exercise the last-resort 120 path. **This is the enabling gap for the entire GRV-02 test.**
- [ ] `tests/test_groove.c` — new harness: drive beat position at 120/128/174 BPM, assert tap offsets track; assert MONO channel equality; assert Page-1 param responsiveness; assert bounded/finite. Add to `Makefile` (`GROOVE_TEST_SRCS` + `test-groove` target + include in `test`).
- [ ] Extend `tests/test_switch.c` — assert `groove2` level (and SEQ LEN/LPF FREQ/LPF POLE keys) present in `ui_hierarchy` iff `model==MODEL_GEN`, absent otherwise; full hierarchy stays brace/bracket-balanced + null-terminated with groove2 present.
- [ ] Extend `tests/test_gen.c` — GEN determinism must hold with the transport clock (same driven BPM + same SEED → byte-identical); SEQ LEN control changes the pattern.
- [ ] Framework install: none — plain C, system `cc`.

*(Existing infrastructure — WAV writer, malloc trap, mock host skeleton, param-responsiveness battery — is reused; the mock-host beat drivability is the one real new piece.)*

## Open Questions

1. **DC-08 assert bound vs. the mask buffer size**
   - What we know: DC-01 prefers the 131072 power-of-two mask; that makes the two float rings ~1.0 MB, pushing `sizeof(bohm_instance)` to ~1.24 MB. DC-08's example bound is `< 1,100,000`.
   - What's unclear: whether the planner takes the mask (needs a higher assert, ~1,300,000) or the literal 88200 modulo (fits under 1,100,000).
   - Recommendation: use the mask (RT performance, DC-01 preference) and size the assert to the true computed `sizeof` (Option A). DC-08 worded the number as "e.g." — treat it as illustrative, not a hard cap. Confirm Move RAM headroom holds (16 tracks × ~1.24 MB ≈ 20 MB — still acceptable; DC-08's 14 MB estimate assumed the smaller buffer).

2. **GEN-as-groove structural split (DC-05)**
   - What we know: DC-05 says when GEN is active its sequence drives the *groove* voice; gen.c today renders as a *kick model*. The clean reading: GEN's model output already *is* the rumble, so Phase C (a) feeds it the transport `samples_per_16th`, (b) exposes SEQ LEN/LPF FREQ/LPF POLE, (c) surfaces them on Groove Page 2 (not Kick Page 2), and (d) bypasses the kick-fed multitap stage when model==GEN.
   - What's unclear: whether the planner keeps GEN's Phase-B minimal Kick-Page-2 (SEED/SCALE/DENSITY) AND adds a Groove Page 2, or *moves* those three to Groove Page 2 (leaving GEN's Kick Page 2 as just FX). REQUIREMENTS GRV-04 lists all six (SEED/SCALE/SEQ LEN/LPF FREQ/LPF POLE/DENSITY) on Groove Page 2, which argues for moving SEED/SCALE/DENSITY to Groove Page 2 and dropping GEN's Kick Page 2 interior.
   - Recommendation: put all six GRV-04 controls on Groove Page 2; make GEN's Kick Page 2 emit no model interior (just FX TYPE/AMT, which the ui.c splice already handles gracefully). This matches GRV-04 verbatim and avoids duplicate SEED/SCALE/DENSITY controls. **Flag for planner confirmation** — it changes gen.c's `p2_slot_desc` and touches the B-09 dynamic-splice behavior (the `assert_p2_json_valid` GEN count would drop from 3 to 0-interior).

3. **New param-key macros needed in omega.h**
   - What we know: omega.h has `PK_GEN_SEED`/`PK_GEN_SCALE`/`PK_GEN_DENSITY` but NOT SEQ LEN, LPF FREQ, LPF POLE, nor any `PK_GRV_*` (VOL/LENGTH/COLOR/TAP1-4/MONO). gen.c has no `seq_len`/`lpf` fields.
   - What's unclear: exact key strings (naming convention).
   - Recommendation: add `PK_GRV_VOL`/`PK_GRV_LENGTH`/`PK_GRV_COLOR`/`PK_GRV_TAP1..4`/`PK_GRV_MONO` and `PK_GEN_SEQLEN`/`PK_GEN_LPFFREQ`/`PK_GEN_LPFPOLE`, following the existing lowercase-string convention (`"grv_vol"`, `"gen_seqlen"`, etc.). Keys must be unique across the flat strcmp dispatch (Pattern from B-01). Groove keys dispatch in `omega_set_param` (dsp.c) to a new `groove_set_param`, NOT through the kick model vtable (the groove voice is model-independent for non-GEN; the GEN Page-2 keys route to gen.c).

4. **Where groove `set_param` dispatch lives**
   - What we know: dsp.c routes all non-model/master keys through `g_models[inst->model]->set_param`. Groove Page-1 controls are model-independent.
   - What's unclear: whether groove keys go through the model vtable or a dedicated path.
   - Recommendation: add a groove-key branch in `omega_set_param` (dsp.c) that calls a new `groove_set_param(&inst->groove, key, val)` before the model-vtable fallback. GEN Page-2 keys (SEQ LEN/LPF/etc.) can still route through the GEN model's `set_param` since they configure gen.c state. Keep the tempo clock in the groove struct so both paths read one `samples_per_16th`.

5. **On-device tempo-lock feel** (deferred, same posture as A-04/B-09)
   - What we know: the offline harness proves the tempo *math* (tap offsets track driven BPM). Real-transport jitter, host beat-position quantization, and the audible "in-the-pocket" feel need hardware.
   - Recommendation: fold a Groove tempo-lock check into the deferred on-device voicing round; not a code blocker.

## Sources

### Primary (HIGH confidence)
- `Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` — rumble signal flow (§1-2), 16th multi-tap emphasis, `Y_mono=0.5×(X_L+X_R)` mono rule (§2), LPF <150 Hz sub-bass role (§4). Note: the reverb "smear" (§1 Stage 1) is explicitly out of scope per DC-07.
- `Context/02_OHMFORCE_BOHM_SYSTEM_SPEC.md` — Groove Expander params (§4: TAP1-4/LENGTH/COLOR/VOL) and HPN "redefines the Groove circuit" + "Sub-bass Low-pass Filter (2-pole/4-pole)" / Sequence Length / Seed / Scale (§2 model 10, DC-05/DC-06 basis).
- `Context/04_BOHM_SCHWUNG_MODULE_DESIGN.md` — reference `groove_state_t` + 4-tap circular-delay render (lines 30-127). STUDIED for shape; the hardcoded 120-BPM `tap_interval` (line 118), unbounded int16 cast (152-153), and `atof` (165) are the forbidden bugs.
- `src/omega.h` — LOCKED ABI (host_api_v1_t `get_beat_position`/`get_bpm` at +... ; `bohm_instance` layout + the `< 800000` assert to raise per DC-08; PK_* macros).
- `src/dsp.c` — `omega_render_block` (kick+groove sum site), `g_host` handle, `omega_set_param` dispatch, single calloc in `omega_create`.
- `src/models/gen.c` — the Phase-B generative engine (self-clock `GEN_STEP_FRAMES`, `GEN_SEQ_LEN`, `prng_t`, `scale_quantize`, Euclidean `euclid_hit`, `color_lp`) that Phase C re-clocks and extends.
- `src/dsp_primitives.h` — `tpt1_lp`, `env_t`/`env_coeff_from_ms`, `prng_t`, `scale_quantize`, `omega_to_i16`, `NUM_SCALES`.
- `src/ui.c` — `omega_build_ui` bounded `ui_append` + dynamic-splice pattern to extend with groove1/groove2 levels.
- `.planning/phases/A-foundation-fm2-model/A-RESEARCH.md` — established primitives, RT-safety rules, mock-host + WAV harness patterns, ui_hierarchy buf_len open question.
- `.planning/STATE.md` — the five reference bugs (DC-02 kills the hardcoded-BPM one); the B-08 note that gen.c's transport-sync + full Groove Page 2 are explicitly Phase C.

### Secondary (MEDIUM confidence)
- Standard multi-tap-delay / tempo-sync DSP practice (beat-delta → BPM → samples-per-subdivision; EMA smoothing) — corroborated by the DC-02 formula and general real-time audio convention; the *feel* remains an on-device unknown.

## Metadata

**Confidence breakdown:**
- Circular delay + Page-1 controls (GRV-01/03/05): HIGH — direct assembly of in-tree primitives + Context/04 shape (minus its bugs).
- Tempo derivation (GRV-02): HIGH on the algorithm and RT-safety (formula from DC-02, guarded chain from the ABI); MEDIUM on smoothing constants and on-device feel (tune by ear, deferred).
- GEN↔Groove wiring + conditional Page 2 (GRV-04/DC-05/DC-06): HIGH on mechanism (gen.c already has the engine; ui.c already splices dynamically); the structural split (Open Q2) needs a one-line planner decision.
- Validation: HIGH — the mock host's beat position is drivable once the Wave-0 gap is closed; every requirement maps to an offline assert.

**Research date:** 2026-09-29
**Valid until:** ~2026-10-29 (stable C/DSP domain; re-check if the Schwung host ABI or `get_beat_position` semantics change, or if the DC-08 buffer sizing is revisited).
