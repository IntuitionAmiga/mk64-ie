/*
 * ie/coproc/gfx_svc_main.c - M68K worker service entry for the gfx
 * translation offload (plan Phase 1).
 *
 * A second compile pass of the shared translator TUs (gfx_pc, gfx_fast3d,
 * gfx_cc, ie_gfx_voodoo, ie_gfx_window, ie_coproc) links against this ring-2
 * dispatch loop under -DIE_GFX_SERVICE. The service owns translation,
 * Voodoo submission and swap pacing; everything ordered against game state
 * stays on the main CPU and arrives per frame in the request block.
 *
 * The two game-state imports the translator TUs need (P1.0 audit) are
 * defined here: gIsMirrorMode is a per-frame flag, segmented_to_virtual
 * resolves against the per-frame segment snapshot.
 */
#include <stdint.h>

#include "coproc_layout.h"
#include "gfx_svc_proto.h"

#include "gfx/gfx_pc.h"
#include "gfx/gfx_rendering_api.h"
#include "gfx/gfx_window_manager_api.h"

/* Descriptor field offsets within a 32-byte mailbox request entry (read
 * unswapped: the manager stores them little-endian and CoprocMode skips the
 * byte-swap over the mailbox). */
#define DESC_OP_OFF 8u
#define DESC_REQ_PTR_OFF 16u
#define DESC_RESP_PTR_OFF 24u
#define RESP_TICKET_OFF 0u
#define RESP_STATUS_OFF 4u
#define RESP_OK 2u

extern struct GfxRenderingAPI ie_gfx_voodoo_api;
extern struct GfxWindowManagerAPI ie_gfx_window_api;

void gfx_texture_cache_invalidate(void *addr);
void nuke_everything(void);
uint64_t ie_time_usec(void);
int ie_coproc_attach(void);

/* Game-state imports (see P1.0 audit in GFX_SVC_NOTES.md). */
int32_t gIsMirrorMode;
static uintptr_t svc_segments[16];

void *segmented_to_virtual(const void *addr) {
    uintptr_t a = (uintptr_t) addr;
    uintptr_t segment = a >> 24;
    if (segment > 0x0F) {
        return (void *) a;
    }
    return (void *) (svc_segments[segment] + (a & 0x00FFFFFF));
}

static uint32_t frames_done;

/* Bring-up trace + timing block at a fixed RAM address for IEScript
 * (0x9EF80..0x9EFA3, next to the ie_coproc drain stats at 0x9EF00).
 * Slots: 0 op, 1 phase, 2 ticket, 3 last frame usec (incl swap wait),
 * 4 frames done, 5 last translation+submit usec (pre swap wait),
 * 6 cumulative translation usec, 7 cumulative frame usec, 8 max
 * translation usec. Cumulative slots let measurement scripts compute
 * span averages from two reads - debugger freezes are expensive
 * (~0.7 s of guest progress each), so per-frame polling distorts. */
#define SVC_TRACE_BASE 0x0009EF80u
static void svc_trace(uint32_t slot, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)(SVC_TRACE_BASE + slot * 4u) = value;
}

static uint32_t rd32(uint32_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}
static void wr32(uint32_t addr, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static void handle_frame(const ie_gfx_frame_req *req, ie_gfx_frame_resp *resp) {
    uint32_t i;
    uint32_t xlat_us;
    uint64_t start = ie_time_usec();
    for (i = 0; i < 16; i++) {
        svc_segments[i] = req->segment_table[i];
    }
    gIsMirrorMode = (int32_t) req->is_mirror;
    svc_trace(1, 1);
    for (i = 0; i < req->invalidate_count; i++) {
        gfx_texture_cache_invalidate((void *) req->invalidate_list[i]);
    }
    svc_trace(1, 2);
    gfx_start_frame();
    gfx_run((Gfx *) req->dl);
    svc_trace(1, 3);
    /* Slot 5: translation + submit time, BEFORE gfx_end_frame's
     * vblank-paced swap. Slot 3 minus this is almost all pacing wait. */
    xlat_us = (uint32_t)(ie_time_usec() - start);
    svc_trace(5, xlat_us);
    gfx_end_frame();
    svc_trace(1, 4);
    if (resp != 0) {
        resp->usec = (uint32_t)(ie_time_usec() - start);
        resp->frames = ++frames_done;
        resp->status = 1;
    }
    /* Also publish to the fixed trace block (slots 3/4) so measurement
     * scripts don't have to chase the client's .bss around the map. */
    {
        static uint32_t cum_xlat_us, cum_frame_us, max_xlat_us;
        uint32_t frame_us = (uint32_t)(ie_time_usec() - start);
        svc_trace(3, frame_us);
        svc_trace(4, frames_done);
        cum_xlat_us += xlat_us;
        cum_frame_us += frame_us;
        if (xlat_us > max_xlat_us) {
            max_xlat_us = xlat_us;
        }
        svc_trace(6, cum_xlat_us);
        svc_trace(7, cum_frame_us);
        svc_trace(8, max_xlat_us);
    }
}

static void handle(uint32_t op, uint32_t req_ptr, uint32_t resp_ptr) {
    switch (op) {
    case IE_GFX_OP_INIT:
        /* The main CPU already COSTARTed the IE64 T&L worker and reset its
         * ring; this compile pass only needs the dispatch flag armed. */
        ie_coproc_attach();
        gfx_init(&ie_gfx_window_api, &ie_gfx_voodoo_api, "Mario Kart 64", 0);
        if (resp_ptr != 0) {
            ((ie_gfx_frame_resp *)(uintptr_t) resp_ptr)->status = 1;
        }
        break;
    case IE_GFX_OP_FRAME:
        handle_frame((const ie_gfx_frame_req *)(uintptr_t) req_ptr,
                     (ie_gfx_frame_resp *)(uintptr_t) resp_ptr);
        break;
    case IE_GFX_OP_RESET:
        nuke_everything();
        if (resp_ptr != 0) {
            ((ie_gfx_frame_resp *)(uintptr_t) resp_ptr)->status = 1;
        }
        break;
    default:
        break;
    }
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
            svc_trace(0, rd32(desc + DESC_OP_OFF));
            handle(rd32(desc + DESC_OP_OFF), rd32(desc + DESC_REQ_PTR_OFF),
                   rd32(desc + DESC_RESP_PTR_OFF));
            svc_trace(2, ticket);
            wr32(resp + RESP_TICKET_OFF, ticket);
            wr32(resp + RESP_STATUS_OFF, RESP_OK);
            ring[IE_COPROC_RING_TAIL_OFFSET] =
                (uint8_t)((tail + 1u) & (IE_COPROC_RING_SLOTS - 1u));
        }
    }
}
