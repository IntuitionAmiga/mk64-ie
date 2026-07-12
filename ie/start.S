/*
 * Intuition Engine M68K entry stub.
 *
 * The engine loads the raw image at 0x1000 and starts executing at its
 * first byte in supervisor mode with A7 = 0xFF0000 already set; this
 * stub must therefore be the first thing in the image (the link script
 * keeps .text.boot first).
 *
 * Structure salvaged from the ie68-port branch (commit 99507c62,
 * ie/start.S) and revalidated against the current engine: the stack
 * pointer is set explicitly anyway for determinism, and the BSS clear
 * is delegated to a C helper - a tight assembly clear loop over RAM is
 * miscompiled by the engine's M68K JIT (stores silently dropped), a
 * hazard documented on that branch.
 */

/* m68k-atari-mint-gcc emits a.out objects where C symbols carry a
 * leading underscore; plain ELF toolchains do not. */
#ifdef __ELF__
#define CSYM(name) name
#else
#define CSYM(name) _##name
#endif

/* a.out has no named sections; this object is listed first on the link
 * line so the entry stub is the first byte of the image. The entry
 * label is exported under both the a.out (_start) and ELF (start)
 * spellings so the link script's ENTRY resolves with either toolchain. */
    .text
    .globl start
    .globl _start
start:
_start:
    move.w  #0x2700, %sr            /* supervisor, interrupts masked */
    move.l  #0x00FF0000, %sp        /* IE_STACK_TOP; loader default, set anyway */
    jsr     CSYM(ie_clear_bss)
    jsr     CSYM(ie_main)

halt_loop:
    stop    #0x2700
    bra.s   halt_loop
