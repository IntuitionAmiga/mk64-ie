#ifndef IE_MMIO_H
#define IE_MMIO_H

/*
 * Intuition Engine M68K MMIO constants used by this port.
 *
 * Sources of truth: the engine SDK header (IntuitionEngine
 * sdk/include/ie68.inc) and constants revalidated from the ie68-port
 * salvage branch (commit 99507c62, ie/ie_intuition.h) - only addresses
 * that appear in both are used. Grow this header per subsystem; do not
 * copy the whole map speculatively.
 */

#include <stdint.h>

/* Loader contract (engine cpu_m68k.go): raw image loaded at 0x1000,
 * PC = 0x1000, A7 = 0xFF0000, supervisor mode, no vector table needed. */
#define IE_PROGRAM_START 0x00001000u
#define IE_STACK_TOP 0x00FF0000u

/* First intercepting device aperture; the loadable image must end
 * below this. */
#define IE_APERTURE_BASE 0x000A0000u

/* Terminal character output: write one byte (as a 32-bit store).
 * IE_TERM_STATUS bit 1 = output ready. */
#define IE_TERM_OUT 0x000F0700u
#define IE_TERM_STATUS 0x000F0704u
#define IE_TERM_STATUS_OUT_READY 0x2u

/* Monotonic microseconds since engine start (64-bit split pair). */
#define IE_RTC_MONO_USEC_LO 0x000F075Cu
#define IE_RTC_MONO_USEC_HI 0x000F0760u

/* Generic platform capability bitmap. Old engines read as zero. */
#define IE_SYSINFO_FEATURES 0x000F2410u
#define IE_SYSINFO_FEATURE_WAIT 0x00000001u
#define IE_SYSINFO_FEATURE_VOODOO_CMD_STREAM 0x00000002u
#define IE_SYSINFO_FEATURE_MMIO_STATS 0x00000004u
#define IE_SYSINFO_FEATURE_VOODOO_TEX_SLOTS 0x00000008u

/* CPU wait device. Writes park the CPU goroutine inside the engine. */
#define IE_WAIT_VBLANK 0x000F2580u
#define IE_WAIT_UNTIL_LO 0x000F2584u
#define IE_WAIT_UNTIL_HI 0x000F2588u
#define IE_WAIT_UNTIL_GO 0x000F258Cu

/* Video status; bit 1 is the VBlank flag. */
#define IE_VIDEO_STATUS 0x000F0008u
#define IE_VIDEO_STATUS_VBLANK 0x2u

/* Keyboard scancode FIFO: reading IE_SCAN_CODE pops the next code
 * (XT/set-1 make codes; break = make | 0x80); IE_SCAN_STATUS bit 0
 * reports availability. */
#define IE_SCAN_CODE 0x000F0740u
#define IE_SCAN_STATUS 0x000F0744u
#define IE_SCAN_MODIFIERS 0x000F0748u
#define IE_SCAN_STATUS_READY 0x1u

/*
 * File I/O device (engine file_io_constants.go; -file-root selects the
 * host directory served to the guest). Operations are synchronous:
 * writing IE_FILE_CTRL performs the transfer. IE_FILE_READ_MAX, when
 * set non-zero before a READ, refuses files larger than the cap with
 * IE_FILE_ERR_RANGE before any bytes are copied, and is consumed by
 * that read. There is no offset register: reads are whole-file.
 */
#define IE_FILE_NAME_PTR 0x000F2200u
#define IE_FILE_DATA_PTR 0x000F2204u
#define IE_FILE_DATA_LEN 0x000F2208u
#define IE_FILE_CTRL 0x000F220Cu
#define IE_FILE_STATUS 0x000F2210u
#define IE_FILE_RESULT_LEN 0x000F2214u
#define IE_FILE_ERROR_CODE 0x000F2218u
#define IE_FILE_READ_MAX 0x000F221Cu

#define IE_FILE_OP_READ 1u
#define IE_FILE_OP_WRITE 2u

#define IE_FILE_STATUS_OK 0u

#define IE_FILE_ERR_OK 0u
#define IE_FILE_ERR_NOT_FOUND 1u
#define IE_FILE_ERR_PERMISSION 2u
#define IE_FILE_ERR_PATH_TRAVERSAL 3u
#define IE_FILE_ERR_RANGE 4u

/*
 * SFX sample-trigger block (legacy aliases: 4 channels at 0xF0E80,
 * extended aliases: 32 channels at 0xF2600, 0x20 stride; mono phase-1
 * output, pan stored but ignored). SFX_CTRL write bits:
 * trigger/stop/loop-enable; reads return status (playing/error).
 * Sample data is decoded from guest RAM; s16 samples are read
 * little-endian (device convention), so the big-endian guest stores
 * ring samples byte-swapped.
 */
#define IE_AUDIO_CTRL 0x000F0800u
#define IE_AUDIO_CTRL_ENABLE 0x1u
#define IE_AUDIO_REVERB_MIX 0x000F0A50u
#define IE_AUDIO_REVERB_DECAY 0x000F0A54u

#define IE_SFX_CH0_BASE 0x000F0E80u
#define IE_SFX_EXT_BASE 0x000F2600u
#define IE_SFX_CHANNEL_STRIDE 0x20u
#define IE_SFX_CHANNELS 32u
#define IE_SFX_CH_BASE(ch) (IE_SFX_EXT_BASE + ((uint32_t) (ch) * IE_SFX_CHANNEL_STRIDE))
#define IE_SFX_PTR (IE_SFX_CH0_BASE + 0x00u)
#define IE_SFX_LEN (IE_SFX_CH0_BASE + 0x04u)
#define IE_SFX_LOOP_PTR (IE_SFX_CH0_BASE + 0x08u)
#define IE_SFX_LOOP_LEN (IE_SFX_CH0_BASE + 0x0Cu)
#define IE_SFX_FREQ (IE_SFX_CH0_BASE + 0x10u)
#define IE_SFX_VOL (IE_SFX_CH0_BASE + 0x14u)
#define IE_SFX_PAN_RESERVED (IE_SFX_CH0_BASE + 0x16u)
#define IE_SFX_FORMAT (IE_SFX_CH0_BASE + 0x18u)
#define IE_SFX_CTRL (IE_SFX_CH0_BASE + 0x1Cu)

#define IE_SFX_OFF_PTR 0x00u
#define IE_SFX_OFF_LEN 0x04u
#define IE_SFX_OFF_LOOP_PTR 0x08u
#define IE_SFX_OFF_LOOP_LEN 0x0Cu
#define IE_SFX_OFF_FREQ 0x10u
#define IE_SFX_OFF_VOL 0x14u
#define IE_SFX_OFF_PAN_RESERVED 0x16u
#define IE_SFX_OFF_FORMAT 0x18u
#define IE_SFX_OFF_CTRL 0x1Cu

#define IE_SFX_FORMAT_SIGNED16 2u
#define IE_SFX_CTRL_TRIGGER 0x1u
#define IE_SFX_CTRL_STOP 0x2u
#define IE_SFX_CTRL_LOOP_EN 0x4u
#define IE_SFX_STATUS_PLAYING 0x1u
#define IE_SFX_STATUS_ERROR 0x2u

/* Voodoo command-stream extension: guest RAM contains big-endian
 * {absolute register address, value} u32 pairs. Writing SUBMIT=1 replays
 * COUNT pairs from PTR inside the engine's Voodoo register path. */
#define IE_VOODOO_CMD_PTR 0x000F833Cu
#define IE_VOODOO_CMD_COUNT 0x000F8340u
#define IE_VOODOO_CMD_SUBMIT 0x000F8344u
#define IE_VOODOO_CMD_SUBMIT_REPLAY 1u

/* Voodoo bulk texture upload extension: when PTR is non-zero and BYTES
 * covers width*height*4, TEX_UPLOAD copies big-endian u32 texel words directly
 * from guest RAM instead of requiring per-word texture-memory MMIO writes. */
#define IE_VOODOO_TEX_SRC_PTR 0x000F8348u
#define IE_VOODOO_TEX_SRC_BYTES 0x000F834Cu

/* Voodoo texture-slot extension (feature bit 3): TEX_SLOT selects a slot
 * id the next TEX_UPLOAD also stores to engine-side; TEX_BIND re-selects
 * a stored slot as the current texture with no data transfer, replacing
 * the full re-stream that a texture switch otherwise costs. */
#define IE_VOODOO_TEX_SLOT 0x000F8350u
#define IE_VOODOO_TEX_BIND 0x000F8354u
#define IE_VOODOO_TEX_SLOT_NONE 0xFFFFFFFFu

/*
 * Staging window for ranged asset reads (the file device has no offset
 * register, so ranges are served from a whole-file staging copy).
 * Plain guest RAM above the VideoChip VRAM aperture (0x100000-0x5FFFFF)
 * and well below the stack at 0xFF0000. 8 MiB covers the largest
 * generated asset (audiotables.bin, ~2.3 MiB) with headroom.
 */
#define IE_ASSET_STAGING_BASE 0x00600000u
#define IE_ASSET_STAGING_SIZE 0x00800000u

/*
 * Boot status block for IEScript smokes: not hardware, just a fixed
 * RAM location below the aperture base that scripts poll. Layout:
 *   +0   u32 magic 'MK64'
 *   +4   u32 check bitmask (see IE_BOOT_CHECK_*)
 *   +8   u32 heartbeat counter
 *   +12  u32 fatal marker: 'DEAD' once platform_fatal has run
 *   +16+ per-smoke payload words (documented in the smoke program)
 *
 * Perf-counter builds also publish a frame snapshot at +0x100:
 *   +0   u32 magic 'PERF'
 *   +4   u32 frame sequence
 *   +8+  u32 counters in enum IEPerfCounter order
 */
#define IE_BOOT_STATUS_ADDR 0x0009F000u
#define IE_BOOT_MAGIC 0x4D4B3634u /* 'MK64' */
#define IE_BOOT_CHECK_BSS 0x1u    /* BSS observed zero before init */
#define IE_BOOT_CHECK_DATA 0x2u   /* initialised .data value intact */
#define IE_BOOT_CHECK_STACK 0x4u  /* stack local round-trips */

#define IE_BOOT_FATAL_ADDR (IE_BOOT_STATUS_ADDR + 12u)
#define IE_BOOT_FATAL_MAGIC 0x44454144u /* 'DEAD' */

#ifdef IE_MMIO_HOST_CAPTURE
void ie_mmio_host_write32(uint32_t addr, uint32_t value);
uint32_t ie_mmio_host_read32(uint32_t addr);
void ie_mmio_host_bulk_texture_upload(const uint32_t* pixels, uint32_t count);
uint32_t ie_mmio_host_guest_addr(const void* ptr);

static inline void ie_mmio_write32(uint32_t addr, uint32_t value) {
    ie_mmio_host_write32(addr, value);
}

static inline uint32_t ie_mmio_read32(uint32_t addr) {
    return ie_mmio_host_read32(addr);
}

static inline uint32_t ie_guest_addr(const void* ptr) {
    return ie_mmio_host_guest_addr(ptr);
}
#else
static inline void ie_mmio_write32(uint32_t addr, uint32_t value) {
    *(volatile uint32_t*) (uintptr_t) addr = value;
}

static inline uint32_t ie_mmio_read32(uint32_t addr) {
    return *(volatile uint32_t*) (uintptr_t) addr;
}

static inline uint32_t ie_guest_addr(const void* ptr) {
    return (uint32_t)(uintptr_t) ptr;
}
#endif

/* Compiler barrier for MMIO transactions that hand guest-RAM buffers to an
 * engine device or coprocessor worker: without it the compiler may sink
 * buffer stores past the volatile MMIO kick, or hoist result loads above the
 * completion wait (both observed with -Ofast). No runtime cost. */
static inline void ie_compiler_barrier(void) {
    __asm__ __volatile__("" ::: "memory");
}

static inline uint32_t ie_sysinfo_features(void) {
    return ie_mmio_read32(IE_SYSINFO_FEATURES);
}

static inline uint8_t ie_feature_available(uint32_t bit) {
    return (ie_sysinfo_features() & bit) != 0;
}

#endif /* IE_MMIO_H */
