/* malloc_trap.c — audio-thread allocation trap (FNDTN-03, D-13).
 *
 * macOS caveat: on the macOS host, symbol interposition via __libc_* is
 * unavailable; the trap is authoritative only in the Linux CI native build.
 * On macOS `make test` runs WITHOUT the trap (the Makefile omits
 * -DOMEGA_MALLOC_TRAP on Darwin), so CI is the gate for FNDTN-03.
 *
 * When compiled (Linux, -DOMEGA_MALLOC_TRAP): malloc/free/calloc/realloc are
 * interposed and abort() if g_audio_thread_active is true. The harness sets the
 * flag true at the START of the render loop and false at the END, catching any
 * post-init lazy allocation (D-13 / CONTEXT specifics).
 */
#ifdef OMEGA_MALLOC_TRAP

#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

/* Real libc allocators (glibc). Declared here to avoid pulling private headers. */
extern void *__libc_malloc(size_t n);
extern void  __libc_free(void *p);
extern void *__libc_calloc(size_t a, size_t b);
extern void *__libc_realloc(void *p, size_t n);

bool g_audio_thread_active = false;

void *malloc(size_t n) {
    if (g_audio_thread_active) abort();
    return __libc_malloc(n);
}

void free(void *p) {
    if (g_audio_thread_active) abort();
    __libc_free(p);
}

void *calloc(size_t a, size_t b) {
    if (g_audio_thread_active) abort();
    return __libc_calloc(a, b);
}

void *realloc(void *p, size_t n) {
    if (g_audio_thread_active) abort();
    return __libc_realloc(p, n);
}

#endif /* OMEGA_MALLOC_TRAP */
