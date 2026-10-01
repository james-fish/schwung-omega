## Omega v0.3.1 — hotfix: dynamic UI pickers update again

Fixes a regression from v0.3.0: after adding `chain_params` (which enabled
p-locking), the kick **Model** picker and the groove **TYPE** (Taps/Gen) picker
stopped updating the on-screen parameters — switching model left the old model's
params visible, and switching to Gen still showed the Taps controls.

**Why:** the new Schwung host builds its page set once and only re-reads a
module's dynamic `ui_hierarchy` when a parameter named by a `visible_if`
condition changes. Omega swaps which levels it emits on `model`/`grv_type` but
declared no `visible_if`, so the host never re-read after a picker change.

**Fix:** each dynamically-emitted level now carries a `visible_if` gate on its
discriminator (`model` for the kick page, `grv_type` for the groove pages), so a
picker change triggers a re-plan and the correct params are shown. P-locking from
v0.3.0 is unaffected.

Native test suite green; aarch64 `dsp.so` glibc-2.35 gated via CI.
