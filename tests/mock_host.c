/* mock_host.c — native mock host_api_v1_t (FNDTN-06). VERBATIM structure from
 * A-RESEARCH §Test Harness. sample_rate=44100, frames_per_block=128; log prints
 * to stderr; MIDI/clock stubs.
 *
 * Phase C (Wave 0, GRV-02): the transport callbacks are now DRIVABLE so the
 * offline harness can derive live BPM the way the groove tempo clock will
 * (get_beat_position beat-delta -> get_bpm fallback -> 120 constant).
 *   - get_beat_position() reads a module-static g_mock_beat the test advances
 *     per block (mock_host_set_beat / mock_host_advance_beat).
 *   - get_bpm() reports a settable g_mock_bpm (mock_host_set_bpm).
 *   - make_mock_host_null_transport() returns a host with BOTH callbacks NULL to
 *     exercise the last-resort 120 constant path (C-RESEARCH Pitfall 2).
 * make_mock_host() RESETS the transport (beat=0, bpm=120) so each test starts
 * from a known state. */
#include "mock_host.h"
#include <stdio.h>

static double g_mock_beat = 0.0;    /* current beat position (quarter notes) */
static float  g_mock_bpm  = 120.0f; /* BPM reported by mock_get_bpm */

static void   mock_log(const char *m)               { fprintf(stderr, "[host] %s\n", m); }
static int    mock_midi(const uint8_t *b, int n)     { (void)b; (void)n; return 0; }
static int    mock_clock(void)                       { return 0; }
static double mock_beat(void)                        { return g_mock_beat; }
static float  mock_get_bpm(void)                     { return g_mock_bpm; }

/* Drive the transport from the test harness (all off the audio thread). */
void mock_host_set_beat(double beat)      { g_mock_beat = beat; }
void mock_host_advance_beat(double dbeat) { g_mock_beat += dbeat; }
void mock_host_set_bpm(float bpm)         { g_mock_bpm = bpm; }

host_api_v1_t make_mock_host(void) {
    /* Reset transport so every test starts from a known state. */
    g_mock_beat = 0.0;
    g_mock_bpm  = 120.0f;

    host_api_v1_t h = {0};
    h.api_version         = 1;
    h.sample_rate         = 44100;
    h.frames_per_block    = 128;
    h.mapped_memory       = NULL;
    h.audio_out_offset    = 0;
    h.audio_in_offset     = 0;
    h.log                 = mock_log;
    h.midi_send_internal  = mock_midi;
    h.midi_send_external  = mock_midi;
    h.get_clock_status    = mock_clock;
    h.get_beat_position   = mock_beat;
    h.get_bpm             = mock_get_bpm;
    return h;
}

/* NULL-transport variant: BOTH get_beat_position and get_bpm are NULL, exercising
 * the groove tempo clock's last-resort 120 constant path (GRV-02, Pitfall 2). All
 * other stubs (log/midi/clock/sample_rate/frames_per_block) match make_mock_host. */
host_api_v1_t make_mock_host_null_transport(void) {
    host_api_v1_t h = {0};
    h.api_version         = 1;
    h.sample_rate         = 44100;
    h.frames_per_block    = 128;
    h.mapped_memory       = NULL;
    h.audio_out_offset    = 0;
    h.audio_in_offset     = 0;
    h.log                 = mock_log;
    h.midi_send_internal  = mock_midi;
    h.midi_send_external  = mock_midi;
    h.get_clock_status    = mock_clock;
    h.get_beat_position   = NULL;   /* no transport */
    h.get_bpm             = NULL;   /* no BPM either -> 120 last-resort */
    return h;
}
