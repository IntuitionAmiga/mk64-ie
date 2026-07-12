#ifndef IE_MEMORY_LAYOUT_H
#define IE_MEMORY_LAYOUT_H

#include <stdint.h>

#include "gfx/gfx_rendering_api.h"
#include "ie/ie_pack.h"

#define IE_GAME_BASE             0x10000000u

#define IE_PACK_DATA_MEM_BASE    (IE_LOAD_BASE + IE_PACK_DATA_FOFF)
#define IE_PACK_DATA_MEM_LIMIT   0x08000000u

/* +8: the backend's private slots (baked-combiner cache pool), matching
 * TEX_PRIVATE_SLOTS in ie_gfx_voodoo.c. */
#define IE_TEX_STORE_BASE        0x08000000u
#define IE_TEX_STORE_BYTES       ((GFX_TEXTURE_CACHE_SIZE + 8u) * GFX_MAX_TEXTURE_BYTES)
#define IE_TEX_STORE_LIMIT       (IE_TEX_STORE_BASE + IE_TEX_STORE_BYTES)

#if IE_PACK_DATA_MEM_BASE >= IE_PACK_DATA_MEM_LIMIT
#error "packed data window is empty"
#endif

#if IE_PACK_DATA_MEM_LIMIT > IE_TEX_STORE_BASE
#error "packed data window overlaps Voodoo texture store"
#endif

#if IE_TEX_STORE_LIMIT > IE_GAME_BASE
#error "Voodoo texture store overlaps game image/heap window"
#endif

static inline uint8_t ie_ranges_overlap(uint32_t a0, uint32_t a1,
                                        uint32_t b0, uint32_t b1) {
    return a0 < b1 && b0 < a1;
}

#endif /* IE_MEMORY_LAYOUT_H */
