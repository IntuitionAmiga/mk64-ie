#ifndef IE_SHIM_STDINT_H
#define IE_SHIM_STDINT_H

/*
 * Freestanding <stdint.h> for the bare-metal Intuition Engine build.
 * The mint-targeted GCC ships no libc headers; the types come from the
 * compiler's own predefined macros, so this stays correct for any
 * m68k GCC.
 */

typedef __INT8_TYPE__ int8_t;
typedef __INT16_TYPE__ int16_t;
typedef __INT32_TYPE__ int32_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINT8_TYPE__ uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT64_TYPE__ uint64_t;

/* Spelled as plain (unsigned) int rather than __INTPTR_TYPE__: the
 * mint toolchain's builtin type is long, and the game's libultra
 * headers declare pointer-sized values as u32/s32 (= [unsigned] int).
 * Same 32-bit width on this target, but GCC 15 rejects the redeclared
 * prototypes unless the underlying types match exactly. */
typedef int intptr_t;
typedef unsigned int uintptr_t;
typedef __INTMAX_TYPE__ intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;

#ifndef __INT64_C
#define __INT64_C(c) c##LL
#endif
#ifndef __UINT64_C
#define __UINT64_C(c) c##ULL
#endif

#define INT8_MIN (-128)
#define INT16_MIN (-32768)
#define INT32_MIN (-2147483647 - 1)
#define INT64_MIN (-__INT64_C(9223372036854775807) - 1)
#define INT8_MAX 127
#define INT16_MAX 32767
#define INT32_MAX 2147483647
#define INT64_MAX __INT64_C(9223372036854775807)
#define UINT8_MAX 255
#define UINT16_MAX 65535
#define UINT32_MAX 4294967295U
#define UINT64_MAX __UINT64_C(18446744073709551615)

#define INTPTR_MIN INT32_MIN
#define INTPTR_MAX INT32_MAX
#define UINTPTR_MAX UINT32_MAX

#endif /* IE_SHIM_STDINT_H */
