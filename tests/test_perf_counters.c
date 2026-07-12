#include "test_support.h"

#include <stdint.h>

#include "ie/ie_perf_counters.h"

static void test_accumulate_snapshot_and_reset(void) {
    uint32_t snapshot[IE_PERF_COUNTER_COUNT + 2];

    ie_perf_reset();
    IE_PERF_INC(IE_PERF_TEXRECTS);
    IE_PERF_ADD(IE_PERF_TRIS, 3);
    IE_PERF_ADD(IE_PERF_TRIS, 4);
    IE_PERF_ADD(IE_PERF_TEX_STREAM_BYTES, 128);

    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEXRECTS), 1);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TRIS), 7);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEX_STREAM_BYTES), 128);

    ie_perf_snapshot(snapshot, sizeof(snapshot) / sizeof(snapshot[0]));
    EXPECT_EQ_INT(snapshot[IE_PERF_TEXRECTS], 1);
    EXPECT_EQ_INT(snapshot[IE_PERF_TRIS], 7);
    EXPECT_EQ_INT(snapshot[IE_PERF_TEX_STREAM_BYTES], 128);
    EXPECT_EQ_INT(snapshot[IE_PERF_COUNTER_COUNT], 0);
    EXPECT_EQ_INT(snapshot[IE_PERF_COUNTER_COUNT + 1], 0);

    ie_perf_reset();
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEXRECTS), 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TRIS), 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEX_STREAM_BYTES), 0);
}

int main(void) {
    test_accumulate_snapshot_and_reset();
    return test_finish("test_perf_counters");
}
