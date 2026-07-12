#include <ultra64.h>
#include <PR/ultratypes.h>
#include <macros.h>
#include <common_structs.h>
#include <segments.h>
#include <decode.h>

#include "memory.h"
#include "main.h"
#include "code_800029B0.h"
#include "math_util.h"
#include "course_metadata.h"
#include "defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"

extern s16 gCurrentCourseId;

#include "buffer_sizes.h"
extern uint8_t __attribute__((aligned(32))) COURSE_BUF[COURSE_BUF_SIZE];
extern uint8_t __attribute__((aligned(32))) UNPACK_BUF[UNPACK_BUF_SIZE];
extern Gfx __attribute__((aligned(32))) UNPACKED_DL_BUF[UNPACKED_DL_BUF_SIZE / 8];
extern uint8_t __attribute__((aligned(32))) COURSE_OFFSETS_BUF[COURSE_OFFSETS_BUF_SIZE];
extern uint8_t __attribute__((aligned(32))) SEG4_BUF[SEG4_BUF_SIZE];
extern uint8_t __attribute__((aligned(32))) SEG5_BUF[SEG5_BUF_SIZE];
extern uint8_t __attribute__((aligned(32))) COMP_VERT_BUF[COMP_VERT_BUF_SIZE];
extern uint8_t __attribute__((aligned(32))) DECOMP_VERT_BUF[DECOMP_VERT_BUF_SIZE];

static char asset_name[256];

struct UnkStruct_802B8CD4 D_802B8CD4[] = { 0 };
s32 D_802B8CE4 = 0; // pad
s32 memoryPadding[2];

/**
 * @brief Stores the physical memory addr for segmented memory in `gSegmentTable` using the segment number as an index.
 *
 * This function takes a segment number and a pointer to a memory address, and stores the address in the `gSegmentTable`
 * array at the specified segment index. The stored address is truncated to a 29-bit value to ensure that it fits within
 * the memory address. This allows converting between segmented memory and physical memory.
 *
 * @param segment A segment number from 0x0 to 0xF to set the base address.
 * @param addr A pointer containing the physical memory address of the data.
 * @return The stored base address, truncated to a 29-bit value.
 */
uintptr_t set_segment_base_addr(s32 segment, void* addr) {
    gSegmentTable[segment] = (uintptr_t) addr;
    return gSegmentTable[segment];
}

/**
 * @brief Returns the physical memory location of a segment.
 * @param permits segment numbers from 0x0 to 0xF.
 */
void* get_segment_base_addr(s32 segment) {
    return (void*) (gSegmentTable[segment]);
}

/**
 * @brief converts an RSP segment + offset address to a normal memory address
 */
void* segmented_to_virtual(const void* addr) {
    uintptr_t uip_addr = (uintptr_t) addr;

    /*
     * Real-pointer rule: segment ids occupy the top byte and only
     * 0x00-0x0F are segments. Anything above is an already-virtual
     * pointer (the IE image, heap, and stack live at 0x10000000+;
     * the old console port's pointers sat at 0x8Cxxxxxx and stay
     * covered by the same rule).
     */
    size_t segment = (uintptr_t) uip_addr >> 24;

    if (segment > 0x0F) {
        return (void*) uip_addr;
    }

#if DEBUG
    if (segment > 0xf) {
        platform_fatal("%08x converts to bad segment %02x", (uintptr_t) addr, segment);
    }
#endif
    /* if (gSegmentTable[segment] == 0) {
        printf("segment %02x has null base address, original address %08x\n", segment, uip_addr);
    } */

    size_t offset = (uintptr_t) uip_addr & 0x00FFFFFF;
    return (void*) ((gSegmentTable[segment] + offset));
}

void move_segment_table_to_dmem(void) {
    /* s32 i;
    for (i = 0; i < 16; i++) {
        gSPSegment(gDisplayListHead++, i, gSegmentTable[i]);
    } */
}

void n64_memcpy(void* dst, const void* src, size_t size) {
    uint8_t* bdst = (uint8_t*) dst;
    uint8_t* bsrc = (uint8_t*) src;
    uint16_t *sdst = (uint16_t *)dst;
    uint16_t *ssrc = (uint16_t *)src;
    uint32_t* wdst = (uint32_t*) dst;
    uint32_t* wsrc = (uint32_t*) src;

    int size_to_copy = size;
    int words_to_copy = size_to_copy >> 2;
    int shorts_to_copy = size_to_copy >> 1;
    int bytes_to_copy = size_to_copy - (words_to_copy<<2);
    int sbytes_to_copy = size_to_copy - (shorts_to_copy<<1);

    __builtin_prefetch(bsrc);
    if ((!(((uintptr_t)bdst | (uintptr_t)bsrc) & 3))) {
        while (words_to_copy--) {
            if ((words_to_copy & 3) == 0) {
                __builtin_prefetch(bsrc + 16);
            }
            *wdst++ = *wsrc++;
        }

        bdst = (uint8_t*) wdst;
        bsrc = (uint8_t*) wsrc;

        switch (bytes_to_copy) {
            case 0:
                return;
            case 1:
                goto n64copy1;
            case 2:
                goto n64copy2;
            case 3:
                goto n64copy3;
            case 4:
                goto n64copy4;
            case 5:
                goto n64copy5;
            case 6:
                goto n64copy6;
            case 7:
                goto n64copy7;
        }
    }  else if ((!(((uintptr_t)bdst | (uintptr_t)bsrc) & 1))) {
        while (shorts_to_copy--) {
            *sdst++ = *ssrc++;
        }

        bdst = (uint8_t*) sdst;
        bsrc = (uint8_t*) ssrc;

        switch (sbytes_to_copy) {
            case 0:
                return;
            case 1:
                goto n64copy1;
            case 2:
                goto n64copy2;
            case 3:
                goto n64copy3;
            case 4:
                goto n64copy4;
            case 5:
                goto n64copy5;
            case 6:
                goto n64copy6;
            case 7:
                goto n64copy7;
        }
    } else {
        while (words_to_copy > 0) {
            uint8_t b1, b2, b3, b4;
            b1 = *bsrc++;
            b2 = *bsrc++;
            b3 = *bsrc++;
            b4 = *bsrc++;
#if 1
            asm volatile("" : : : "memory");
#endif
            *bdst++ = b1; //*bsrc++;
            *bdst++ = b2; //*bsrc++;
            *bdst++ = b3; //*bsrc++;
            *bdst++ = b4; //*bsrc++;

            words_to_copy--;
        }

        switch (bytes_to_copy) {
            case 0:
                return;
            case 1:
                goto n64copy1;
            case 2:
                goto n64copy2;
            case 3:
                goto n64copy3;
            case 4:
                goto n64copy4;
            case 5:
                goto n64copy5;
            case 6:
                goto n64copy6;
            case 7:
                goto n64copy7;
        }
    }

n64copy7:
    *bdst++ = *bsrc++;
n64copy6:
    *bdst++ = *bsrc++;
n64copy5:
    *bdst++ = *bsrc++;
n64copy4:
    *bdst++ = *bsrc++;
n64copy3:
    *bdst++ = *bsrc++;
n64copy2:
    *bdst++ = *bsrc++;
n64copy1:
    *bdst++ = *bsrc++;
    return;
}

void gfx_texture_cache_invalidate(void* arg);
extern u8 *ROVING_SEG3_BUF;
extern uint8_t __attribute__((aligned(32))) OTHER_BUF[OTHER_BUF_SIZE];

// starting address for this texture COMPRESSED is gNextFree+arg2
// starting address for this texture DECOMPRESSED is gNextFree
void mio0decode_noinval(const unsigned char *in, unsigned char *out);

u8* dma_textures(u8 texture[], UNUSED u32 arg1, u32 arg2) {
    u8* temp_v0;
    temp_v0 = (u8*) ROVING_SEG3_BUF;
    arg2 = ALIGN16(arg2);
    mio0decode_noinval((u8 *)texture, temp_v0);
    ROVING_SEG3_BUF += arg2;
    return temp_v0;
}

void func_802A86A8(CourseVtx* data, u32 arg1) {
    /* The decompressed vertex stream is in ROM byte order (big-endian
     * 14-byte records); course_vertex_convert owns the byte-order seam
     * and writes native Vtx records into SEG4_BUF. */
    course_vertex_convert(data, arg1, (Vtx*) SEG4_BUF, gIsMirrorMode, vtxStretchY);
}
void mio0decode_noinval(const unsigned char *in, unsigned char *out);

void decompress_vtx(CourseVtx* arg0, u32 vertexCount, void *target) {
	u8* vtxCompressed;	
	vtxCompressed = (u8*)segmented_to_virtual(arg0);

    mio0decode_noinval(vtxCompressed, (u8*) target);
    func_802A86A8((CourseVtx*) target, vertexCount);
    set_segment_base_addr(4, (void*) SEG4_BUF);
}

/* displaylist_unpack and the unpack_* helpers live in
 * src/racing/displaylist_unpack.c (restored from the original game for
 * the ROM-extracted packed streams). */

struct UnkStr_802AA7C8 {
    u8* unk0;
    uintptr_t unk4;
    uintptr_t unk8;
    uintptr_t unkC;
};

char __attribute__((aligned(32))) coursenames[20][32] = {
"mario_raceway",
"choco_mountain",
"bowsers_castle",
"banshee_boardwalk",
"yoshi_valley",
"frappe_snowland",
"koopa_troopa_beach",
"royal_raceway",
"luigi_raceway",
"moo_moo_farm",
"toads_turnpike",
"kalimari_desert",
"sherbet_land",
"rainbow_road",
"wario_stadium",
"block_fort",
"skyscraper",
"double_deck",
"dks_jungle_parkway",
"big_donut",
};

char *get_course_name(s16 course) {
    return coursenames[course];
}

/*
 * <course>_tex.bin is the ready-made segment-5 image: every course
 * texture already mio0-decompressed at the cumulative aligned offset
 * the course display lists expect (the layout the original game built
 * on its heap from the segment-9 texture table).
 */
void decompress_textures(UNUSED u32* arg0) {
    char *courseName = get_course_name(gCurrentCourseId);
    sprintf(asset_name, "%s_tex.bin", courseName);

    if (!platform_asset_read(asset_name, SEG5_BUF, sizeof(SEG5_BUF), NULL)) {
        platform_fatal("failed to read asset %s", asset_name);
    }

    set_segment_base_addr(0x5, (void*)SEG5_BUF);
}

void mio0decode_noinval(const unsigned char *in, unsigned char *out);

void* decompress_segments(u8* start, u8 *target) {
    mio0decode_noinval(segmented_to_virtual(start), (u8*) target);
    return (void*) target;
}

extern void nuke_everything(void);

/**
 * @brief Loads course data through the neutral asset layout. Vtx,
 * textures, displaylists, offset tables, etc.
 *
 * All per-course values (vertex count, packed displaylist offset, final
 * displaylist offset, unknown1) come from course_metadata.bin - the
 * runtime form of the ROM course table - instead of the stale compiled
 * tables the previous port carried.
 * @param courseId
 */
u8* load_course(s32 courseId) {
    const struct CourseMetadata* metadata = course_metadata_get(courseId);
    char* courseName;

    nuke_everything();

    memset(COURSE_BUF, 0, sizeof(COURSE_BUF));
    memset(COMP_VERT_BUF, 0, sizeof(COMP_VERT_BUF));
    memset(DECOMP_VERT_BUF, 0, sizeof(DECOMP_VERT_BUF));
    memset(UNPACK_BUF, 0, sizeof(UNPACK_BUF));

    courseName = get_course_name(gCurrentCourseId);

    // course data (display lists, track sections)
    sprintf(asset_name, "%s_data.bin", courseName);
    if (!platform_asset_read(asset_name, COURSE_BUF, sizeof(COURSE_BUF), NULL)) {
        platform_fatal("failed to read asset %s", asset_name);
    }
    set_segment_base_addr(6, COURSE_BUF);

    // segment 9 offset tables (lights, texture table, displaylist tables)
    sprintf(asset_name, "%s_offsets.bin", courseName);
    if (!platform_asset_read(asset_name, COURSE_OFFSETS_BUF, sizeof(COURSE_OFFSETS_BUF), NULL)) {
        platform_fatal("failed to read asset %s", asset_name);
    }
    set_segment_base_addr(9, COURSE_OFFSETS_BUF);

    // course geography: compressed vertices followed by packed displaylists
    sprintf(asset_name, "%s_geography.bin", courseName);
    {
        size_t got = 0;
        size_t packed_offset = metadata->packedOffset;

        if (!platform_asset_read_range(asset_name, 0, COMP_VERT_BUF, packed_offset, &got) ||
            got != packed_offset) {
            platform_fatal("failed to read vertices from asset %s", asset_name);
        }

        if (!platform_asset_read_range(asset_name, packed_offset, UNPACK_BUF, sizeof(UNPACK_BUF), &got)) {
            platform_fatal("failed to read displaylists from asset %s", asset_name);
        }

        set_segment_base_addr(0xF, (void*) COMP_VERT_BUF);
        decompress_vtx((CourseVtx*) COMP_VERT_BUF, metadata->vertexCount, DECOMP_VERT_BUF);

        displaylist_unpack((uintptr_t*) UNPACK_BUF, metadata->finalDisplaylistOffset,
                           metadata->unknown1);
        set_segment_base_addr(0x7, UNPACKED_DL_BUF);
    }
    decompress_textures(0);
    return COMP_VERT_BUF;
}
