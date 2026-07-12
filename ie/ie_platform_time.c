/*
 * IE backend for the platform timing contract (platform_time_ns from
 * src/platform/platform.h) and the frame-pacing primitive the main
 * loop will use.
 *
 * Time comes from the engine's monotonic microsecond counter, a split
 * 64-bit MMIO pair; the read loops until the high word is stable
 * across the low-word read so a carry between the two reads cannot
 * produce a torn value. Frame pacing polls the VBlank bit in
 * VIDEO_STATUS, waiting for a rising edge so consecutive calls always
 * advance one frame.
 */

#include <stdint.h>

#include "platform/platform.h"

#include "ie_mmio.h"

uint64_t ie_time_usec(void) {
    uint32_t hi;
    uint32_t lo;
    uint32_t hi2;

    do {
        hi = ie_mmio_read32(IE_RTC_MONO_USEC_HI);
        lo = ie_mmio_read32(IE_RTC_MONO_USEC_LO);
        hi2 = ie_mmio_read32(IE_RTC_MONO_USEC_HI);
    } while (hi != hi2);

    return ((uint64_t) hi << 32) | lo;
}

uint64_t platform_time_ns(void) {
    return ie_time_usec() * 1000u;
}

void ie_video_wait_vblank(void) {
    if (ie_feature_available(IE_SYSINFO_FEATURE_WAIT)) {
        ie_mmio_write32(IE_WAIT_VBLANK, 1);
        return;
    }

    /* Wait out any in-progress VBlank, then catch the next rising
     * edge, so each call is one whole frame boundary. */
    while ((ie_mmio_read32(IE_VIDEO_STATUS) & IE_VIDEO_STATUS_VBLANK) != 0) {
    }
    while ((ie_mmio_read32(IE_VIDEO_STATUS) & IE_VIDEO_STATUS_VBLANK) == 0) {
    }
}
