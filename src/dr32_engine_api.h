// dr32_engine_api.h — how ANOTHER MODULE offers DR32 a synth engine.
//
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Josh Gaines / legsmechanical
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// (MIT for this header, so that a module of any licence can include it. DR32
// itself is GPL-3.0-or-later.)
//
// ⭐ WHAT YOU SHIP. One extra shared object beside your module's own files:
//
//     modules/sound_generators/<your module>/dr32_engine.so
//
// exporting one function, `dr32_engine_plugin`. DR32 finds the file by its
// NAME in its sibling module folders, loads it, and your models appear in its
// engine picker under your module's name. Nothing in your module.json, no DR32
// release, no host change.
//
// ⭐ WHAT AN ENGINE IS. One drum VOICE and nothing else: a trigger in, MONO
// audio out. Volume, pan, velocity-to-volume, choke, the per-pad bus, the
// sends, the stereo stage, state, copy/paste and every page's layout are
// DR32's. Leave your module's own master stage (reverb, limiter, sequencer)
// out; DR32 has one of each per pad or per kit already.
//
// ⭐ ENGINE vs MODEL. An ENGINE is a DSP class with a parameter table. A MODEL
// is one named starting point for it ("Kick", "Closed Hat"): the engine to run
// and a value for each of its parameters. The picker offers models; a pad RUNS
// an engine, and from then on the values are the pad's own.
//
// ⚠ THE RULES THE AUDIO THREAD HOLDS YOU TO
//   - `create` / `destroy` may allocate, and may be called from more than one
//     thread (DR32 resamples a pad on a worker with an instance of its own):
//     keep them free of unguarded shared state. `set`, `note_on`, `choke` and
//     `render` must be REAL-TIME SAFE: no allocation, no file I/O, no locks.
//   - One instance per PAD, up to 32 at once. Keep an instance small.
//   - `render` OVERWRITES `out` with n mono float frames (n <= 1024) and says
//     whether the voice is still going (DR32X_RENDER_*). You need not work out
//     when it has gone quiet: DR32 stops calling a voice whose output has
//     stayed under -80 dB for 100 ms, until the next `note_on`.
//   - `set` takes effect on a sounding voice where the DSP allows.
//   - 44100 Hz today; honour the `sample_rate` you are given.
//
// ⚠ BUILD IT SELF-CONTAINED: `-fvisibility=hidden -Wl,-Bsymbolic`, with only
// the entry point exported. Your module's dsp.so may be loaded in the same
// process, and without that the two copies of your code can bind to each
// other's globals.
//
// ⚠ KEYS ARE BARE AND PERMANENT. A parameter's `key` ("pitch") is yours; DR32
// prefixes it with your plugin id and engine slug, and that full key is what a
// saved kit stores. So are a model's `slug` and your plugin `id`. Rename one
// and saved kits lose it. Add parameters and models freely.

#ifndef DR32_ENGINE_API_H
#define DR32_ENGINE_API_H

#ifdef __cplusplus
extern "C" {
#endif

#define DR32X_API_VERSION 1
#define DR32X_ENTRY       "dr32_engine_plugin"
#define DR32X_FILE        "dr32_engine.so"

/* What `render` returns. */
#define DR32X_RENDER_DONE  0   /* finished: do not call me again until the next note_on  */
#define DR32X_RENDER_ALIVE 1   /* still going; DR32 may end it once it has gone quiet     */
#define DR32X_RENDER_HOLD  2   /* still going AND not to be ended on silence: a voice
                                * with a gap in it longer than 100 ms (a late burst, a
                                * slow repeat). It must then return DONE itself.         */

#define DR32X_MAX_PARAMS  32   /* per engine                          */
#define DR32X_PAGE_KNOBS  8    /* per page: a bank holds eight knobs  */

/** One parameter, in DISPLAY units: the numbers the knob shows and a saved
 *  kit stores. Convert to your DSP's own unit in `set`.
 *  ⓘ The host's knob is LINEAR (a detent is 0.5% of the range). If a range
 *  crowds the useful values into a few detents, narrow the range. */
typedef struct dr32x_param {
    const char *key;        /* bare: [a-z0-9_], <= 16 chars: "pitch"           */
    const char *name;       /* knob label, <= 32 chars: "Pitch"                */
    const char *short_name; /* the cell's label, <= 8 chars: "PITCH"           */
    float       min, max, def, step;   /* a step < 1 makes it a float knob; a
                                        * whole-number knob always moves by 1   */
    const char *unit;       /* <= 8 chars: "hz", "%", "dB", "st", "ms"; or NULL */
    const char *page;       /* the bank it sits on, <= 16 chars: "Tone". Not a
                             * name DR32 uses: Pad, Shape, Mix, Stereo, Master,
                             * Resample, Category, Kit.                        */
    const char *options;    /* an ENUM: "Sine|Saw"; value = the index. Or NULL */
} dr32x_param;

typedef struct dr32x_engine {
    const char *slug;       /* [a-z0-9] (no underscore), <= 12 chars: "drum"   */
    const char *name;       /* "My Drum"                                       */
    int         nparams;    /* 1..DR32X_MAX_PARAMS                             */
    const dr32x_param *params;

    void *(*create)(int sample_rate);
    void  (*destroy)(void *e);
    /* Write parameter `idx` (display units). */
    void  (*set)(void *e, int idx, float display);
    /* Start a hit. vel01 is velocity/127; tune_st is the pad's pitch offset in
     * semitones (transpose + detune), 0 = the engine's own pitch. */
    void  (*note_on)(void *e, float vel01, float tune_st);
    /* OPTIONAL (may be NULL). The pad was cut short (choke group, all-off).
     * DR32 fades its output over 3 ms itself, so this is never needed for the
     * sound; it only lets a long-tailed voice stop costing CPU sooner. If you
     * do stop, fade rather than cut, so the two ramps do not click. */
    void  (*choke)(void *e);
    /* n MONO frames into `out`; returns a DR32X_RENDER_* value. */
    int   (*render)(void *e, float *out, int n);
} dr32x_engine;

typedef struct dr32x_model {
    const char *slug;       /* [a-z0-9_], <= 24 chars, unique in the plugin    */
    const char *name;       /* shown on the pad and in the picker              */
    int         engine;     /* index into the plugin's `engines`               */
    const float *values;    /* that engine's nparams values, display units     */
    float       volume_db;  /* the pad Volume to start from                    */
    float       pan;        /* the pad Pan to start from, -50..+50             */
} dr32x_model;

typedef struct dr32x_plugin {
    unsigned    api_version;    /* DR32X_API_VERSION                           */
    unsigned    struct_size;    /* sizeof(dr32x_plugin)                        */
    const char *id;             /* [a-z0-9], <= 12 chars: "mysynth"            */
    const char *name;           /* the picker's section: "My Synth"            */
    int         nengines;
    const dr32x_engine *engines;
    int         nmodels;
    const dr32x_model  *models;
} dr32x_plugin;

/** What DR32 tells the plugin. Valid only during the call. */
typedef struct dr32x_host {
    unsigned    api_version;    /* the version DR32 speaks                     */
    int         sample_rate;
    const char *module_dir;     /* YOUR module's folder (for samples, tables)  */
} dr32x_host;

/** The one export. Called off the audio thread; may read files. Return NULL to
 *  offer nothing. What it returns must stay valid for the life of the process.
 *  Normally called once; it is called again if the host unloads and reloads
 *  DR32, and must then return the same thing.
 *
 *  ⚠ VERSIONS. Refuse only a host OLDER than you were built for
 *  (`host->api_version < DR32X_API_VERSION`): DR32 keeps reading every
 *  version of this contract it has shipped, so a newer DR32 still loads a
 *  plugin built against an older header. */
const dr32x_plugin *dr32_engine_plugin(const dr32x_host *host);

#ifdef __cplusplus
}
#endif

#endif
