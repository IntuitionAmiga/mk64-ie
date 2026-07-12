/*
 * Golden tests for the portable gu* matrix/vector helpers that replaced the
 * SH4 assembly implementations.
 */
#include <ultra64.h>
#include <math.h>

#include "test_support.h"

void guMtxIdentF(float mf[4][4]);
void guTranslateF(float m[4][4], float x, float y, float z);
void guScaleF(float mf[4][4], float x, float y, float z);
void guRotateF(float m[4][4], float a, float x, float y, float z);
void guMtxCatF(float mf[4][4], float nf[4][4], float res[4][4]);
void guMtxF2L(float mf[4][4], Mtx* m);
void guLookAtF(float mf[4][4], float xEye, float yEye, float zEye, float xAt, float yAt, float zAt, float xUp,
               float yUp, float zUp);
void guPerspectiveF(float mf[4][4], u16* perspNorm, float fovy, float aspect, float near, float far, float scale);
void guNormalize(f32* x, f32* y, f32* z);
void guMtxXFMF(float mf[4][4], float x, float y, float z, float* ox, float* oy, float* oz);

static const float kIdentity[4][4] = {
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, 0, 1 },
};

static void test_ident_translate_scale(void) {
    float m[4][4];

    guMtxIdentF(m);
    EXPECT_MAT4_NEAR(m, kIdentity, 0.0f);

    guTranslateF(m, 3.0f, -4.0f, 5.0f);
    EXPECT_NEAR(m[3][0], 3.0f, 0.0f);
    EXPECT_NEAR(m[3][1], -4.0f, 0.0f);
    EXPECT_NEAR(m[3][2], 5.0f, 0.0f);
    EXPECT_NEAR(m[0][0], 1.0f, 0.0f);
    EXPECT_NEAR(m[1][2], 0.0f, 0.0f);
    EXPECT_NEAR(m[3][3], 1.0f, 0.0f);

    guScaleF(m, 2.0f, 3.0f, 4.0f);
    EXPECT_NEAR(m[0][0], 2.0f, 0.0f);
    EXPECT_NEAR(m[1][1], 3.0f, 0.0f);
    EXPECT_NEAR(m[2][2], 4.0f, 0.0f);
    EXPECT_NEAR(m[3][0], 0.0f, 0.0f);
    EXPECT_NEAR(m[3][3], 1.0f, 0.0f);
}

static void test_mtx_cat(void) {
    /* A = translate(1,2,3), B = scale(2,2,2): A*B scales the translation. */
    float a[4][4], b[4][4], res[4][4];

    guTranslateF(a, 1.0f, 2.0f, 3.0f);
    guScaleF(b, 2.0f, 2.0f, 2.0f);
    guMtxCatF(a, b, res);

    static const float expected[4][4] = {
        { 2, 0, 0, 0 },
        { 0, 2, 0, 0 },
        { 0, 0, 2, 0 },
        { 2, 4, 6, 1 },
    };
    EXPECT_MAT4_NEAR(res, expected, 1e-6);

    /* Aliased output must behave as if computed with a temporary. */
    guMtxCatF(a, b, a);
    EXPECT_MAT4_NEAR(a, expected, 1e-6);
}

static void test_mtx_xfmf(void) {
    float m[4][4];
    float ox, oy, oz;

    guTranslateF(m, 10.0f, 20.0f, 30.0f);
    guMtxXFMF(m, 1.0f, 2.0f, 3.0f, &ox, &oy, &oz);
    EXPECT_NEAR(ox, 11.0f, 1e-6);
    EXPECT_NEAR(oy, 22.0f, 1e-6);
    EXPECT_NEAR(oz, 33.0f, 1e-6);
}

static void test_rotate(void) {
    /* 90 degrees about Z: X axis maps to Y. */
    float m[4][4];
    float ox, oy, oz;

    guRotateF(m, 90.0f, 0.0f, 0.0f, 1.0f);
    guMtxXFMF(m, 1.0f, 0.0f, 0.0f, &ox, &oy, &oz);
    EXPECT_NEAR(ox, 0.0f, 1e-6);
    EXPECT_NEAR(oy, 1.0f, 1e-6);
    EXPECT_NEAR(oz, 0.0f, 1e-6);
}

static void test_normalize(void) {
    f32 x = 3.0f, y = 0.0f, z = 4.0f;
    guNormalize(&x, &y, &z);
    EXPECT_NEAR(x, 0.6f, 1e-6);
    EXPECT_NEAR(y, 0.0f, 1e-6);
    EXPECT_NEAR(z, 0.8f, 1e-6);

    /* Zero-length input must not produce NaN/Inf. */
    x = y = z = 0.0f;
    guNormalize(&x, &y, &z);
    EXPECT_NEAR(x, 0.0f, 0.0f);
    EXPECT_NEAR(y, 0.0f, 0.0f);
    EXPECT_NEAR(z, 0.0f, 0.0f);
}

static void test_lookat(void) {
    /*
     * Camera at (0,0,10) looking at origin, up = +Y. Standard axes:
     * right = +X, up = +Y, look = -Z (negated to +Z row storage per the
     * libultra convention), eye translation is -10 along look.
     */
    float m[4][4];

    guLookAtF(m, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    static const float expected[4][4] = {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, -10, 1 },
    };
    EXPECT_MAT4_NEAR(m, expected, 1e-5);

    /* Rotated case: camera on +X axis looking at origin. look = +X. */
    guLookAtF(m, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    /* right = up x look = (0,1,0) x (1,0,0) = (0,0,-1) -> stored column 0 */
    EXPECT_NEAR(m[0][0], 0.0f, 1e-5);
    EXPECT_NEAR(m[1][0], 0.0f, 1e-5);
    EXPECT_NEAR(m[2][0], -1.0f, 1e-5);
    /* look = (eye-at)/|..| = (1,0,0) stored column 2 */
    EXPECT_NEAR(m[0][2], 1.0f, 1e-5);
    EXPECT_NEAR(m[1][2], 0.0f, 1e-5);
    EXPECT_NEAR(m[2][2], 0.0f, 1e-5);
    /* translation = -(eye . axis) */
    EXPECT_NEAR(m[3][0], 0.0f, 1e-5);
    EXPECT_NEAR(m[3][1], 0.0f, 1e-5);
    EXPECT_NEAR(m[3][2], -10.0f, 1e-5);
}

static void test_mtx_f2l_roundtrip(void) {
    float m[4][4];
    Mtx fixed;

    guTranslateF(m, 1.5f, -2.25f, 3.75f);
    guMtxF2L(m, &fixed);
    EXPECT_NEAR(fixed.m[3][0], 1.5f, 0.0f);
    EXPECT_NEAR(fixed.m[3][1], -2.25f, 0.0f);
    EXPECT_NEAR(fixed.m[3][2], 3.75f, 0.0f);
    EXPECT_NEAR(fixed.m[0][0], 1.0f, 0.0f);
}

static void test_perspective(void) {
    float m[4][4];
    u16 perspNorm = 0;

    guPerspectiveF(m, &perspNorm, 60.0f, 4.0f / 3.0f, 1.0f, 100.0f, 1.0f);

    float halfFov = 60.0f * ((float) M_PI / 180.0f) / 2.0f;
    float yscale = cosf(halfFov) / sinf(halfFov);
    EXPECT_NEAR(m[0][0], yscale / (4.0f / 3.0f), 1e-4);
    EXPECT_NEAR(m[1][1], yscale, 1e-4);
    EXPECT_NEAR(m[2][2], (1.0f + 100.0f) / (1.0f - 100.0f), 1e-5);
    EXPECT_NEAR(m[2][3], -1.0f, 0.0f);
    EXPECT_NEAR(m[3][2], 2.0f * 1.0f * 100.0f / (1.0f - 100.0f), 1e-4);
    EXPECT_EQ_INT(perspNorm, (u16) ((double) (1 << 17) / 101.0));
}

int main(void) {
    test_ident_translate_scale();
    test_mtx_cat();
    test_mtx_xfmf();
    test_rotate();
    test_normalize();
    test_lookat();
    test_mtx_f2l_roundtrip();
    test_perspective();
    return test_finish("test_gu_math");
}
