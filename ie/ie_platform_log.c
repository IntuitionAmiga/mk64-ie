/*
 * IE backend for the platform logging contract (platform_log,
 * platform_fatal from src/platform/platform.h): formatted text goes to
 * the engine terminal, and platform_fatal additionally publishes the
 * 'DEAD' marker in the boot status block so IEScript smokes observe
 * fatal exits deterministically, then halts the CPU.
 */

#include <stdarg.h>

#include "platform/platform.h"

#include "ie_fmt.h"
#include "ie_mmio.h"

void ie_term_putc(char c);
void ie_term_puts(const char* s);

#define IE_LOG_BUF_SIZE 256

static void log_line(const char* prefix, const char* fmt, va_list ap) {
    char buf[IE_LOG_BUF_SIZE];

    ie_vformat(buf, sizeof(buf), fmt, ap);
    if (prefix != NULL) {
        ie_term_puts(prefix);
    }
    ie_term_puts(buf);
    ie_term_putc('\n');
}

void platform_log(const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    log_line(NULL, fmt, ap);
    va_end(ap);
}

void platform_fatal(const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    log_line("FATAL: ", fmt, ap);
    va_end(ap);

    ie_mmio_write32(IE_BOOT_FATAL_ADDR, IE_BOOT_FATAL_MAGIC);

    for (;;) {
        __asm__ volatile("stop #0x2700");
    }
}
