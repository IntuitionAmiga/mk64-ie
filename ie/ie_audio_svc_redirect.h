/*
 * ie/ie_audio_svc_redirect.h - main-CPU view of worker-owned sequencer state
 * (IE_AUDIO_SVC main build only; the worker pass defines IE_AUDIO_SERVICE
 * and must never include this).
 *
 * With the audio service active the authoritative gSequencePlayers array and
 * the gSequenceChannelNone sentinel live in the second M68K worker's RAM
 * window. The client resolves their addresses from the worker's feedback
 * block at init and the few producer-side readers (P2.0 audit condition 3)
 * are redirected here. Channel-pointer dereferences additionally fail closed
 * while the worker reports a sequence load in progress - the pointers are
 * transition-time unstable by design and the readers tolerate a skipped
 * frame.
 */
#ifndef IE_AUDIO_SVC_REDIRECT_H
#define IE_AUDIO_SVC_REDIRECT_H

#if defined(IE_AUDIO_SVC) && !defined(IE_AUDIO_SERVICE)

struct SequencePlayer;

extern struct SequencePlayer *gIeAudioSvcSeqp;  /* worker &gSequencePlayers[0] */
extern uintptr_t gIeAudioSvcChanNone;           /* worker &gSequenceChannelNone */
u32 ie_audio_svc_load_active(void);
int ie_audio_svc_active(void);

#define gSequencePlayers gIeAudioSvcSeqp
#define gSequenceChannelNone (*(struct SequenceChannel *)(void *) gIeAudioSvcChanNone)

#undef IS_SEQUENCE_CHANNEL_VALID
#define IS_SEQUENCE_CHANNEL_VALID(ptr)                        \
    (!ie_audio_svc_load_active() && (ptr) != NULL &&          \
     (uintptr_t)(ptr) != (uintptr_t) &gSequenceChannelNone)

#endif /* IE_AUDIO_SVC && !IE_AUDIO_SERVICE */

#endif /* IE_AUDIO_SVC_REDIRECT_H */
