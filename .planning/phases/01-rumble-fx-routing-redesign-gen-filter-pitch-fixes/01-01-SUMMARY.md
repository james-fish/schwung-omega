# Plan 01-01 Summary — RUMBLE-CORE (feedback-free FIR)

**Status:** Complete. Commit 673a523.

Removed the resonant feedback core (fb_amount, ap1/ap2 diffusion, in-loop
2-pole LP, ~30Hz feedback HP, rv_pre/post). New FIR tap engine: ring holds only
the raw kick; rumble = tap_norm·Σ tap_w[k]·ring[k·spq], weights precomputed at
control rate (groove_update_tap_weights), LENGTH→tau 40·30^(1-v) ms. Bounded by
construction. Instance shrinks ~1.4KB. Tests: test_no_runaway (4 routes),
test_rumble_audible (tail on 0.32 vs off 0.16), test_length_morph (smear 0.21 >
distinct 0.063). GREEN.
