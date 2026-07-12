ENTRY(_start)
PROGRAM_START = 0x00420000;
PROGRAM_END = 0x004A0000;
SECTIONS
{
    . = PROGRAM_START;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    __load_end = .;
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }
    __image_end = .;
    /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr) }
}
ASSERT(__image_end <= PROGRAM_END - 0x100, "M68K service exceeds worker RAM")
