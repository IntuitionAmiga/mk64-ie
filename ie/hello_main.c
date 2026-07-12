/*
 * Stage 3 runtime-skeleton smoke program.
 *
 * Proves the image layout and loader contract: entry runs, .data
 * arrives initialised, BSS is cleared, the stack works, the terminal
 * MMIO is reachable, and the program keeps executing. Results are
 * published in the boot status block that ie/smoke/hello_smoke.ies
 * polls.
 */

#include <stdint.h>

#include "ie_mmio.h"

void ie_term_puts(const char* s);

static uint32_t initialised_data = 0x12345678u;
static uint32_t zeroed_bss[4];

static uint32_t stack_roundtrip(uint32_t seed) {
    volatile uint32_t local[4];
    uint32_t i;

    for (i = 0; i < 4; i++) {
        local[i] = seed + i;
    }
    return local[0] + local[1] + local[2] + local[3];
}

void ie_main(void) {
    volatile uint32_t* status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;
    uint32_t checks = 0;
    uint32_t beat = 0;

    if (zeroed_bss[0] == 0 && zeroed_bss[1] == 0 && zeroed_bss[2] == 0 && zeroed_bss[3] == 0) {
        checks |= IE_BOOT_CHECK_BSS;
    }
    if (initialised_data == 0x12345678u) {
        checks |= IE_BOOT_CHECK_DATA;
    }
    if (stack_roundtrip(0x1000u) == 0x4006u) {
        checks |= IE_BOOT_CHECK_STACK;
    }

    ie_term_puts("MK64-IE68 STAGE3 HELLO\n");

    status[1] = checks;
    status[2] = 0;
    status[0] = IE_BOOT_MAGIC; /* magic last: checks are valid once seen */

    for (;;) {
        beat++;
        status[2] = beat;
    }
}
