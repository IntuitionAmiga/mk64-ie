#include "ie_fmt.h"

#include <stdint.h>

struct out_state {
    char* buf;
    size_t cap;
    size_t len;
};

static void out_char(struct out_state* o, char c) {
    if (o->len + 1 < o->cap) {
        o->buf[o->len++] = c;
    }
}

static void out_padded(struct out_state* o, const char* digits, size_t ndigits,
                       int negative, unsigned width, char pad) {
    size_t total = ndigits + (negative ? 1 : 0);
    size_t i;

    if (negative && pad == '0') {
        out_char(o, '-');
    }
    for (i = total; i < width; i++) {
        out_char(o, pad);
    }
    if (negative && pad != '0') {
        out_char(o, '-');
    }
    while (ndigits-- > 0) {
        out_char(o, *digits++);
    }
}

static void out_unsigned(struct out_state* o, unsigned long value, unsigned base,
                         int uppercase, int negative, unsigned width, char pad) {
    const char* alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[16];
    size_t n = 0;

    do {
        tmp[n++] = alphabet[value % base];
        value /= base;
    } while (value != 0 && n < sizeof(tmp));

    /* Digits were produced least-significant first. */
    {
        char digits[16];
        size_t i;
        for (i = 0; i < n; i++) {
            digits[i] = tmp[n - 1 - i];
        }
        out_padded(o, digits, n, negative, width, pad);
    }
}

int ie_vformat(char* buf, size_t cap, const char* fmt, va_list ap) {
    struct out_state o;

    o.buf = buf;
    o.cap = cap;
    o.len = 0;

    while (*fmt != '\0') {
        char pad = ' ';
        unsigned width = 0;
        int is_long = 0;
        const char* spec_start = fmt;

        if (*fmt != '%') {
            out_char(&o, *fmt++);
            continue;
        }
        fmt++;

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (unsigned)(*fmt - '0');
            fmt++;
        }
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        switch (*fmt) {
            case '%':
                out_char(&o, '%');
                break;
            case 'c':
                out_char(&o, (char) va_arg(ap, int));
                break;
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (s == NULL) {
                    s = "(null)";
                }
                while (*s != '\0') {
                    out_char(&o, *s++);
                }
                break;
            }
            case 'd':
            case 'i': {
                long v = is_long ? va_arg(ap, long) : va_arg(ap, int);
                unsigned long mag = v < 0 ? (unsigned long) - (v + 1) + 1 : (unsigned long) v;
                out_unsigned(&o, mag, 10, 0, v < 0, width, pad);
                break;
            }
            case 'u': {
                unsigned long v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                out_unsigned(&o, v, 10, 0, 0, width, pad);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long v = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                out_unsigned(&o, v, 16, *fmt == 'X', 0, width, pad);
                break;
            }
            case 'p': {
                void* p = va_arg(ap, void*);
                out_char(&o, '0');
                out_char(&o, 'x');
                out_unsigned(&o, (unsigned long) (uintptr_t) p, 16, 0, 0, 0, ' ');
                break;
            }
            case '\0':
                /* Trailing '%': emit it and stop. */
                out_char(&o, '%');
                if (cap > 0) {
                    buf[o.len < cap ? o.len : cap - 1] = '\0';
                }
                return (int) o.len;
            default:
                /* Unknown specifier: pass the sequence through. */
                while (spec_start <= fmt) {
                    out_char(&o, *spec_start++);
                }
                break;
        }
        fmt++;
    }

    if (cap > 0) {
        buf[o.len] = '\0';
    }
    return (int) o.len;
}

int ie_format(char* buf, size_t cap, const char* fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = ie_vformat(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

/* sprintf for game code (see ie/include/stdio.h); same conversions as
 * ie_vformat, unbounded destination like the real thing. Game call
 * sites format short asset names and debug text only. Freestanding
 * m68k build only: host builds link the real libc. */
#ifdef __mc68000__
int sprintf(char* dst, const char* fmt, ...) {
    va_list args;
    int n;

    va_start(args, fmt);
    n = ie_vformat(dst, (size_t) -1, fmt, args);
    va_end(args);
    return n;
}

void ie_term_puts(const char* s);

int printf(const char* fmt, ...) {
    static char buf[256];
    va_list args;
    int n;

    va_start(args, fmt);
    n = ie_vformat(buf, sizeof(buf), fmt, args);
    va_end(args);
    ie_term_puts(buf);
    return n;
}
#endif /* __mc68000__ */
