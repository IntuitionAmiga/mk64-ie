#include "ie_perf_counters.h"

#ifdef IE_PERF_COUNTERS
static uint32_t counters[IE_PERF_COUNTER_COUNT];
static uint32_t frame_seq;

void ie_perf_reset(void) {
    size_t i;

    for (i = 0; i < IE_PERF_COUNTER_COUNT; i++) {
        counters[i] = 0;
    }
}

void ie_perf_inc(enum IEPerfCounter id) {
    if ((unsigned) id < IE_PERF_COUNTER_COUNT) {
        counters[id]++;
    }
}

void ie_perf_add(enum IEPerfCounter id, uint32_t value) {
    if ((unsigned) id < IE_PERF_COUNTER_COUNT) {
        counters[id] += value;
    }
}

uint32_t ie_perf_get(enum IEPerfCounter id) {
    if ((unsigned) id >= IE_PERF_COUNTER_COUNT) {
        return 0;
    }
    return counters[id];
}

void ie_perf_snapshot(uint32_t* dst, size_t words) {
    size_t i;

    for (i = 0; i < words && i < IE_PERF_COUNTER_COUNT; i++) {
        dst[i] = counters[i];
    }
    for (; i < words; i++) {
        dst[i] = 0;
    }
}

void ie_perf_frame_end(void) {
    volatile uint32_t* dump = (volatile uint32_t*) (uintptr_t) IE_PERF_DUMP_ADDR;
    size_t i;

    dump[IE_PERF_DUMP_MAGIC_WORD] = IE_PERF_DUMP_MAGIC;
    dump[IE_PERF_DUMP_FRAME_WORD] = ++frame_seq;
    for (i = 0; i < IE_PERF_COUNTER_COUNT; i++) {
        dump[IE_PERF_DUMP_COUNTER_WORD + i] = counters[i];
        counters[i] = 0;
    }
}
#endif
