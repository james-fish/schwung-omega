---
phase: quick-260930-vhr
verified: 2026-09-30T00:00:00Z
status: human_needed
score: 10/10 must-have truths verified (automated); 1 blocking on-device listening gate outstanding
human_verification:
  - test: "On-device TAPS listening checklist (Task 4, blocking gate)"
    expected: "LENGTH full-right + all TAP1-4 full sounds like the same kick placed on every 16th (equal-level, pummeling, not decaying/bit-crushed); LENGTH full-left = stable smeared drone; COLOR audibly dials darkness; rumble level-matches kick at VOL~0.8/DRIVE=0; REVERB left=pre-smear / center=off / right=post; live BPM change is click-free; GEN unchanged."
    why_human: "Requires Move hardware + a gate-passing aarch64 dsp.so (Docker unavailable on this host). Perceptual audio quality (darkness feel, pummel, click-free glide, level match) cannot be verified programmatically. This is the same on-device UAT posture as prior phases (A-04 / B-09)."
---

# Quick 260930-vhr: TAPS Rumble Redesign Verification Report

**Phase Goal:** Pick up the TAPS rumble redesign — bidirectional LENGTH (right = equal-level clean kick copies on every 16th; left = smeared resonant drone), no per-tap attenuation, loudness fixed without a hardcoded x3 makeup, COLOR as the darkness tool, bidirectional pre/off/post REVERB, fractional/slewed tempo-synced taps, GEN unchanged.
**Verified:** 2026-09-30
**Status:** human_needed (all automated must-haves verified; one blocking on-device listening gate remains, by design — Task 4)
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | At LENGTH max with all TAP1-4 up + chain neutralised, the added output = equal-level clean copies of the RAW kick on every 16th (ratio 1.0, xcorr 1.0 within eps, per equal-power 0.5 scale) | ✓ VERIFIED | Direct settled-spq engine trace (see below): offset=0, rel_err=0.00000, amp_ratio=1.00000, xcorr=1.00000. Engine yields EXACT copies. groove.c:420-450 writes RAW kick to ring (fb=0 at v=1), 4 equal taps × tap_norm=0.5. |
| 2 | At LENGTH max all 4 taps are equal level (no per-tap decay) | ✓ VERIFIED | `tap_decay[4]` removed from groove.h/groove.c (grep: 0 hits). Per-tap weight is `tap_level[t]*tap_trim` with a single global tap_trim (groove.c:444). gate2: tap4/tap1 = 0.169 dB. |
| 3 | At LENGTH min the TAPS voice is a bounded, stable smeared feedback drone (30 s) | ✓ VERIFIED | fb_amount=0.85 at v=0, feedback-path 30 Hz HP + 2-pole COLOR LP + diffusion (groove.c:352-450). gate3: mid=0.1025, last=0.0455 (bounded, non-runaway) over 30 s. |
| 4 | COLOR is the darkness control; no hidden fixed 1.5 kHz loop LP | ✓ VERIFIED | `FB_LP_A` (old hidden 1.5 kHz const) removed (grep: 0 hits). Loop LP + output LP both driven by color_g (groove.c:405-413, 476-487). gate6/7: ZCR lo=0.0442, hi=0.0544 (COLOR darkens; HF retained when open). |
| 5 | Loudness stays roughly constant across LENGTH; level-matches kick (not too quiet) | ✓ VERIFIED | Equal-power tap_norm + tap_trim loudness comp (groove.c:83-88, 97). gate4: RMS spread across LENGTH sweep = 0.10 dB. |
| 6 | Never bit-crushed; no hardcoded x3.0 makeup into a saturator | ✓ VERIFIED | `gl *= 3.0f`/`gr *= 3.0f` TAPS makeup removed (grep: 0 hits; the GEN `gen_fold*3.0f` at groove.c:330 is intentionally preserved). gate5: 0.0000% of samples at the int16 rail. |
| 7 | REVERB MIX is bidirectional (left=pre, center=off, right=post); reuses the Schroeder | ✓ VERIFIED | PK_GRV_RVMIX handler replaced with pre/off/post split + ±0.03 center deadzone (groove.c:576-591). One `groove_reverb_mono` helper called at PRE (groove.c:422-428) or POST (groove.c:514-519). Default 0.5 = off. gate10: pre-path 30 s stable + non-trivial (last=0.0755). |
| 8 | Tap spacing is fractional + slewed so live BPM changes glide without clicks | ✓ VERIFIED | `spq`/`spq_target` float fields; per-sample slew SPQ_SLEW=0.0005 (groove.c:352); linear-interp fractional reads (groove.c:358-365, 436-447). gate9: max inter-sample jump=0.3052 (no whole-sample click). |
| 9 | render_block does zero allocations; no NaN/Inf; peak <= 1.0 across all TAPS states | ✓ VERIFIED | malloc_trap linked; groove_tick has no malloc/log/file-IO. Per-sample banned ops: only `floorf`. gate8 finite/peak asserts + zero-alloc guard PASS across all gates. |
| 10 | GEN groove voice behaviour is unchanged | ✓ VERIFIED | `git diff a3a4a20 HEAD -- src/groove.c`: zero changed lines touch any gen_* logic, groove_gen_rebuild/restart, or the GEN tick block. Removed lines are exclusively old-TAPS code. |

**Score:** 10/10 truths verified (automated).

### Deep-Dive: Gate #1 Honesty Check (scrutinised per verification request)

The suite reports gate #1 at xcorr=0.967, rel_err=0.256, best_lag=6289 vs S=6300 (an ~11-sample offset). This was flagged as possibly hiding a real defect behind relaxed thresholds (xcorr>0.95, rel_err<0.30). **Finding: the thresholds do NOT hide an engine defect.**

Root cause of the deviation, proven two ways:
1. **Slew-convergence probe:** `SPQ_SLEW=0.0005` is a ~30 ms one-pole glide. The test warms up only 40 blocks (~5120 samples) after the 120→105 BPM change before triggering. Starting spq=5512.5 (120 BPM) slewing toward 6300 (105 BPM), spq is still ~6226 at trigger and only ~6297 by the tap-1 window — never reaching 6300. So the effective tap delay is a few samples short and still gliding during measurement. This exactly explains best_lag=6289 and the residual rel_err.
2. **Direct settled-spq engine trace** (drove `groove_tick` directly with spq forced to the exact integer S=6300, LENGTH=1, all taps=1, filter OFF): **offset=0, rel_err=0.00000, amp_ratio=1.00000, xcorr=1.00000.** With spq settled, the engine produces a mathematically EXACT, equal-level clean copy of the raw kick (0.5× per the stated equal-power scale).

Conclusion: the engine genuinely satisfies the user intent "sounds exactly like the kick on every 16th." The gate #1 deviation is a **test-harness warm-up artifact** (too-short settle for the intentionally slow slew), not a defect in the read/slew/tap stage. The relaxed thresholds are realistic tolerances for a sharp FM2 transient measured across two independently int16-quantized, still-slewing renders — they are honest, not a masked failure. `amp_ratio=0.988` independently confirms equal level even in the un-settled test.

### Required Artifacts

| Artifact | Expected | Status | Details |
| --- | --- | --- | --- |
| `src/groove.h` | Redesigned fields; tap_decay[4] & FB_LP_A removed | ✓ VERIFIED | spq/spq_target, tap_trim, tap_norm, diffuse_amt, loop HP, 2-pole fb LP, ap1[241]/ap2[113], rv_pre/post_amt present (groove.h:41-80). tap_decay removed; rv_mix removed. |
| `src/groove.c` | Redesigned TAPS branch + mappings; GEN byte-identical | ✓ VERIFIED | 656 lines. Bidirectional LENGTH (95-99), equal-power (83-88), fractional slewed reads, feedback-path HP, 2-pole loop LP, diffusion, pre/post reverb. RAW kick to ring. GEN branch diff-clean vs a3a4a20. |
| `src/params.c` | GKI_GRV_RVMIX default 0.5 | ✓ VERIFIED | `[GKI_GRV_RVMIX]=0.5f` (params.c:68); key mapping preserved (params.c:121). |
| `tests/test_readback.c` | Asserts grv_rvmix==0.5 on bare create | ✓ VERIFIED | test_readback.c:71 asserts approx 0.5 with center-off comment. |
| `tests/test_taps_redesign.c` | 10 gates + zero-alloc; helpers copied | ✓ VERIFIED | 707 lines; gates 1-10 + zero-alloc all defined and PASS; neutralise_chain helper; expected_scale=0.5 documented. |
| `Makefile` | test-taps-redesign wired into test aggregate | ✓ VERIFIED | Target builds/runs; included in `make test` (ran GREEN). |

### Key Link Verification

| From | To | Via | Status | Details |
| --- | --- | --- | --- | --- |
| groove_tick TAPS branch | ring buf_l/buf_r fractional read | k*spq behind write_pos, & GRV_DELAY_MASK | ✓ WIRED | groove.c:436-447, linear interp, mask wrap. |
| groove_set_length | fb_amount + tap_trim (opposing) | fb=0.85*(1-v), tap_trim=0.6+0.4*v, diffuse=1-v | ✓ WIRED | groove.c:96-98, exact. |
| groove_set_param PK_GRV_RVMIX | rv_pre_amt/rv_post_amt with center deadzone | bidirectional split around 0.5 (±0.03) | ✓ WIRED | groove.c:582-591. |
| tests/test_taps_redesign.c | real plugin via move_plugin_init_v2 chain | mock_host + RAW isolated-kick reference | ✓ WIRED | gate1 dual-render RAW vs GROOVE (test:165-225). |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| --- | --- | --- | --- |
| Full TAPS acceptance suite | make test-taps-redesign | all 10 gates + zero-alloc PASS | ✓ PASS |
| Full aggregate | make test | 11 suites + aggregate ALL TESTS PASSED | ✓ PASS |
| Settled-spq clean-copy exactness | direct groove_tick trace | offset=0, rel_err=0, ratio=1.0, xcorr=1.0 | ✓ PASS |
| GEN branch unchanged | git diff a3a4a20 HEAD -- src/groove.c | 0 gen_* line changes | ✓ PASS |
| Removed constructs gone | grep FB_LP_A / x3.0 / tap_decay | 0 hits | ✓ PASS |
| RT-safety (per-sample ops) | grep TAPS branch 343-451 | only floorf + compile-time const div | ✓ PASS |

### Requirements Coverage

| Requirement | Status | Evidence |
| --- | --- | --- |
| TAPS-LENGTH-BIDIR | ✓ SATISFIED | truth 1/3; groove_set_length opposing morph |
| TAPS-EQUAL-TAPS | ✓ SATISFIED | truth 2; gate2 0.169 dB |
| TAPS-CLEAN-COPIES | ✓ SATISFIED | truth 1; settled-spq trace exact |
| TAPS-DRONE-STABLE | ✓ SATISFIED | truth 3; gate3 30 s bounded |
| TAPS-EQUAL-POWER | ✓ SATISFIED | tap_norm 1/sqrt(sum); gate4 0.10 dB |
| TAPS-COLOR-DARKNESS | ✓ SATISFIED | truth 4; gate6/7 |
| TAPS-LOOP-HP | ✓ SATISFIED | 30 Hz feedback-path HP (groove.c:367-379) |
| TAPS-FRACTIONAL-SLEW | ✓ SATISFIED | truth 8; gate9 |
| TAPS-REVERB-BIDIR | ✓ SATISFIED | truth 7; gate10 |
| TAPS-NO-BITCRUSH | ✓ SATISFIED | truth 6; gate5 0.0000% at rail |
| TAPS-RT-SAFE | ✓ SATISFIED | truth 9; zero-alloc, only floorf per sample |
| TAPS-GEN-UNCHANGED | ✓ SATISFIED | truth 10; diff-clean |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| --- | --- | --- | --- | --- |
| src/groove.c | 216-218 | per-sample `/` in DRIVE block | ℹ️ Info | Pre-existing (present in a3a4a20), gated behind `if (g->drive > 0.0f)`; not introduced by this task, not in the TAPS core path. |

No blocker or warning anti-patterns. No stubs, no placeholders, no hardcoded empty data. The removed `rv_mix` field was dead after the routing change and correctly deleted.

### Human Verification Required

**On-device TAPS listening checklist (Task 4 — blocking gate, by design).** On-device audio and Docker cross-build are unavailable in this environment; the aarch64 build + glibc gate and perceptual audition remain the authoritative on-hardware checks. When Move hardware is available, run the 9-item checklist in the SUMMARY (LENGTH extremes, COLOR sweep, level match, reverb pre/off/post, live BPM click-free, GEN unchanged). This is the only outstanding item.

### Gaps Summary

No engineering gaps. All 10 automated must-have truths are verified, all artifacts substantive and wired, all key links connected, GEN byte-identical, RT-safety intact, and the full 11-suite test aggregate is GREEN. The gate #1 threshold concern was investigated directly and shown to be a test warm-up (slew-convergence) artifact — the engine itself produces mathematically exact equal-level clean copies when spq is settled. The only remaining item is the deliberate blocking on-device human listening gate (Task 4), which needs Move hardware and cannot be closed programmatically.

Optional (non-blocking) test-quality note for a future pass: gate #1 could be made a tighter, self-contained assertion by lengthening the warm-up (or forcing spq to converge before the trigger) so it measures offset≈0 / rel_err≈0 like the direct trace — this would remove the need for the relaxed tolerances without changing any src/ behaviour.

---

_Verified: 2026-09-30_
_Verifier: Claude (gsd-verifier)_
