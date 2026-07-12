/*
 * IE backend for the platform asset contract (platform_asset_read,
 * platform_asset_read_range from src/platform/platform.h), on the
 * engine's synchronous File I/O device. The engine is launched with
 * -file-root pointing at the Stage 1.5 generated layout (rom_assets/),
 * so asset ids are served directly as file names.
 *
 * The device has no offset register, so ranged reads stage the whole
 * asset in a fixed RAM window and copy the requested span out of it;
 * the staged file is remembered, which makes the two consecutive
 * ranged reads load_course performs on <course>_geography.bin cost one
 * device transfer.
 *
 * platform_asset_read uses the device's READ_MAX cap, so an asset
 * larger than the caller's buffer is refused whole by the engine
 * before any bytes move - an explicit failure, never a truncation.
 */

#include <stdint.h>

#include "platform/platform.h"

#include "ie_mmio.h"
#include "ie_pack.h"

/* Self-contained pack: when booted from mariokart64.ie68 the asset blobs are
 * already in RAM (see ie_pack.h) and the TOC survives into runtime. A hit is
 * served in place, bypassing the File I/O device entirely; a miss (or a
 * non-pack boot) falls back to the device. Names are matched RAW (as the game
 * passes them), so the pack works under any -file-root or none at all. */
static const unsigned char* pack_find(const char* name, uint32_t* size_out) {
    const ie_pack_header* hdr = (const ie_pack_header*) (uintptr_t) IE_PACK_HDR_ADDR;
    const ie_pack_entry* e;
    uint32_t i;

    if (hdr->magic0 != IE_PACK_MAGIC0 || hdr->magic1 != IE_PACK_MAGIC1) {
        return NULL;
    }
    e = (const ie_pack_entry*) (const void*) (hdr + 1);
    for (i = 0; i < hdr->toc_count; i++, e++) {
        const char* a = e->name;
        const char* b = name;
        while (*a != '\0' && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            *size_out = e->size;
            return (const unsigned char*) (uintptr_t) (IE_LOAD_BASE + e->offset);
        }
    }
    return NULL;
}

/*
 * Prefix applied to asset names before they reach the FILE device.
 * Subsystem smokes run with -file-root rom_assets (empty prefix); the
 * full game runs with -file-root at the repository root so saves can
 * live outside the asset tree, and its build defines
 * IE_ASSET_PREFIX="rom_assets/".
 */
#ifndef IE_ASSET_PREFIX
#define IE_ASSET_PREFIX ""
#endif

static const char* prefixed_name(const char* name) {
    static char buf[128];
    const char* p = IE_ASSET_PREFIX;
    uint32_t i = 0;

    if (p[0] == '\0') {
        return name;
    }
    while (*p != '\0' && i < sizeof(buf) - 1) {
        buf[i++] = *p++;
    }
    while (*name != '\0' && i < sizeof(buf) - 1) {
        buf[i++] = *name++;
    }
    buf[i] = '\0';
    if (*name != '\0') {
        platform_fatal("asset name too long for prefix buffer");
    }
    return buf;
}

static uint32_t file_read_into(const char* name, uint32_t dst_addr, uint32_t read_max,
                               uint32_t* result_len) {
    ie_mmio_write32(IE_FILE_NAME_PTR, (uint32_t) (uintptr_t) prefixed_name(name));
    ie_mmio_write32(IE_FILE_DATA_PTR, dst_addr);
    ie_mmio_write32(IE_FILE_DATA_LEN, 0);
    ie_mmio_write32(IE_FILE_READ_MAX, read_max);
    ie_mmio_write32(IE_FILE_CTRL, IE_FILE_OP_READ);

    if (ie_mmio_read32(IE_FILE_STATUS) != IE_FILE_STATUS_OK) {
        return ie_mmio_read32(IE_FILE_ERROR_CODE);
    }
    *result_len = ie_mmio_read32(IE_FILE_RESULT_LEN);
    return IE_FILE_ERR_OK;
}

/* Optional service hook (the game wires the audio poll here so long
 * asset loads keep the ring fed; smoke images leave it NULL). */
static void (*asset_io_hook)(void);

void ie_asset_set_io_hook(void (*hook)(void)) {
    asset_io_hook = hook;
}

static void asset_service(void) {
    ((volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR)[8]++;
    if (asset_io_hook != NULL) {
        asset_io_hook();
    }
}

bool platform_asset_read(const char* name, void* dst, size_t max_size, size_t* size_out) {
    uint32_t got = 0;
    uint32_t err;
    const unsigned char* blob;
    uint32_t blob_len;

    asset_service();

    if (name == NULL || dst == NULL || max_size == 0) {
        return false;
    }

    blob = pack_find(name, &blob_len);
    if (blob != NULL) {
        uint8_t* out = (uint8_t*) dst;
        uint32_t i;
        if (blob_len > max_size) {
            /* Match the device semantics: larger-than-buffer is an
             * explicit whole-read refusal, never a truncation. */
            platform_log("asset read '%s' exceeds buffer (%u > %u)", name,
                         (unsigned) blob_len, (unsigned) max_size);
            return false;
        }
        for (i = 0; i < blob_len; i++) {
            out[i] = blob[i];
        }
        if (size_out != NULL) {
            *size_out = blob_len;
        }
        return true;
    }

    err = file_read_into(name, (uint32_t) (uintptr_t) dst, (uint32_t) max_size, &got);
    if (err != IE_FILE_ERR_OK) {
        platform_log("asset read '%s' failed (file error %u)", name, err);
        return false;
    }
    if (size_out != NULL) {
        *size_out = got;
    }
    return true;
}

/* Name of the asset currently staged at IE_ASSET_STAGING_BASE. */
static char staged_name[64];
static uint32_t staged_len;
static int staged_valid;

static int name_equals_staged(const char* name) {
    const char* a = staged_name;

    if (!staged_valid) {
        return 0;
    }
    while (*a != '\0' && *a == *name) {
        a++;
        name++;
    }
    return *a == '\0' && *name == '\0';
}

static int stage_asset(const char* name) {
    uint32_t err;
    uint32_t i;

    if (name_equals_staged(name)) {
        return 1;
    }

    staged_valid = 0;
    err = file_read_into(name, IE_ASSET_STAGING_BASE, IE_ASSET_STAGING_SIZE, &staged_len);
    if (err != IE_FILE_ERR_OK) {
        platform_log("asset stage '%s' failed (file error %u)", name, err);
        return 0;
    }

    for (i = 0; i < sizeof(staged_name); i++) {
        staged_name[i] = name[i];
        if (name[i] == '\0') {
            break;
        }
    }
    if (i == sizeof(staged_name)) {
        /* Name (with terminator) does not fit the cache; the read
         * still succeeded, it just will not be reused. */
        staged_name[0] = '\0';
        staged_valid = 0;
    } else {
        staged_valid = 1;
    }
    return 1;
}

bool platform_asset_read_range(const char* name, size_t offset, void* dst, size_t nbytes,
                               size_t* bytes_read) {
    asset_service();

    const uint8_t* staging = (const uint8_t*) (uintptr_t) IE_ASSET_STAGING_BASE;
    uint8_t* out = (uint8_t*) dst;
    size_t avail;
    size_t i;
    const unsigned char* blob;
    uint32_t blob_len;

    if (name == NULL || dst == NULL) {
        return false;
    }

    blob = pack_find(name, &blob_len);
    if (blob != NULL) {
        /* Pack blobs are served in place: ranged reads copy straight
         * from RAM, no staging window needed. */
        if (offset >= blob_len) {
            avail = 0;
        } else {
            avail = blob_len - offset;
            if (avail > nbytes) {
                avail = nbytes;
            }
        }
        for (i = 0; i < avail; i++) {
            out[i] = blob[offset + i];
        }
        if (bytes_read != NULL) {
            *bytes_read = avail;
        }
        return true;
    }
    if (!stage_asset(name)) {
        return false;
    }

    if (offset >= staged_len) {
        avail = 0;
    } else {
        avail = staged_len - offset;
        if (avail > nbytes) {
            avail = nbytes;
        }
    }

    for (i = 0; i < avail; i++) {
        out[i] = staging[offset + i];
    }
    if (bytes_read != NULL) {
        *bytes_read = avail;
    }
    return true;
}
