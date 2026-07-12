/* Freestanding assert shim: failures abort via platform_fatal. */
#ifndef IE_ASSERT_H
#define IE_ASSERT_H

void platform_fatal(const char* fmt, ...) __attribute__((noreturn));

#ifdef NDEBUG
#define assert(expr) ((void) 0)
#else
#define assert(expr) \
    ((expr) ? (void) 0 : platform_fatal("assert failed: %s (%s:%d)", #expr, __FILE__, __LINE__))
#endif

#endif /* IE_ASSERT_H */
