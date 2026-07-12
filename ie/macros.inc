# Assembly macros for the IE m68k (a.out) build of the data/*.s
# incbin segments. a.out C symbols carry a leading underscore, so
# glabel exports both spellings; include/macros.inc stays MIPS-only.

.macro glabel label
    .global \label
    .global _\label
    .balign 4
\label:
_\label:
.endm
