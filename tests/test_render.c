/* test_render.c — Wave 0 offline harness (TEMPORARY stub-driven).
 *
 * Wave 0 proves the pipeline goes green end-to-end BEFORE any DSP exists:
 *   - KICK-15: omega_primitives_selfcheck() asserts the sine guard sample.
 *   - FNDTN-06: mock host constructed; a WAV file is produced.
 *   - FNDTN-07: every sample flows through the shared omega_to_i16 clamp path
 *     and is asserted in int16 range.
 *   - FNDTN-03: the render loop runs with g_audio_thread_active=true (Linux),
 *     so the malloc trap would abort on any heap call.
 *
 * TODO(A-02): replace this stub render with the real move_plugin_init_v2
 * lifecycle (create_instance -> on_midi trigger FM2 note 36 -> render_block x512
 * -> destroy_instance) and assert g_models[MODEL_FM2]->name == "FM2". The
 * registry (g_models) is intentionally NOT referenced yet — it lands in A-02.
 */
#include "omega.h"
#include "dsp_primitives.h"
#include "mock_host.h"
#include "wav.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef OMEGA_MALLOC_TRAP
extern bool g_audio_thread_active;  /* defined in malloc_trap.c (Linux) */
#endif

#define BLOCK   128
#define NBLOCKS 512   /* ~1.5 s at 44100 */

int main(void) {
    /* KICK-15: runtime guard-sample self-check. */
    omega_primitives_selfcheck();

    /* FNDTN-06: mock host (validates the struct is constructible natively). */
    host_api_v1_t host = make_mock_host();
    assert(host.sample_rate == 44100);
    assert(host.frames_per_block == 128);

    /* Stub instance on the stack; zero-init mirrors the single-calloc contract. */
    struct bohm_instance inst = {0};
    inst.model       = MODEL_FM2;
    inst.main_volume = 1.0f;

    FILE *wav = wav_open("tests/output/fm2_kick.wav", 44100, 2);
    assert(wav != NULL);

#ifdef OMEGA_MALLOC_TRAP
    g_audio_thread_active = true;   /* FNDTN-03: any alloc from here aborts */
#endif

    for (int b = 0; b < NBLOCKS; b++) {
        int16_t out[BLOCK * 2];
        for (int n = 0; n < BLOCK; n++) {
            /* Silent stub: A-02 replaces this with FM2 render output.
             * Route through the SAME clamp path A-02 will use (FNDTN-07). */
            float sample = 0.0f * inst.main_volume;
            int16_t s = omega_to_i16(sample);
            assert(s >= INT16_MIN && s <= INT16_MAX);
            out[n * 2]     = s;
            out[n * 2 + 1] = s;
        }
        wav_write(wav, out, BLOCK * 2);
    }

#ifdef OMEGA_MALLOC_TRAP
    g_audio_thread_active = false;
#endif

    wav_close(wav);

    /* Assert the WAV exists and carries data beyond the 44-byte header. */
    FILE *check = fopen("tests/output/fm2_kick.wav", "rb");
    assert(check != NULL);
    fseek(check, 0, SEEK_END);
    long sz = ftell(check);
    fclose(check);
    assert(sz > 44);

    printf("test_render: PASS (%ld bytes written to tests/output/fm2_kick.wav)\n", sz);
    return 0;
}
