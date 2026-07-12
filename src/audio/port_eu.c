#include <ultra64.h>
#include <macros.h>
#include <PR/ucode.h>

#include "audio/synthesis.h"
#include "audio/seqplayer.h"
#include "audio/port_eu.h"
#include "audio/load.h"
#include "audio/heap.h"
#include "audio/data.h"

s32 AosSendMesg( OSMesgQueue *mq,  OSMesg msg,  s32 flag);
s32 AosRecvMesg( OSMesgQueue *mq,  OSMesg *msg,  s32 flag);

volatile s32 gThing = 0;
volatile s32 gThingSent = 0;


OSMesgQueue D_801937C0 = {0};
OSMesgQueue D_801937D8 = {0};
OSMesgQueue D_801937F0 = {0};
OSMesgQueue D_80193808 = {0};

#ifdef IE_AUDIO_SERVICE
/* Audio service worker pass: the command ring is the MAIN image's
 * sAudioCmd array in guest RAM (the producer side stays on the main CPU).
 * Bound once at OP_AUDIO_INIT. */
struct EuAudioCmd *sAudioCmd;
/* Pending (start,end) range handed over by the service dispatch loop in
 * place of the OSMesg mailbox: end | start<<8, same packing func_800CBCB0
 * expects. */
volatile u32 ie_audio_svc_cmd_word;
volatile u32 ie_audio_svc_cmd_avail;

void ie_audio_svc_bind_cmds(void *ring) {
    sAudioCmd = (struct EuAudioCmd *) ring;
}

void ie_audio_svc_set_load_active(u32 on);
#else
struct EuAudioCmd sAudioCmd[0x100] = {0};
#define ie_audio_svc_set_load_active(on) ((void) 0)
#endif

#if defined(IE_AUDIO_SVC) && !defined(IE_AUDIO_SERVICE)
/* Same fence ie_mmio.h provides; declared locally to keep the audio TU free
 * of the IE MMIO header. */
static inline void ie_audio_svc_barrier(void) {
    __asm__ volatile("" ::: "memory");
}
#define ie_compiler_barrier ie_audio_svc_barrier
#endif

// Seems oversized by 1
OSMesg D_80194020[2] = {0};
OSMesg D_80194028[4] = {0};
OSMesg D_80194038[1] = {0};
OSMesg D_8019403C[1] = {0};

u8 D_800EA3A0[] = { 0, 0, 0, 0 };

u8 D_800EA3A4[] = { 0, 0, 0, 0 };

OSMesgQueue* D_800EA3A8 = &D_801937C0;
OSMesgQueue* D_800EA3AC = &D_801937D8;
OSMesgQueue* D_800EA3B0 = &D_801937F0;
OSMesgQueue* D_800EA3B4 = &D_80193808;

char port_eu_unused_string0[] = "DAC:Lost 1 Frame.\n";
char port_eu_unused_string1[] = "DMA: Request queue over.( %d )\n";
char port_eu_unused_string2[] = "DMA [ %d lines] TIMEOUT\n";
char port_eu_unused_string3[] = "Warning: WaveDmaQ contains %d msgs.\n";
char port_eu_unused_string4[] = "Audio:now-max tasklen is %d / %d\n";
char port_eu_unused_string5[] = "Audio:Warning:ABI Tasklist length over (%d)\n";

s32 D_800EA484 = 128;

char port_eu_unused_string6[] = "AudioSend: %d -> %d (%d)\n";

s32 D_800EA4A4 = 0;

char port_eu_unused_string7[] = "Undefined Port Command %d\n";
extern volatile s32 gPresetId;
extern volatile s32 gPresetSent;

void ie_audio_voice_frame_begin(void);
void ie_audio_voice_update(s32 updateIndex);
void ie_audio_voice_reset_all(void);

void game_audio_pump_voices(void) {
    OSMesg msg = {0};
    s32 i = 0;
    static u8 reset_seen;

    if (gAudioFrameCount == 0) {
        gAudioRandom = osGetTime();
    }

    gAudioFrameCount++;
    gCurrAudioFrameDmaCount = 0;

    if (gAudioResetStatus != 0) {
        if (!reset_seen) {
            ie_audio_voice_reset_all();
            reset_seen = 1;
        }
        if (audio_shut_down_and_reset_step() == 0) {
            return;
        }
    } else {
        reset_seen = 0;
    }

#ifdef IE_AUDIO_SERVICE
    /* Service worker: the (start,end) index range arrives in the
     * OP_AUDIO_PUMP request instead of the OSMesg mailbox (P2.0 audit
     * condition 1 - the ultra_reimpl queue is not cross-CPU safe). */
    (void) msg;
    if (ie_audio_svc_cmd_avail != 0) {
        ie_audio_svc_cmd_avail = 0;
        func_800CBCB0(ie_audio_svc_cmd_word);
    }
#else
    if (AosRecvMesg(D_800EA3AC, &msg, 0) != -1) {
        func_800CBCB0((u32)msg);
    }
#endif

    ie_audio_voice_frame_begin();
    for (i = gAudioBufferParameters.updatesPerFrame; i > 0; i--) {
        s32 updateIndex = gAudioBufferParameters.updatesPerFrame - i;
        process_sequences(i - 1);
        synthesis_load_note_subs_eu(updateIndex);
        ie_audio_voice_update(updateIndex);
    }

    gAudioRandom = osGetCount() * (gAudioRandom + gAudioFrameCount);
}

void eu_process_audio_cmd(struct EuAudioCmd* cmd) {
    s32 i = 0;

    switch (cmd->u.s.op) {
        case 0x81:
            //printf("preload %08x\n", cmd->u.s.arg2);
            ie_audio_svc_set_load_active(1);
            preload_sequence(cmd->u.s.arg2, 3);
            ie_audio_svc_set_load_active(0);
            break;

        case 0x82:
        case 0x88:
            //printf("load_sequence %08x %08x\n", cmd->u.s.bankId, cmd->u.s.arg2);
            ie_audio_svc_set_load_active(1);
            load_sequence(cmd->u.s.bankId, cmd->u.s.arg2);
            //printf("func_800CBA64 %08x %08x\n", cmd->u.s.bankId, cmd->u2.as_s32);
            func_800CBA64(cmd->u.s.bankId, cmd->u2.as_s32);
            ie_audio_svc_set_load_active(0);
            break;

        case 0x83:
            if (gSequencePlayers[cmd->u.s.bankId].enabled != 0) {
                if (cmd->u2.as_s32 == 0) {
                    //printf("sequence player disable %08x\n", &gSequencePlayers[cmd->u.s.bankId]);
#ifdef AUDIO_LOAD_TRACE
                    printf("cmd disable p%d\n", cmd->u.s.bankId);
#endif
                    sequence_player_disable(&gSequencePlayers[cmd->u.s.bankId]);
                } else {
                    //printf("seq_player_fade_to_zero_volume %08x %08x\n", cmd->u.s.bankId, cmd->u2.as_s32);
                    seq_player_fade_to_zero_volume(cmd->u.s.bankId, cmd->u2.as_s32);
                }
            }
            break;

        case 0xf0:
            gAudioLibSoundMode = cmd->u2.as_s32;
            break;

        case 0xf1:
            for (i = 0; i < 4; i++) {
                gSequencePlayers[i].muted = 1;
                gSequencePlayers[i].recalculateVolume = 1;
            }
            break;

        case 0xf2:
            for (i = 0; i < 4; i++) {
                gSequencePlayers[i].muted = 0;
                gSequencePlayers[i].recalculateVolume = 1;
            }
            break;
        case 0xF3:
            //printf("func_800BB388(%08x,%08x,%08x)\n", cmd->u.s.bankId, cmd->u.s.arg2, cmd->u.s.arg3);
            func_800BB388(cmd->u.s.bankId, cmd->u.s.arg2, cmd->u.s.arg3);
            break;
    }
}

void seq_player_fade_to_zero_volume(s32 arg0, s32 fadeOutTime) {
    struct SequencePlayer* player = NULL;

    if (fadeOutTime == 0) {
        fadeOutTime = 1;
    }
    player = &gSequencePlayers[arg0];
    player->state = 2;
    player->fadeRemainingFrames = fadeOutTime;
    player->fadeVelocity = -(player->fadeVolume / fadeOutTime);
}

void func_800CBA64(s32 playerIndex, s32 fadeInTime) {
    struct SequencePlayer* player = NULL;

    if (fadeInTime != 0) {
        player = &gSequencePlayers[playerIndex];
        player->state = 1;
        player->fadeTimerUnkEu = fadeInTime;
        player->fadeRemainingFrames = fadeInTime;
        player->fadeVolume = 0.0f;
        player->fadeVelocity = 0.0f;
    }
}

void port_eu_init_queues(void) {
    D_800EA3A0[0] = 0;
    D_800EA3A4[0] = 0;
    osCreateMesgQueue(D_800EA3A8, D_80194020, 1);
    osCreateMesgQueue(D_800EA3AC, D_80194028, 4);
    osCreateMesgQueue(D_800EA3B0, D_80194038, 1);
    osCreateMesgQueue(D_800EA3B4, D_8019403C, 1);
}

void func_800CBB48(s32 arg0, s32* arg1) {
    struct EuAudioCmd* cmd = &sAudioCmd[D_800EA3A0[0] & 0xff];
    //printf("%s(%d, %d)\n", __func__, arg0, *arg1);
    //printf("arg0: %08x\n", arg0);
    cmd->u.first = arg0;
    //printf("op: %02x\n", cmd->u.s.op);
    cmd->u2.as_u32 = *arg1;
#if defined(IE_AUDIO_SVC) && !defined(IE_AUDIO_SERVICE)
    /* The worker consumes this ring cross-CPU: the slot must be fully
     * written before the producer index moves (P2.0 audit condition 2). */
    ie_compiler_barrier();
#endif
    D_800EA3A0[0]++;
}

void func_800CBB88(u32 arg0, f32 arg1) {
    //printf("%s(%u, %f)\n", __func__, arg0, arg1);
    func_800CBB48(arg0, (s32*) &arg1);
   // struct EuAudioCmd* cmd = &sAudioCmd[D_800EA3A0[0] & 0xff];
    //cmd->u.first = arg0;
    //cmd->u2.as_f32 = arg1;
    //D_800EA3A0[0]++;
}

void func_800CBBB8(u32 arg0, u32 arg1) {
    //printf("%s(%u, %u)\n", __func__, arg0, arg1);
    func_800CBB48(arg0, (s32*) &arg1);
}

void func_800CBBE8(u32 arg0, s8 arg1) {
    s32 sp34 = arg1 << 24;
    func_800CBB48(arg0, &sp34);
}


//! @todo clenanup, something's weird with the variables. D_800EA4A4 is probably EuAudioCmd bc of the + 0x100
void func_800CBC24(void) {
    s32 temp_t6 = 0;
    s32 test = 0;
    OSMesg thing = {0};

    temp_t6 = D_800EA3A0[0] - D_800EA3A4[0];
    test = (u8) temp_t6;

    test = (test + 0x100) & 0xFF;
    if (D_800EA4A4 < test) {
        D_800EA4A4 = test;
    }
#if defined(IE_AUDIO_SVC) || defined(IE_AUDIO_SERVICE)
    /* Service mode: no mailbox send - the client samples the producer
     * index directly when it enqueues OP_AUDIO_PUMP. Keep the high-water
     * bookkeeping above and the consumed-baseline advance below. (The
     * worker pass never calls this; gated the same to avoid linking the
     * mailbox reimpl there.) */
    (void) thing;
#else
    thing = (OSMesg) ((uint32_t)(D_800EA3A0[0] & 0xFF) | ((uint32_t)(D_800EA3A4[0] & 0xFF) << 8));
    AosSendMesg(D_800EA3AC, thing, 0);
#endif
    D_800EA3A4[0] = D_800EA3A0[0];
}


void func_800CBCB0(u32 arg0) {
    struct EuAudioCmd* cmd = NULL;
    struct SequencePlayer* seqPlayer = NULL;
    struct SequenceChannel* chan = NULL;
    u8 end = arg0 & 0xff;
    u8 i = (arg0 >> 8) & 0xff;

    for (;;) {
        if (i == end) {
            break;
        }
        cmd = &sAudioCmd[i++ & 0xff];
        //printf("cmd is %08x\n\top is %02x\n", cmd, cmd->u.s.op);

        if ((cmd->u.s.op & 0xf0) == 0xf0) {
            eu_process_audio_cmd(cmd);
            goto why;
        }

        if (cmd->u.s.bankId < SEQUENCE_PLAYERS) {
            float tmpfloat;
            seqPlayer = &gSequencePlayers[cmd->u.s.bankId];
            if ((cmd->u.s.op & 0x80) != 0) {
                eu_process_audio_cmd(cmd);
            } else if ((cmd->u.s.op & 0x40) != 0) {
                switch (cmd->u.s.op) {
                    case 0x41:
                        seqPlayer->fadeVolumeScale = cmd->u2.as_f32;

                        seqPlayer->recalculateVolume = 1;
                        break;

                    case 0x47:
                        seqPlayer->tempo = cmd->u2.as_s32 * TATUMS_PER_BEAT;
                        break;

                    case 0x48:
                        seqPlayer->transposition = cmd->u2.as_s8;
                        break;

                    case 0x46:
                        seqPlayer->seqVariationEu[cmd->u.s.arg3] = cmd->u2.as_s8;
                        break;
                    default:
                        printf("unhandled op %02x\n", cmd->u.s.op);
                        break;
                }
            } else if (seqPlayer->enabled != 0 && cmd->u.s.arg2 < 0x10) {
                chan = seqPlayer->channels[cmd->u.s.arg2];
                if (IS_SEQUENCE_CHANNEL_VALID(chan)) {
                    switch (cmd->u.s.op) {
                        case 1:
                            chan->volumeScale = cmd->u2.as_f32;
                            chan->changes.as_bitfields.volume = 1;
                            break;
                        case 2:
                            chan->volume = cmd->u2.as_f32;
                            chan->changes.as_bitfields.volume = 1;
                            break;
                        case 3:
                            chan->newPan = cmd->u2.as_s8;
                            chan->changes.as_bitfields.pan = 1;
                            break;
                        case 4:
                            chan->freqScale = cmd->u2.as_f32;
                            chan->changes.as_bitfields.freqScale = 1;
                            break;
                        case 5:
                            chan->reverbVol = cmd->u2.as_s8;
                            break;
                        case 6:
                            if (cmd->u.s.arg3 < 8) {
#ifdef AUDIO_LOAD_TRACE
                                {
                                    static int iolog;
                                    if (iolog < 20 && cmd->u2.as_s8 != -1) {
                                        iolog++;
                                        printf("chan io write ch %d io[%d]=%d\n",
                                               cmd->u.s.arg2, cmd->u.s.arg3, cmd->u2.as_s8);
                                    }
                                }
#endif
                                chan->soundScriptIO[cmd->u.s.arg3] = cmd->u2.as_s8;
                            }
                            break;
                        case 8:
                            chan->stopSomething2 = cmd->u2.as_s8;
                    }
                }
            }
        }

    why:
        cmd->u.s.op = 0;
    }
}

void port_eu_init() {
    port_eu_init_queues();
}
