#ifndef IE_PERF_COUNTERS_H
#define IE_PERF_COUNTERS_H

#include <stddef.h>
#include <stdint.h>

#include "ie_mmio.h"

#define IE_PERF_DUMP_MAGIC 0x50455246u /* 'PERF' */
#define IE_PERF_DUMP_ADDR (IE_BOOT_STATUS_ADDR + 0x100u)
#define IE_PERF_DUMP_MAGIC_WORD 0u
#define IE_PERF_DUMP_FRAME_WORD 1u
#define IE_PERF_DUMP_COUNTER_WORD 2u

enum IEPerfCounter {
    IE_PERF_TEXRECTS = 0,
    IE_PERF_TRIS,
    IE_PERF_TEX_IMPORTS,
    IE_PERF_TEX_STREAM_BYTES,
    IE_PERF_DRAW_CALLS,
    IE_PERF_MMIO_WRITES,
    IE_PERF_CMD_SUBMITS,
    IE_PERF_CMD_PAIRS,
    IE_PERF_CLIPPED_TRIS,
    IE_PERF_AUDIO_VOICE_WRITES,
    IE_PERF_VTX_MTX_REUSE,
    IE_PERF_MEMCPY_0_63,
    IE_PERF_MEMCPY_64_255,
    IE_PERF_MEMCPY_256_1023,
    IE_PERF_MEMCPY_1024_PLUS,
    IE_PERF_COUNTER_COUNT
};

#ifdef IE_PERF_COUNTERS
void ie_perf_reset(void);
void ie_perf_inc(enum IEPerfCounter id);
void ie_perf_add(enum IEPerfCounter id, uint32_t value);
uint32_t ie_perf_get(enum IEPerfCounter id);
void ie_perf_snapshot(uint32_t* dst, size_t words);
void ie_perf_frame_end(void);

#define IE_PERF_INC(id_) ie_perf_inc(id_)
#define IE_PERF_ADD(id_, value_) ie_perf_add((id_), (uint32_t) (value_))
#else
static inline void ie_perf_reset(void) {
}

static inline void ie_perf_inc(enum IEPerfCounter id) {
    (void) id;
}

static inline void ie_perf_add(enum IEPerfCounter id, uint32_t value) {
    (void) id;
    (void) value;
}

static inline uint32_t ie_perf_get(enum IEPerfCounter id) {
    (void) id;
    return 0;
}

static inline void ie_perf_snapshot(uint32_t* dst, size_t words) {
    size_t i;

    for (i = 0; i < words; i++) {
        dst[i] = 0;
    }
}

static inline void ie_perf_frame_end(void) {
}

#define IE_PERF_INC(id_) ((void) 0)
#define IE_PERF_ADD(id_, value_) ((void) 0)
#endif

#endif /* IE_PERF_COUNTERS_H */
