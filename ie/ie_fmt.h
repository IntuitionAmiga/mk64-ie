#ifndef IE_FMT_H
#define IE_FMT_H

/*
 * Freestanding printf-style formatter for the IE backend's logging
 * path. Pure portable C (no libc), also compiled into the host test
 * suite (tests/test_ie_fmt.c).
 *
 * Supported: %c %s %d %i %u %x %X %p %%, the '0' flag and a decimal
 * field width, and the 'l' length modifier. No floating point, no
 * 64-bit integers (avoids libgcc divide helpers on the target).
 * Output is always NUL-terminated when cap > 0; the return value is
 * the number of characters written, excluding the terminator.
 */

#include <stdarg.h>
#include <stddef.h>

int ie_vformat(char* buf, size_t cap, const char* fmt, va_list ap);
int ie_format(char* buf, size_t cap, const char* fmt, ...);

#endif /* IE_FMT_H */
