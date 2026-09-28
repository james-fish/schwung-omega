---
phase: A-foundation-fm2-model
plan: 04
type: execute
wave: 4
depends_on: ["02", "03"]
files_modified:
  - docs/ON_DEVICE_VALIDATION.md
  - docs/SOC_IDENTIFICATION.md
autonomous: false
requirements: [FNDTN-01, FNDTN-04]
must_haves:
  truths:
    - "The same unmodified dsp.so + module.json loads and produces audio in Schwung slot, DR32 pad, and Movy track (SC1)"
    - "The ui_hierarchy buf_len passed by each host is captured from the device log (SC5, D-10)"
    - "The Move SoC / Cortex core is identified from /proc/cpuinfo and documented (D-15)"
  artifacts:
    - path: "docs/ON_DEVICE_VALIDATION.md"
      provides: "recorded 3-host load/trigger results + captured per-host buf_len values"
      contains: "buf_len"
    - path: "docs/SOC_IDENTIFICATION.md"
      provides: "recorded /proc/cpuinfo core + -mcpu recommendation"
      contains: "cpuinfo"
  key_links:
    - from: "build/dsp.so (cross-compiled)"
      to: "Move device via scripts/deploy.sh"
      via: "scp .new + atomic rename (D-14)"
      pattern: "deploy.sh"
---

<objective>
Validate the foundation on real hardware: cross-build `dsp.so` in the pinned Docker image, deploy via the atomic scp+rename script (D-14), and confirm the SAME unmodified `dsp.so` + `module.json` loads and produces an audible FM2 kick in all three host contexts — Schwung slot, DR32 pad, and Movy track (SC1). While on-device, capture the two empirical unknowns: the `ui_hierarchy` buf_len each host passes (SC5, from the D-10 spike log) and the exact SoC/Cortex core from `/proc/cpuinfo` (D-15). Results are recorded to docs that unblock Phase E (buf_len) and inform the future `-mcpu` decision.

This is a checkpoint plan: Claude automates the build + deploy + log-fetch; the human performs the physical trigger-and-listen verification in each host UI.

Purpose: Prove the end-to-end pipeline works on Move (the phase's headline success criterion) and retire the two on-device unknowns flagged since project init.
Output: A green cross-build, a deployed module confirmed in 3 hosts, and two recorded docs (buf_len per host, SoC identification).
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/phases/A-foundation-fm2-model/A-CONTEXT.md
@.planning/phases/A-foundation-fm2-model/A-RESEARCH.md
@CLAUDE.md
@.planning/phases/A-foundation-fm2-model/A-01-SUMMARY.md
@.planning/phases/A-foundation-fm2-model/A-02-SUMMARY.md
@.planning/phases/A-foundation-fm2-model/A-03-SUMMARY.md

<interfaces>
Build: `docker run --rm -v "$PWD:/workspace" -w /workspace ghcr.io/charlesvestal/schwung-builder:latest make dsp.so` (A-RESEARCH 405-409).
Gate: `./scripts/glibc_gate.sh build/dsp.so` (A-01).
Deploy: `./scripts/deploy.sh` — scp build/dsp.so as dsp.so.new then atomic mv (A-01, D-14).
D-10 spike log location (A-VALIDATION Manual-Only): `/data/UserData/schwung/debug.log` — first get_param("ui_hierarchy") per host writes `ui_buflen=<n>`.
D-15: read `/proc/cpuinfo` on-device; if Cortex-A53 confirmed, document for potential `-mcpu=cortex-a53` (do NOT bake in — A-RESEARCH Open Q2, CLAUDE.md MEDIUM confidence).
on_midi trigger: pad hit / note-on velocity>0 (A-RESEARCH Pitfall 5).
</interfaces>
</context>

<tasks>

<task type="auto">
  <name>Task 1: Cross-build, gate, and deploy dsp.so to the Move device</name>
  <files>build/dsp.so (produced), scripts/deploy.sh (invoked)</files>
  <read_first>
    - Makefile, scripts/glibc_gate.sh, scripts/deploy.sh (A-01)
    - module.json (A-02)
    - .planning/phases/A-foundation-fm2-model/A-CONTEXT.md D-14
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md §Cross-Compilation (405-432), Environment Availability (391-401)
  </read_first>
  <action>
    1. Confirm Docker + image availability: `docker image inspect ghcr.io/charlesvestal/schwung-builder:latest >/dev/null 2>&1 || docker pull ghcr.io/charlesvestal/schwung-builder:latest`.
    2. Cross-build: `docker run --rm -v "$PWD:/workspace" -w /workspace ghcr.io/charlesvestal/schwung-builder:latest make dsp.so`.
    3. Run the glibc gate: `./scripts/glibc_gate.sh build/dsp.so` — must report GLIBC<=2.35, no libmvec, exactly one export.
    4. Deploy via the atomic script: `./scripts/deploy.sh` (uploads build/dsp.so + module.json to the device, atomic rename). If the device is unreachable, record the failure clearly and pause — the deploy is required for SC1.
    Do NOT proceed to Task 3's human checkpoint until dsp.so is on-device.
  </action>
  <verify>
    <automated>docker run --rm -v "$PWD:/workspace" -w /workspace ghcr.io/charlesvestal/schwung-builder:latest make dsp.so && ./scripts/glibc_gate.sh build/dsp.so</automated>
  </verify>
  <acceptance_criteria>
    - `build/dsp.so` exists after the Docker build
    - `scripts/glibc_gate.sh build/dsp.so` exits 0 (GLIBC<=2.35, no `_ZGV`/libmvec, exactly 1 `move_plugin_init_v2` export)
    - `scripts/deploy.sh` completes (dsp.so.new uploaded then renamed to dsp.so on device) OR the unreachable-device state is explicitly recorded
  </acceptance_criteria>
  <done>A gate-passing aarch64 dsp.so is deployed to the Move device via atomic rename, ready for 3-host verification.</done>
</task>

<task type="auto">
  <name>Task 2: Identify the Move SoC/Cortex core (D-15) and prep buf_len capture</name>
  <files>docs/SOC_IDENTIFICATION.md, docs/ON_DEVICE_VALIDATION.md</files>
  <read_first>
    - .planning/phases/A-foundation-fm2-model/A-CONTEXT.md D-15, D-10
    - .planning/phases/A-foundation-fm2-model/A-RESEARCH.md Open Questions 1 & 2 (546-556)
    - scripts/deploy.sh (for the device ssh host/path env vars)
  </read_first>
  <action>
    1. SSH to the device and read the core (use the same device host var as deploy.sh): `ssh <device> cat /proc/cpuinfo | grep -E "CPU part|CPU implementer|Features|model name"`. Cortex-A53 reports `CPU part : 0xd03`.
    2. Create `docs/SOC_IDENTIFICATION.md` recording the raw cpuinfo excerpt, the decoded core name, and a recommendation: if Cortex-A53 (`0xd03`) confirmed, note "`-mcpu=cortex-a53` is safe to add in a future build task" but state it remains NOT baked in for Phase A (D-15, A-RESEARCH Open Q2 — avoid SIGILL on unconfirmed cores). If a different core, document it and keep baseline ARMv8-A.
    3. Clear/rotate the device log so the buf_len capture in Task 3 is clean: `ssh <device> ': > /data/UserData/schwung/debug.log' 2>/dev/null || true` (best-effort; note if the path differs).
    4. Create `docs/ON_DEVICE_VALIDATION.md` with an empty results table (3 hosts x {loads, audible, buf_len}) ready for Task 3 to fill.
  </action>
  <verify>
    <automated>test -f docs/SOC_IDENTIFICATION.md && grep -qi cpuinfo docs/SOC_IDENTIFICATION.md && test -f docs/ON_DEVICE_VALIDATION.md</automated>
  </verify>
  <acceptance_criteria>
    - `docs/SOC_IDENTIFICATION.md` exists, contains the raw `/proc/cpuinfo` CPU part line and a decoded core name
    - `docs/SOC_IDENTIFICATION.md` states whether `-mcpu=cortex-a53` is recommended and explicitly confirms it is NOT baked into Phase A
    - `docs/ON_DEVICE_VALIDATION.md` exists with a 3-host results table (columns: loads / audible / buf_len)
  </acceptance_criteria>
  <done>The SoC is identified and documented with an -mcpu recommendation (deferred); the device log is cleared and the validation results doc is prepped for capture.</done>
</task>

<task type="checkpoint:human-verify" gate="blocking">
  <name>Task 3: Human verifies FM2 kick in all 3 hosts + captures per-host buf_len (SC1, SC5)</name>
  <files>docs/ON_DEVICE_VALIDATION.md</files>
  <read_first>
    - docs/ON_DEVICE_VALIDATION.md (the results table to fill)
    - .planning/phases/A-foundation-fm2-model/A-VALIDATION.md Manual-Only Verifications table
  </read_first>
  <action>
    Claude automates log retrieval before and after the human check: run `ssh <device> cat /data/UserData/schwung/debug.log | grep ui_buflen` to capture the per-host buf_len lines, and transcribe the human's load/audible results plus the buf_len values into `docs/ON_DEVICE_VALIDATION.md`. The physical load-and-listen in each host UI is the human's part (see how-to-verify).
  </action>
  <what-built>
    A gate-passing, deployed aarch64 `dsp.so` + `module.json` on the Move device implementing FM2. The D-10 spike logs the host-supplied `ui_hierarchy` buf_len to `/data/UserData/schwung/debug.log` on the first get_param per host.
  </what-built>
  <how-to-verify>
    For EACH of the three host contexts, in this order — Schwung instrument slot, DR32 pad slot, Movy track:
    1. Load Omega into the slot/pad/track. Confirm it loads without error (module appears, no crash).
    2. Trigger a hit (pad hit or note-on). Confirm you HEAR an FM2 kick.
    3. Sweep a couple of Kick Page 1 encoders (e.g. PITCH, LENGTH) and confirm the sound changes audibly.
    4. Record loads=yes/no and audible=yes/no in `docs/ON_DEVICE_VALIDATION.md` for that host.
    After all three: fetch the log and read the captured buf_len values:
    `ssh <device> cat /data/UserData/schwung/debug.log | grep ui_buflen`
    Record each host's `ui_buflen=<n>` value into the table (SC5, unblocks Phase E). If a host shows no line, note it (the host may cache/not re-query).
    Confirm the SAME unmodified dsp.so was used in all three (no rebuild between hosts) — this is SC1's core assertion.
  </how-to-verify>
  <verify>
    <automated>grep -q "ui_buflen" docs/ON_DEVICE_VALIDATION.md && echo "buf_len recorded"</automated>
  </verify>
  <acceptance_criteria>
    - `docs/ON_DEVICE_VALIDATION.md` records loads=yes and audible=yes for all three hosts (Schwung slot, DR32 pad, Movy track) — SC1
    - `docs/ON_DEVICE_VALIDATION.md` records a `ui_buflen` value (or an explicit "not logged" note) for each host — SC5
    - A note confirms the identical dsp.so was used across all three hosts (no per-host rebuild)
  </acceptance_criteria>
  <done>All three hosts load and sound the identical dsp.so (SC1); per-host buf_len captured (SC5); results recorded in docs/ON_DEVICE_VALIDATION.md.</done>
  <resume-signal>Type "approved" once all three hosts are verified and buf_len values are recorded, or describe any host that failed to load/sound.</resume-signal>
</task>

</tasks>

<verification>
- Cross-build passes the glibc gate (FNDTN-04) and deploys via atomic rename (D-14).
- The identical unmodified dsp.so + module.json loads and sounds an FM2 kick in Schwung slot, DR32 pad, and Movy track (SC1 / FNDTN-01).
- Per-host ui_hierarchy buf_len captured from the device log (SC5 / D-10) — unblocks Phase E.
- Move SoC / Cortex core identified and documented; -mcpu recommendation recorded but not baked in (D-15).
</verification>

<success_criteria>
- SC1 (3-host load + audio) confirmed on real hardware with one unmodified binary.
- SC5 (buf_len logged per host) captured and recorded.
- D-15 SoC identification complete; Phase A foundation validated end-to-end on-device.
</success_criteria>

<output>
After completion, create `.planning/phases/A-foundation-fm2-model/A-04-SUMMARY.md`
</output>
