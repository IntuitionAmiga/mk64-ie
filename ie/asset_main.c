/*
 * Stage 3 file/asset backend smoke program.
 *
 * Runs against the Stage 1.5 generated layout (engine launched with
 * -file-root rom_assets) and proves assets arrive byte-identical:
 * whole reads, ranged reads with offsets, end-of-asset short reads,
 * and the explicit failure paths (missing asset, asset larger than
 * the caller's buffer).
 *
 * The golden numbers below are properties of the accepted source ROM
 * (sha1 579c48e2...), computed from the generated layout; the smoke
 * script checks the published words against the same values.
 *
 * Status block payload (common fields in ie/ie_mmio.h):
 *   +16  u32 result bitmask (one bit per sub-check, want 0x1FF)
 *   +20  u32 instrument_sets.bin byte sum (want 35666)
 *   +24  u32 ranged mario vertex_count (want 5757)
 *   +28  u32 ranged mario packed_offset (want 0x96F4)
 */

#include <stdint.h>

#include "platform/platform.h"

#include "ie_mmio.h"

#define CHECK_WHOLE_READ 0x01u   /* instrument_sets.bin read, size 256 */
#define CHECK_WHOLE_SUM 0x02u    /* its byte sum matches the layout */
#define CHECK_META_MAGIC 0x04u   /* course_metadata.bin starts "MK64META" */
#define CHECK_RANGE_READ 0x08u   /* ranged read at offset 16 succeeds */
#define CHECK_RANGE_VALUES 0x10u /* mario record values match the ROM */
#define CHECK_SHORT_READ 0x20u   /* read past EOF returns the short tail */
#define CHECK_MISSING 0x40u      /* missing asset fails, does not fatal */
#define CHECK_OVERSIZE 0x80u     /* asset bigger than buffer is refused */
#define CHECK_GEO_RANGE 0x100u   /* staged range deep inside a real course asset */

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

void ie_main(void) {
    volatile uint32_t* status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;
    static uint8_t buf[512];
    uint32_t results = 0;
    uint32_t sum = 0;
    uint32_t vertex_count = 0;
    uint32_t packed_offset = 0;
    size_t got = 0;
    size_t i;

    platform_log("MK64-IE68 STAGE3 ASSET smoke");

    /* Whole-file read, byte-exact. */
    if (platform_asset_read("instrument_sets.bin", buf, sizeof(buf), &got) && got == 256) {
        results |= CHECK_WHOLE_READ;
        for (i = 0; i < got; i++) {
            sum += buf[i];
        }
        if (sum == 35666u && be32(buf) == 0x003C003Eu) {
            results |= CHECK_WHOLE_SUM;
        }
    }

    /* Big-endian content lands unmodified: "MK64META" byte-for-byte. */
    if (platform_asset_read("course_metadata.bin", buf, sizeof(buf), &got) && got == 336) {
        if (buf[0] == 'M' && buf[1] == 'K' && buf[2] == '6' && buf[3] == '4' &&
            buf[4] == 'M' && buf[5] == 'E' && buf[6] == 'T' && buf[7] == 'A') {
            results |= CHECK_META_MAGIC;
        }
    }

    /* Ranged read: first course record at offset 16. */
    if (platform_asset_read_range("course_metadata.bin", 16, buf, 16, &got) && got == 16) {
        results |= CHECK_RANGE_READ;
        vertex_count = be32(buf);
        packed_offset = be32(buf + 4);
        if (vertex_count == 5757u && packed_offset == 0x96F4u && be32(buf + 8) == 26928u) {
            results |= CHECK_RANGE_VALUES;
        }
    }

    /* Short read at end-of-asset (336-byte file, ask past the end). */
    if (platform_asset_read_range("course_metadata.bin", 332, buf, 16, &got) && got == 4) {
        results |= CHECK_SHORT_READ;
    }

    /* Explicit failures: missing asset, and an asset larger than the
     * destination buffer (must refuse, not truncate). */
    if (!platform_asset_read("no_such_asset.bin", buf, sizeof(buf), &got)) {
        results |= CHECK_MISSING;
    }
    if (!platform_asset_read("course_metadata.bin", buf, 64, &got)) {
        results |= CHECK_OVERSIZE;
    }

    /* Staged range deep inside a real course asset (47,872 bytes):
     * the first packed display-list bytes of Mario Raceway, straight
     * from the ROM course table's packed_offset. */
    if (platform_asset_read_range("mario_raceway_geography.bin", 0x96F4, buf, 8, &got) &&
        got == 8 && buf[0] == 0x26 && buf[1] == 0x1A && buf[2] == 0x50 && buf[3] == 0x50 &&
        buf[4] == 0x20 && buf[5] == 0x03 && buf[6] == 0x00 && buf[7] == 0x70) {
        results |= CHECK_GEO_RANGE;
    }

    platform_log("asset checks 0x%03x sum=%u vc=%u po=0x%x", results, sum, vertex_count,
                 packed_offset);

    status[4] = results;
    status[5] = sum;
    status[6] = vertex_count;
    status[7] = packed_offset;
    status[1] = 0;
    status[2] = 0;
    status[0] = IE_BOOT_MAGIC;

    for (;;) {
    }
}
