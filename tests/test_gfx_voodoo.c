/*
 * Host tests for the Intuition Engine Voodoo backend. IE_MMIO_HOST_CAPTURE
 * routes register writes into the capture functions below, so the backend
 * can be characterised without mapping the engine apertures on the host.
 */
#include "test_support.h"

#include <stdarg.h>
#include <stdint.h>

#include "gfx/gfx_cc.h"
#include "gfx/gfx_rendering_api.h"
#include "ie/ie_mmio.h"
#include "ie/ie_memory_layout.h"
#include "ie/ie_perf_counters.h"

extern struct GfxRenderingAPI ie_gfx_voodoo_api;

#define VOODOO_BASE 0x000F8000u
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
#define VOODOO_TEXTURE_MODE (VOODOO_BASE + 0x300u)
#define VOODOO_TEX_WIDTH (VOODOO_BASE + 0x330u)
#define VOODOO_TEX_HEIGHT (VOODOO_BASE + 0x334u)
#define VOODOO_TEX_UPLOAD (VOODOO_BASE + 0x338u)
#define VOODOO_TEXMEM_BASE 0x000D0000u
#define VOODOO_TEXMEM_SIZE 0x00010000u

#define FBZ_CLIPPING (1u << 0)
#define FBZ_ALPHA_PLANES (1u << 18)
#define FBZ_DEPTH_ENABLE (1u << 4)
#define FBZ_DEPTH_FUNC_SHIFT 5
#define FBZ_DEPTH_LESSEQUAL 3u
#define FBZ_DEPTH_ALWAYS 7u
#define FBZ_RGB_WRITE (1u << 9)
#define FBZ_DEPTH_WRITE (1u << 10)

#define ALPHA_BLEND_EN (1u << 4)
#define ALPHA_SRC_RGB_SHIFT 8
#define ALPHA_DST_RGB_SHIFT 12
#define ALPHA_TEST_EN (1u << 0)
#define ALPHA_TEST_FUNC_SHIFT 1
#define ALPHA_TEST_GREATEREQUAL 6u
#define ALPHA_TEST_REF_SHIFT 24
#define ALPHA_TEST_REF 0x80u
#define BLEND_SRC_ALPHA 1u
#define BLEND_INV_SRC_A 5u

#define TEX_ENABLE (1u << 0)
#define TEX_MAGNIFY_BILINEAR (1u << 4)
#define TEX_CLAMP_S (1u << 5)
#define TEX_CLAMP_T (1u << 6)
#define TEX_FMT_ARGB8888 (10u << 8)
#define TEX_PERSPECTIVE (1u << 14)

#define VOODOO_COMBINE_ITERATED 0u
#define VOODOO_COMBINE_TEXTURE 1u
#define VOODOO_COMBINE_MODULATE (1u | (6u << 4))

#define MAX_WRITES (GFX_MAX_TEXTURE_TEXELS + 4096u)

struct MmioWrite {
    uint32_t addr;
    uint32_t value;
};

static struct MmioWrite writes[MAX_WRITES];
static struct MmioWrite raw_writes[MAX_WRITES];
static size_t num_writes;
static size_t num_raw_writes;
static uint32_t boot_status[16];
static uint32_t texmem[VOODOO_TEXMEM_SIZE / 4u];
static uint32_t host_features = IE_SYSINFO_FEATURE_VOODOO_CMD_STREAM;
static uint32_t cmd_ptr;
static uint32_t cmd_count;
static uint32_t tex_src_ptr;
static uint32_t tex_src_bytes;
static size_t cmd_submit_count;
static size_t cmd_pair_count;

#define HOST_GUEST_BASE 0x80000000u
#define HOST_GUEST_MAX_MAPPINGS 64u

struct HostGuestMapping {
    uint32_t addr;
    const void* ptr;
};

static struct HostGuestMapping host_guest_mappings[HOST_GUEST_MAX_MAPPINGS];
static size_t host_guest_mapping_count;

void platform_log(const char* fmt, ...) {
    (void) fmt;
}

void platform_fatal(const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    printf("platform_fatal called: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    exit(2);
}

uint32_t ie_mmio_host_guest_addr(const void* ptr) {
    size_t i;

    if (ptr == NULL) {
        return 0;
    }
    for (i = 0; i < host_guest_mapping_count; i++) {
        if (host_guest_mappings[i].ptr == ptr) {
            return host_guest_mappings[i].addr;
        }
    }
    if (host_guest_mapping_count == HOST_GUEST_MAX_MAPPINGS) {
        platform_fatal("host guest mapping table exhausted");
    }
    host_guest_mappings[host_guest_mapping_count].addr =
        HOST_GUEST_BASE + (uint32_t) host_guest_mapping_count * 0x10000u;
    host_guest_mappings[host_guest_mapping_count].ptr = ptr;
    host_guest_mapping_count++;
    return host_guest_mappings[host_guest_mapping_count - 1u].addr;
}

static const void* host_resolve_guest_addr(uint32_t addr) {
    size_t i;

    for (i = 0; i < host_guest_mapping_count; i++) {
        if (host_guest_mappings[i].addr == addr) {
            return host_guest_mappings[i].ptr;
        }
    }
    printf("FAIL %s:%d: unresolved guest addr 0x%08x\n", __FILE__, __LINE__, addr);
    test_failures++;
    return NULL;
}

static void record_raw_write(uint32_t addr, uint32_t value) {
    if (num_raw_writes < MAX_WRITES) {
        raw_writes[num_raw_writes].addr = addr;
        raw_writes[num_raw_writes].value = value;
        num_raw_writes++;
    }
}

static void apply_device_write32(uint32_t addr, uint32_t value) {
    if (num_writes < MAX_WRITES) {
        writes[num_writes].addr = addr;
        writes[num_writes].value = value;
        num_writes++;
    }
    if (addr == IE_VOODOO_TEX_SRC_PTR) {
        tex_src_ptr = value;
    }
    if (addr == IE_VOODOO_TEX_SRC_BYTES) {
        tex_src_bytes = value;
    }
    if (addr == VOODOO_TEX_UPLOAD && tex_src_ptr != 0 && tex_src_bytes != 0) {
        const uint32_t* pixels = host_resolve_guest_addr(tex_src_ptr);
        uint32_t count = tex_src_bytes / 4u;

        if (count > VOODOO_TEXMEM_SIZE / 4u) {
            count = VOODOO_TEXMEM_SIZE / 4u;
        }
        if (pixels != NULL) {
            memcpy(texmem, pixels, count * sizeof(texmem[0]));
        }
    }
    if (addr >= VOODOO_TEXMEM_BASE && addr < VOODOO_TEXMEM_BASE + VOODOO_TEXMEM_SIZE) {
        texmem[(addr - VOODOO_TEXMEM_BASE) / 4u] = value;
    }
    if (addr >= IE_BOOT_STATUS_ADDR && addr < IE_BOOT_STATUS_ADDR + sizeof(boot_status)) {
        boot_status[(addr - IE_BOOT_STATUS_ADDR) / 4u] = value;
    }
}

void ie_mmio_host_write32(uint32_t addr, uint32_t value) {
    record_raw_write(addr, value);
    if (addr == IE_VOODOO_CMD_PTR) {
        cmd_ptr = value;
        return;
    }
    if (addr == IE_VOODOO_CMD_COUNT) {
        cmd_count = value;
        return;
    }
    if (addr == IE_VOODOO_CMD_SUBMIT && value == IE_VOODOO_CMD_SUBMIT_REPLAY) {
        const struct MmioWrite* cmds = host_resolve_guest_addr(cmd_ptr);
        uint32_t i;

        cmd_submit_count++;
        cmd_pair_count += cmd_count;
        if (cmds != NULL) {
            for (i = 0; i < cmd_count; i++) {
                apply_device_write32(cmds[i].addr, cmds[i].value);
            }
        }
        cmd_count = 0;
        return;
    }
    apply_device_write32(addr, value);
}

uint32_t ie_mmio_host_read32(uint32_t addr) {
    if (addr == IE_SYSINFO_FEATURES) {
        return host_features;
    }
    if (addr >= VOODOO_TEXMEM_BASE && addr < VOODOO_TEXMEM_BASE + VOODOO_TEXMEM_SIZE) {
        return texmem[(addr - VOODOO_TEXMEM_BASE) / 4u];
    }
    if (addr >= IE_BOOT_STATUS_ADDR && addr < IE_BOOT_STATUS_ADDR + sizeof(boot_status)) {
        return boot_status[(addr - IE_BOOT_STATUS_ADDR) / 4u];
    }
    return 0;
}

void ie_mmio_host_bulk_texture_upload(const uint32_t* pixels, uint32_t count) {
    if (count > VOODOO_TEXMEM_SIZE / 4u) {
        count = VOODOO_TEXMEM_SIZE / 4u;
    }
    memcpy(texmem, pixels, count * sizeof(texmem[0]));
}

static void reset_capture(void) {
    ie_gfx_voodoo_api.finish_render();
    num_writes = 0;
    num_raw_writes = 0;
    cmd_ptr = 0;
    cmd_count = 0;
    tex_src_ptr = 0;
    tex_src_bytes = 0;
    cmd_submit_count = 0;
    cmd_pair_count = 0;
    ie_perf_reset();
}

static void set_cmd_stream_capable(uint8_t capable) {
    if (capable) {
        host_features |= IE_SYSINFO_FEATURE_VOODOO_CMD_STREAM;
    } else {
        host_features &= ~IE_SYSINFO_FEATURE_VOODOO_CMD_STREAM;
    }
}

static uint8_t cmdstream_compiled(void) {
#ifdef IE_NO_CMDSTREAM
    return 0;
#else
    return 1;
#endif
}

static uint32_t last_value_for(uint32_t addr) {
    size_t i;

    for (i = num_writes; i > 0; i--) {
        if (writes[i - 1].addr == addr) {
            return writes[i - 1].value;
        }
    }
    printf("FAIL %s:%d: no write for addr 0x%08x\n", __FILE__, __LINE__, addr);
    test_failures++;
    return 0;
}

static size_t count_writes_for(uint32_t addr) {
    size_t i;
    size_t count = 0;

    for (i = 0; i < num_writes; i++) {
        if (writes[i].addr == addr) {
            count++;
        }
    }
    return count;
}

static size_t count_raw_writes_for(uint32_t addr) {
    size_t i;
    size_t count = 0;

    for (i = 0; i < num_raw_writes; i++) {
        if (raw_writes[i].addr == addr) {
            count++;
        }
    }
    return count;
}

static void test_pack_data_window_does_not_overlap_texture_store(void) {
    EXPECT_EQ_INT(IE_PACK_DATA_MEM_BASE, 0x01000000u);
    EXPECT_EQ_INT(IE_PACK_DATA_MEM_LIMIT, IE_TEX_STORE_BASE);
    EXPECT_TRUE(!ie_ranges_overlap(IE_PACK_DATA_MEM_BASE, IE_PACK_DATA_MEM_LIMIT,
                                   IE_TEX_STORE_BASE, IE_TEX_STORE_LIMIT));
    EXPECT_TRUE(IE_TEX_STORE_LIMIT <= IE_GAME_BASE);
}

static void expect_stream_submit(uint32_t pairs) {
    EXPECT_EQ_INT(cmd_submit_count, 1);
    EXPECT_EQ_INT(cmd_pair_count, pairs);
    EXPECT_EQ_INT(count_raw_writes_for(IE_VOODOO_CMD_PTR), 1);
    EXPECT_EQ_INT(count_raw_writes_for(IE_VOODOO_CMD_COUNT), 1);
    EXPECT_EQ_INT(count_raw_writes_for(IE_VOODOO_CMD_SUBMIT), 1);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_SUBMITS), 1);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_PAIRS), pairs);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_MMIO_WRITES), 3);
}

static size_t find_nth_write_for(uint32_t addr, size_t nth) {
    size_t i;
    size_t count = 0;

    for (i = 0; i < num_writes; i++) {
        if (writes[i].addr == addr) {
            if (count == nth) {
                return i;
            }
            count++;
        }
    }
    printf("FAIL %s:%d: no write %zu for addr 0x%08x\n", __FILE__, __LINE__, nth, addr);
    test_failures++;
    return num_writes;
}

static void expect_next_write(size_t* cursor, uint32_t addr, uint32_t value) {
    if (*cursor >= num_writes) {
        printf("FAIL %s:%d: missing write addr 0x%08x value 0x%08x\n", __FILE__, __LINE__,
               addr, value);
        test_failures++;
        return;
    }
    EXPECT_EQ_INT(writes[*cursor].addr, addr);
    EXPECT_EQ_INT(writes[*cursor].value, value);
    (*cursor)++;
}

static uint32_t fbz_word(uint8_t depth_test, uint8_t depth_mask) {
    uint32_t mode = FBZ_RGB_WRITE | FBZ_CLIPPING | FBZ_ALPHA_PLANES | FBZ_DEPTH_ENABLE;

    mode |= (depth_test ? FBZ_DEPTH_LESSEQUAL : FBZ_DEPTH_ALWAYS) << FBZ_DEPTH_FUNC_SHIFT;
    if (depth_mask) {
        mode |= FBZ_DEPTH_WRITE;
    }
    return mode;
}

static uint32_t alpha_blend_word(void) {
    return ALPHA_BLEND_EN | (BLEND_SRC_ALPHA << ALPHA_SRC_RGB_SHIFT)
         | (BLEND_INV_SRC_A << ALPHA_DST_RGB_SHIFT);
}

static uint32_t alpha_test_word(void) {
    return ALPHA_TEST_EN | (ALPHA_TEST_GREATEREQUAL << ALPHA_TEST_FUNC_SHIFT)
         | (ALPHA_TEST_REF << ALPHA_TEST_REF_SHIFT);
}

static uint32_t texture_shader_id(uint32_t opts) {
    return (SHADER_TEXEL0 << 6) | (SHADER_INPUT_1 << 9)
         | (SHADER_INPUT_1 << (12 + 9)) | opts;
}

static uint32_t shader_id_from_terms(uint8_t c0, uint8_t c1, uint8_t c2, uint8_t c3,
                                     uint8_t a0, uint8_t a1, uint8_t a2, uint8_t a3,
                                     uint32_t opts) {
    return ((uint32_t) c0 << 0) | ((uint32_t) c1 << 3) | ((uint32_t) c2 << 6)
         | ((uint32_t) c3 << 9) | ((uint32_t) a0 << 12) | ((uint32_t) a1 << 15)
         | ((uint32_t) a2 << 18) | ((uint32_t) a3 << 21) | opts;
}

static uint32_t kart_tint_shader_id(uint32_t opts) {
    return shader_id_from_terms(SHADER_0, SHADER_INPUT_1, SHADER_TEXEL0,
                                SHADER_INPUT_2, SHADER_INPUT_1, SHADER_0,
                                SHADER_TEXEL0, SHADER_0, opts);
}

static uint32_t particle_smoke_shader_id(uint32_t opts) {
    return shader_id_from_terms(SHADER_INPUT_1, SHADER_INPUT_2, SHADER_TEXEL0,
                                SHADER_INPUT_2, SHADER_TEXEL0, SHADER_0,
                                SHADER_INPUT_1, SHADER_0, opts);
}

static void draw_textured_alpha_triangle_z(uint32_t opts, float z) {
    struct ShaderProgram* shader = ie_gfx_voodoo_api.create_and_load_new_shader(
        texture_shader_id(SHADER_OPT_ALPHA | opts));
    float vbo[] = {
        -0.5f, -0.5f, z, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
         0.5f, -0.5f, z, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f,
         0.0f,  0.5f, z, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.25f,
    };

    ie_gfx_voodoo_api.load_shader(shader);
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();
}

static void draw_textured_alpha_triangle(uint32_t opts) {
    draw_textured_alpha_triangle_z(opts, 0.0f);
}

static void fill_two_input_textured_vbo(float vbo[42], const uint8_t prim[4],
                                        const uint8_t env[4], int input1_is_prim) {
    static const float verts[3][6] = {
        { -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
        { 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
    };
    const uint8_t* input1 = input1_is_prim ? prim : env;
    const uint8_t* input2 = input1_is_prim ? env : prim;
    int i;

    for (i = 0; i < 3; i++) {
        size_t o = (size_t) i * 14u;

        vbo[o + 0] = verts[i][0];
        vbo[o + 1] = verts[i][1];
        vbo[o + 2] = verts[i][2];
        vbo[o + 3] = verts[i][3];
        vbo[o + 4] = verts[i][4];
        vbo[o + 5] = verts[i][5];
        vbo[o + 6] = input1[0] / 255.0f;
        vbo[o + 7] = input1[1] / 255.0f;
        vbo[o + 8] = input1[2] / 255.0f;
        vbo[o + 9] = input1[3] / 255.0f;
        vbo[o + 10] = input2[0] / 255.0f;
        vbo[o + 11] = input2[1] / 255.0f;
        vbo[o + 12] = input2[2] / 255.0f;
        vbo[o + 13] = input2[3] / 255.0f;
    }
}

static uint8_t formula_byte(float v) {
    if (v < 0.0f) {
        v = 0.0f;
    }
    if (v > 1.0f) {
        v = 1.0f;
    }
    return (uint8_t)(v * 255.0f + 0.5f);
}

static uint32_t rgba_word(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t) r | ((uint32_t) g << 8) | ((uint32_t) b << 16)
         | ((uint32_t) a << 24);
}

static uint32_t expected_kart_texel(const uint8_t texel[4], const uint8_t prim[4],
                                    const uint8_t env[4]) {
    float tr = texel[0] / 255.0f;
    float tg = texel[1] / 255.0f;
    float tb = texel[2] / 255.0f;
    float ta = texel[3] / 255.0f;
    float pr = prim[0] / 255.0f;
    float pg = prim[1] / 255.0f;
    float pb = prim[2] / 255.0f;
    float pa = prim[3] / 255.0f;
    float er = env[0] / 255.0f;
    float eg = env[1] / 255.0f;
    float eb = env[2] / 255.0f;

    return rgba_word(formula_byte((1.0f - er) * tr + pr),
                     formula_byte((1.0f - eg) * tg + pg),
                     formula_byte((1.0f - eb) * tb + pb), formula_byte(pa * ta));
}

static uint32_t expected_smoke_texel(const uint8_t texel[4], const uint8_t prim[4],
                                     const uint8_t env[4]) {
    float tr = texel[0] / 255.0f;
    float tg = texel[1] / 255.0f;
    float tb = texel[2] / 255.0f;
    float ta = texel[3] / 255.0f;
    float pr = prim[0] / 255.0f;
    float pg = prim[1] / 255.0f;
    float pb = prim[2] / 255.0f;
    float pa = prim[3] / 255.0f;
    float er = env[0] / 255.0f;
    float eg = env[1] / 255.0f;
    float eb = env[2] / 255.0f;

    return rgba_word(formula_byte((pr - er) * tr + er),
                     formula_byte((pg - eg) * tg + eg),
                     formula_byte((pb - eb) * tb + eb), formula_byte(ta * pa));
}

static uint32_t upload_test_texture(const uint8_t* texels, int width, int height) {
    uint32_t texture = ie_gfx_voodoo_api.new_texture();

    ie_gfx_voodoo_api.select_texture(0, texture);
    ie_gfx_voodoo_api.upload_texture(texels, width, height);
    return texture;
}

static void test_fbz_depth_state_words(void) {
    ie_gfx_voodoo_api.init();

    reset_capture();
    ie_gfx_voodoo_api.set_depth_test(0);
    ie_gfx_voodoo_api.set_depth_mask(0);
    ie_gfx_voodoo_api.finish_render();
    EXPECT_EQ_INT(last_value_for(VOODOO_FBZ_MODE), fbz_word(0, 0));

    reset_capture();
    ie_gfx_voodoo_api.set_depth_test(0);
    ie_gfx_voodoo_api.set_depth_mask(1);
    ie_gfx_voodoo_api.finish_render();
    EXPECT_EQ_INT(last_value_for(VOODOO_FBZ_MODE), fbz_word(0, 1));

    reset_capture();
    ie_gfx_voodoo_api.set_depth_test(1);
    ie_gfx_voodoo_api.set_depth_mask(0);
    ie_gfx_voodoo_api.finish_render();
    EXPECT_EQ_INT(last_value_for(VOODOO_FBZ_MODE), fbz_word(1, 0));

    reset_capture();
    ie_gfx_voodoo_api.set_depth_test(1);
    ie_gfx_voodoo_api.set_depth_mask(1);
    ie_gfx_voodoo_api.finish_render();
    EXPECT_EQ_INT(last_value_for(VOODOO_FBZ_MODE), fbz_word(1, 1));
}

static void test_alpha_blend_state_word(void) {
    reset_capture();
    ie_gfx_voodoo_api.set_use_alpha(0);
    ie_gfx_voodoo_api.finish_render();
    EXPECT_EQ_INT(last_value_for(VOODOO_ALPHA_MODE), 0);

    reset_capture();
    ie_gfx_voodoo_api.set_use_alpha(1);
    ie_gfx_voodoo_api.finish_render();
    EXPECT_EQ_INT(last_value_for(VOODOO_ALPHA_MODE), alpha_blend_word());
}

static void test_texture_upload_residency(void) {
    const uint8_t texel[4] = { 0x12, 0x34, 0x56, 0x78 };
    uint32_t texture = ie_gfx_voodoo_api.new_texture();

    ie_gfx_voodoo_api.select_texture(0, texture);
    reset_capture();
    ie_gfx_voodoo_api.upload_texture(texel, 1, 1);

    if (cmdstream_compiled()) {
        EXPECT_EQ_INT(count_writes_for(VOODOO_TEXMEM_BASE), 0);
        EXPECT_TRUE(last_value_for(IE_VOODOO_TEX_SRC_PTR) != 0);
        EXPECT_EQ_INT(last_value_for(IE_VOODOO_TEX_SRC_BYTES), 4);
    } else {
        EXPECT_EQ_INT(count_writes_for(IE_VOODOO_TEX_SRC_PTR), 0);
        EXPECT_EQ_INT(count_writes_for(IE_VOODOO_TEX_SRC_BYTES), 0);
        EXPECT_EQ_INT(count_writes_for(VOODOO_TEXMEM_BASE), 1);
    }
    EXPECT_EQ_INT(last_value_for(VOODOO_TEX_WIDTH), 1);
    EXPECT_EQ_INT(last_value_for(VOODOO_TEX_HEIGHT), 1);
    EXPECT_EQ_INT(last_value_for(VOODOO_TEX_UPLOAD), 1);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEX_STREAM_BYTES), 4);
    if (cmdstream_compiled()) {
        expect_stream_submit(5);
    } else {
        EXPECT_EQ_INT(ie_perf_get(IE_PERF_MMIO_WRITES), num_writes);
    }
}

static void test_same_content_upload_does_not_restream_resident_texture(void) {
    const uint8_t texel[4] = { 0x12, 0x34, 0x56, 0x78 };
    uint32_t texture;

    ie_gfx_voodoo_api.init();
    texture = ie_gfx_voodoo_api.new_texture();
    ie_gfx_voodoo_api.select_texture(0, texture);
    ie_gfx_voodoo_api.upload_texture(texel, 1, 1);

    reset_capture();
    ie_gfx_voodoo_api.upload_texture(texel, 1, 1);

    EXPECT_EQ_INT(count_writes_for(VOODOO_TEXMEM_BASE), 0);
    EXPECT_EQ_INT(count_writes_for(VOODOO_TEX_UPLOAD), 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEX_STREAM_BYTES), 0);
}

static void test_changed_content_upload_restreams_resident_texture(void) {
    const uint8_t texel_a[4] = { 0x12, 0x34, 0x56, 0x78 };
    const uint8_t texel_b[4] = { 0x9A, 0xBC, 0xDE, 0xF0 };
    uint32_t texture;

    ie_gfx_voodoo_api.init();
    texture = ie_gfx_voodoo_api.new_texture();
    ie_gfx_voodoo_api.select_texture(0, texture);
    ie_gfx_voodoo_api.upload_texture(texel_a, 1, 1);

    reset_capture();
    ie_gfx_voodoo_api.upload_texture(texel_b, 1, 1);

    if (cmdstream_compiled()) {
        EXPECT_EQ_INT(count_writes_for(VOODOO_TEXMEM_BASE), 0);
        EXPECT_TRUE(last_value_for(IE_VOODOO_TEX_SRC_PTR) != 0);
        EXPECT_EQ_INT(last_value_for(IE_VOODOO_TEX_SRC_BYTES), 4);
    } else {
        EXPECT_EQ_INT(count_writes_for(IE_VOODOO_TEX_SRC_PTR), 0);
        EXPECT_EQ_INT(count_writes_for(IE_VOODOO_TEX_SRC_BYTES), 0);
        EXPECT_EQ_INT(count_writes_for(VOODOO_TEXMEM_BASE), 1);
    }
    EXPECT_EQ_INT(count_writes_for(VOODOO_TEX_UPLOAD), 1);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEX_STREAM_BYTES), 4);
}

static void test_alternating_textures_restream_single_voodoo_window(void) {
    const uint8_t texel_a[4] = { 0x10, 0x20, 0x30, 0x40 };
    const uint8_t texel_b[4] = { 0x50, 0x60, 0x70, 0x80 };
    uint32_t tex_a;
    uint32_t tex_b;

    ie_gfx_voodoo_api.init();
    tex_a = ie_gfx_voodoo_api.new_texture();
    tex_b = ie_gfx_voodoo_api.new_texture();

    reset_capture();
    ie_gfx_voodoo_api.select_texture(0, tex_a);
    ie_gfx_voodoo_api.upload_texture(texel_a, 1, 1);
    ie_gfx_voodoo_api.select_texture(0, tex_b);
    ie_gfx_voodoo_api.upload_texture(texel_b, 1, 1);
    ie_gfx_voodoo_api.select_texture(0, tex_a);

    EXPECT_EQ_INT(count_writes_for(VOODOO_TEX_UPLOAD), 3);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEX_STREAM_BYTES), 12);
}

static void test_large_texture_upload_fits_slot(void) {
    static uint8_t texels[GFX_MAX_TEXTURE_BYTES];
    uint32_t texture;
    size_t last = (64u * 250u - 1u) * 4u;

    ie_gfx_voodoo_api.init();
    memset(texels, 0, sizeof(texels));
    texels[0] = 0x12;
    texels[1] = 0x34;
    texels[2] = 0x56;
    texels[3] = 0x78;
    texels[last + 0] = 0x9A;
    texels[last + 1] = 0xBC;
    texels[last + 2] = 0xDE;
    texels[last + 3] = 0xF0;

    texture = ie_gfx_voodoo_api.new_texture();
    ie_gfx_voodoo_api.select_texture(0, texture);
    reset_capture();
    ie_gfx_voodoo_api.upload_texture(texels, 64, 250);

    if (cmdstream_compiled()) {
        EXPECT_EQ_INT(count_writes_for(VOODOO_TEXMEM_BASE), 0);
        EXPECT_TRUE(last_value_for(IE_VOODOO_TEX_SRC_PTR) != 0);
        EXPECT_EQ_INT(last_value_for(IE_VOODOO_TEX_SRC_BYTES), 64u * 250u * 4u);
    } else {
        EXPECT_EQ_INT(count_writes_for(IE_VOODOO_TEX_SRC_PTR), 0);
        EXPECT_EQ_INT(count_writes_for(IE_VOODOO_TEX_SRC_BYTES), 0);
        EXPECT_EQ_INT(count_writes_for(VOODOO_TEXMEM_BASE), 1);
    }
    EXPECT_EQ_INT(last_value_for(VOODOO_TEX_WIDTH), 64);
    EXPECT_EQ_INT(last_value_for(VOODOO_TEX_HEIGHT), 250);
    EXPECT_EQ_INT(last_value_for(VOODOO_TEX_UPLOAD), 1);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEX_STREAM_BYTES), 64u * 250u * 4u);
    if (cmdstream_compiled()) {
        expect_stream_submit(5);
    } else {
        EXPECT_EQ_INT(ie_perf_get(IE_PERF_MMIO_WRITES), num_writes);
    }
}

static void test_textured_modulate_draw_register_sequence(void) {
    struct ShaderProgram* shader =
        ie_gfx_voodoo_api.create_and_load_new_shader(texture_shader_id(SHADER_OPT_ALPHA));
    float vbo[] = {
        -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
         0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f,
         0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.25f,
    };
    size_t cursor = 0;

    ie_gfx_voodoo_api.load_shader(shader);
    ie_gfx_voodoo_api.set_use_alpha(1);
    reset_capture();
    boot_status[9] = 10;

    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();

    expect_next_write(&cursor, IE_BOOT_STATUS_ADDR + 9u * 4u, 11);
    expect_next_write(&cursor, VOODOO_TEXTURE_MODE,
                      TEX_ENABLE | TEX_FMT_ARGB8888 | TEX_PERSPECTIVE | TEX_CLAMP_S
                          | TEX_CLAMP_T);

    expect_next_write(&cursor, VOODOO_FBZCOLOR_PATH, VOODOO_COMBINE_MODULATE);
    expect_next_write(&cursor, VOODOO_COLOR_SELECT, 0);
    expect_next_write(&cursor, VOODOO_START_W, 0x40000000u);
    expect_next_write(&cursor, VOODOO_VERTEX_AX, 0x0A00u);
    expect_next_write(&cursor, VOODOO_VERTEX_AY, 0x1680u);
    expect_next_write(&cursor, VOODOO_START_S, 0);
    expect_next_write(&cursor, VOODOO_START_T, 0);
    expect_next_write(&cursor, VOODOO_START_R, 4096);
    expect_next_write(&cursor, VOODOO_START_G, 0);
    expect_next_write(&cursor, VOODOO_START_B, 0);
    expect_next_write(&cursor, VOODOO_START_A, 4096);
    expect_next_write(&cursor, VOODOO_START_Z, 2048);

    expect_next_write(&cursor, VOODOO_COLOR_SELECT, 1);
    expect_next_write(&cursor, VOODOO_START_W, 0x40000000u);
    expect_next_write(&cursor, VOODOO_VERTEX_BX, 0x1E00u);
    expect_next_write(&cursor, VOODOO_VERTEX_BY, 0x1680u);
    expect_next_write(&cursor, VOODOO_START_S, 262144);
    expect_next_write(&cursor, VOODOO_START_T, 0);
    expect_next_write(&cursor, VOODOO_START_R, 0);
    expect_next_write(&cursor, VOODOO_START_G, 4096);
    expect_next_write(&cursor, VOODOO_START_B, 0);
    expect_next_write(&cursor, VOODOO_START_A, 2048);
    expect_next_write(&cursor, VOODOO_START_Z, 2048);

    expect_next_write(&cursor, VOODOO_COLOR_SELECT, 2);
    expect_next_write(&cursor, VOODOO_START_W, 0x40000000u);
    expect_next_write(&cursor, VOODOO_VERTEX_CX, 0x1400u);
    expect_next_write(&cursor, VOODOO_VERTEX_CY, 0x0780u);
    expect_next_write(&cursor, VOODOO_START_S, 0);
    expect_next_write(&cursor, VOODOO_START_T, 262144);
    expect_next_write(&cursor, VOODOO_START_R, 0);
    expect_next_write(&cursor, VOODOO_START_G, 0);
    expect_next_write(&cursor, VOODOO_START_B, 4096);
    expect_next_write(&cursor, VOODOO_START_A, 1024);
    expect_next_write(&cursor, VOODOO_START_Z, 2048);

    expect_next_write(&cursor, VOODOO_TRIANGLE_CMD, 0);
    EXPECT_EQ_INT(cursor, num_writes);
    if (cmdstream_compiled()) {
        EXPECT_EQ_INT(cmd_submit_count, 1);
        EXPECT_EQ_INT(cmd_pair_count, num_writes - 1u);
        EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_SUBMITS), 1);
        EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_PAIRS), num_writes - 1u);
        EXPECT_EQ_INT(ie_perf_get(IE_PERF_MMIO_WRITES), 4);
    } else {
        EXPECT_EQ_INT(cmd_submit_count, 0);
        EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_SUBMITS), 0);
        EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_PAIRS), 0);
        EXPECT_EQ_INT(ie_perf_get(IE_PERF_MMIO_WRITES), num_writes);
    }
}

static void test_texture_mode_shadow_skips_identical_draws(void) {
    uint32_t expected_linear_mode =
        TEX_ENABLE | TEX_FMT_ARGB8888 | TEX_PERSPECTIVE | TEX_MAGNIFY_BILINEAR
        | TEX_CLAMP_S | TEX_CLAMP_T;

    ie_gfx_voodoo_api.init();
    draw_textured_alpha_triangle(0);

    reset_capture();
    draw_textured_alpha_triangle(0);
    EXPECT_EQ_INT(count_writes_for(VOODOO_TEXTURE_MODE), 0);

    ie_gfx_voodoo_api.set_sampler_parameters(0, 1, 1, 1);
    reset_capture();
    draw_textured_alpha_triangle(0);
    EXPECT_EQ_INT(count_writes_for(VOODOO_TEXTURE_MODE), 1);
    EXPECT_EQ_INT(last_value_for(VOODOO_TEXTURE_MODE), expected_linear_mode);
}

static void test_perspective_projection_words_for_mixed_w(void) {
    struct ShaderProgram* shader =
        ie_gfx_voodoo_api.create_and_load_new_shader(SHADER_INPUT_1 << 9);
    float vbo[] = {
        -0.5f, 0.0f, 0.0f, 2.0f, 1.0f, 0.0f, 0.0f,
         0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
         0.0f, 0.5f, 0.0f, 4.0f, 0.0f, 0.0f, 1.0f,
    };

    ie_gfx_voodoo_api.init();
    ie_gfx_voodoo_api.load_shader(shader);
    reset_capture();
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();

    EXPECT_EQ_INT(writes[find_nth_write_for(VOODOO_START_W, 0)].value, 0x20000000u);
    EXPECT_EQ_INT(writes[find_nth_write_for(VOODOO_START_W, 1)].value, 0x40000000u);
    EXPECT_EQ_INT(writes[find_nth_write_for(VOODOO_START_W, 2)].value, 0x10000000u);
    EXPECT_EQ_INT(last_value_for(VOODOO_VERTEX_AX), 0x0F00u);
    EXPECT_EQ_INT(last_value_for(VOODOO_VERTEX_AY), 0x0F00u);
    EXPECT_EQ_INT(last_value_for(VOODOO_VERTEX_BX), 0x1E00u);
    EXPECT_EQ_INT(last_value_for(VOODOO_VERTEX_BY), 0x0F00u);
    EXPECT_EQ_INT(last_value_for(VOODOO_VERTEX_CX), 0x1400u);
    EXPECT_EQ_INT(last_value_for(VOODOO_VERTEX_CY), 0x0D20u);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_Z), 2048);
}

static void test_baked_kart_combiner_texture_and_scratch_reuse(void) {
    static const uint8_t texels[8] = {
        128, 64, 32, 200,
        20, 220, 100, 64,
    };
    static const uint8_t prim_a[4] = { 80, 200, 30, 128 };
    static const uint8_t env_a[4] = { 10, 40, 60, 32 };
    static const uint8_t prim_b[4] = { 40, 30, 220, 220 };
    static const uint8_t env_b[4] = { 100, 20, 10, 16 };
    struct ShaderProgram* shader = ie_gfx_voodoo_api.create_and_load_new_shader(
        kart_tint_shader_id(SHADER_OPT_ALPHA | SHADER_OPT_TEXTURE_EDGE));
    float vbo[42];
    uint32_t source = upload_test_texture(texels, 2, 1);
    uint32_t probe;

    ie_gfx_voodoo_api.set_use_alpha(1);
    ie_gfx_voodoo_api.load_shader(shader);
    fill_two_input_textured_vbo(vbo, prim_a, env_a, 0);
    reset_capture();
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();

    EXPECT_EQ_INT(last_value_for(VOODOO_FBZCOLOR_PATH), VOODOO_COMBINE_TEXTURE);
    EXPECT_EQ_INT(last_value_for(VOODOO_ALPHA_MODE), alpha_blend_word() | alpha_test_word());
    EXPECT_EQ_INT(texmem[0], expected_kart_texel(&texels[0], prim_a, env_a));
    EXPECT_EQ_INT(texmem[1], expected_kart_texel(&texels[4], prim_a, env_a));
    EXPECT_EQ_INT(last_value_for(VOODOO_START_R), 4096);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_G), 4096);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_B), 4096);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_A), 4096);

    reset_capture();
    ie_gfx_voodoo_api.select_texture(0, source);
    EXPECT_EQ_INT(texmem[0], rgba_word(texels[0], texels[1], texels[2], texels[3]));
    EXPECT_EQ_INT(texmem[1], rgba_word(texels[4], texels[5], texels[6], texels[7]));

    fill_two_input_textured_vbo(vbo, prim_b, env_b, 0);
    reset_capture();
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();

    EXPECT_EQ_INT(count_writes_for(VOODOO_FBZCOLOR_PATH), 0);
    EXPECT_EQ_INT(texmem[0], expected_kart_texel(&texels[0], prim_b, env_b));
    EXPECT_EQ_INT(texmem[1], expected_kart_texel(&texels[4], prim_b, env_b));

    probe = ie_gfx_voodoo_api.new_texture();
    EXPECT_TRUE(probe <= source + 2u);
}

static void test_baked_kart_constants_change_inside_one_draw_preserves_order(void) {
    static const uint8_t texels[4] = { 128, 64, 32, 200 };
    static const uint8_t prim_a[4] = { 80, 200, 30, 128 };
    static const uint8_t env_a[4] = { 10, 40, 60, 32 };
    static const uint8_t prim_b[4] = { 40, 30, 220, 220 };
    static const uint8_t env_b[4] = { 100, 20, 10, 16 };
    struct ShaderProgram* shader = ie_gfx_voodoo_api.create_and_load_new_shader(
        kart_tint_shader_id(SHADER_OPT_ALPHA));
    float vbo[84];
    size_t bake_a;
    size_t bake_b;
    size_t tri_a;
    size_t tri_b;

    upload_test_texture(texels, 1, 1);
    ie_gfx_voodoo_api.load_shader(shader);
    fill_two_input_textured_vbo(vbo, prim_a, env_a, 0);
    fill_two_input_textured_vbo(vbo + 42, prim_b, env_b, 0);
    reset_capture();
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 2);
    ie_gfx_voodoo_api.finish_render();

    EXPECT_EQ_INT(count_writes_for(VOODOO_TEX_UPLOAD), 2);
    EXPECT_EQ_INT(count_writes_for(VOODOO_TRIANGLE_CMD), 2);
    bake_a = find_nth_write_for(VOODOO_TEX_UPLOAD, 0);
    bake_b = find_nth_write_for(VOODOO_TEX_UPLOAD, 1);
    tri_a = find_nth_write_for(VOODOO_TRIANGLE_CMD, 0);
    tri_b = find_nth_write_for(VOODOO_TRIANGLE_CMD, 1);
    EXPECT_TRUE(bake_a < tri_a);
    EXPECT_TRUE(tri_a < bake_b);
    EXPECT_TRUE(bake_b < tri_b);
}

static void test_baked_kart_same_constants_reuse_cached_scratch(void) {
    static const uint8_t texels[4] = { 128, 64, 32, 200 };
    static const uint8_t prim_a[4] = { 80, 200, 30, 128 };
    static const uint8_t env_a[4] = { 10, 40, 60, 32 };
    static const uint8_t prim_b[4] = { 40, 30, 220, 220 };
    static const uint8_t env_b[4] = { 100, 20, 10, 16 };
    struct ShaderProgram* shader = ie_gfx_voodoo_api.create_and_load_new_shader(
        kart_tint_shader_id(SHADER_OPT_ALPHA));
    float vbo[42];

    ie_gfx_voodoo_api.init();
    upload_test_texture(texels, 1, 1);
    ie_gfx_voodoo_api.load_shader(shader);

    fill_two_input_textured_vbo(vbo, prim_a, env_a, 0);
    reset_capture();
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();
    EXPECT_EQ_INT(count_writes_for(VOODOO_TEX_UPLOAD), 1);

    reset_capture();
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();
    EXPECT_EQ_INT(count_writes_for(VOODOO_TEX_UPLOAD), 0);

    fill_two_input_textured_vbo(vbo, prim_b, env_b, 0);
    reset_capture();
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();
    EXPECT_EQ_INT(count_writes_for(VOODOO_TEX_UPLOAD), 1);
}

static void test_baked_smoke_combiner_blend_only_alpha(void) {
    static const uint8_t texels[8] = {
        200, 60, 140, 96,
        40, 180, 20, 224,
    };
    static const uint8_t prim[4] = { 210, 50, 160, 96 };
    static const uint8_t env[4] = { 20, 100, 30, 240 };
    struct ShaderProgram* shader =
        ie_gfx_voodoo_api.create_and_load_new_shader(particle_smoke_shader_id(SHADER_OPT_ALPHA));
    float vbo[42];

    ie_gfx_voodoo_api.set_use_alpha(1);
    draw_textured_alpha_triangle(SHADER_OPT_TEXTURE_EDGE);
    upload_test_texture(texels, 2, 1);
    ie_gfx_voodoo_api.load_shader(shader);
    fill_two_input_textured_vbo(vbo, prim, env, 1);
    reset_capture();
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();

    EXPECT_EQ_INT(last_value_for(VOODOO_FBZCOLOR_PATH), VOODOO_COMBINE_TEXTURE);
    EXPECT_EQ_INT(last_value_for(VOODOO_ALPHA_MODE), alpha_blend_word());
    EXPECT_EQ_INT(texmem[0], expected_smoke_texel(&texels[0], prim, env));
    EXPECT_EQ_INT(texmem[1], expected_smoke_texel(&texels[4], prim, env));
    EXPECT_EQ_INT(last_value_for(VOODOO_START_R), 4096);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_G), 4096);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_B), 4096);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_A), 4096);
}

static void draw_untextured_shade_triangle(void) {
    struct ShaderProgram* shader =
        ie_gfx_voodoo_api.create_and_load_new_shader(SHADER_INPUT_1 << 9);
    float vbo[] = {
        -0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
         0.0f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };

    ie_gfx_voodoo_api.load_shader(shader);
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();
}

static void test_color_path_restored_after_baked_draws(void) {
    static const uint8_t texels[4] = { 128, 128, 128, 255 };
    static const uint8_t prim[4] = { 80, 200, 30, 128 };
    static const uint8_t env[4] = { 10, 40, 60, 32 };
    struct ShaderProgram* shader =
        ie_gfx_voodoo_api.create_and_load_new_shader(kart_tint_shader_id(SHADER_OPT_ALPHA));
    float vbo[42];

    upload_test_texture(texels, 1, 1);
    ie_gfx_voodoo_api.load_shader(shader);
    fill_two_input_textured_vbo(vbo, prim, env, 0);
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();

    reset_capture();
    draw_textured_alpha_triangle(0);
    EXPECT_EQ_INT(last_value_for(VOODOO_FBZCOLOR_PATH), VOODOO_COMBINE_MODULATE);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_R), 0);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_G), 0);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_B), 4096);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_A), 1024);

    reset_capture();
    draw_untextured_shade_triangle();
    EXPECT_EQ_INT(last_value_for(VOODOO_FBZCOLOR_PATH), VOODOO_COMBINE_ITERATED);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_R), 0);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_G), 0);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_B), 4096);
}

static void test_texture_edge_enables_alpha_test_with_blend(void) {
    ie_gfx_voodoo_api.set_use_alpha(1);
    reset_capture();

    draw_textured_alpha_triangle(SHADER_OPT_TEXTURE_EDGE);

    EXPECT_EQ_INT(last_value_for(VOODOO_ALPHA_MODE), alpha_blend_word() | alpha_test_word());
}

static void test_texture_edge_to_non_edge_clears_alpha_test(void) {
    ie_gfx_voodoo_api.set_use_alpha(1);
    draw_textured_alpha_triangle(SHADER_OPT_TEXTURE_EDGE);
    reset_capture();

    draw_textured_alpha_triangle(0);

    EXPECT_EQ_INT(last_value_for(VOODOO_ALPHA_MODE), alpha_blend_word());
}

static void test_non_edge_alpha_draw_leaves_alpha_mode_unchanged(void) {
    ie_gfx_voodoo_api.set_use_alpha(1);
    draw_textured_alpha_triangle(0);
    reset_capture();

    draw_textured_alpha_triangle(0);

    EXPECT_EQ_INT(count_writes_for(VOODOO_ALPHA_MODE), 0);
}

static void test_decal_depth_bias_moves_start_z_toward_camera(void) {
    ie_gfx_voodoo_api.set_zmode_decal(0);
    reset_capture();
    draw_textured_alpha_triangle(0);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_Z), 2048);

    ie_gfx_voodoo_api.set_zmode_decal(1);
    reset_capture();
    draw_textured_alpha_triangle(0);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_Z), 2044);
}

static void test_decal_depth_bias_clamps_at_near_end(void) {
    ie_gfx_voodoo_api.set_zmode_decal(1);
    reset_capture();
    draw_textured_alpha_triangle_z(0, -1.0f);
    EXPECT_EQ_INT(last_value_for(VOODOO_START_Z), 0);

    ie_gfx_voodoo_api.set_zmode_decal(0);
}

static void test_texture_slot_pool_reserves_bake_scratch(void) {
    static const uint8_t texels[4] = { 128, 64, 32, 200 };
    static const uint8_t prim[4] = { 80, 200, 30, 128 };
    static const uint8_t env[4] = { 10, 40, 60, 32 };
    struct ShaderProgram* shader;
    float vbo[42];
    uint32_t source;
    uint32_t last = 0;
    uint32_t i;

    ie_gfx_voodoo_api.init();
    shader = ie_gfx_voodoo_api.create_and_load_new_shader(kart_tint_shader_id(SHADER_OPT_ALPHA));
    source = upload_test_texture(texels, 1, 1);
    ie_gfx_voodoo_api.load_shader(shader);
    fill_two_input_textured_vbo(vbo, prim, env, 0);
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), 1);
    ie_gfx_voodoo_api.finish_render();

    for (i = 1u; i < GFX_TEXTURE_CACHE_SIZE; i++) {
        last = ie_gfx_voodoo_api.new_texture();
    }

    EXPECT_EQ_INT(last, GFX_TEXTURE_CACHE_SIZE);
    ie_gfx_voodoo_api.select_texture(0, source);
    EXPECT_EQ_INT(texmem[0], rgba_word(texels[0], texels[1], texels[2], texels[3]));
}

static void test_no_cmdstream_texture_upload_uses_texmem_loop(void) {
    const uint8_t texel[4] = { 0x12, 0x34, 0x56, 0x78 };
    uint32_t texture;

    set_cmd_stream_capable(0);
    ie_gfx_voodoo_api.init();
    texture = ie_gfx_voodoo_api.new_texture();
    ie_gfx_voodoo_api.select_texture(0, texture);
    reset_capture();
    ie_gfx_voodoo_api.upload_texture(texel, 1, 1);

    EXPECT_EQ_INT(count_writes_for(IE_VOODOO_TEX_SRC_PTR), 0);
    EXPECT_EQ_INT(count_writes_for(IE_VOODOO_TEX_SRC_BYTES), 0);
    EXPECT_EQ_INT(count_writes_for(VOODOO_TEXMEM_BASE), 1);
    EXPECT_EQ_INT(texmem[0], rgba_word(texel[0], texel[1], texel[2], texel[3]));
    EXPECT_EQ_INT(last_value_for(VOODOO_TEX_WIDTH), 1);
    EXPECT_EQ_INT(last_value_for(VOODOO_TEX_HEIGHT), 1);
    EXPECT_EQ_INT(last_value_for(VOODOO_TEX_UPLOAD), 1);
    EXPECT_EQ_INT(cmd_submit_count, 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_SUBMITS), 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_PAIRS), 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_MMIO_WRITES), num_writes);
    set_cmd_stream_capable(1);
}

static void test_no_cmdstream_draw_writes_registers_directly(void) {
    set_cmd_stream_capable(0);
    ie_gfx_voodoo_api.init();
    reset_capture();
    draw_untextured_shade_triangle();

    EXPECT_EQ_INT(cmd_submit_count, 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_SUBMITS), 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_PAIRS), 0);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_MMIO_WRITES), num_writes);
    EXPECT_EQ_INT(last_value_for(VOODOO_TRIANGLE_CMD), 0);
    EXPECT_EQ_INT(last_value_for(VOODOO_FBZCOLOR_PATH), VOODOO_COMBINE_ITERATED);
    set_cmd_stream_capable(1);
}

static void test_cmdstream_capacity_flushes_and_continues(void) {
    enum { TRI_COUNT = 220 };
    static float vbo[TRI_COUNT * 3u * 7u];
    struct ShaderProgram* shader;
    size_t i;

    if (!cmdstream_compiled()) {
        return;
    }
    set_cmd_stream_capable(1);
    ie_gfx_voodoo_api.init();
    shader = ie_gfx_voodoo_api.create_and_load_new_shader(SHADER_INPUT_1 << 9);
    for (i = 0; i < TRI_COUNT; i++) {
        size_t o = i * 21u;

        vbo[o + 0] = -0.5f;
        vbo[o + 1] = -0.5f;
        vbo[o + 2] = 0.0f;
        vbo[o + 3] = 1.0f;
        vbo[o + 4] = 1.0f;
        vbo[o + 5] = 0.0f;
        vbo[o + 6] = 0.0f;
        vbo[o + 7] = 0.5f;
        vbo[o + 8] = -0.5f;
        vbo[o + 9] = 0.0f;
        vbo[o + 10] = 1.0f;
        vbo[o + 11] = 0.0f;
        vbo[o + 12] = 1.0f;
        vbo[o + 13] = 0.0f;
        vbo[o + 14] = 0.0f;
        vbo[o + 15] = 0.5f;
        vbo[o + 16] = 0.0f;
        vbo[o + 17] = 1.0f;
        vbo[o + 18] = 0.0f;
        vbo[o + 19] = 0.0f;
        vbo[o + 20] = 1.0f;
    }

    ie_gfx_voodoo_api.load_shader(shader);
    reset_capture();
    ie_gfx_voodoo_api.draw_triangles(vbo, sizeof(vbo) / sizeof(vbo[0]), TRI_COUNT);
    ie_gfx_voodoo_api.finish_render();

    EXPECT_TRUE(cmd_submit_count >= 2);
    EXPECT_EQ_INT(cmd_pair_count, num_writes - 1u);
    EXPECT_EQ_INT(count_writes_for(VOODOO_TRIANGLE_CMD), TRI_COUNT);
    EXPECT_EQ_INT(count_raw_writes_for(IE_VOODOO_CMD_PTR), cmd_submit_count);
    EXPECT_EQ_INT(count_raw_writes_for(IE_VOODOO_CMD_COUNT), cmd_submit_count);
    EXPECT_EQ_INT(count_raw_writes_for(IE_VOODOO_CMD_SUBMIT), cmd_submit_count);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_SUBMITS), cmd_submit_count);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_CMD_PAIRS), cmd_pair_count);
}

int main(void) {
    set_cmd_stream_capable(1);
    test_pack_data_window_does_not_overlap_texture_store();
    test_fbz_depth_state_words();
    test_alpha_blend_state_word();
    test_texture_upload_residency();
    test_same_content_upload_does_not_restream_resident_texture();
    test_changed_content_upload_restreams_resident_texture();
    test_alternating_textures_restream_single_voodoo_window();
    test_large_texture_upload_fits_slot();
    test_textured_modulate_draw_register_sequence();
    test_texture_mode_shadow_skips_identical_draws();
    test_perspective_projection_words_for_mixed_w();
    test_baked_kart_combiner_texture_and_scratch_reuse();
    test_baked_kart_constants_change_inside_one_draw_preserves_order();
    test_baked_kart_same_constants_reuse_cached_scratch();
    test_baked_smoke_combiner_blend_only_alpha();
    test_color_path_restored_after_baked_draws();
    test_texture_edge_enables_alpha_test_with_blend();
    test_texture_edge_to_non_edge_clears_alpha_test();
    test_non_edge_alpha_draw_leaves_alpha_mode_unchanged();
    test_decal_depth_bias_moves_start_z_toward_camera();
    test_decal_depth_bias_clamps_at_near_end();
    test_texture_slot_pool_reserves_bake_scratch();
    test_no_cmdstream_texture_upload_uses_texmem_loop();
    test_no_cmdstream_draw_writes_registers_directly();
    test_cmdstream_capacity_flushes_and_continues();
    return test_finish("test_gfx_voodoo");
}
