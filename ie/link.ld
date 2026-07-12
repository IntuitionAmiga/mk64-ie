/*
 * Link script for bare Intuition Engine M68K images (Stage 3 runtime
 * skeleton).
 *
 * The engine loads the raw binary at 0x1000 and jumps to its first
 * byte; everything loadable must end below the first device aperture
 * at 0xA0000. BSS is not part of the file - the runtime clears it at
 * boot. Layout salvaged from the ie68-port branch (commit 99507c62,
 * ie/link.ld) and revalidated against the engine's loader contract.
 *
 * The 0x9F000..0x9FFFF window is reserved for the boot status block
 * that IEScript smokes poll (see ie/ie_mmio.h).
 */

ENTRY(_start)

PROGRAM_START = 0x00001000;
BOOT_STATUS_BASE = 0x0009F000;

SECTIONS
{
    . = PROGRAM_START;

    /* Input order matters: the entry stub's object is listed first on
     * the link line, making it the first byte of the image. */
    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    __load_end = .;

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    __image_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
    }
}

/* Loadable bytes and BSS must both stay below the status block and the
 * device apertures. */
ASSERT(__load_end <= BOOT_STATUS_BASE, "image overflows into the boot status block")
ASSERT(__image_end <= BOOT_STATUS_BASE, "BSS overflows into the boot status block")
