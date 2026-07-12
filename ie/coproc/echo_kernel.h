/*
 * ie/coproc/echo_kernel.h - shared P0.3 throughput kernel.
 *
 * One kernel body compiled into both the M68K worker service and the main-CPU
 * caller so the two throughput numbers measure the same work: a word sweep
 * over RAM (read + XOR-write), one Voodoo register write, and one IE64 ring
 * dispatch per iteration. Timing uses ie_time_usec on either CPU.
 *
 * The IE64 dispatch writes the ring-5 descriptor with explicit byte stores,
 * so it is endian-correct from the main CPU (byte-swapping) and from the
 * M68K worker (byte-swap skipped over the mailbox) alike.
 */
#ifndef IE_COPROC_ECHO_KERNEL_H
#define IE_COPROC_ECHO_KERNEL_H

#include <stdint.h>

#include "../ie_mmio.h"
#include "coproc_layout.h"
#include "echo_proto.h"

#define IE_ECHO_VOODOO_ENABLE 0x000F8004u
#define IE_ECHO_DESC_OP_OFF 8u

uint64_t ie_time_usec(void);

/* Enqueue a deliberately-unknown op on the IE64 ring and wait for the worker
 * to consume it (the service answers unknown ops with an error response and
 * advances the tail - dispatch legality is what this proves). */
static void echo_ie64_noop(void) {
    volatile uint8_t *ring = (volatile uint8_t *)(uintptr_t)IE_COPROC_IE64_RING_BASE;
    uint8_t head = ring[IE_COPROC_RING_HEAD_OFFSET];
    uint8_t next = (uint8_t)((head + 1u) & (IE_COPROC_RING_SLOTS - 1u));
    uint32_t desc = IE_COPROC_IE64_RING_BASE + IE_COPROC_RING_ENTRIES_OFFSET +
                    (uint32_t)head * IE_COPROC_REQ_DESC_SIZE;
    if (next == ring[IE_COPROC_RING_TAIL_OFFSET]) {
        return;
    }
    { volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)(desc + IE_ECHO_DESC_OP_OFF);
      p[0] = 0xff; p[1] = 0; p[2] = 0; p[3] = 0; }
    ring[IE_COPROC_RING_HEAD_OFFSET] = next;
    while (ring[IE_COPROC_RING_TAIL_OFFSET] != next) { }
}

/* Run the kernel `iterations` times and fill the status block, including the
 * self-timed microseconds per iteration. `dispatch_ie64` gates the per-
 * iteration IE64 ring round-trip. */
static void echo_kernel_run_ex(uint32_t iterations, int dispatch_ie64,
                               ie_echo_request *req, ie_echo_status *status) {
    uint32_t i, j, value = 0;
    uint64_t start = ie_time_usec();
    for (i = 0; i < iterations; i++) {
        for (j = 0; j < req->words; j++) {
            value ^= req->source[j];
            req->destination[j] = req->source[j] ^ IE_ECHO_XOR_VALUE;
        }
        ie_mmio_write32(IE_ECHO_VOODOO_ENABLE, 1u);
        if (dispatch_ie64) {
            echo_ie64_noop();
        }
    }
    status->value = value;
    status->operations = iterations;
    status->usec_per_op = iterations ? (uint32_t)((ie_time_usec() - start) / iterations) : 0u;
    status->flags = 0x0Fu;
}

#endif /* IE_COPROC_ECHO_KERNEL_H */
