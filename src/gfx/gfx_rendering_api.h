#ifndef GFX_RENDERING_API_H
#define GFX_RENDERING_API_H

#include <stddef.h>
#include <stdint.h>

/*
 * Neutral renderer-backend contract, driven by the display-list
 * translator in src/gfx/gfx_pc.c.
 *
 * - Shaders are identified by the 32-bit shader id produced from the
 *   N64 combine mode plus SHADER_OPT_* flags (see gfx_cc.h);
 *   shader_get_info reports how many colour inputs and textures the
 *   shader consumes.
 * - upload_texture receives RGBA32 texel data in row-major order.
 * - new_texture returns a backend-chosen id; ids are only compared for
 *   identity by the translator.
 * - draw_triangles receives an interleaved float vertex buffer with,
 *   per vertex: clip-space x, y, z, w; then u, v when the shader uses a
 *   texture; then fog r, g, b, intensity (all 0..1) when fog is on;
 *   then r, g, b (plus a when alpha is in use) for each shader colour
 *   input. buf_vbo_len counts floats.
 */

/*
 * Maximum number of logical texture IDs the translator can keep alive in
 * one cache epoch. Backends that retain CPU-side texture copies must be
 * able to represent at least this many IDs.
 */
#define GFX_TEXTURE_CACHE_SIZE 1024u

/*
 * Maximum decoded texture upload size. The translator expands all
 * formats to RGBA32 before calling upload_texture; large load-tile
 * imports in the game can approach the Voodoo texture window limit.
 */
#define GFX_MAX_TEXTURE_BYTES 0x10000u
#define GFX_MAX_TEXTURE_TEXELS (GFX_MAX_TEXTURE_BYTES / 4u)

struct ShaderProgram;

struct GfxRenderingAPI {
    uint8_t (*z_is_from_0_to_1)(void);
    void (*unload_shader)(struct ShaderProgram *old_prg);
    void (*load_shader)(struct ShaderProgram *new_prg);
    struct ShaderProgram *(*create_and_load_new_shader)(uint32_t shader_id);
    struct ShaderProgram *(*lookup_shader)(uint32_t shader_id);
    void (*shader_get_info)(struct ShaderProgram *prg, uint8_t *num_inputs, uint8_t used_textures[2]);
    uint32_t (*new_texture)(void);
    void (*select_texture)(int tile, uint32_t texture_id);
    void (*upload_texture)(const uint8_t *rgba32_buf, int width, int height);
    void (*set_sampler_parameters)(int sampler, uint8_t linear_filter, uint32_t cms, uint32_t cmt);
    void (*set_depth_test)(uint8_t depth_test);
    void (*set_depth_mask)(uint8_t z_upd);
    void (*set_zmode_decal)(uint8_t zmode_decal);
    void (*set_viewport)(int x, int y, int width, int height);
    void (*set_scissor)(int x, int y, int width, int height);
    void (*set_use_alpha)(uint8_t use_alpha);
    void (*draw_triangles)(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris);
    void (*init)(void);
    void (*on_resize)(void);
    void (*start_frame)(void);
    void (*end_frame)(void);
    void (*finish_render)(void);
};

#endif
