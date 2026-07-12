#ifndef AUDIO_SEQFILE_H
#define AUDIO_SEQFILE_H

/*
 * ALSeqFile handling for the big-endian audio assets (audiobanks.bin,
 * audiotables.bin, sequences.bin). The stored form is always ROM byte
 * order: a 16-bit revision, a 16-bit entry count, then per entry a
 * 32-bit offset and 32-bit length. These helpers read that form with
 * explicit byte order and expand it in place into native ALSeqFile
 * records (which are wider than the stored records when pointers are
 * wider than 32 bits).
 */

#include <stdint.h>

#include <PR/ultratypes.h>
#include <PR/libaudio.h>

#define AL_SEQ_FILE_RAW_HEADER_SIZE 4
#define AL_SEQ_FILE_RAW_ENTRY_SIZE 8

/* Entry count of a raw header (needs only the first 4 bytes). */
u16 al_seq_file_seq_count(const void* raw_header);

/* Byte size of the whole raw image for a raw header's entry count. */
u32 al_seq_file_raw_size(const void* raw_header);

/*
 * Expand the raw image at f into native ALSeqFile records in place and
 * relocate every non-empty entry's offset against base. The buffer at
 * f must have room for the native form:
 * sizeof(ALSeqFile) + (count - 1) * sizeof(ALSeqData).
 */
void al_seq_file_patch(ALSeqFile* f, u8* base);

#endif /* AUDIO_SEQFILE_H */
