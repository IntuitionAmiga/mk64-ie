#include <ultra64.h>
#include <PR/ultratypes.h>

#include "asset_endian.h"
#include "memory.h"

/*
 * Convert course vertices from their asset form to GBI Vtx records.
 *
 * The decompressed vertex stream in <course>_geography.bin is a packed
 * array of 14-byte CourseVtx records in ROM byte order (big-endian):
 * s16 ob[3], s16 tc[2], s8 ca[4]. The low two bits of the first two
 * colour channels carry per-vertex flags, exactly as in the original
 * game's func_802A86A8.
 */

#define COURSE_VTX_ASSET_SIZE 14

void course_vertex_convert(const void* src, u32 count, Vtx* dst, s32 mirror, f32 stretchY) {
    const u8* rec = (const u8*) src;
    Vtx* vtx = dst;
    u32 i;
    s8 temp_a0;
    s8 temp_a3;
    s8 flags;

    for (i = 0; i < count; i++) {
        s16 ob0 = asset_be_s16(rec + 0);

        if (mirror) {
            vtx->v.ob[0] = -ob0;
        } else {
            vtx->v.ob[0] = ob0;
        }

        vtx->v.ob[1] = (asset_be_s16(rec + 2) * stretchY);
        temp_a0 = (s8) rec[10];
        temp_a3 = (s8) rec[11];

        flags = temp_a0 & 3;
        flags |= (temp_a3 << 2) & 0xC;

        vtx->v.ob[2] = asset_be_s16(rec + 4);
        vtx->v.tc[0] = asset_be_s16(rec + 6);
        vtx->v.tc[1] = asset_be_s16(rec + 8);
        vtx->v.cn[0] = (temp_a0 & 0xFC);
        vtx->v.cn[1] = (temp_a3 & 0xFC);
        vtx->v.cn[2] = (s8) rec[12];
        vtx->v.flag = flags;
        vtx->v.cn[3] = 0xFF;
        vtx++;
        rec += COURSE_VTX_ASSET_SIZE;
    }
}
