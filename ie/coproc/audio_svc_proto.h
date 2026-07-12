/*
 * ie/coproc/audio_svc_proto.h - ABI between the main CPU and the M68K audio
 * service worker (second M68K instance, ring index 6).
 *
 * Both sides are big-endian M68K, so request/response blocks are plain
 * native structs in guest RAM passed by pointer; only the ring descriptors
 * are little-endian (see gfx_svc_proto.h).
 *
 * The seam is the existing EU audio command queue: the main CPU keeps
 * producing packed EuAudioCmd entries
 * into its sAudioCmd ring and, instead of the OSMesg mailbox, passes the
 * (start, end) producer indices in each OP_AUDIO_PUMP request. The worker
 * owns the whole sequencer: audio_init, the reset state machine, sequence
 * loads and the voice MMIO updates.
 */
#ifndef IE_COPROC_AUDIO_SVC_PROTO_H
#define IE_COPROC_AUDIO_SVC_PROTO_H

#include <stdint.h>

#define IE_AUDIO_OP_INIT 0x41494E49u /* 'AINI' */
#define IE_AUDIO_OP_PUMP 0x41504D50u /* 'APMP' */

typedef struct ie_audio_init_req {
    /* Audio ROM segments loaded by the main CPU (guest RAM, big-endian). */
    uint32_t banks_rom;
    uint32_t tables_rom;
    uint32_t tables_pcm16_rom;
    uint32_t instrument_sets_rom;
    uint32_t sequences_rom;
    /* Main image's sAudioCmd[0x100] command ring (shared, main produces). */
    uint32_t cmd_ring;
} ie_audio_init_req;

typedef struct ie_audio_pump_req {
    /* Producer index range to consume this pump: sAudioCmd slots
     * [cmd_start, cmd_end) modulo 256, u8 index semantics. */
    uint32_t cmd_start;
    uint32_t cmd_end;
} ie_audio_pump_req;

typedef struct ie_audio_resp {
    uint32_t usec;   /* worker time for this op        */
    uint32_t pumps;  /* pumps completed since INIT     */
    uint32_t status; /* 1 = ok                         */
} ie_audio_resp;

/*
 * Feedback block, published by the worker into main RAM (fixed address next
 * to the gfx service trace block at 0x9EF80). The main CPU's few sequencer
 * readers (P2.0 audit condition 3) resolve worker-side state through it.
 *
 *   +0x00 magic/ready   'ASVC' once INIT (including the reset drain) is done
 *   +0x04 seq_players   worker &gSequencePlayers[0]
 *   +0x08 chan_none     worker &gSequenceChannelNone (channel-valid sentinel)
 *   +0x0C load_active   nonzero while a pump is executing sequence loads;
 *                       channel-pointer derefs on the main CPU must fail
 *                       closed while set
 *   +0x10 pumps         pumps completed (progress/liveness)
 *   +0x14 last_usec     duration of the last pump
 *   +0x18 phase         bring-up trace
 *   +0x1C cum_usec      cumulative pump usec - two reads give a span
 *                       average without per-pump debugger polling
 */
#define IE_AUDIO_SVC_FEEDBACK_BASE  0x0009EFC0u
#define IE_AUDIO_SVC_FB_MAGIC_OFF   0x00u
#define IE_AUDIO_SVC_FB_SEQP_OFF    0x04u
#define IE_AUDIO_SVC_FB_CHAN0_OFF   0x08u
#define IE_AUDIO_SVC_FB_LOAD_OFF    0x0Cu
#define IE_AUDIO_SVC_FB_PUMPS_OFF   0x10u
#define IE_AUDIO_SVC_FB_USEC_OFF    0x14u
#define IE_AUDIO_SVC_FB_CUM_USEC_OFF 0x1Cu
#define IE_AUDIO_SVC_FB_MAGIC       0x41535643u /* 'ASVC' */

#endif /* IE_COPROC_AUDIO_SVC_PROTO_H */
