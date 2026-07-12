/*
 * Host tests for the audio table seam (src/audio/seqfile.c): ALSeqFile
 * headers as stored in the big-endian assets (audiobanks.bin,
 * audiotables.bin, sequences.bin) are parsed with explicit byte order
 * and expanded in place into native ALSeqFile records.
 */
#include "test_support.h"

#include <stdint.h>

#include <ultra64.h>
#include <PR/libaudio.h>

#include "audio/seqfile.h"

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t) v;
}

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t) v;
}

/* Raw big-endian ALSeqFile image: revision, count, then per entry a
 * 32-bit offset and 32-bit length. */
static size_t build_raw_seqfile(uint8_t* raw, uint16_t revision,
                                const uint32_t* offsets, const uint32_t* lengths,
                                uint16_t count) {
    uint16_t i;

    put_be16(raw + 0, revision);
    put_be16(raw + 2, count);
    for (i = 0; i < count; i++) {
        put_be32(raw + 4 + i * 8 + 0, offsets[i]);
        put_be32(raw + 4 + i * 8 + 4, lengths[i]);
    }
    return AL_SEQ_FILE_RAW_HEADER_SIZE + count * AL_SEQ_FILE_RAW_ENTRY_SIZE;
}

static void test_seq_count_read(void) {
    uint8_t raw[4];

    put_be16(raw + 0, 1);
    put_be16(raw + 2, 43);
    EXPECT_EQ_INT(al_seq_file_seq_count(raw), 43);
}

static void test_patch_expands_in_place(void) {
    /* The buffer holds the raw image at its start and must be large
     * enough for the native record form (wider on 64-bit hosts). */
    static const uint32_t offsets[3] = { 0x100, 0x0, 0x2340 };
    static const uint32_t lengths[3] = { 0x40, 0x0, 0x1000 };
    uint8_t buf[sizeof(ALSeqFile) + 3 * sizeof(ALSeqData)];
    uint8_t* base = (uint8_t*) 0x10000;
    ALSeqFile* f = (ALSeqFile*) buf;

    memset(buf, 0, sizeof(buf));
    build_raw_seqfile(buf, 2, offsets, lengths, 3);
    al_seq_file_patch(f, base);

    EXPECT_EQ_INT(f->revision, 2);
    EXPECT_EQ_INT(f->seqCount, 3);
    /* Entries with a length are relocated against base... */
    EXPECT_TRUE(f->seqArray[0].offset == base + 0x100);
    EXPECT_EQ_INT(f->seqArray[0].len, 0x40);
    EXPECT_TRUE(f->seqArray[2].offset == base + 0x2340);
    EXPECT_EQ_INT(f->seqArray[2].len, 0x1000);
    /* ...zero-length entries stay as raw values (they alias another
     * entry by index in the original data). */
    EXPECT_TRUE(f->seqArray[1].offset == (u8*) 0);
    EXPECT_EQ_INT(f->seqArray[1].len, 0);
}

static void test_patch_single_entry(void) {
    static const uint32_t offsets[1] = { 8 };
    static const uint32_t lengths[1] = { 16 };
    uint8_t buf[sizeof(ALSeqFile) + sizeof(ALSeqData)];
    ALSeqFile* f = (ALSeqFile*) buf;

    memset(buf, 0, sizeof(buf));
    build_raw_seqfile(buf, 1, offsets, lengths, 1);
    al_seq_file_patch(f, (u8*) 0);
    EXPECT_EQ_INT(f->seqCount, 1);
    EXPECT_TRUE(f->seqArray[0].offset == (u8*) 8);
    EXPECT_EQ_INT(f->seqArray[0].len, 16);
}

int main(void) {
    test_seq_count_read();
    test_patch_expands_in_place();
    test_patch_single_entry();
    return test_finish("test_seqfile");
}
