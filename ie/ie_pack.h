/*
 * ie/ie_pack.h - self-contained mariokart64.ie68 pack format.
 *
 * IE loads the whole .ie68 file linearly at M68K_ENTRY_POINT (0x1000): byte at
 * file offset F lands at memory IE_LOAD_BASE + F. The device apertures
 * (VGA/Voodoo/MMIO/VideoChip VRAM) occupy 0xA0000-0x5FFFFF, swallowing any
 * file bytes that map there, and the guest stack grows down from just below
 * 0xFF0000. The pack therefore uses three placements:
 *
 *   boot loader:  file 0x000000, mem 0x001000 (low RAM window)
 *   header + TOC: file 0x5FF000, mem 0x600000 (past the apertures; this small
 *                 block survives into game runtime, where the asset layer
 *                 reads it)
 *   asset blobs + game image:
 *                 file 0xFFF000+, mem 0x1000000+ (the pack data window:
 *                 above the stack and below the Voodoo texture store at
 *                 0x08000000)
 *
 * The loader copies the game image from its loaded address to 0x10000000 and
 * jumps; asset blobs are served in place from the in-RAM TOC - no File I/O,
 * so the image is truly self-contained and runs from any directory.
 *
 * All multi-byte fields are BIG-ENDIAN (68k native).
 */
#ifndef IE_PACK_H
#define IE_PACK_H

#include <stdint.h>

/* Where IE deposits file offset 0 (M68K_ENTRY_POINT): mem = IE_LOAD_BASE + foff. */
#define IE_LOAD_BASE          0x00001000u

/* Pack header file offset and its resulting RAM address (mem 0x600000). */
#define IE_PACK_HDR_FOFF      0x005FF000u
#define IE_PACK_HDR_ADDR      (IE_LOAD_BASE + IE_PACK_HDR_FOFF)

/* First file offset of the blob/game region (mem 0x1000000). */
#define IE_PACK_DATA_FOFF     0x00FFF000u

/* Last file offset allowed in the blob/game region, exclusive.
 * This maps to mem 0x08000000, where the Voodoo texture store begins. */
#define IE_PACK_DATA_LIMIT_FOFF 0x07FFF000u

#define IE_PACK_MAGIC0        0x4D4B3634u    /* "MK64" */
#define IE_PACK_MAGIC1        0x50414B32u    /* "PAK2" (v2: high-window data) */

#define IE_PACK_NAME_LEN      48

/* One asset entry in the on-file table of contents. */
typedef struct {
    char     name[IE_PACK_NAME_LEN]; /* raw asset name, e.g. "audiobanks.bin";
                                        the coprocessor service is "svc/tnl.ie64" */
    uint32_t offset;                 /* file offset of the blob (= mem - IE_LOAD_BASE) */
    uint32_t size;                   /* blob length in bytes                  */
} ie_pack_entry;

/* On-file pack header, immediately followed by toc_count ie_pack_entry. */
typedef struct {
    uint32_t magic0;       /* IE_PACK_MAGIC0 */
    uint32_t magic1;       /* IE_PACK_MAGIC1 */
    uint32_t game_offset;  /* file offset of the game image  */
    uint32_t game_size;    /* game image length              */
    uint32_t toc_count;    /* number of ie_pack_entry that follow */
} ie_pack_header;

#endif /* IE_PACK_H */
