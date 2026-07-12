/*
 * Host tests for the Stage 2 course-loading seam:
 *
 *  - course_metadata.bin parsing (the runtime form of the ROM course
 *    table produced by tools/rom_assets.py),
 *  - packed display-list unpacking (displaylist_unpack and the unpack_*
 *    helpers restored from the original game),
 *  - course vertex conversion from the big-endian asset form.
 *
 * When a generated rom_assets/ layout is present, the packed display
 * lists of all twenty courses are unpacked end-to-end and checked
 * against the course metadata contract.
 */
#include "test_support.h"

#include <stdbool.h>
#include <ultra64.h>
#include <mk64.h>

#include "buffer_sizes.h"
#include "common_structs.h"
#include "racing/course_metadata.h"
#include "racing/memory.h"

/* Test-local definitions for globals the seam code references.
 * UNPACKED_DL_BUF_SIZE counts original 8-byte Gfx units; the buffer
 * holds that many native Gfx entries. */
s32 gIsMirrorMode = 0;
Gfx __attribute__((aligned(32))) UNPACKED_DL_BUF[UNPACKED_DL_BUF_SIZE / 8];

void platform_fatal(const char* fmt, ...) {
    (void) fmt;
    printf("platform_fatal called\n");
    exit(2);
}

/* course_metadata_load is not exercised here (it needs a platform asset
 * backend); this stub only satisfies the linker. */
bool platform_asset_read(const char* name, void* dst, size_t max_size, size_t* size_out) {
    (void) name;
    (void) dst;
    (void) max_size;
    (void) size_out;
    return false;
}

/* ---- course_metadata.bin parsing ---------------------------------------- */

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t) v;
}

static size_t build_metadata_blob(uint8_t* blob, uint32_t count) {
    memcpy(blob, "MK64META", 8);
    put_be32(blob + 8, 1);
    put_be32(blob + 12, count);
    for (uint32_t i = 0; i < count; i++) {
        uint8_t* rec = blob + 16 + i * 16;
        put_be32(rec + 0, 0x1000 + i);  /* vertex_count */
        put_be32(rec + 4, 0x2000 + i);  /* packed_offset */
        put_be32(rec + 8, 0x3000 + i);  /* final_displaylist_offset */
        put_be32(rec + 12, i & 1);      /* unknown1 */
    }
    return 16 + count * 16;
}

static void test_metadata_parse_valid(void) {
    uint8_t blob[16 + 20 * 16];
    struct CourseMetadata records[20];
    size_t size = build_metadata_blob(blob, 20);

    EXPECT_EQ_INT(course_metadata_parse(blob, size, records, 20), 20);
    EXPECT_EQ_INT(records[0].vertexCount, 0x1000);
    EXPECT_EQ_INT(records[0].packedOffset, 0x2000);
    EXPECT_EQ_INT(records[0].finalDisplaylistOffset, 0x3000);
    EXPECT_EQ_INT(records[0].unknown1, 0);
    EXPECT_EQ_INT(records[19].vertexCount, 0x1013);
    EXPECT_EQ_INT(records[19].unknown1, 1);
}

static void test_metadata_parse_rejects_corruption(void) {
    uint8_t blob[16 + 20 * 16];
    struct CourseMetadata records[20];
    size_t size = build_metadata_blob(blob, 20);

    /* Wrong magic. */
    blob[0] = 'X';
    EXPECT_EQ_INT(course_metadata_parse(blob, size, records, 20), -1);
    build_metadata_blob(blob, 20);

    /* Unsupported version. */
    put_be32(blob + 8, 2);
    EXPECT_EQ_INT(course_metadata_parse(blob, size, records, 20), -1);
    build_metadata_blob(blob, 20);

    /* Truncated payload. */
    EXPECT_EQ_INT(course_metadata_parse(blob, size - 1, records, 20), -1);

    /* Too short for a header at all. */
    EXPECT_EQ_INT(course_metadata_parse(blob, 8, records, 20), -1);

    /* More courses than the caller can hold. */
    EXPECT_EQ_INT(course_metadata_parse(blob, size, records, 19), -1);
}

/* ---- course vertex conversion (big-endian asset form) -------------------- */

static void test_course_vertex_convert(void) {
    /* One CourseVtx in asset byte order: ob = {0x0102, -2, 0x0304},
     * tc = {0x0506, 0x0708}, ca = {0xA7, 0x5B, 0x99, 0x00}. */
    static const uint8_t src[14] = {
        0x01, 0x02, 0xFF, 0xFE, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,
        0xA7, 0x5B, 0x99, 0x00,
    };
    Vtx out[1];

    memset(out, 0, sizeof(out));
    course_vertex_convert(src, 1, out, 0, 1.0f);
    EXPECT_EQ_INT(out[0].v.ob[0], 0x0102);
    EXPECT_EQ_INT(out[0].v.ob[1], -2);
    EXPECT_EQ_INT(out[0].v.ob[2], 0x0304);
    EXPECT_EQ_INT(out[0].v.tc[0], 0x0506);
    EXPECT_EQ_INT(out[0].v.tc[1], 0x0708);
    /* Colour channels lose their low two flag bits; flags are rebuilt. */
    EXPECT_EQ_INT((uint8_t) out[0].v.cn[0], 0xA4);
    EXPECT_EQ_INT((uint8_t) out[0].v.cn[1], 0x58);
    EXPECT_EQ_INT((uint8_t) out[0].v.cn[2], 0x99);
    EXPECT_EQ_INT((uint8_t) out[0].v.cn[3], 0xFF);
    EXPECT_EQ_INT(out[0].v.flag, (0xA7 & 3) | ((0x5B << 2) & 0xC));

    /* Mirror mode negates x; stretch scales y. */
    memset(out, 0, sizeof(out));
    course_vertex_convert(src, 1, out, 1, 2.0f);
    EXPECT_EQ_INT(out[0].v.ob[0], -0x0102);
    EXPECT_EQ_INT(out[0].v.ob[1], -4);
}

/* ---- displaylist_unpack -------------------------------------------------- */

static const Gfx* unpack(const uint8_t* packed, size_t packed_size,
                         uintptr_t final_offset, u32 arg2) {
    static uint8_t copy[UNPACK_BUF_SIZE];

    memset(UNPACKED_DL_BUF, 0, sizeof(UNPACKED_DL_BUF));
    memcpy(copy, packed, packed_size);
    displaylist_unpack((uintptr_t*) copy, final_offset, arg2);
    return (const Gfx*) UNPACKED_DL_BUF;
}

static void test_unpack_terminator_only(void) {
    static const uint8_t packed[] = { 0xFF };

    unpack(packed, sizeof(packed), 0x100, 0);
    EXPECT_EQ_INT(sGfxSeekPosition, 0);
}

static void test_unpack_macro_opcodes(void) {
    /* end displaylist, set geometry mode, clear geometry mode, cull,
     * texture on, texture off, combine mode 1, render mode opaque. */
    static const uint8_t packed[] = { 0x2A, 0x56, 0x57, 0x2D, 0x26, 0x27, 0x15, 0x18, 0xFF };
    static const Gfx expected[] = {
        gsSPEndDisplayList(),
        gsSPSetGeometryMode(G_CULL_BACK),
        gsSPClearGeometryMode(G_CULL_BACK),
        gsSPCullDisplayList(0, 160),
        gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),
        gsSPTexture(0x1, 0x1, 0, G_TX_RENDERTILE, G_OFF),
        gsDPSetCombineMode(G_CC_MODULATERGBA, G_CC_MODULATERGBA),
        gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2),
    };
    const Gfx* gfx = unpack(packed, sizeof(packed), 0x100, 0);
    size_t i;

    EXPECT_EQ_INT(sGfxSeekPosition, 8);
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        EXPECT_EQ_INT(gfx[i].words.w0, expected[i].words.w0);
        EXPECT_EQ_INT(gfx[i].words.w1, expected[i].words.w1);
    }
}

static void test_unpack_lights(void) {
    /* Opcode 0x02: gsSPNumLights(NUMLIGHTS_1) then two segment-9
     * G_MOVEMEM words at 0x18-byte strides. */
    static const uint8_t packed[] = { 0x02, 0xFF };
    static const Gfx numlights[] = { gsSPNumLights(NUMLIGHTS_1) };
    const Gfx* gfx = unpack(packed, sizeof(packed), 0x100, 0);

    EXPECT_EQ_INT(sGfxSeekPosition, 3);
    EXPECT_EQ_INT(gfx[0].words.w0, numlights[0].words.w0);
    EXPECT_EQ_INT(gfx[0].words.w1, numlights[0].words.w1);
    EXPECT_EQ_INT(gfx[1].words.w0, 0x03860010);
    EXPECT_EQ_INT(gfx[1].words.w1, 0x09000008 + 2 * 0x18);
    EXPECT_EQ_INT(gfx[2].words.w0, 0x03880010);
    EXPECT_EQ_INT(gfx[2].words.w1, 0x09000000 + 2 * 0x18);
}

static void test_unpack_displaylist_reference(void) {
    /* Opcode 0x2B: the packed stream stores the target command index
     * into the unpacked segment-7 stream (byte offset / 8 on the N64).
     * The emitted address is scaled by the native Gfx size so segment-7
     * references stay correct on hosts where Gfx is wider than 8 bytes. */
    static const uint8_t packed[] = { 0x2B, 0x34, 0x12, 0xFF };
    const Gfx* gfx = unpack(packed, sizeof(packed), 0x100, 0);

    EXPECT_EQ_INT(sGfxSeekPosition, 1);
    EXPECT_EQ_INT(gfx[0].words.w0, 0x06000000);
    EXPECT_EQ_INT(gfx[0].words.w1, 0x07000000 + 0x1234 * sizeof(Gfx));
}

static void test_unpack_vertices(void) {
    /* vtx1 (0x28): addr 0x0120*0x10, count 5, dest index 2.
     * vtx2 (0x34): count = opcode - 50 = 2, addr 0x0010*0x10. */
    static const uint8_t packed[] = { 0x28, 0x20, 0x01, 0x05, 0x02, 0x34, 0x10, 0x00, 0xFF };
    const Gfx* gfx = unpack(packed, sizeof(packed), 0x100, 0);

    EXPECT_EQ_INT(sGfxSeekPosition, 2);
    EXPECT_EQ_INT(gfx[0].words.w0,
                  ((uint32_t) G_VTX << 24) | (2 * 2 << 16) | ((5 << 10) + (5 * 0x10 - 1)));
    EXPECT_EQ_INT(gfx[0].words.w1, 0x04000000 + 0x0120 * 0x10);
    EXPECT_EQ_INT(gfx[1].words.w0,
                  ((uint32_t) G_VTX << 24) | ((2 << 10) + (2 * 0x10 - 1)));
    EXPECT_EQ_INT(gfx[1].words.w1, 0x04000000 + 0x0010 * 0x10);
}

static void test_unpack_triangle(void) {
    /* Bytes 0xA5, 0x0E: v0 = 5, v1 = 5 | 16 = 21, v2 = 3 (unmirrored). */
    static const uint8_t packed[] = { 0x29, 0xA5, 0x0E, 0xFF };
    const Gfx* gfx;

    gIsMirrorMode = 0;
    gfx = unpack(packed, sizeof(packed), 0x100, 0);
    EXPECT_EQ_INT(sGfxSeekPosition, 1);
    EXPECT_EQ_INT(gfx[0].words.w0, (uint32_t) G_TRI1 << 24);
    EXPECT_EQ_INT(gfx[0].words.w1, (5 * 2 << 16) | (21 * 2 << 8) | (3 * 2));

    /* Mirror mode swaps the first and third vertex. */
    gIsMirrorMode = 1;
    gfx = unpack(packed, sizeof(packed), 0x100, 0);
    EXPECT_EQ_INT(gfx[0].words.w1, (3 * 2 << 16) | (21 * 2 << 8) | (5 * 2));
    gIsMirrorMode = 0;
}

static void test_unpack_quadrangle(void) {
    /* Two triangle-style vertex tuples: {5, 21, 3} and {1, 9, 2}
     * (second tuple packs as t2, t1, a2 in the unmirrored order). */
    static const uint8_t packed[] = { 0x58, 0xA5, 0x0E, 0x21, 0x09, 0xFF };
    const Gfx* gfx = unpack(packed, sizeof(packed), 0x100, 0);

    EXPECT_EQ_INT(sGfxSeekPosition, 1);
    EXPECT_EQ_INT(gfx[0].words.w0,
                  ((uint32_t) G_TRI2 << 24) | (5 * 2 << 16) | (21 * 2 << 8) | (3 * 2));
    EXPECT_EQ_INT(gfx[0].words.w1, (1 * 2 << 16) | (9 * 2 << 8) | (2 * 2));
}

static void test_unpack_tile_sync(void) {
    /* Opcode 0x1A: 32x32 rgba16, tmem 0; cms/masks byte 0x21, cmt/maskt
     * byte 0x43 -> gsDPTileSync, G_SETTILE, G_SETTILESIZE. */
    static const uint8_t packed[] = { 0x1A, 0x21, 0x43, 0xFF };
    static const Gfx tile_sync[] = { gsDPTileSync() };
    const Gfx* gfx = unpack(packed, sizeof(packed), 0x100, 0);
    uint32_t line = ((32 * 2) + 7) >> 3;

    EXPECT_EQ_INT(sGfxSeekPosition, 3);
    EXPECT_EQ_INT(gfx[0].words.w0, tile_sync[0].words.w0);
    EXPECT_EQ_INT(gfx[1].words.w0,
                  ((uint32_t) G_SETTILE << 24) | (0 << 21) | (G_IM_SIZ_16b_BYTES << 19)
                  | (line << 9) | 0);
    EXPECT_EQ_INT(gfx[1].words.w1, (3 << 18) | (4 << 14) | (1 << 8) | (2 << 4));
    EXPECT_EQ_INT(gfx[2].words.w0, (uint32_t) G_SETTILESIZE << 24);
    EXPECT_EQ_INT(gfx[2].words.w1, (((32 - 1) << 2) << 12) | ((32 - 1) << 2));
}

static void test_unpack_tile_load_sync(void) {
    /* Opcode 0x20: 32x32 rgba16 from segment 5. Address byte 3 selects
     * 3 << 11; one byte skipped; tmem/tile byte 0x21 -> tmem 1, tile 2. */
    static const uint8_t packed[] = { 0x20, 0x03, 0x00, 0x21, 0xFF };
    static const Gfx syncs[] = { gsDPTileSync(), gsDPLoadSync() };
    const Gfx* gfx = unpack(packed, sizeof(packed), 0x100, 0);

    EXPECT_EQ_INT(sGfxSeekPosition, 5);
    EXPECT_EQ_INT(gfx[0].words.w0,
                  ((uint32_t) G_SETTIMG << 24) | (0 << 21) | ((uint32_t) G_IM_SIZ_16b << 19));
    EXPECT_EQ_INT(gfx[0].words.w1, 0x05000000 + (3 << 11));
    EXPECT_EQ_INT(gfx[1].words.w0, syncs[0].words.w0);
    EXPECT_EQ_INT(gfx[2].words.w0,
                  ((uint32_t) G_SETTILE << 24) | (0 << 21) | ((uint32_t) G_IM_SIZ_16b << 19) | 1);
    EXPECT_EQ_INT(gfx[2].words.w1, (uint32_t) 2 << 24);
    EXPECT_EQ_INT(gfx[3].words.w0, syncs[1].words.w0);
    EXPECT_EQ_INT(gfx[4].words.w0, (uint32_t) G_LOADBLOCK << 24);
    EXPECT_EQ_INT(gfx[4].words.w1,
                  ((uint32_t) 2 << 24) | ((32 * 32 - 1) << 12)
                  | CALC_DXT(32, G_IM_SIZ_16b_BYTES));
}

/* ---- end-to-end against the generated layout ----------------------------- */

static const char* const kCourseNames[20] = {
    "mario_raceway",     "choco_mountain",  "bowsers_castle", "banshee_boardwalk",
    "yoshi_valley",      "frappe_snowland", "koopa_troopa_beach", "royal_raceway",
    "luigi_raceway",     "moo_moo_farm",    "toads_turnpike", "kalimari_desert",
    "sherbet_land",      "rainbow_road",    "wario_stadium",  "block_fort",
    "skyscraper",        "double_deck",     "dks_jungle_parkway", "big_donut",
};

static long read_file(const char* path, uint8_t* dst, size_t max) {
    FILE* f = fopen(path, "rb");
    long n;

    if (f == NULL) {
        return -1;
    }
    n = (long) fread(dst, 1, max, f);
    fclose(f);
    return n;
}

static void test_unpack_generated_layout(void) {
    static uint8_t metadata_blob[16 + 20 * 16];
    static uint8_t geography[COMP_VERT_BUF_SIZE + UNPACK_BUF_SIZE];
    struct CourseMetadata records[20];
    char path[256];
    long size;
    int i;

    size = read_file("rom_assets/course_metadata.bin", metadata_blob, sizeof(metadata_blob));
    if (size < 0) {
        printf("SKIP: rom_assets/ not generated; end-to-end unpack check skipped\n");
        return;
    }
    EXPECT_EQ_INT(course_metadata_parse(metadata_blob, (size_t) size, records, 20), 20);

    for (i = 0; i < 20; i++) {
        uint32_t packed_size;

        snprintf(path, sizeof(path), "rom_assets/%s_geography.bin", kCourseNames[i]);
        size = read_file(path, geography, sizeof(geography));
        EXPECT_TRUE(size > 0 && (uint32_t) size > records[i].packedOffset);
        packed_size = (uint32_t) size - records[i].packedOffset;
        EXPECT_TRUE(packed_size <= UNPACK_BUF_SIZE);

        memset(UNPACKED_DL_BUF, 0, sizeof(UNPACKED_DL_BUF));
        displaylist_unpack((uintptr_t*) (geography + records[i].packedOffset),
                           records[i].finalDisplaylistOffset, records[i].unknown1);

        /* finalDisplaylistOffset is the offset (in original 8-byte Gfx
         * units) of the last unpacked command, so the stream spans
         * exactly finalDisplaylistOffset + 8 bytes. Segment-7 offsets
         * inside the course display lists depend on this placement. */
        EXPECT_EQ_INT(sGfxSeekPosition * 8, (s32) records[i].finalDisplaylistOffset + 8);
    }
    printf("unpacked all 20 courses against course_metadata.bin\n");
}

int main(void) {
    test_metadata_parse_valid();
    test_metadata_parse_rejects_corruption();
    test_course_vertex_convert();
    test_unpack_terminator_only();
    test_unpack_macro_opcodes();
    test_unpack_lights();
    test_unpack_displaylist_reference();
    test_unpack_vertices();
    test_unpack_triangle();
    test_unpack_quadrangle();
    test_unpack_tile_sync();
    test_unpack_tile_load_sync();
    test_unpack_generated_layout();
    return test_finish("test_course_loading");
}
