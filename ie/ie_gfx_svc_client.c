/*
 * ie/ie_gfx_svc_client.c - main-CPU client for the M68K gfx service
 * (plan Phase 1, IE_GFX_SVC=1).
 *
 * The client owns the main side of the frame-flight contract: at most one
 * frame is queued or in flight, and every enqueue is preceded by a drain of
 * the previous frame (the completion barrier; worker completion is
 * vblank-paced). The audio pump runs right after the enqueue, once per
 * main-loop iteration, at the same vblank-derived cadence as the old
 * swap-end frame hook.
 *
 * Ring protocol: descriptors on ring 4 are written directly with explicit
 * little-endian byte stores (the worker reads the mailbox unswapped), same
 * as ie_coproc.c does on ring 10. The request/response blocks are native
 * big-endian structs in this image's .bss. COSTART goes through the manager
 * MMIO once, from the pack blob when present.
 */
#include <stdint.h>

#include "coproc/coproc_layout.h"
#include "coproc/gfx_svc_proto.h"
#include "ie_mmio.h"
#include "ie_pack.h"

#include "platform/platform.h"

#define COPROC_BASE 0x000F2340u
#define COPROC_CMD (COPROC_BASE + 0x00u)
#define COPROC_CPU_TYPE (COPROC_BASE + 0x04u)
#define COPROC_CMD_STATUS (COPROC_BASE + 0x08u)
#define COPROC_REQ_PTR (COPROC_BASE + 0x1Cu)
#define COPROC_REQ_LEN (COPROC_BASE + 0x20u)
#define COPROC_NAME_PTR (COPROC_BASE + 0x30u)
#define COPROC_WORKER_STATE (COPROC_BASE + 0x34u)
#define CMD_START 1u
#define CMD_START_MEM 6u
#define CPU_M68K 4u

#define RING_BASE IE_COPROC_M68K_RING_BASE
#define RING_HEAD (RING_BASE + IE_COPROC_RING_HEAD_OFFSET)
#define RING_TAIL (RING_BASE + IE_COPROC_RING_TAIL_OFFSET)
#define RING_CAP (RING_BASE + IE_COPROC_RING_CAP_OFFSET)
#define RING_ENTRIES (RING_BASE + IE_COPROC_RING_ENTRIES_OFFSET)
#define DESC_TICKET 0u
#define DESC_OP 8u
#define DESC_REQ_PTR 16u
#define DESC_RESP_PTR 24u

void ie_audio_poll(void);

static const char svc_name[] = "build/ie/gfxsvc.ie68";

static ie_gfx_frame_req g_req;
static ie_gfx_frame_resp g_resp;

/* Two invalidate lists: the worker may still be reading the in-flight
 * frame's list, so producers accumulate into the other one. */
static void *g_inval[2][IE_GFX_INVAL_MAX];
static uint32_t g_inval_count;
static uint32_t g_inval_slot;

static int g_active;
static int g_pending;          /* a frame is enqueued but not yet drained */
static uint8_t g_pending_head; /* ring head we wrote; tail there = done   */
static uint32_t g_ticket = 0x47000000u;

static inline uint8_t rd8(uint32_t a) { return *(volatile uint8_t *)(uintptr_t) a; }
static inline void wr8(uint32_t a, uint8_t v) { *(volatile uint8_t *)(uintptr_t) a = v; }

/* Store a u32 into the mailbox in the little-endian byte order the worker
 * reads it back with (byte-swap is skipped over the mailbox). */
static void desc_wr32(uint32_t addr, uint32_t value) {
    wr8(addr + 0u, (uint8_t) value);
    wr8(addr + 1u, (uint8_t)(value >> 8));
    wr8(addr + 2u, (uint8_t)(value >> 16));
    wr8(addr + 3u, (uint8_t)(value >> 24));
}

/* Frame-parity index for the kart texture double buffer (see
 * KART_TEXTURE_ARENA in src/buffers.h). Flipped once per enqueued frame
 * so the game decodes into the copy the in-flight frame is NOT reading.
 * Stays 0 while the service is inactive (single-arena local path). */
uint32_t gIeGfxKartParity;

int ie_gfx_svc_active(void) {
    return g_active;
}

static void svc_drain(void) {
    if (!g_pending) {
        return;
    }
    while (rd8(RING_TAIL) != g_pending_head) {
    }
    g_pending = 0;
}

static void svc_enqueue(uint32_t op, void *req, void *resp) {
    uint8_t head = rd8(RING_HEAD);
    uint8_t next = (uint8_t)((head + 1u) & (IE_COPROC_RING_SLOTS - 1u));
    uint32_t desc = RING_ENTRIES + (uint32_t) head * IE_COPROC_REQ_DESC_SIZE;
    ie_compiler_barrier(); /* request stores must precede the ring kick */
    desc_wr32(desc + DESC_TICKET, g_ticket++);
    desc_wr32(desc + DESC_OP, op);
    desc_wr32(desc + DESC_REQ_PTR, (uint32_t)(uintptr_t) req);
    desc_wr32(desc + DESC_RESP_PTR, (uint32_t)(uintptr_t) resp);
    wr8(RING_HEAD, next);
    g_pending = 1;
    g_pending_head = next;
}

static void svc_call_sync(uint32_t op, void *req, void *resp) {
    svc_enqueue(op, req, resp);
    svc_drain();
    ie_compiler_barrier(); /* response loads must follow the drain */
}

/* Barrier for producers that overwrite texture memory a still-in-flight
 * frame may reference (transition-time decoders, framebuffer-sourced
 * course textures). No-op when the service is idle or inactive. */
void ie_gfx_svc_drain_for_write(void) {
    if (!g_active) {
        return;
    }
    svc_drain();
    ie_compiler_barrier();
}

static int svc_start_from_pack(void) {
    const ie_pack_header *hdr = (const ie_pack_header *)(uintptr_t) IE_PACK_HDR_ADDR;
    const ie_pack_entry *e;
    uint32_t i;

    if (hdr->magic0 != IE_PACK_MAGIC0 || hdr->magic1 != IE_PACK_MAGIC1) {
        return 0;
    }
    e = (const ie_pack_entry *)(const void *) (hdr + 1);
    for (i = 0; i < hdr->toc_count; i++, e++) {
        const char *a = e->name;
        const char *b = "svc/gfx.ie68";
        while (*a != '\0' && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            ie_mmio_write32(COPROC_CPU_TYPE, CPU_M68K);
            ie_mmio_write32(COPROC_REQ_PTR, IE_LOAD_BASE + e->offset);
            ie_mmio_write32(COPROC_REQ_LEN, e->size);
            ie_mmio_write32(COPROC_CMD, CMD_START_MEM);
            return 1;
        }
    }
    return 0;
}

/* COSTART the gfx service and run OP_GFX_INIT synchronously. Failure leaves
 * g_active clear and the local translator path untouched. Must run AFTER
 * ie_coproc_init (the service attaches to the already-started IE64 worker).
 */
int ie_gfx_svc_init(void) {
    /* The pack/ROM load clobbers the mailbox; reset ring 4 BEFORE the
     * worker starts polling it (same rationale as ie_coproc_init). */
    wr8(RING_HEAD, 0u);
    wr8(RING_TAIL, 0u);
    wr8(RING_CAP, 16u);

    if (!svc_start_from_pack()) {
        ie_mmio_write32(COPROC_NAME_PTR, (uint32_t)(uintptr_t) svc_name);
        ie_mmio_write32(COPROC_CPU_TYPE, CPU_M68K);
        ie_mmio_write32(COPROC_CMD, CMD_START);
    }
    if (ie_mmio_read32(COPROC_CMD_STATUS) != 0u) {
        return 0;
    }
    if ((ie_mmio_read32(COPROC_WORKER_STATE) & (1u << CPU_M68K)) == 0u) {
        return 0;
    }

    g_resp.status = 0;
    g_active = 1; /* svc_enqueue path live from here */
    svc_call_sync(IE_GFX_OP_INIT, 0, &g_resp);
    if (g_resp.status != 1u) {
        g_active = 0;
        return 0;
    }
    platform_log("gfx service online (M68K worker)");
    return 1;
}

void ie_gfx_svc_invalidate(void *addr) {
    if (g_inval_count >= IE_GFX_INVAL_MAX) {
        platform_fatal("gfx svc: invalidate list overflow");
    }
    g_inval[g_inval_slot][g_inval_count++] = addr;
}

extern uintptr_t gSegmentTable[16];
extern int32_t gIsMirrorMode;

void ie_gfx_svc_frame(void *dl) {
    uint32_t i;

    /* Drain frame N before enqueueing N+1: the one barrier point. After
     * this the worker no longer reads the previous request, segment
     * snapshot or invalidate list. */
    svc_drain();
    ie_compiler_barrier();

    g_req.dl = dl;
    for (i = 0; i < 16; i++) {
        g_req.segment_table[i] = gSegmentTable[i];
    }
    g_req.is_mirror = (uint32_t) gIsMirrorMode;
    g_req.invalidate_list = (void *const *) g_inval[g_inval_slot];
    g_req.invalidate_count = g_inval_count;
    g_req.flags = 0;
    g_inval_slot ^= 1u;
    g_inval_count = 0;

    svc_enqueue(IE_GFX_OP_FRAME, &g_req, &g_resp);
#ifdef IE_GFX_SVC_SYNC
    /* Bring-up / bisection mode: no pipelining, same code paths. */
    svc_drain();
#endif
    /* The frame just enqueued reads the current-parity kart arena copy;
     * flip so the next iteration decodes into the other one. */
    gIeGfxKartParity ^= 1u;

    /* One pump per main-loop iteration, right after the barrier+enqueue -
     * the same vblank-derived cadence the swap-end frame hook provided. */
    ie_audio_poll();
}

void ie_gfx_svc_reset(void) {
    svc_drain();
    svc_call_sync(IE_GFX_OP_RESET, 0, &g_resp);
}
