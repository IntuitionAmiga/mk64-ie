/*
 * Stage 3 timing/frame-pacing smoke program.
 *
 * Proves platform_time_ns is monotonic and advancing, and measures the
 * VBlank frame period against the RTC so the smoke can assert a sane,
 * deterministic frame rate.
 *
 * Status block payload (common fields in ie/ie_mmio.h):
 *   +16  u32 check bitmask (see below, want 0x7)
 *   +20  u32 measured busy-loop time delta in microseconds (> 0)
 *   +24  u32 measured VBlank period in microseconds (avg of 30 frames)
 */

#include <stdint.h>

#include "platform/platform.h"

#include "ie_mmio.h"

uint64_t ie_time_usec(void);
void ie_video_wait_vblank(void);

#define CHECK_MONOTONIC 0x1u /* time strictly increases across work */
#define CHECK_NONZERO 0x2u   /* platform_time_ns returns a live value */
#define CHECK_VBLANK 0x4u    /* VBlank edges arrive and are timeable */

#define FRAMES_MEASURED 30u

void ie_main(void) {
    volatile uint32_t* status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;
    uint32_t checks = 0;
    uint32_t busy_delta_us = 0;
    uint32_t period_us = 0;
    uint64_t t0;
    uint64_t t1;
    volatile uint32_t spin;
    uint32_t i;

    platform_log("MK64-IE68 STAGE3 TIME smoke");

    t0 = platform_time_ns();
    for (spin = 0; spin < 200000u; spin++) {
    }
    t1 = platform_time_ns();

    if (t1 > t0) {
        checks |= CHECK_MONOTONIC;
        busy_delta_us = (uint32_t) ((t1 - t0) / 1000u);
    }
    if (t0 != 0) {
        checks |= CHECK_NONZERO;
    }

    /* Average VBlank period over a fixed frame count. */
    ie_video_wait_vblank();
    t0 = ie_time_usec();
    for (i = 0; i < FRAMES_MEASURED; i++) {
        ie_video_wait_vblank();
    }
    t1 = ie_time_usec();
    if (t1 > t0) {
        period_us = (uint32_t) ((t1 - t0) / FRAMES_MEASURED);
        if (period_us > 0) {
            checks |= CHECK_VBLANK;
        }
    }

    platform_log("time checks 0x%x busy=%uus frame=%uus", checks, busy_delta_us, period_us);

    status[4] = checks;
    status[5] = busy_delta_us;
    status[6] = period_us;
    status[1] = 0;
    status[2] = 0;
    status[0] = IE_BOOT_MAGIC;

    for (;;) {
    }
}
