/*
 * ie/coproc/gfx_svc_proto.h - frame ABI between the main CPU and the M68K
 * gfx service worker (ring index 2).
 *
 * Both sides are big-endian M68K, so request/response blocks are plain
 * native structs in guest RAM passed by pointer. Only the ring DESCRIPTORS
 * live in the mailbox, which the worker reads unswapped (little-endian, as
 * the manager writes them) - the client stores those fields with explicit
 * LE byte writes, mirroring ie_coproc.c's ring-5 protocol.
 */
#ifndef IE_COPROC_GFX_SVC_PROTO_H
#define IE_COPROC_GFX_SVC_PROTO_H

#include <stdint.h>

#define IE_GFX_OP_INIT  0x47494E49u /* 'GINI' */
#define IE_GFX_OP_FRAME 0x4746524Du /* 'GFRM' */
#define IE_GFX_OP_RESET 0x47525354u /* 'GRST' */

/* Course loads invalidate whole decompressed regions one texture at a time;
 * the worst frame observed carries menu + course churn. Overflow is a
 * platform_fatal on the producer side, not silent truncation. */
#define IE_GFX_INVAL_MAX 512u

typedef struct ie_gfx_frame_req {
    void *dl;                       /* spTask data_ptr                     */
    uintptr_t segment_table[16];    /* gSegmentTable snapshot              */
    uint32_t is_mirror;             /* gIsMirrorMode                       */
    void *const *invalidate_list;   /* texture invalidations since last frame */
    uint32_t invalidate_count;
    uint32_t flags;                 /* reserved                            */
} ie_gfx_frame_req;

typedef struct ie_gfx_frame_resp {
    uint32_t usec;                  /* worker time for this op             */
    uint32_t frames;                /* frames translated since INIT        */
    uint32_t status;                /* 1 = ok                              */
} ie_gfx_frame_resp;

#endif /* IE_COPROC_GFX_SVC_PROTO_H */
