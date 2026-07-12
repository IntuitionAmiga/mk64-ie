#include "seqfile.h"

#include "asset_endian.h"

/*
 * This is the byte-order-explicit replacement for the original game's
 * alSeqFileNew (and the previous port's func_800BB43C, which assumed a
 * little-endian host and a partially pre-swapped sequence table). The
 * stored form is always big-endian; parsing works on any host byte
 * order, and expansion runs backwards so the native records can be
 * wider than the stored ones inside the same buffer.
 */

u16 al_seq_file_seq_count(const void* raw_header) {
    return asset_be_u16((const u8*) raw_header + 2);
}

u32 al_seq_file_raw_size(const void* raw_header) {
    return AL_SEQ_FILE_RAW_HEADER_SIZE
         + (u32) al_seq_file_seq_count(raw_header) * AL_SEQ_FILE_RAW_ENTRY_SIZE;
}

void al_seq_file_patch(ALSeqFile* f, u8* base) {
    u8* raw = (u8*) f;
    s16 revision = (s16) asset_be_u16(raw + 0);
    u16 count = asset_be_u16(raw + 2);
    s32 i;

    for (i = (s32) count - 1; i >= 0; i--) {
        const u8* entry = raw + AL_SEQ_FILE_RAW_HEADER_SIZE + i * AL_SEQ_FILE_RAW_ENTRY_SIZE;
        u32 offset = asset_be_u32(entry + 0);
        s32 len = (s32) asset_be_u32(entry + 4);

        if (len != 0) {
            f->seqArray[i].offset = base + offset;
        } else {
            /* Empty entries alias another entry by index; keep raw. */
            f->seqArray[i].offset = (u8*) (uintptr_t) offset;
        }
        f->seqArray[i].len = len;
    }
    f->revision = revision;
    f->seqCount = (s16) count;
}
