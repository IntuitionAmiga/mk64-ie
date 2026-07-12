/*
 * Freestanding heap for the IE game image: a bump allocator over the
 * region the game link script reserves after .bss (__heap_start ..
 * __heap_end). The game allocates at startup (audio segments, course
 * buffers) and never frees individually, so free() is a no-op; that is
 * a deliberate simplification and platform_fatal fires on exhaustion
 * rather than returning NULL for these startup-critical allocations.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform/platform.h"

extern uint8_t __heap_start __asm__("__heap_start");
extern uint8_t __heap_end __asm__("__heap_end");

static uintptr_t heap_next;

void* malloc(size_t size) {
    uintptr_t base;

    if (heap_next == 0) {
        heap_next = (uintptr_t) &__heap_start;
    }
    /* 16-byte alignment covers the game's alignment expectations; a
     * size word sits just below each block so realloc can copy. */
    base = ((heap_next + sizeof(size_t) + 15u) & ~(uintptr_t) 15u);
    if (size == 0) {
        size = 1;
    }
    if (base + size > (uintptr_t) &__heap_end || base + size < base) {
        platform_fatal("heap exhausted (%u bytes requested)", (unsigned) size);
    }
    ((size_t*) base)[-1] = size;
    heap_next = base + size;
    return (void*) base;
}

void* realloc(void* ptr, size_t size) {
    void* fresh;
    size_t old;

    if (ptr == NULL) {
        return malloc(size);
    }
    old = ((size_t*) ptr)[-1];
    if (size <= old) {
        return ptr;
    }
    fresh = malloc(size);
    memcpy(fresh, ptr, old);
    return fresh;
}

void free(void* ptr) {
    (void) ptr;
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    uint8_t* p;
    size_t i;

    if (size != 0 && total / size != nmemb) {
        platform_fatal("calloc overflow");
    }
    p = malloc(total);
    for (i = 0; i < total; i++) {
        p[i] = 0;
    }
    return p;
}

void exit(int status) {
    platform_fatal("exit(%d) called", status);
}
