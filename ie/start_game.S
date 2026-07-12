/*
 * Entry stub for the high-linked game image.
 *
 * The loader image (ie/loader_main.c) reads this binary to GAME_BASE
 * (0x10000000, ie/game.ld) via the FILE MMIO device and jumps to its
 * first byte. Same conventions as ie/start.S, but the stack moves to
 * a dedicated region above the heap so the ~1.9MB low window at
 * 0xFF0000 is not a constraint on the game's call depth. Everything
 * stays below the engine's 256MB boot-allocation clamp.
 */

#ifdef __ELF__
#define CSYM(name) name
#else
#define CSYM(name) _##name
#endif

/* Stack top: plain RAM above the heap region (ie/game.ld ends the
 * heap at 0x1F000000), kept clear of the 0x20000000 boundary rather
 * than sitting exactly on it. */
#define GAME_STACK_TOP 0x1FF00000

    .text
    .globl start
    .globl _start
start:
_start:
    move.w  #0x2700, %sr
    move.l  #GAME_STACK_TOP, %sp
    jsr     CSYM(ie_clear_bss)
    jsr     CSYM(ie_game_main)

halt_loop:
    stop    #0x2700
    bra.s   halt_loop
