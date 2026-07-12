#include <stdint.h>
#include "ie_mmio.h"
#include "coproc/ie_coproc.h"
#include "coproc/echo_kernel.h"
#include "coproc/echo_proto.h"

#define C 0x000F2340u
#define CMD (C+0x00u)
#define CPU (C+0x04u)
#define CMD_STATUS (C+0x08u)
#define TICKET (C+0x10u)
#define TICKET_STATUS (C+0x14u)
#define OP (C+0x18u)
#define REQ_PTR (C+0x1Cu)
#define REQ_LEN (C+0x20u)
#define RESP_PTR (C+0x24u)
#define RESP_CAP (C+0x28u)
#define TIMEOUT (C+0x2Cu)
#define NAME_PTR (C+0x30u)
#define CMD_START 1u
#define CMD_ENQUEUE 3u
#define CMD_WAIT 5u
#define CPU_M68K 4u

/* P0.3 kernel shape: 4 KiB sweep per iteration, enough iterations to hide
 * timer granularity while keeping the smoke under a second per side. */
#define THROUGHPUT_WORDS 1024u
#define THROUGHPUT_ITERATIONS 200u

static const char echo_name[] = "build/ie/echo.ie68";
static ie_echo_request request;
static ie_echo_status response;
static uint32_t sweep_src[THROUGHPUT_WORDS];
static uint32_t sweep_dst[THROUGHPUT_WORDS];

static uint32_t run_worker(uint32_t op, uint32_t timeout_ms) {
    uint32_t ticket;
    ie_compiler_barrier(); /* request stores must precede the enqueue */
    ie_mmio_write32(CPU, CPU_M68K);
    ie_mmio_write32(OP, op);
    ie_mmio_write32(REQ_PTR, (uint32_t)(uintptr_t)&request);
    ie_mmio_write32(REQ_LEN, sizeof(request));
    ie_mmio_write32(RESP_PTR, (uint32_t)(uintptr_t)&response);
    ie_mmio_write32(RESP_CAP, sizeof(response));
    ie_mmio_write32(CMD, CMD_ENQUEUE);
    ticket = ie_mmio_read32(TICKET);
    if (ticket != 0u) {
        ie_mmio_write32(TICKET, ticket);
        ie_mmio_write32(TIMEOUT, timeout_ms);
        ie_mmio_write32(CMD, CMD_WAIT);
    }
    ie_compiler_barrier(); /* response loads must follow the wait */
    return ticket;
}

void ie_main(void) {
    volatile uint32_t *boot = (volatile uint32_t *)(uintptr_t)IE_BOOT_STATUS_ADDR;
    volatile uint32_t *src = (volatile uint32_t *)(uintptr_t)0x10000000u;
    volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)0x10001000u;
    ie_echo_status local_status, local_status2;
    uint32_t i, ticket;
    int ok = ie_coproc_init();

    ie_mmio_write32(CPU, CPU_M68K);
    ie_mmio_write32(NAME_PTR, (uint32_t)(uintptr_t)echo_name);
    ie_mmio_write32(CMD, CMD_START);
    ok = ok && ie_mmio_read32(CMD_STATUS) == 0u;

    /* P0.2: single BE echo round-trip through the worker. */
    *src = 0x12345678u;
    request.source = src; request.destination = dst;
    request.iterations = 1; request.words = 1;
    ticket = run_worker(IE_ECHO_OP_ONCE, 5000u);
    ok = ok && ticket != 0u && *dst == 0xB76E6ABBu && response.flags == 0x0Fu;

    /* P0.3: identical throughput kernel, main CPU then worker. */
    for (i = 0; i < THROUGHPUT_WORDS; i++) {
        sweep_src[i] = i * 0x01010101u;
    }
    request.source = sweep_src; request.destination = sweep_dst;
    request.iterations = THROUGHPUT_ITERATIONS; request.words = THROUGHPUT_WORDS;
    echo_kernel_run_ex(THROUGHPUT_ITERATIONS, 1, &request, &local_status);
    response.usec_per_op = 0;
    ticket = run_worker(IE_ECHO_OP_THROUGHPUT, 20000u);
    ok = ok && ticket != 0u && response.flags == 0x0Fu &&
         response.value == local_status.value;
    boot[8] = response.usec_per_op;       /* worker µs, kernel + IE64 dispatch */

    /* Sweep-only pass, both sides, to attribute the IE64 ring-wait share. */
    echo_kernel_run_ex(THROUGHPUT_ITERATIONS, 0, &request, &local_status2);
    response.usec_per_op = 0;
    ticket = run_worker(IE_ECHO_OP_SWEEP_ONLY, 20000u);
    ok = ok && ticket != 0u && response.flags == 0x0Fu;

    boot[4] = ok ? 0x0Fu : 0u;
    boot[5] = *dst;
    boot[6] = response.flags;
    boot[7] = local_status.usec_per_op;   /* main-CPU µs, kernel + IE64 dispatch */
    boot[9] = local_status2.usec_per_op;  /* main-CPU µs, sweep only           */
    boot[10] = response.usec_per_op;      /* worker µs, sweep only             */
    boot[0] = 0x4D4B3634u;
}
