#include <ultra64.h>
#include <PR/ultratypes.h>
#include <macros.h>

#include "buffer_sizes.h"
#include "memory.h"
#include "platform/platform.h"

/*
 * Course packed display-list unpacking, restored from the original
 * game (the previous port replaced the unpacked stream with compiled
 * display lists and reduced displaylist_unpack to a segment
 * assignment; the ROM-extracted <course>_geography.bin carries the
 * original packed byte stream again).
 *
 * The unpacked stream is written to UNPACKED_DL_BUF as native Gfx
 * entries. Offsets in the packed data count original 8-byte Gfx units,
 * so the one place that emits an intra-segment-7 reference
 * (unpack_displaylist) scales by sizeof(Gfx); on 32-bit targets that
 * is the original arithmetic.
 */

extern s32 gIsMirrorMode;
extern Gfx __attribute__((aligned(32))) UNPACKED_DL_BUF[UNPACKED_DL_BUF_SIZE / 8];

s32 sGfxSeekPosition;
s32 sPackedSeekPosition;

void unpack_lights(Gfx* arg0, UNUSED u8* arg1, s8 arg2) {
    s32 a = (arg2 * 0x18) + 0x9000008;
    s32 b = (arg2 * 0x18) + 0x9000000;
    Gfx macro[] = { gsSPNumLights(NUMLIGHTS_1) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;

    sGfxSeekPosition++;
    arg0[sGfxSeekPosition].words.w0 = 0x3860010;
    arg0[sGfxSeekPosition].words.w1 = a;

    sGfxSeekPosition++;
    arg0[sGfxSeekPosition].words.w0 = 0x3880010;
    arg0[sGfxSeekPosition].words.w1 = b;
    sGfxSeekPosition++;
}

void unpack_displaylist(Gfx* arg0, u8* args, UNUSED s8 opcode) {
    u32 temp_v0 = args[sPackedSeekPosition++];
    uintptr_t index = (args[sPackedSeekPosition++] << 8) | temp_v0;

    arg0[sGfxSeekPosition].words.w0 = 0x06000000;
    /* Segment-7 address of the referenced unpacked command. */
    arg0[sGfxSeekPosition].words.w1 = 0x07000000 + index * sizeof(Gfx);
    sGfxSeekPosition++;
}

void unpack_end_displaylist(Gfx* arg0, UNUSED u8* arg1, UNUSED s8 arg2) {
    arg0[sGfxSeekPosition].words.w0 = (u32) G_ENDDL << 24;
    arg0[sGfxSeekPosition].words.w1 = 0;
    sGfxSeekPosition++;
}

void unpack_set_geometry_mode(Gfx* arg0, UNUSED u8* arg1, UNUSED s8 arg2) {
    Gfx macro[] = { gsSPSetGeometryMode(G_CULL_BACK) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_clear_geometry_mode(Gfx* arg0, UNUSED u8* arg1, UNUSED s8 arg2) {
    Gfx macro[] = { gsSPClearGeometryMode(G_CULL_BACK) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_cull_displaylist(Gfx* arg0, UNUSED u8* arg1, UNUSED s8 arg2) {
    Gfx macro[] = { gsSPCullDisplayList(0, 160) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode1(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_MODULATERGBA) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode2(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetCombineMode(G_CC_MODULATERGBDECALA, G_CC_MODULATERGBDECALA) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode_shade(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode4(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetCombineMode(G_CC_MODULATERGBDECALA, G_CC_MODULATERGBDECALA) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_combine_mode5(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetCombineMode(G_CC_DECALRGBA, G_CC_DECALRGBA) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_opaque(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_tex_edge(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetRenderMode(G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_translucent(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetRenderMode(G_RM_AA_ZB_XLU_SURF, G_RM_AA_ZB_XLU_SURF2) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_opaque_decal(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetRenderMode(G_RM_AA_ZB_OPA_DECAL, G_RM_AA_ZB_OPA_DECAL) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_render_mode_translucent_decal(Gfx* arg0, UNUSED u8* arg1, UNUSED uintptr_t arg2) {
    Gfx macro[] = { gsDPSetRenderMode(G_RM_AA_ZB_XLU_DECAL, G_RM_AA_ZB_XLU_DECAL) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_tile_sync(Gfx* gfx, u8* args, s8 opcode) {
    Gfx tileSync[] = { gsDPTileSync() };
    u32 temp_a0;
    u32 lo;
    u32 hi;
    s32 width = 32;
    s32 height = 32;
    s32 fmt = 0;
    s32 siz;
    s32 line;
    s32 tmem;
    s32 cms;
    s32 masks;
    s32 cmt;
    s32 maskt;
    s32 lrs;
    s32 lrt;

    tmem = 0;
    switch (opcode) {
        case 26:
            break;
        case 44:
            tmem = 256;
            break;
        case 27:
            width = 64;
            break;
        case 28:
            height = 64;
            break;
        case 29:
            fmt = 3;
            break;
        case 30:
            width = 64;
            fmt = 3;
            break;
        case 31:
            height = 64;
            fmt = 3;
            break;
    }

    siz = G_IM_SIZ_16b_BYTES;
    line = (((width * 2) + 7) >> 3);

    temp_a0 = args[sPackedSeekPosition++];
    cms = temp_a0 & 0xF;
    masks = (temp_a0 & 0xF0) >> 4;

    temp_a0 = args[sPackedSeekPosition++];
    cmt = temp_a0 & 0xF;
    maskt = (temp_a0 & 0xF0) >> 4;

    gfx[sGfxSeekPosition].words.w0 = tileSync->words.w0;
    gfx[sGfxSeekPosition].words.w1 = tileSync->words.w1;
    sGfxSeekPosition++;

    lo = ((u32) G_SETTILE << 24) | (fmt << 21) | (siz << 19) | (line << 9) | tmem;
    hi = (cmt << 18) | (maskt << 14) | (cms << 8) | (masks << 4);

    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = hi;
    sGfxSeekPosition++;

    lrs = (width - 1) << 2;
    lrt = (height - 1) << 2;

    lo = (u32) G_SETTILESIZE << 24;
    hi = (lrs << 12) | lrt;

    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = hi;
    sGfxSeekPosition++;
}

void unpack_tile_load_sync(Gfx* gfx, u8* args, s8 opcode) {
    Gfx tileSync[] = { gsDPTileSync() };
    Gfx loadSync[] = { gsDPLoadSync() };
    u32 arg;
    u32 lo;
    u32 hi;
    u32 addr;
    u32 width = 32;
    u32 height = 32;
    u32 fmt = 0;
    u32 siz;
    u32 tmem;
    u32 tile;

    switch (opcode) {
        case 32:
            break;
        case 33:
            width = 64;
            break;
        case 34:
            height = 64;
            break;
        case 35:
            fmt = 3;
            break;
        case 36:
            width = 64;
            fmt = 3;
            break;
        case 37:
            height = 64;
            fmt = 3;
            break;
    }

    /* Texture address inside segment 5 (2 KiB granularity). */
    addr = SEGMENT_ADDR(0x05, args[sPackedSeekPosition++] << 11);
    sPackedSeekPosition++;
    arg = args[sPackedSeekPosition++];
    siz = G_IM_SIZ_16b;
    tmem = (arg & 0xF);
    tile = (arg & 0xF0) >> 4;

    lo = ((u32) G_SETTIMG << 24) | (fmt << 21) | (siz << 19);
    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = addr;
    sGfxSeekPosition++;

    gfx[sGfxSeekPosition].words.w0 = tileSync->words.w0;
    gfx[sGfxSeekPosition].words.w1 = tileSync->words.w1;
    sGfxSeekPosition++;

    lo = ((u32) G_SETTILE << 24) | (fmt << 21) | (siz << 19) | tmem;
    hi = tile << 24;

    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = hi;
    sGfxSeekPosition++;

    gfx[sGfxSeekPosition].words.w0 = loadSync->words.w0;
    gfx[sGfxSeekPosition].words.w1 = loadSync->words.w1;
    sGfxSeekPosition++;

    lo = (u32) G_LOADBLOCK << 24;
    hi = (tile << 24) | (MIN((width * height) - 1, 0x7FF) << 12)
       | CALC_DXT(width, G_IM_SIZ_16b_BYTES);

    gfx[sGfxSeekPosition].words.w0 = lo;
    gfx[sGfxSeekPosition].words.w1 = hi;
    sGfxSeekPosition++;
}

void unpack_texture_on(Gfx* arg0, UNUSED u8* args, UNUSED s8 arg2) {
    Gfx macro[] = { gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_texture_off(Gfx* arg0, UNUSED u8* args, UNUSED s8 arg2) {
    Gfx macro[] = { gsSPTexture(0x1, 0x1, 0, G_TX_RENDERTILE, G_OFF) };

    arg0[sGfxSeekPosition].words.w0 = macro->words.w0;
    arg0[sGfxSeekPosition].words.w1 = macro->words.w1;
    sGfxSeekPosition++;
}

void unpack_vtx1(Gfx* gfx, u8* args, UNUSED s8 arg2) {
    u32 temp_t7;
    u32 temp_t7_2;
    u32 temp = args[sPackedSeekPosition++];
    u32 temp2 = ((args[sPackedSeekPosition++] << 8) | temp) * 0x10;

    temp = args[sPackedSeekPosition++];
    temp_t7 = temp & 0x3F;
    temp = args[sPackedSeekPosition++];
    temp_t7_2 = temp & 0x3F;

    gfx[sGfxSeekPosition].words.w0 =
        (G_VTX << 24) | (temp_t7_2 * 2 << 16) | ((temp_t7 << 10) + ((0x10 * temp_t7) - 1));
    gfx[sGfxSeekPosition].words.w1 = 0x04000000 + temp2;
    sGfxSeekPosition++;
}

void unpack_vtx2(Gfx* gfx, u8* args, s8 arg2) {
    u32 temp_t9;
    u32 temp_v1;
    u32 temp_v2;

    temp_v1 = args[sPackedSeekPosition++];
    temp_v2 = ((args[sPackedSeekPosition++] << 8) | temp_v1) * 0x10;

    temp_t9 = arg2 - 50;

    gfx[sGfxSeekPosition].words.w0 = (G_VTX << 24) | ((temp_t9 << 10) + ((temp_t9 * 0x10) - 1));
    gfx[sGfxSeekPosition].words.w1 = 0x4000000 + temp_v2;
    sGfxSeekPosition++;
}

void unpack_triangle(Gfx* gfx, u8* args, UNUSED s8 arg2) {
    u32 temp_v0;
    u32 phi_a0;
    u32 phi_a2;
    u32 phi_a3;

    temp_v0 = args[sPackedSeekPosition++];

    if (gIsMirrorMode) {
        phi_a3 = temp_v0 & 0x1F;
        phi_a2 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_a2 |= (temp_v0 & 3) * 8;
        phi_a0 = (temp_v0 >> 2) & 0x1F;
    } else {
        phi_a0 = temp_v0 & 0x1F;
        phi_a2 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_a2 |= (temp_v0 & 3) * 8;
        phi_a3 = (temp_v0 >> 2) & 0x1F;
    }
    gfx[sGfxSeekPosition].words.w0 = (u32) G_TRI1 << 24;
    gfx[sGfxSeekPosition].words.w1 = ((phi_a0 * 2) << 16) | ((phi_a2 * 2) << 8) | (phi_a3 * 2);
    sGfxSeekPosition++;
}

void unpack_quadrangle(Gfx* gfx, u8* args, UNUSED s8 arg2) {
    u32 temp_v0;
    u32 phi_t0;
    u32 phi_a3;
    u32 phi_a0;
    u32 phi_t2;
    u32 phi_t1;
    u32 phi_a2;

    temp_v0 = args[sPackedSeekPosition++];

    if (gIsMirrorMode) {
        phi_t0 = temp_v0 & 0x1F;
        phi_a3 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_a3 |= (temp_v0 & 3) * 8;
        phi_a0 = (temp_v0 >> 2) & 0x1F;
    } else {
        phi_a0 = temp_v0 & 0x1F;
        phi_a3 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_a3 |= (temp_v0 & 3) * 8;
        phi_t0 = (temp_v0 >> 2) & 0x1F;
    }

    temp_v0 = args[sPackedSeekPosition++];

    if (gIsMirrorMode) {
        phi_a2 = temp_v0 & 0x1F;
        phi_t1 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_t1 |= (temp_v0 & 3) * 8;
        phi_t2 = (temp_v0 >> 2) & 0x1F;
    } else {
        phi_t2 = temp_v0 & 0x1F;
        phi_t1 = (temp_v0 >> 5) & 7;
        temp_v0 = args[sPackedSeekPosition++];
        phi_t1 |= (temp_v0 & 3) * 8;
        phi_a2 = (temp_v0 >> 2) & 0x1F;
    }
    gfx[sGfxSeekPosition].words.w0 =
        ((u32) G_TRI2 << 24) | ((phi_a0 * 2) << 16) | ((phi_a3 * 2) << 8) | (phi_t0 * 2);
    gfx[sGfxSeekPosition].words.w1 = ((phi_t2 * 2) << 16) | ((phi_t1 * 2) << 8) | (phi_a2 * 2);
    sGfxSeekPosition++;
}

void unpack_spline_3D(Gfx* gfx, u8* args, UNUSED s8 arg2) {
    u32 temp_v0;
    u32 phi_a0;
    u32 phi_t0;
    u32 phi_a3;
    u32 phi_a2;

    temp_v0 = args[sPackedSeekPosition++];

    if (gIsMirrorMode != 0) {
        phi_a0 = temp_v0 & 0x1F;
        phi_a2 = ((temp_v0 >> 5) & 7);
        temp_v0 = args[sPackedSeekPosition++];
        phi_a2 |= ((temp_v0 & 3) * 8);
        phi_a3 = (temp_v0 >> 2) & 0x1F;
        phi_t0 = ((temp_v0 >> 7) & 1);
        temp_v0 = args[sPackedSeekPosition++];
        phi_t0 |= (temp_v0 & 0xF) * 2;
    } else {
        phi_t0 = temp_v0 & 0x1F;
        phi_a3 = ((temp_v0 >> 5) & 7);
        temp_v0 = args[sPackedSeekPosition++];
        phi_a3 |= ((temp_v0 & 3) * 8);
        phi_a2 = (temp_v0 >> 2) & 0x1F;
        phi_a0 = ((temp_v0 >> 7) & 1);
        temp_v0 = args[sPackedSeekPosition++];
        phi_a0 |= (temp_v0 & 0xF) * 2;
    }
    gfx[sGfxSeekPosition].words.w0 = (u32) G_LINE3D << 24;
    gfx[sGfxSeekPosition].words.w1 =
        ((phi_a0 * 2) << 24) | ((phi_t0 * 2) << 16) | ((phi_a3 * 2) << 8) | (phi_a2 * 2);
    sGfxSeekPosition++;
}

/* Exact number of Gfx entries each opcode emits, for the buffer guard. */
static s32 unpack_emit_count(u8 opcode) {
    if (opcode <= 0x14) {
        return 3; /* unpack_lights */
    }
    if ((opcode >= 0x1A && opcode <= 0x1F) || opcode == 0x2C) {
        return 3; /* unpack_tile_sync */
    }
    if (opcode >= 0x20 && opcode <= 0x25) {
        return 5; /* unpack_tile_load_sync */
    }
    return 1;
}

/**
 * Unpacks course packed displaylists by iterating through each byte of the packed file.
 * Each packed displaylist entry has an opcode and any number of arguments.
 * The opcodes range from 0 to 87 which are used to run the relevant unpack function.
 * The file pointer increments when arguments are used. This way,
 * displaylist_unpack will always read an opcode and not an argument by accident.
 *
 * The caller passes the packed byte stream directly (the tail of
 * <course>_geography.bin) plus the course's finalDisplaylistOffset and
 * unknown1 values from the course metadata. The unpacked commands land
 * in UNPACKED_DL_BUF; the caller assigns it to segment 7.
 *
 * @warning opcodes that do not contain a definition in the switch are ignored. If an undefined opcode
 * contained arguments the unpacker might try to unpack those arguments.
 * This issue is prevented so long as the packed file adheres to correct opcodes and unpack code
 * increments the file pointer the correct number of times.
 */
void displaylist_unpack(uintptr_t* data, uintptr_t finalDisplaylistOffset, u32 arg2) {
    u8* packed_dl = (u8*) data;
    Gfx* gfx = UNPACKED_DL_BUF;
    u8 opcode;

    if (ALIGN16(finalDisplaylistOffset) + 8 > UNPACKED_DL_BUF_SIZE) {
        platform_fatal("unpacked displaylists need %u bytes, buffer holds %u",
                       (u32)(ALIGN16(finalDisplaylistOffset) + 8), (u32) UNPACKED_DL_BUF_SIZE);
    }

    sGfxSeekPosition = 0;
    sPackedSeekPosition = 0;

    while (1) {
        opcode = packed_dl[sPackedSeekPosition++];

        /* Break when the eof has been reached denoted by opcode 0xFF */
        if (opcode == 0xFF) {
            break;
        }

        if (sGfxSeekPosition + unpack_emit_count(opcode) > (s32)(UNPACKED_DL_BUF_SIZE / 8)) {
            platform_fatal("packed displaylist overruns UNPACKED_DL_BUF");
        }

        switch (opcode) {
            case 0x0:
            case 0x1:
            case 0x2:
            case 0x3:
            case 0x4:
            case 0x5:
            case 0x6:
            case 0x7:
            case 0x8:
            case 0x9:
            case 0xA:
            case 0xB:
            case 0xC:
            case 0xD:
            case 0xE:
            case 0xF:
            case 0x10:
            case 0x11:
            case 0x12:
            case 0x13:
            case 0x14:
                unpack_lights(gfx, packed_dl, opcode);
                break;
            case 0x15:
                unpack_combine_mode1(gfx, packed_dl, arg2);
                break;
            case 0x16:
                unpack_combine_mode2(gfx, packed_dl, arg2);
                break;
            case 0x17:
                unpack_combine_mode_shade(gfx, packed_dl, arg2);
                break;
            case 0x2E:
                unpack_combine_mode4(gfx, packed_dl, arg2);
                break;
            case 0x53:
                unpack_combine_mode5(gfx, packed_dl, arg2);
                break;
            case 0x18:
                unpack_render_mode_opaque(gfx, packed_dl, arg2);
                break;
            case 0x19:
                unpack_render_mode_tex_edge(gfx, packed_dl, arg2);
                break;
            case 0x2F:
                unpack_render_mode_translucent(gfx, packed_dl, arg2);
                break;
            case 0x54:
                unpack_render_mode_opaque_decal(gfx, packed_dl, arg2);
                break;
            case 0x55:
                unpack_render_mode_translucent_decal(gfx, packed_dl, arg2);
                break;
            case 0x1A:
            case 0x1B:
            case 0x1C:
            case 0x1D:
            case 0x1E:
            case 0x1F:
            case 0x2C:
                unpack_tile_sync(gfx, packed_dl, opcode);
                break;
            case 0x20:
            case 0x21:
            case 0x22:
            case 0x23:
            case 0x24:
            case 0x25:
                unpack_tile_load_sync(gfx, packed_dl, opcode);
                break;
            case 0x26:
                unpack_texture_on(gfx, packed_dl, opcode);
                break;
            case 0x27:
                unpack_texture_off(gfx, packed_dl, opcode);
                break;
            case 0x28:
                unpack_vtx1(gfx, packed_dl, opcode);
                break;
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x36:
            case 0x37:
            case 0x38:
            case 0x39:
            case 0x3A:
            case 0x3B:
            case 0x3C:
            case 0x3D:
            case 0x3E:
            case 0x3F:
            case 0x40:
            case 0x41:
            case 0x42:
            case 0x43:
            case 0x44:
            case 0x45:
            case 0x46:
            case 0x47:
            case 0x48:
            case 0x49:
            case 0x4A:
            case 0x4B:
            case 0x4C:
            case 0x4D:
            case 0x4E:
            case 0x4F:
            case 0x50:
            case 0x51:
            case 0x52:
                unpack_vtx2(gfx, packed_dl, opcode);
                break;
            case 0x29:
                unpack_triangle(gfx, packed_dl, opcode);
                break;
            case 0x58:
                unpack_quadrangle(gfx, packed_dl, opcode);
                break;
            case 0x30:
                unpack_spline_3D(gfx, packed_dl, opcode);
                break;
            case 0x2D:
                unpack_cull_displaylist(gfx, packed_dl, opcode);
                break;
            case 0x2A:
                unpack_end_displaylist(gfx, packed_dl, opcode);
                break;
            case 0x56:
                unpack_set_geometry_mode(gfx, packed_dl, opcode);
                break;
            case 0x57:
                unpack_clear_geometry_mode(gfx, packed_dl, opcode);
                break;
            case 0x2B:
                unpack_displaylist(gfx, packed_dl, opcode);
                break;
            default:
                /* Skip unknown values */
                break;
        }
    }
}
