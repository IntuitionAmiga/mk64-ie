#ifndef ASSET_ENDIAN_H
#define ASSET_ENDIAN_H

/*
 * Byte-order accessors for generated asset data.
 *
 * The neutral asset layout produced by tools/rom_assets.py preserves the
 * source ROM's byte order: big-endian. Code that reads multi-byte fields
 * out of asset bytes must go through these accessors instead of casting
 * to wider types, so it is correct on both little-endian hosts (tests)
 * and big-endian targets. Compilers reduce these to plain loads where
 * the target byte order already matches.
 */

#include <stdint.h>

static inline uint16_t asset_be_u16(const void* p) {
    const uint8_t* b = (const uint8_t*) p;
    return (uint16_t)(((uint16_t) b[0] << 8) | b[1]);
}

static inline int16_t asset_be_s16(const void* p) {
    return (int16_t) asset_be_u16(p);
}

static inline uint32_t asset_be_u32(const void* p) {
    const uint8_t* b = (const uint8_t*) p;
    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16)
         | ((uint32_t) b[2] << 8) | (uint32_t) b[3];
}

/* Store in asset (big-endian) byte order, for code that rewrites asset
 * data in place (e.g. menu texture fades). */
static inline void asset_be_store_u16(void* p, uint16_t v) {
    uint8_t* b = (uint8_t*) p;
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t) v;
}

#endif /* ASSET_ENDIAN_H */
