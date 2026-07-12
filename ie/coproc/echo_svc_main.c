#include <stdint.h>

#include "coproc_layout.h"
#include "echo_kernel.h"
#include "echo_proto.h"

#define DESC_OP_OFF 8u
#define DESC_REQ_PTR_OFF 16u
#define DESC_RESP_PTR_OFF 24u
#define RESP_TICKET_OFF 0u
#define RESP_STATUS_OFF 4u
#define RESP_OK 2u

static uint32_t rd32(uint32_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}
static void wr32(uint32_t addr, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static void handle(uint32_t op, ie_echo_request *req, ie_echo_status *status) {
    uint32_t iterations = op == IE_ECHO_OP_ONCE ? 1u : req->iterations;
    echo_kernel_run_ex(iterations, op != IE_ECHO_OP_SWEEP_ONLY, req, status);
}

void ie_main(void) {
    volatile uint8_t *ring = (volatile uint8_t *)(uintptr_t)IE_COPROC_M68K_RING_BASE;
    /* Version-gate handshake: echo the layout version so the host routes work
     * here instead of failing START with COPROC_ERR_STALE_WORKER. */
    ring[IE_COPROC_RING_ACK_OFFSET] = (uint8_t)IE_COPROC_LAYOUT_VERSION;
    for (;;) {
        uint8_t tail = ring[IE_COPROC_RING_TAIL_OFFSET];
        if (tail != ring[IE_COPROC_RING_HEAD_OFFSET]) {
            uint32_t desc = IE_COPROC_M68K_RING_BASE + IE_COPROC_RING_ENTRIES_OFFSET +
                            (uint32_t)tail * IE_COPROC_REQ_DESC_SIZE;
            uint32_t resp = IE_COPROC_M68K_RING_BASE + IE_COPROC_RING_RESP_OFFSET +
                            (uint32_t)tail * IE_COPROC_RESP_DESC_SIZE;
            uint32_t ticket = rd32(desc + 0u);
            handle(rd32(desc + DESC_OP_OFF),
                   (ie_echo_request *)(uintptr_t)rd32(desc + DESC_REQ_PTR_OFF),
                   (ie_echo_status *)(uintptr_t)rd32(desc + DESC_RESP_PTR_OFF));
            wr32(resp + RESP_TICKET_OFF, ticket);
            wr32(resp + RESP_STATUS_OFF, RESP_OK);
            ring[IE_COPROC_RING_TAIL_OFFSET] = (uint8_t)((tail + 1u) & 15u);
        }
    }
}
