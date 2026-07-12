/*
 * Portable Fast3D display-list translator.
 *
 * Walks the game's Gfx display lists, maintains the RDP-side state
 * (textures, tiles, combiners, colours, other-mode), and submits work
 * exclusively through the neutral GfxRenderingAPI/GfxWindowManagerAPI
 * function tables. The RSP-side math (matrix stack, vertex transform,
 * lighting, clip rejection) lives in gfx_fast3d.c.
 *
 * This rebuilds the translator the previous console port fused with its
 * renderer backend; every backend-specific path (direct GL/PVR calls,
 * platform vertex formats, per-game render workarounds that patched the
 * old backend's blending) has been removed or replaced with neutral
 * equivalents. Texture decoding converts every N64 format to RGBA32 in
 * texel order, reading texels byte-wise so the code is byte-order
 * portable. Sub-image extraction quirks for textures loaded out of
 * wider images are ported behaviour-for-behaviour from the previous
 * translator, which was debugged against this game's content.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/mbi.h>
#include <PR/gbi.h>

#include "gfx_pc.h"
#include "gfx_cc.h"
#include "gfx_rendering_api.h"
#include "gfx_window_manager_api.h"
#include "gfx_fast3d.h"

#include "ie/ie_perf_counters.h"
#include "platform/platform.h"

void* segmented_to_virtual(const void* addr);

extern s32 gIsMirrorMode;

#ifdef IE_GFX_SVC
/* M68K gfx-service client seams (ie/ie_gfx_svc_client.c). When the service
 * is online, translation/submission/pacing run on the worker and these
 * entry points forward; when COSTART failed the local path below runs
 * unchanged. The service's own compile pass (-DIE_GFX_SERVICE) never
 * defines IE_GFX_SVC, so the worker image keeps the plain local bodies. */
int ie_gfx_svc_active(void);
int ie_gfx_svc_init(void);
void ie_gfx_svc_frame(void* dl);
void ie_gfx_svc_invalidate(void* addr);
void ie_gfx_svc_reset(void);
#endif

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define HALF_SCREEN_WIDTH (SCREEN_WIDTH / 2)
#define HALF_SCREEN_HEIGHT (SCREEN_HEIGHT / 2)

#define RATIO_X (gfx_current_dimensions.width / (2.0f * HALF_SCREEN_WIDTH))
#define RATIO_Y (gfx_current_dimensions.height / (2.0f * HALF_SCREEN_HEIGHT))

#define MAX_BUFFERED 256
/* position (4) + uv (2) + fog (4) + two colour inputs (4 each) */
#define MAX_VERTEX_FLOATS 18

#define SCALE_5_8(VAL_) (((VAL_) << 3) | ((VAL_) >> 2))
#define SCALE_4_8(VAL_) ((VAL_) * 0x11)
#define SCALE_3_8(VAL_) ((VAL_) << 5)
#define BYTE_TO_UNIT_SCALE (1.0f / 255.0f)

struct RGBA {
    uint8_t r, g, b, a;
};

struct XYWidthHeight {
    uint16_t x, y, width, height;
};

struct ShaderProgram;

/* ---- texture cache -------------------------------------------------------
 *
 * Keyed on the resolved source address plus the tile parameters that
 * change how the source decodes. gfx_texture_cache_invalidate marks all
 * entries for an address dirty so the next use re-uploads.
 */

#define TEXTURE_HASHMAP_SIZE 1024

struct TextureCacheNode {
    struct TextureCacheNode* next;
    const void* addr;
    const void* palette;
    uint32_t texture_id;
    uint32_t size_bytes;
    uint32_t line_size_bytes;
    uint32_t source_width;
    uint16_t uls, ult;
    uint16_t lrs, lrt;
    uint8_t fmt, siz;
    uint8_t id_allocated;
    uint8_t dirty;
    uint8_t linear_filter;
    uint8_t cms, cmt;
};

struct TextureCacheKey {
    const void* addr;
    const void* palette;
    uint32_t size_bytes;
    uint32_t line_size_bytes;
    uint32_t source_width;
    uint16_t uls, ult, lrs, lrt;
    uint8_t fmt, siz;
};

static struct {
    struct TextureCacheNode* hashmap[TEXTURE_HASHMAP_SIZE];
    struct TextureCacheNode pool[GFX_TEXTURE_CACHE_SIZE];
    uint32_t pool_pos;
} gfx_texture_cache;

struct ColorCombiner {
    uint32_t cc_id;
    struct ShaderProgram* prg;
    uint8_t shader_input_mapping[2][4];
};

#define COLOR_COMBINER_POOL_SIZE 64

static struct ColorCombiner color_combiner_pool[COLOR_COMBINER_POOL_SIZE];
static uint8_t color_combiner_pool_size;

static struct RDP {
    const uint8_t* palette;
    struct {
        const uint8_t* addr;
        uint8_t siz;
        uint8_t tile_number;
    } texture_to_load;
    struct {
        const uint8_t* addr;
        uint32_t size_bytes;
    } loaded_texture[2];
    struct {
        uint8_t fmt;
        uint8_t siz;
        uint8_t cms, cmt;
        uint16_t uls, ult, lrs, lrt; /* U10.2 */
        uint32_t line_size_bytes;
    } texture_tile;
    uint8_t textures_changed[2];

    uint32_t other_mode_l, other_mode_h;
    uint32_t combine_mode;

    struct RGBA env_color, prim_color, fog_color, fill_color;
    struct XYWidthHeight viewport, scissor;
    uint8_t viewport_or_scissor_changed;
    void* z_buf_address;
    void* color_image_address;
} rdp;

static struct RenderingState {
    struct ShaderProgram* shader_program;
    struct TextureCacheNode* textures[2];
    uint8_t depth_test;
    uint8_t depth_mask;
    uint8_t decal_mode;
    uint8_t alpha_blend;
    struct XYWidthHeight viewport;
    struct XYWidthHeight scissor;
} rendering_state;

struct GfxDimensions gfx_current_dimensions;

/* G_MW_FOG state; the fog factor is computed per vertex at emission. */
static int16_t rsp_fog_mul;
static int16_t rsp_fog_offset;

static uint8_t dropped_frame;

static float buf_vbo[MAX_BUFFERED * 3 * MAX_VERTEX_FLOATS];
static size_t buf_vbo_len;
static size_t buf_vbo_num_tris;

static struct GfxWindowManagerAPI* gfx_wapi;
static struct GfxRenderingAPI* gfx_rapi;

/* Image width (minus one) from the last G_SETTIMG; nonzero when tiles
 * are loaded out of a wider source image. */
static uint32_t last_set_texture_image_width;

/* Decoded RGBA32 texel staging buffer and 1-byte-per-texel scratch. */
static uint8_t rgba32_buf[GFX_MAX_TEXTURE_BYTES];
static uint8_t xform_buf[GFX_MAX_TEXTURE_TEXELS];

static inline uint16_t texel_be_u16(const uint8_t* p) {
    return (uint16_t)(((uint16_t) p[0] << 8) | p[1]);
}

static inline float byte_to_unit(uint8_t v) {
    return (float) v * BYTE_TO_UNIT_SCALE;
}

static void gfx_flush(void) {
    if (buf_vbo_len > 0) {
        IE_PERF_INC(IE_PERF_DRAW_CALLS);
        IE_PERF_ADD(IE_PERF_TRIS, buf_vbo_num_tris);
        gfx_rapi->draw_triangles(buf_vbo, buf_vbo_len, buf_vbo_num_tris);
        buf_vbo_len = 0;
        buf_vbo_num_tris = 0;
    }
}

static struct ShaderProgram* gfx_lookup_or_create_shader_program(uint32_t shader_id) {
    struct ShaderProgram* prg = gfx_rapi->lookup_shader(shader_id);
    if (prg == NULL) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        prg = gfx_rapi->create_and_load_new_shader(shader_id);
        rendering_state.shader_program = prg;
    }
    return prg;
}

static void gfx_generate_cc(struct ColorCombiner* comb, uint32_t cc_id) {
    uint8_t c[2][4];
    uint32_t shader_id = (cc_id >> 24) << 24;
    uint8_t shader_input_mapping[2][4] = { { 0 } };
    int i, j;

    for (i = 0; i < 4; i++) {
        c[0][i] = (cc_id >> (i * 3)) & 7;
        c[1][i] = (cc_id >> (12 + i * 3)) & 7;
    }
    for (i = 0; i < 2; i++) {
        uint8_t input_number[8] = { 0 };
        int next_input_number = SHADER_INPUT_1;

        if (c[i][0] == c[i][1] || c[i][2] == CC_0) {
            c[i][0] = c[i][1] = c[i][2] = 0;
        }
        for (j = 0; j < 4; j++) {
            int val = 0;
            switch (c[i][j]) {
                case CC_0:
                    break;
                case CC_TEXEL0:
                    val = SHADER_TEXEL0;
                    break;
                case CC_TEXEL1:
                    val = SHADER_TEXEL1;
                    break;
                case CC_TEXEL0A:
                    val = SHADER_TEXEL0A;
                    break;
                case CC_PRIM:
                case CC_SHADE:
                case CC_ENV:
                case CC_LOD:
                    if (input_number[c[i][j]] == 0) {
                        shader_input_mapping[i][next_input_number - 1] = c[i][j];
                        input_number[c[i][j]] = next_input_number++;
                    }
                    val = input_number[c[i][j]];
                    break;
            }
            shader_id |= val << (i * 12 + j * 3);
        }
    }
    comb->cc_id = cc_id;
    comb->prg = gfx_lookup_or_create_shader_program(shader_id);
    memcpy(comb->shader_input_mapping, shader_input_mapping, sizeof(shader_input_mapping));
}

static struct ColorCombiner* gfx_lookup_or_create_color_combiner(uint32_t cc_id) {
    static struct ColorCombiner* prev_combiner;
    size_t i;
    struct ColorCombiner* comb;

    if (prev_combiner != NULL && prev_combiner->cc_id == cc_id) {
        return prev_combiner;
    }

    for (i = 0; i < color_combiner_pool_size; i++) {
        if (color_combiner_pool[i].cc_id == cc_id) {
            return prev_combiner = &color_combiner_pool[i];
        }
    }
    gfx_flush();
    if (color_combiner_pool_size == COLOR_COMBINER_POOL_SIZE) {
        platform_fatal("color combiner pool exhausted");
    }
    comb = &color_combiner_pool[color_combiner_pool_size++];
    gfx_generate_cc(comb, cc_id);
    return prev_combiner = comb;
}

/* ---- texture cache -------------------------------------------------------- */

static size_t texture_cache_hash(const void* addr) {
    return ((uintptr_t) addr >> 5) & (TEXTURE_HASHMAP_SIZE - 1);
}

void gfx_texture_cache_invalidate(void* orig_addr) {
#ifdef IE_GFX_SVC
    if (ie_gfx_svc_active()) {
        ie_gfx_svc_invalidate(orig_addr);
        return;
    }
#endif
    const void* addr = segmented_to_virtual(orig_addr);
    struct TextureCacheNode* node = gfx_texture_cache.hashmap[texture_cache_hash(addr)];
    struct TextureCacheNode* pool_end = &gfx_texture_cache.pool[gfx_texture_cache.pool_pos];

    while (node != NULL && node < pool_end) {
        if (node->addr == addr) {
            node->dirty = 1;
        }
        node = node->next;
    }
}

/*
 * Drop every cached texture and force full state resubmission. The
 * game calls this when it repurposes the memory the cached textures
 * were decoded from (course loads and similar wholesale swaps).
 */
void nuke_everything(void) {
#ifdef IE_GFX_SVC
    if (ie_gfx_svc_active()) {
        ie_gfx_svc_reset();
        return;
    }
#endif
    gfx_flush();
    gfx_texture_cache.pool_pos = 0;
    memset(gfx_texture_cache.hashmap, 0, sizeof(gfx_texture_cache.hashmap));
    memset(&rendering_state, 0, sizeof(rendering_state));
    rendering_state.depth_test = 0xFF;
    rendering_state.depth_mask = 0xFF;
    rendering_state.decal_mode = 0xFF;
    rendering_state.alpha_blend = 0xFF;
    rdp.textures_changed[0] = 1;
    rdp.textures_changed[1] = 1;
    rdp.viewport_or_scissor_changed = 1;
}

static void texture_cache_key_from_rdp(int tile, struct TextureCacheKey* key) {
    key->addr = segmented_to_virtual(rdp.loaded_texture[tile].addr);
    key->palette = rdp.texture_tile.fmt == G_IM_FMT_CI ? rdp.palette : NULL;
    key->size_bytes = rdp.loaded_texture[tile].size_bytes;
    key->line_size_bytes = rdp.texture_tile.line_size_bytes;
    key->source_width = last_set_texture_image_width;
    key->uls = rdp.texture_tile.uls;
    key->ult = rdp.texture_tile.ult;
    key->lrs = rdp.texture_tile.lrs;
    key->lrt = rdp.texture_tile.lrt;
    key->fmt = rdp.texture_tile.fmt;
    key->siz = rdp.texture_tile.siz;
}

static bool texture_cache_node_matches_key(const struct TextureCacheNode* node,
                                           const struct TextureCacheKey* key) {
    return node != NULL && node->addr == key->addr && node->palette == key->palette &&
           node->size_bytes == key->size_bytes &&
           node->line_size_bytes == key->line_size_bytes &&
           node->source_width == key->source_width && node->uls == key->uls &&
           node->ult == key->ult && node->lrs == key->lrs && node->lrt == key->lrt &&
           node->fmt == key->fmt && node->siz == key->siz;
}

/* Returns true when the texture was already uploaded and clean. */
static bool gfx_texture_cache_lookup(int tile, struct TextureCacheNode** n,
                                     const struct TextureCacheKey* key) {
    struct TextureCacheNode** node = &gfx_texture_cache.hashmap[texture_cache_hash(key->addr)];
    struct TextureCacheNode* pool_end = &gfx_texture_cache.pool[gfx_texture_cache.pool_pos];

    while (*node != NULL && *node < pool_end) {
        if (texture_cache_node_matches_key(*node, key)) {
            gfx_rapi->select_texture(tile, (*node)->texture_id);
            *n = *node;
            if ((*node)->dirty) {
                (*node)->dirty = 0;
                return false;
            }
            return true;
        }
        node = &(*node)->next;
    }

    if (gfx_texture_cache.pool_pos == GFX_TEXTURE_CACHE_SIZE) {
        platform_fatal("texture cache exhausted");
    }

    *node = &gfx_texture_cache.pool[gfx_texture_cache.pool_pos++];
    if (!(*node)->id_allocated) {
        (*node)->texture_id = gfx_rapi->new_texture();
        (*node)->id_allocated = 1;
    }
    gfx_rapi->select_texture(tile, (*node)->texture_id);
    gfx_rapi->set_sampler_parameters(tile, 0, 0, 0);
    (*node)->addr = key->addr;
    (*node)->palette = key->palette;
    (*node)->size_bytes = key->size_bytes;
    (*node)->line_size_bytes = key->line_size_bytes;
    (*node)->source_width = key->source_width;
    (*node)->fmt = key->fmt;
    (*node)->siz = key->siz;
    (*node)->uls = key->uls;
    (*node)->ult = key->ult;
    (*node)->lrs = key->lrs;
    (*node)->lrt = key->lrt;
    (*node)->dirty = 0;
    (*node)->cms = 0;
    (*node)->cmt = 0;
    (*node)->linear_filter = 0;
    (*node)->next = NULL;
    *n = *node;
    return false;
}

/* ---- texture import (all formats decode to RGBA32) ----------------------- */

static void put_rgba16(uint8_t* out, uint16_t texel) {
    out[0] = SCALE_5_8((texel >> 11) & 0x1F);
    out[1] = SCALE_5_8((texel >> 6) & 0x1F);
    out[2] = SCALE_5_8((texel >> 1) & 0x1F);
    out[3] = (texel & 1) ? 0xFF : 0x00;
}

static void put_ia(uint8_t* out, uint8_t intensity, uint8_t alpha) {
    out[0] = intensity;
    out[1] = intensity;
    out[2] = intensity;
    out[3] = alpha;
}

static void import_texture_rgba16(int tile) {
    uint32_t width = rdp.texture_tile.line_size_bytes / 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;
    const uint8_t* addr = rdp.loaded_texture[tile].addr;
    uint32_t i;

    if (last_set_texture_image_width == 0) {
        uint32_t count = rdp.loaded_texture[tile].size_bytes / 2;
        for (i = 0; i < count; i++) {
            put_rgba16(rgba32_buf + 4 * i, texel_be_u16(addr + 2 * i));
        }
    } else {
        /* Tile loaded out of a wider image: extract the sub-rectangle.
         * The width fixups mirror the previous translator, which was
         * tuned against this game's images. */
        uint32_t src_width = last_set_texture_image_width + 1;
        uint32_t somewidth = src_width;
        const uint8_t* start;
        uint8_t* out = rgba32_buf;
        uint32_t y, x;

        if (width <= (src_width / 2) + 4) {
            somewidth = width;
        } else if (width == 20 && last_set_texture_image_width == 30) {
            somewidth = width - 4;
        }

        start = addr + ((rdp.texture_tile.uls >> G_TEXTURE_IMAGE_FRAC) * 2)
              + ((rdp.texture_tile.ult >> G_TEXTURE_IMAGE_FRAC) * src_width * 2);
        for (y = 0; y < height; y++) {
            for (x = 0; x < somewidth; x++) {
                put_rgba16(out, texel_be_u16(start + 2 * x));
                out += 4;
            }
            start += src_width * 2;
        }
        width = somewidth;
    }

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_rgba32(int tile) {
    uint32_t width = rdp.texture_tile.line_size_bytes / 2;
    uint32_t height = (rdp.loaded_texture[tile].size_bytes / 2) / rdp.texture_tile.line_size_bytes;
    uint32_t count = width * height;
    const uint8_t* addr = rdp.loaded_texture[tile].addr;
    uint32_t i;

    for (i = 0; i < count; i++) {
        rgba32_buf[4 * i + 0] = addr[4 * i + 0];
        rgba32_buf[4 * i + 1] = addr[4 * i + 1];
        rgba32_buf[4 * i + 2] = addr[4 * i + 2];
        rgba32_buf[4 * i + 3] = addr[4 * i + 3];
    }

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_ia4(int tile) {
    uint32_t width = rdp.texture_tile.line_size_bytes * 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;
    const uint8_t* addr = rdp.loaded_texture[tile].addr;
    uint32_t i;

    if (last_set_texture_image_width == 0) {
        for (i = 0; i < rdp.loaded_texture[tile].size_bytes * 2; i++) {
            uint8_t byte = addr[i / 2];
            uint8_t part = (byte >> (4 - (i % 2) * 4)) & 0xF;
            put_ia(rgba32_buf + 4 * i, SCALE_3_8(part >> 1), (part & 1) ? 0xFF : 0x00);
        }
    } else {
        /* Sub-image extraction with the previous translator's odd-line
         * nibble ordering (TMEM interleave behaviour). */
        uint32_t src_width = last_set_texture_image_width + 1;
        const uint8_t* start = addr
            + (((rdp.texture_tile.ult >> G_TEXTURE_IMAGE_FRAC) / 2) * src_width)
            + ((rdp.texture_tile.uls >> G_TEXTURE_IMAGE_FRAC) / 2);
        uint8_t* tex8 = xform_buf;
        uint32_t y, x;

        memset(xform_buf, 0, sizeof(xform_buf));
        for (y = 0; y < height; y++) {
            for (x = 0; x < src_width * 2; x += 2) {
                uint32_t sidx = x / 2;
                if (y & 1) {
                    tex8[x] = start[sidx] & 0xF;
                    tex8[x + 1] = (start[sidx] >> 4) & 0xF;
                } else {
                    tex8[x] = (start[sidx] >> 4) & 0xF;
                    tex8[x + 1] = start[sidx] & 0xF;
                }
            }
            start += src_width;
            tex8 += src_width;
        }
        for (i = 0; i < rdp.loaded_texture[tile].size_bytes * 2; i++) {
            uint8_t part = xform_buf[i];
            put_ia(rgba32_buf + 4 * i, SCALE_3_8(part >> 1), (part & 1) ? 0xFF : 0x00);
        }
    }

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_ia8(int tile) {
    uint32_t width = rdp.texture_tile.line_size_bytes;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;
    uint32_t src_width = last_set_texture_image_width ? last_set_texture_image_width + 1 : width;
    const uint8_t* start = rdp.loaded_texture[tile].addr
        + ((rdp.texture_tile.ult >> G_TEXTURE_IMAGE_FRAC) * (last_set_texture_image_width + 1))
        + (rdp.texture_tile.uls >> G_TEXTURE_IMAGE_FRAC);
    uint8_t* out = rgba32_buf;
    uint32_t y, x;

    for (y = 0; y < height; y++) {
        for (x = 0; x < src_width; x++) {
            uint8_t val = start[x];
            put_ia(out + 4 * x, SCALE_4_8((val >> 4) & 0xF), SCALE_4_8(val & 0xF));
        }
        start += src_width;
        out += 4 * src_width;
    }

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_ia16(int tile) {
    uint32_t width = rdp.texture_tile.line_size_bytes / 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;
    uint32_t src_width = last_set_texture_image_width ? last_set_texture_image_width + 1 : width;
    const uint8_t* start = rdp.loaded_texture[tile].addr;
    uint8_t* out = rgba32_buf;
    uint32_t y, x;

    if (last_set_texture_image_width) {
        start += ((rdp.texture_tile.uls >> G_TEXTURE_IMAGE_FRAC) * 2)
               + ((rdp.texture_tile.ult >> G_TEXTURE_IMAGE_FRAC) * src_width * 2);
    }

    for (y = 0; y < height; y++) {
        for (x = 0; x < src_width; x++) {
            put_ia(out + 4 * x, start[2 * x], start[2 * x + 1]);
        }
        start += src_width * 2;
        out += 4 * src_width;
    }

    gfx_rapi->upload_texture(rgba32_buf, src_width, height);
}

static void import_texture_i4(int tile) {
    uint32_t width = rdp.texture_tile.line_size_bytes * 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;
    const uint8_t* addr = rdp.loaded_texture[tile].addr;
    uint32_t i;

    height = (height + 3) & ~3;

    if (last_set_texture_image_width == 0) {
        for (i = 0; i < rdp.loaded_texture[tile].size_bytes; i++) {
            uint8_t byte = addr[i];
            uint8_t hi = SCALE_4_8((byte >> 4) & 0xF);
            uint8_t lo = SCALE_4_8(byte & 0xF);
            put_ia(rgba32_buf + 8 * i, hi, hi);
            put_ia(rgba32_buf + 8 * i + 4, lo, lo);
        }
    } else {
        const uint8_t* start = addr
            + (((((rdp.texture_tile.ult >> G_TEXTURE_IMAGE_FRAC) - 1) / 2) * width) / 2)
            + (((rdp.texture_tile.uls >> G_TEXTURE_IMAGE_FRAC) - 1) / 2);
        uint32_t y, x;

        memset(rgba32_buf, 0, sizeof(rgba32_buf));
        for (y = 0; y < height; y++) {
            uint8_t* row = rgba32_buf + 4 * y * width;
            for (x = 0; x < (last_set_texture_image_width + 1) * 2; x += 2) {
                uint8_t byte = start[x / 2];
                uint8_t hi = SCALE_4_8((byte >> 4) & 0xF);
                uint8_t lo = SCALE_4_8(byte & 0xF);
                put_ia(row + 4 * x, hi, hi);
                put_ia(row + 4 * (x + 1), lo, lo);
            }
            start += last_set_texture_image_width + 1;
        }
    }

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_i8(int tile) {
    uint32_t width = rdp.texture_tile.line_size_bytes;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;
    uint32_t src_width = last_set_texture_image_width ? last_set_texture_image_width + 1 : width;
    const uint8_t* start = rdp.loaded_texture[tile].addr
        + ((rdp.texture_tile.ult >> G_TEXTURE_IMAGE_FRAC) * (last_set_texture_image_width + 1))
        + (rdp.texture_tile.uls >> G_TEXTURE_IMAGE_FRAC);
    uint32_t y, x, i;

    memset(xform_buf, 0, sizeof(xform_buf));
    for (y = 0; y < height; y++) {
        for (x = 0; x < src_width; x++) {
            xform_buf[y * width + x] = start[x];
        }
        start += src_width;
    }

    for (i = 0; i < rdp.loaded_texture[tile].size_bytes; i++) {
        uint8_t val = xform_buf[i];
        put_ia(rgba32_buf + 4 * i, val, val);
    }

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static uint16_t tlut_lookup(uint8_t index) {
    return texel_be_u16(rdp.palette + 2 * index);
}

static void import_texture_ci4(int tile) {
    uint32_t width = rdp.texture_tile.line_size_bytes * 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;
    const uint8_t* addr = rdp.loaded_texture[tile].addr;
    uint32_t i;

    if (last_set_texture_image_width == 0) {
        for (i = 0; i < rdp.loaded_texture[tile].size_bytes * 2; i++) {
            uint8_t byte = addr[i / 2];
            uint8_t part = (i & 1) ? (byte & 0xF) : ((byte >> 4) & 0xF);
            put_rgba16(rgba32_buf + 4 * i, tlut_lookup(part));
        }
    } else {
        /* Sub-image path preserved from the previous translator's TMEM
         * interleave behaviour, but without shifting the output texels. */
        uint32_t src_width = last_set_texture_image_width + 1;
        const uint8_t* start = addr
            + (((rdp.texture_tile.ult >> G_TEXTURE_IMAGE_FRAC) / 2) * src_width)
            + ((rdp.texture_tile.uls >> G_TEXTURE_IMAGE_FRAC) / 2);
        uint32_t y, x;

        memset(xform_buf, 0, sizeof(xform_buf));
        for (y = 0; y < height; y++) {
            for (x = 0; x < src_width * 2; x += 2) {
                if (y & 1) {
                    xform_buf[(y * (width / 2)) + x] = start[x / 2] & 0xF;
                    xform_buf[(y * (width / 2)) + x + 1] = (start[x / 2] >> 4) & 0xF;
                } else {
                    xform_buf[(y * (width / 2)) + x] = (start[x / 2] >> 4) & 0xF;
                    xform_buf[(y * (width / 2)) + x + 1] = start[x / 2] & 0xF;
                }
            }
            start += src_width;
        }
        for (i = 0; i < rdp.loaded_texture[tile].size_bytes * 2; i++) {
            put_rgba16(rgba32_buf + 4 * i, tlut_lookup(xform_buf[i]));
        }
    }

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_ci8(int tile) {
    uint32_t width = rdp.texture_tile.line_size_bytes;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;
    const uint8_t* addr = rdp.loaded_texture[tile].addr;

    if (last_set_texture_image_width == 0) {
        uint32_t i;
        for (i = 0; i < rdp.loaded_texture[tile].size_bytes; i++) {
            put_rgba16(rgba32_buf + 4 * i, tlut_lookup(addr[i]));
        }
    } else {
        const uint8_t* start = addr
            + ((rdp.texture_tile.ult >> G_TEXTURE_IMAGE_FRAC) * last_set_texture_image_width)
            + (rdp.texture_tile.uls >> G_TEXTURE_IMAGE_FRAC);
        uint32_t src_width = last_set_texture_image_width + 1;
        uint8_t* out = rgba32_buf;
        uint32_t y, x;

        for (y = 0; y < height; y++) {
            for (x = 0; x < src_width; x++) {
                put_rgba16(out + 4 * x, tlut_lookup(start[x]));
            }
            start += src_width;
            out += 4 * width;
        }
    }

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture(int tile, const struct TextureCacheKey* key) {
    uint8_t fmt = key->fmt;
    uint8_t siz = key->siz;

    if (gfx_texture_cache_lookup(tile, &rendering_state.textures[tile], key)) {
        return;
    }

    IE_PERF_INC(IE_PERF_TEX_IMPORTS);
    if (fmt == G_IM_FMT_RGBA) {
        if (siz == G_IM_SIZ_16b) {
            import_texture_rgba16(tile);
        } else if (siz == G_IM_SIZ_32b) {
            import_texture_rgba32(tile);
        }
    } else if (fmt == G_IM_FMT_IA) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_ia4(tile);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_ia8(tile);
        } else if (siz == G_IM_SIZ_16b) {
            import_texture_ia16(tile);
        }
    } else if (fmt == G_IM_FMT_CI) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_ci4(tile);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_ci8(tile);
        }
    } else if (fmt == G_IM_FMT_I) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_i4(tile);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_i8(tile);
        }
    }
}

/* ---- triangle emission ---------------------------------------------------- */

struct GfxClipVertex {
    float x, y, z, w;
    float u, v;
    float color[4];
    float lod_w;
    uint8_t wlt0;
};

static void clip_vertex_from_loaded(struct GfxClipVertex* out,
                                    const struct F3DLoadedVertex* in) {
    out->x = in->_x;
    out->y = in->_y;
    out->z = in->_z;
    out->w = in->_w;
    out->u = in->u;
    out->v = in->v;
    out->color[0] = byte_to_unit(in->color.r);
    out->color[1] = byte_to_unit(in->color.g);
    out->color[2] = byte_to_unit(in->color.b);
    out->color[3] = byte_to_unit(in->color.a);
    out->lod_w = in->w;
    out->wlt0 = in->wlt0;
}

static float near_clip_distance(const struct GfxClipVertex* v) {
    return v->z + v->w;
}

static struct GfxClipVertex clip_vertex_lerp(const struct GfxClipVertex* a,
                                             const struct GfxClipVertex* b, float t) {
    struct GfxClipVertex out;
    int i;

    out.x = a->x + (b->x - a->x) * t;
    out.y = a->y + (b->y - a->y) * t;
    out.z = a->z + (b->z - a->z) * t;
    out.w = a->w + (b->w - a->w) * t;
    out.u = a->u + (b->u - a->u) * t;
    out.v = a->v + (b->v - a->v) * t;
    for (i = 0; i < 4; i++) {
        out.color[i] = a->color[i] + (b->color[i] - a->color[i]) * t;
    }
    out.lod_w = a->lod_w + (b->lod_w - a->lod_w) * t;
    out.wlt0 = out.w < 0.0f;
    return out;
}

static uint8_t clip_triangle_near_plane(const struct GfxClipVertex in[3],
                                        struct GfxClipVertex out[4]) {
    struct GfxClipVertex prev = in[2];
    float prev_dist = near_clip_distance(&prev);
    uint8_t prev_inside = prev_dist >= 0.0f;
    uint8_t out_count = 0;
    int i;

    for (i = 0; i < 3; i++) {
        const struct GfxClipVertex* cur = &in[i];
        float cur_dist = near_clip_distance(cur);
        uint8_t cur_inside = cur_dist >= 0.0f;

        if (cur_inside != prev_inside) {
            float denom = prev_dist - cur_dist;
            float t = denom != 0.0f ? prev_dist / denom : 0.0f;
            out[out_count++] = clip_vertex_lerp(&prev, cur, t);
        }
        if (cur_inside) {
            out[out_count++] = *cur;
        }

        prev = *cur;
        prev_dist = cur_dist;
        prev_inside = cur_inside;
    }
    return out_count;
}

static uint8_t triangle_cross_culled(float x1, float y1, float w1,
                                     float x2, float y2, float w2,
                                     float x3, float y3, float w3) {
    float cross;

    if ((gF3dRsp.geometry_mode & G_CULL_BOTH) == 0 || gIsMirrorMode) {
        return 0;
    }

    /* Screen-space winding without the three 1/w divides: the NDC edge
     * cross product equals -det[(x,y,w) rows] / (w1*w2*w3), and the
     * wlt0-parity sign flip the divided form needed is exactly the sign
     * of that w product - so the flipped cross and -det agree in sign
     * for every w-sign combination, and the divides (plus their w~0
     * guard) drop out. */
    cross = x1 * (y3 * w2 - y2 * w3) + y1 * (x2 * w3 - x3 * w2)
          + w1 * (x3 * y2 - x2 * y3);

    switch (gF3dRsp.geometry_mode & G_CULL_BOTH) {
        case G_CULL_FRONT:
            return cross <= 0.0f;
        case G_CULL_BACK:
            return cross >= 0.0f;
        case G_CULL_BOTH:
            return 1;
        default:
            return 0;
    }
}

static uint8_t clip_triangle_culled(const struct GfxClipVertex* v1,
                                    const struct GfxClipVertex* v2,
                                    const struct GfxClipVertex* v3) {
    return triangle_cross_culled(v1->x, v1->y, v1->w,
                                 v2->x, v2->y, v2->w,
                                 v3->x, v3->y, v3->w);
}

static uint8_t loaded_triangle_culled(const struct F3DLoadedVertex* v1,
                                      const struct F3DLoadedVertex* v2,
                                      const struct F3DLoadedVertex* v3) {
    return triangle_cross_culled(v1->_x, v1->_y, v1->_w,
                                 v2->_x, v2->_y, v2->_w,
                                 v3->_x, v3->_y, v3->_w);
}

static void append_rgba_input(float r, float g, float b, float a, uint8_t use_alpha) {
    buf_vbo[buf_vbo_len++] = r;
    buf_vbo[buf_vbo_len++] = g;
    buf_vbo[buf_vbo_len++] = b;
    if (use_alpha) {
        buf_vbo[buf_vbo_len++] = a;
    }
}

/* Per-shader-input descriptor, resolved once per gfx_sp_tri1 instead of
 * re-classified for every emitted vertex. Constant inputs (PRIM/ENV and the
 * default white) carry their resolved unit-range RGBA; SHADE and LOD stay
 * per-vertex because they vary across the triangle. */
enum EmitInputKind { EIN_CONST, EIN_SHADE, EIN_LOD };
struct EmitInput {
    uint8_t kind;
    float rgba[4];
};

static void build_emit_inputs(struct EmitInput inputs[4], const struct ColorCombiner* comb,
                              uint8_t num_inputs) {
    int j;
    for (j = 0; j < num_inputs; j++) {
        struct EmitInput* e = &inputs[j];
        switch (comb->shader_input_mapping[0][j]) {
            case CC_PRIM:
                e->kind = EIN_CONST;
                e->rgba[0] = byte_to_unit(rdp.prim_color.r);
                e->rgba[1] = byte_to_unit(rdp.prim_color.g);
                e->rgba[2] = byte_to_unit(rdp.prim_color.b);
                e->rgba[3] = byte_to_unit(rdp.prim_color.a);
                break;
            case CC_ENV:
                e->kind = EIN_CONST;
                e->rgba[0] = byte_to_unit(rdp.env_color.r);
                e->rgba[1] = byte_to_unit(rdp.env_color.g);
                e->rgba[2] = byte_to_unit(rdp.env_color.b);
                e->rgba[3] = byte_to_unit(rdp.env_color.a);
                break;
            case CC_SHADE:
                e->kind = EIN_SHADE;
                break;
            case CC_LOD:
                e->kind = EIN_LOD;
                break;
            default:
                e->kind = EIN_CONST;
                e->rgba[0] = e->rgba[1] = e->rgba[2] = e->rgba[3] = 1.0f;
                break;
        }
    }
}

/* Append one vertex's attributes to buf_vbo. shade is the CC_SHADE
 * input as unit-range RGBA; lod_w feeds the CC_LOD ramp. */
static void emit_vertex(float x, float y, float z, float w, float u_in, float v_in,
                        const float shade[4], float lod_w, const struct EmitInput* inputs,
                        uint8_t num_inputs, uint8_t use_texture, float inv_tex_width,
                        float inv_tex_height, uint8_t use_fog, uint8_t use_alpha) {
    int j;

    buf_vbo[buf_vbo_len++] = x;
    buf_vbo[buf_vbo_len++] = y;
    buf_vbo[buf_vbo_len++] = z;
    buf_vbo[buf_vbo_len++] = w;

    if (use_texture) {
        float u = (u_in - (rdp.texture_tile.uls << 3)) / 32.0f;
        float tv = (v_in - (rdp.texture_tile.ult << 3)) / 32.0f;
        if ((rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT) {
            u += 0.5f;
            tv += 0.5f;
        }
        buf_vbo[buf_vbo_len++] = u * inv_tex_width;
        buf_vbo[buf_vbo_len++] = tv * inv_tex_height;
    }

    if (use_fog) {
        float winv = 1.0f / w;
        float fog_z;
        if (winv < 0) {
            winv = 32767.0f;
        }
        fog_z = z * winv * rsp_fog_mul + rsp_fog_offset;
        if (fog_z < 0.0f) {
            fog_z = 0.0f;
        }
        if (fog_z > 255.0f) {
            fog_z = 255.0f;
        }
        append_rgba_input(byte_to_unit(rdp.fog_color.r), byte_to_unit(rdp.fog_color.g),
                          byte_to_unit(rdp.fog_color.b), fog_z * BYTE_TO_UNIT_SCALE, 1);
    }

    for (j = 0; j < num_inputs; j++) {
        const struct EmitInput* e = &inputs[j];

        if (e->kind == EIN_SHADE) {
            append_rgba_input(shade[0], shade[1], shade[2], shade[3], use_alpha);
        } else if (e->kind == EIN_LOD) {
            float distance_frac = (lod_w - 3000.0f) / 3000.0f;
            int k;
            float lod;
            if (distance_frac < 0.0f) {
                distance_frac = 0.0f;
            }
            if (distance_frac > 1.0f) {
                distance_frac = 1.0f;
            }
            k = (int) (distance_frac * 255.0f);
            lod = k * BYTE_TO_UNIT_SCALE;
            append_rgba_input(lod, lod, lod, lod, use_alpha);
        } else {
            append_rgba_input(e->rgba[0], e->rgba[1], e->rgba[2], e->rgba[3], use_alpha);
        }
    }
}

static void emit_triangle_end(void) {
    buf_vbo_num_tris++;

    if (buf_vbo_num_tris == MAX_BUFFERED) {
        gfx_flush();
    }
}

static void emit_clipped_triangle(const struct GfxClipVertex* v_arr[3],
                                  const struct EmitInput* inputs, uint8_t num_inputs,
                                  uint8_t use_texture, float inv_tex_width,
                                  float inv_tex_height, uint8_t use_fog,
                                  uint8_t use_alpha) {
    int i;

    for (i = 0; i < 3; i++) {
        const struct GfxClipVertex* v = v_arr[i];

        emit_vertex(v->x, v->y, v->z, v->w, v->u, v->v, v->color, v->lod_w, inputs,
                    num_inputs, use_texture, inv_tex_width, inv_tex_height, use_fog,
                    use_alpha);
    }
    emit_triangle_end();
}

/* Near-plane fast path: emit straight from the loaded vertices, skipping
 * the GfxClipVertex conversions the clipper needs. Field mapping mirrors
 * clip_vertex_from_loaded exactly. */
static void emit_loaded_triangle(const struct F3DLoadedVertex* const v_arr[3],
                                 const struct EmitInput* inputs, uint8_t num_inputs,
                                 uint8_t use_texture, float inv_tex_width,
                                 float inv_tex_height, uint8_t use_fog,
                                 uint8_t use_alpha) {
    int i;

    for (i = 0; i < 3; i++) {
        const struct F3DLoadedVertex* v = v_arr[i];
        float shade[4];

        shade[0] = byte_to_unit(v->color.r);
        shade[1] = byte_to_unit(v->color.g);
        shade[2] = byte_to_unit(v->color.b);
        shade[3] = byte_to_unit(v->color.a);
        emit_vertex(v->_x, v->_y, v->_z, v->_w, v->u, v->v, shade, v->w, inputs,
                    num_inputs, use_texture, inv_tex_width, inv_tex_height, use_fog,
                    use_alpha);
    }
    emit_triangle_end();
}

static void gfx_sp_tri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx) {
    gfx_tnl_drain(); /* an async T&L batch may still be writing these */
    struct F3DLoadedVertex* v1 = &gF3dRsp.loaded_vertices[vtx1_idx];
    struct F3DLoadedVertex* v2 = &gF3dRsp.loaded_vertices[vtx2_idx];
    struct F3DLoadedVertex* v3 = &gF3dRsp.loaded_vertices[vtx3_idx];
    struct GfxClipVertex unclipped[3];
    struct GfxClipVertex clipped[4];
    uint8_t clipped_count;
    uint8_t output_tris = 0;
    uint8_t tri_culled[2] = { 0, 0 };
    uint8_t any_output = 0;
    uint8_t near_inside;
    uint32_t cc_id;
    uint8_t use_alpha, use_fog, texture_edge, use_noise;
    struct ColorCombiner* comb;
    struct ShaderProgram* prg;
    uint8_t num_inputs;
    uint8_t used_textures[2];
    uint8_t use_texture;
    uint32_t tex_width = 0, tex_height = 0;
    float inv_tex_width = 0.0f, inv_tex_height = 0.0f;
    uint8_t depth_test, z_upd, zmode_decal;
    struct EmitInput emit_inputs[4];
    int i;

    /* Each clip_rej bit is set when the vertex is inside that plane's
     * half-space; a plane with the bit clear for all three vertices
     * rejects the whole triangle. */
    if ((v1->clip_rej | v2->clip_rej | v3->clip_rej) != 0x3F) {
        return;
    }

    /* Near-plane fast path: clip_rej bit 4 is set when the vertex is
     * inside the near plane (z >= -w, the same predicate the clipper
     * tests). With all three vertices inside, the clipper provably
     * returns the input triangle unchanged, so skip the GfxClipVertex
     * builds and the clip walk entirely. This is the overwhelmingly
     * common case: whole frames pass with only a handful of triangles
     * actually crossing the near plane. */
    near_inside = (v1->clip_rej & v2->clip_rej & v3->clip_rej & 0x10) != 0;
    if (near_inside) {
        if (loaded_triangle_culled(v1, v2, v3)) {
            return;
        }
    } else {
        clip_vertex_from_loaded(&unclipped[0], v1);
        clip_vertex_from_loaded(&unclipped[1], v2);
        clip_vertex_from_loaded(&unclipped[2], v3);
        clipped_count = clip_triangle_near_plane(unclipped, clipped);
        if (clipped_count < 3) {
            return;
        }

        output_tris = clipped_count - 2;
        if (clipped_count != 3) {
            IE_PERF_INC(IE_PERF_CLIPPED_TRIS);
        }
        for (i = 0; i < output_tris; i++) {
            tri_culled[i] = clip_triangle_culled(&clipped[0], &clipped[i + 1], &clipped[i + 2]);
            if (!tri_culled[i]) {
                any_output = 1;
            }
        }
        if (!any_output) {
            return;
        }
    }

    depth_test = (gF3dRsp.geometry_mode & G_ZBUFFER) == G_ZBUFFER;
    if (depth_test != rendering_state.depth_test) {
        gfx_flush();
        gfx_rapi->set_depth_test(depth_test);
        rendering_state.depth_test = depth_test;
    }

    z_upd = (rdp.other_mode_l & Z_UPD) == Z_UPD;
    if (z_upd != rendering_state.depth_mask) {
        gfx_flush();
        gfx_rapi->set_depth_mask(z_upd);
        rendering_state.depth_mask = z_upd;
    }

    zmode_decal = (rdp.other_mode_l & ZMODE_DEC) == ZMODE_DEC;
    if (zmode_decal != rendering_state.decal_mode) {
        gfx_flush();
        gfx_rapi->set_zmode_decal(zmode_decal);
        rendering_state.decal_mode = zmode_decal;
    }

    if (rdp.viewport_or_scissor_changed) {
        if (memcmp(&rdp.viewport, &rendering_state.viewport, sizeof(rdp.viewport)) != 0) {
            gfx_flush();
            gfx_rapi->set_viewport(rdp.viewport.x, rdp.viewport.y, rdp.viewport.width,
                                   rdp.viewport.height);
            rendering_state.viewport = rdp.viewport;
        }
        if (memcmp(&rdp.scissor, &rendering_state.scissor, sizeof(rdp.scissor)) != 0) {
            gfx_flush();
            gfx_rapi->set_scissor(rdp.scissor.x, rdp.scissor.y, rdp.scissor.width,
                                  rdp.scissor.height);
            rendering_state.scissor = rdp.scissor;
        }
        rdp.viewport_or_scissor_changed = 0;
    }

    cc_id = rdp.combine_mode;

    use_alpha = (rdp.other_mode_l & (G_BL_A_MEM << 18)) == 0;
    use_fog = (rdp.other_mode_l >> 30) == G_BL_CLR_FOG;
    texture_edge = (rdp.other_mode_l & CVG_X_ALPHA) == CVG_X_ALPHA;
    use_noise = (rdp.other_mode_l & G_AC_DITHER) == G_AC_DITHER;

    if (texture_edge) {
        use_alpha = 1;
    }

    if (use_alpha) {
        cc_id |= SHADER_OPT_ALPHA;
    }
    if (use_fog) {
        cc_id |= SHADER_OPT_FOG;
    }
    if (texture_edge) {
        cc_id |= SHADER_OPT_TEXTURE_EDGE;
    }
    if (use_noise) {
        cc_id |= SHADER_OPT_NOISE;
    }

    if (!use_alpha) {
        cc_id &= ~0xFFF000;
    }

    comb = gfx_lookup_or_create_color_combiner(cc_id);
    prg = comb->prg;
    if (prg != rendering_state.shader_program) {
        gfx_flush();
        gfx_rapi->unload_shader(rendering_state.shader_program);
        gfx_rapi->load_shader(prg);
        rendering_state.shader_program = prg;
    }

    if (use_alpha != rendering_state.alpha_blend) {
        gfx_flush();
        gfx_rapi->set_use_alpha(use_alpha);
        rendering_state.alpha_blend = use_alpha;
    }

    gfx_rapi->shader_get_info(prg, &num_inputs, used_textures);

    for (i = 0; i < 2; i++) {
        if (used_textures[i]) {
            if (rdp.textures_changed[i]) {
                struct TextureCacheKey key;

                texture_cache_key_from_rdp(i, &key);
                if (!texture_cache_node_matches_key(rendering_state.textures[i], &key) ||
                    rendering_state.textures[i]->dirty) {
                    gfx_flush();
                }
                import_texture(i, &key);
                rdp.textures_changed[i] = 0;
            }
            uint8_t linear_filter = (rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT;
            if (linear_filter != rendering_state.textures[i]->linear_filter ||
                rdp.texture_tile.cms != rendering_state.textures[i]->cms ||
                rdp.texture_tile.cmt != rendering_state.textures[i]->cmt) {
                gfx_flush();
                gfx_rapi->set_sampler_parameters(i, linear_filter, rdp.texture_tile.cms,
                                                 rdp.texture_tile.cmt);
                rendering_state.textures[i]->linear_filter = linear_filter;
                rendering_state.textures[i]->cms = rdp.texture_tile.cms;
                rendering_state.textures[i]->cmt = rdp.texture_tile.cmt;
            }
        }
    }

    build_emit_inputs(emit_inputs, comb, num_inputs);

    use_texture = used_textures[0] || used_textures[1];
    if (use_texture) {
        tex_width = (rdp.texture_tile.lrs - rdp.texture_tile.uls + 4) / 4;
        tex_height = (rdp.texture_tile.lrt - rdp.texture_tile.ult + 4) / 4;
        inv_tex_width = 1.0f / (float) tex_width;
        inv_tex_height = 1.0f / (float) tex_height;
    }

    if (near_inside) {
        const struct F3DLoadedVertex* tri[3];

        tri[0] = v1;
        tri[1] = v2;
        tri[2] = v3;
        emit_loaded_triangle(tri, emit_inputs, num_inputs, use_texture, inv_tex_width,
                             inv_tex_height, use_fog, use_alpha);
        return;
    }

    for (i = 0; i < output_tris; i++) {
        const struct GfxClipVertex* tri[3];

        if (tri_culled[i]) {
            continue;
        }
        tri[0] = &clipped[0];
        tri[1] = &clipped[i + 1];
        tri[2] = &clipped[i + 2];
        emit_clipped_triangle(tri, emit_inputs, num_inputs, use_texture, inv_tex_width,
                              inv_tex_height, use_fog, use_alpha);
    }
}

/* ---- SP state ------------------------------------------------------------- */

static void gfx_sp_geometry_mode(uint32_t clear, uint32_t set) {
    gF3dRsp.geometry_mode &= ~clear;
    gF3dRsp.geometry_mode |= set;
}

static void gfx_calc_and_set_viewport(const Vp_t* viewport) {
    /* vscale/vtrans carry 2 fraction bits and half-extents. */
    float width = viewport->vscale[0] * 0.5f;
    float height = viewport->vscale[1] * 0.5f;
    float x = (viewport->vtrans[0] * 0.25f) - width * 0.5f;
    float y = SCREEN_HEIGHT - ((viewport->vtrans[1] * 0.25f) + height * 0.5f);

    width *= RATIO_X;
    height *= RATIO_Y;
    x *= RATIO_X;
    y *= RATIO_Y;

    rdp.viewport.x = x;
    rdp.viewport.y = y;
    rdp.viewport.width = width;
    rdp.viewport.height = height;

    rdp.viewport_or_scissor_changed = 1;
}

static void gfx_sp_movemem(uint8_t index, const void* data) {
    switch (index) {
        case G_MV_VIEWPORT:
            gfx_calc_and_set_viewport((const Vp_t*) data);
            break;
        case G_MV_L0:
        case G_MV_L1:
        case G_MV_L2:
            /* NOTE: reads out of bounds if it is an ambient light. */
            memcpy(gF3dRsp.current_lights + (index - G_MV_L0) / 2, data, sizeof(Light_t));
            gF3dRsp.lights_changed = 1;
            break;
        default:
            break;
    }
}

static void gfx_sp_moveword(uint8_t index, uint32_t data) {
    switch (index) {
        case G_MW_NUMLIGHT:
            /* Ambient light is included; the 0x80000000 base is the RSP
             * numlight encoding offset, subtracted to recover the count. */
            gF3dRsp.current_num_lights = (data - 0x80000000U) / 32;
            gF3dRsp.lights_changed = 1;
            break;
        case G_MW_FOG:
            rsp_fog_mul = (int16_t)(data >> 16);
            rsp_fog_offset = (int16_t) data;
            break;
        default:
            break;
    }
}

static void gfx_sp_texture(uint16_t sc, uint16_t tc) {
    gF3dRsp.texture_scaling_factor.s = sc;
    gF3dRsp.texture_scaling_factor.t = tc;
}

/* ---- DP state ------------------------------------------------------------- */

static void gfx_dp_set_scissor(uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
    float x = ulx * 0.25f * RATIO_X;
    float y = (SCREEN_HEIGHT - lry * 0.25f) * RATIO_Y;
    float width = (lrx - ulx) * 0.25f * RATIO_X;
    float height = (lry - uly) * 0.25f * RATIO_Y;

    rdp.scissor.x = x;
    rdp.scissor.y = y;
    rdp.scissor.width = width;
    rdp.scissor.height = height;

    rdp.viewport_or_scissor_changed = 1;
}

static void gfx_dp_set_texture_image(uint32_t size, uint32_t width, const void* addr) {
    rdp.texture_to_load.addr = segmented_to_virtual(addr);
    rdp.texture_to_load.siz = size;
    last_set_texture_image_width = width;
}

static void gfx_dp_set_tile(uint8_t fmt, uint32_t siz, uint32_t line, uint32_t tmem, uint8_t tile,
                            uint32_t cmt, uint32_t maskt, uint32_t cms, uint32_t masks) {
    if (tile == G_TX_RENDERTILE) {
        rdp.texture_tile.fmt = fmt;
        rdp.texture_tile.siz = siz;

        if (cms == G_TX_WRAP && masks == G_TX_NOMASK) {
            cms = G_TX_CLAMP;
        }
        if (cmt == G_TX_WRAP && maskt == G_TX_NOMASK) {
            cmt = G_TX_CLAMP;
        }
        rdp.texture_tile.cms = cms;
        rdp.texture_tile.cmt = cmt;
        rdp.texture_tile.line_size_bytes = line * 8;
        rdp.textures_changed[0] = 1;
        rdp.textures_changed[1] = 1;
    }

    if (tile == G_TX_LOADTILE) {
        rdp.texture_to_load.tile_number = tmem >> 8;
    } else {
        rdp.texture_to_load.tile_number = tile;
    }
}

static void gfx_dp_set_tile_size(uint8_t tile, uint16_t uls, uint16_t ult, uint16_t lrs,
                                 uint16_t lrt) {
    if (tile == G_TX_RENDERTILE) {
        rdp.texture_tile.uls = uls;
        rdp.texture_tile.ult = ult;
        rdp.texture_tile.lrs = lrs;
        rdp.texture_tile.lrt = lrt;
        rdp.textures_changed[0] = 1;
        rdp.textures_changed[1] = 1;
    }
}

static void gfx_dp_load_tlut(void) {
    /* Palette entries stay in stored (big-endian rgba16) form and are
     * decoded per texel by the CI importers. */
    rdp.palette = rdp.texture_to_load.addr;
}

static void gfx_dp_load_block(uint32_t lrs) {
    /* lrs is the number of texels to load, minus one. */
    uint32_t word_size_shift = 0;

    switch (rdp.texture_to_load.siz) {
        case G_IM_SIZ_4b:
        case G_IM_SIZ_8b:
            word_size_shift = 0;
            break;
        case G_IM_SIZ_16b:
            word_size_shift = 1;
            break;
        case G_IM_SIZ_32b:
            word_size_shift = 2;
            break;
    }
    rdp.loaded_texture[rdp.texture_to_load.tile_number].size_bytes = (lrs + 1) << word_size_shift;
    rdp.loaded_texture[rdp.texture_to_load.tile_number].addr = rdp.texture_to_load.addr;
    rdp.textures_changed[rdp.texture_to_load.tile_number] = 1;
}

static void gfx_dp_load_tile(uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    uint32_t word_size_shift = 0;
    uint32_t size_bytes;

    switch (rdp.texture_to_load.siz) {
        case G_IM_SIZ_4b:
        case G_IM_SIZ_8b:
            word_size_shift = 0;
            break;
        case G_IM_SIZ_16b:
            word_size_shift = 1;
            break;
        case G_IM_SIZ_32b:
            word_size_shift = 2;
            break;
    }

    size_bytes = ((((lrs - uls) >> G_TEXTURE_IMAGE_FRAC) + 1)
                  * (((lrt - ult) >> G_TEXTURE_IMAGE_FRAC) + 1))
                 << word_size_shift;
    rdp.loaded_texture[rdp.texture_to_load.tile_number].size_bytes = size_bytes;
    rdp.loaded_texture[rdp.texture_to_load.tile_number].addr = rdp.texture_to_load.addr;
    rdp.texture_tile.uls = uls;
    rdp.texture_tile.ult = ult;
    rdp.texture_tile.lrs = lrs;
    rdp.texture_tile.lrt = lrt;
    rdp.textures_changed[rdp.texture_to_load.tile_number] = 1;
}

static uint8_t color_comb_component(uint32_t v) {
    switch (v) {
        case G_CCMUX_TEXEL0:
            return CC_TEXEL0;
        case G_CCMUX_TEXEL1:
            return CC_TEXEL1;
        case G_CCMUX_PRIMITIVE:
            return CC_PRIM;
        case G_CCMUX_SHADE:
            return CC_SHADE;
        case G_CCMUX_ENVIRONMENT:
            return CC_ENV;
        case G_CCMUX_TEXEL0_ALPHA:
            return CC_TEXEL0A;
        case G_CCMUX_LOD_FRACTION:
            return CC_LOD;
        default:
            return CC_0;
    }
}

static inline uint32_t color_comb(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return color_comb_component(a) | (color_comb_component(b) << 3)
         | (color_comb_component(c) << 6) | (color_comb_component(d) << 9);
}

static void gfx_dp_set_combine_mode(uint32_t rgb, uint32_t alpha) {
    rdp.combine_mode = rgb | (alpha << 12);
}

static void gfx_dp_set_env_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.env_color.r = r;
    rdp.env_color.g = g;
    rdp.env_color.b = b;
    rdp.env_color.a = a;
}

static void gfx_dp_set_prim_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.prim_color.r = r;
    rdp.prim_color.g = g;
    rdp.prim_color.b = b;
    rdp.prim_color.a = a;
}

static void gfx_dp_set_fog_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.fog_color.r = r;
    rdp.fog_color.g = g;
    rdp.fog_color.b = b;
    rdp.fog_color.a = a;
}

static void gfx_dp_set_fill_color(uint32_t packed_color) {
    uint16_t col16 = (uint16_t) packed_color;

    rdp.fill_color.r = SCALE_5_8((col16 >> 11) & 0x1F);
    rdp.fill_color.g = SCALE_5_8((col16 >> 6) & 0x1F);
    rdp.fill_color.b = SCALE_5_8((col16 >> 1) & 0x1F);
    rdp.fill_color.a = (col16 & 1) * 255;
}

/* ---- rectangles ------------------------------------------------------------
 *
 * Rectangles are drawn through the normal triangle path using the four
 * scratch vertex slots past the RSP's 64 loadable vertices, with
 * pre-projected clip-space coordinates and the viewport bypassed.
 */

#define RECT_VTX0 F3D_MAX_VERTICES
#define RECT_VTX1 (F3D_MAX_VERTICES + 1)
#define RECT_VTX2 (F3D_MAX_VERTICES + 2)
#define RECT_VTX3 (F3D_MAX_VERTICES + 3)

static void gfx_draw_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    uint32_t saved_other_mode_h = rdp.other_mode_h;
    uint32_t cycle_type = rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE);
    gfx_tnl_drain(); /* rect slots share loaded_vertices with async T&L */
    struct F3DLoadedVertex* ul = &gF3dRsp.loaded_vertices[RECT_VTX0];
    struct F3DLoadedVertex* ll = &gF3dRsp.loaded_vertices[RECT_VTX1];
    struct F3DLoadedVertex* lr = &gF3dRsp.loaded_vertices[RECT_VTX2];
    struct F3DLoadedVertex* ur = &gF3dRsp.loaded_vertices[RECT_VTX3];
    struct XYWidthHeight default_viewport;
    struct XYWidthHeight viewport_saved = rdp.viewport;
    uint32_t geometry_mode_saved = gF3dRsp.geometry_mode;
    float ulxf, ulyf, lrxf, lryf;
    int i;

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = (rdp.other_mode_h & ~(3U << G_MDSFT_TEXTFILT)) | G_TF_POINT;
    }

    /* U10.2 screen coordinates to clip space (y up). */
    ulxf = ulx / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
    ulyf = -(uly / (4.0f * HALF_SCREEN_HEIGHT)) + 1.0f;
    lrxf = lrx / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
    lryf = -(lry / (4.0f * HALF_SCREEN_HEIGHT)) + 1.0f;

    ul->_x = ulxf;
    ul->_y = ulyf;
    ll->_x = ulxf;
    ll->_y = lryf;
    lr->_x = lrxf;
    lr->_y = lryf;
    ur->_x = lrxf;
    ur->_y = ulyf;

    for (i = 0; i < 4; i++) {
        struct F3DLoadedVertex* v = &gF3dRsp.loaded_vertices[F3D_MAX_VERTICES + i];
        v->_z = -1.0f;
        v->_w = 1.0f;
        v->w = 1.0f;
        v->clip_rej = 0x3F;
        v->wlt0 = 0;
    }

    default_viewport.x = 0;
    default_viewport.y = 0;
    default_viewport.width = gfx_current_dimensions.width;
    default_viewport.height = gfx_current_dimensions.height;

    rdp.viewport = default_viewport;
    rdp.viewport_or_scissor_changed = 1;
    gF3dRsp.geometry_mode = 0;

    gfx_sp_tri1(RECT_VTX0, RECT_VTX1, RECT_VTX3);
    gfx_sp_tri1(RECT_VTX1, RECT_VTX2, RECT_VTX3);

    gF3dRsp.geometry_mode = geometry_mode_saved;
    rdp.viewport = viewport_saved;
    rdp.viewport_or_scissor_changed = 1;

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = saved_other_mode_h;
    }
}

static void gfx_dp_texture_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry,
                                     int16_t uls, int16_t ult, int16_t dsdx, int16_t dtdy,
                                     bool flip) {
    uint32_t saved_combine_mode = rdp.combine_mode;
    gfx_tnl_drain(); /* rect slots share loaded_vertices with async T&L */
    struct F3DLoadedVertex* ul = &gF3dRsp.loaded_vertices[RECT_VTX0];
    struct F3DLoadedVertex* ll = &gF3dRsp.loaded_vertices[RECT_VTX1];
    struct F3DLoadedVertex* lr = &gF3dRsp.loaded_vertices[RECT_VTX2];
    struct F3DLoadedVertex* ur = &gF3dRsp.loaded_vertices[RECT_VTX3];
    int16_t width, height;
    float lrs, lrt;

    IE_PERF_INC(IE_PERF_TEXRECTS);
    if ((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {
        /* In copy mode, dsdx is in 4-texel units; the combiner is off. */
        dsdx >>= 2;
        gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_TEXEL0),
                                color_comb(0, 0, 0, G_ACMUX_TEXEL0));
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    if (flip) {
        dsdx = -dsdx;
        dtdy = -dtdy;
    }
    width = !flip ? lrx - ulx : lry - uly;
    height = !flip ? lry - uly : lrx - ulx;
    lrs = ((uls << 7) + dsdx * width) >> 7;
    lrt = ((ult << 7) + dtdy * height) >> 7;

    /* Vertex u/v are in the same S10.5 space as transformed vertices. */
    ul->u = uls;
    ul->v = ult;
    lr->u = lrs;
    lr->v = lrt;
    if (!flip) {
        ll->u = uls;
        ll->v = lrt;
        ur->u = lrs;
        ur->v = ult;
    } else {
        ll->u = lrs;
        ll->v = ult;
        ur->u = uls;
        ur->v = lrt;
    }

    gfx_draw_rectangle(ulx, uly, lrx, lry);
    rdp.combine_mode = saved_combine_mode;
}

static void gfx_dp_fill_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    uint32_t saved_combine_mode = rdp.combine_mode;
    uint32_t mode = rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE);
    int i;

    if (rdp.color_image_address == rdp.z_buf_address) {
        /* Z-buffer clears are the backend's business (start_frame). */
        return;
    }

    if (mode == G_CYC_COPY || mode == G_CYC_FILL) {
        /* One extra pixel is added to each edge in these modes. */
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    for (i = 0; i < 4; i++) {
        struct F3DLoadedVertex* v = &gF3dRsp.loaded_vertices[F3D_MAX_VERTICES + i];
        v->color.r = rdp.fill_color.r;
        v->color.g = rdp.fill_color.g;
        v->color.b = rdp.fill_color.b;
        v->color.a = rdp.fill_color.a;
    }

    gfx_dp_set_combine_mode(color_comb(0, 0, 0, G_CCMUX_SHADE), color_comb(0, 0, 0, G_ACMUX_SHADE));
    gfx_draw_rectangle(ulx, uly, lrx, lry);
    rdp.combine_mode = saved_combine_mode;
}

static void gfx_sp_set_other_mode(uint32_t shift, uint32_t num_bits, uint64_t mode) {
    uint64_t mask = (((uint64_t) 1 << num_bits) - 1) << shift;
    uint64_t om = rdp.other_mode_l | ((uint64_t) rdp.other_mode_h << 32);

    om = (om & ~mask) | mode;
    rdp.other_mode_l = (uint32_t) om;
    rdp.other_mode_h = (uint32_t)(om >> 32);
}

/* ---- display list interpreter ---------------------------------------------- */

static inline void* seg_addr(uintptr_t w1) {
    return segmented_to_virtual((void*) w1);
}

#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1U << width) - 1))
#define C1(pos, width) ((cmd->words.w1 >> (pos)) & ((1U << width) - 1))

/* The previous port injected marker commands into the display stream
 * to steer its backend's blender; they carry no meaning here. */
#define GFX_INHERITED_MARKER_W0 0x424C4E44

static void gfx_run_dl(Gfx* cmd) {
    cmd = (Gfx*) seg_addr((uintptr_t) cmd);
    for (;;) {
        uint32_t opcode = cmd->words.w0 >> 24;

        if (cmd->words.w0 == GFX_INHERITED_MARKER_W0) {
            ++cmd;
            continue;
        }

        switch (opcode) {
            /* RSP commands: */
            case G_MTX:
                gfx_sp_matrix(C0(16, 8), (const float(*)[4]) seg_addr(cmd->words.w1));
                break;

            case (uint8_t) G_POPMTX:
                gfx_sp_pop_matrix(1);
                break;

            case G_MOVEMEM:
                gfx_sp_movemem(C0(16, 8), seg_addr(cmd->words.w1));
                break;

            case (uint8_t) G_MOVEWORD:
                gfx_sp_moveword(C0(0, 8), cmd->words.w1);
                break;

            case (uint8_t) G_TEXTURE:
                gfx_sp_texture(C1(16, 16), C1(0, 16));
                break;

            case G_VTX:
                gfx_sp_vertex(C0(10, 6), C0(16, 8) / 2, (const Vtx*) seg_addr(cmd->words.w1));
                break;

            case G_DL:
                if (C0(16, 1) == 0) {
                    gfx_run_dl((Gfx*) seg_addr(cmd->words.w1));
                } else {
                    cmd = (Gfx*) seg_addr(cmd->words.w1);
                    --cmd; /* increased after break */
                }
                break;

            case (uint8_t) G_ENDDL:
                return;

            case (uint8_t) G_SETGEOMETRYMODE:
                gfx_sp_geometry_mode(0, cmd->words.w1);
                break;

            case (uint8_t) G_CLEARGEOMETRYMODE:
                gfx_sp_geometry_mode(cmd->words.w1, 0);
                break;

            case (uint8_t) G_QUAD:
                gfx_sp_tri1(C1(16, 8) / 2, C1(8, 8) / 2, C1(24, 8) / 2);
                gfx_sp_tri1(C1(8, 8) / 2, C1(0, 8) / 2, C1(24, 8) / 2);
                break;

            case (uint8_t) G_TRI1:
                gfx_sp_tri1(C1(16, 8) / 2, C1(8, 8) / 2, C1(0, 8) / 2);
                break;

            case (uint8_t) G_TRI2:
                gfx_sp_tri1(C0(16, 8) / 2, C0(8, 8) / 2, C0(0, 8) / 2);
                gfx_sp_tri1(C1(16, 8) / 2, C1(8, 8) / 2, C1(0, 8) / 2);
                break;

            case (uint8_t) G_SETOTHERMODE_L:
                gfx_sp_set_other_mode(C0(8, 8), C0(0, 8), cmd->words.w1);
                break;

            case (uint8_t) G_SETOTHERMODE_H:
                gfx_sp_set_other_mode(C0(8, 8) + 32, C0(0, 8), (uint64_t) cmd->words.w1 << 32);
                break;

            /* RDP commands: */
            case G_SETTIMG:
                gfx_dp_set_texture_image(C0(19, 2), C0(0, 10), (const void*) cmd->words.w1);
                break;

            case G_LOADBLOCK:
                gfx_dp_load_block(C1(12, 12));
                break;

            case G_LOADTILE:
                gfx_dp_load_tile(C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;

            case G_SETTILE:
                gfx_dp_set_tile(C0(21, 3), C0(19, 2), C0(9, 9), C0(0, 9), C1(24, 3), C1(18, 2),
                                C1(14, 4), C1(8, 2), C1(4, 4));
                break;

            case G_SETTILESIZE:
                gfx_dp_set_tile_size(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;

            case G_LOADTLUT:
                gfx_dp_load_tlut();
                break;

            case G_SETENVCOLOR:
                gfx_dp_set_env_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;

            case G_SETPRIMCOLOR:
                gfx_dp_set_prim_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;

            case G_SETFOGCOLOR:
                gfx_dp_set_fog_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;

            case G_SETFILLCOLOR:
                gfx_dp_set_fill_color(cmd->words.w1);
                break;

            case G_SETCOMBINE:
                gfx_dp_set_combine_mode(
                    color_comb(C0(20, 4), C1(28, 4), C0(15, 5), C1(15, 3)),
                    color_comb(C0(12, 3), C1(12, 3), C0(9, 3), C1(9, 3)));
                break;

            case G_TEXRECT:
            case G_TEXRECTFLIP: {
                int32_t lrx, lry, ulx, uly;
                uint32_t uls, ult, dsdx, dtdy;

                lrx = C0(12, 12);
                lry = C0(0, 12);
                ulx = C1(12, 12);
                uly = C1(0, 12);
                ++cmd;
                uls = C1(16, 16);
                ult = C1(0, 16);
                ++cmd;
                dsdx = C1(16, 16);
                dtdy = C1(0, 16);
                gfx_dp_texture_rectangle(ulx, uly, lrx, lry, uls, ult, dsdx, dtdy,
                                         opcode == G_TEXRECTFLIP);
                break;
            }

            case G_FILLRECT:
                gfx_dp_fill_rectangle(C1(12, 12), C1(0, 12), C0(12, 12), C0(0, 12));
                break;

            case G_SETSCISSOR:
                gfx_dp_set_scissor(C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;

            case G_SETZIMG:
                rdp.z_buf_address = seg_addr(cmd->words.w1);
                break;

            case G_SETCIMG:
                rdp.color_image_address = seg_addr(cmd->words.w1);
                break;

            default:
                /* Syncs, no-ops, and unknown commands are skipped. */
                break;
        }
        ++cmd;
    }
}

/* ---- public API ------------------------------------------------------------ */

void gfx_init(struct GfxWindowManagerAPI* wapi, struct GfxRenderingAPI* rapi,
              const char* game_name, uint8_t start_in_fullscreen) {
    if (wapi == NULL || rapi == NULL) {
        platform_fatal("gfx_init: no window/rendering backend registered");
    }
#ifdef IE_GFX_SVC
    /* Try the worker first; on success the local backend stays cold and
     * every per-frame entry point below forwards to the service. */
    if (ie_gfx_svc_init()) {
        return;
    }
#endif
    gfx_wapi = wapi;
    gfx_rapi = rapi;
    gfx_wapi->init(game_name, start_in_fullscreen);
    gfx_rapi->init();
    gfx_wapi->get_dimensions(&gfx_current_dimensions.width, &gfx_current_dimensions.height);
    if (gfx_current_dimensions.height == 0) {
        gfx_current_dimensions.height = 1;
    }
    gfx_current_dimensions.aspect_ratio =
        (float) gfx_current_dimensions.width / (float) gfx_current_dimensions.height;
}

struct GfxRenderingAPI* gfx_get_current_rendering_api(void) {
    return gfx_rapi;
}

void gfx_start_frame(void) {
#ifdef IE_GFX_SVC
    if (ie_gfx_svc_active()) {
        return; /* handle_events is an empty stub; nothing to order */
    }
#endif
    gfx_wapi->handle_events();
}

void gfx_run(Gfx* commands) {
#ifdef IE_GFX_SVC
    if (ie_gfx_svc_active()) {
        ie_gfx_svc_frame(commands);
        return;
    }
#endif
    gfx_sp_reset();

    if (!gfx_wapi->start_frame()) {
        dropped_frame = 1;
        return;
    }
    dropped_frame = 0;

    gfx_rapi->start_frame();
    gfx_run_dl(commands);
    gfx_flush();
    gfx_rapi->end_frame();
    gfx_wapi->swap_buffers_begin();
}

void gfx_end_frame(void) {
#ifdef IE_GFX_SVC
    if (ie_gfx_svc_active()) {
        return; /* the worker's swap end paces the frame */
    }
#endif
    if (!dropped_frame) {
        gfx_rapi->finish_render();
        gfx_wapi->swap_buffers_end();
    }
}
