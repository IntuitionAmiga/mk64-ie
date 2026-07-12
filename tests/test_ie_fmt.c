/*
 * Host tests for the freestanding formatter backing the IE backend's
 * platform_log/platform_fatal (ie/ie_fmt.c). The formatter is pure
 * portable C so its behaviour is locked here on the host; the on-target
 * smoke re-checks a golden length/byte-sum pair for the same inputs.
 */
#include "test_support.h"

#include <stdarg.h>
#include <stdint.h>

#include "../ie/ie_fmt.h"

static int fmt(char* buf, size_t cap, const char* f, ...) {
    va_list ap;
    int n;

    va_start(ap, f);
    n = ie_vformat(buf, cap, f, ap);
    va_end(ap);
    return n;
}

static void test_plain_and_percent(void) {
    char buf[64];

    EXPECT_EQ_INT(fmt(buf, sizeof(buf), "hello"), 5);
    EXPECT_TRUE(strcmp(buf, "hello") == 0);
    EXPECT_EQ_INT(fmt(buf, sizeof(buf), "100%%"), 4);
    EXPECT_TRUE(strcmp(buf, "100%") == 0);
}

static void test_strings_and_chars(void) {
    char buf[64];

    fmt(buf, sizeof(buf), "[%s|%c]", "abc", 'Z');
    EXPECT_TRUE(strcmp(buf, "[abc|Z]") == 0);
    fmt(buf, sizeof(buf), "%s", (char*) 0);
    EXPECT_TRUE(strcmp(buf, "(null)") == 0);
}

static void test_decimal(void) {
    char buf[64];

    fmt(buf, sizeof(buf), "%d %d %d", 0, -1, 2147483647);
    EXPECT_TRUE(strcmp(buf, "0 -1 2147483647") == 0);
    fmt(buf, sizeof(buf), "%u", 4294967295u);
    EXPECT_TRUE(strcmp(buf, "4294967295") == 0);
    fmt(buf, sizeof(buf), "%d", -2147483647 - 1);
    EXPECT_TRUE(strcmp(buf, "-2147483648") == 0);
}

static void test_hex(void) {
    char buf[64];

    fmt(buf, sizeof(buf), "%x %X", 0xdeadbeefu, 0xCAFEu);
    EXPECT_TRUE(strcmp(buf, "deadbeef CAFE") == 0);
    fmt(buf, sizeof(buf), "%08x", 0x1234u);
    EXPECT_TRUE(strcmp(buf, "00001234") == 0);
    fmt(buf, sizeof(buf), "%04d", 42);
    EXPECT_TRUE(strcmp(buf, "0042") == 0);
    fmt(buf, sizeof(buf), "%6d", 42);
    EXPECT_TRUE(strcmp(buf, "    42") == 0);
}

static void test_long_and_pointer(void) {
    char buf[64];

    /* %l values stay below 2^31 so the expectation is identical on
     * 32-bit targets and 64-bit hosts. */
    fmt(buf, sizeof(buf), "%ld %lu %lx", (long) -123, (unsigned long) 456, (unsigned long) 0xABCu);
    EXPECT_TRUE(strcmp(buf, "-123 456 abc") == 0);
    fmt(buf, sizeof(buf), "%p", (void*) 0x1000);
    EXPECT_TRUE(strcmp(buf, "0x1000") == 0);
}

static void test_truncation(void) {
    char buf[8];

    /* Truncated output is still NUL-terminated; the return value is
     * the number of characters actually written. */
    EXPECT_EQ_INT(fmt(buf, sizeof(buf), "0123456789"), 7);
    EXPECT_TRUE(strcmp(buf, "0123456") == 0);
    EXPECT_EQ_INT(fmt(buf, 1, "abc"), 0);
    EXPECT_TRUE(buf[0] == '\0');
}

static void test_unknown_specifier_passthrough(void) {
    char buf[16];

    fmt(buf, sizeof(buf), "%q");
    EXPECT_TRUE(strcmp(buf, "%q") == 0);
}

/* Golden pair shared with the on-target smoke (ie/log_main.c formats
 * the same string and publishes length and byte sum). */
static void test_target_golden(void) {
    char buf[128];
    unsigned sum = 0;
    int n = fmt(buf, sizeof(buf), "log %s %d 0x%08x %c%%", "ok", -42, 0xBEEFu, '!');
    int i;

    EXPECT_TRUE(strcmp(buf, "log ok -42 0x0000beef !%") == 0);
    EXPECT_EQ_INT(n, 24);
    for (i = 0; i < n; i++) {
        sum += (unsigned char) buf[i];
    }
    EXPECT_EQ_INT(sum, 1647);
}

int main(void) {
    test_plain_and_percent();
    test_strings_and_chars();
    test_decimal();
    test_hex();
    test_long_and_pointer();
    test_truncation();
    test_unknown_specifier_passthrough();
    test_target_golden();
    return test_finish("test_ie_fmt");
}
