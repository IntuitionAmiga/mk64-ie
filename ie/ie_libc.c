/*
 * Minimal libc surface for the bare-metal IE build. The compiler can
 * emit calls to these for struct copies/compares even under
 * -fno-builtin, and the shared translator uses them directly.
 */

#include <stddef.h>
#include <stdint.h>

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*) dst;
    const uint8_t* s = (const uint8_t*) src;

    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

void* memset(void* dst, int value, size_t n) {
    uint8_t* d = (uint8_t*) dst;

    while (n-- > 0) {
        *d++ = (uint8_t) value;
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* pa = (const uint8_t*) a;
    const uint8_t* pb = (const uint8_t*) b;

    while (n-- > 0) {
        if (*pa != *pb) {
            return *pa < *pb ? -1 : 1;
        }
        pa++;
        pb++;
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;

    while (s[n] != '\0') {
        n++;
    }
    return n;
}

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = dst;
    const unsigned char* s = src;

    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char) *a - (unsigned char) *b;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;

    while ((*d++ = *src++) != '\0') {
    }
    return dst;
}
