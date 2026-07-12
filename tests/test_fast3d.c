/*
 * Golden tests for the portable Fast3D translation core (matrix stack,
 * vertex transform, vertex lighting) extracted from the removed
 * platform-fused translator.
 */
#include <ultra64.h>
#include <math.h>

#include "test_support.h"
#include "gfx/gfx_fast3d.h"

static void set_identity(float m[4][4]) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            m[r][c] = (r == c) ? 1.0f : 0.0f;
        }
    }
}

static void reset_rsp(void) {
    memset(&gF3dRsp, 0, sizeof(gF3dRsp));
    gfx_sp_reset();
    gF3dRsp.current_num_lights = 0;
    set_identity(gF3dRsp.modelview_matrix_stack[0]);
    set_identity(gF3dRsp.P_matrix);
    set_identity(gF3dRsp.MP_matrix);
    gF3dRsp.texture_scaling_factor.s = 0xFFFF;
    gF3dRsp.texture_scaling_factor.t = 0xFFFF;
}

static void test_reset_from_cold_state(void) {
    /*
     * The production init path: a backend calls gfx_sp_reset() on a
     * freshly zero-initialised gF3dRsp before processing a display list.
     * Without it the first matrix command would index stack[-1].
     */
    float identity[4][4];

    memset(&gF3dRsp, 0, sizeof(gF3dRsp));
    gfx_sp_reset();

    EXPECT_EQ_INT(gF3dRsp.modelview_matrix_stack_size, 1);
    EXPECT_EQ_INT(gF3dRsp.current_num_lights, 2);
    EXPECT_EQ_INT(gF3dRsp.lights_changed, 1);

    /* First matrix commands after reset must land in stack slot 0. */
    set_identity(identity);
    identity[3][0] = 7.0f;
    gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH, (const float(*)[4]) identity);
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH, (const float(*)[4]) identity);

    EXPECT_EQ_INT(gF3dRsp.modelview_matrix_stack_size, 1);
    EXPECT_NEAR(gF3dRsp.modelview_matrix_stack[0][3][0], 7.0f, 0.0f);
    EXPECT_NEAR(gF3dRsp.P_matrix[3][0], 7.0f, 0.0f);
    /* MP = modelview * P: translation rows combine. */
    EXPECT_NEAR(gF3dRsp.MP_matrix[3][0], 14.0f, 0.0f);

    /* Popping the last entry must not recompute MP from stack[-1]. */
    gfx_sp_pop_matrix(1);
    EXPECT_EQ_INT(gF3dRsp.modelview_matrix_stack_size, 0);
}

/*
 * Matrix-reuse fast path: a plain LOAD byte-identical to the resident matrix
 * is skipped. This must stay correct across the stale-MP hazard: gfx_sp_reset
 * moves the stack top back to slot 0 WITHOUT recomputing MP, so MP still holds
 * the pre-reset product. Reloading a matrix identical to the new top must NOT
 * be short-circuited to the stale MP - the mp_consistent guard forces a
 * recompute after a reset.
 */
static void test_matrix_reuse_after_reset(void) {
    float proj[4][4], mv_a[4][4], mv_b[4][4], ident[4][4];

    reset_rsp(); /* stack[0] = identity, P = identity */

    set_identity(proj);
    set_identity(mv_a);
    mv_a[3][0] = 3.0f;
    set_identity(mv_b);
    mv_b[3][0] = 9.0f;

    gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH, (const float(*)[4]) proj);
    /* Grow the stack so the top is no longer slot 0, and MP reflects mv_b. */
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH, (const float(*)[4]) mv_a);
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH, (const float(*)[4]) mv_b);
    EXPECT_NEAR(gF3dRsp.MP_matrix[3][0], 9.0f, 0.0f); /* MP now stale-worthy */

    /* Reset: size -> 1, top is slot 0 (identity), but MP still holds mv_b*P. */
    gfx_sp_reset();
    EXPECT_EQ_INT(gF3dRsp.modelview_matrix_stack_size, 1);

    /* Reload a modelview identical to the new top (slot 0 == identity). The
     * correct MP is identity * P = identity, so [3][0] == 0. A reuse skip that
     * ignored the reset would wrongly leave the stale 9. */
    set_identity(ident);
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH, (const float(*)[4]) ident);
    EXPECT_NEAR(gF3dRsp.MP_matrix[3][0], 0.0f, 0.0f);
}

/*
 * The reuse fast path must be transparent in the common case: reloading the
 * same non-identity matrix a second time leaves MP exactly as a full recompute
 * would.
 */
static void test_matrix_reuse_idempotent(void) {
    float proj[4][4], mv[4][4];

    reset_rsp();
    set_identity(proj);
    proj[0][0] = 2.0f;
    set_identity(mv);
    mv[3][1] = 4.0f;

    gfx_sp_matrix(G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH, (const float(*)[4]) proj);
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH, (const float(*)[4]) mv);
    float mp_x = gF3dRsp.MP_matrix[0][0];
    float mp_ty = gF3dRsp.MP_matrix[3][1];

    /* Byte-identical reload -> skipped, MP unchanged. */
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH, (const float(*)[4]) mv);
    EXPECT_NEAR(gF3dRsp.MP_matrix[0][0], mp_x, 0.0f);
    EXPECT_NEAR(gF3dRsp.MP_matrix[3][1], mp_ty, 0.0f);

    /* A genuinely different reload still updates. */
    mv[3][1] = 7.0f;
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH, (const float(*)[4]) mv);
    EXPECT_NEAR(gF3dRsp.MP_matrix[3][1], 7.0f, 0.0f);
}

static void test_matrix_mul(void) {
    float a[4][4], b[4][4], res[4][4];

    set_identity(a);
    a[3][0] = 1.0f;
    a[3][1] = 2.0f;
    a[3][2] = 3.0f;

    set_identity(b);
    b[0][0] = b[1][1] = b[2][2] = 2.0f;

    gfx_matrix_mul(res, (const float(*)[4]) a, (const float(*)[4]) b);
    EXPECT_NEAR(res[0][0], 2.0f, 0.0f);
    EXPECT_NEAR(res[3][0], 2.0f, 0.0f);
    EXPECT_NEAR(res[3][1], 4.0f, 0.0f);
    EXPECT_NEAR(res[3][2], 6.0f, 0.0f);

    /* aliasing safety */
    gfx_matrix_mul(a, (const float(*)[4]) a, (const float(*)[4]) b);
    EXPECT_NEAR(a[3][0], 2.0f, 0.0f);
}

static void test_matrix_stack(void) {
    float translate[4][4];

    reset_rsp();

    set_identity(translate);
    translate[3][0] = 5.0f;

    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH, (const float(*)[4]) translate);
    EXPECT_EQ_INT(gF3dRsp.modelview_matrix_stack_size, 2);
    EXPECT_NEAR(gF3dRsp.MP_matrix[3][0], 5.0f, 0.0f);

    /* multiply another translation on top */
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_NOPUSH, (const float(*)[4]) translate);
    EXPECT_NEAR(gF3dRsp.MP_matrix[3][0], 10.0f, 0.0f);

    gfx_sp_pop_matrix(1);
    EXPECT_EQ_INT(gF3dRsp.modelview_matrix_stack_size, 1);
    EXPECT_NEAR(gF3dRsp.MP_matrix[3][0], 0.0f, 0.0f);
}

static void test_vertex_transform(void) {
    Vtx v[1];

    reset_rsp();
    memset(v, 0, sizeof(v));
    v[0].v.ob[0] = 10;
    v[0].v.ob[1] = 20;
    v[0].v.ob[2] = 30;
    v[0].v.cn[0] = 40;
    v[0].v.cn[1] = 50;
    v[0].v.cn[2] = 60;
    v[0].v.cn[3] = 70;

    /* identity MP: clip coords equal object coords, w = 1 */
    gfx_sp_vertex(1, 0, v);
    struct F3DLoadedVertex* d = &gF3dRsp.loaded_vertices[0];
    EXPECT_NEAR(d->_x, 10.0f, 0.0f);
    EXPECT_NEAR(d->_y, 20.0f, 0.0f);
    EXPECT_NEAR(d->_z, 30.0f, 0.0f);
    EXPECT_NEAR(d->_w, 1.0f, 0.0f);
    EXPECT_EQ_INT(d->color.r, 40);
    EXPECT_EQ_INT(d->color.g, 50);
    EXPECT_EQ_INT(d->color.b, 60);
    EXPECT_EQ_INT(d->color.a, 70);
    EXPECT_EQ_INT(d->wlt0, 0);
    /*
     * clip_rej bits are per-plane "inside" flags (1 = not beyond that
     * plane); a triangle is rejected when OR of its vertices != 0x3F.
     * (10,20,30,w=1) is beyond +x/+y/+z: bits z+,y+,x+ clear -> 0b010101.
     */
    EXPECT_EQ_INT(d->clip_rej, 0x15);

    /* translation via the matrix stack affects the transform */
    float translate[4][4];
    set_identity(translate);
    translate[3][0] = -10.0f;
    translate[3][1] = -20.0f;
    translate[3][2] = -30.0f;
    gfx_sp_matrix(G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH, (const float(*)[4]) translate);

    gfx_sp_vertex(1, 0, v);
    EXPECT_NEAR(d->_x, 0.0f, 0.0f);
    EXPECT_NEAR(d->_y, 0.0f, 0.0f);
    EXPECT_NEAR(d->_z, 0.0f, 0.0f);
    EXPECT_NEAR(d->_w, 1.0f, 0.0f);
    /* origin is inside the clip volume: all "inside" bits set */
    EXPECT_EQ_INT(d->clip_rej, 0x3F);
}

static void test_vertex_lighting(void) {
    Vtx v[1];

    reset_rsp();
    gF3dRsp.geometry_mode = G_LIGHTING;
    gF3dRsp.current_num_lights = 2;
    gF3dRsp.lights_changed = 1;

    /* Directional light pointing +X, colour (200, 100, 50); ambient (10, 20, 30). */
    gF3dRsp.current_lights[0].col[0] = 200;
    gF3dRsp.current_lights[0].col[1] = 100;
    gF3dRsp.current_lights[0].col[2] = 50;
    gF3dRsp.current_lights[0].dir[0] = 127;
    gF3dRsp.current_lights[1].col[0] = 10;
    gF3dRsp.current_lights[1].col[1] = 20;
    gF3dRsp.current_lights[1].col[2] = 30;

    memset(v, 0, sizeof(v));
    v[0].n.n[0] = 127; /* normal along +X: full intensity */
    v[0].n.a = 255;

    gfx_sp_vertex(1, 0, v);
    struct F3DLoadedVertex* d = &gF3dRsp.loaded_vertices[0];

    /* intensity = 1.0 -> ambient + light colour, clamped to 255 */
    EXPECT_EQ_INT(d->color.r, 210);
    EXPECT_EQ_INT(d->color.g, 120);
    EXPECT_EQ_INT(d->color.b, 80);

    /* Normal along -X: no contribution, ambient only. */
    v[0].n.n[0] = -127;
    gF3dRsp.lights_changed = 1;
    gfx_sp_vertex(1, 0, v);
    EXPECT_EQ_INT(d->color.r, 10);
    EXPECT_EQ_INT(d->color.g, 20);
    EXPECT_EQ_INT(d->color.b, 30);

    /* Saturation clamps at 255. */
    gF3dRsp.current_lights[1].col[0] = 250;
    v[0].n.n[0] = 127;
    gF3dRsp.lights_changed = 1;
    gfx_sp_vertex(1, 0, v);
    EXPECT_EQ_INT(d->color.r, 255);
}

int main(void) {
    test_reset_from_cold_state();
    test_matrix_mul();
    test_matrix_stack();
    test_matrix_reuse_after_reset();
    test_matrix_reuse_idempotent();
    test_vertex_transform();
    test_vertex_lighting();
    return test_finish("test_fast3d");
}
