/* Freestanding stdio shim for the IE m68k build: sprintf maps onto the
 * ie_fmt formatter (see ie/ie_fmt.c for the supported conversions). */
#ifndef IE_STDIO_H
#define IE_STDIO_H

#include <stddef.h>

int sprintf(char* dst, const char* fmt, ...);

/* Debug prints in game code route to the IE terminal (ie_fmt
 * conversions only; no floats). */
int printf(const char* fmt, ...);

/* Opaque FILE so host-tooling prototypes (e.g. src/compression/utils.h)
 * compile; no stream implementation exists - any definition that
 * actually uses streams must stay behind host-only guards. */
typedef struct ie_file FILE;

#endif /* IE_STDIO_H */
