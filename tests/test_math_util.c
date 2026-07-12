/*
 * Golden tests for the portable racing/math_util.c helpers that replaced
 * the SH4 assembly implementations.
 */
#include <ultra64.h>
#include <mk64.h>
#include <math_util.h>
#include <math.h>

#include "test_support.h"

#define U16_TO_RAD (2.0 * M_PI / 65536.0)

static void test_sins_coss(void) {
    /* sins/coss are N64-faithful table lookups: the angle quantizes
     * to >>4 resolution (gSineTable has 0x1000 entries per turn), so
     * the golden value is the sine of the QUANTIZED angle. */
    static const u16 angles[] = { 0, 0x2000, 0x4000, 0x6000, 0x8000, 0xC000, 0xFFFF };
    for (size_t i = 0; i < sizeof(angles) / sizeof(angles[0]); i++) {
        u16 a = angles[i];
        u16 q = a & 0xFFF0;
        EXPECT_NEAR(sins(a), sin((double) q * U16_TO_RAD), 1e-5);
        EXPECT_NEAR(coss(a), cos((double) q * U16_TO_RAD), 1e-5);
    }
    EXPECT_NEAR(sins(0x4000), 1.0, 1e-6);
    EXPECT_NEAR(coss(0x8000), -1.0, 1e-6);
}

static void test_mtxf_identity_translate(void) {
    Mat4 m;
    Vec3f t = { 5.0f, 6.0f, 7.0f };

    mtxf_identity(m);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            EXPECT_NEAR(m[r][c], (r == c) ? 1.0f : 0.0f, 0.0f);
        }
    }

    mtxf_translate(m, t);
    EXPECT_NEAR(m[3][0], 5.0f, 0.0f);
    EXPECT_NEAR(m[3][1], 6.0f, 0.0f);
    EXPECT_NEAR(m[3][2], 7.0f, 0.0f);
    EXPECT_NEAR(m[0][0], 1.0f, 0.0f);
}

static void test_mtxf_rotations(void) {
    Mat4 m;
    /* 0x4000 = quarter turn */
    mtxf_rotate_x(m, 0x4000);
    EXPECT_NEAR(m[1][1], 0.0f, 1e-6);
    EXPECT_NEAR(m[1][2], 1.0f, 1e-6);
    EXPECT_NEAR(m[2][1], -1.0f, 1e-6);
    EXPECT_NEAR(m[2][2], 0.0f, 1e-6);
    EXPECT_NEAR(m[0][0], 1.0f, 0.0f);

    mtxf_rotate_y(m, 0x4000);
    EXPECT_NEAR(m[0][0], 0.0f, 1e-6);
    EXPECT_NEAR(m[0][2], -1.0f, 1e-6);
    EXPECT_NEAR(m[2][0], 1.0f, 1e-6);
    EXPECT_NEAR(m[2][2], 0.0f, 1e-6);

    mtxf_s16_rotate_z(m, 0x4000);
    EXPECT_NEAR(m[0][0], 0.0f, 1e-6);
    EXPECT_NEAR(m[0][1], 1.0f, 1e-6);
    EXPECT_NEAR(m[1][0], -1.0f, 1e-6);
    EXPECT_NEAR(m[1][1], 0.0f, 1e-6);
}

static void test_mtxf_multiplication(void) {
    Mat4 a, b, res;
    Vec3f ta = { 1.0f, 2.0f, 3.0f };

    mtxf_translate(a, ta);
    mtxf_identity(b);
    b[0][0] = 2.0f;
    b[1][1] = 2.0f;
    b[2][2] = 2.0f;

    mtxf_multiplication(res, a, b);
    EXPECT_NEAR(res[0][0], 2.0f, 1e-6);
    EXPECT_NEAR(res[3][0], 2.0f, 1e-6);
    EXPECT_NEAR(res[3][1], 4.0f, 1e-6);
    EXPECT_NEAR(res[3][2], 6.0f, 1e-6);
    EXPECT_NEAR(res[3][3], 1.0f, 1e-6);

    /* Aliased destination must still be correct. */
    mtxf_multiplication(a, a, b);
    EXPECT_NEAR(a[3][0], 2.0f, 1e-6);
    EXPECT_NEAR(a[3][1], 4.0f, 1e-6);
    EXPECT_NEAR(a[3][2], 6.0f, 1e-6);
}

static void test_mtxf_pos_rotation_xyz(void) {
    Mat4 m;
    Vec3f pos = { 9.0f, 8.0f, 7.0f };
    Vec3s rot = { 0, 0, 0 };

    mtxf_pos_rotation_xyz(m, pos, rot);
    EXPECT_NEAR(m[0][0], 1.0f, 1e-6);
    EXPECT_NEAR(m[1][1], 1.0f, 1e-6);
    EXPECT_NEAR(m[2][2], 1.0f, 1e-6);
    EXPECT_NEAR(m[3][0], 9.0f, 0.0f);
    EXPECT_NEAR(m[3][1], 8.0f, 0.0f);
    EXPECT_NEAR(m[3][2], 7.0f, 0.0f);

    /* Quarter turn about Y only. */
    rot[1] = 0x4000;
    mtxf_pos_rotation_xyz(m, pos, rot);
    EXPECT_NEAR(m[0][0], 0.0f, 1e-6);
    EXPECT_NEAR(m[2][0], 1.0f, 1e-6);
    EXPECT_NEAR(m[0][2], -1.0f, 1e-6);
    EXPECT_NEAR(m[2][2], 0.0f, 1e-6);
    EXPECT_NEAR(m[1][1], 1.0f, 1e-6);
}

static void test_mtxf_translate_vec3f(void) {
    Mat4 m;
    Vec3s rot = { 0, 0x4000, 0 };
    Vec3f pos = { 0.0f, 0.0f, 0.0f };
    Vec3f v = { 1.0f, 0.0f, 0.0f };

    mtxf_pos_rotation_xyz(m, pos, rot);
    /*
     * mtxf_translate_vec3f_mat4 applies the matrix rows to the vector,
     * i.e. the inverse of the rotation encoded in the row-vector matrix:
     * +X maps to +Z for a quarter turn about Y.
     */
    mtxf_translate_vec3f_mat4(v, m);
    EXPECT_NEAR(v[0], 0.0f, 1e-6);
    EXPECT_NEAR(v[1], 0.0f, 1e-6);
    EXPECT_NEAR(v[2], 1.0f, 1e-6);
}

static void test_atan2s(void) {
    EXPECT_EQ_INT(atan2s(0.0f, 100.0f), 0x0000);
    EXPECT_EQ_INT(atan2s(100.0f, 0.0f), 0x4000);
    EXPECT_EQ_INT(atan2s(0.0f, -100.0f), 0x8000);
    EXPECT_EQ_INT((u16) atan2s(-100.0f, 0.0f), 0xC000);
    EXPECT_EQ_INT(atan2s(100.0f, 100.0f), 0x2000);
}

static void test_mtxf_lookat(void) {
    /* func_802B5794: skybox look-at, from origin toward -Z. */
    Mat4 m;
    Vec3f from = { 0.0f, 0.0f, 10.0f };
    Vec3f to = { 0.0f, 0.0f, 0.0f };

    func_802B5794(m, from, to);
    EXPECT_NEAR(m[0][0], 1.0f, 1e-5);
    EXPECT_NEAR(m[1][1], 1.0f, 1e-5);
    EXPECT_NEAR(m[2][2], 1.0f, 1e-5);
    EXPECT_NEAR(m[3][2], -10.0f, 1e-5);
    EXPECT_NEAR(m[3][0], 0.0f, 1e-5);
    EXPECT_NEAR(m[3][3], 1.0f, 0.0f);
}

int main(void) {
    test_sins_coss();
    test_mtxf_identity_translate();
    test_mtxf_rotations();
    test_mtxf_multiplication();
    test_mtxf_pos_rotation_xyz();
    test_mtxf_translate_vec3f();
    test_atan2s();
    test_mtxf_lookat();
    return test_finish("test_math_util");
}
