#include "test_support.h"

#include <stdint.h>

#include "ie/ie_perf_counters.h"

static void test_disabled_build_is_noop(void) {
    uint32_t snapshot[IE_PERF_COUNTER_COUNT];

    IE_PERF_INC(IE_PERF_TEXRECTS);
    IE_PERF_ADD(IE_PERF_TRIS, 99);
    ie_perf_snapshot(snapshot, sizeof(snapshot) / sizeof(snapshot[0]));

    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEXRECTS), 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TRIS), 0);
    EXPECT_EQ_INT(snapshot[IE_PERF_TEXRECTS], 0);
    EXPECT_EQ_INT(snapshot[IE_PERF_TRIS], 0);
}

int main(void) {
    test_disabled_build_is_noop();
    return test_finish("test_perf_counters_disabled");
}
