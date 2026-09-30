# Schwung Ecosystem Research — for Omega TAPS / Rumble Redesign

**Researched:** 2026-09-30
**Domain:** Schwung (Ableton Move custom-module framework, github.com/charlesvestal/schwung) audio-FX module survey — delay, reverb, filter, sidechain — to inform Omega's TAPS/rumble redesign.
**Schwung release surveyed:** **v1.6.1** (`charlesvestal/schwung/release.json`, updated 2026-09-30)
**Catalogued modules:** **147** total (from `charlesvestal/schwung/module-catalog.json`)
**Overall confidence:** HIGH for API/tempo facts (read directly from Schwung's own headers), HIGH for the four deep-read modules (source read line-by-line), MEDIUM for the broader catalog scan (metadata only).

Research-only. No files under `src/` or `tests/` were modified.

---

## Summary

Schwung has a large, active FX ecosystem (147 modules; dozens are `audio_fx`). Several are directly relevant to Omega's TAPS/rumble work: a tempo-synced tape delay (`schwung-space-delay`, MIT), a MIT feedback delay with cubic interpolation (`schwung-ruminant`), a MIDI-triggered sidechain ducker (`schwung-ducker`, MIT), a multimode SVF filter (`schwung-filter`), and a tempo-synced curve-shaper sidechain (`schwung-pushnpull`). Together they establish the community-standard patterns for exactly the four subsystems Omega's groove touches: **fractional/interpolated delay reads, click-free parameter smoothing, MIDI-clock tempo derivation, a canonical TPT-SVF, and a phased sidechain envelope.**

The single most important finding for the user's explicit question ("check for updated tempo API"): the module-facing **host ABI has NOT changed** — Omega's `host_api_v1_t` still matches the official `plugin_api_v1.h` byte-for-byte, including `reserved[8]` at +120, and `get_beat_position()`/`get_bpm()` remain the only two ABI transport callbacks. **BUT** Schwung now ships a separate, richer, header-only transport API — **`src/host/move_info.h`** — that exposes Move's authoritative song `tempo`, `playing`, `song_beats`, `ts_upper/lower`, and `midi_clock_sync` via a lock-free seqlock snapshot that is explicitly documented as **safe to read on the audio thread**. This is a materially better BPM source than Omega's per-block beat-delta heuristic and is the recommended tempo upgrade.

**Primary recommendation:** Keep Omega's transport-locked architecture, but (1) adopt `move_info.h` for authoritative BPM + transport-running detection, (2) add fractional-delay interpolation to the tap reads (linear per space-delay, or Catmull-Rom cubic per Ruminant) so tap spacing is smooth and BPM-change-proof, and (3) add a per-sample parameter smoother (space-delay `SmoothedValue` / filter `smoother_t`) for LENGTH/feedback/tap-level/COLOR to kill zipper noise. All three are proven-on-Move patterns.

---

## Q1 — Latest Schwung release & tempo/transport API

### Release
- **Version 1.6.1.** Source: `charlesvestal/schwung/release.json` → `{"version":"1.6.1", "download_url":".../v1.6.1/schwung.tar.gz"}`. (HIGH)
- The catalog carries a `min_host_version` per module (e.g. Dexed `0.3.0`); Omega should target `>= 1.x`. (HIGH)

### The module-facing ABI has NOT changed — Omega is current
Read verbatim from `charlesvestal/schwung/src/host/plugin_api_v1.h` (HIGH):

- `host_api_v1_t` fields, order, and the `reserved[8]` tail at **offset +120** are **identical to Omega's `src/omega.h`** declaration. The `_Static_assert(offsetof(...reserved) == 120)` is in both. Omega's ABI copy is correct and current.
- The **only two transport callbacks in the ABI** remain:
  - `float (*get_bpm)(void)` at +88 — "Uses sampler_get_bpm() fallback chain: MIDI clock → set tempo → settings → 120."
  - `double (*get_beat_position)(void)` (appended 2026-07) — "Beats since transport start... derived from 24-PPQN realtime ticks and interpolated per block. Returns < 0 when no transport is running." **Exactly what Omega's `groove_update_tempo` already consumes.** (HIGH)
- `int (*get_clock_status)(void)` returns `MOVE_CLOCK_STATUS_UNAVAILABLE/STOPPED/RUNNING` (0/1/2). **Omega declares this field but does not use it** — it is a cleaner "is transport running" signal than Omega's beat-delta inference (GRVX-05). (HIGH)
- **Hard rule:** new host capabilities arrive as **dlsym'd exports, never new struct fields** (the header devotes ~40 lines to the `breakbeat` boot-loop caused by a module appending `get_project_bpm` past +120). Do NOT extend `host_api_v1_t`. (HIGH)

### NEW richer transport API — `move_info.h` (the "updated tempo API" the user asked about)
Source: `charlesvestal/schwung/src/host/move_info.h` + `docs/MODULES.md` §"Reading Move's set". (HIGH)

Schwung publishes Move's live song document to a shared-memory segment `/schwung-move-info`. A module copies **one header file** (`move_info.h`, self-contained) and calls `move_info_read(&mi)`. Relevant fields on `move_info_t`:

| Field | Type | Meaning | Unknown sentinel |
|-------|------|---------|------------------|
| `tempo` | `float` | **Move's authoritative song BPM** | `<= 0` |
| `playing` | `uint8_t` | transport running | `255` |
| `song_beats` | `double` | transport position, beats since Play | `< 0` |
| `ts_upper`, `ts_lower` | `uint8_t` | time signature | `0` |
| `midi_clock_sync` | `uint8_t` | Move follows external MIDI clock | `255` |
| `metronome_on`, `groove`, `root_note`, `scale[24]`, `global_quant_name[24]` | mixed | set-wide musical context | explicit |
| `valid` | `uint8_t` | 0 = document not being read (treat all as unknown) | — |

Threading (verbatim): **"inside MoveOriginal (chain synths and FX, Master FX, overtake DSP) it calls the shim's exported `schwung_move_info()`... a copy out of an already-mapped page, NO SYSCALLS, safe on the audio thread. Make the FIRST call in `create_instance`"** (the first call does a `dlsym`, which takes the loader lock once). Compatibility is by struct `size`; forward/backward compatible. Compile with `-D_GNU_SOURCE`, link `-ldl` on older glibc; `move_info_read()` returns 0 on a host that doesn't publish it, so it degrades gracefully. (HIGH)

**Why this matters for TAPS:** Omega currently derives BPM by differencing `get_beat_position()` across blocks and EMA-smoothing the result — robust but noisy and only as good as the per-block interpolation. `mi.tempo` is Move's own project tempo (exact, no differentiation), `mi.playing` gives a clean run/stop for GRVX-05, and `mi.song_beats` gives an absolute phase if you ever want the taps phase-locked to the bar grid. Recommended: use `mi.tempo` as the primary BPM, keep the existing `get_beat_position`/`get_bpm`/120 chain as the documented fallback for hosts that don't publish move-info.

### Other v1.6.1 features relevant to Omega
- **`requires_continuous_processing`** manifest capability (`docs/MODULES.md`): "Audio FX that owns non-trivial internal time (rolling loopers, granular, modulated delays/reverb). Keeps the slot's DSP rendering through silence instead of idle-parking it... so the plugin's internal clock doesn't freeze." Omega's groove ring + feedback loop is exactly this class — **if Move idle-parks Omega during kick silence, the rumble tail and feedback state would freeze**. Omega is a `sound_generator` (not `audio_fx`), so this specific flag may not apply, but the underlying idle-park behaviour is worth verifying on-device for the rumble tail. (MEDIUM — flag is documented for audio_fx; applicability to sound_generator groove tail unverified.)
- **`move_plugin_render_split`** (dlsym'd, optional): a sound_generator can render named voices into separate buffers so the Signal Chain routes kick vs. rumble to different insert chains/sends. Directly maps to Omega's kick+rumble split and the "hard-pan kick L / groove R" workflow from the Bohm community (SOURCE_INDEX #30). Future enhancement, not required for the redesign. (HIGH that the mechanism exists; MEDIUM on desirability.)
- **Threading contract is stricter than Omega's CLAUDE.md assumes:** the current header states **"THERE IS NO CONTROL THREAD"** — `create_instance`, `set_param`, `get_param` all run on the SPI audio callback (SCHED_FIFO 70, core 3, ~2370 µs slack/128-frame block). CLAUDE.md says "set/get and render run on the same thread so you likely won't [need atomics]" — consistent — but CLAUDE.md's Testing section implies a "control rate" that is off-thread. It is not off-thread; it is merely *less frequent*. Omega's existing discipline (all `powf`/`tanf` in `set_param`, none in `groove_tick`) is still correct because `set_param` is bounded work, but note that `host->log` and any file I/O are forbidden even in `set_param`/`create_instance` when loaded as a chain SLOT. (HIGH)

---

## Q2 — Audio-FX module enumeration (from module-catalog.json, 147 modules)

Delay / reverb / filter / sidechain / drive modules (all `component_type: audio_fx` unless noted). Prioritized: DELAY, REVERB, TEMPO-SYNC, SIDECHAIN, FILTER.

| Module | id | Repo | Lang | License | What it does | Tempo? | Priority |
|--------|-----|------|------|---------|--------------|--------|----------|
| **TapeDelay** | `tapedelay` | `charlesvestal/schwung-space-delay` | C | **MIT** | Tape delay, linear-interp fractional read, smoothed params, ping-pong + width, MIDI-clock BPM | **Yes (0xF8 count)** | ★★★ |
| **Ruminant** | `ruminant` | `mestela/schwung-ruminant` | C | **MIT** | Freq-shifting feedback delay, **cubic** fractional read, LP-in-loop feedback, soft-clip | delay_ms param | ★★★ |
| **Ducker** | `ducker` | `charlesvestal/schwung-ducker` | C | **MIT** | MIDI-triggered sidechain ducker, ADHR envelope, 4 curve shapes, trigger/gate | note-triggered | ★★★ |
| **Filter** | `filter` | `charlesvestal/schwung-filter` | C | none (⚠ technique-only) | Multimode SVF (LP/HP/BP/notch/peak/AP) + Moog ladder, env follower, tempo LFO | LFO sync | ★★★ |
| **PushNPull** | `pushnpull` | `legsmechanical/schwung-pushnpull` | C | none (⚠) | Tempo-synced curve shaper: ducks/boosts vol + sweeps filter, MIDI-clock phasor | **Yes (0xF8 EMA)** | ★★ |
| **Tape Echo 2** | `tape-echo2` | `athousanddetails/...` | C? | GPL-3.0 (⚠ copyleft) | 3-head tape echo + spring reverb, component-modeled | likely | ★ |
| **MVerb** | `mverb` | `charlesvestal/schwung-mverb` | C++ | (check) | Algorithmic plate reverb (Martin Eastwood MVerb) | no | ★★ |
| **Midiverb** | `midiverb` | `charlesvestal/schwung-midiverb` | C | (check) | Alesis Midiverb lo-fi 23.4 kHz reverb emulation | no | ★ |
| **CloudSeed** | `cloudseed` | (Ghost Note Audio port) | C++? | (check) | Algorithmic reverb | no | ★ |
| **Dragonfly Hall** | `dragonfly-hall` | `bradcoomber/...` | C++? | GPL (likely) | Lush hall reverb, 25 presets | no | ★ |
| **PSX Verb** | `psxverb` | (charlesvestal) | C? | (check) | PS1 SPU reverb emulation | no | ★ |
| **RRVerb-10** | `rrverb10` | `legsmechanical/...` | C | (check) | 80s micro-rack digital reverb (MUNT gate-array) | no | ★ |

Other FX of note (not deep-read): OTTx (multiband comp), TAPESCAM/CHOWTape/Super Boom/Magneto (tape/saturation), Junologue Chorus, Gate (noise gate/expander), Usefulity (stereo utility — bass-mono, width), Airwindows (500+ effects via airwin2rack), Structor/Dissolver/Spectra/Ambiotica/Verglas/Boris Granular (granular/spectral), PALETTE/War Bells/Work/Smack (multi-FX). MIDI-FX with tempo: Eucalypso, Euclidrum, Branchage, Genera (Euclidean/generative sequencers — relevant to Omega's GEN groove).

**Sources:** `module-catalog.json` (metadata, MEDIUM); individual repo API calls for the ★★★ rows (HIGH). Repos with "(check)" were not license-verified in this pass.

---

## Q3 — Deep-read DSP patterns (source read line-by-line)

### 3a. `schwung-space-delay` / `spacecho.c` (MIT) — tempo-synced fractional delay
Repo: https://github.com/charlesvestal/schwung-space-delay · File: `src/dsp/spacecho.c` · Based on cyrusasfa/TapeDelay.

**Fractional delay read (linear interp)** — the pattern Omega's integer taps lack:
```c
/* Source: schwung-space-delay src/dsp/spacecho.c (MIT) */
static float DelayLine_Read(DelayLine *dl, float delayTimeSeconds) {
    float delaySamples = delayTimeSeconds * dl->sampleRate;
    if (delaySamples < 1.0f) delaySamples = 1.0f;
    if (delaySamples > dl->bufferLength - 1) delaySamples = dl->bufferLength - 1;
    float readPos = (float)dl->writePosition - delaySamples;
    if (readPos < 0) readPos += dl->bufferLength;
    float fraction = readPos - floorf(readPos);
    int index0 = (int)floorf(readPos);
    int index1 = (index0 + 1) % dl->bufferLength;
    return dl->buffer[index0] + fraction * (dl->buffer[index1] - dl->buffer[index0]);
}
```

**Click-free param smoothing** — ramp target over N samples (RAMP_SAMPLES = 2205 ≈ 50 ms):
```c
/* Source: schwung-space-delay src/dsp/spacecho.c (MIT) */
static void  SmoothedValue_SetTarget(SmoothedValue *sv, float t, int ramp) {
    sv->targetValue = t;
    if (ramp > 0) { sv->step = (t - sv->currentValue) / (float)ramp; sv->stepsRemaining = ramp; }
    else { sv->currentValue = t; sv->step = 0; sv->stepsRemaining = 0; }
}
static float SmoothedValue_GetNext(SmoothedValue *sv) {
    if (sv->stepsRemaining > 0) {
        sv->currentValue += sv->step;
        if (--sv->stepsRemaining == 0) sv->currentValue = sv->targetValue;
    }
    return sv->currentValue;
}
```

**MIDI-clock BPM detection** (from `spacecho_on_midi`): counts 0xF8 ticks; every 24 ticks (`CLOCKS_PER_QUARTER`) it takes `total_samples` accumulated over that quarter, computes BPM, and only re-locks when `!clock_running || abs(bpm - param_bpm) >= 3` (a 3-BPM hysteresis, analogous to Omega's 0.5-BPM EMA relock). 0xFA/0xFB set running, 0xFC clears it.

**Musical division table** → delay-time in ms:
```c
/* Source: schwung-space-delay (MIT). 1/1 .. 1/16t incl. dotted & triplets */
static const float division_multipliers[] = {0,4,2,3,1,1.5f,0.66667f,0.5f,0.75f,0.33333f,0.25f,0.16667f};
static int compute_synced_time(int bpm, int div) {          /* -> ms, clamped 20..2000 */
    float beat_ms = 60000.0f / (float)bpm;
    int ms = (int)(division_multipliers[div] * beat_ms + 0.5f);
    return ms < 20 ? 20 : (ms > 2000 ? 2000 : ms);
}
```

**Ping-pong feedback + stereo width + level compensation** (directly relevant to a stereo rumble):
```c
/* Source: schwung-space-delay src/dsp/spacecho.c (MIT) */
DelayLine_Write(&dl[0], pingInputL + delayedR * feedback);   /* cross-feed = ping-pong */
DelayLine_Write(&dl[1], pingInputR + delayedL * feedback);
float widthLevelComp = 1.0f / sqrtf(1.0f - 0.5f * stereoWidth);   /* keep energy constant */
if (widthLevelComp > 1.333333f) widthLevelComp = 1.333333f;      /* clamp */
```
`GetFeedback(x) = x * 0.95f` (feedback capped below 1.0). `GetToneFrequency(x) = 500 * powf(24, x)` (exp tone sweep 500 Hz–12 kHz), one-pole tone filter in the loop.

**Reusable in Omega (MIT — copy permitted with attribution):** `DelayLine_Read` linear interp, `SmoothedValue`, `division_multipliers` + `compute_synced_time`, ping-pong write, width level comp.

### 3b. `schwung-ruminant` / `ruminant.c` (MIT) — feedback delay, cubic read
Repo: https://github.com/mestela/schwung-ruminant · File: `src/dsp/ruminant.c`.

**Catmull-Rom cubic fractional read** (higher-quality than linear; ~4 taps + 11 mults):
```c
/* Source: schwung-ruminant src/dsp/ruminant.c (MIT) */
static float cubic_delay_read(const float *buffer, int write_pos, float delay) {
    float read_pos = (float)write_pos - delay;
    while (read_pos < 0.0f) read_pos += (float)RUM_DELAY_SIZE;
    int i1 = (int)read_pos; float frac = read_pos - (float)i1;
    float x0=buffer[wrap_index(i1-1)], x1=buffer[wrap_index(i1)],
          x2=buffer[wrap_index(i1+1)], x3=buffer[wrap_index(i1+2)];
    float a=-0.5f*x0+1.5f*x1-1.5f*x2+0.5f*x3;
    float b=x0-2.5f*x1+2.0f*x2-0.5f*x3;
    float c=-0.5f*x0+0.5f*x2;
    return ((a*frac+b)*frac+c)*frac + x1;
}
```

**Feedback loop with one-pole LP + soft-clip — VALIDATES Omega's current TAPS design:**
```c
/* Source: schwung-ruminant src/dsp/ruminant.c (MIT) */
fx->feedback += 0.15f * (fx->feedback_target - fx->feedback);   /* per-BLOCK smoothing */
...
fx->feedback_lp[ch] += lp_coeff * (wet[ch] - fx->feedback_lp[ch]);   /* LP in loop (tone) */
float write_value = input[ch] + fx->feedback * fx->feedback_lp[ch];  /* recirculate */
fx->delay[ch][fx->delay_write] = soft_clip(write_value);            /* bound the loop */
```
This is structurally identical to Omega's `groove_tick` TAPS feedback (`wl = kick_l + fb_amount * fb_lp_l_s; wl = wl/(1+0.25*|wl|)`). Ruminant confirms the approach is the community norm; the differences worth borrowing: (1) **cubic read** for smoother taps, (2) **per-block feedback smoothing** (`+= 0.15*(target-cur)`), (3) **feedback allowed up to 110%** (`clampf(value,0,110)*0.01`) — self-oscillation is intentional and made safe by the LP + soft-clip, which is exactly the "continuous resonant drone at LENGTH-max" the user wants (feedback_taps_redesign.md).

**Reusable (MIT — copy permitted):** `cubic_delay_read`, `allpass2` (2nd-order allpass for diffusion), feedback-smoothing idiom.

### 3c. `schwung-filter` / `svf_core.c` (no license — TECHNIQUE ONLY) — canonical TPT-SVF
Repo: https://github.com/charlesvestal/schwung-filter · Files: `src/dsp/svf_core.{c,h}`, `smoother.{c,h}`.

This is the exact Zavalishin/Cytomic TPT-SVF that Omega's CLAUDE.md mandates for the DJ filter — a full multimode reference:
```c
/* Source: schwung-filter src/dsp/svf_core.c (⚠ NO LICENSE — study, reimplement, do NOT copy) */
void svf_set(svf_t *s, double fc, double res, svf_mode_t mode) {
    s->g  = tan(M_PI * fc / s->fs);
    s->k  = 2.0 - 2.0 * res; if (s->k < 0.0001) s->k = 0.0001;   /* res 0..1 -> k 2..0 */
    s->a1 = 1.0 / (1.0 + s->g * (s->g + s->k));
    s->a2 = s->g * s->a1;  s->a3 = s->g * s->a2;
}
double svf_process(svf_t *s, double v0) {
    double v3 = v0 - s->ic2eq;
    double v1 = s->a1*s->ic1eq + s->a2*v3;
    double v2 = s->ic2eq + s->a2*s->ic1eq + s->a3*v3;
    s->ic1eq = 2.0*v1 - s->ic1eq;  s->ic2eq = 2.0*v2 - s->ic2eq;
    double low=v2, band=v1, high=v0 - s->k*v1 - v2;
    /* LP=low, HP=high, BP=band, NOTCH=low+high, PEAK=low-high, AP=v0-2*k*band */
}
```
Note it uses `double` state (Omega uses `float` per its FPU decision — fine at bass frequencies; the algebra is identical). Omega already has `tpt1_lp` and DJ-filter SVF integrator state in `bohm_instance`; this reference confirms the coefficient math and the `res→k` mapping (`k = 2 - 2*res`, floored at 0.0001 for stable self-oscillation) for the DJ RESO control.

**Param smoother (one-pole, tau-based)** — cleaner than space-delay's fixed-ramp for continuous knobs:
```c
/* Source: schwung-filter src/dsp/smoother.h (⚠ no license — reimplement) */
typedef struct { double fs, coeff, cur, target; } smoother_t;
/* smooth_set_tau(s, tau_sec); smooth_target(s, v); cur += coeff*(target-cur) per sample */
```

**Reusable:** technique/algorithm only (no license). Omega's CLAUDE.md already independently specifies TPT-SVF, so this is a cross-check, not a dependency.

### 3d. `schwung-pushnpull` / `clock.c` (no license — TECHNIQUE ONLY) — MIDI-clock phasor
Repo: https://github.com/legsmechanical/schwung-pushnpull · File: `src/dsp/clock.c`.

Its header comment is a key ecosystem data point (MEDIUM–HIGH):
> *"The Schwung host has no reliable transport getter, so we lock to the MIDI clock the host broadcasts to audio-FX slots... 24 PPQN: 0xF8 tick, 0xFA Start, 0xFB Continue, 0xFC Stop."*

That is a **community judgement that (at least for audio-FX) raw MIDI-clock derivation is preferred over `get_bpm()`/`get_beat_position()`.** It builds a beat phasor by EMA-ing samples-per-tick and resyncing `beat_pos = tick_count / 24` on each new tick; free-runs from a fallback BPM when stopped; and exposes `pnp_clock_active()` (true only if a tick arrived within 0.5 s) for run/stop gating:
```c
/* Source: schwung-pushnpull src/dsp/clock.c (⚠ no license — technique) */
case 0xF8: { double d = sample_pos - last_tick_sample;
             if (have_period) period_ema += 0.15*(d - period_ema); else {period_ema=d; have_period=1;}
             last_tick_sample = sample_pos; tick_count++; resync_pending=1; } break;
/* block_start(): samples_per_beat = period_ema * 24; return 1.0/samples_per_beat; */
/* active(): (sample_pos - last_tick_sample) < fs*0.5 */
```

**Caveat for Omega:** PushNPull is an `audio_fx` and receives the host's FX-broadcast MIDI clock (`MOVE_MIDI_SOURCE_FX_BROADCAST`). Omega is a `sound_generator`; whether it receives 0xF8 clock via `on_midi` depends on host routing (`MOVE_MIDI_SOURCE_HOST` is defined for "Host-generated (clock, etc)"). **Given `move_info.h` now exists (§Q1), Omega should prefer `mi.tempo` over reinventing this phasor** — but the EMA-of-tick-period + hysteresis idiom is the fallback pattern if move-info is ever unavailable.

---

## Q4 — Kick / rumble / sidechain / DJ-filter modules comparable to Omega

- **No single module combines kick synthesis + techno rumble + sidechain + DJ filter** the way Omega (Bohm) does. `gh search repos "schwung rumble"` and `"schwung techno"` return **empty**. Omega is unique in the catalog. (HIGH — searched.)
- Closest **kick/drum** references (for the kick engines, not the groove redesign): `athousanddetails/schwung-9W9` (909), `-8W8` (808), `-6W6` (606), `-cw-78` (CR-78); `filliformes/forge-move` (FM/subtractive drums, ZDF Moog ladder); `legsmechanical/schwung-simian`, `filliformes/weird-dreams-move`, `mestela/schwung-sophie` (FM percussion). Cited in `Context/05_SOURCE_INDEX_AND_REFERENCES.md`.
- Closest **sidechain**: `schwung-ducker` (MIT, §3a-style ADHR — the closest analog to Omega's PERF DUCK) and `schwung-pushnpull` (vol-duck + filter sweep, tempo-synced).
- Closest **DJ/multimode filter**: `schwung-filter` (§3c — the SVF Omega's DJ filter should match).
- **Multi-tap / groove-delay for rumble specifically:** none dedicated; `space-delay` (multi-echo via feedback) and `ruminant` (feedback delay) are the nearest, plus MIDI-FX Euclidean sequencers (Eucalypso, Euclidrum, Branchage) parallel Omega's GEN groove Euclidean gate.

---

## Q5 — Gotchas from Schwung docs / headers (audio-FX & realtime)

All from `plugin_api_v1.h` and `docs/` (HIGH unless noted):

1. **NO CONTROL THREAD.** Every entry point (`create_instance`, `set_param`, `get_param`, `on_midi`, `render_block`) runs on the SPI callback (SCHED_FIFO 70, core 3). A 2026-08 audit found ~150 realtime violations across 113 modules — many with comments falsely claiming a control thread. Omega's "control-rate = in set_param" discipline is fine because that work is bounded, but **no `host->log`, no file I/O, no malloc even in `set_param`/`create_instance`** when loaded as a chain slot.
2. **CPU budget ≈ 2370 µs per 128-frame block** (measured 2026-08-26; older docs said ~2 ms). At 44.1 kHz, 128 frames = 2.9 ms wall; ~2370 µs is usable slack. Omega's target 10–15% is comfortable, but the groove's two 131072-float rings + feedback + reverb + LFO + filter per sample must stay inside this. (HIGH)
3. **Do NOT extend `host_api_v1_t`.** Appending a field past `reserved` (+120) is what boot-looped devices via `breakbeat`'s `get_project_bpm`. New host caps = dlsym'd exports. Omega already respects this. (HIGH)
4. **`midi_send_external` now flows to chain sub-plugins** (was NULL for years). A module that assumed NULL-guard = no-op may now emit to USB-A unexpectedly; check `slot_recv_channel()` (returns -2 if not slot-registered). Low risk for Omega unless it sends MIDI. (HIGH)
5. **`get_beat_position()` returns < 0 when stopped; may be NULL on old hosts** — always guard (Omega does). `get_bpm()` may be NULL too. `move_info_read()` returns 0 if unpublished. Layer the fallbacks. (HIGH)
6. **MIDI clock downbeat is at pulse 1, not 0** (`transport_grid.h`): `shadow_transport_pulses` zeroes on 0xFA then increments on each 0xF8, so beats land at `24N+1`. Anything firing on `pulses % 24 == 0` is one pulse early (20.8 ms @120 BPM, 125 ms @20 BPM). Relevant only if Omega ever counts raw clock pulses for bar-phase alignment. (HIGH)
7. **Idle-parking / `requires_continuous_processing`:** modules with internal time can have their DSP paused during silence, freezing internal clocks/tails. Verify Omega's rumble tail + feedback survive kick silence on-device. (MEDIUM — flag documented for audio_fx.)
8. **int16 I/O boundary + clamp:** every deep-read module converts `int16 → float (/32768) → DSP → clamp [-1,1] → *32767 → int16` (space-delay clamps to 32767/-32768; ruminant uses `lrintf(x*32767)`). Matches Omega's convention. Watch the asymmetric int16 range. (HIGH)
9. **Fixed 44.1 kHz, 128 frames/block** (`MOVE_SAMPLE_RATE`, `MOVE_FRAMES_PER_BLOCK`) — no negotiation; matches Omega. (HIGH)
10. **`get_param` is served per repaint, not per click** — keep it cheap (no rescans). Omega's `ui_hierarchy` assembly is already bounded/zero-alloc. (HIGH)

---

## Licenses (per repo — respect before copying code)

| Repo | License | Omega may… |
|------|---------|------------|
| `charlesvestal/schwung` (headers: `plugin_api_v1.h`, `move_info.h`, `audio_fx_api_v2.h`) | (repo has `LICENSE`; headers marked "PUBLIC: copy this one file") | **Copy the public headers** (`move_info.h` explicitly says so). Verify repo LICENSE spdx before bulk copy. |
| `charlesvestal/schwung-space-delay` | **MIT** | **Copy code** with attribution (DelayLine_Read, SmoothedValue, division table, ping-pong). |
| `mestela/schwung-ruminant` | **MIT** | **Copy code** with attribution (cubic_delay_read, allpass2, feedback idiom). |
| `charlesvestal/schwung-ducker` | **MIT** | **Copy code** with attribution (ADHR envelope, curve shapes). |
| `charlesvestal/schwung-filter` | **none declared** | **Technique only** — reimplement (Omega's CLAUDE.md already specifies TPT-SVF independently). |
| `legsmechanical/schwung-pushnpull` | **none declared** | **Technique only** — reimplement the clock idiom. |
| `athousanddetails/schwung-tape-echo2` | **GPL-3.0** | **Do NOT copy** into Omega unless Omega goes GPL — copyleft. Technique only. |

⚠ "none declared" = default all-rights-reserved; do not copy source, only learn the technique. Omega's CLAUDE.md gives no project license; confirm Omega's own license before vendoring MIT code (MIT requires retaining the copyright notice).

---

## Actionable takeaways for Omega TAPS redesign (ranked by value)

1. **Adopt `move_info.h` for tempo (HIGH value, LOW risk).** Copy `src/host/move_info.h` (public, self-contained). Use `mi.tempo` as the primary BPM and `mi.playing` for run/stop, with Omega's existing `get_beat_position → get_bpm → 120` chain as documented fallback (`move_info_read()==0` path). First call in `create_instance` (does the one-time dlsym). Compile `-D_GNU_SOURCE`. This is the "updated tempo API" the user flagged in `feedback_taps_redesign.md` and it removes the beat-differencing noise from `groove_update_tempo`. *Verify `mi.tempo` is populated on the target firmware before removing the fallback.*

2. **Fractional-delay tap reads (HIGH value).** Replace Omega's integer `(t+1)*samples_per_16th` tap indexing with a fractional read. Use **linear** (space-delay `DelayLine_Read`, MIT) for cheapness, or **Catmull-Rom cubic** (ruminant `cubic_delay_read`, MIT) for quality. This makes tap spacing smooth across BPM changes and lets `samples_per_16th` be a float — no more re-quantizing/zipper on tempo drift. Directly supports the "distinct kick copies on every 16th" goal at LENGTH-max.

3. **Per-sample parameter smoothing (HIGH value).** Add a smoother (space-delay `SmoothedValue` fixed-ramp, MIT; or filter `smoother_t` one-pole tau) for `fb_amount`/LENGTH, `tap_level[]`, `color_g`, and the DJ-filter cutoff. Omega currently only re-locks the tap interval on >0.5 BPM change and applies knob values instantly — smoothing kills clicks on live LENGTH/COLOR/TAP moves (a stated quality goal).

4. **Bidirectional LENGTH = feedback amount, validated (HIGH value).** Ruminant confirms Omega's chosen architecture (feedback + LP-in-loop + soft-clip) is the community-standard rumble drone. Implement the user's bidirectional LENGTH by making it drive **feedback 0→~1.1** (self-oscillating drone at left) while **flattening `tap_decay[]` toward equal at right** (distinct taps). Borrow ruminant's per-block feedback smoothing (`fb += 0.15*(target-fb)`) and its willingness to exceed 1.0 feedback (safe under LP+soft-clip). This replaces the current fixed `fb_amount = 0.30 + 0.58*v` + geometric `tap_decay = base^(t+1)` with a curve that reaches EQUAL tap levels at LENGTH-max (fixes the "later taps inaudible" bug in `feedback_taps_redesign.md`).

5. **Bidirectional reverb routing — feasibility CONFIRMED (MEDIUM-HIGH value).** The user's "reverb before taps (pre-smear) ↔ off ↔ after taps (post)" idea is standard techno technique and structurally trivial with a feedback ring: pre-smear = run the kick through the Schroeder reverb **before** `buf[write_pos]` write (reverb feeds the ring, so taps smear); post = current path (reverb on the tap sum). Omega already has the 2-comb + 1-allpass Schroeder in `groove_tick`; the redesign is just a routing switch on the MIX knob sign. No new DSP needed. Consider a heavier reverb (space-delay's tone-filtered feedback, or a small FDN) only if the Schroeder is too thin for pre-smear.

6. **Ping-pong / stereo width for the rumble (MEDIUM value).** space-delay's cross-feed write + `widthLevelComp = 1/sqrt(1-0.5*width)` (clamped 1.333) gives a stereo rumble that mono-sums cleanly (Omega already has a MONO force-sum for sub-bass, GRV-05). Optional polish.

7. **DJ filter / DUCK cross-checks (LOW-MEDIUM value).** `schwung-filter`'s `svf_set` (`k = 2 - 2*res`, floored) confirms Omega's DJ RESO mapping. `schwung-ducker`'s 4 curve shapes — especially **"Pump"** (cubic ease-out release `1 - (1-t)^3`) — are worth adopting for Omega's DUCK release shape (currently a plain exponential); the pump curve is the classic sidechain "breathe."

8. **Verify idle-park behaviour on-device (LOW value, DO-NOT-SKIP check).** Confirm Omega's rumble tail + feedback state don't freeze during kick silence. If they do and Omega can't set `requires_continuous_processing` (it's a sound_generator), keep the ring write advancing (Omega's GEN branch already writes zeros to keep `write_pos` coherent — extend that to all silent frames).

---

## Sources

### Primary (HIGH — read directly)
- `charlesvestal/schwung/src/host/plugin_api_v1.h` — current host ABI, threading contract, reserved-tail, render_split (via `gh api`).
- `charlesvestal/schwung/src/host/move_info.h` + `docs/MODULES.md` §"Reading Move's set" — the new tempo/transport API.
- `charlesvestal/schwung/src/host/audio_fx_api_v2.h`, `audio_fx_api_v1.h` — FX plugin interface.
- `charlesvestal/schwung/src/host/shadow_transport.h`, `transport_grid.h` — host-internal transport (24 PPQN, downbeat at pulse 1).
- `charlesvestal/schwung/release.json` (v1.6.1), `module-catalog.json` (147 modules).
- `charlesvestal/schwung-space-delay/src/dsp/spacecho.c` (MIT) — tempo-synced fractional delay.
- `mestela/schwung-ruminant/src/dsp/ruminant.c` (MIT) — cubic feedback delay.
- `charlesvestal/schwung-ducker/src/dsp/ducker.c` (MIT) — sidechain ADHR ducker.
- `charlesvestal/schwung-filter/src/dsp/svf_core.{c,h}`, `smoother.h` — TPT-SVF + smoother.
- `legsmechanical/schwung-pushnpull/src/dsp/clock.{c,h}` — MIDI-clock phasor.

### Secondary (MEDIUM)
- `module-catalog.json` metadata for the ~30 FX modules not individually source-read.
- `Context/05_SOURCE_INDEX_AND_REFERENCES.md` (Omega's own reference index) for drum-engine repos.

### Local context read
- `Omega/CLAUDE.md`, `src/groove.c`, `src/groove.h`, `src/omega.h`, `Context/01_SCHWUNG_DEV_ARCHITECTURE.md`, `Context/05_SOURCE_INDEX_AND_REFERENCES.md`, memory `feedback_taps_redesign.md`.

## Metadata
**Confidence breakdown:**
- Schwung release + ABI + tempo API: **HIGH** — read from Schwung's own v1.6.1 headers.
- Four deep-read FX modules' DSP: **HIGH** — source read line-by-line; licenses verified via `gh api`.
- Broader 147-module catalog scan: **MEDIUM** — metadata only; several licenses unverified ("(check)").
- `requires_continuous_processing` applicability to a sound_generator groove tail: **MEDIUM** — documented for audio_fx.
- PushNPull's "no reliable transport getter" claim: **MEDIUM** — true for its audio_fx context; superseded for Omega by move_info.h.

**Research date:** 2026-09-30
**Valid until:** ~2026-10-30 (Schwung is fast-moving — 147 modules, repo updated daily; re-verify release version and move_info.h fields before a later phase).
