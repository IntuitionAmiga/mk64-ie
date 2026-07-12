#include <math.h>
#include <string.h>

#include "gfx_fast3d.h"
#include "ie/ie_perf_counters.h"

#ifdef IE_TNL_COPROC
#include "../../ie/coproc/ie_coproc.h"
/* Compile-time guard: the wire offsets the x86 service writes must match the
 * structs this file consumes. */
#define IE_TNL_ASSERT_LAYOUT
#include "../../ie/coproc/tnl_proto.h"
#endif

struct F3DRsp gF3dRsp;

/* Tracks whether MP_matrix currently equals modelview_top * P_matrix. Only
 * true after gfx_sp_matrix / gfx_sp_pop_matrix actually recompute MP. It is
 * cleared whenever the stack pointer moves without a recompute (reset, and a
 * pop that empties the stack), because then MP still reflects the OLD top.
 * The matrix-reuse fast path may skip the MP recompute only when this is
 * already set - otherwise a stale MP would transform vertices wrongly. */
static int mp_consistent;

void gfx_sp_reset(void) {
    gF3dRsp.modelview_matrix_stack_size = 1;
    gF3dRsp.current_num_lights = 2;
    gF3dRsp.lights_changed = 1;
    mp_consistent = 0; /* reset moves the stack top without touching MP */
}

#define recip127 0.00787402f
#define recip2pi 0.159155f

static void gfx_normalize_vector(float v[3]) {
    float magSqr = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    float s;

    if (magSqr == 0.0f) {
        return;
    }

    s = sqrtf(magSqr);
    v[0] /= s;
    v[1] /= s;
    v[2] /= s;
}

static void gfx_transposed_matrix_mul(float res[3], const float a[3], const float b[4][4]) {
    res[0] = a[0] * b[0][0] + a[1] * b[0][1] + a[2] * b[0][2];
    res[1] = a[0] * b[1][0] + a[1] * b[1][1] + a[2] * b[1][2];
    res[2] = a[0] * b[2][0] + a[1] * b[2][1] + a[2] * b[2][2];
}

void calculate_normal_dir(const Light_t* light, float coeffs[3]) {
    float light_dir[3] = { light->dir[0] * recip127, light->dir[1] * recip127, light->dir[2] * recip127 };
    gfx_transposed_matrix_mul(
        coeffs, light_dir,
        (const float(*)[4]) gF3dRsp.modelview_matrix_stack[gF3dRsp.modelview_matrix_stack_size - 1]);
    gfx_normalize_vector(coeffs);
}

void gfx_matrix_mul(float res[4][4], const float a[4][4], const float b[4][4]) {
    float tmp[4][4];
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(res, tmp, sizeof(tmp));
}

void gfx_sp_matrix(uint8_t parameters, const float matrix[4][4]) {
    /* Loop-invariant matrix reuse: a plain LOAD (no PUSH, no multiply) of a
     * matrix byte-identical to the resident one leaves that matrix - and,
     * when MP is already in sync, the derived MP_matrix - exactly as they are,
     * so the memcpy and the full 4x4 MP recompute are pure recomputation of an
     * unchanged result. The game reloads identical matrices constantly (shared
     * object/base matrices, repeated identity for 2D). Skip, staying bit-exact.
     * Requires mp_consistent: after a reset/stack-empty the resident matrix can
     * match while MP still reflects a different (old) top, so skipping the
     * recompute then would leave a stale MP. */
    if (mp_consistent && (parameters & G_MTX_LOAD) && !(parameters & G_MTX_PUSH)) {
        const float (*resident)[4] = (parameters & G_MTX_PROJECTION)
            ? (const float(*)[4]) gF3dRsp.P_matrix
            : (const float(*)[4]) gF3dRsp.modelview_matrix_stack
                  [gF3dRsp.modelview_matrix_stack_size - 1];
        if (memcmp(resident, matrix, sizeof(gF3dRsp.P_matrix)) == 0) {
            IE_PERF_INC(IE_PERF_VTX_MTX_REUSE);
            return;
        }
    }
    if (parameters & G_MTX_PROJECTION) {
        if (parameters & G_MTX_LOAD) {
            memcpy(gF3dRsp.P_matrix, matrix, sizeof(gF3dRsp.P_matrix));
        } else {
            gfx_matrix_mul(gF3dRsp.P_matrix, matrix, (const float(*)[4]) gF3dRsp.P_matrix);
        }
    } else { // G_MTX_MODELVIEW
        if ((parameters & G_MTX_PUSH) && gF3dRsp.modelview_matrix_stack_size < F3D_MATRIX_STACK_DEPTH) {
            ++gF3dRsp.modelview_matrix_stack_size;
            memcpy(gF3dRsp.modelview_matrix_stack[gF3dRsp.modelview_matrix_stack_size - 1],
                   gF3dRsp.modelview_matrix_stack[gF3dRsp.modelview_matrix_stack_size - 2],
                   sizeof(gF3dRsp.modelview_matrix_stack[0]));
        }
        if (parameters & G_MTX_LOAD) {
            memcpy(gF3dRsp.modelview_matrix_stack[gF3dRsp.modelview_matrix_stack_size - 1], matrix,
                   sizeof(gF3dRsp.modelview_matrix_stack[0]));
        } else {
            gfx_matrix_mul(gF3dRsp.modelview_matrix_stack[gF3dRsp.modelview_matrix_stack_size - 1], matrix,
                           (const float(*)[4]) gF3dRsp.modelview_matrix_stack[gF3dRsp.modelview_matrix_stack_size - 1]);
        }
        gF3dRsp.lights_changed = 1;
    }
    gfx_matrix_mul(gF3dRsp.MP_matrix,
                   (const float(*)[4]) gF3dRsp.modelview_matrix_stack[gF3dRsp.modelview_matrix_stack_size - 1],
                   (const float(*)[4]) gF3dRsp.P_matrix);
    mp_consistent = 1;
}

void gfx_sp_pop_matrix(uint32_t count) {
    while (count--) {
        if (gF3dRsp.modelview_matrix_stack_size > 0) {
            --gF3dRsp.modelview_matrix_stack_size;
        }
    }
    if (gF3dRsp.modelview_matrix_stack_size > 0) {
        gfx_matrix_mul(gF3dRsp.MP_matrix,
                       (const float(*)[4]) gF3dRsp.modelview_matrix_stack[gF3dRsp.modelview_matrix_stack_size - 1],
                       (const float(*)[4]) gF3dRsp.P_matrix);
        mp_consistent = 1;
    } else {
        mp_consistent = 0; /* no top: MP left as-is, no longer trustworthy */
    }
}

/*
 * Transform an object-space position by the combined modelview-projection
 * matrix. The matrices follow the N64 row-vector convention: the resulting
 * component i is the dot product of the position with matrix column i.
 */
static void gfx_transform_vertex(float out[4], const float in[3], const float m[4][4]) {
    out[0] = in[0] * m[0][0] + in[1] * m[1][0] + in[2] * m[2][0] + m[3][0];
    out[1] = in[0] * m[0][1] + in[1] * m[1][1] + in[2] * m[2][1] + m[3][1];
    out[2] = in[0] * m[0][2] + in[1] * m[1][2] + in[2] * m[2][2] + m[3][2];
    out[3] = in[0] * m[0][3] + in[1] * m[1][3] + in[2] * m[2][3] + m[3][3];
}

static uint8_t gfx_clip_reject(float x, float y, float z, float w) {
    uint8_t cr = 0;
    cr = !(z > w);
    cr = (cr << 1) | !(z < -w);
    cr = (cr << 1) | !(y > w);
    cr = (cr << 1) | !(y < -w);
    cr = (cr << 1) | !(x > w);
    cr = (cr << 1) | !(x < -w);
    return cr;
}

static void gfx_sp_vertex_light(size_t n_vertices, size_t dest_index, const Vtx* vertices) {
    for (size_t i = 0; i < n_vertices; i++, dest_index++) {
        const Vtx_t* v = &vertices[i].v;
        const Vtx_tn* vn = &vertices[i].n;
        struct F3DLoadedVertex* d = &gF3dRsp.loaded_vertices[dest_index];

        float in[3] = { v->ob[0], v->ob[1], v->ob[2] };
        float out[4];
        gfx_transform_vertex(out, in, (const float(*)[4]) gF3dRsp.MP_matrix);
        float x = out[0];
        float y = out[1];
        float z = out[2];
        float w = out[3];

        short U = v->tc[0] * gF3dRsp.texture_scaling_factor.s >> 16;
        short V = v->tc[1] * gF3dRsp.texture_scaling_factor.t >> 16;

        int r = gF3dRsp.current_lights[gF3dRsp.current_num_lights - 1].col[0];
        int g = gF3dRsp.current_lights[gF3dRsp.current_num_lights - 1].col[1];
        int b = gF3dRsp.current_lights[gF3dRsp.current_num_lights - 1].col[2];

        if (gF3dRsp.current_num_lights == 2) {
            float intensity = recip127 * (vn->n[0] * gF3dRsp.current_lights_coeffs[0][0] +
                                          vn->n[1] * gF3dRsp.current_lights_coeffs[0][1] +
                                          vn->n[2] * gF3dRsp.current_lights_coeffs[0][2]);

            if (intensity > 0.0f) {
                r += intensity * gF3dRsp.current_lights[0].col[0];
                g += intensity * gF3dRsp.current_lights[0].col[1];
                b += intensity * gF3dRsp.current_lights[0].col[2];
            }
        }

        d->color.r = r > 255 ? 255 : r;
        d->color.g = g > 255 ? 255 : g;
        d->color.b = b > 255 ? 255 : b;
        d->color.a = v->cn[3];

        if (gF3dRsp.geometry_mode & G_TEXTURE_GEN) {
            float dotx = recip127 * (vn->n[0] * gF3dRsp.current_lookat_coeffs[0][0] +
                                     vn->n[1] * gF3dRsp.current_lookat_coeffs[0][1] +
                                     vn->n[2] * gF3dRsp.current_lookat_coeffs[0][2]);
            float doty = recip127 * (vn->n[0] * gF3dRsp.current_lookat_coeffs[1][0] +
                                     vn->n[1] * gF3dRsp.current_lookat_coeffs[1][1] +
                                     vn->n[2] * gF3dRsp.current_lookat_coeffs[1][2]);

            if (dotx < -1.0f)
                dotx = -1.0f;
            else if (dotx > 1.0f)
                dotx = 1.0f;

            if (doty < -1.0f)
                doty = -1.0f;
            else if (doty > 1.0f)
                doty = 1.0f;

            if (gF3dRsp.geometry_mode & G_TEXTURE_GEN_LINEAR) {
                dotx = acosf(-dotx) * recip2pi;
                doty = acosf(-doty) * recip2pi;
            } else {
                dotx = (dotx * 0.25f) + 0.25f;
                doty = (doty * 0.25f) + 0.25f;
            }

            U = (int32_t) (dotx * gF3dRsp.texture_scaling_factor.s);
            V = (int32_t) (doty * gF3dRsp.texture_scaling_factor.t);
        }

        d->u = U;
        d->v = V;

        d->wlt0 = (w < 0);
        d->clip_rej = gfx_clip_reject(x, y, z, w);

        d->x = v->ob[0];
        d->y = v->ob[1];
        d->z = v->ob[2];
        d->w = 1.0f;

        d->_x = x;
        d->_y = y;
        d->_z = z;
        d->_w = w;
    }
}

static void gfx_sp_vertex_no(size_t n_vertices, size_t dest_index, const Vtx* vertices) {
    for (size_t i = 0; i < n_vertices; i++, dest_index++) {
        const Vtx_t* v = &vertices[i].v;
        struct F3DLoadedVertex* d = &gF3dRsp.loaded_vertices[dest_index];

        d->x = v->ob[0];
        d->y = v->ob[1];
        d->z = v->ob[2];
        d->w = 1.0f;

        float in[3] = { d->x, d->y, d->z };
        float out[4];
        gfx_transform_vertex(out, in, (const float(*)[4]) gF3dRsp.MP_matrix);
        d->_x = out[0];
        d->_y = out[1];
        d->_z = out[2];
        d->_w = out[3];

        d->color.r = v->cn[0];
        d->color.g = v->cn[1];
        d->color.b = v->cn[2];
        d->color.a = v->cn[3];

        d->u = (float) (v->tc[0] * gF3dRsp.texture_scaling_factor.s >> 16);
        d->v = (float) (v->tc[1] * gF3dRsp.texture_scaling_factor.t >> 16);

        d->wlt0 = (d->_w < 0);
        d->clip_rej = gfx_clip_reject(d->_x, d->_y, d->_z, d->_w);
    }
}

#ifdef IE_TNL_COPROC
/* Dispatch the batch to the x86 T&L coprocessor. The worker transforms in
 * parallel with the M68K's continued display-list walk; gfx_tnl_drain() (called
 * before anything touches loaded_vertices) collects the result. Falls back to
 * the local path when the service is offline or the batch needs acosf
 * (G_TEXTURE_GEN_LINEAR), which the freestanding service lacks. */
static int gfx_sp_vertex_coproc(size_t n_vertices, size_t dest_index, const Vtx* vertices) {
    ie_tnl_params p;

    if ((gF3dRsp.geometry_mode & (G_TEXTURE_GEN_LINEAR | G_TEXTURE_GEN)) ==
        (G_TEXTURE_GEN_LINEAR | G_TEXTURE_GEN)) {
        /* Falling back to the LOCAL path: a previous async batch may
         * still be writing loaded_vertices slots the local path is
         * about to reuse - drain it first or the two writers race. */
        ie_coproc_drain();
        return 0;
    }
    p.m = (const float*) gF3dRsp.MP_matrix;
    p.ss = gF3dRsp.texture_scaling_factor.s;
    p.st = gF3dRsp.texture_scaling_factor.t;
    p.geomode = gF3dRsp.geometry_mode;
    p.num_lights = 0;
    p.amb_col[0] = p.amb_col[1] = p.amb_col[2] = 0;
    p.dir_col[0] = p.dir_col[1] = p.dir_col[2] = 0;
    p.light_coef = 0;
    p.lookat = 0;
    if (gF3dRsp.geometry_mode & G_LIGHTING) {
        unsigned nl = gF3dRsp.current_num_lights;
        if (nl < 1 || nl > F3D_MAX_LIGHTS) {
            ie_coproc_drain(); /* see texgen-linear fallback above */
            return 0;          /* out-of-contract light count: local path */
        }
        p.num_lights = nl;
        p.amb_col[0] = gF3dRsp.current_lights[nl - 1].col[0];
        p.amb_col[1] = gF3dRsp.current_lights[nl - 1].col[1];
        p.amb_col[2] = gF3dRsp.current_lights[nl - 1].col[2];
        p.dir_col[0] = gF3dRsp.current_lights[0].col[0];
        p.dir_col[1] = gF3dRsp.current_lights[0].col[1];
        p.dir_col[2] = gF3dRsp.current_lights[0].col[2];
        p.light_coef = gF3dRsp.current_lights_coeffs[0];
        p.lookat = gF3dRsp.current_lookat_coeffs[0];
    }
    p.n = n_vertices;
    p.verts = vertices;
    p.out = &gF3dRsp.loaded_vertices[dest_index];
    return ie_coproc_tnl_batch_async(&p);
}

void gfx_tnl_drain(void) {
    ie_coproc_drain();
}
#endif /* IE_TNL_COPROC */

void gfx_sp_vertex(size_t n_vertices, size_t dest_index, const Vtx* vertices) {
    if (gF3dRsp.geometry_mode & G_LIGHTING) {
        if (gF3dRsp.lights_changed) {
            if (gF3dRsp.current_num_lights == 2) {
                calculate_normal_dir(&gF3dRsp.current_lights[0], gF3dRsp.current_lights_coeffs[0]);
            }
            static const Light_t lookat_x = { { 0, 0, 0 }, 0, { 0, 0, 0 }, 0, { 127, 0, 0 }, 0 };
            static const Light_t lookat_y = { { 0, 0, 0 }, 0, { 0, 0, 0 }, 0, { 0, 127, 0 }, 0 };
            calculate_normal_dir(&lookat_x, gF3dRsp.current_lookat_coeffs[0]);
            calculate_normal_dir(&lookat_y, gF3dRsp.current_lookat_coeffs[1]);
            gF3dRsp.lights_changed = 0;
        }
#ifdef IE_TNL_COPROC
        if (gfx_sp_vertex_coproc(n_vertices, dest_index, vertices)) {
            return;
        }
#endif
        gfx_sp_vertex_light(n_vertices, dest_index, vertices);
    } else {
#ifdef IE_TNL_COPROC
        if (gfx_sp_vertex_coproc(n_vertices, dest_index, vertices)) {
            return;
        }
#endif
        gfx_sp_vertex_no(n_vertices, dest_index, vertices);
    }
}
