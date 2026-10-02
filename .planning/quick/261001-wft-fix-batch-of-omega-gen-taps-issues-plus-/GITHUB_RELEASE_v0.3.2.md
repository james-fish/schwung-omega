## Omega v0.3.2 — hotfix: model & groove pickers switch pages correctly

Fixes a regression in v0.3.0/v0.3.1 where changing a picker didn't update the
on-screen parameters:
- switching the kick **Model** left the previous model's params showing (v0.3.1
  made it worse — the Kick page could disappear);
- switching the groove **TYPE** (Taps/Gen) didn't swap the groove pages (v0.3.1
  made them vanish and jumped focus to the Model picker).

**Why:** the new Schwung host builds its page set once and, on a knob-driven
parameter change, re-filters the **cached** `ui_hierarchy` by `visible_if` — it
does not re-read the hierarchy. Omega had been re-emitting a different hierarchy
per selection (which only the initial load picked up), and v0.3.1's `visible_if`
used the *current* value, so the matching level hid the instant the value changed.

**Fix:** Omega now publishes a single **static** hierarchy and lets the host do
the switching via fixed `visible_if` conditions — the host's own mechanism:
- the Kick page declares every model's params once, each shown only for its model;
- the Taps and Gen groove pages are both declared, each shown only for its groove
  type, with a shared effects page.

Both pickers now update instantly. P-locking (v0.3.0) and all the GEN/TAPS sound
changes are unaffected.

Native test suite green; aarch64 `dsp.so` glibc-2.35 gated via CI.
