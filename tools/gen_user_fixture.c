/* gen_user_fixture.c — emit tests/fixtures/user_kick.wav (KICK-10 fixture).
 *
 * Host-side (native cc) generator. Writes a short, valid canonical PCM16 WAV
 * (a synthesized ~200 ms decaying sine "kick") using the SAME tests/wav.c
 * writer the plugin's USR loader (dsp.c usr_load_wav) parses. Committed/built
 * as a fixture so the USR off-render load path has a real file to read.
 *
 * Usage: gen_user_fixture <output-path>
 */
#include "../tests/wav.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <out.wav>\n", argv[0]); return 1; }
    const char *path = argv[1];

    const int sr = 44100;
    const int nframes = sr / 5;   /* ~200 ms one-shot */

    FILE *f = wav_open(path, sr, 1);   /* mono */
    if (!f) { fprintf(stderr, "gen_user_fixture: cannot open %s\n", path); return 1; }

    /* A simple decaying sine with a downward pitch sweep — a recognizable kick. */
    double phase = 0.0;
    for (int n = 0; n < nframes; n++) {
        double t   = (double)n / (double)sr;
        double env = exp(-t * 18.0);                 /* ~55 ms tail */
        double f0  = 50.0 + 120.0 * exp(-t * 40.0);  /* sweep 170 -> 50 Hz */
        phase += 2.0 * M_PI * f0 / (double)sr;
        double s = sin(phase) * env * 0.9;
        int16_t v = (int16_t)lrint(s * 32767.0);
        wav_write(f, &v, 1);
    }
    wav_close(f);
    return 0;
}
