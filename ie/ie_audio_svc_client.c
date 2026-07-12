/*
 * ie/ie_audio_svc_client.c - main-CPU client for the M68K audio service
 * (plan Phase 2, IE_AUDIO_SVC=1).
 *
 * The service is the SECOND M68K worker instance: COSTART selects it with
 * COPROC_INSTANCE=1 and the per-request traffic runs on ring 5, written
 * directly with little-endian byte stores exactly like the gfx client on
 * ring 4. One OP_AUDIO_PUMP per ie_audio_poll window (the same cadence the
 * local pump had), fire-and-forget with a drain of the previous pump before
 * each enqueue so at most one pump is in flight.
 *
 * The (start,end) command-ring index handoff replaces the OSMesg mailbox
 * (P2.0 audit condition 1); the high-water guard below is condition 2's
 * overflow protection for the 256-slot sAudioCmd ring.
 */
#include <stdint.h>

#include "coproc/coproc_layout.h"
#include "coproc/audio_svc_proto.h"
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
#define COPROC_INSTANCE (COPROC_BASE + 0x4Cu)
/* Per-(cpuType,instance) running bitmask, bit (cpuType*2 + instance). A single
 * atomic read that needs no COPROC_CPU_TYPE/COPROC_INSTANCE selection, so it
 * replaces the retired COPROC_WORKER_STATE (base + 0x34) bit-7 M68K-instance-1
 * overload, which no longer distinguishes the two M68K instances. */
#define COPROC_INSTANCE_STATE (0x000F25B8u)
#define CMD_START 1u
#define CMD_START_MEM 6u
#define CPU_M68K 4u
/* M68K (cpuType 4) instance 1: bit 4*2 + 1 = 9 in COPROC_INSTANCE_STATE. */
#define INSTANCE_STATE_M68K2_BIT (1u << 9)

#define RING_BASE IE_COPROC_M68K2_RING_BASE
#define RING_HEAD (RING_BASE + IE_COPROC_RING_HEAD_OFFSET)
#define RING_TAIL (RING_BASE + IE_COPROC_RING_TAIL_OFFSET)
#define RING_CAP (RING_BASE + IE_COPROC_RING_CAP_OFFSET)
#define RING_ENTRIES (RING_BASE + IE_COPROC_RING_ENTRIES_OFFSET)
#define DESC_TICKET 0u
#define DESC_OP 8u
#define DESC_REQ_PTR 16u
#define DESC_RESP_PTR 24u

/* If the producer runs this far ahead of the last consumed index within one
 * frame, force a synchronous drain+pump: the 256-slot ring has no ownership
 * handshake beyond the index arithmetic. Measured per-frame high water
 * (D_800EA4A4) stays far below this. */
#define CMD_RING_HIGH_WATER 0xC0u

struct SequencePlayer;

/* Referenced by address only, so the loose extern types are fine here and
 * avoid dragging the audio headers (this file never includes the redirect
 * header - it needs the real local symbols for the fallback defaults). */
extern unsigned char sAudioCmd[];            /* struct EuAudioCmd[0x100]   */
extern unsigned char D_800EA3A0[];           /* producer index, port_eu.c  */
extern unsigned char gSequencePlayers[];     /* local sequencer state      */
extern unsigned char gSequenceChannelNone;   /* local channel sentinel     */

/* Worker-state view for the producer-side readers (external.c). Defaults
 * keep the local path working when the service is absent. */
struct SequencePlayer *gIeAudioSvcSeqp =
    (struct SequencePlayer *)(void *) gSequencePlayers;
uintptr_t gIeAudioSvcChanNone = (uintptr_t) &gSequenceChannelNone;

static const char svc_name[] = "build/ie/audiosvc.ie68";

static ie_audio_pump_req g_pump_req;
static ie_audio_init_req g_init_req;
static ie_audio_resp g_resp;

static int g_active;
static int g_pending;
static uint8_t g_pending_head;
static uint8_t g_last_sent; /* producer index consumed through here */
static uint32_t g_ticket = 0x41000000u;

static inline uint8_t rd8(uint32_t a) { return *(volatile uint8_t *)(uintptr_t) a; }
static inline void wr8(uint32_t a, uint8_t v) { *(volatile uint8_t *)(uintptr_t) a = v; }

static uint32_t fb_rd(uint32_t off) {
    return *(volatile uint32_t *)(uintptr_t)(IE_AUDIO_SVC_FEEDBACK_BASE + off);
}

static void desc_wr32(uint32_t addr, uint32_t value) {
    wr8(addr + 0u, (uint8_t) value);
    wr8(addr + 1u, (uint8_t)(value >> 8));
    wr8(addr + 2u, (uint8_t)(value >> 16));
    wr8(addr + 3u, (uint8_t)(value >> 24));
}

int ie_audio_svc_active(void) {
    return g_active;
}

/* Fail-closed gate for channel-pointer readers (redirect header). */
uint32_t ie_audio_svc_load_active(void) {
    if (!g_active) {
        return 0;
    }
    return fb_rd(IE_AUDIO_SVC_FB_LOAD_OFF);
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
        const char *b = "svc/audio.ie68";
        while (*a != '\0' && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            ie_mmio_write32(COPROC_CPU_TYPE, CPU_M68K);
            ie_mmio_write32(COPROC_INSTANCE, 1u);
            ie_mmio_write32(COPROC_REQ_PTR, IE_LOAD_BASE + e->offset);
            ie_mmio_write32(COPROC_REQ_LEN, e->size);
            ie_mmio_write32(COPROC_CMD, CMD_START_MEM);
            ie_mmio_write32(COPROC_INSTANCE, 0u);
            return 1;
        }
    }
    return 0;
}

/*
 * COSTART the audio service (M68K instance 1) and run OP_AUDIO_INIT
 * synchronously: the worker performs audio_init and the boot reset drain,
 * then publishes its sequencer addresses through the feedback block.
 * Returns 0 (local path untouched) on any failure. Segment loading and
 * _AudioInit (host backend registration + one-time audio MMIO enable) have
 * already run on the main CPU.
 */
int ie_audio_svc_init(void) {
    extern unsigned char *_audio_banksSegmentRomStart;
    extern unsigned char *_audio_tablesSegmentRomStart;
    extern unsigned char *_audio_tablesPcm16SegmentRomStart;
    extern unsigned char *_instrument_setsSegmentRomStart;
    extern unsigned char *_sequencesSegmentRomStart;

    /* The pack/ROM load clobbers the mailbox; reset ring 5 BEFORE the
     * worker starts polling it. */
    wr8(RING_HEAD, 0u);
    wr8(RING_TAIL, 0u);
    wr8(RING_CAP, 16u);
    *(volatile uint32_t *)(uintptr_t)(IE_AUDIO_SVC_FEEDBACK_BASE + IE_AUDIO_SVC_FB_MAGIC_OFF) = 0;

    if (!svc_start_from_pack()) {
        ie_mmio_write32(COPROC_NAME_PTR, (uint32_t)(uintptr_t) svc_name);
        ie_mmio_write32(COPROC_CPU_TYPE, CPU_M68K);
        ie_mmio_write32(COPROC_INSTANCE, 1u);
        ie_mmio_write32(COPROC_CMD, CMD_START);
        ie_mmio_write32(COPROC_INSTANCE, 0u);
    }
    if (ie_mmio_read32(COPROC_CMD_STATUS) != 0u) {
        platform_log("audio svc: COSTART failed (status %u error %u)",
                     ie_mmio_read32(COPROC_CMD_STATUS),
                     ie_mmio_read32(COPROC_BASE + 0x0Cu));
        return 0;
    }
    if ((ie_mmio_read32(COPROC_INSTANCE_STATE) & INSTANCE_STATE_M68K2_BIT) == 0u) {
        platform_log("audio svc: worker state bit missing (state 0x%x)",
                     ie_mmio_read32(COPROC_INSTANCE_STATE));
        return 0;
    }

    g_init_req.banks_rom = (uint32_t)(uintptr_t) _audio_banksSegmentRomStart;
    g_init_req.tables_rom = (uint32_t)(uintptr_t) _audio_tablesSegmentRomStart;
    g_init_req.tables_pcm16_rom = (uint32_t)(uintptr_t) _audio_tablesPcm16SegmentRomStart;
    g_init_req.instrument_sets_rom = (uint32_t)(uintptr_t) _instrument_setsSegmentRomStart;
    g_init_req.sequences_rom = (uint32_t)(uintptr_t) _sequencesSegmentRomStart;
    g_init_req.cmd_ring = (uint32_t)(uintptr_t) sAudioCmd;

    g_resp.status = 0;
    g_active = 1; /* enqueue path live from here */
    svc_call_sync(IE_AUDIO_OP_INIT, &g_init_req, &g_resp);
    if (g_resp.status != 1u || fb_rd(IE_AUDIO_SVC_FB_MAGIC_OFF) != IE_AUDIO_SVC_FB_MAGIC) {
        platform_log("audio svc: INIT failed (resp %u magic 0x%x)",
                     g_resp.status, fb_rd(IE_AUDIO_SVC_FB_MAGIC_OFF));
        g_active = 0;
        return 0;
    }

    gIeAudioSvcSeqp = (struct SequencePlayer *)(uintptr_t) fb_rd(IE_AUDIO_SVC_FB_SEQP_OFF);
    gIeAudioSvcChanNone = (uintptr_t) fb_rd(IE_AUDIO_SVC_FB_CHAN0_OFF);
    g_last_sent = D_800EA3A0[0];

    platform_log("audio service online (M68K worker B)");
    return 1;
}

/*
 * One pump kick, called at the local pump's cadence (ie_audio_poll window).
 * Drains the previous pump first so at most one is in flight and the worker
 * never reads a request block being rewritten.
 */
void ie_audio_svc_pump(void) {
    uint8_t producer;
    uint8_t backlog;

    svc_drain();
    ie_compiler_barrier();

    /* Backlog is measured against the PREVIOUS consumed baseline, before
     * g_last_sent moves - measuring after would always read ~0 and the
     * guard below could never fire. */
    producer = D_800EA3A0[0];
    backlog = (uint8_t)(producer - g_last_sent);

    g_pump_req.cmd_start = g_last_sent;
    g_pump_req.cmd_end = producer;
    g_last_sent = producer;

    svc_enqueue(IE_AUDIO_OP_PUMP, &g_pump_req, &g_resp);

    /* Overflow guard: a backlog near ring capacity means producers outran
     * the pump during the previous flight and the u8 index arithmetic is
     * close to aliasing the 256-slot ring. Wait this pump out so producers
     * cannot lap the worker's in-flight [start,end) window. */
    if (backlog > CMD_RING_HIGH_WATER) {
        svc_drain();
    }
}
