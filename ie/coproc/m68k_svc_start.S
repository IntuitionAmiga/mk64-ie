#ifdef __ELF__
#define CSYM(name) name
#else
#define CSYM(name) _##name
#endif

/* Stack top defaults to the first M68K worker window; the second-instance
 * service (audiosvc, window 0x420000-0x49FFFF) overrides it at compile
 * time. Keep in sync with the matching linker script's PROGRAM_END. */
#ifndef IE_SVC_STACK_TOP
#define IE_SVC_STACK_TOP 0x002fff00
#endif

    .text
    .globl start
    .globl _start
start:
_start:
    move.w  #0x2700, %sr
    move.l  #IE_SVC_STACK_TOP, %sp
    /* createM68KWorker zeroes the complete 512 KiB window before copying the
     * image, so BSS is already clear. Avoid the main-runtime clear loop here;
     * worker JIT execution has no need to touch it. */
    jsr     CSYM(ie_main)
1:
    bra.s   1b
