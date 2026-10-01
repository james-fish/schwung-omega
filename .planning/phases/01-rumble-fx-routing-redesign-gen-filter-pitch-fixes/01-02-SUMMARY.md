# Plan 01-02 Summary — FX-ROUTE

**Status:** Complete. Commit 673a523.

Added PK_GRV_ROUTE/GKI_GRV_ROUTE (OMEGA_GKI_COUNT 37→38), route_order[3] table,
per-sample switch dispatch (no transcendental). Reverb is now a plain in-line
dry/wet block (rv_mix 0..1), never into the ring; comb fb clamped <1. DRIVE+LFO
folded into groove_drive_block. OPT_FXROUTE enum + ROUTE on the shared FX page;
dropped no-op RV TYPE to stay ≤8 encoders. Test: test_route_changes_output
(5940 samples differ between order 0 and order 2) + reverb bounded under
test_no_runaway. GREEN.
