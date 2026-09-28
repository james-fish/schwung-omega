/* wav.h — minimal 16-bit PCM WAV writer for the offline test harness. */
#ifndef OMEGA_WAV_H
#define OMEGA_WAV_H

#include <stdio.h>
#include <stdint.h>

/* Opens path and writes a 44-byte canonical RIFF/WAVE/fmt/data PCM16 header
 * with placeholder chunk sizes. Returns NULL on failure. */
FILE *wav_open(const char *path, int sample_rate, int channels);

/* Appends nsamples int16 samples (interleaved) to an open WAV file. */
void  wav_write(FILE *f, const int16_t *samples, int nsamples);

/* Seeks back and patches the RIFF + data chunk sizes, then closes the file. */
void  wav_close(FILE *f);

#endif /* OMEGA_WAV_H */
