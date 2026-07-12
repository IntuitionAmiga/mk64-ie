/*
 * Host tests for the Stage 2 Fast3D display-list translator
 * (src/gfx/gfx_pc.c): display lists go in, GfxRenderingAPI calls come
 * out. A recording mock backend captures every call; shader decoding
 * in the mock reuses gfx_cc.c so num_inputs/used_textures agree with
 * the translator's expectations.
 */
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/mbi.h>
#include <PR/gbi.h>

#include "gfx/gfx_pc.h"
#include "gfx/gfx_cc.h"
#include "gfx/gfx_rendering_api.h"
#include "gfx/gfx_window_manager_api.h"
#include "ie/ie_perf_counters.h"

s32 gIsMirrorMode = 0;

void platform_fatal(const char* fmt, ...) {
    (void) fmt;
    printf("platform_fatal called\n");
    exit(2);
}

/* ---- recording mock backend --------------------------------------------- */

#define MAX_EVENTS 64
#define MAX_MOCK_EVENTS 128
#define MAX_VBO_FLOATS 4096

enum MockEventType {
    MOCK_EVENT_UPLOAD,
    MOCK_EVENT_DRAW,
};

struct MockEvent {
    enum MockEventType type;
    int width;
    int height;
    size_t tris;
    uint32_t cms;
    uint32_t cmt;
};

static struct {
    int init_called;
    int start_frames, end_frames, finish_renders;

    uint32_t created_shaders[MAX_EVENTS];
    int num_created_shaders;
    uint32_t loaded_shader;

    int num_uploads;
    uint8_t upload_data[GFX_MAX_TEXTURE_BYTES];
    int upload_width, upload_height;

    int num_draws;
    float vbo[MAX_VBO_FLOATS];
    size_t vbo_len;
    size_t num_tris;

    int viewport_x, viewport_y, viewport_w, viewport_h;
    int scissor_x, scissor_y, scissor_w, scissor_h;
    int num_viewports, num_scissors;
    int num_zmode_decals;
    uint8_t zmode_decals[MAX_EVENTS];

    uint32_t selected_texture[2];
    uint32_t next_texture_id;
    struct MockEvent events[MAX_MOCK_EVENTS];
    int num_events;
} mock;

struct ShaderProgram {
    uint32_t shader_id;
    struct CCFeatures cc;
};

static struct ShaderProgram mock_shaders[MAX_EVENTS];
static int mock_num_shaders;

static uint8_t mock_z_is_from_0_to_1(void) {
    return 0;
}
static void mock_unload_shader(struct ShaderProgram* prg) {
    (void) prg;
}
static void mock_load_shader(struct ShaderProgram* prg) {
    mock.loaded_shader = prg->shader_id;
}
static struct ShaderProgram* mock_create_and_load_new_shader(uint32_t shader_id) {
    struct ShaderProgram* prg = &mock_shaders[mock_num_shaders++];
    prg->shader_id = shader_id;
    gfx_cc_get_features(shader_id, &prg->cc);
    mock.created_shaders[mock.num_created_shaders++] = shader_id;
    mock.loaded_shader = shader_id;
    return prg;
}
static struct ShaderProgram* mock_lookup_shader(uint32_t shader_id) {
    int i;
    for (i = 0; i < mock_num_shaders; i++) {
        if (mock_shaders[i].shader_id == shader_id) {
            return &mock_shaders[i];
        }
    }
    return NULL;
}
static void mock_shader_get_info(struct ShaderProgram* prg, uint8_t* num_inputs,
                                 uint8_t used_textures[2]) {
    *num_inputs = prg->cc.num_inputs;
    used_textures[0] = prg->cc.used_textures[0];
    used_textures[1] = prg->cc.used_textures[1];
}
static uint32_t mock_new_texture(void) {
    return mock.next_texture_id++;
}
static void mock_select_texture(int tile, uint32_t texture_id) {
    mock.selected_texture[tile] = texture_id;
}
static struct MockEvent* mock_record_event(enum MockEventType type) {
    if (mock.num_events < MAX_MOCK_EVENTS) {
        struct MockEvent* event = &mock.events[mock.num_events++];

        event->type = type;
        return event;
    }
    return NULL;
}
static void mock_upload_texture(const uint8_t* rgba32_buf, int width, int height) {
    size_t n = (size_t) width * height * 4;
    struct MockEvent* event;

    if (n > sizeof(mock.upload_data)) {
        n = sizeof(mock.upload_data);
    }
    memcpy(mock.upload_data, rgba32_buf, n);
    mock.upload_width = width;
    mock.upload_height = height;
    mock.num_uploads++;
    event = mock_record_event(MOCK_EVENT_UPLOAD);
    if (event != NULL) {
        event->width = width;
        event->height = height;
    }
}
static void mock_set_sampler_parameters(int sampler, uint8_t linear, uint32_t cms, uint32_t cmt) {
    (void) sampler;
    (void) linear;
    (void) cms;
    (void) cmt;
}
static void mock_set_depth_test(uint8_t depth_test) {
    (void) depth_test;
}
static void mock_set_depth_mask(uint8_t z_upd) {
    (void) z_upd;
}
static void mock_set_zmode_decal(uint8_t zmode_decal) {
    mock.zmode_decals[mock.num_zmode_decals++] = zmode_decal;
}
static void mock_set_viewport(int x, int y, int width, int height) {
    mock.viewport_x = x;
    mock.viewport_y = y;
    mock.viewport_w = width;
    mock.viewport_h = height;
    mock.num_viewports++;
}
static void mock_set_scissor(int x, int y, int width, int height) {
    mock.scissor_x = x;
    mock.scissor_y = y;
    mock.scissor_w = width;
    mock.scissor_h = height;
    mock.num_scissors++;
}
static void mock_set_use_alpha(uint8_t use_alpha) {
    (void) use_alpha;
}
static void mock_draw_triangles(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris) {
    size_t n = buf_vbo_len < MAX_VBO_FLOATS ? buf_vbo_len : MAX_VBO_FLOATS;
    struct MockEvent* event;

    memcpy(mock.vbo, buf_vbo, n * sizeof(float));
    mock.vbo_len = buf_vbo_len;
    mock.num_tris = buf_vbo_num_tris;
    mock.num_draws++;
    event = mock_record_event(MOCK_EVENT_DRAW);
    if (event != NULL) {
        event->tris = buf_vbo_num_tris;
    }
}
static void mock_init(void) {
    mock.init_called = 1;
}
static void mock_on_resize(void) {
}
static void mock_start_frame(void) {
    mock.start_frames++;
}
static void mock_end_frame(void) {
    mock.end_frames++;
}
static void mock_finish_render(void) {
    mock.finish_renders++;
}

static struct GfxRenderingAPI mock_rapi = {
    mock_z_is_from_0_to_1, mock_unload_shader,          mock_load_shader,
    mock_create_and_load_new_shader, mock_lookup_shader, mock_shader_get_info,
    mock_new_texture,      mock_select_texture,         mock_upload_texture,
    mock_set_sampler_parameters,     mock_set_depth_test, mock_set_depth_mask,
    mock_set_zmode_decal,  mock_set_viewport,           mock_set_scissor,
    mock_set_use_alpha,    mock_draw_triangles,         mock_init,
    mock_on_resize,        mock_start_frame,            mock_end_frame,
    mock_finish_render,
};

static int wapi_inited;
static void wm_init(const char* game_name, uint8_t start_in_fullscreen) {
    (void) game_name;
    (void) start_in_fullscreen;
    wapi_inited = 1;
}
static void wm_set_keyboard_callbacks(uint8_t (*a)(int), uint8_t (*b)(int), void (*c)(void)) {
    (void) a;
    (void) b;
    (void) c;
}
static void wm_set_fullscreen_changed_callback(void (*cb)(uint8_t)) {
    (void) cb;
}
static void wm_set_fullscreen(uint8_t enable) {
    (void) enable;
}
static void wm_main_loop(void (*run)(void)) {
    (void) run;
}
static void wm_get_dimensions(uint32_t* width, uint32_t* height) {
    *width = 640;
    *height = 480;
}
static void wm_handle_events(void) {
}
static uint8_t wm_start_frame(void) {
    return 1;
}
static void wm_swap_buffers_begin(void) {
}
static void wm_swap_buffers_end(void) {
}
static double wm_get_time(void) {
    return 0.0;
}

static struct GfxWindowManagerAPI mock_wapi = {
    wm_init,
    wm_set_keyboard_callbacks,
    wm_set_fullscreen_changed_callback,
    wm_set_fullscreen,
    wm_main_loop,
    wm_get_dimensions,
    wm_handle_events,
    wm_start_frame,
    wm_swap_buffers_begin,
    wm_swap_buffers_end,
    wm_get_time,
};

/* ---- display-list building helpers --------------------------------------- */

static const float identity[4][4] = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
};

/* Shrinks the test vertices into the unit frustum. */
static const float scale_proj[4][4] = {
    { 0.01f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.01f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.01f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
};

static const Vtx tri_vertices[3] = {
    { { { -10, 0, -2 }, 0, { 0, 0 }, { 0x10, 0x20, 0x30, 0xFF } } },
    { { { 10, 0, -2 }, 0, { 992, 0 }, { 0x40, 0x50, 0x60, 0xFF } } },
    { { { 0, 10, -2 }, 0, { 0, 992 }, { 0x70, 0x80, 0x90, 0xFF } } },
};

static const Vtx near_straddle_vertices[3] = {
    { { { 0, -10, -200 }, 0, { 0, 0 }, { 0x10, 0x20, 0x30, 0xFF } } },
    { { { -10, 10, -50 }, 0, { 0, 0 }, { 0x40, 0x50, 0x60, 0xFF } } },
    { { { 10, 10, -50 }, 0, { 0, 0 }, { 0x70, 0x80, 0x90, 0xFF } } },
};

static const Vtx near_behind_vertices[3] = {
    { { { -10, 0, -200 }, 0, { 0, 0 }, { 0x10, 0x20, 0x30, 0xFF } } },
    { { { 10, 0, -200 }, 0, { 0, 0 }, { 0x40, 0x50, 0x60, 0xFF } } },
    { { { 0, 10, -200 }, 0, { 0, 0 }, { 0x70, 0x80, 0x90, 0xFF } } },
};

static const float tiny_w_proj[4][4] = {
    { 0.01f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.01f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 0.0001f },
};

static void run_dl_keep_state(Gfx* dl) {
    memset(&mock, 0, sizeof(mock));
    mock.next_texture_id = 100;
    ie_perf_reset();
    gfx_run(dl);
}

static void run_dl(Gfx* dl) {
    /* Fresh backend-facing state for every list, as after course loads. */
    nuke_everything();
    run_dl_keep_state(dl);
}

static void test_init(void) {
    gfx_init(&mock_wapi, &mock_rapi, "test", false);
    EXPECT_TRUE(wapi_inited);
    EXPECT_TRUE(mock.init_called);
    EXPECT_TRUE(gfx_get_current_rendering_api() == &mock_rapi);
    EXPECT_EQ_INT(gfx_current_dimensions.width, 640);
    EXPECT_EQ_INT(gfx_current_dimensions.height, 480);
}

static void test_shade_triangle(void) {
    static Gfx dl[16];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_tris, 1);
    /* Layout: 4 position floats + one RGB input per vertex. */
    EXPECT_EQ_INT(mock.vbo_len, 3 * (4 + 3));
    /* Scale-only MP: clip coords == object coords / 100. */
    EXPECT_NEAR(mock.vbo[0], -0.1f, 1e-6);
    EXPECT_NEAR(mock.vbo[1], 0.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[2], -0.02f, 1e-6);
    EXPECT_NEAR(mock.vbo[3], 1.0f, 1e-6);
    /* Shade colour of vertex 0. */
    EXPECT_NEAR(mock.vbo[4], 0x10 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[5], 0x20 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[6], 0x30 / 255.0f, 1e-6);
    /* Vertex 1 position starts at stride 7. */
    EXPECT_NEAR(mock.vbo[7], 0.1f, 1e-6);
    /* The shader consumed one non-texture input and no textures. */
    EXPECT_EQ_INT(mock.num_uploads, 0);
}

static void test_clip_rejected_triangle_not_drawn(void) {
    static const Vtx far_vertices[3] = {
        { { { 30000, 30000, 30000 }, 0, { 0, 0 }, { 0, 0, 0, 0xFF } } },
        { { { 30001, 30000, 30000 }, 0, { 0, 0 }, { 0, 0, 0, 0xFF } } },
        { { { 30000, 30001, 30000 }, 0, { 0, 0 }, { 0, 0, 0, 0xFF } } },
    };
    static Gfx dl[16];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(g++, (uintptr_t) far_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);
    EXPECT_EQ_INT(mock.num_draws, 0);
}

static void test_near_plane_straddling_triangle_is_clipped(void) {
    static Gfx dl[16];
    Gfx* g = dl;
    size_t i;
    int on_near = 0;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(g++, (uintptr_t) near_straddle_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_tris, 2);
    EXPECT_EQ_INT(mock.vbo_len, 6 * (4 + 3));
    for (i = 0; i < 6; i++) {
        float z = mock.vbo[i * 7 + 2];
        float w = mock.vbo[i * 7 + 3];
        EXPECT_TRUE(z + w >= -1e-6f);
        if (fabsf(z + w) <= 1e-6f) {
            on_near = 1;
        }
    }
    EXPECT_TRUE(on_near);
}

static void test_near_plane_fully_behind_triangle_not_drawn(void) {
    static Gfx dl[16];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(g++, (uintptr_t) near_behind_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);
    EXPECT_EQ_INT(mock.num_draws, 0);
}

static void test_tiny_w_triangle_emits_finite_vbo_for_backend_guard(void) {
    static Gfx dl[16];
    Gfx* g = dl;
    size_t i;

    gSPMatrix(g++, (uintptr_t) tiny_w_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_tris, 1);
    for (i = 0; i < mock.vbo_len; i++) {
        EXPECT_TRUE(isfinite(mock.vbo[i]));
    }
}

static void test_nuke_everything_resubmits_non_decal_zmode(void) {
    static Gfx decal_dl[16];
    static Gfx surf_dl[16];
    Gfx* g = decal_dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL2);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    g = surf_dl;
    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(decal_dl);
    EXPECT_EQ_INT(mock.num_zmode_decals, 1);
    EXPECT_EQ_INT(mock.zmode_decals[0], 1);

    nuke_everything();
    run_dl_keep_state(surf_dl);

    EXPECT_EQ_INT(mock.num_zmode_decals, 1);
    EXPECT_EQ_INT(mock.zmode_decals[0], 0);
}

static void test_nested_display_list(void) {
    static Gfx sub[8];
    static Gfx dl[16];
    Gfx* g = sub;

    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    g = dl;
    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPDisplayList(g++, (uintptr_t) sub);
    gSPEndDisplayList(g++);

    run_dl(dl);
    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_tris, 1);
}

static void test_matrix_command_does_not_flush_buffered_triangles(void) {
    static Gfx dl[24];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_tris, 2);
}

/* 4x4 rgba16 texture in ROM (big-endian texel) order. Texel 0 is pure
 * red with alpha, texel 1 pure green with alpha, rest blue opaque. */
static const uint8_t rgba16_texels[32] = {
    0xF8, 0x01, 0x07, 0xC1, 0x00, 0x3F, 0x00, 0x3F,
    0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F,
    0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F,
    0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F,
};
static const uint8_t rgba16_texels_alt[32] = {
    0x07, 0xC1, 0xF8, 0x01, 0x00, 0x3F, 0x00, 0x3F,
    0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F,
    0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F,
    0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F,
};
static uint8_t npot_rgba16_texels[7 * 5 * 2];
static uint8_t tall_rgba16_texels[64 * 250 * 2];

static const Vtx npot_uv_vertices[3] = {
    { { { -10, 0, -2 }, 0, { 0, 0 }, { 0x10, 0x20, 0x30, 0xFF } } },
    { { { 10, 0, -2 }, 0, { 224, 0 }, { 0x40, 0x50, 0x60, 0xFF } } },
    { { { 0, 10, -2 }, 0, { 0, 160 }, { 0x70, 0x80, 0x90, 0xFF } } },
};

static void append_textured_rect_setup(Gfx** gp, const uint8_t* texels, uint32_t fmt,
                                       uint32_t siz, uint32_t width, uint32_t height,
                                       uint32_t cms, uint32_t cmt, uint32_t masks,
                                       uint32_t maskt) {
    Gfx* g = *gp;
    uint32_t line_words = (width * G_IM_SIZ_16b_BYTES + 7u) / 8u;

    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetTextureImage(g++, fmt, siz, 1, (uintptr_t) texels);
    gDPSetTile(g++, fmt, siz, 0, 0, G_TX_LOADTILE, 0, G_TX_CLAMP, 0, 0,
               G_TX_CLAMP, 0, 0);
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, width * height - 1,
                 CALC_DXT(width, G_IM_SIZ_16b_BYTES));
    gDPSetTile(g++, fmt, siz, line_words, 0, G_TX_RENDERTILE, 0, cmt, maskt, 0, cms,
               masks, 0);
    gDPSetTileSize(g++, G_TX_RENDERTILE, 0, 0, (width - 1) << G_TEXTURE_IMAGE_FRAC,
                   (height - 1) << G_TEXTURE_IMAGE_FRAC);

    *gp = g;
}

static void append_textured_rect(Gfx** gp, int x, int y, int width, int height) {
    Gfx* g = *gp;

    gSPTextureRectangle(g++, x << 2, y << 2, (x + width) << 2, (y + height) << 2,
                        G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    *gp = g;
}

static void append_redundant_rgba16_rect(Gfx** gp, const uint8_t* texels, int x) {
    append_textured_rect_setup(gp, texels, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 4,
                               G_TX_CLAMP, G_TX_CLAMP, 2, 2);
    append_textured_rect(gp, x, 16, 4, 4);
}

static void append_textured_triangle(Gfx** gp) {
    Gfx* g = *gp;

    gDPSetTileSize(g++, G_TX_RENDERTILE, 0, 0, (16 - 1) << G_TEXTURE_IMAGE_FRAC, 0);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    *gp = g;
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

static void test_textured_triangle(void) {
    static Gfx dl[24];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetTextureImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (uintptr_t) rgba16_texels);
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
               G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 4 * 4 - 1, CALC_DXT(4, G_IM_SIZ_16b_BYTES));
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, 0, G_TX_RENDERTILE, 0,
               G_TX_CLAMP, 2, 0, G_TX_CLAMP, 2, 0);
    gDPSetTileSize(g++, G_TX_RENDERTILE, 0, 0, (4 - 1) << G_TEXTURE_IMAGE_FRAC,
                   (4 - 1) << G_TEXTURE_IMAGE_FRAC);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.upload_width, 4);
    EXPECT_EQ_INT(mock.upload_height, 4);
    /* Texel 0: 0xF801 -> r=11111 g=00000 b=00000 a=1. */
    EXPECT_EQ_INT(mock.upload_data[0], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[1], 0x00);
    EXPECT_EQ_INT(mock.upload_data[2], 0x00);
    EXPECT_EQ_INT(mock.upload_data[3], 0xFF);
    /* Texel 1: 0x07C1 -> g=11111. */
    EXPECT_EQ_INT(mock.upload_data[4], 0x00);
    EXPECT_EQ_INT(mock.upload_data[5], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[6], 0x00);
    EXPECT_EQ_INT(mock.upload_data[7], 0xFF);
    /* Layout: 4 position + 2 uv floats, no colour inputs for decal. */
    EXPECT_EQ_INT(mock.vbo_len, 3 * (4 + 2));
    /* Vertex 1 has tc = 992, scaled by 0xFFFF/0x10000 and truncated
     * to 991 texel units (S10.5) before tile-relative division. */
    EXPECT_NEAR(mock.vbo[6 + 4], ((992 * 0xFFFF >> 16) / 32.0f) / 4.0f, 1e-4);

    /* Re-running the same list must reuse the cached texture... */
    run_dl_keep_state(dl);
    EXPECT_EQ_INT(mock.num_uploads, 0);

    /* ...until the source data is invalidated. */
    gfx_texture_cache_invalidate((void*) rgba16_texels);
    run_dl_keep_state(dl);
    EXPECT_EQ_INT(mock.num_uploads, 1);
}

static void test_npot_texture_coordinates_use_tile_reciprocal(void) {
    static Gfx dl[32];
    Gfx* g = dl;
    float expected_u = (((224 * 0xFFFF) >> 16) / 32.0f) / 7.0f;
    float expected_v = (((160 * 0xFFFF) >> 16) / 32.0f) / 5.0f;

    memset(npot_rgba16_texels, 0xFF, sizeof(npot_rgba16_texels));
    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    append_textured_rect_setup(&g, npot_rgba16_texels, G_IM_FMT_RGBA, G_IM_SIZ_16b, 7,
                               5, G_TX_CLAMP, G_TX_CLAMP, 0, 0);
    gSPVertex(g++, (uintptr_t) npot_uv_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.vbo_len, 3 * (4 + 2));
    EXPECT_NEAR(mock.vbo[10], expected_u, 1e-6);
    EXPECT_NEAR(mock.vbo[17], expected_v, 1e-6);
}

static void test_perf_counters_for_textured_rects_and_tris(void) {
    static Gfx dl[64];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    append_textured_rect_setup(&g, rgba16_texels, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 4,
                               G_TX_CLAMP, G_TX_CLAMP, 2, 2);
    append_textured_rect(&g, 16, 16, 4, 4);
    append_textured_rect(&g, 24, 16, 4, 4);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEXRECTS), 2);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TRIS), 7);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_DRAW_CALLS), 2);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEX_IMPORTS), mock.num_uploads);
    EXPECT_EQ_INT(mock.num_uploads, 1);
}

static void test_redundant_texture_state_batches_texrects(void) {
    static Gfx dl[80];
    Gfx* g = dl;
    int i;

    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetRenderMode(g++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    for (i = 0; i < 4; i++) {
        append_redundant_rgba16_rect(&g, rgba16_texels, 16 + i * 8);
    }
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_tris, 8);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_DRAW_CALLS), 1);
    EXPECT_EQ_INT(ie_perf_get(IE_PERF_TEXRECTS), 4);
}

static void test_changed_texture_flushes_in_source_order(void) {
    static Gfx dl[48];
    Gfx* g = dl;

    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    append_redundant_rgba16_rect(&g, rgba16_texels, 16);
    append_redundant_rgba16_rect(&g, rgba16_texels_alt, 24);
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_uploads, 2);
    EXPECT_EQ_INT(mock.num_draws, 2);
    EXPECT_EQ_INT(mock.num_events, 4);
    EXPECT_EQ_INT(mock.events[0].type, MOCK_EVENT_UPLOAD);
    EXPECT_EQ_INT(mock.events[1].type, MOCK_EVENT_DRAW);
    EXPECT_EQ_INT(mock.events[2].type, MOCK_EVENT_UPLOAD);
    EXPECT_EQ_INT(mock.events[3].type, MOCK_EVENT_DRAW);
}

static void test_sampler_parameter_change_still_flushes(void) {
    static Gfx dl[48];
    Gfx* g = dl;

    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    append_textured_rect_setup(&g, rgba16_texels, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 4,
                               G_TX_CLAMP, G_TX_CLAMP, 2, 2);
    append_textured_rect(&g, 16, 16, 4, 4);
    append_textured_rect_setup(&g, rgba16_texels, G_IM_FMT_RGBA, G_IM_SIZ_16b, 4, 4,
                               G_TX_WRAP, G_TX_WRAP, 2, 2);
    append_textured_rect(&g, 24, 16, 4, 4);
    gSPEndDisplayList(g++);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.num_draws, 2);
}

static void test_texture_alias_guards_still_flush(void) {
    static Gfx fmt_dl[48];
    static Gfx size_dl[48];
    Gfx* g = fmt_dl;

    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    append_redundant_rgba16_rect(&g, rgba16_texels, 16);
    append_textured_rect_setup(&g, rgba16_texels, G_IM_FMT_IA, G_IM_SIZ_16b, 4, 4,
                               G_TX_CLAMP, G_TX_CLAMP, 2, 2);
    append_textured_rect(&g, 24, 16, 4, 4);
    gSPEndDisplayList(g++);

    run_dl(fmt_dl);
    EXPECT_EQ_INT(mock.num_uploads, 2);
    EXPECT_EQ_INT(mock.num_draws, 2);

    g = size_dl;
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    append_redundant_rgba16_rect(&g, rgba16_texels, 16);
    append_textured_rect_setup(&g, rgba16_texels, G_IM_FMT_RGBA, G_IM_SIZ_16b, 2, 4,
                               G_TX_CLAMP, G_TX_CLAMP, 1, 2);
    append_textured_rect(&g, 24, 16, 2, 4);
    gSPEndDisplayList(g++);

    run_dl(size_dl);
    EXPECT_EQ_INT(mock.num_uploads, 2);
    EXPECT_EQ_INT(mock.num_draws, 2);
}

static void test_load_tile_large_texture_upload_uses_full_staging_buffer(void) {
    static Gfx dl[40];
    Gfx* g = dl;
    size_t last_texel = (64u * 250u - 1u) * 2u;
    size_t last_upload = (64u * 250u - 1u) * 4u;

    memset(tall_rgba16_texels, 0, sizeof(tall_rgba16_texels));
    tall_rgba16_texels[0] = 0xF8;
    tall_rgba16_texels[1] = 0x01;
    tall_rgba16_texels[last_texel + 0] = 0x07;
    tall_rgba16_texels[last_texel + 1] = 0xC1;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPLoadTextureTile(g++, tall_rgba16_texels, G_IM_FMT_RGBA, G_IM_SIZ_16b, 64,
                       250, 0, 0, 63, 249, 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
    append_textured_triangle(&g);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.upload_width, 64);
    EXPECT_EQ_INT(mock.upload_height, 250);
    EXPECT_EQ_INT(mock.upload_data[0], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[1], 0x00);
    EXPECT_EQ_INT(mock.upload_data[2], 0x00);
    EXPECT_EQ_INT(mock.upload_data[3], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[last_upload + 0], 0x00);
    EXPECT_EQ_INT(mock.upload_data[last_upload + 1], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[last_upload + 2], 0x00);
    EXPECT_EQ_INT(mock.upload_data[last_upload + 3], 0xFF);
}

static void test_kart_tint_combiner_vbo_inputs(void) {
    static Gfx dl[32];
    Gfx* g = dl;
    uint32_t expected_shader =
        kart_tint_shader_id(SHADER_OPT_ALPHA | SHADER_OPT_TEXTURE_EDGE);
    struct ShaderProgram* created_shader;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetPrimColor(g++, 0, 0, 0x80, 0xA0, 0xC0, 0xE0);
    gDPSetEnvColor(g++, 0x20, 0x40, 0x60, 0x22);
    gDPSetCombineLERP(g++, 1, ENVIRONMENT, TEXEL0, PRIMITIVE, PRIMITIVE, 0, TEXEL0,
                      0, 1, ENVIRONMENT, TEXEL0, PRIMITIVE, PRIMITIVE, 0, TEXEL0,
                      0);
    gDPSetRenderMode(g++, G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2);
    gDPSetTextureImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (uintptr_t) rgba16_texels);
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
               G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 4 * 4 - 1, CALC_DXT(4, G_IM_SIZ_16b_BYTES));
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, 0, G_TX_RENDERTILE, 0,
               G_TX_CLAMP, 2, 0, G_TX_CLAMP, 2, 0);
    append_textured_triangle(&g);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_created_shaders, 1);
    EXPECT_EQ_INT(mock.created_shaders[0], expected_shader);
    EXPECT_EQ_INT(mock.loaded_shader, expected_shader);
    created_shader = mock_lookup_shader(expected_shader);
    EXPECT_TRUE(created_shader != NULL);
    if (created_shader != NULL) {
        EXPECT_EQ_INT(created_shader->cc.used_textures[0], 1);
        EXPECT_EQ_INT(created_shader->cc.used_textures[1], 0);
        EXPECT_EQ_INT(created_shader->cc.num_inputs, 2);
        EXPECT_EQ_INT(created_shader->cc.opt_alpha, 1);
        EXPECT_EQ_INT(created_shader->cc.opt_texture_edge, 1);
    }
    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.vbo_len, 3 * (4 + 2 + 2 * 4));
    /* RGB mapping makes input 1 ENV and input 2 PRIM for this normalised shader. */
    EXPECT_NEAR(mock.vbo[6], 0x20 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[7], 0x40 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[8], 0x60 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[9], 0x22 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[10], 0x80 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[11], 0xA0 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[12], 0xC0 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[13], 0xE0 / 255.0f, 1e-6);
}

static void test_particle_smoke_combiner_vbo_inputs(void) {
    static Gfx dl[32];
    Gfx* g = dl;
    uint32_t expected_shader = particle_smoke_shader_id(SHADER_OPT_ALPHA);
    struct ShaderProgram* created_shader;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetPrimColor(g++, 0, 0, 0xD0, 0x70, 0x30, 0x66);
    gDPSetEnvColor(g++, 0x10, 0x28, 0x48, 0xCC);
    gDPSetCombineLERP(g++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0,
                      PRIMITIVE, 0, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT,
                      TEXEL0, 0, PRIMITIVE, 0);
    gDPSetRenderMode(g++, G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2);
    gDPSetTextureImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (uintptr_t) rgba16_texels);
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
               G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 4 * 4 - 1, CALC_DXT(4, G_IM_SIZ_16b_BYTES));
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, 0, G_TX_RENDERTILE, 0,
               G_TX_CLAMP, 2, 0, G_TX_CLAMP, 2, 0);
    append_textured_triangle(&g);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_created_shaders, 1);
    EXPECT_EQ_INT(mock.created_shaders[0], expected_shader);
    EXPECT_EQ_INT(mock.loaded_shader, expected_shader);
    created_shader = mock_lookup_shader(expected_shader);
    EXPECT_TRUE(created_shader != NULL);
    if (created_shader != NULL) {
        EXPECT_EQ_INT(created_shader->cc.used_textures[0], 1);
        EXPECT_EQ_INT(created_shader->cc.used_textures[1], 0);
        EXPECT_EQ_INT(created_shader->cc.num_inputs, 2);
        EXPECT_EQ_INT(created_shader->cc.opt_alpha, 1);
        EXPECT_EQ_INT(created_shader->cc.opt_texture_edge, 0);
    }
    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.vbo_len, 3 * (4 + 2 + 2 * 4));
    /* RGB mapping makes input 1 PRIM and input 2 ENV for smoke. */
    EXPECT_NEAR(mock.vbo[6], 0xD0 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[7], 0x70 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[8], 0x30 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[9], 0x66 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[10], 0x10 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[11], 0x28 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[12], 0x48 / 255.0f, 1e-6);
    EXPECT_NEAR(mock.vbo[13], 0xCC / 255.0f, 1e-6);
}

static const uint8_t i4_texels[8] = {
    0x0F, 0x17, 0x28, 0x39, 0x4A, 0x5B, 0x6C, 0xDE,
};

static void test_i4_texture_alpha_tracks_intensity(void) {
    static Gfx dl[32];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPLoadTextureBlock_4b(g++, i4_texels, G_IM_FMT_I, 16, 1, 0, G_TX_CLAMP,
                           G_TX_CLAMP, 0, 0, 0, 0);
    append_textured_triangle(&g);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.upload_width, 16);
    EXPECT_EQ_INT(mock.upload_height, 4);
    EXPECT_EQ_INT(mock.upload_data[0], 0x00);
    EXPECT_EQ_INT(mock.upload_data[1], 0x00);
    EXPECT_EQ_INT(mock.upload_data[2], 0x00);
    EXPECT_EQ_INT(mock.upload_data[3], 0x00);
    EXPECT_EQ_INT(mock.upload_data[4], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[5], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[6], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[7], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[8], 0x11);
    EXPECT_EQ_INT(mock.upload_data[9], 0x11);
    EXPECT_EQ_INT(mock.upload_data[10], 0x11);
    EXPECT_EQ_INT(mock.upload_data[11], 0x11);
}

static const uint8_t i8_texels[8] = {
    0x00, 0x11, 0x7F, 0x80, 0xA5, 0xC3, 0xFE, 0xFF,
};

static void test_i8_texture_uses_full_byte_and_alpha(void) {
    static Gfx dl[32];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPLoadTextureBlock(g++, i8_texels, G_IM_FMT_I, G_IM_SIZ_8b, 8, 1, 0, G_TX_CLAMP,
                        G_TX_CLAMP, 0, 0, 0, 0);
    append_textured_triangle(&g);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.upload_width, 8);
    EXPECT_EQ_INT(mock.upload_height, 1);
    EXPECT_EQ_INT(mock.upload_data[8], 0x7F);
    EXPECT_EQ_INT(mock.upload_data[9], 0x7F);
    EXPECT_EQ_INT(mock.upload_data[10], 0x7F);
    EXPECT_EQ_INT(mock.upload_data[11], 0x7F);
    EXPECT_EQ_INT(mock.upload_data[16], 0xA5);
    EXPECT_EQ_INT(mock.upload_data[17], 0xA5);
    EXPECT_EQ_INT(mock.upload_data[18], 0xA5);
    EXPECT_EQ_INT(mock.upload_data[19], 0xA5);
}

static const uint8_t ci_palette[8] = {
    0x00, 0x01, 0xF8, 0x01, 0x07, 0xC1, 0x00, 0x3F,
};

static void load_test_palette(Gfx** gp) {
    Gfx* g = *gp;

    gDPSetTextureImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (uintptr_t) ci_palette);
    gDPSetTile(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
               G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPLoadTLUTCmd(g++, G_TX_LOADTILE, 3);

    *gp = g;
}

static const uint8_t ci8_texels[8] = {
    0x00, 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x00,
};

static void test_ci8_texture_uses_tlut_rgba16_alpha(void) {
    static Gfx dl[40];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    load_test_palette(&g);
    gDPLoadTextureBlock(g++, ci8_texels, G_IM_FMT_CI, G_IM_SIZ_8b, 8, 1, 0, G_TX_CLAMP,
                        G_TX_CLAMP, 0, 0, 0, 0);
    append_textured_triangle(&g);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.upload_width, 8);
    EXPECT_EQ_INT(mock.upload_height, 1);
    EXPECT_EQ_INT(mock.upload_data[3], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[4], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[5], 0x00);
    EXPECT_EQ_INT(mock.upload_data[6], 0x00);
    EXPECT_EQ_INT(mock.upload_data[7], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[8], 0x00);
    EXPECT_EQ_INT(mock.upload_data[9], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[10], 0x00);
    EXPECT_EQ_INT(mock.upload_data[11], 0xFF);
}

static const uint8_t ci4_wide_texels[16] = {
    0x12, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void test_ci4_subimage_starts_at_first_nibble(void) {
    static Gfx dl[40];
    Gfx* g = dl;

    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    load_test_palette(&g);
    gDPSetTextureImage(g++, G_IM_FMT_CI, G_IM_SIZ_16b, 16, (uintptr_t) ci4_wide_texels);
    gDPSetTile(g++, G_IM_FMT_CI, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
               G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPLoadBlock(g++, G_TX_LOADTILE, 0, 0, 3, 0);
    gDPSetTile(g++, G_IM_FMT_CI, G_IM_SIZ_4b, 1, 0, G_TX_RENDERTILE, 0,
               G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    append_textured_triangle(&g);

    run_dl(dl);

    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_uploads, 1);
    EXPECT_EQ_INT(mock.upload_width, 16);
    EXPECT_EQ_INT(mock.upload_height, 1);
    EXPECT_EQ_INT(mock.upload_data[0], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[1], 0x00);
    EXPECT_EQ_INT(mock.upload_data[2], 0x00);
    EXPECT_EQ_INT(mock.upload_data[3], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[4], 0x00);
    EXPECT_EQ_INT(mock.upload_data[5], 0xFF);
    EXPECT_EQ_INT(mock.upload_data[6], 0x00);
    EXPECT_EQ_INT(mock.upload_data[7], 0xFF);
}

static void test_fill_rectangle(void) {
    static uint16_t color_buf[4];
    static uint16_t depth_buf[4];
    static Gfx dl[12];
    Gfx* g = dl;

    gDPSetDepthImage(g++, (uintptr_t) depth_buf);
    gDPSetColorImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, (uintptr_t) color_buf);
    gDPSetCycleType(g++, G_CYC_FILL);
    gDPSetRenderMode(g++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetFillColor(g++, (GPACK_RGBA5551(255, 0, 0, 1) << 16) | GPACK_RGBA5551(255, 0, 0, 1));
    gDPFillRectangle(g++, 0, 0, 319, 239);
    gSPEndDisplayList(g++);

    run_dl(dl);
    EXPECT_EQ_INT(mock.num_draws, 1);
    EXPECT_EQ_INT(mock.num_tris, 2);
}

static void test_viewport_and_scissor(void) {
    /* Full 320x240 N64 screen: vscale/vtrans carry 2 fraction bits and
     * half-extents, so both are {640, 480} for the full screen. */
    static const Vp_t vp = { { 640, 480, 511, 0 }, { 640, 480, 511, 0 } };
    static Gfx dl[12];
    Gfx* g = dl;

    gSPViewport(g++, (uintptr_t) &vp);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, 320, 240);
    /* State is applied lazily at the next draw. */
    gSPMatrix(g++, (uintptr_t) scale_proj, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, (uintptr_t) identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH | G_FOG);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPVertex(g++, (uintptr_t) tri_vertices, 3, 0);
    gSP1Triangle(g++, 0, 1, 2, 0);
    gSPEndDisplayList(g++);

    run_dl(dl);
    EXPECT_EQ_INT(mock.num_viewports, 1);
    /* vscale 320x240 -> full 320x240 N64 screen, scaled 2x to 640x480. */
    EXPECT_EQ_INT(mock.viewport_w, 640);
    EXPECT_EQ_INT(mock.viewport_h, 480);
    EXPECT_EQ_INT(mock.viewport_x, 0);
    EXPECT_EQ_INT(mock.viewport_y, 0);
    EXPECT_EQ_INT(mock.num_scissors, 1);
    EXPECT_EQ_INT(mock.scissor_w, 640);
    EXPECT_EQ_INT(mock.scissor_h, 480);
}

static void test_inherited_markers_are_skipped(void) {
    /* The previous port injected 0x424C4E44 marker commands into the
     * display stream; the neutral translator must skip them. */
    static Gfx dl[8];
    Gfx* g = dl;

    g->words.w0 = 0x424C4E44;
    g->words.w1 = 0x4655434C;
    g++;
    gSPEndDisplayList(g++);

    run_dl(dl);
    EXPECT_EQ_INT(mock.num_draws, 0);
}

int main(void) {
    test_init();
    test_shade_triangle();
    test_clip_rejected_triangle_not_drawn();
    test_near_plane_straddling_triangle_is_clipped();
    test_near_plane_fully_behind_triangle_not_drawn();
    test_tiny_w_triangle_emits_finite_vbo_for_backend_guard();
    test_nuke_everything_resubmits_non_decal_zmode();
    test_nested_display_list();
    test_matrix_command_does_not_flush_buffered_triangles();
    test_textured_triangle();
    test_npot_texture_coordinates_use_tile_reciprocal();
    test_perf_counters_for_textured_rects_and_tris();
    test_redundant_texture_state_batches_texrects();
    test_changed_texture_flushes_in_source_order();
    test_sampler_parameter_change_still_flushes();
    test_texture_alias_guards_still_flush();
    test_load_tile_large_texture_upload_uses_full_staging_buffer();
    test_kart_tint_combiner_vbo_inputs();
    test_particle_smoke_combiner_vbo_inputs();
    test_i4_texture_alpha_tracks_intensity();
    test_i8_texture_uses_full_byte_and_alpha();
    test_ci8_texture_uses_tlut_rgba16_alpha();
    test_ci4_subimage_starts_at_first_nibble();
    test_fill_rectangle();
    test_viewport_and_scissor();
    test_inherited_markers_are_skipped();
    return test_finish("test_gfx_translate");
}
