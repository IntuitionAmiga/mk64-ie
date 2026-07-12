#ifndef GFX_FAST3D_H
#define GFX_FAST3D_H

/*
 * Portable Fast3D translation core: RSP matrix stack, vertex transform,
 * and vertex lighting, extracted from the previous console port's fused
 * translator/renderer. This module is backend-independent; a full display
 * list translator built on top of it, submitting through GfxRenderingAPI,
 * is Stage 2/3 work.
 */

#include <stddef.h>
#include <stdint.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#define F3D_MAX_LIGHTS 2
#define F3D_MAX_VERTICES 64
#define F3D_MATRIX_STACK_DEPTH 11

struct F3DVertexColor {
    uint8_t r, g, b, a;
};

/* With the x86 T&L coprocessor enabled, vertex batches transform in parallel
 * with the display-list walk; call gfx_tnl_drain() before reading or writing
 * loaded_vertices so an in-flight batch has landed. No-op otherwise. */
#ifdef IE_TNL_COPROC
void gfx_tnl_drain(void);
#else
#define gfx_tnl_drain() ((void) 0)
#endif

struct F3DLoadedVertex {
    /* object-space position (w always 1.0f) */
    float x, y, z, w;
    /* clip-space position */
    float _x, _y, _z, _w;
    /* texture coordinates after scaling / texgen */
    float u, v;
    struct F3DVertexColor color;
    /* per-plane trivial-rejection bits, see gfx_sp_vertex */
    uint8_t clip_rej;
    /* clip-space w < 0 */
    uint8_t wlt0;
};

struct F3DRsp {
    float modelview_matrix_stack[F3D_MATRIX_STACK_DEPTH][4][4];
    float MP_matrix[4][4];
    float P_matrix[4][4];

    Light_t current_lights[F3D_MAX_LIGHTS + 1];
    float current_lights_coeffs[F3D_MAX_LIGHTS][3];
    float current_lookat_coeffs[2][3]; /* lookat_x, lookat_y */

    uint32_t geometry_mode;
    struct {
        /* U0.16 */
        uint16_t s, t;
    } texture_scaling_factor;

    uint8_t modelview_matrix_stack_size;
    uint8_t current_num_lights; /* includes ambient light */
    uint8_t lights_changed;

    struct F3DLoadedVertex loaded_vertices[F3D_MAX_VERTICES + 4];
};

extern struct F3DRsp gF3dRsp;

/*
 * Reset the per-display-list RSP state (matrix stack depth, light count,
 * light-recompute flag). Must be called before processing each display
 * list - matrix commands assume modelview_matrix_stack_size >= 1, so
 * using the zero-initialised gF3dRsp without this underflows the stack.
 * Mirrors the removed translator's gfx_sp_reset(), which ran at the top
 * of every gfx_run().
 */
void gfx_sp_reset(void);

/* res = a * b (row-major); res may alias a or b. */
void gfx_matrix_mul(float res[4][4], const float a[4][4], const float b[4][4]);

/* G_MTX_* handling for a float matrix already fetched from the display list. */
void gfx_sp_matrix(uint8_t parameters, const float matrix[4][4]);

void gfx_sp_pop_matrix(uint32_t count);

/* Transform a light direction into the current model space. */
void calculate_normal_dir(const Light_t* light, float coeffs[3]);

/* Load, transform, light, and clip-test vertices from a display list. */
void gfx_sp_vertex(size_t n_vertices, size_t dest_index, const Vtx* vertices);

#endif /* GFX_FAST3D_H */
