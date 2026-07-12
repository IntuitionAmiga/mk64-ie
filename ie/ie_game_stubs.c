/*
 * Explicit stand-ins for symbols the full game image references but
 * the IE backend does not (or does not yet) provide for real. Every
 * entry documents why it is safe or what replaces it later; nothing
 * here fails silently at runtime beyond what is noted.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform/platform.h"

/* BSD-isms used by a handful of decomp files. */
void bcopy(const void* src, void* dst, size_t n) {
    memmove(dst, src, n);
}

void bzero(void* dst, size_t n) {
    memset(dst, 0, n);
}

/* RSP task yielding: there is no RSP; the translator runs the display
 * list synchronously, so yields are meaningless. */
void osSpTaskYield(void) {
}

int osSpTaskYielded(void* task) {
    (void) task;
    return 0;
}

/* Flag the previous port's loader set while its intro movie played;
 * the menu code clears it. Never set on IE. */
int in_intro = 0;

/* Registered exit handlers cannot run: the image never exits. */
int atexit(void (*fn)(void)) {
    (void) fn;
    return 0;
}

/*
 * Segment bases live elsewhere: data_segment2's start symbol comes
 * from ie/game.ld (the native .data of textures.o + data_segment2.o
 * IS the segment, as on the previous console port), and the startup
 * logo segment is a mio0-compressed blob of startup_logo.o's data
 * assembled from build output (see the game.bin rules).
 */
