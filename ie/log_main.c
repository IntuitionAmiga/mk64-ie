/*
 * Stage 3 terminal/log smoke program.
 *
 * Exercises the platform logging backend on the engine: emits
 * formatted platform_log lines, publishes the golden formatter
 * length/byte-sum pair (shared with tests/test_ie_fmt.c
 * test_target_golden) in the status block, and finishes through
 * platform_fatal so the smoke can assert the fatal path.
 *
 * Status block payload (see ie/ie_mmio.h for the common fields):
 *   +16  u32 golden formatted length (expect 24)
 *   +20  u32 golden byte sum (expect 1647)
 */

#include <stdint.h>

#include "platform/platform.h"

#include "ie_fmt.h"
#include "ie_mmio.h"

void ie_main(void) {
    volatile uint32_t* status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;
    char buf[128];
    uint32_t sum = 0;
    int n;
    int i;

    platform_log("MK64-IE68 STAGE3 LOG up (%s build, %d)", "soft-float", 2);

    n = ie_format(buf, sizeof(buf), "log %s %d 0x%08x %c%%", "ok", -42, 0xBEEFu, '!');
    for (i = 0; i < n; i++) {
        sum += (uint8_t) buf[i];
    }
    platform_log("golden: \"%s\" len=%d sum=%u", buf, n, sum);

    status[4] = (uint32_t) n;
    status[5] = sum;
    status[1] = 0;
    status[2] = 0;
    status[0] = IE_BOOT_MAGIC;

    platform_fatal("expected fatal exit %d", 42);
}
