#include "libultra_internal.h"
#include <math.h>

void guNormalize(f32* x, f32* y, f32* z) {
    f32 magSqr = *x * *x + *y * *y + *z * *z;
    f32 invLen;

    if (magSqr == 0.0f) {
        return;
    }

    invLen = 1.0f / sqrtf(magSqr);
    *x *= invLen;
    *y *= invLen;
    *z *= invLen;
}
