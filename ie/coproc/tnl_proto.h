/*
 * ie/coproc/tnl_proto.h - request layout shared by the M68K caller and the IE64
 * T&L service. ALL multi-byte fields are BIG-ENDIAN (M68K native); the x86
 * service byte-swaps. Buffers live in guest RAM and are passed by pointer - the
 * IE64 worker reads/writes them directly on the shared bus.
 *
 * Wire layouts (guarded by the IE_TNL_ASSERT_LAYOUT block below):
 *   Vtx              = 16 bytes: ob[3] s16 @0,2,4; flag u16 @6; tc[2] s16 @8,10;
 *                      cn[4] u8 @12 (also n[3]+a for the lit path).
 *   F3DLoadedVertex  = 48 bytes: x,y,z,w f32 @0 (object); _x,_y,_z,_w f32 @16
 *                      (clip); u,v f32 @32; color rgba @40; clip_rej u8 @44;
 *                      wlt0 u8 @45; 2 bytes padding.
 */
#ifndef IE_TNL_PROTO_H
#define IE_TNL_PROTO_H

#define OP_ADD        1u  /* P0 plumbing proof                                  */
#define OP_TNL_XFORM  2u  /* single matrix*vec4 (P1 proof)                      */
#define OP_TNL_BATCH  3u  /* batch: gfx_sp_vertex (unlit or lit) over n verts   */
/* op 4 was OP_TRI_EMIT on the ie68-port branch; retired (this branch's
 * gfx_pc emits flat-float buf_vbo, not dc_fast_t). Do not reuse op 4. */

/* Geometry-mode bits the kernel needs (PR/gbi.h). G_TEXTURE_GEN_LINEAR needs
 * acosf, which the freestanding service lacks - the caller routes those batches
 * to the local M68K path instead. */
#define TNL_G_LIGHTING     0x00020000u
#define TNL_G_TEXTURE_GEN  0x00040000u

/* OP_TNL_BATCH request header (all BE). The source Vtx array and the
 * F3DLoadedVertex output array are referenced by pointer (read/written directly
 * in guest RAM). */
#define TNL_REQ_MATRIX     0u    /* float[16], MP_matrix (row*4+col)            */
#define TNL_REQ_TEXSCALE_S 64u   /* u32, low 16 = texture_scaling_factor.s      */
#define TNL_REQ_TEXSCALE_T 68u   /* u32, low 16 = .t                           */
#define TNL_REQ_NVERTS     72u   /* u32                                         */
#define TNL_REQ_VERTS_PTR  76u   /* u32, &Vtx[0]                                */
#define TNL_REQ_OUT_PTR    80u   /* u32, &loaded_vertices[dest]                 */
#define TNL_REQ_GEOMODE    84u   /* u32, rsp.geometry_mode                      */
#define TNL_REQ_NUMLIGHTS  88u   /* u32, rsp.current_num_lights                 */
#define TNL_REQ_AMB_COL    92u   /* u32[3], ambient col (current_lights[N-1])   */
#define TNL_REQ_DIR_COL    104u  /* u32[3], directional col (current_lights[0]) */
#define TNL_REQ_LIGHTCOEF  116u  /* float[3], current_lights_coeffs[0]          */
#define TNL_REQ_LOOKAT     128u  /* float[6], current_lookat_coeffs[0][3],[1][3]*/
#define TNL_REQ_SIZE       152u

/* Vtx (source) field offsets. */
#define VTX_SIZE       16u
#define VTX_OB         0u   /* s16[3]   */
#define VTX_TC         8u   /* s16[2]   */
#define VTX_CN         12u  /* u8[4]    */

/* F3DLoadedVertex (output) field offsets (src/gfx/gfx_fast3d.h). */
#define LV_SIZE        48u
#define LV_X           0u   /* f32 x,y,z,w (object, w = 1.0)  */
#define LV_TX          16u  /* f32 _x,_y,_z,_w (clip space)   */
#define LV_U           32u  /* f32 u,v                        */
#define LV_COLOR       40u  /* u8 r,g,b,a                     */
#define LV_CLIP_REJ    44u  /* u8                             */
#define LV_WLT0        45u  /* u8 (bytes 46,47 are padding)   */

/* Layout guards: a translation unit that sees the real structs defines
 * IE_TNL_ASSERT_LAYOUT before including this header; the build fails if the
 * wire offsets above drift from the structs the game actually uses. */
#ifdef IE_TNL_ASSERT_LAYOUT
#include <stddef.h>
_Static_assert(sizeof(Vtx) == VTX_SIZE, "Vtx size drifted from tnl_proto.h");
_Static_assert(offsetof(Vtx_t, ob) == VTX_OB && offsetof(Vtx_t, tc) == VTX_TC &&
                   offsetof(Vtx_t, cn) == VTX_CN,
               "Vtx_t field offsets drifted from tnl_proto.h");
_Static_assert(sizeof(struct F3DLoadedVertex) == LV_SIZE,
               "F3DLoadedVertex size drifted from tnl_proto.h");
_Static_assert(offsetof(struct F3DLoadedVertex, x) == LV_X &&
                   offsetof(struct F3DLoadedVertex, _x) == LV_TX &&
                   offsetof(struct F3DLoadedVertex, u) == LV_U &&
                   offsetof(struct F3DLoadedVertex, color) == LV_COLOR &&
                   offsetof(struct F3DLoadedVertex, clip_rej) == LV_CLIP_REJ &&
                   offsetof(struct F3DLoadedVertex, wlt0) == LV_WLT0,
               "F3DLoadedVertex field offsets drifted from tnl_proto.h");
#endif

#endif /* IE_TNL_PROTO_H */
