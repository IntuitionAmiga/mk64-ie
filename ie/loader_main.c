/*
 * Boot loader image for the full game.
 *
 * The engine always loads .ie68 images at 0x1000 and the low RAM
 * window ends at the first device aperture (0xA0000), far too small
 * for the game. This image boots at 0x1000, pulls the high-linked
 * game binary (ie/game.ld, GAME_BASE 0x10000000) into place through
 * the FILE MMIO device, and jumps to its first byte.
 *
 * The game binary's path is relative to the engine's -file-root, which
 * the game runs with the repository root (assets live under
 * rom_assets/, saves under build/ie/).
 */

#include <stddef.h>
#include <stdint.h>

#include "platform/platform.h"

#include "ie_pack.h"

#define GAME_BASE 0x10000000u
#define GAME_MAX_BYTES 0x04000000u /* 64MB cap; fatal if larger */
#ifndef IE_GAME_BIN_NAME
#define IE_GAME_BIN_NAME "build/ie/game.bin"
#endif
#define GAME_BIN_NAME IE_GAME_BIN_NAME

/* Self-contained pack boot: the whole mariokart64.ie68 is already in RAM
 * (see ie_pack.h), so the game image just needs copying to its link base.
 * Returns the image size, or 0 when this boot is not pack-backed. */
static size_t pack_load_game(void) {
    const ie_pack_header* hdr = (const ie_pack_header*) (uintptr_t) IE_PACK_HDR_ADDR;
    const uint32_t* src;
    uint32_t* dst = (uint32_t*) (uintptr_t) GAME_BASE;
    uint32_t words;
    uint32_t i;

    if (hdr->magic0 != IE_PACK_MAGIC0 || hdr->magic1 != IE_PACK_MAGIC1) {
        return 0;
    }
    if (hdr->game_size == 0 || hdr->game_size > GAME_MAX_BYTES) {
        platform_fatal("loader: pack game image size out of range");
    }
    src = (const uint32_t*) (uintptr_t) (IE_LOAD_BASE + hdr->game_offset);
    words = (hdr->game_size + 3u) / 4u;
    for (i = 0; i < words; i++) {
        dst[i] = src[i];
    }
    return hdr->game_size;
}

void ie_main(void) {
    size_t loaded;
    void (*entry)(void) = (void (*)(void))(uintptr_t) GAME_BASE;

    loaded = pack_load_game();
    if (loaded != 0) {
        platform_log("MK64-IE68 loader: pack image, %u bytes to 0x%x, jumping",
                     (unsigned) loaded, GAME_BASE);
        entry();
        platform_fatal("loader: game entry returned");
    }

    platform_log("MK64-IE68 loader: reading " GAME_BIN_NAME);

    if (!platform_asset_read(GAME_BIN_NAME, (void*) (uintptr_t) GAME_BASE, GAME_MAX_BYTES,
                             &loaded)) {
        platform_fatal("loader: failed to read " GAME_BIN_NAME);
    }
    platform_log("loader: %u bytes at 0x%x, jumping", (unsigned) loaded, GAME_BASE);

    entry();
    platform_fatal("loader: game entry returned");
}
