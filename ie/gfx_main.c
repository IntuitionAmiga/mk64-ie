/*
 * Stage 3 Voodoo graphics smoke program.
 *
 * Runs the real shared Fast3D translator (src/gfx/gfx_pc.c) on the
 * engine, against the Voodoo backend. The engine's Voodoo HLE applies
 * raster state to the whole triangle batch at swap time (one state per
 * frame - recorded as an engine-side blocker for full-game rendering
 * in STAGE3-NOTES.md), so this smoke renders two single-state phases:
 * frames 0-599 a flat red shade triangle at the centre, then a
 * textured red/green checker quad on the right. The smoke script
 * verifies compositor pixels for both phases.
 *
 * Status block payload (common fields in ie/ie_mmio.h):
 *   +16  u32 frame counter (advances every rendered frame)
 */

#include <stdint.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/mbi.h>
#include <PR/gbi.h>

#include "gfx/gfx_pc.h"
#include "gfx/gfx_rendering_api.h"
#include "gfx/gfx_window_manager_api.h"

#include "platform/platform.h"

#include "ie_mmio.h"

extern struct GfxRenderingAPI ie_gfx_voodoo_api;
extern struct GfxWindowManagerAPI ie_gfx_window_api;

/* Shared-translator dependencies. */
s32 gIsMirrorMode = 0;

void* segmented_to_virtual(const void* addr) {
    return (void*) addr;
}

static const float scale_proj[4][4] = {
    { 0.01f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.01f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.01f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
};

static const float identity[4][4] = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
};

/* Full-screen N64 viewport (half-extents with 2 fraction bits). */
static const Vp_t viewport = { { 640, 480, 511, 0 }, { 640, 480, 511, 0 } };

/* Red shade triangle around the screen centre (object coords are
 * scaled by 0.01 into NDC by the projection). */
static const Vtx tri_vertices[3] = {
    { { { 0, 40, 0 }, 0, { 0, 0 }, { 0xFF, 0x00, 0x00, 0xFF } } },
    { { { -40, -40, 0 }, 0, { 0, 0 }, { 0xFF, 0x00, 0x00, 0xFF } } },
    { { { 40, -40, 0 }, 0, { 0, 0 }, { 0xFF, 0x00, 0x00, 0xFF } } },
};

/* Textured quad on the right side: NDC x 0.55..0.95, y -0.2..0.2.
 * Texture coordinates span exactly one 4-texel tile (S10.5: 4 << 5). */
static const Vtx quad_vertices[4] = {
    { { { 55, 20, 0 }, 0, { 0, 0 }, { 0xFF, 0xFF, 0xFF, 0xFF } } },
    { { { 95, 20, 0 }, 0, { 128, 0 }, { 0xFF, 0xFF, 0xFF, 0xFF } } },
    { { { 95, -20, 0 }, 0, { 128, 128 }, { 0xFF, 0xFF, 0xFF, 0xFF } } },
    { { { 55, -20, 0 }, 0, { 0, 128 }, { 0xFF, 0xFF, 0xFF, 0xFF } } },
};

/* 4x4 rgba16 texture, stored (big-endian) texel order: left half pure
 * red (0xF801), right half pure green (0x07C1). */
static const uint8_t checker_texels[32] = {
    0xF8, 0x01, 0xF8, 0x01, 0x07, 0xC1, 0x07, 0xC1,
    0xF8, 0x01, 0xF8, 0x01, 0x07, 0xC1, 0x07, 0xC1,
    0xF8, 0x01, 0xF8, 0x01, 0x07, 0xC1, 0x07, 0xC1,
    0xF8, 0x01, 0xF8, 0x01, 0x07, 0xC1, 0x07, 0xC1,
};

static Gfx dl_shade[24];
static Gfx dl_textured[32];

static Gfx* common_prologue(Gfx* g) {
    gSPViewport(g++, (uintptr_t) &viewport);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    return g;
}

static void build_display_lists(void) {
    Gfx* g;

    /* Phase 1: flat red triangle from vertex shade colour. */
    g = common_prologue(dl_shade);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    /* Phase 2: textured checker quad. */
    g = common_prologue(dl_textured);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetTextureImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (uintptr_t) checker_texels);
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, G_TX_CLAMP, 0, 0,
               G_TX_CLAMP, 0, 0);
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 4 * 4 - 1, CALC_DXT(4, G_IM_SIZ_16b_BYTES));
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 2, 0,
               G_TX_CLAMP, 2, 0);
    gDPSetTileSize(g++, G_TX_RENDERTILE, 0, 0, (4 - 1) << G_TEXTURE_IMAGE_FRAC,
                   (4 - 1) << G_TEXTURE_IMAGE_FRAC);
    gSPVertex(g++, (uintptr_t) quad_vertices, 4, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSP1Triangle(g++, 0, 2, 3, 0);
    gSPEndDisplayList(g++);
}

void ie_main(void) {
    volatile uint32_t* status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;
    uint32_t frames = 0;

    platform_log("MK64-IE68 STAGE3 GFX smoke");

    build_display_lists();
    gfx_init(&ie_gfx_window_api, &ie_gfx_voodoo_api, "mk64-ie gfx smoke", 0);

    status[1] = 0;
    status[2] = 0;
    status[4] = 0;
    status[0] = IE_BOOT_MAGIC;

    for (;;) {
        gfx_start_frame();
        gfx_run(frames < 600 ? dl_shade : dl_textured);
        gfx_end_frame();
        frames++;
        status[4] = frames;
        status[2] = frames;
    }
}
