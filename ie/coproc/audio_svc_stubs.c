/*
 * ie/coproc/audio_svc_stubs.c - link closure for the audio service image.
 *
 * external.c carries both halves of the audio seam; the producer half
 * references game-logic globals and helpers that never execute inside the
 * service (the worker only runs the pump path). Those get zero dummies and
 * no-op stubs here so the image links; the two helpers the pump path DOES
 * need (segmented_to_virtual, n64_memcpy) get real definitions.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- helpers the pump path really uses (load.c) ------------------------- */

/* Audio data pointers are raw guest addresses in this port (the segment
 * buffers are passed to the worker as virtual pointers at init); the
 * segment table never feeds the audio path, so resolution is the
 * identity mapping. */
void *segmented_to_virtual(const void *addr) {
    return (void *) (uintptr_t) addr;
}

void n64_memcpy(void *dst, const void *src, size_t size) {
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (size--) {
        *d++ = *s++;
    }
}

/* --- libultra reimpl subset the audio TUs reference ---------------------
 * Semantics copied from src/os/ultra_reimpl.c; linking that TU whole would
 * add ~33KB of save/ghost .bss the 512KB worker window cannot spare. */

typedef struct {
    int32_t validCount;
    int32_t first;
    int32_t msgCount;
    void **msg;
} svc_OSMesgQueue;

void osCreateMesgQueue(svc_OSMesgQueue *mq, void **msgBuf, int32_t count) {
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msgBuf;
}

uint64_t platform_time_ns(void);

#define N64_NS_PER_TICK 21.91f

uint64_t osGetTime(void) {
    return (uint64_t)((double) platform_time_ns() / (double) N64_NS_PER_TICK);
}

static uint32_t svc_os_counter;
static uint32_t svc_os_ticked;
uint32_t osGetCount(void) {
    svc_os_ticked++;
    svc_os_counter += 757576;
    svc_os_counter += svc_os_ticked & 1;
    return svc_os_counter;
}

void osWritebackDCacheAll(void) {}

int32_t osAiSetFrequency(uint32_t freq) {
    uint32_t dac = 0x02E6D354;
    uint32_t a1 = dac / (float) freq + .5f;

    if (a1 < 0x84) {
        return -1;
    }
    return dac / (int32_t) a1;
}

/* --- producer-half game state, never executed worker-side --------------- */

uint32_t gPlayers[0x400];     /* Player[]: zero dummy, address-only */
uint32_t camera1[0x40];
uint32_t camera2[0x40];
uint32_t camera3[0x40];
uint32_t camera4[0x40];
int32_t gCurrentCourseId;
int32_t gModeSelection;
int32_t gScreenModeSelection;
int32_t gPlayerCountSelection1;
int32_t gPlayerWinningIndex;
int32_t came_from_battle;
uint8_t D_801657E5;
uint8_t D_8018ED90[0x40];

void func_8001AAAC(int32_t player) { (void) player; }
int32_t func_800416D8(int32_t a) { (void) a; return 0; }
int32_t func_80041724(int32_t a) { (void) a; return 0; }

/* --- ultra_reimpl save/ghost hooks: audio never touches saves ----------- */

bool platform_save_probe(void) { return false; }
bool platform_save_read(size_t offset, void *dst, size_t nbytes) {
    (void) offset; (void) dst; (void) nbytes;
    return false;
}
bool platform_save_write(size_t offset, const void *src, size_t nbytes) {
    (void) offset; (void) src; (void) nbytes;
    return false;
}
bool platform_ghost_present(void) { return false; }
bool platform_ghost_delete(void) { return false; }
bool platform_ghost_load(size_t offset, void *dst, size_t nbytes) {
    (void) offset; (void) dst; (void) nbytes;
    return false;
}
bool platform_ghost_store(size_t offset, const void *src, size_t nbytes) {
    (void) offset; (void) src; (void) nbytes;
    return false;
}
