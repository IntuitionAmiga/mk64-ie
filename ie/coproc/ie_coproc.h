/*
 * ie/coproc/ie_coproc.h - M68K-side caller for the IE64 T&L coprocessor.
 *
 * The guest (big-endian M68K) dispatches transform work to the IE64 coprocessor
 * (little-endian, near-native on the host) over the COPROC mailbox. Buffers live
 * in the guest's own RAM and are passed by pointer - the IE64 worker reads them
 * directly on the shared bus (verified) - and the SERVICE byte-swaps, so the
 * guest writes its native big-endian floats with no per-value work.
 */
#ifndef IE_COPROC_H
#define IE_COPROC_H

#include <stdint.h>

/* COSTART the IE64 T&L service and (re)initialise its mailbox ring (the ROM load
 * clobbers the 0x790000 mailbox region). Returns 1 if the worker is online. */
int ie_coproc_init(void);

/* Transform one vertex (x,y,z,1) by the 16-float matrix m (row*4+col layout,
 * matching gfx_transform_vec4); writes out[4]. Synchronous. Returns 1 on success,
 * 0 on any failure (caller should fall back to the local transform). */
int ie_coproc_xform(const float m[16], float x, float y, float z, float out[4]);

/* Vertex batch (gfx_sp_vertex, unlit or lit) for the IE64 coprocessor. The worker
 * reads `verts` (n Vtx) and writes `out` (n F3DLoadedVertex, 48 bytes each) directly
 * in guest RAM. Lit fields are ignored unless geomode has G_LIGHTING. NOTE: the
 * caller must NOT dispatch G_TEXTURE_GEN_LINEAR batches (the service has no acosf)
 * - run those on the local path. Synchronous. Returns 1 ok, 0 to fall back. */
typedef struct {
    const float *m;          /* MP_matrix, 16 floats (row*4+col)               */
    unsigned     ss, st;     /* texture_scaling_factor s/t (low 16 used)        */
    unsigned     geomode;    /* rsp.geometry_mode                               */
    unsigned     num_lights; /* rsp.current_num_lights                          */
    int          amb_col[3]; /* current_lights[num_lights-1].col (ambient)      */
    int          dir_col[3]; /* current_lights[0].col (directional)             */
    const float *light_coef; /* current_lights_coeffs[0], 3 floats              */
    const float *lookat;     /* current_lookat_coeffs[0],[1] flattened, 6 floats*/
    unsigned     n;          /* vertex count                                    */
    const void  *verts;      /* &Vtx[0]                                         */
    void        *out;        /* &LoadedVertex[dest]                             */
} ie_tnl_params;

int ie_coproc_tnl_batch(const ie_tnl_params *p);

/* Async variant: enqueue the batch and return immediately so the M68K keeps
 * walking the display list while the worker transforms in parallel. The caller
 * MUST call ie_coproc_drain() before reading the transformed verts. */
int ie_coproc_tnl_batch_async(const ie_tnl_params *p);

/* Block until the in-flight async batch (if any) has been transformed. */
void ie_coproc_drain(void);

#endif /* IE_COPROC_H */
