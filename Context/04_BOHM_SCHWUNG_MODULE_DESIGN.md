# Bohm Schwung Module C/C++ Technical Design

> **Module ID:** `bohm-kick`  
> **Module Name:** Bohm Kick & Techno Rumble System  
> **API Version:** Schwung C Plugin API v2 (`plugin_api_v2_t`)  

---

## 1. C Data Structure Definitions (`bohm_instance.h`)

```c
#ifndef BOHM_INSTANCE_H
#define BOHM_INSTANCE_H

#include <stdint.h>
#include <stdbool.h>
#include "host/plugin_api_v1.h"

#define MAX_DELAY_FRAMES 88200 // 2 seconds at 44.1kHz
#define NUM_TAPS 4

// Inner Kick Engine Enum
typedef enum {
    INNER_KICK_9W9 = 0,
    INNER_KICK_FM2X,
    INNER_KICK_WAVETABLE,
    INNER_KICK_SAMPLE
} kick_engine_type_t;

// Groove 4-Tap Delay State
typedef struct {
    float delay_buffer_l[MAX_DELAY_FRAMES];
    float delay_buffer_r[MAX_DELAY_FRAMES];
    int write_pos;
    float tap_vol[NUM_TAPS]; // 16th-note subdivisions
    float tap_length;
    float color_filter_cutoff;
    float env_state;
} groove_state_t;

// Performer Ducking & FX State
typedef struct {
    float duck_depth;      // 0.0 to 1.0
    float duck_release_ms; // 10ms to 500ms
    float duck_env;        // Current envelope attenuation (1.0 = dry, 0.0 = fully ducked)
    float dj_filter_hp_lp; // 0.0 = LP, 0.5 = Neutral, 1.0 = HP
    float dj_reso;         // 0.0 to 1.0
    bool  soft_clip_enable;
} performer_state_t;

// Primary Bohm Instance Structure
typedef struct bohm_instance {
    const host_api_v1_t *host;
    
    // Master Parameters
    float main_volume;
    kick_engine_type_t active_engine;
    
    // Inner Kick Voice State
    float pitch;
    float pitch_curve_808_909; // 0 = 808 slow boom, 127 = 909 punch
    float length_ms;
    float sustain;
    float trs_decay;
    float trs_tone;
    
    // Nested Inner Kick Pointer (e.g. 9W9 or FM)
    void *inner_kick_instance;
    plugin_api_v2_t *inner_kick_api;
    
    // Sub-Modules
    groove_state_t groove;
    performer_state_t performer;
    
    // Internal Scratch Buffers (Pre-allocated for RT Audio Thread)
    int16_t scratch_kick_buf[256];   // Stereo interleaved [L0, R0, L1, R1...]
    int16_t scratch_groove_buf[256];
} bohm_instance_t;

#endif // BOHM_INSTANCE_H
```

---

## 2. Audio Callback Execution Pipeline (`render_block`)

```c
#include "bohm_instance.h"
#include <math.h>

static float fast_tanh(float x) {
    if (x >= 0.0f) return x / (1.0f + x);
    return x / (1.0f - x);
}

void bohm_render_block(void *instance, int16_t *out_lr, int frames) {
    bohm_instance_t *inst = (bohm_instance_t*)instance;
    
    // 1. Render Inner Kick Voice into Scratch Buffer
    if (inst->inner_kick_instance && inst->inner_kick_api) {
        inst->inner_kick_api->render_block(inst->inner_kick_instance, inst->scratch_kick_buf, frames);
    } else {
        // Fallback: silence scratch buffer
        for (int i = 0; i < frames * 2; i++) inst->scratch_kick_buf[i] = 0;
    }
    
    // 2. Process Groove 4-Tap Rumble Generator
    for (int f = 0; f < frames; f++) {
        float kick_l = (float)inst->scratch_kick_buf[f * 2] / 32768.0f;
        float kick_r = (float)inst->scratch_kick_buf[f * 2 + 1] / 32768.0f;
        
        // Write kick signal into Groove circular delay buffer
        inst->groove.delay_buffer_l[inst->groove.write_pos] = kick_l;
        inst->groove.delay_buffer_r[inst->groove.write_pos] = kick_r;
        
        // Calculate 16th-note multi-tap delay read positions
        float groove_l = 0.0f, groove_r = 0.0f;
        int tap_interval = (int)(inst->host->sample_rate * 0.125f); // 16th note at 120BPM
        
        for (int t = 0; t < NUM_TAPS; t++) {
            int read_pos = (inst->groove.write_pos - (t + 1) * tap_interval + MAX_DELAY_FRAMES) % MAX_DELAY_FRAMES;
            groove_l += inst->groove.delay_buffer_l[read_pos] * inst->groove.tap_vol[t];
            groove_r += inst->groove.delay_buffer_r[read_pos] * inst->groove.tap_vol[t];
        }
        
        // Update circular buffer write pointer
        inst->groove.write_pos = (inst->groove.write_pos + 1) % MAX_DELAY_FRAMES;
        
        // 3. Sidechain Ducking Envelope Calculation
        if (fabsf(kick_l) > 0.1f) {
            inst->performer.duck_env = 1.0f - inst->performer.duck_depth; // Trigger duck
        } else {
            // Release ducking curve back to 1.0 (dry)
            inst->performer.duck_env += (1.0f - inst->performer.duck_env) * 0.005f;
        }
        
        // Apply ducking to Groove Rumble signal
        groove_l *= inst->performer.duck_env;
        groove_r *= inst->performer.duck_env;
        
        // 4. Sum Kick + Groove Rumble
        float mix_l = (kick_l + groove_l) * inst->main_volume;
        float mix_r = (kick_r + groove_r) * inst->main_volume;
        
        // 5. End-of-Chain Soft-Clipping Limiter
        if (inst->performer.soft_clip_enable) {
            mix_l = fast_tanh(mix_l);
            mix_r = fast_tanh(mix_r);
        }
        
        // Write to Output Buffer
        out_lr[f * 2]     = (int16_t)(mix_l * 32767.0f);
        out_lr[f * 2 + 1] = (int16_t)(mix_r * 32767.0f);
    }
}
```

---

## 3. Parameter Dispatching (`set_param` / `get_param`)

```c
void bohm_set_param(void *instance, const char *key, const char *val) {
    bohm_instance_t *inst = (bohm_instance_t*)instance;
    float fval = atof(val);
    int ival = atoi(val);

    if (strcmp(key, "kick_model") == 0) {
        inst->active_engine = (kick_engine_type_t)ival;
    } else if (strcmp(key, "main_volume") == 0) {
        inst->main_volume = fval;
    } else if (strcmp(key, "kick_length") == 0) {
        inst->length_ms = fval;
    } else if (strcmp(key, "pitch_curve") == 0) {
        inst->pitch_curve_808_909 = fval;
    } else if (strcmp(key, "tap1_vol") == 0) {
        inst->groove.tap_vol[0] = fval / 127.0f;
    } else if (strcmp(key, "tap2_vol") == 0) {
        inst->groove.tap_vol[1] = fval / 127.0f;
    } else if (strcmp(key, "duck_depth") == 0) {
        inst->performer.duck_depth = fval / 127.0f;
    }
    
    // Pass relevant parameters down to inner kick engine
    if (inst->inner_kick_instance && inst->inner_kick_api) {
        inst->inner_kick_api->set_param(inst->inner_kick_instance, key, val);
    }
}
```
