#ifndef IE_COPROC_ECHO_PROTO_H
#define IE_COPROC_ECHO_PROTO_H

#include <stdint.h>

#define IE_ECHO_OP_ONCE       0x4543484Fu
#define IE_ECHO_OP_THROUGHPUT 0x42574D4Bu
/* Same sweep + Voodoo write, but no IE64 dispatch: isolates how much of the
 * throughput number is ring-wait latency vs bus/JIT work. */
#define IE_ECHO_OP_SWEEP_ONLY 0x53575030u
#define IE_ECHO_XOR_VALUE     0xA55A3CC3u

typedef struct ie_echo_request {
    volatile uint32_t *source;
    volatile uint32_t *destination;
    uint32_t iterations;
    uint32_t words;
} ie_echo_request;

typedef struct ie_echo_status {
    uint32_t value;
    uint32_t usec_per_op;
    uint32_t operations;
    uint32_t flags;
} ie_echo_status;

#endif
