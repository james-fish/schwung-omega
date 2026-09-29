# Schwung Framework & Host Development Architecture

> **Target Platform:** Ableton Move running the Schwung Shadow UI Framework  
> **Target Architecture:** Linux ARM64 (`aarch64`), pinned to `glibc 2.35`  
> **Compilation Environment:** Docker ARM64 Cross-Compiler Toolchain  

---

## 1. Overview of Schwung Architecture

Schwung (formerly *Move Everything*) is an open-source framework running on Ableton Move that enables custom C/C++ audio plugins, synths, drum racks, and MIDI effects to execute natively alongside Ableton Move's stock firmware.

### Key Architectural Concepts
1. **Shadow UI:** A parallel interface process (`shadow_ui`) that runs alongside Move's stock firmware. It intercepts hardware inputs (encoders, pads, buttons, jog wheel) and renders a custom UI to Move's OLED screen.
2. **Slots & Chains:** Schwung provides 4 instrument slots (corresponding to Move's 4 tracks) plus 1 Master FX slot. Each slot hosts a chain: `MIDI FX -> Sound Generator / Synth -> Audio FX 1 -> Audio FX 2`.
3. **C Plugin API v2 (`plugin_api_v2_t`):** The native C/C++ shared library interface (`dsp.so`) implemented by sound generators and audio/MIDI effects.
4. **Parameter Hierarchy (`ui_hierarchy`):** A JSON schema reported by the plugin that maps C DSP variables to Move's 8 physical rotary encoders and OLED page navigation.

---

## 2. Schwung C Plugin API v2 Specification

All Schwung sound generator modules must implement the `plugin_api_v2_t` interface exported via `move_plugin_init_v2`.

### Shared Header Interface (`plugin_api_v1.h`)

```c
#include <stdint.h>

typedef struct host_api_v1 {
    uint32_t api_version;
    int sample_rate;         /* Fixed at 44100 Hz */
    int frames_per_block;    /* Block size, typically 128 frames */

    /* Memory Mapped Direct Access */
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;

    /* Logging function (writes to /data/UserData/schwung/debug.log) */
    void (*log)(const char *msg);

    /* USB-MIDI Packet Transmit Functions */
    int (*midi_send_internal)(const uint8_t *msg, int len);
    int (*midi_send_external)(const uint8_t *msg, int len);

    /* Clock & Transport */
    int (*get_clock_status)(void);
    double (*get_beat_position)(void); /* Transport beat position (24-PPQN synced) */
} host_api_v1_t;

typedef struct plugin_api_v2 {
    uint32_t api_version; /* Must be set to 2 */
    
    /* Instance Lifecycle */
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void  (*destroy_instance)(void *instance);
    
    /* Input Events */
    void  (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void  (*set_param)(void *instance, const char *key, const char *val);
    int   (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    
    /* Audio Rendering Callback (SPI Audio Thread) */
    void  (*render_block)(void *instance, int16_t *out_lr, int frames);
} plugin_api_v2_t;

/* Plugin Export Initialization Function */
plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);
```

### Real-time Audio Thread Rules
- **Thread Context:** `render_block`, `set_param`, `get_param`, `on_midi`, `create_instance`, and `destroy_instance` all execute directly on Schwung's high-priority SPI audio callback thread (`SCHED_FIFO 70`, core 3).
- **CRITICAL:** **Zero dynamic allocation (`malloc`, `free`, `calloc`, `realloc`, `new`, `delete`) on the audio thread.** All memory structures, voice pools, delay lines, and reverb buffers must be pre-allocated inside `create_instance`.
- **Zero blocking IO:** No file reads (`fopen`, `fread`), directory scans, network calls, or mutex locking.

---

## 3. Module Manifest (`module.json`)

Every module directory requires a `module.json` manifest.

```json
{
  "id": "bohm-kick",
  "name": "Bohm Kick & Rumble",
  "abbrev": "BOHM",
  "version": "1.0.0",
  "author": "Custom Developer",
  "description": "Ohm Force Bohm-inspired stereo kick & techno rumble engine for Schwung",
  "api_version": 2,
  "capabilities": {
    "chainable": true,
    "component_type": "sound_generator",
    "audio_out": true,
    "midi_in": true,
    "pad_layout": "drums"
  }
}
```

### Key Manifest Fields
- `id`: Lowercase hyphenated string (e.g. `bohm-kick`).
- `component_type`: `sound_generator` for instruments, `audio_fx` for insert/send effects, `midi_fx` for MIDI transformers, or `tool` / `overtake` for full-screen utilities.
- `pad_layout`: `"drums"` indicates that Move's 16 pads represent drum slots rather than a chromatic keyboard.

---

## 4. Parameter Hierarchy & UI Schema (`ui_hierarchy`)

For `sound_generator` modules, Schwung queries `get_param(instance, "ui_hierarchy", buf, buf_len)` to dynamically discover the knob layout, page structure, and parameter mappings for Move's 8 physical rotary encoders.

### Schema Structure (`ui_hierarchy`)

```json
{
  "pad_layout": "drums",
  "child_index_param": "current_pad",
  "levels": {
    "root": {
      "name": "Bohm Master",
      "params": [
        {"key": "kick_model", "name": "Model", "type": "enum", "options": ["9W9", "FM2X", "Wavetable", "Sample"]},
        {"key": "main_volume", "name": "Volume", "type": "float", "min": 0.0, "max": 1.0},
        {"level": "kick_page", "label": "Kick Engine"},
        {"level": "rumble_page", "label": "Groove Rumble"},
        {"level": "ducker_page", "label": "Performer FX"}
      ],
      "knobs": ["kick_model", "main_volume"]
    },
    "kick_page": {
      "name": "Kick Engine",
      "params": [
        {"key": "kick_length", "name": "Length", "type": "int", "min": 0, "max": 127},
        {"key": "kick_sustain", "name": "Sustain", "type": "int", "min": 0, "max": 127},
        {"key": "pitch_curve", "name": "Curve (808/909)", "type": "int", "min": 0, "max": 127},
        {"key": "trs_decay", "name": "TRS Decay", "type": "int", "min": 0, "max": 127},
        {"key": "trs_tone", "name": "TRS Tone", "type": "int", "min": 0, "max": 127}
      ],
      "knobs": ["kick_length", "kick_sustain", "pitch_curve", "trs_decay", "trs_tone"]
    },
    "rumble_page": {
      "name": "Groove Rumble",
      "params": [
        {"key": "grv_vol", "name": "Rumble Vol", "type": "float", "min": 0.0, "max": 1.0},
        {"key": "grv_length", "name": "Rumble Len", "type": "int", "min": 0, "max": 127},
        {"key": "tap1_vol", "name": "Tap 1", "type": "int", "min": 0, "max": 127},
        {"key": "tap2_vol", "name": "Tap 2", "type": "int", "min": 0, "max": 127},
        {"key": "tap3_vol", "name": "Tap 3", "type": "int", "min": 0, "max": 127},
        {"key": "tap4_vol", "name": "Tap 4", "type": "int", "min": 0, "max": 127}
      ],
      "knobs": ["grv_vol", "grv_length", "tap1_vol", "tap2_vol", "tap3_vol", "tap4_vol"]
    }
  }
}
```

### Parameter Types
- `int`: Integer range (`min`, `max`).
- `float`: Continuous float value (`min`, `max`, optional `step`).
- `enum`: Discrete selection (`options: ["Opt1", "Opt2", ...]`). Passed as 0-indexed string or integer key.
- `canvas`: Custom fullscreen or cell graphic (e.g. ADSR envelopes, filter response curves).

---

## 5. Hosting Architecture in DR32 & Movy

### How DR32 (`schwung-dr32`) Hosts Modules
1. **32 Pad Memory Layout:** DR32 creates 32 pad slots. Each pad slot holds an engine selector (`ENGN`).
2. **Inner Engine Instantiation:** When a pad is assigned an engine (such as `9W9`, `Simian`, or a custom module), DR32 calls `create_instance()` for that engine and stores the instance pointer in its pad array:
   ```c
   typedef struct dr32_pad_t {
       int engine_type; // SAMPLE, SIMIAN, 9W9, CUSTOM_BOHM, etc.
       void *engine_instance;
       plugin_api_v2_t *engine_api;
       float volume;
       float pan;
       int choke_group;
       float send_a;
       float send_b;
   } dr32_pad_t;
   ```
3. **Block Rendering:** In DR32's `render_block`, it loops through all active pads, calls `engine_api->render_block(pad->engine_instance, pad_buffer, frames)`, applies pad-level volume/pan/mutes, routes audio to per-pad module buses, and sums the output.
4. **Nesting Capability:** Because DR32 treats any module implementing `plugin_api_v2_t` as a valid engine, your custom Bohm module can be loaded into DR32 on Pad 1. Your Bohm module's internal `render_block` can in turn host and trigger an inner sound source (such as a 9W9 kick, FM synth, or sample reader), process that audio buffer through its Bohm Groove rumble and Performer ducking DSP, and output the result to DR32's pad bus.

### How Movy (`schwung-movy`) Hosts Chains
1. **16-Track Host:** Movy hosts 16 complete module chains natively inside a Rust/C engine (`dsp.so`).
2. **Track Chain Navigation:** Each of Movy's 16 tracks contains a full slot chain (`MIDI FX -> Synth -> FX 1 -> FX 2`).
3. **UI Page Mapping:** Movy inspects the child parameter hierarchy (`ui_hierarchy`) of loaded modules and automatically renders Elektron-style parameter pages, arc knobs, and ADSR graphics on Move's OLED screen.

---

## 6. Docker ARM64 Cross-Compilation Pipeline

All Schwung binaries must be cross-compiled for Ableton Move's Linux ARM64 architecture (`aarch64`), pinned strictly to `glibc 2.35`.

### Build Script Template (`scripts/build.sh`)

```bash
#!/bin/bash
set -e

# Use official Schwung Docker build image for ARM64 cross-compilation
DOCKER_IMAGE="ghcr.io/charlesvestal/schwung-builder:latest"

echo "Building Bohm Schwung module for ARM64..."

docker run --rm -v "$(pwd):/workspace" -w /workspace "$DOCKER_IMAGE" bash -c "
    aarch64-linux-gnu-gcc -O3 -shared -fPIC \
        -Isrc -Isrc/include \
        src/bohm_module.c \
        src/dsp/groove_rumble.c \
        src/dsp/performer_duck.c \
        -o build/dsp.so -lm
"

echo "Build complete: build/dsp.so"
```

### Deployment Commands
```bash
# Safe deployment via atomic rename over SCP to Move
scp build/dsp.so root@move.local:/data/UserData/schwung/modules/sound_generators/bohm-kick/dsp.so
```
