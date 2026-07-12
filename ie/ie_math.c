/*
 * Freestanding math routines for the bare-metal IE build (no libm for
 * this target). With -m68881 the hot ones map straight onto FPU
 * instructions - the engine implements the 68881 transcendentals
 * (FSQRT/FSIN/FCOS/FACOS/FATAN) and the JIT compiles them to SSE.
 * The polynomial fallbacks remain below under IE_SOFT_FLOAT for any
 * future soft-float build.
 */

#include <math.h>

#ifdef __HAVE_68881__

float sqrtf(float x) {
    float r;

    __asm__("fsqrt.x %1, %0" : "=f"(r) : "f"(x));
    return r;
}

float sinf(float x) {
    float r;

    __asm__("fsin.x %1, %0" : "=f"(r) : "f"(x));
    return r;
}

float cosf(float x) {
    float r;

    __asm__("fcos.x %1, %0" : "=f"(r) : "f"(x));
    return r;
}

float acosf(float x) {
    float r;

    __asm__("facos.x %1, %0" : "=f"(r) : "f"(x));
    return r;
}

float atan2f(float y, float x) {
    /* 68881 has no fatan2; quadrant fixup around fatan. */
    float r;
    float ax = x < 0.0f ? -x : x;

    if (x == 0.0f && y == 0.0f) {
        return 0.0f;
    }
    if (ax > 0.0f) {
        float q = y / x;

        __asm__("fatan.x %1, %0" : "=f"(r) : "f"(q));
        if (x < 0.0f) {
            r += y >= 0.0f ? 3.14159265f : -3.14159265f;
        }
        return r;
    }
    return y > 0.0f ? 1.57079633f : -1.57079633f;
}

#else /* soft-float fallbacks */

float sqrtf(float x) {
    float guess;
    int i;

    if (x <= 0.0f) {
        return 0.0f;
    }
    guess = x > 1.0f ? x * 0.5f : 1.0f;
    for (i = 0; i < 20; i++) {
        guess = 0.5f * (guess + x / guess);
    }
    return guess;
}

float acosf(float x) {
    /* acos(x) = sqrt(1-x) * (a0 + a1 x + a2 x^2 + a3 x^3), 0 <= x <= 1 */
    static const float pi = 3.14159265f;
    float negate = 0.0f;
    float result;

    if (x < -1.0f) {
        x = -1.0f;
    }
    if (x > 1.0f) {
        x = 1.0f;
    }
    if (x < 0.0f) {
        negate = 1.0f;
        x = -x;
    }
    result = ((-0.0187293f * x + 0.0742610f) * x - 0.2121144f) * x + 1.5707288f;
    result *= sqrtf(1.0f - x);
    return negate * pi + (1.0f - 2.0f * negate) * result;
}

static float sin_core(float x) {
    /* Taylor series around 0; |x| <= pi/2 after reduction. */
    float x2 = x * x;

    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f))));
}

float sinf(float x) {
    static const float pi = 3.14159265f;
    static const float two_pi = 6.28318531f;
    float sign = 1.0f;

    while (x > pi) {
        x -= two_pi;
    }
    while (x < -pi) {
        x += two_pi;
    }
    if (x > pi * 0.5f) {
        x = pi - x;
    } else if (x < -pi * 0.5f) {
        x = -pi - x;
    }
    return sign * sin_core(x);
}

float cosf(float x) {
    static const float half_pi = 1.57079633f;

    return sinf(half_pi - x);
}

/* atan approximation on |z| <= 1 (max error ~1e-4 rad), extended to
 * the full plane with the usual quadrant identities. */
#define IE_PI 3.14159265f
#define IE_PI_2 1.57079633f

static float atanf_unit(float z) {
    float z2 = z * z;

    return z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
}

float atan2f(float y, float x) {
    float ax = x < 0.0f ? -x : x;
    float ay = y < 0.0f ? -y : y;
    float r;

    if (ax == 0.0f && ay == 0.0f) {
        return 0.0f;
    }
    if (ax >= ay) {
        r = atanf_unit(ay / ax);
    } else {
        r = IE_PI_2 - atanf_unit(ax / ay);
    }
    if (x < 0.0f) {
        r = IE_PI - r;
    }
    return y < 0.0f ? -r : r;
}

#endif /* __HAVE_68881__ */
