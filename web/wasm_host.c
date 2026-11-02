/* WebAssembly shell around the Omega engine (browser bench only; never shipped to the Move).
 * Adapted from Quark's web/wasm_host.c. The same src/ sources that build dsp.so compile to wasm32;
 * this is a one-instance Schwung stand-in with fixed scratch buffers so JavaScript needs no malloc.
 * It also owns the bench's 16-step kick sequencer, so steps land sample-accurately on the same
 * beat clock the engine's transport reads (both the AudioWorklet and the fallback path share it). */
#include <stdint.h>
#include <string.h>
#include "omega.h"

extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

#define EXPORT __attribute__((used, visibility("default")))
#define KEY_CAP 256
#define VAL_CAP 65536
#define OUT_CAP 65536

static host_api_v1_t g_host;
static plugin_api_v2_t *g_api;
static void *g_inst;
static float g_bpm = 128.0f;
static int g_running;
static double g_beats;
static int g_sr = 44100;
static unsigned g_steps = 0x8888u;     /* bit 15 = step 1: four on the floor */
static unsigned g_accent = 0x0000u;
static int g_cur_step = -1;
static int g_note_off_in = -1;

static char g_key[KEY_CAP];
static char g_val[VAL_CAP];
static char g_out[OUT_CAP];
static int16_t g_pcm[128 * 2];

static float host_bpm(void) { return g_bpm; }
static int host_clock(void) { return g_running ? 2 : 1; }   /* MOVE_CLOCK_STATUS_RUNNING / _STOPPED */
static double host_beats(void) { return g_running ? g_beats : -1.0; }

EXPORT char *wa_key(void) { return g_key; }
EXPORT char *wa_val(void) { return g_val; }
EXPORT char *wa_out(void) { return g_out; }
EXPORT int16_t *wa_pcm(void) { return g_pcm; }
EXPORT int wa_key_cap(void) { return KEY_CAP; }
EXPORT int wa_val_cap(void) { return VAL_CAP; }

EXPORT int wa_init(int sample_rate) {
    if (g_inst) return 1;
    g_sr = sample_rate > 0 ? sample_rate : 44100;
    memset(&g_host, 0, sizeof g_host);
    g_host.api_version = 2;
    g_host.sample_rate = g_sr;
    g_host.frames_per_block = 128;
    g_host.get_bpm = host_bpm;
    g_host.get_clock_status = host_clock;
    g_host.get_beat_position = host_beats;
    g_api = move_plugin_init_v2(&g_host);
    if (!g_api) return 0;
    g_inst = g_api->create_instance("", 0);
    return g_inst ? 1 : 0;
}

EXPORT void wa_set_tempo(float bpm, int running) {
    g_bpm = bpm > 20.0f ? bpm : 128.0f;
    if (running && !g_running) { g_beats = 0.0; g_cur_step = -1; }
    if (!running) g_cur_step = -1;
    g_running = running ? 1 : 0;
}
EXPORT void wa_seq(int steps, int accent) { g_steps = (unsigned)steps & 0xFFFFu; g_accent = (unsigned)accent & 0xFFFFu; }
EXPORT int wa_seq_step(void) { return g_cur_step; }

EXPORT void wa_set(void) {
    g_key[KEY_CAP - 1] = 0;
    g_val[VAL_CAP - 1] = 0;
    if (g_inst) g_api->set_param(g_inst, g_key, g_val);
}

EXPORT int wa_get(void) {
    g_key[KEY_CAP - 1] = 0;
    if (!g_inst) return -1;
    g_out[0] = 0;
    return g_api->get_param(g_inst, g_key, g_out, OUT_CAP);
}

EXPORT void wa_midi(int status, int d1, int d2) {
    if (!g_inst) return;
    uint8_t msg[3] = {(uint8_t)status, (uint8_t)d1, (uint8_t)d2};
    g_api->on_midi(g_inst, msg, 3, 0);
}

/* Render in sub-blocks split at 16th boundaries so each kick lands on its sample. */
EXPORT void wa_render(int frames) {
    if (!g_inst) return;
    if (frames > 128) frames = 128;
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        if (g_running) {
            double spb16 = 60.0 / g_bpm * g_sr / 4.0;               /* samples per 16th */
            double pos16 = g_beats * 4.0;
            long idx = (long)pos16;
            if (g_cur_step < 0 || (idx & 15) != g_cur_step) {
                g_cur_step = (int)(idx & 15);
                if ((g_steps >> (15 - g_cur_step)) & 1u) {
                    int vel = ((g_accent >> (15 - g_cur_step)) & 1u) ? 127 : 100;
                    wa_midi(0x90, 36, vel);
                    g_note_off_in = (int)(spb16 * 0.5);
                }
            }
            double to_next = ((double)(idx + 1) - pos16) * spb16;
            int tn = (int)to_next + 1;
            if (tn < n) n = tn < 1 ? 1 : tn;
        }
        g_api->render_block(g_inst, g_pcm + 2 * done, n);
        if (g_running) g_beats += (double)n * (double)g_bpm / (60.0 * g_sr);
        if (g_note_off_in >= 0) { g_note_off_in -= n; if (g_note_off_in < 0) wa_midi(0x80, 36, 0); }
        done += n;
    }
}
