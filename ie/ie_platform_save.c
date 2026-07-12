/*
 * IE backend for the platform save and ghost storage contracts
 * (platform_save_* / platform_ghost_* from src/platform/platform.h),
 * on the engine's File I/O device.
 *
 * The device reads and writes whole files only, so each record keeps a
 * RAM shadow: reads are served from the shadow (loaded lazily from the
 * backing file) and writes update the shadow then persist the whole
 * record. The EEPROM record is 512 bytes (the 4-kbit part the game
 * probes for); the ghost record is the 32 KiB controller-pak
 * replacement blob. The device has no delete operation, so
 * platform_ghost_delete truncates the backing file to zero length,
 * which subsequent loads treat as absent.
 */

#include <stdint.h>

#include "platform/platform.h"

#include "ie_mmio.h"

#define IE_EEPROM_SIZE 512u
#define IE_GHOST_SIZE 32768u

/*
 * Save file locations relative to the engine's -file-root. Subsystem
 * smokes run with a scratch root (empty prefix); the full game runs
 * rooted at the repository and defines IE_SAVE_PREFIX="build/ie/" so
 * saves never land in the asset tree (rom_assets/ hygiene gate).
 */
#ifndef IE_SAVE_PREFIX
#define IE_SAVE_PREFIX ""
#endif

#include "ie_pack.h"

/* Pack boots root the guest filesystem at the image's own directory,
 * so saves live beside the .ie68 (bare names); file boots keep the
 * build-time prefix (repository-rooted layout). */
static const char* save_path(const char* prefixed, const char* bare) {
    const ie_pack_header* hdr = (const ie_pack_header*) (uintptr_t) IE_PACK_HDR_ADDR;
    if (hdr->magic0 == IE_PACK_MAGIC0 && hdr->magic1 == IE_PACK_MAGIC1) {
        return bare;
    }
    return prefixed;
}

#define IE_EEPROM_FILE save_path(IE_SAVE_PREFIX "eeprom.sav", "eeprom.sav")
#define IE_GHOST_FILE save_path(IE_SAVE_PREFIX "ghost.sav", "ghost.sav")

static uint8_t eeprom_shadow[IE_EEPROM_SIZE];
static uint8_t ghost_shadow[IE_GHOST_SIZE];
static uint8_t eeprom_loaded;
static uint8_t ghost_loaded;
static uint8_t ghost_present_flag;
static uint8_t ghost_store_blocked;

static uint32_t file_read_whole(const char* name, void* dst, uint32_t cap, uint32_t* got) {
    ie_mmio_write32(IE_FILE_NAME_PTR, (uint32_t) (uintptr_t) name);
    ie_mmio_write32(IE_FILE_DATA_PTR, (uint32_t) (uintptr_t) dst);
    ie_mmio_write32(IE_FILE_DATA_LEN, 0);
    ie_mmio_write32(IE_FILE_READ_MAX, cap);
    ie_mmio_write32(IE_FILE_CTRL, IE_FILE_OP_READ);

    if (ie_mmio_read32(IE_FILE_STATUS) != IE_FILE_STATUS_OK) {
        return ie_mmio_read32(IE_FILE_ERROR_CODE);
    }
    *got = ie_mmio_read32(IE_FILE_RESULT_LEN);
    return IE_FILE_ERR_OK;
}

static int file_write_whole(const char* name, const void* src, uint32_t len) {
    ie_mmio_write32(IE_FILE_NAME_PTR, (uint32_t) (uintptr_t) name);
    ie_mmio_write32(IE_FILE_DATA_PTR, (uint32_t) (uintptr_t) src);
    ie_mmio_write32(IE_FILE_DATA_LEN, len);
    ie_mmio_write32(IE_FILE_CTRL, IE_FILE_OP_WRITE);

    if (ie_mmio_read32(IE_FILE_STATUS) != IE_FILE_STATUS_OK) {
        platform_log("save write '%s' failed (file error %u)", name,
                     ie_mmio_read32(IE_FILE_ERROR_CODE));
        return 0;
    }
    return 1;
}

/* ---- EEPROM-style save record --------------------------------------------- */

static int eeprom_ensure_loaded(void) {
    uint32_t got = 0;
    uint32_t err;
    uint32_t i;

    if (eeprom_loaded) {
        return 1;
    }
    for (i = 0; i < IE_EEPROM_SIZE; i++) {
        eeprom_shadow[i] = 0;
    }
    err = file_read_whole(IE_EEPROM_FILE, eeprom_shadow, IE_EEPROM_SIZE, &got);
    if (err != IE_FILE_ERR_OK && err != IE_FILE_ERR_NOT_FOUND) {
        /* Missing file is a fresh save; anything else is a real
         * storage failure. */
        return 0;
    }
    eeprom_loaded = 1;
    return 1;
}

bool platform_save_probe(void) {
    return eeprom_ensure_loaded();
}

bool platform_save_read(size_t offset, void* dst, size_t nbytes) {
    uint8_t* out = (uint8_t*) dst;
    size_t i;

    if (dst == NULL || offset > IE_EEPROM_SIZE || nbytes > IE_EEPROM_SIZE - offset ||
        !eeprom_ensure_loaded()) {
        return false;
    }
    for (i = 0; i < nbytes; i++) {
        out[i] = eeprom_shadow[offset + i];
    }
    return true;
}

bool platform_save_write(size_t offset, const void* src, size_t nbytes) {
    const uint8_t* in = (const uint8_t*) src;
    size_t i;

    if (src == NULL || offset > IE_EEPROM_SIZE || nbytes > IE_EEPROM_SIZE - offset ||
        !eeprom_ensure_loaded()) {
        return false;
    }
    for (i = 0; i < nbytes; i++) {
        eeprom_shadow[offset + i] = in[i];
    }
    return file_write_whole(IE_EEPROM_FILE, eeprom_shadow, IE_EEPROM_SIZE) != 0;
}

/* ---- Ghost record (controller-pak replacement) ----------------------------- */

static void ghost_ensure_loaded(void) {
    uint32_t got = 0;
    uint32_t err;
    uint32_t i;

    if (ghost_loaded) {
        return;
    }
    for (i = 0; i < IE_GHOST_SIZE; i++) {
        ghost_shadow[i] = 0;
    }
    err = file_read_whole(IE_GHOST_FILE, ghost_shadow, IE_GHOST_SIZE, &got);
    /* A zero-length file is a deleted ghost; a missing file has never
     * had one. Both mean "no ghost", but storage itself is fine. Any
     * other error (oversized/corrupt file, I/O failure) fails closed:
     * the ghost stays "not loaded" so a later store cannot clobber
     * the backing file with the zeroed shadow, matching the EEPROM
     * path's behaviour. */
    if (err != IE_FILE_ERR_OK && err != IE_FILE_ERR_NOT_FOUND) {
        platform_log("ghost read failed (err %u); ghost storage disabled", (unsigned) err);
        ghost_present_flag = 0;
        ghost_loaded = 0;
        ghost_store_blocked = 1;
        return;
    }
    ghost_present_flag = (err == IE_FILE_ERR_OK && got > 0);
    ghost_store_blocked = 0;
    ghost_loaded = 1;
}

bool platform_ghost_present(void) {
    ghost_ensure_loaded();
    return true;
}

bool platform_ghost_load(size_t offset, void* dst, size_t nbytes) {
    uint8_t* out = (uint8_t*) dst;
    size_t i;

    ghost_ensure_loaded();
    if (dst == NULL || !ghost_present_flag || offset > IE_GHOST_SIZE ||
        nbytes > IE_GHOST_SIZE - offset) {
        return false;
    }
    for (i = 0; i < nbytes; i++) {
        out[i] = ghost_shadow[offset + i];
    }
    return true;
}

bool platform_ghost_store(size_t offset, const void* src, size_t nbytes) {
    const uint8_t* in = (const uint8_t*) src;
    size_t i;

    ghost_ensure_loaded();
    if (ghost_store_blocked || src == NULL || offset > IE_GHOST_SIZE
        || nbytes > IE_GHOST_SIZE - offset) {
        return false;
    }
    for (i = 0; i < nbytes; i++) {
        ghost_shadow[offset + i] = in[i];
    }
    if (!file_write_whole(IE_GHOST_FILE, ghost_shadow, IE_GHOST_SIZE)) {
        return false;
    }
    ghost_present_flag = 1;
    return true;
}

bool platform_ghost_delete(void) {
    uint32_t i;

    if (!file_write_whole(IE_GHOST_FILE, ghost_shadow, 0)) {
        return false;
    }
    for (i = 0; i < IE_GHOST_SIZE; i++) {
        ghost_shadow[i] = 0;
    }
    ghost_present_flag = 0;
    ghost_loaded = 1;
    ghost_store_blocked = 0; /* explicit delete establishes fresh state */
    return true;
}
