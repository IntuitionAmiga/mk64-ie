; tnl_service_ie64.asm - IE64 coprocessor T&L service for mk64-ie.
;
; Native IE64 port of tnl_service.c: polls the ring-5 mailbox for request
; descriptors from the M68K host, dispatches on the op field, and advances
; the ring tail. OP_TNL_BATCH implements gfx_sp_vertex (unlit and lit,
; including spherical texgen) over a whole vertex batch: reads Vtx[] and
; writes F3DLoadedVertex[] directly in guest RAM on the shared bus.
;
; Protocol: ie/coproc/tnl_proto.h. ALL request payload fields are BIG-ENDIAN
; (M68K native); this little-endian service byte-swaps with native bswap.l.
; Descriptors use the engine-standard ring layout (LE), same as the caller
; in ie/coproc/ie_coproc.c writes.
;
; The IE64 FPU is scalar IEEE-754 single precision - the same float32 the
; M68K reference path computes with, so per-vertex results track the local
; gfx_sp_vertex closely (FCVTFI truncates toward zero, matching C casts).
;
; Assemble: ie64asm -o tnl.ie64 tnl_service_ie64.asm

; --- Worker / ring constants ---
WORKER_BASE       equ 0x3A0000                  ; IE64 worker region base
MAILBOX_BASE      equ 0x790000
RING_STRIDE       equ 0x400
RING_INDEX        equ 10                        ; IE64 = cpuTypeIndex 5 * 2 + inst 0
RING_BASE         equ MAILBOX_BASE + (RING_INDEX * RING_STRIDE)

RING_HEAD         equ 0x00
RING_TAIL         equ 0x01
RING_ACK          equ 0x04                      ; version-gate ack byte
LAYOUT_VER        equ 1                         ; COPROC_LAYOUT_VERSION
RING_ENTRIES      equ 0x08
RING_RESPONSES    equ 0x208
REQ_SIZE          equ 32
RESP_SIZE         equ 16

; Request descriptor offsets (engine-standard)
REQ_TICKET        equ 0x00
REQ_OP            equ 0x08
REQ_REQPTR        equ 0x10
REQ_RESPPTR       equ 0x18

; Response descriptor offsets
RESP_TICKET       equ 0x00
RESP_STATUS       equ 0x04
RESP_RESULT      equ 0x08
RESP_LEN          equ 0x0C

STATUS_OK         equ 2
STATUS_ERROR      equ 3

; Ops (tnl_proto.h)
OP_ADD            equ 1
OP_TNL_XFORM      equ 2
OP_TNL_BATCH      equ 3

; OP_TNL_BATCH request header offsets (payload is BE)
TREQ_MATRIX       equ 0                         ; float[16] row*4+col
TREQ_TEXSCALE_S   equ 64
TREQ_TEXSCALE_T   equ 68
TREQ_NVERTS       equ 72
TREQ_VERTS_PTR    equ 76
TREQ_OUT_PTR      equ 80
TREQ_GEOMODE      equ 84
TREQ_NUMLIGHTS    equ 88
TREQ_AMB_COL      equ 92                        ; u32[3]
TREQ_DIR_COL      equ 104                       ; u32[3]
TREQ_LIGHTCOEF    equ 116                       ; float[3]
TREQ_LOOKAT       equ 128                       ; float[6]

G_LIGHTING        equ 0x00020000
G_TEXTURE_GEN     equ 0x00040000

; Vtx (16B): ob s16[3]@0, tc s16[2]@8, cn u8[4]@12
VTX_SIZE          equ 16
VTX_OB            equ 0
VTX_TC            equ 8
VTX_CN            equ 12

; F3DLoadedVertex (48B): obj f32[4]@0, clip f32[4]@16, uv f32[2]@32,
; rgba u8[4]@40, clip_rej u8@44, wlt0 u8@45
LV_SIZE           equ 48
LV_X              equ 0
LV_TX             equ 16
LV_U              equ 32
LV_COLOR          equ 40
LV_CLIP_REJ       equ 44

; Scratch block (in worker RAM, above the code): batch header converted to
; native LE once per batch so the vertex loop uses plain fload/loads.
SCRATCH           equ WORKER_BASE + 0x40000     ; 0x3E0000
SCR_M             equ 0                         ; float[16]
SCR_LC            equ 64                        ; float[3] light coeffs
SCR_LA            equ 76                        ; float[6] lookat coeffs
SCR_FSS           equ 100                       ; float(ss) for texgen
SCR_FST           equ 104                       ; float(st)

; Diagnostics (shared with the probe tooling; LE u32, plain data)
DIAG_CONSUMED     equ 0x0009EF14
DIAG_KEXIT        equ 0x0009EF2C

; --- Entry ---
    org WORKER_BASE

    la r30, RING_BASE
    la r27, SCRATCH

    ; Version-gate handshake: echo the layout version so the host routes work
    ; here instead of failing START with COPROC_ERR_STALE_WORKER.
    move.l r2, #LAYOUT_VER
    store.b r2, RING_ACK(r30)

; --- Poll loop ---
poll_loop:
    load.b r2, RING_HEAD(r30)
    load.b r3, RING_TAIL(r30)
    beq r2, r3, poll_loop

    ; Request descriptor at ring_base + RING_ENTRIES + tail*32 (LE fields)
    mulu.l r4, r3, #REQ_SIZE
    add.l r4, r4, #RING_ENTRIES
    add.l r5, r30, r4
    load.l r10, REQ_TICKET(r5)
    load.l r11, REQ_OP(r5)
    load.l r12, REQ_REQPTR(r5)
    load.l r14, REQ_RESPPTR(r5)

    ; Response descriptor at ring_base + RING_RESPONSES + tail*16
    mulu.l r6, r3, #RESP_SIZE
    add.l r6, r6, #RING_RESPONSES
    add.l r20, r30, r6
    store.l r10, RESP_TICKET(r20)

    ; diag: consumed++
    la r7, DIAG_CONSUMED
    load.l r6, 0(r7)
    add.l r6, r6, #1
    store.l r6, 0(r7)

    move.l r8, #OP_ADD
    beq r11, r8, op_add
    move.l r8, #OP_TNL_XFORM
    beq r11, r8, op_xform
    move.l r8, #OP_TNL_BATCH
    beq r11, r8, op_batch
    bra op_error

; --- OP_ADD: respPtr[0] = reqPtr[0] + reqPtr[4] (LE u32, plumbing proof) ---
op_add:
    load.l r6, 0(r12)
    load.l r7, 4(r12)
    add.l r6, r6, r7
    store.l r6, 0(r14)
    bra op_done_ok

; --- OP_TNL_XFORM: single matrix*vec4, BE floats in and out ---
; reqPtr: m[16] @0, x @64, y @68, z @72; respPtr: out[4]
op_xform:
    ; x,y,z -> f8,f9,f10
    load.l r6, 64(r12)
    bswap.l r6, r6
    fmovi f8, r6
    load.l r6, 68(r12)
    bswap.l r6, r6
    fmovi f9, r6
    load.l r6, 72(r12)
    bswap.l r6, r6
    fmovi f10, r6

    ; out[c] = x*m[c] + y*m[4+c] + z*m[8+c] + m[12+c], c = 0..3
    move.l r24, r0                        ; c = 0
xform_col:
    move.l r7, #4
    bge r24, r7, op_done_ok
    lsl.l r6, r24, #2                     ; c*4 bytes
    add.l r7, r12, r6                     ; &m[c]

    load.l r8, 0(r7)                      ; m[c] (BE)
    bswap.l r8, r8
    fmovi f0, r8
    fmul f4, f8, f0                       ; x*m[c]
    load.l r8, 16(r7)                     ; m[4+c]
    bswap.l r8, r8
    fmovi f0, r8
    fmul f1, f9, f0
    fadd f4, f4, f1
    load.l r8, 32(r7)                     ; m[8+c]
    bswap.l r8, r8
    fmovi f0, r8
    fmul f1, f10, f0
    fadd f4, f4, f1
    load.l r8, 48(r7)                     ; m[12+c]
    bswap.l r8, r8
    fmovi f0, r8
    fadd f4, f4, f0

    fmovo r8, f4
    bswap.l r8, r8
    add.l r7, r14, r6
    store.l r8, 0(r7)

    add.l r24, r24, #1
    bra xform_col

; --- OP_TNL_BATCH: gfx_sp_vertex over n vertices ---
op_batch:
    ; Header parse: BE -> LE/native. Matrix + coeffs into SCRATCH floats.
    move.l r24, r0                        ; i = 0
batch_copy_m:
    move.l r7, #16
    bge r24, r7, batch_copy_lc
    lsl.l r6, r24, #2
    add.l r7, r12, r6
    load.l r8, TREQ_MATRIX(r7)
    bswap.l r8, r8
    add.l r7, r27, r6
    store.l r8, SCR_M(r7)
    add.l r24, r24, #1
    bra batch_copy_m

batch_copy_lc:
    move.l r24, r0
batch_copy_lc_loop:
    move.l r7, #3
    bge r24, r7, batch_copy_la
    lsl.l r6, r24, #2
    add.l r7, r12, r6
    load.l r8, TREQ_LIGHTCOEF(r7)
    bswap.l r8, r8
    add.l r7, r27, r6
    store.l r8, SCR_LC(r7)
    add.l r24, r24, #1
    bra batch_copy_lc_loop

batch_copy_la:
    move.l r24, r0
batch_copy_la_loop:
    move.l r7, #6
    bge r24, r7, batch_header
    lsl.l r6, r24, #2
    add.l r7, r12, r6
    load.l r8, TREQ_LOOKAT(r7)
    bswap.l r8, r8
    add.l r7, r27, r6
    store.l r8, SCR_LA(r7)
    add.l r24, r24, #1
    bra batch_copy_la_loop

batch_header:
    ; ss/st (u16 in BE u32) -> r8/r9; float versions -> scratch for texgen
    load.l r8, TREQ_TEXSCALE_S(r12)
    bswap.l r8, r8
    move.l r6, #0xFFFF
    and.l r8, r8, r6
    fcvtif f0, r8
    fstore f0, SCR_FSS(r27)
    load.l r9, TREQ_TEXSCALE_T(r12)
    bswap.l r9, r9
    and.l r9, r9, r6
    fcvtif f0, r9
    fstore f0, SCR_FST(r27)

    load.l r23, TREQ_NVERTS(r12)          ; n
    bswap.l r23, r23
    load.l r21, TREQ_VERTS_PTR(r12)       ; vp
    bswap.l r21, r21
    load.l r22, TREQ_OUT_PTR(r12)         ; out
    bswap.l r22, r22
    load.l r25, TREQ_GEOMODE(r12)         ; geo
    bswap.l r25, r25
    load.l r28, TREQ_NUMLIGHTS(r12)       ; nl
    bswap.l r28, r28

    ; amb r13,r15,r16; dir r17,r18,r19
    load.l r13, TREQ_AMB_COL(r12)
    bswap.l r13, r13
    load.l r15, 96(r12)                   ; TREQ_AMB_COL+4
    bswap.l r15, r15
    load.l r16, 100(r12)                  ; TREQ_AMB_COL+8
    bswap.l r16, r16
    load.l r17, TREQ_DIR_COL(r12)
    bswap.l r17, r17
    load.l r18, 108(r12)                  ; TREQ_DIR_COL+4
    bswap.l r18, r18
    load.l r19, 112(r12)                  ; TREQ_DIR_COL+8
    bswap.l r19, r19

    ; f11 = 1/127, f12 = 0.25, f13 = 0.0 (batch constants)
    move.l r6, #0x3C010204                ; 1.0f/127.0f bits
    fmovi f11, r6
    move.l r6, #0x3E800000                ; 0.25f bits
    fmovi f12, r6
    fmovi f13, r0                         ; 0.0f

    move.l r24, r0                        ; k = 0
batch_vert:
    bge r24, r23, batch_done

    ; v = vp + k*16 -> r1 ; o = out + k*48 -> r2
    lsl.l r6, r24, #4
    add.l r1, r21, r6
    mulu.l r6, r24, #LV_SIZE
    add.l r2, r22, r6

    ; --- object coords: BE s16 -> float x,y,z (f8,f9,f10) ---
    load.b r6, VTX_OB(r1)
    lsl.l r6, r6, #8
    load.b r7, 1(r1)
    or.l r6, r6, r7
    sext.w r6, r6
    fcvtif f8, r6
    load.b r6, 2(r1)
    lsl.l r6, r6, #8
    load.b r7, 3(r1)
    or.l r6, r6, r7
    sext.w r6, r6
    fcvtif f9, r6
    load.b r6, 4(r1)
    lsl.l r6, r6, #8
    load.b r7, 5(r1)
    or.l r6, r6, r7
    sext.w r6, r6
    fcvtif f10, r6

    ; --- clip transform: t[c] = x*m[c] + y*m[4+c] + z*m[8+c] + m[12+c] ---
    ; column c=0 -> f4 (tx)
    fload f0, SCR_M+0(r27)
    fmul f4, f8, f0
    fload f0, SCR_M+16(r27)
    fmul f1, f9, f0
    fadd f4, f4, f1
    fload f0, SCR_M+32(r27)
    fmul f1, f10, f0
    fadd f4, f4, f1
    fload f0, SCR_M+48(r27)
    fadd f4, f4, f0
    ; c=1 -> f5 (ty)
    fload f0, SCR_M+4(r27)
    fmul f5, f8, f0
    fload f0, SCR_M+20(r27)
    fmul f1, f9, f0
    fadd f5, f5, f1
    fload f0, SCR_M+36(r27)
    fmul f1, f10, f0
    fadd f5, f5, f1
    fload f0, SCR_M+52(r27)
    fadd f5, f5, f0
    ; c=2 -> f6 (tz)
    fload f0, SCR_M+8(r27)
    fmul f6, f8, f0
    fload f0, SCR_M+24(r27)
    fmul f1, f9, f0
    fadd f6, f6, f1
    fload f0, SCR_M+40(r27)
    fmul f1, f10, f0
    fadd f6, f6, f1
    fload f0, SCR_M+56(r27)
    fadd f6, f6, f0
    ; c=3 -> f7 (tw)
    fload f0, SCR_M+12(r27)
    fmul f7, f8, f0
    fload f0, SCR_M+28(r27)
    fmul f1, f9, f0
    fadd f7, f7, f1
    fload f0, SCR_M+44(r27)
    fmul f1, f10, f0
    fadd f7, f7, f1
    fload f0, SCR_M+60(r27)
    fadd f7, f7, f0

    ; --- texcoords: u = (s16(tc.s) * ss) >> 16, v likewise (int domain) ---
    load.b r6, VTX_TC(r1)
    lsl.l r6, r6, #8
    load.b r7, 9(r1)
    or.l r6, r6, r7
    sext.w r6, r6
    muls.l r6, r6, r8
    asr.l r6, r6, #16                     ; u -> r6 (kept)
    load.b r7, 10(r1)
    lsl.l r7, r7, #8
    load.b r4, 11(r1)
    or.l r7, r7, r4
    sext.w r7, r7
    muls.l r7, r7, r9
    asr.l r7, r7, #16                     ; v -> r7 (kept)

    ; --- colour bytes b0..b3 -> r4 (packed LE r,g,b,a builds here) ---
    ; unlit: rgba = cn bytes verbatim
    move.l r29, r0                        ; r29 = packed colour accumulator
    move.l r5, #G_LIGHTING
    and.l r5, r25, r5
    beq r5, r0, col_unlit

    ; --- lit: n s8 -> nx,ny,nz floats (f0,f1,f2 -> then dots) ---
    load.b r4, VTX_CN(r1)
    sext.b r4, r4
    fcvtif f14, r4                        ; nx
    load.b r4, 13(r1)
    sext.b r4, r4
    fcvtif f15, r4                        ; ny
    load.b r4, 14(r1)
    sext.b r4, r4
    fcvtif f3, r4                         ; nz

    ; r,g,b start at ambient
    move.l r5, r13                        ; r
    move.l r26, r15                       ; g
    move.l r11, r16                       ; b  (r11 dead until dispatch)

    move.l r4, #2
    bne r28, r4, lit_clamp                ; nl != 2 -> ambient only

    ; intensity = recip127 * (nx*lc0 + ny*lc1 + nz*lc2)
    fload f0, SCR_LC+0(r27)
    fmul f0, f14, f0
    fload f1, SCR_LC+4(r27)
    fmul f1, f15, f1
    fadd f0, f0, f1
    fload f1, SCR_LC+8(r27)
    fmul f1, f3, f1
    fadd f0, f0, f1
    fmul f0, f0, f11                      ; intensity

    fcmp r4, f0, f13
    move.l r10, #1
    bne r4, r10, lit_clamp                ; intensity <= 0 -> skip

    ; r += (int)(intensity * dir); truncation matches C casts
    fcvtif f1, r17
    fmul f1, f1, f0
    fcvtfi r4, f1
    add.l r5, r5, r4
    fcvtif f1, r18
    fmul f1, f1, f0
    fcvtfi r4, f1
    add.l r26, r26, r4
    fcvtif f1, r19
    fmul f1, f1, f0
    fcvtfi r4, f1
    add.l r11, r11, r4

lit_clamp:
    ; clamp to 255 and pack r|g<<8|b<<16|a<<24
    move.l r4, #255
    blt r5, r4, lit_r_ok
    move.l r5, #255
lit_r_ok:
    blt r26, r4, lit_g_ok
    move.l r26, #255
lit_g_ok:
    blt r11, r4, lit_b_ok
    move.l r11, #255
lit_b_ok:
    move.l r29, r5
    lsl.l r4, r26, #8
    or.l r29, r29, r4
    lsl.l r4, r11, #16
    or.l r29, r29, r4
    load.b r4, 15(r1)                     ; alpha
    lsl.l r4, r4, #24
    or.l r29, r29, r4

    ; --- texgen (spherical): overrides u,v ---
    move.l r5, #G_TEXTURE_GEN
    and.l r5, r25, r5
    beq r5, r0, col_done

    ; dotx = recip127 * (nx*la0 + ny*la1 + nz*la2), clamp +-1, *0.25+0.25
    fload f0, SCR_LA+0(r27)
    fmul f0, f14, f0
    fload f1, SCR_LA+4(r27)
    fmul f1, f15, f1
    fadd f0, f0, f1
    fload f1, SCR_LA+8(r27)
    fmul f1, f3, f1
    fadd f0, f0, f1
    fmul f0, f0, f11
    ; clamp f0 to [-1, 1] via constants in f1
    move.l r4, #0x3F800000                ; 1.0f
    fmovi f1, r4
    fcmp r4, f0, f1
    move.l r5, #1
    bne r4, r5, texgen_x_hi_ok
    fmov f0, f1
texgen_x_hi_ok:
    fneg f1, f1
    fcmp r4, f0, f1
    move.l r5, #-1
    bne r4, r5, texgen_x_lo_ok
    fmov f0, f1
texgen_x_lo_ok:
    fmul f0, f0, f12
    fadd f0, f0, f12                      ; dotx*0.25+0.25
    fload f1, SCR_FSS(r27)
    fmul f0, f0, f1
    fcvtfi r6, f0                         ; u = (int)(dotx * ss)

    ; doty likewise with la[3..5] and st
    fload f0, SCR_LA+12(r27)
    fmul f0, f14, f0
    fload f1, SCR_LA+16(r27)
    fmul f1, f15, f1
    fadd f0, f0, f1
    fload f1, SCR_LA+20(r27)
    fmul f1, f3, f1
    fadd f0, f0, f1
    fmul f0, f0, f11
    move.l r4, #0x3F800000
    fmovi f1, r4
    fcmp r4, f0, f1
    move.l r5, #1
    bne r4, r5, texgen_y_hi_ok
    fmov f0, f1
texgen_y_hi_ok:
    fneg f1, f1
    fcmp r4, f0, f1
    move.l r5, #-1
    bne r4, r5, texgen_y_lo_ok
    fmov f0, f1
texgen_y_lo_ok:
    fmul f0, f0, f12
    fadd f0, f0, f12
    fload f1, SCR_FST(r27)
    fmul f0, f0, f1
    fcvtfi r7, f0                         ; v = (int)(doty * st)
    bra col_done

col_unlit:
    ; rgba = cn bytes verbatim (LE pack = byte order r,g,b,a)
    load.b r29, VTX_CN(r1)
    load.b r4, 13(r1)
    lsl.l r4, r4, #8
    or.l r29, r29, r4
    load.b r4, 14(r1)
    lsl.l r4, r4, #16
    or.l r29, r29, r4
    load.b r4, 15(r1)
    lsl.l r4, r4, #24
    or.l r29, r29, r4

col_done:
    ; --- output stores (BE floats via fmovo+bswap) ---
    ; clip @16..28
    fmovo r4, f4
    bswap.l r4, r4
    store.l r4, LV_TX+0(r2)
    fmovo r4, f5
    bswap.l r4, r4
    store.l r4, LV_TX+4(r2)
    fmovo r4, f6
    bswap.l r4, r4
    store.l r4, LV_TX+8(r2)
    fmovo r4, f7
    bswap.l r4, r4
    store.l r4, LV_TX+12(r2)
    ; u,v as floats @32,36
    fcvtif f0, r6
    fmovo r4, f0
    bswap.l r4, r4
    store.l r4, LV_U+0(r2)
    fcvtif f0, r7
    fmovo r4, f0
    bswap.l r4, r4
    store.l r4, LV_U+4(r2)
    ; packed rgba @40 (LE store puts r,g,b,a at +0..+3)
    store.l r29, LV_COLOR(r2)
    ; object x,y,z,w @0..12 (w = 1.0)
    fmovo r4, f8
    bswap.l r4, r4
    store.l r4, LV_X+0(r2)
    fmovo r4, f9
    bswap.l r4, r4
    store.l r4, LV_X+4(r2)
    fmovo r4, f10
    bswap.l r4, r4
    store.l r4, LV_X+8(r2)
    move.l r4, #0x3F800000                ; 1.0f
    bswap.l r4, r4
    store.l r4, LV_X+12(r2)

    ; --- clip_rej: 6 plane tests, bit = !(a > b) per gfx_clip_reject ---
    ; order: tz>tw, tz<-tw, ty>tw, ty<-tw, tx>tw, tx<-tw
    fneg f2, f7                           ; -tw
    move.l r5, r0                         ; cr = 0
    move.l r10, #1

    fcmp r4, f6, f7                       ; tz vs tw
    lsl.l r5, r5, #1
    beq r4, r10, crj1
    or.l r5, r5, #1
crj1:
    fcmp r4, f2, f6                       ; (tz < -tw) == (-tw > tz)
    lsl.l r5, r5, #1
    beq r4, r10, crj2
    or.l r5, r5, #1
crj2:
    fcmp r4, f5, f7                       ; ty vs tw
    lsl.l r5, r5, #1
    beq r4, r10, crj3
    or.l r5, r5, #1
crj3:
    fcmp r4, f2, f5
    lsl.l r5, r5, #1
    beq r4, r10, crj4
    or.l r5, r5, #1
crj4:
    fcmp r4, f4, f7                       ; tx vs tw
    lsl.l r5, r5, #1
    beq r4, r10, crj5
    or.l r5, r5, #1
crj5:
    fcmp r4, f2, f4
    lsl.l r5, r5, #1
    beq r4, r10, crj6
    or.l r5, r5, #1
crj6:
    ; wlt0 = (tw < 0.0) -> byte 1 of the packed store
    fcmp r4, f7, f13
    move.l r10, #-1
    bne r4, r10, crj_wpos
    move.l r4, #0x100
    or.l r5, r5, r4
crj_wpos:
    store.l r5, LV_CLIP_REJ(r2)           ; cr | wlt0<<8 (bytes 46,47 pad)

    add.l r24, r24, #1
    bra batch_vert

batch_done:
    ; diag: kexit++
    la r7, DIAG_KEXIT
    load.l r6, 0(r7)
    add.l r6, r6, #1
    store.l r6, 0(r7)
    bra op_done_ok

; --- Completion ---
op_done_ok:
    move.l r7, #STATUS_OK
    store.l r7, RESP_STATUS(r20)
    move.l r7, r0
    store.l r7, RESP_RESULT(r20)
    store.l r7, RESP_LEN(r20)
    bra advance_tail

op_error:
    move.l r7, #STATUS_ERROR
    store.l r7, RESP_STATUS(r20)
    move.l r7, #1
    store.l r7, RESP_RESULT(r20)
    move.l r7, r0
    store.l r7, RESP_LEN(r20)

advance_tail:
    add.l r3, r3, #1
    and.l r3, r3, #0x0F
    store.b r3, RING_TAIL(r30)
    bra poll_loop
