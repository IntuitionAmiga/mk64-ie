/*
 * Minimal freestanding runtime for bare Intuition Engine M68K images.
 *
 * Stage 3 runtime skeleton: BSS clear and raw terminal output only.
 * Higher-level services (platform.h backends, libc surface) arrive one
 * subsystem at a time.
 *
 * The BSS clear uses discrete volatile stores instead of memset or a
 * tight assembly loop: the engine's M68K JIT miscompiles chained RAM
 * write loops (stores silently dropped), a hazard documented and
 * worked around the same way on the ie68-port salvage branch
 * (commit 99507c62, ie/runtime.c).
 */

#include <stdint.h>

#include "ie_mmio.h"

/* Linker-script symbols. The explicit asm names bypass the a.out
 * leading-underscore convention, so these bind to the script's
 * definitions under both a.out and ELF toolchains. */
extern uint8_t ie_bss_start[] __asm__("__bss_start");
extern uint8_t ie_bss_end[] __asm__("__bss_end");

void ie_clear_bss(void) {
    volatile uint32_t* p = (volatile uint32_t*) ie_bss_start;
    volatile uint32_t* end = (volatile uint32_t*) ie_bss_end;

    while (p < end) {
        *p++ = 0;
    }
}

void ie_term_putc(char c) {
    /* Honour the output-ready handshake so characters are not lost
     * when the CPU halts right after printing (platform_fatal). */
    while ((ie_mmio_read32(IE_TERM_STATUS) & IE_TERM_STATUS_OUT_READY) == 0) {
    }
    ie_mmio_write32(IE_TERM_OUT, (uint8_t) c);
}

void ie_term_puts(const char* s) {
    while (*s != '\0') {
        ie_term_putc(*s++);
    }
}
