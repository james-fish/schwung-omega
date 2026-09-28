/* mock_host.c — native mock host_api_v1_t (FNDTN-06). VERBATIM structure from
 * A-RESEARCH §Test Harness. sample_rate=44100, frames_per_block=128; log prints
 * to stderr; MIDI/clock/beat are drivable stubs (get_beat_position for transport
 * tests later). */
#include "mock_host.h"
#include <stdio.h>

static void   mock_log(const char *m)               { fprintf(stderr, "[host] %s\n", m); }
static int    mock_midi(const uint8_t *b, int n)     { (void)b; (void)n; return 0; }
static int    mock_clock(void)                       { return 0; }
static double mock_beat(void)                        { return 0.0; }

host_api_v1_t make_mock_host(void) {
    host_api_v1_t h = {0};
    h.api_version         = 1;
    h.sample_rate         = 44100;
    h.frames_per_block    = 128;
    h.log                 = mock_log;
    h.midi_send_internal  = mock_midi;
    h.midi_send_external  = mock_midi;
    h.get_clock_status    = mock_clock;
    h.get_beat_position   = mock_beat;
    return h;
}
