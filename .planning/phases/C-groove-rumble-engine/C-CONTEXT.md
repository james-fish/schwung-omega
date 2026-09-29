# Phase C: Groove Rumble Engine - Context

**Gathered:** 2026-09-29
**Status:** Ready for planning
**Source:** Orchestrator judgment per user directive ("continue in auto; trust your judgment on technical, ask only on critical UI/behavior"). ROADMAP + REQUIREMENTS (GRV-01..05) are authoritative; this locks the technical reading + a few judgment calls.

<domain>
## Phase Boundary

Add the Groove rumble voice fed from the kick, tempo-locked to the project clock, with Groove Page 1 controls and the GEN model's Groove Page 2 generative controls. Phase C produces the `kick + groove` sum. The full performer chain (ducking, DJ filter, soft clip, CPU budget) is **Phase D — out of scope here**.

**In scope:** GRV-01..GRV-05.
**Out of scope:** ducking/duck env (Phase D), DJ filter, output soft-clip, CPU measurement (Phase D); UI nav tree (Phase E); macros (F); presets (G).

</domain>

<decisions>
## Implementation Decisions

### DC-01: Groove = kick-fed 4-tap 16th-note multi-tap delay (GRV-01)
Write the per-sample kick output into a pre-allocated circular delay buffer; read 4 taps at 16th-note offsets back out, each scaled by its TAP level. Buffer pre-allocated in `create_instance` (88,200 frames × 2ch = ~705 KB), by value in the single instance calloc. Planner's discretion: modulo wrap vs power-of-two mask (round MAX_DELAY_FRAMES up to 131072 for branch-free `& mask`) — prefer the mask.

### DC-02: Tempo from the host clock, NEVER hardcoded 120 (GRV-02)
Derive BPM from `host->get_beat_position()` beat-delta across blocks; `samples_per_16th = (60/bpm) × sr / 4`, integer tap positions for v1. **Guard the transport edge cases** (this is the reference-code bug we must not repeat): `get_beat_position()` returns < 0 when no transport is running, and the pointer may be NULL on older hosts — fall back to `host->get_bpm()` (also NULL-guard), then to a 120 default ONLY as the last-resort constant, never as the live value. Recompute tap interval at control rate when BPM changes so the rumble re-locks across tempo changes.

### DC-03: Groove Page 1 — 8 encoders (GRV-03)
VOL (groove master), LENGTH (per-tap decay), COLOR (TPT SVF low-pass timbre on the groove voice — reuse the existing `tpt1`/SVF primitive), TAP1, TAP2, TAP3, TAP4 (individual 16th-tap levels), MONO (toggle).

### DC-04: MONO force-sum (GRV-05)
MONO toggle sums L+R to mono on the groove voice for sub-bass club routing (standard below ~150 Hz). Apply to the groove signal; the kick path is unaffected.

### DC-05: GEN ↔ Groove Page 2 integration (GRV-04) — behavior decision
Groove Page 2 (SEED, SCALE, SEQ LEN, LPF FREQ, LPF POLE, DENSITY) is shown **only when the active model == GEN (MODEL_GEN)** and hidden for all other models (ui.c already assembles Page 2 dynamically; extend it to conditionally emit a Groove Page 2 for GEN). Behavior per the Bohm spec (Context/02: HPN "redefines the Groove circuit"): **when GEN is active, its generative scale-quantized pitch sequence drives the Groove voice** (the generative engine built in Phase B now connects to these Page-2 controls + the tempo clock), rather than the plain delayed-kick multitap. For all non-GEN models, Groove is the kick-fed multitap of DC-01. The GEN generative engine already exists (KICK-11); Phase C wires its SEED/SCALE/SEQ-LEN/DENSITY controls to Groove Page 2 and clocks it from the transport (DC-02), and adds the Groove LPF (LPF FREQ + LPF POLE 2/4-pole).

### DC-06: LPF POLE 2/4-pole toggle (GRV-04)
Implement the Groove Page 2 LPF as cascaded TPT 1-pole low-pass stages: 1 stage = 2-pole equivalent path / 2 stages = 4-pole, toggled by LPF POLE. Reuse the shared TPT primitive; no biquad for the swept filter (CLAUDE.md).

### DC-07: NO reverb / no smear stage (v1 scope)
Traditional techno rumble offers a reverb "smear" path; Bohm — and Omega — deliberately use the **dry multi-tap delay** approach only (confirmed with the user). No reverb, allpass, or diffusion anywhere in the Groove chain. (Deferred idea below if the warehouse-reverb character is wanted later.)

### DC-08: Instance-size static_assert must be raised
Adding the ~705 KB groove delay buffer to `bohm_instance` (which already grew for USR's ~184 KB buffers) pushes `sizeof(struct bohm_instance)` past the current `_Static_assert(... < 800000)`. Raise the bound (e.g. to < 1,100,000) and keep the single-calloc strategy. Confirm Move RAM headroom is fine (16 Movy tracks × ~0.9 MB ≈ 14 MB — acceptable).

### Claude's Discretion
- Circular-buffer wrap strategy (mask vs modulo), exact COLOR/LPF cutoff mappings, per-tap decay curve shape, and how the GEN sequence phase maps onto the tempo grid — choose musically, refine in the (deferred) voicing round.
- Whether the groove voice reuses the shared FX chain or has its own COLOR-only path.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` — rumble signal flow, multi-tap delay stage, COLOR/LPF role, mono-sub practice.
- `Context/02_OHMFORCE_BOHM_SYSTEM_SPEC.md` §Groove Expander + §HPN — Groove taps/LENGTH/COLOR/VOL/MONO semantics; HPN "redefines the Groove circuit".
- `Context/04_BOHM_SCHWUNG_MODULE_DESIGN.md` — the reference groove render (4-tap circular delay); STUDY but do NOT copy its bugs (hardcoded 120 BPM tap interval is exactly DC-02's forbidden pattern).
- `src/omega.h` — LOCKED ABI structs (do NOT alter); `bohm_instance` (grow it for the delay buffer + raise the size assert); `host_api_v1_t` `get_beat_position`/`get_bpm` callbacks.
- `src/models/gen.c` — the Phase B generative engine to connect to Groove Page 2.
- `src/dsp_primitives.h` — `tpt1` SVF (COLOR + LPF POLE cascade), `env_t`, `scale_quantize`/`g_scales`, `prng_t`.
- `src/ui.c` — dynamic Page 2 assembly to extend with the GEN-only Groove Page 2.
- `CLAUDE.md` — groove buffer sizing (~700 KB, single calloc), TPT-over-biquad, RT-safety, no-hardcoded-BPM.

</canonical_refs>

<specifics>
## Specific Ideas

- The five reference-code bugs to NOT carry forward (STATE.md) — DC-02 directly kills the "hardcoded 120 BPM tap interval" one; keep the int16 clamp/isfinite and margin-scratch disciplines.
- Groove output sums with the kick (`kick + groove`) at the module output for Phase C; the duck/filter/clip that sit after this sum are Phase D — leave a clean insertion point.
- RT-safety: delay buffer + all groove/GEN-seq state pre-allocated in `create_instance`; zero audio-thread alloc/log/file-IO; tempo read only via the (NULL/negative-guarded) host callbacks.

</specifics>

<deferred>
## Deferred Ideas

- **Reverb / smear stage** for a warehouse-rumble character (DC-07) — out of scope for v1; revisit if the user wants the reverb-smeared sound.
- **On-device voicing round for Phase B (`docs/VOICING_AUDIT.md`)** — deferred by user; still outstanding UAT debt to complete before shipping. Groove voicing will fold into a later on-device round.
- Fractional/interpolated tap positions (v1 uses integer tap positions per GRV-02).

</deferred>

---

*Phase: C-groove-rumble-engine*
*Context captured: 2026-09-29 — technical decisions locked per user "trust your judgment" directive*
