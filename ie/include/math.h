#ifndef IE_SHIM_MATH_H
#define IE_SHIM_MATH_H

/*
 * Freestanding <math.h> for the bare-metal Intuition Engine build.
 * Only the functions the shared code actually uses are provided;
 * implementations live in ie/ie_math.c (soft-float).
 */

#define M_PI 3.14159265358979323846

float sqrtf(float x);
float atan2f(float y, float x);
static inline float fabsf(float x) {
    return x < 0.0f ? -x : x;
}
float acosf(float x);
float sinf(float x);
float cosf(float x);

#endif /* IE_SHIM_MATH_H */
