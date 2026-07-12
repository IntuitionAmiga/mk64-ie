/*
 * Link script for the full game image, linked high at 0x10000000.
 *
 * The base cannot sit lower: segmented addresses carry their segment
 * id in the top byte (0x00-0x0F), so real pointers must keep a top
 * byte above 0x0F for segmented_to_virtual's real-pointer rule -
 * 0x10000000 is the first eligible address. On Linux/macOS hosts the
 * bus backs the full 32-bit space (demand-paged), so image, heap, and
 * stack are all valid; other host platforms clamp the boot allocation
 * at 256MB and cannot run the full game image until the engine grows
 * its allocation there (recorded in STAGE3-NOTES.md).
 *
 * The game image is not booted directly: the engine always loads
 * .ie68 files at 0x1000, so a small loader image (ie/loader_main.c,
 * linked with ie/link.ld) reads this binary via the FILE MMIO device
 * to GAME_BASE and jumps to it. Entry point is the first byte
 * (ie/start_game.S must be first on the link line).
 *
 * Heap for ie/ie_malloc.c sits between the end of .bss and the stack
 * region; the stack top grows down from GAME_STACK_TOP set by
 * start_game.S.
 */

GAME_BASE = 0x10000000;
GAME_HEAP_END = 0x1F000000;   /* image + heap; stack sits above */

ENTRY(_start)

SECTIONS
{
    . = GAME_BASE;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        /*
         * Segment 2 (menu/HUD textures + display lists): on the
         * previous console port the segment base pointed at the
         * .data of exactly these two objects, laid out in ROM
         * order, so segmented 0x02xxxxxx addresses resolve into the
         * native arrays (whose names encode their segment offsets,
         * e.g. D_02007650). Keep them first and contiguous here and
         * expose the same start symbol.
         */
        __data_segment2SegmentRomStart = .;
        */src/data/textures.o(.data)
        */src/data/data_segment2.o(.data)
        __data_segment2SegmentRomEnd = .;
        *(.data)
        *(.data.*)
    }

    __load_end = .;

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    __heap_start = ALIGN(16);
    __heap_end = GAME_HEAP_END;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}

ASSERT(__heap_start < GAME_HEAP_END, "game image + bss overflow the heap region")
