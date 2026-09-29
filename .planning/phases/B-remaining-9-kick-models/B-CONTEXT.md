# Phase B: Remaining 9 Kick Models - Context

**Gathered:** 2026-09-29
**Status:** Ready for planning
**Source:** User directive (post Phase-A on-device validation)

<domain>
## Phase Boundary

Implement the remaining 9 kick models (FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN) behind the Phase A vtable/registry, each with its own context-sensitive Kick Page 2 (6 model-specific slots assembled from `p2_slot_desc`), the shared post-kick FX chain (Diode/Clip/SAT/Fold/Crush), clean model-switch re-init, and USR WAV loading with zero audio-thread file I/O.

**In scope:** KICK-03..KICK-11, KICK-13, KICK-14 — plus a cross-cutting **voicing round for ALL 10 models including FM2** (see decisions).
**Out of scope:** Groove (Phase C), Performer (Phase D), full nav tree (Phase E), macros (F), presets (G).

</domain>

<decisions>
## Implementation Decisions

### D-B01: Per-model voicing round is a first-class deliverable (not optional polish)
Every model — **including FM2, which was built in Phase A but is NOT yet voiced** (user reports it "sounds like FM but the parameter ranges and CURVE don't sound good yet") — must pass a voicing pass before the phase is considered done. Phase A proved the models *function*; Phase B must make them *sound musical*. Voicing is explicitly in scope for all 10 models: FM2, FM4, WTR, PHY, HRD, DIG, TRS, ANA, USR, GEN.

### D-B02: The voicing checklist (per model)
Each model must satisfy this checklist. Split into automated (offline WAV render + asserts) and manual (on-device listening — the ear is the authority, mirroring the A-04 hardware checkpoint):

**Automated (offline, in `make test` / WAV harness):**
- Renders non-silent output for a default trigger; output finite and clamped ≤ 1.0 pre-int16.
- Each of the model's Kick Page 2 params measurably changes the output (lo vs hi render differs).
- No aliasing blow-up or divergence at extreme param settings (output stays bounded across the full param sweep).
- Model switch into/out of this model re-inits cleanly (no stale-state / NaN carried across a switch).

**Manual (on-device, per-model voicing sign-off):**
- Default/12-o'clock preset sounds like a usable techno kick out of the box (no obvious tuning required to be listenable).
- The PITCH sweep / CURVE (808↔909 character) sounds musical across its range — not clicky, not muddy, decay feels right.
- Each knob sweeps a **musically useful range** end-to-end (no dead zones, no all-the-action-in-the-last-5%). Re-map param min/max and response curves as needed.
- The model has a **distinct sonic character** vs the others (FM2≠FM4≠ANA≠HRD… each earns its slot).
- No clipping, zipper noise, or artifacts when turning knobs live.

### D-B03: FM2 re-voicing specifically
Treat FM2 as the first voicing target and the reference bar. Revisit its parameter ranges (PITCH, LENGTH, SUSTAIN, CURVE, ATTACK, TRS DEC, TRS TNE, COLOR, FM RATIO, FM INDEX, OP2 WAVE) and the CURVE 808↔909 envelope-blend shape. The FM2 DSP code lives in `src/models/fm2.c` (Phase A); voicing changes there are in-scope for Phase B.

### D-B04: Voicing is delivered as (a) per-model tasks + (b) a final voicing audit
Each model's plan carries the automated voicing criteria in its `must_haves`. A dedicated **voicing/audit step** at the end of the phase produces a per-model checklist doc (like `docs/ON_DEVICE_VALIDATION.md`) listing the 10 models with PASS/PENDING for each checklist item, so the on-device manual sign-off is tracked, not skipped. Phase B is not "complete" until that doc is filled on-device (manual verification), same pattern as A-04.

### Claude's Discretion
- Model DSP recipes (modal PM for PHY, band-limited wavetables for WTR, FM4 operator algorithms, ANA analog-style, DIG digital/bitcrush character, TRS 808/909-ish transistor, HRD hard/distorted, GEN generative) — research picks the concrete approach per model; CLAUDE.md tech-stack guidance is the starting point.
- Default parameter values and exact min/max ranges per model — choose musically, refine in the voicing round.
- Order in which models are built/voiced within the phase.
- FX chain implementation details (the 5 modes) beyond "audibly alter the kick, no divergence/clipping."

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `src/host/plugin_api_v1.h` equivalent is already captured in `src/omega.h` (the ABI is now ground-truthed and locked — do NOT alter struct layouts; `_Static_assert`s enforce them).
- `Context/03_TECHNO_RUMBLE_AND_KICK_SYNTHESIS.md` — FM/transient/envelope design, per-engine character, COLOR filter role.
- `Context/02_OHMFORCE_BOHM_SYSTEM_SPEC.md` — Bohm per-model parameter semantics.
- `Context/04_BOHM_SCHWUNG_MODULE_DESIGN.md` — Page 2 assembly, param naming, FX chain.
- `Context/05_SOURCE_INDEX_AND_REFERENCES.md` — reference DSP engines safe to study.
- `CLAUDE.md` — DSP stack guidance (modal PM, band-limited wavetables, TPT filters, FM algorithms, PRNG/scale tables for GEN); RT-safety rules.
- `.planning/phases/A-foundation-fm2-model/A-RESEARCH.md` — established FM2 recipe, shared primitives (env_t, wt_read, tpt1), vtable pattern to extend.

</canonical_refs>

<specifics>
## Specific Ideas

- Reuse Phase A shared primitives (`env_t`, `wt_read` on the `.rodata` sine table, `tpt1` TPT 1-pole); add new shared primitives (modal resonator, band-limited wavetable read, PRNG, scale-quantize table) in `dsp_primitives.*` so all models share them.
- Extend `model_registry.c` by appending vtable entries only (model IDs are append-only; MODEL_FM2 stays 0).
- The voicing manual round needs the same deploy loop that worked for A-04 (CI artifact → `scripts/deploy.sh` → trigger on Move). Bake the exact commands into the voicing audit doc.
- **RT-safety reminder learned in Phase A:** no logging of any kind on the audio thread (no `host->log` in any entry point); USR file load off the audio thread at `create_instance`.

</specifics>

<deferred>
## Deferred Ideas

- buf_len measurement (was the removed D-10 spike) — capture off-thread in Phase E when the full hierarchy is sized.

</deferred>

---

*Phase: B-remaining-9-kick-models*
*Context captured: 2026-09-29 — voicing checklist locked per user directive*
