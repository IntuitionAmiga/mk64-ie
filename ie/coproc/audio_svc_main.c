/*
 * ie/coproc/audio_svc_main.c - M68K worker service entry for the audio
 * sequencer offload (plan Phase 2, second M68K instance, ring index 6).
 *
 * A second compile pass of the audio TUs links against this dispatch loop
 * under -DIE_AUDIO_SERVICE. The worker owns the whole sequencer: audio_init
 * and the boot reset drain, sequence loads, process_sequences and the voice
 * MMIO updates. The main CPU keeps the producer half (sound requests packed
 * into its sAudioCmd ring) and passes the consumed index range per pump
 * (see audio_svc_proto.h and the P2.0 audit in GFX_SVC_NOTES.md).
 */
#include <stdint.h>

#include <ultra64.h>

#include "coproc_layout.h"
#include "audio_svc_proto.h"

#include "audio/data.h"
#include "audio/heap.h"
#include "audio/load.h"
#include "audio/port_eu.h"

/* Descriptor field offsets within a 32-byte mailbox request entry (read
 * unswapped, little-endian - see gfx_svc_main.c). */
#define DESC_OP_OFF 8u
#define DESC_REQ_PTR_OFF 16u
#define DESC_RESP_PTR_OFF 24u
#define RESP_TICKET_OFF 0u
#define RESP_STATUS_OFF 4u
#define RESP_OK 2u

uint64_t ie_time_usec(void);
void audio_init(void);
void game_audio_pump_voices(void);
void ie_audio_voice_note_reset_drained(void);

/* Service-mode mailbox replacement, defined in port_eu.c. */
extern volatile u32 ie_audio_svc_cmd_word;
extern volatile u32 ie_audio_svc_cmd_avail;

/* Audio ROM segment buffers, latched from the init request. The main CPU
 * loaded them; both CPUs read guest RAM with the same big-endian view. */
u8 *_audio_banksSegmentRomStart;
u8 *_audio_tablesSegmentRomStart;
u8 *_audio_tablesPcm16SegmentRomStart;
u8 *_instrument_setsSegmentRomStart;
u8 *_sequencesSegmentRomStart;

static uint32_t pumps_done;

static void fb_wr(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)(IE_AUDIO_SVC_FEEDBACK_BASE + off) = value;
}

void ie_audio_svc_set_load_active(u32 on) {
    fb_wr(IE_AUDIO_SVC_FB_LOAD_OFF, on);
}

static uint32_t rd32(uint32_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}
static void wr32(uint32_t addr, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static void handle_init(const ie_audio_init_req *req, ie_audio_resp *resp) {
    _audio_banksSegmentRomStart = (u8 *)(uintptr_t) req->banks_rom;
    _audio_tablesSegmentRomStart = (u8 *)(uintptr_t) req->tables_rom;
    _audio_tablesPcm16SegmentRomStart = (u8 *)(uintptr_t) req->tables_pcm16_rom;
    _instrument_setsSegmentRomStart = (u8 *)(uintptr_t) req->instrument_sets_rom;
    _sequencesSegmentRomStart = (u8 *)(uintptr_t) req->sequences_rom;
    ie_audio_svc_bind_cmds((void *)(uintptr_t) req->cmd_ring);

    audio_init();

    /* Drain the boot reset state machine exactly as the local path does in
     * setup_audio_data - the first real pump must not run shutdown steps. */
    while (gAudioResetStatus != 0) {
        game_audio_pump_voices();
    }
    ie_audio_voice_note_reset_drained();

    fb_wr(IE_AUDIO_SVC_FB_SEQP_OFF, (uint32_t)(uintptr_t) &gSequencePlayers[0]);
    fb_wr(IE_AUDIO_SVC_FB_CHAN0_OFF, (uint32_t)(uintptr_t) &gSequenceChannelNone);
    fb_wr(IE_AUDIO_SVC_FB_LOAD_OFF, 0);
    fb_wr(IE_AUDIO_SVC_FB_PUMPS_OFF, 0);
    fb_wr(IE_AUDIO_SVC_FB_MAGIC_OFF, IE_AUDIO_SVC_FB_MAGIC);

    if (resp != 0) {
        resp->status = 1;
    }
}

static void handle_pump(const ie_audio_pump_req *req, ie_audio_resp *resp) {
    uint64_t start = ie_time_usec();

    /* Hand the index range to the pump in the mailbox word packing
     * func_800CBCB0 expects: end | start<<8. */
    ie_audio_svc_cmd_word = (req->cmd_end & 0xFFu) | ((req->cmd_start & 0xFFu) << 8);
    ie_audio_svc_cmd_avail = 1;

    fb_wr(0x18u, 1); /* bring-up trace: in pump */
    game_audio_pump_voices();
    fb_wr(0x18u, 2); /* bring-up trace: pump done */

    pumps_done++;
    fb_wr(IE_AUDIO_SVC_FB_PUMPS_OFF, pumps_done);
    {
        static uint32_t cum_usec;
        uint32_t usec = (uint32_t)(ie_time_usec() - start);
        fb_wr(IE_AUDIO_SVC_FB_USEC_OFF, usec);
        cum_usec += usec;
        fb_wr(IE_AUDIO_SVC_FB_CUM_USEC_OFF, cum_usec);
    }
    if (resp != 0) {
        resp->usec = (uint32_t)(ie_time_usec() - start);
        resp->pumps = pumps_done;
        resp->status = 1;
    }
}

static void handle(uint32_t op, uint32_t req_ptr, uint32_t resp_ptr) {
    switch (op) {
    case IE_AUDIO_OP_INIT:
        handle_init((const ie_audio_init_req *)(uintptr_t) req_ptr,
                    (ie_audio_resp *)(uintptr_t) resp_ptr);
        break;
    case IE_AUDIO_OP_PUMP:
        handle_pump((const ie_audio_pump_req *)(uintptr_t) req_ptr,
                    (ie_audio_resp *)(uintptr_t) resp_ptr);
        break;
    default:
        break;
    }
}

void ie_main(void) {
    volatile uint8_t *ring = (volatile uint8_t *)(uintptr_t)IE_COPROC_M68K2_RING_BASE;
    /* Version-gate handshake: echo the layout version so the host routes work
     * here instead of failing START with COPROC_ERR_STALE_WORKER. */
    ring[IE_COPROC_RING_ACK_OFFSET] = (uint8_t)IE_COPROC_LAYOUT_VERSION;
    for (;;) {
        uint8_t tail = ring[IE_COPROC_RING_TAIL_OFFSET];
        if (tail != ring[IE_COPROC_RING_HEAD_OFFSET]) {
            uint32_t desc = IE_COPROC_M68K2_RING_BASE + IE_COPROC_RING_ENTRIES_OFFSET +
                            (uint32_t)tail * IE_COPROC_REQ_DESC_SIZE;
            uint32_t resp = IE_COPROC_M68K2_RING_BASE + IE_COPROC_RING_RESP_OFFSET +
                            (uint32_t)tail * IE_COPROC_RESP_DESC_SIZE;
            uint32_t ticket = rd32(desc + 0u);
            handle(rd32(desc + DESC_OP_OFF), rd32(desc + DESC_REQ_PTR_OFF),
                   rd32(desc + DESC_RESP_PTR_OFF));
            wr32(resp + RESP_TICKET_OFF, ticket);
            wr32(resp + RESP_STATUS_OFF, RESP_OK);
            ring[IE_COPROC_RING_TAIL_OFFSET] =
                (uint8_t)((tail + 1u) & (IE_COPROC_RING_SLOTS - 1u));
        }
    }
}
