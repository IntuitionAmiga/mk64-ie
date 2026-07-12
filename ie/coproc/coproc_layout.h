#ifndef IE_COPROC_LAYOUT_H
#define IE_COPROC_LAYOUT_H

/* Manual mirror of IntuitionEngine/coprocessor_constants.go. Keep all guest
 * coprocessor code derived from this header; old design documents contain
 * obsolete ring addresses. */
#define IE_COPROC_MAILBOX_BASE       0x00790000u
#define IE_COPROC_MAILBOX_END        0x00793000u
#define IE_COPROC_RING_STRIDE        0x00000400u
#define IE_COPROC_RING_HEAD_OFFSET   0x000u
#define IE_COPROC_RING_TAIL_OFFSET   0x001u
#define IE_COPROC_RING_CAP_OFFSET    0x002u
/* Version-gate handshake bytes in the reserved header ahead of the entries.
 * The host publishes the layout version at +0x03 and clears the ack at +0x04;
 * a conforming worker echoes IE_COPROC_LAYOUT_VERSION into +0x04 at startup or
 * the host tears it down with COPROC_ERR_STALE_WORKER. */
#define IE_COPROC_RING_LAYOUT_VER_OFFSET 0x003u
#define IE_COPROC_RING_ACK_OFFSET    0x004u
#define IE_COPROC_LAYOUT_VERSION     1u
#define IE_COPROC_RING_ENTRIES_OFFSET 0x008u
#define IE_COPROC_RING_RESP_OFFSET   0x208u
#define IE_COPROC_RING_SLOTS         16u
#define IE_COPROC_REQ_DESC_SIZE      32u
#define IE_COPROC_RESP_DESC_SIZE     16u

/* Ring index = cpuTypeToIndex(cpuType)*2 + instance, uniform 0x400 stride.
 * M68K cpuTypeToIndex is 2, IE64 is 5. Instance 0 unless noted. */
#define IE_COPROC_RING_INDEX_M68K    4u
#define IE_COPROC_RING_INDEX_IE64    10u
/* Second M68K worker instance (COPROC_INSTANCE=1): ring 5, own RAM window.
 * With the 0x400 stride a ring's content (0x308 bytes) no longer overflows
 * into the next ring's header, so instance 1 lands at the plain uniform slot. */
#define IE_COPROC_RING_INDEX_M68K2   5u
#define IE_COPROC_RING_BASE(index) \
    (IE_COPROC_MAILBOX_BASE + (index) * IE_COPROC_RING_STRIDE)
#define IE_COPROC_M68K_RING_BASE \
    IE_COPROC_RING_BASE(IE_COPROC_RING_INDEX_M68K)
#define IE_COPROC_M68K2_RING_BASE \
    IE_COPROC_RING_BASE(IE_COPROC_RING_INDEX_M68K2)
#define IE_COPROC_IE64_RING_BASE \
    IE_COPROC_RING_BASE(IE_COPROC_RING_INDEX_IE64)

#define IE_COPROC_M68K_RAM_BASE      0x00280000u
#define IE_COPROC_M68K_RAM_END       0x00300000u
#define IE_COPROC_M68K2_RAM_BASE     0x00420000u
#define IE_COPROC_M68K2_RAM_END      0x004A0000u
#define IE_COPROC_X86_RAM_BASE       0x00320000u
#define IE_COPROC_X86_RAM_END        0x003A0000u
#define IE_COPROC_IE64_RAM_BASE      0x003A0000u
#define IE_COPROC_IE64_RAM_END       0x00420000u

#endif
