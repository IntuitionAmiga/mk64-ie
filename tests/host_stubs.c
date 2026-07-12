/*
 * Minimal host definitions for globals referenced by the Stage 1 test
 * scope. These exist only so focused test binaries can link; they are not
 * a platform backend.
 */
#include <stddef.h>
#include <string.h>

#include <ultra64.h>
#include <mk64.h>
#include <main.h>

struct GfxPool* gGfxPool;
Gfx* gDisplayListHead;
u16 gMatrixObjectCount;

void* segmented_to_virtual(const void* addr) {
    return (void*) addr;
}

/* Byte-wise copy is sufficient for host tests. */
void n64_memcpy(void* dst, const void* src, size_t size) {
    memcpy(dst, src, size);
}
