/* wav.c — 44-byte canonical PCM16 WAV writer.
 *
 * wav_open writes a 44-byte RIFF/WAVE/fmt /data header with placeholder sizes.
 * wav_write appends interleaved int16 PCM. wav_close uses the final file
 * position to compute the actual data size and patches the RIFF (offset 4) and
 * data (offset 40) size fields, so no running byte counter is needed.
 */
#include "wav.h"
#include <string.h>

static void put_u32le(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void put_u16le(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

FILE *wav_open(const char *path, int sample_rate, int channels) {
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;

    const uint16_t bits_per_sample = 16;
    const uint16_t block_align      = (uint16_t)(channels * (bits_per_sample / 8));
    const uint32_t byte_rate        = (uint32_t)sample_rate * block_align;

    unsigned char hdr[44];
    memcpy(hdr + 0,  "RIFF", 4);
    put_u32le(hdr + 4, 0);            /* RIFF chunk size — patched in wav_close */
    memcpy(hdr + 8,  "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    put_u32le(hdr + 16, 16);          /* fmt chunk size */
    put_u16le(hdr + 20, 1);           /* audio format = PCM */
    put_u16le(hdr + 22, (uint16_t)channels);
    put_u32le(hdr + 24, (uint32_t)sample_rate);
    put_u32le(hdr + 28, byte_rate);
    put_u16le(hdr + 32, block_align);
    put_u16le(hdr + 34, bits_per_sample);
    memcpy(hdr + 36, "data", 4);
    put_u32le(hdr + 40, 0);           /* data chunk size — patched in wav_close */

    fwrite(hdr, 1, sizeof(hdr), f);
    return f;
}

void wav_write(FILE *f, const int16_t *samples, int nsamples) {
    if (!f || nsamples <= 0) return;
    fwrite(samples, sizeof(int16_t), (size_t)nsamples, f);
}

void wav_close(FILE *f) {
    if (!f) return;

    long end = ftell(f);
    if (end >= 44) {
        uint32_t data_size = (uint32_t)(end - 44);
        uint32_t riff_size = (uint32_t)(end - 8);
        unsigned char buf[4];

        put_u32le(buf, riff_size);
        fseek(f, 4, SEEK_SET);
        fwrite(buf, 1, 4, f);

        put_u32le(buf, data_size);
        fseek(f, 40, SEEK_SET);
        fwrite(buf, 1, 4, f);
    }
    fclose(f);
}
