/*
 * IE Voodoo renderer backend: implements the neutral GfxRenderingAPI
 * (src/gfx/gfx_rendering_api.h) on the engine's 3DFX Voodoo SST-1 HLE
 * (register aperture 0xF8000, texture window 0xD0000; authoritative
 * table in the engine's ie_voodoo_abi.tsv).
 *
 * The translator hands over clip-space vertices; this backend performs
 * the perspective divide and viewport transform, then submits screen-
 * space triangles through the HLE's per-vertex extension: writing
 * VOODOO_COLOR_SELECT with a vertex index routes the following
 * VOODOO_START_* attribute writes to that vertex, so no SST-1 gradient
 * setup is needed.
 *
 * Texture memory is a single 64 KiB window, far too small for a whole
 * scene, so every uploaded texture keeps a CPU-side ARGB8888 copy in a
 * high-RAM slot pool and is re-streamed into the window when selected.
 * Combiner emulation is approximate in this subsystem: the first
 * shader colour input drives the vertex colour, textures sample decal;
 * refinement happens at the integration subsystem. Only texture tile 0
 * is honoured.
 *
 * Rewritten fresh against the engine ABI per the plan's salvage policy
 * (renderer integration from the old branch is rewrite-only).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gfx/gfx_rendering_api.h"
#include "gfx/gfx_cc.h"

#include "platform/platform.h"

#include "ie_mmio.h"
#include "ie_memory_layout.h"
#include "ie_perf_counters.h"

/* ---- Voodoo registers (engine voodoo_constants.go / ie_voodoo_abi.tsv) --- */

#define VOODOO_BASE 0x000F8000u
#define VOODOO_STATUS (VOODOO_BASE + 0x000u)
#define VOODOO_ENABLE (VOODOO_BASE + 0x004u)
#define VOODOO_VERTEX_AX (VOODOO_BASE + 0x008u)
#define VOODOO_VERTEX_AY (VOODOO_BASE + 0x00Cu)
#define VOODOO_VERTEX_BX (VOODOO_BASE + 0x010u)
#define VOODOO_VERTEX_BY (VOODOO_BASE + 0x014u)
#define VOODOO_VERTEX_CX (VOODOO_BASE + 0x018u)
#define VOODOO_VERTEX_CY (VOODOO_BASE + 0x01Cu)
#define VOODOO_START_R (VOODOO_BASE + 0x020u)
#define VOODOO_START_G (VOODOO_BASE + 0x024u)
#define VOODOO_START_B (VOODOO_BASE + 0x028u)
#define VOODOO_START_Z (VOODOO_BASE + 0x02Cu)
#define VOODOO_START_A (VOODOO_BASE + 0x030u)
#define VOODOO_START_S (VOODOO_BASE + 0x034u)
#define VOODOO_START_T (VOODOO_BASE + 0x038u)
#define VOODOO_START_W (VOODOO_BASE + 0x03Cu)
#define VOODOO_TRIANGLE_CMD (VOODOO_BASE + 0x080u)
#define VOODOO_COLOR_SELECT (VOODOO_BASE + 0x088u)
#define VOODOO_FBZCOLOR_PATH (VOODOO_BASE + 0x104u)
#define VOODOO_ALPHA_MODE (VOODOO_BASE + 0x10Cu)
#define VOODOO_FBZ_MODE (VOODOO_BASE + 0x110u)
#define VOODOO_CLIP_LEFT_RIGHT (VOODOO_BASE + 0x118u)
#define VOODOO_CLIP_LOW_Y_HIGH (VOODOO_BASE + 0x11Cu)
#define VOODOO_FAST_FILL_CMD (VOODOO_BASE + 0x124u)
#define VOODOO_SWAP_BUFFER_CMD (VOODOO_BASE + 0x128u)
#define VOODOO_COLOR0 (VOODOO_BASE + 0x1D8u)
#define VOODOO_VIDEO_DIM (VOODOO_BASE + 0x214u)
#define VOODOO_TEXTURE_MODE (VOODOO_BASE + 0x300u)
#define VOODOO_TEX_WIDTH (VOODOO_BASE + 0x330u)
#define VOODOO_TEX_HEIGHT (VOODOO_BASE + 0x334u)
#define VOODOO_TEX_UPLOAD (VOODOO_BASE + 0x338u)
#define VOODOO_PALETTE_BASE (VOODOO_BASE + 0x400u)
#define VOODOO_TEXMEM_BASE 0x000D0000u
#define VOODOO_TEXMEM_SIZE 0x00010000u

#define VOODOO_CMD_BUFFER_CAPACITY 4096u

struct VoodooCmd {
    uint32_t addr;
    uint32_t value;
};

static struct VoodooCmd voodoo_cmd_buffer[VOODOO_CMD_BUFFER_CAPACITY];
static uint32_t voodoo_cmd_count;
static uint8_t voodoo_cmd_stream_enabled;
static uint8_t voodoo_tex_slots_enabled;

static inline void vd_raw_mmio_write32(uint32_t addr, uint32_t value) {
    IE_PERF_INC(IE_PERF_MMIO_WRITES);
    ie_mmio_write32(addr, value);
}

static void vd_cmd_flush(void);

static uint8_t vd_cmd_streamable(uint32_t addr) {
    return addr >= VOODOO_BASE && addr <= VOODOO_PALETTE_BASE + 1023u
        && addr != IE_VOODOO_CMD_PTR && addr != IE_VOODOO_CMD_COUNT
        && addr != IE_VOODOO_CMD_SUBMIT;
}

static inline void vd_mmio_write32(uint32_t addr, uint32_t value) {
    if (voodoo_cmd_stream_enabled && vd_cmd_streamable(addr)) {
        if (voodoo_cmd_count == VOODOO_CMD_BUFFER_CAPACITY) {
            vd_cmd_flush();
        }
        voodoo_cmd_buffer[voodoo_cmd_count].addr = addr;
        voodoo_cmd_buffer[voodoo_cmd_count].value = value;
        voodoo_cmd_count++;
        if (addr == VOODOO_SWAP_BUFFER_CMD || addr == VOODOO_TEX_UPLOAD) {
            vd_cmd_flush();
        }
        return;
    }

    if (voodoo_cmd_stream_enabled && addr >= VOODOO_TEXMEM_BASE
        && addr < VOODOO_TEXMEM_BASE + VOODOO_TEXMEM_SIZE) {
        vd_cmd_flush();
    }
    vd_raw_mmio_write32(addr, value);
}

/* Bulk reservation for the triangle emit loop: every register it
 * touches is streamable by construction, so the per-write range and
 * capacity checks in vd_mmio_write32 are pure overhead there. Returns
 * a cursor with room for n pairs, or NULL when the command stream is
 * off (caller falls back to per-write emits). The caller must commit
 * the cursor back via vd_cmd_commit BEFORE anything that can flush
 * (texture streaming, bakes) - a flush zeroes voodoo_cmd_count and
 * would orphan uncommitted pairs. */
static inline struct VoodooCmd* vd_cmd_reserve(uint32_t n) {
    if (!voodoo_cmd_stream_enabled) {
        return NULL;
    }
    if (voodoo_cmd_count + n > VOODOO_CMD_BUFFER_CAPACITY) {
        vd_cmd_flush();
    }
    return &voodoo_cmd_buffer[voodoo_cmd_count];
}

static inline void vd_cmd_commit(const struct VoodooCmd* cursor) {
    voodoo_cmd_count = (uint32_t)(cursor - voodoo_cmd_buffer);
}

static void vd_cmd_flush(void) {
    if (voodoo_cmd_count == 0) {
        return;
    }
    IE_PERF_INC(IE_PERF_CMD_SUBMITS);
    IE_PERF_ADD(IE_PERF_CMD_PAIRS, voodoo_cmd_count);
    vd_raw_mmio_write32(IE_VOODOO_CMD_PTR, ie_guest_addr(voodoo_cmd_buffer));
    vd_raw_mmio_write32(IE_VOODOO_CMD_COUNT, voodoo_cmd_count);
    vd_raw_mmio_write32(IE_VOODOO_CMD_SUBMIT, IE_VOODOO_CMD_SUBMIT_REPLAY);
    voodoo_cmd_count = 0;
}

#define ie_mmio_write32 vd_mmio_write32

/* fbzMode bits */
#define FBZ_CLIPPING (1u << 0)
#define FBZ_ALPHA_PLANES (1u << 18)
#define FBZ_DEPTH_ENABLE (1u << 4)
#define FBZ_DEPTH_FUNC_SHIFT 5
#define FBZ_DEPTH_LESSEQUAL 3u
#define FBZ_DEPTH_ALWAYS 7u
#define FBZ_RGB_WRITE (1u << 9)
#define FBZ_DEPTH_WRITE (1u << 10)

/* alphaMode bits */
#define ALPHA_TEST_EN (1u << 0)
#define ALPHA_TEST_FUNC_SHIFT 1
#define ALPHA_TEST_GREATEREQUAL 6u
#define ALPHA_TEST_REF_SHIFT 24
#define ALPHA_TEST_REF 0x80u
#define ALPHA_BLEND_EN (1u << 4)
#define ALPHA_SRC_RGB_SHIFT 8
#define ALPHA_DST_RGB_SHIFT 12
#define BLEND_SRC_ALPHA 1u
#define BLEND_INV_SRC_A 5u

/* textureMode bits */
#define TEX_ENABLE (1u << 0)
#define TEX_MAGNIFY_BILINEAR (1u << 4)
#define TEX_CLAMP_S (1u << 5)
#define TEX_CLAMP_T (1u << 6)
#define TEX_FMT_ARGB8888 (10u << 8)
#define TEX_PERSPECTIVE (1u << 14)

#define VOODOO_COMBINE_ITERATED 0u
#define VOODOO_COMBINE_TEXTURE 1u
#define VOODOO_COMBINE_MODULATE (1u | (6u << 4))

#define DECAL_Z_BIAS (4.0f / 4096.0f)

enum BakeCombinerKind {
    BAKE_COMBINER_NONE,
    BAKE_COMBINER_KART_TINT,
    BAKE_COMBINER_SMOKE,
};

struct BakeConstants {
    float prim[4];
    float env[4];
};

/* ---- CPU-side texture store ------------------------------------------------
 * Slots live in high RAM (plain guest memory above the low apertures);
 * each holds the ARGB8888 image plus its dimensions. */

#define TEX_STORE_BASE IE_TEX_STORE_BASE
#define TEX_SLOT_BYTES GFX_MAX_TEXTURE_BYTES
#define TEX_SLOT_TEXELS (TEX_SLOT_BYTES / 4u)
#define TEX_PRIVATE_SLOTS 8u /* baked-combiner cache pool (keep IE_TEX_STORE_BYTES in sync) */
#define TEX_MAX_SLOTS (GFX_TEXTURE_CACHE_SIZE + TEX_PRIVATE_SLOTS)

struct TexSlot {
    uint16_t width;
    uint16_t height;
    uint32_t generation;
    uint8_t in_use;
    uint8_t content_valid;
    /* Generation the engine-side slot store holds (tex-slot feature);
     * 0 = never uploaded. When it matches `generation`, a switch to this
     * texture is a single TEX_BIND write instead of a full re-stream. */
    uint32_t engine_generation;
};

static struct TexSlot tex_slots[TEX_MAX_SLOTS];
static uint32_t tex_slot_count;
static uint32_t selected_texture[2];
static int selected_tile;
static uint32_t resident_texture = 0xFFFFFFFFu;
static uint8_t texture_filter_linear;
static uint32_t texture_wrap_s = TEX_CLAMP_S;
static uint32_t texture_wrap_t = TEX_CLAMP_T;
static uint32_t texture_mode_shadow = 0xFFFFFFFFu;

struct ShaderProgram {
    uint32_t shader_id;
    struct CCFeatures cc;
    uint8_t in_use;
};

#define MAX_SHADERS 64

static struct ShaderProgram shaders[MAX_SHADERS];
static struct ShaderProgram* current_shader;

/* Optional mid-frame service hook (the game wires the audio poll here
 * so long frames keep the ring fed; smoke images leave it NULL). */
static void (*vd_flush_hook)(void);

void ie_gfx_voodoo_set_flush_hook(void (*hook)(void)) {
    vd_flush_hook = hook;
}

static struct {
    float x, y, width, height;
} viewport;

/* Depth writes start OFF: the translator's cached depth_mask is 0 at
 * init, so it never sends set_depth_mask(0) for the first non-Z_UPD
 * render mode - a default-on mask would let 2D menu primitives
 * populate the depth buffer and cull later depth-tested geometry. */
static uint32_t fbz_shadow = FBZ_RGB_WRITE;
static uint8_t depth_test_on;
static uint8_t depth_mask_on;
static uint32_t alpha_blend_bits;
static uint32_t alpha_test_bits;
static uint32_t alpha_shadow;
static uint8_t decal_on;
static uint32_t color_path_shadow = 0xFFFFFFFFu;

/* Baked-combiner cache: one entry per private scratch slot. A single
 * entry thrashes whenever a scene alternates tinted sources (the course
 * intro pan sweeps eight tinted karts), re-running the whole per-texel
 * bake loop on every switch; with a pool, switching back to a cached
 * bake is just a texture bind. Round-robin eviction. */
#define BAKE_CACHE_ENTRIES TEX_PRIVATE_SLOTS
static struct {
    uint8_t valid;
    enum BakeCombinerKind kind;
    uint32_t source;
    uint32_t source_generation;
    struct BakeConstants constants;
    uint32_t slot; /* private tex slot; 0xFFFFFFFF until allocated */
} bake_cache[BAKE_CACHE_ENTRIES];
static unsigned bake_cache_next;

#ifdef IE_MMIO_HOST_CAPTURE
static uint32_t host_tex_store[TEX_MAX_SLOTS * TEX_SLOT_TEXELS];
#endif

static uint32_t* tex_slot_pixels(uint32_t slot) {
#ifdef IE_MMIO_HOST_CAPTURE
    return &host_tex_store[slot * TEX_SLOT_TEXELS];
#else
    return (uint32_t*) (uintptr_t)(TEX_STORE_BASE + slot * TEX_SLOT_BYTES);
#endif
}

static void vd_reset_textures(void) {
    memset(tex_slots, 0, sizeof(tex_slots));
    tex_slot_count = 0;
    selected_texture[0] = 0;
    selected_texture[1] = 0;
    selected_tile = 0;
    resident_texture = 0xFFFFFFFFu;
    texture_mode_shadow = 0xFFFFFFFFu;
    memset(bake_cache, 0, sizeof(bake_cache));
    {
        unsigned i;
        for (i = 0; i < BAKE_CACHE_ENTRIES; i++) {
            bake_cache[i].slot = 0xFFFFFFFFu;
        }
    }
    bake_cache_next = 0;
}

#ifdef IE_BOOT_STATUS_DRAWS
static void boot_status_bump_draws(void) {
    uint32_t addr = IE_BOOT_STATUS_ADDR + 9u * 4u;
    ie_mmio_write32(addr, ie_mmio_read32(addr) + 1u);
}
#define BOOT_STATUS_BUMP_DRAWS() boot_status_bump_draws()
#else
#define BOOT_STATUS_BUMP_DRAWS() ((void) 0)
#endif

/* ---- shaders --------------------------------------------------------------- */

static uint8_t vd_z_is_from_0_to_1(void) {
    return 0;
}

static void vd_unload_shader(struct ShaderProgram* old_prg) {
    (void) old_prg;
}

static void vd_load_shader(struct ShaderProgram* new_prg) {
    current_shader = new_prg;
}

static struct ShaderProgram* vd_create_and_load_new_shader(uint32_t shader_id) {
    uint32_t i;

    for (i = 0; i < MAX_SHADERS; i++) {
        if (!shaders[i].in_use) {
            shaders[i].in_use = 1;
            shaders[i].shader_id = shader_id;
            gfx_cc_get_features(shader_id, &shaders[i].cc);
            current_shader = &shaders[i];
            return current_shader;
        }
    }
    platform_fatal("voodoo backend: shader pool exhausted");
}

static struct ShaderProgram* vd_lookup_shader(uint32_t shader_id) {
    uint32_t i;

    for (i = 0; i < MAX_SHADERS; i++) {
        if (shaders[i].in_use && shaders[i].shader_id == shader_id) {
            return &shaders[i];
        }
    }
    return NULL;
}

static void vd_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs,
                               uint8_t used_textures[2]) {
    *num_inputs = prg->cc.num_inputs;
    used_textures[0] = prg->cc.used_textures[0];
    used_textures[1] = prg->cc.used_textures[1];
}

/* ---- textures --------------------------------------------------------------- */

static uint32_t vd_new_texture(void) {
    if (tex_slot_count == TEX_MAX_SLOTS) {
        platform_fatal("voodoo backend: texture slot pool exhausted");
    }
    tex_slots[tex_slot_count].in_use = 1;
    return tex_slot_count++;
}

static void tex_stream_slot(uint32_t slot) {
    const uint32_t* pixels = tex_slot_pixels(slot);
    uint32_t count;
    uint32_t i;

    count = (uint32_t) tex_slots[slot].width * tex_slots[slot].height;
    IE_PERF_ADD(IE_PERF_TEX_STREAM_BYTES, count * 4u);
    if (voodoo_tex_slots_enabled) {
        /* Store engine-side under this slot id as well, so later
         * switches back to it are a single TEX_BIND write. */
        ie_mmio_write32(IE_VOODOO_TEX_SLOT, slot);
    }
    if (voodoo_cmd_stream_enabled) {
        ie_mmio_write32(IE_VOODOO_TEX_SRC_PTR, ie_guest_addr(pixels));
        ie_mmio_write32(IE_VOODOO_TEX_SRC_BYTES, count * 4u);
        ie_mmio_write32(VOODOO_TEX_WIDTH, tex_slots[slot].width);
        ie_mmio_write32(VOODOO_TEX_HEIGHT, tex_slots[slot].height);
        ie_mmio_write32(VOODOO_TEX_UPLOAD, 1);
    } else {
        ie_mmio_write32(VOODOO_TEX_WIDTH, tex_slots[slot].width);
        ie_mmio_write32(VOODOO_TEX_HEIGHT, tex_slots[slot].height);
        for (i = 0; i < count; i++) {
            ie_mmio_write32(VOODOO_TEXMEM_BASE + i * 4u, pixels[i]);
        }
        ie_mmio_write32(VOODOO_TEX_UPLOAD, 1);
    }
    if (voodoo_tex_slots_enabled) {
        ie_mmio_write32(IE_VOODOO_TEX_SLOT, IE_VOODOO_TEX_SLOT_NONE);
        tex_slots[slot].engine_generation = tex_slots[slot].generation;
    }
    resident_texture = slot;
}

static void tex_make_resident(uint32_t slot) {
    if (slot >= tex_slot_count || !tex_slots[slot].in_use || tex_slots[slot].width == 0) {
        return;
    }
    if (resident_texture == slot) {
        return;
    }
    if (voodoo_tex_slots_enabled && tex_slots[slot].engine_generation != 0
        && tex_slots[slot].engine_generation == tex_slots[slot].generation) {
        /* Engine already holds this content: bind by id, no re-stream. */
        ie_mmio_write32(IE_VOODOO_TEX_BIND, slot);
        resident_texture = slot;
        return;
    }

    tex_stream_slot(slot);
}

static void vd_select_texture(int tile, uint32_t texture_id) {
    if (tile < 0 || tile > 1) {
        return;
    }
    selected_tile = tile;
    selected_texture[tile] = texture_id;
    if (tile == 0) {
        tex_make_resident(texture_id);
    }
}

static void vd_upload_texture(const uint8_t* rgba32_buf, int width, int height) {
    /* The contract uploads into the most recently selected tile's
     * texture; writing tile 0's slot unconditionally would let TEXEL1
     * imports corrupt the texture actually being sampled. */
    uint32_t slot = selected_texture[selected_tile];
    uint32_t* store = tex_slot_pixels(slot);
    uint32_t count = (uint32_t) width * (uint32_t) height;
    uint8_t same_content;
    uint32_t i;

    if (count > TEX_SLOT_TEXELS) {
        platform_fatal("voodoo backend: %dx%d texture exceeds slot", width, height);
    }
    same_content = tex_slots[slot].content_valid && tex_slots[slot].width == (uint16_t) width
                 && tex_slots[slot].height == (uint16_t) height;
    for (i = 0; i < count; i++) {
        const uint8_t* p = rgba32_buf + i * 4;
        /* The texture window stores u32 values little-endian into the
         * backend's RGBA byte array: byte 0 = R. */
        uint32_t packed = (uint32_t) p[0] | ((uint32_t) p[1] << 8)
                        | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);

        if (same_content && store[i] != packed) {
            same_content = 0;
        }
        store[i] = packed;
    }

    if (same_content) {
        if (selected_tile == 0) {
            tex_make_resident(slot);
        }
        return;
    }

    tex_slots[slot].width = (uint16_t) width;
    tex_slots[slot].height = (uint16_t) height;
    tex_slots[slot].generation++;
    tex_slots[slot].content_valid = 1;
    if (resident_texture == slot) {
        resident_texture = 0xFFFFFFFFu;
    }
    if (selected_tile == 0) {
        tex_make_resident(slot);
    }
}

static void vd_set_sampler_parameters(int sampler, uint8_t linear_filter, uint32_t cms,
                                      uint32_t cmt) {
    if (sampler != 0) {
        return;
    }
    texture_filter_linear = linear_filter;
    /* G_TX_CLAMP has bit 0 set; wrap (0) maps to repeat. */
    texture_wrap_s = (cms & 1u) ? TEX_CLAMP_S : 0u;
    texture_wrap_t = (cmt & 1u) ? TEX_CLAMP_T : 0u;
}

/* ---- raster state ------------------------------------------------------------ */

static void fbz_update(void) {
    /* Clipping stays on: the clip rectangle defaults to the full
     * screen and set_scissor narrows it; the rasteriser only honours
     * VOODOO_CLIP_* when this bit is set. ALPHA_PLANES keeps fragment
     * alpha alive - without it the rasteriser forces alpha to 1.0 and
     * every cutout texture (menu text, the title logo's transparent
     * surround) renders as an opaque plate. */
    uint32_t mode = FBZ_RGB_WRITE | FBZ_CLIPPING | FBZ_ALPHA_PLANES;

    if (depth_test_on) {
        mode |= FBZ_DEPTH_ENABLE | (FBZ_DEPTH_LESSEQUAL << FBZ_DEPTH_FUNC_SHIFT);
    } else {
        mode |= FBZ_DEPTH_ENABLE | (FBZ_DEPTH_ALWAYS << FBZ_DEPTH_FUNC_SHIFT);
    }
    if (depth_mask_on) {
        mode |= FBZ_DEPTH_WRITE;
    }
    fbz_shadow = mode;
    ie_mmio_write32(VOODOO_FBZ_MODE, mode);
}

static void vd_set_depth_test(uint8_t depth_test) {
    depth_test_on = depth_test;
    fbz_update();
}

static void vd_set_depth_mask(uint8_t z_upd) {
    depth_mask_on = z_upd;
    fbz_update();
}

static void vd_set_zmode_decal(uint8_t zmode_decal) {
    decal_on = zmode_decal != 0;
}

static void vd_set_viewport(int x, int y, int width, int height) {
    viewport.x = (float) x;
    viewport.y = (float) y;
    viewport.width = (float) width;
    viewport.height = (float) height;
}

static void vd_set_scissor(int x, int y, int width, int height) {
    uint32_t left = (uint32_t)(x < 0 ? 0 : x);
    uint32_t top = (uint32_t)(y < 0 ? 0 : y);
    uint32_t right = left + (uint32_t)(width < 0 ? 0 : width);
    uint32_t bottom = top + (uint32_t)(height < 0 ? 0 : height);

    ie_mmio_write32(VOODOO_CLIP_LEFT_RIGHT, (left << 16) | (right & 0xFFFFu));
    ie_mmio_write32(VOODOO_CLIP_LOW_Y_HIGH, (top << 16) | (bottom & 0xFFFFu));
}

static void alpha_mode_update(void) {
    alpha_shadow = alpha_blend_bits | alpha_test_bits;
    ie_mmio_write32(VOODOO_ALPHA_MODE, alpha_shadow);
}

static void vd_set_use_alpha(uint8_t use_alpha) {
    if (use_alpha) {
        alpha_blend_bits = ALPHA_BLEND_EN | (BLEND_SRC_ALPHA << ALPHA_SRC_RGB_SHIFT)
                         | (BLEND_INV_SRC_A << ALPHA_DST_RGB_SHIFT);
    } else {
        alpha_blend_bits = 0;
    }
    alpha_mode_update();
}

static void vd_set_alpha_test(uint8_t texture_edge) {
    uint32_t desired = 0;

    if (texture_edge) {
        desired = ALPHA_TEST_EN | (ALPHA_TEST_GREATEREQUAL << ALPHA_TEST_FUNC_SHIFT)
                | (ALPHA_TEST_REF << ALPHA_TEST_REF_SHIFT);
    }
    if (desired != alpha_test_bits) {
        alpha_test_bits = desired;
        alpha_mode_update();
    }
}

static void vd_set_color_path(uint32_t path) {
    if (path != color_path_shadow) {
        color_path_shadow = path;
        ie_mmio_write32(VOODOO_FBZCOLOR_PATH, path);
    }
}

static void vd_set_texture_mode(uint32_t mode) {
    if (mode != texture_mode_shadow) {
        texture_mode_shadow = mode;
        ie_mmio_write32(VOODOO_TEXTURE_MODE, mode);
    }
}

/* ---- triangle submission ------------------------------------------------------ */

static enum BakeCombinerKind vd_bake_combiner_kind(const struct CCFeatures* cc) {
    if (!cc->opt_alpha || cc->opt_fog || !cc->used_textures[0] || cc->used_textures[1]
        || cc->num_inputs < 2) {
        return BAKE_COMBINER_NONE;
    }

    if (cc->c[0][0] == SHADER_0 && cc->c[0][1] == SHADER_INPUT_1
        && cc->c[0][2] == SHADER_TEXEL0 && cc->c[0][3] == SHADER_INPUT_2
        && cc->c[1][0] == SHADER_INPUT_1 && cc->c[1][1] == SHADER_0
        && cc->c[1][2] == SHADER_TEXEL0 && cc->c[1][3] == SHADER_0) {
        return BAKE_COMBINER_KART_TINT;
    }

    if (cc->c[0][0] == SHADER_INPUT_1 && cc->c[0][1] == SHADER_INPUT_2
        && cc->c[0][2] == SHADER_TEXEL0 && cc->c[0][3] == SHADER_INPUT_2
        && cc->c[1][0] == SHADER_TEXEL0 && cc->c[1][1] == SHADER_0
        && cc->c[1][2] == SHADER_INPUT_1 && cc->c[1][3] == SHADER_0) {
        return BAKE_COMBINER_SMOKE;
    }

    return BAKE_COMBINER_NONE;
}

static uint8_t float_to_texel_byte(float v) {
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return (uint8_t)(v * 255.0f + 0.5f);
}

static uint32_t pack_rgba_texel(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t) r | ((uint32_t) g << 8) | ((uint32_t) b << 16)
         | ((uint32_t) a << 24);
}

static void bake_constants_from_triangle(enum BakeCombinerKind kind, const float* tri,
                                         size_t stride, uint8_t use_fog,
                                         struct BakeConstants* constants) {
    const float* input_base = tri + 4 + 2 + (use_fog ? 4 : 0);
    const float* input1 = input_base;
    const float* input2 = input_base + 4;
    const float* prim = kind == BAKE_COMBINER_SMOKE ? input1 : input2;
    const float* env = kind == BAKE_COMBINER_SMOKE ? input2 : input1;
    int i;

    (void) stride;
    for (i = 0; i < 4; i++) {
        constants->prim[i] = prim[i];
        constants->env[i] = env[i];
    }
}

static uint8_t bake_constants_equal(const struct BakeConstants* a,
                                    const struct BakeConstants* b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

static uint32_t ensure_bake_slot(unsigned entry) {
    if (bake_cache[entry].slot != 0xFFFFFFFFu) {
        return bake_cache[entry].slot;
    }
    if (tex_slot_count == TEX_MAX_SLOTS) {
        platform_fatal("voodoo backend: texture slot pool exhausted");
    }
    bake_cache[entry].slot = tex_slot_count++;
    tex_slots[bake_cache[entry].slot].in_use = 1;
    return bake_cache[entry].slot;
}

static uint8_t bake_combiner_texture(enum BakeCombinerKind kind,
                                     const struct BakeConstants* constants) {
    uint32_t source = selected_texture[0];
    uint32_t scratch;
    unsigned entry;
    const uint32_t* src_pixels;
    uint32_t* dst_pixels;
    uint32_t count;
    uint32_t i;

    if (source >= tex_slot_count || !tex_slots[source].in_use || tex_slots[source].width == 0) {
        return 0;
    }

    {
        unsigned e;
        for (e = 0; e < BAKE_CACHE_ENTRIES; e++) {
            if (bake_cache[e].valid && bake_cache[e].kind == kind
                && bake_cache[e].source == source
                && bake_cache[e].source_generation == tex_slots[source].generation
                && bake_constants_equal(&bake_cache[e].constants, constants)) {
                tex_make_resident(bake_cache[e].slot);
                return 1;
            }
        }
    }

    entry = bake_cache_next;
    bake_cache_next = (bake_cache_next + 1u) % BAKE_CACHE_ENTRIES;
    scratch = ensure_bake_slot(entry);
    tex_slots[scratch].width = tex_slots[source].width;
    tex_slots[scratch].height = tex_slots[source].height;
    src_pixels = tex_slot_pixels(source);
    dst_pixels = tex_slot_pixels(scratch);
    count = (uint32_t) tex_slots[source].width * tex_slots[source].height;

    /* Both combines are out = coef*t + off per channel with constants
     * fixed for the whole texture, so hoist them into Q8 integers and
     * keep the per-texel loop in integer math - float per texel costs
     * an order of magnitude more on the guest, and this loop reruns
     * for every animated source frame (kart sprites re-tint whenever
     * their rotation frame changes). Matches the float version within
     * one LSB. */
    {
        int32_t coef[4], off[4];
        int c;

        for (c = 0; c < 3; c++) {
            if (kind == BAKE_COMBINER_KART_TINT) {
                coef[c] = (int32_t)((1.0f - constants->env[c]) * 256.0f + 0.5f);
                off[c] = (int32_t)(constants->prim[c] * 255.0f + 0.5f);
            } else {
                float cf = (constants->prim[c] - constants->env[c]) * 256.0f;
                coef[c] = (int32_t)(cf + (cf >= 0.0f ? 0.5f : -0.5f));
                off[c] = (int32_t)(constants->env[c] * 255.0f + 0.5f);
            }
        }
        coef[3] = (int32_t)(constants->prim[3] * 256.0f + 0.5f);
        off[3] = 0;

        for (i = 0; i < count; i++) {
            uint32_t p = src_pixels[i];
            int32_t r = ((coef[0] * (int32_t)(p & 0xFFu) + 128) >> 8) + off[0];
            int32_t g = ((coef[1] * (int32_t)((p >> 8) & 0xFFu) + 128) >> 8) + off[1];
            int32_t b = ((coef[2] * (int32_t)((p >> 16) & 0xFFu) + 128) >> 8) + off[2];
            int32_t a = ((coef[3] * (int32_t)(p >> 24) + 128) >> 8) + off[3];

            if (r < 0) { r = 0; } else if (r > 255) { r = 255; }
            if (g < 0) { g = 0; } else if (g > 255) { g = 255; }
            if (b < 0) { b = 0; } else if (b > 255) { b = 255; }
            if (a < 0) { a = 0; } else if (a > 255) { a = 255; }

            dst_pixels[i] = pack_rgba_texel((uint8_t) r, (uint8_t) g, (uint8_t) b, (uint8_t) a);
        }
    }

    bake_cache[entry].valid = 1;
    bake_cache[entry].kind = kind;
    bake_cache[entry].source = source;
    bake_cache[entry].source_generation = tex_slots[source].generation;
    bake_cache[entry].constants = *constants;
    /* New baked content: advance the generation so the slot-bind path
     * re-streams now and can bind without streaming while the bake
     * cache stays valid. */
    tex_slots[scratch].generation++;
    tex_stream_slot(scratch);
    return 1;
}

static uint32_t to_12_4(float v) {
    /* Signed 12.4: the register decodes as a sign-extended int16, so
     * off-screen coordinates keep their sign and the rasteriser clips
     * instead of the edge equations warping. */
    int32_t fixed;

    if (v < -2048.0f) {
        v = -2048.0f;
    }
    if (v > 2047.9375f) {
        v = 2047.9375f;
    }
    fixed = (int32_t)(v * 16.0f);
    return (uint32_t) fixed & 0xFFFFu;
}

static uint32_t to_12_12_color(float v) {
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return (uint32_t)(v * 4096.0f);
}

static void vd_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    BOOT_STATUS_BUMP_DRAWS();
    if (vd_flush_hook != NULL) {
        vd_flush_hook();
    }

    uint8_t num_inputs = 0;
    uint8_t used_textures[2] = { 0, 0 };
    int use_alpha = 0;
    int use_fog = 0;
    uint8_t has_texture_coords = 0;
    enum BakeCombinerKind bake_kind = BAKE_COMBINER_NONE;
    struct BakeConstants current_bake_constants;
    uint8_t have_bake_constants = 0;
    size_t stride;
    size_t color_input_stride;
    float viewport_half_width;
    float viewport_half_height;
    float viewport_center_x;
    float viewport_center_y;
    size_t t;
    const float* v;
    uint8_t cc_term_kind[4];
    uint8_t cc_term_off[4];
    uint8_t cc_alpha_kind[4];
    uint8_t cc_alpha_off[4];

    if (current_shader == NULL) {
        return;
    }
    num_inputs = current_shader->cc.num_inputs;
    used_textures[0] = current_shader->cc.used_textures[0];
    used_textures[1] = current_shader->cc.used_textures[1];
    use_alpha = current_shader->cc.opt_alpha;
    use_fog = current_shader->cc.opt_fog;
    has_texture_coords = used_textures[0] || used_textures[1];
    bake_kind = vd_bake_combiner_kind(&current_shader->cc);
    vd_set_alpha_test(current_shader->cc.opt_texture_edge);

    color_input_stride = use_alpha ? 4u : 3u;
    viewport_half_width = viewport.width * 0.5f;
    viewport_half_height = viewport.height * 0.5f;
    viewport_center_x = viewport.x + viewport_half_width;
    viewport_center_y = viewport.y + viewport_half_height;
    stride = 4 + (has_texture_coords ? 2 : 0) + (use_fog ? 4 : 0)
           + (size_t) num_inputs * color_input_stride;
    if (buf_vbo_num_tris * 3 * stride != buf_vbo_len) {
        platform_fatal("voodoo backend: vbo stride mismatch (%u floats, %u tris)",
                       (unsigned) buf_vbo_len, (unsigned) buf_vbo_num_tris);
    }

    vd_set_texture_mode(
        (used_textures[0] ? (TEX_ENABLE | TEX_FMT_ARGB8888 | TEX_PERSPECTIVE) : 0)
        | (texture_filter_linear ? TEX_MAGNIFY_BILINEAR : 0) | texture_wrap_s
        | texture_wrap_t);
    if (bake_kind != BAKE_COMBINER_NONE) {
        vd_set_color_path(VOODOO_COMBINE_TEXTURE);
    } else if (used_textures[0]) {
        tex_make_resident(selected_texture[0]);
        vd_set_color_path(VOODOO_COMBINE_MODULATE);
    } else {
        vd_set_color_path(VOODOO_COMBINE_ITERATED);
    }

    /* Classify each combiner term's source once per draw call. The
     * per-vertex loop below evaluates (a-b)*c+d over cc->c[0] (colour)
     * and cc->c[1] (alpha); which slot each term reads (an input attr,
     * a texel that resolves to 1.0, or the zero term) depends only on
     * current_shader->cc, not on per-vertex data. Hoisting the
     * SHADER_INPUT/TEXEL range tests out of the inner loop drops eight
     * branch cascades per vertex. kind: 0 zero, 1 input (off = attr
     * offset), 2 one. Bit-exact: the fetched values and arithmetic are
     * unchanged. */
    {
        const struct CCFeatures* cc = &current_shader->cc;
        int ti;
        for (ti = 0; ti < 4; ti++) {
            uint8_t citem = cc->c[0][ti];
            uint8_t aitem = cc->c[1][ti];
            if (citem >= SHADER_INPUT_1 && citem <= SHADER_INPUT_4) {
                cc_term_kind[ti] = 1;
                cc_term_off[ti] =
                    (uint8_t)((size_t)(citem - SHADER_INPUT_1) * color_input_stride);
            } else if (citem == SHADER_TEXEL0 || citem == SHADER_TEXEL0A
                       || citem == SHADER_TEXEL1) {
                cc_term_kind[ti] = 2;
            } else {
                cc_term_kind[ti] = 0;
            }
            if (aitem >= SHADER_INPUT_1 && aitem <= SHADER_INPUT_4) {
                cc_alpha_kind[ti] = 1;
                cc_alpha_off[ti] =
                    (uint8_t)((size_t)(aitem - SHADER_INPUT_1) * color_input_stride);
            } else if (aitem == SHADER_TEXEL0 || aitem == SHADER_TEXEL0A
                       || aitem == SHADER_TEXEL1) {
                cc_alpha_kind[ti] = 2;
            } else {
                cc_alpha_kind[ti] = 0;
            }
        }
    }

    v = buf_vbo;
    for (t = 0; t < buf_vbo_num_tris; t++) {
        float tri_w[3];
        float wref;
        int k;
        uint8_t baked_triangle = 0;

        /* Near-plane rejection: the translator does not clip (the
         * console's RSP did), so a vertex at or behind the camera
         * (w <= 0) would otherwise project into a giant inverted
         * screen-covering triangle. Dropping the whole triangle is a
         * crude near clip - slivers vanish at the screen edge instead
         * of being partially drawn - but keeps the frame sane. */
        int rejected = 0;

        for (k = 0; k < 3; k++) {
            float w = v[(size_t) k * stride + 3];

            if (w < 0.001f) {
                rejected = 1;
                break;
            }
            tri_w[k] = w;
        }
        if (rejected) {
            v += 3 * stride;
            continue;
        }

        if (bake_kind != BAKE_COMBINER_NONE) {
            struct BakeConstants constants;

            bake_constants_from_triangle(bake_kind, v, stride, (uint8_t) use_fog, &constants);
            if (!have_bake_constants
                || !bake_constants_equal(&current_bake_constants, &constants)) {
                if (bake_combiner_texture(bake_kind, &constants)) {
                    current_bake_constants = constants;
                    have_bake_constants = 1;
                } else {
                    bake_kind = BAKE_COMBINER_NONE;
                    tex_make_resident(selected_texture[0]);
                    vd_set_color_path(VOODOO_COMBINE_MODULATE);
                }
            }
            baked_triangle = bake_kind != BAKE_COMBINER_NONE;
        }

        /* Bulk-reserve this triangle's register pairs (11 per vertex +
         * the triangle command). Reserved after the bake block: a bake
         * streams texture data and flushes the command buffer, which
         * would orphan an earlier cursor. */
        {
        struct VoodooCmd* c = vd_cmd_reserve(34u);
#define VD_EMIT(a, val) \
        do { \
            if (c != NULL) { c->addr = (a); c->value = (val); c++; } \
            else { ie_mmio_write32((a), (val)); } \
        } while (0)

        /* Perspective texturing wants per-vertex W; the rasteriser's
         * formula is homogeneous in W's scale, so normalise by the
         * triangle's smallest w to keep 1/w inside the register's
         * 2.30 range. */
        wref = tri_w[0];
        if (tri_w[1] < wref) {
            wref = tri_w[1];
        }
        if (tri_w[2] < wref) {
            wref = tri_w[2];
        }

        for (k = 0; k < 3; k++) {
            const float* p = v + (size_t) k * stride;
            float w = tri_w[k];
            float inv_w = 1.0f / w;
            float ndc_x = p[0] * inv_w;
            float ndc_y = p[1] * inv_w;
            float ndc_z = p[2] * inv_w;
            float sx = viewport_center_x + ndc_x * viewport_half_width;
            float sy = viewport_center_y - ndc_y * viewport_half_height;
            float zval = ndc_z * 0.5f + 0.5f;
            const float* attr = p + 4;
            float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

            VD_EMIT(VOODOO_COLOR_SELECT, (uint32_t) k);
            /* W = wref/w in 2.30: 1.0 at the triangle's nearest
             * vertex; the rasteriser's perspective divide needs it on
             * the COLOR_SELECT path (it defaults to 1.0 = affine). */
            VD_EMIT(VOODOO_START_W,
                    (uint32_t)((wref * inv_w) * 1073741824.0f));
            switch (k) {
                case 0:
                    VD_EMIT(VOODOO_VERTEX_AX, to_12_4(sx));
                    VD_EMIT(VOODOO_VERTEX_AY, to_12_4(sy));
                    break;
                case 1:
                    VD_EMIT(VOODOO_VERTEX_BX, to_12_4(sx));
                    VD_EMIT(VOODOO_VERTEX_BY, to_12_4(sy));
                    break;
                default:
                    VD_EMIT(VOODOO_VERTEX_CX, to_12_4(sx));
                    VD_EMIT(VOODOO_VERTEX_CY, to_12_4(sy));
                    break;
            }

            if (has_texture_coords) {
                float s_coord = attr[0];
                float t_coord = attr[1];
                VD_EMIT(VOODOO_START_S, (uint32_t)(int32_t)(s_coord * 262144.0f));
                VD_EMIT(VOODOO_START_T, (uint32_t)(int32_t)(t_coord * 262144.0f));
                attr += 2;
            }
            if (use_fog) {
                attr += 4; /* fog colour+intensity: integration follow-up */
            }
            if (baked_triangle) {
                r = g = b = a = 1.0f;
            } else if (num_inputs > 0) {
                /*
                 * Evaluate the N64 colour combiner (a-b)*c+d per vertex
                 * over the translator's per-vertex inputs. Texel items
                 * evaluate as white here: the rasteriser's default
                 * combine multiplies vertex colour by the sampled texel
                 * (or white when untextured), so modulate/decal cases
                 * compose correctly and input-only combiners like the
                 * checkered flag's PRIMITIVE*SHADE come out exact.
                 */
                float term[4][4];
                int ti, ch;

                for (ti = 0; ti < 4; ti++) {
                    if (cc_term_kind[ti] == 1) {
                        const float* in = attr + cc_term_off[ti];
                        term[ti][0] = in[0];
                        term[ti][1] = in[1];
                        term[ti][2] = in[2];
                        term[ti][3] = use_alpha ? in[3] : 1.0f;
                    } else if (cc_term_kind[ti] == 2) {
                        term[ti][0] = term[ti][1] = term[ti][2] = term[ti][3] = 1.0f;
                    } else {
                        term[ti][0] = term[ti][1] = term[ti][2] = term[ti][3] = 0.0f;
                    }
                }
                for (ch = 0; ch < 3; ch++) {
                    float v = (term[0][ch] - term[1][ch]) * term[2][ch] + term[3][ch];
                    if (ch == 0) {
                        r = v;
                    } else if (ch == 1) {
                        g = v;
                    } else {
                        b = v;
                    }
                }
                if (use_alpha) {
                    /* Alpha: common cases are a plain input or texel in
                     * the d slot; evaluate the same formula on c[1]. */
                    float at[4];
                    for (ti = 0; ti < 4; ti++) {
                        if (cc_alpha_kind[ti] == 1) {
                            at[ti] = attr[cc_alpha_off[ti] + 3];
                        } else if (cc_alpha_kind[ti] == 2) {
                            at[ti] = 1.0f;
                        } else {
                            at[ti] = 0.0f;
                        }
                    }
                    a = (at[0] - at[1]) * at[2] + at[3];
                } else {
                    a = 1.0f;
                }
            }

            VD_EMIT(VOODOO_START_R, to_12_12_color(r));
            VD_EMIT(VOODOO_START_G, to_12_12_color(g));
            VD_EMIT(VOODOO_START_B, to_12_12_color(b));
            VD_EMIT(VOODOO_START_A, to_12_12_color(a));
            if (decal_on) {
                zval -= DECAL_Z_BIAS;
            }
            if (zval < 0.0f) {
                zval = 0.0f;
            }
            if (zval > 1.0f) {
                zval = 1.0f;
            }
            VD_EMIT(VOODOO_START_Z, (uint32_t)(zval * 4096.0f));
        }
        VD_EMIT(VOODOO_TRIANGLE_CMD, 0);
        if (c != NULL) {
            vd_cmd_commit(c);
        }
#undef VD_EMIT
        }
        v += 3 * stride;
    }
}

/* ---- frame lifecycle ----------------------------------------------------------- */

static void vd_init(void) {
    voodoo_cmd_count = 0;
#ifdef IE_NO_CMDSTREAM
    voodoo_cmd_stream_enabled = 0;
#else
    voodoo_cmd_stream_enabled =
        ie_feature_available(IE_SYSINFO_FEATURE_VOODOO_CMD_STREAM);
#endif
    voodoo_tex_slots_enabled =
        ie_feature_available(IE_SYSINFO_FEATURE_VOODOO_TEX_SLOTS);
    ie_mmio_write32(VOODOO_ENABLE, 1);
    ie_mmio_write32(VOODOO_VIDEO_DIM, (640u << 16) | 480u);
    vd_reset_textures();
    color_path_shadow = 0xFFFFFFFFu;
    fbz_update();
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 640.0f;
    viewport.height = 480.0f;
    vd_cmd_flush();
}

static void vd_on_resize(void) {
}

static void vd_start_frame(void) {
    /* Clear colour and depth for the new frame. */
    ie_mmio_write32(VOODOO_COLOR0, 0xFF000000u);
    ie_mmio_write32(VOODOO_FAST_FILL_CMD, 0);
}

static void vd_end_frame(void) {
    ie_mmio_write32(VOODOO_SWAP_BUFFER_CMD, 0);
}

static void vd_finish_render(void) {
    vd_cmd_flush();
}

struct GfxRenderingAPI ie_gfx_voodoo_api = {
    vd_z_is_from_0_to_1,
    vd_unload_shader,
    vd_load_shader,
    vd_create_and_load_new_shader,
    vd_lookup_shader,
    vd_shader_get_info,
    vd_new_texture,
    vd_select_texture,
    vd_upload_texture,
    vd_set_sampler_parameters,
    vd_set_depth_test,
    vd_set_depth_mask,
    vd_set_zmode_decal,
    vd_set_viewport,
    vd_set_scissor,
    vd_set_use_alpha,
    vd_draw_triangles,
    vd_init,
    vd_on_resize,
    vd_start_frame,
    vd_end_frame,
    vd_finish_render,
};
