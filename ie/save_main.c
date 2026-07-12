/*
 * Stage 3 save/ghost storage smoke program.
 *
 * Runs twice against the same file-root scratch directory. The phase
 * is detected from the save content itself: a fresh record (no 'SAV1'
 * header) is phase 1 - write patterns and verify them in-RAM; a
 * persisted record is phase 2 - verify the patterns survived the
 * reboot, then exercise ghost delete.
 *
 * Status block payload (common fields in ie/ie_mmio.h):
 *   +16  u32 phase (1 or 2)
 *   +20  u32 check bitmask (phase 1 wants 0x1F, phase 2 wants 0x1F)
 */

#include <stdint.h>

#include "platform/platform.h"

#include "ie_mmio.h"

#define SAVE_MAGIC 0x53415631u /* 'SAV1' */

/* Phase 1 checks. */
#define P1_PROBE 0x01u      /* storage probes OK */
#define P1_FRESH 0x02u      /* fresh save reads back zeroed */
#define P1_SAVE_RT 0x04u    /* save write + read round-trips in-boot */
#define P1_GHOST_RT 0x08u   /* ghost store + load round-trips in-boot */
#define P1_BOUNDS 0x10u     /* out-of-range accesses are refused */

/* Phase 2 checks. */
#define P2_SAVE_KEPT 0x01u  /* save pattern survived the reboot */
#define P2_GHOST_KEPT 0x02u /* ghost pattern survived the reboot */
#define P2_DELETE 0x04u     /* ghost delete makes loads fail */
#define P2_REWRITE 0x08u    /* save still writable after reload */
#define P2_BOUNDS 0x10u     /* out-of-range accesses are refused */

static uint8_t save_buf[512];
static uint8_t ghost_buf[512];

static uint8_t save_pattern(uint32_t i) {
    return (uint8_t)(0xA5u ^ i);
}

static uint8_t ghost_pattern(uint32_t i) {
    return (uint8_t)(0x5Au + 3u * i);
}

static uint32_t be32_of(const uint8_t* p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static int bounds_refused(void) {
    return !platform_save_read(508, save_buf, 8) && !platform_save_write(512, save_buf, 1) &&
           !platform_ghost_load(32764, ghost_buf, 8) && !platform_ghost_store(32768, ghost_buf, 1);
}

static uint32_t run_phase1(void) {
    uint32_t checks = 0;
    uint32_t i;
    int ok;

    checks |= P1_PROBE; /* probe already succeeded to get here */

    ok = platform_save_read(0, save_buf, sizeof(save_buf));
    if (ok) {
        for (i = 0; i < sizeof(save_buf); i++) {
            if (save_buf[i] != 0) {
                ok = 0;
                break;
            }
        }
    }
    if (ok) {
        checks |= P1_FRESH;
    }

    /* Save record: 'SAV1' then a byte pattern, via two offset writes. */
    save_buf[0] = 'S';
    save_buf[1] = 'A';
    save_buf[2] = 'V';
    save_buf[3] = '1';
    for (i = 4; i < sizeof(save_buf); i++) {
        save_buf[i] = save_pattern(i);
    }
    if (platform_save_write(0, save_buf, 256) && platform_save_write(256, save_buf + 256, 256)) {
        uint8_t back[512];
        ok = platform_save_read(0, back, sizeof(back));
        for (i = 0; ok && i < sizeof(back); i++) {
            if (back[i] != save_buf[i]) {
                ok = 0;
            }
        }
        if (ok) {
            checks |= P1_SAVE_RT;
        }
    }

    /* Ghost record: pattern at both ends of the 32 KiB blob. */
    for (i = 0; i < sizeof(ghost_buf); i++) {
        ghost_buf[i] = ghost_pattern(i);
    }
    if (platform_ghost_store(0, ghost_buf, sizeof(ghost_buf)) &&
        platform_ghost_store(32768 - 512, ghost_buf, 512)) {
        uint8_t back[512];
        ok = platform_ghost_load(32768 - 512, back, sizeof(back));
        for (i = 0; ok && i < sizeof(back); i++) {
            if (back[i] != ghost_pattern(i)) {
                ok = 0;
            }
        }
        if (ok) {
            checks |= P1_GHOST_RT;
        }
    }

    if (bounds_refused()) {
        checks |= P1_BOUNDS;
    }
    return checks;
}

static uint32_t run_phase2(void) {
    uint32_t checks = 0;
    uint32_t i;
    int ok;

    /* Save pattern survived the reboot (header verified by caller). */
    ok = 1;
    for (i = 4; i < sizeof(save_buf); i++) {
        if (save_buf[i] != save_pattern(i)) {
            ok = 0;
            break;
        }
    }
    if (ok) {
        checks |= P2_SAVE_KEPT;
    }

    if (platform_ghost_present() && platform_ghost_load(32768 - 512, ghost_buf, sizeof(ghost_buf))) {
        ok = 1;
        for (i = 0; i < sizeof(ghost_buf); i++) {
            if (ghost_buf[i] != ghost_pattern(i)) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            checks |= P2_GHOST_KEPT;
        }
    }

    if (platform_ghost_delete() && !platform_ghost_load(0, ghost_buf, 4)) {
        checks |= P2_DELETE;
    }

    save_buf[4] = 0xEE;
    if (platform_save_write(4, save_buf + 4, 1) && platform_save_read(4, save_buf + 8, 1) &&
        save_buf[8] == 0xEE) {
        checks |= P2_REWRITE;
    }

    if (bounds_refused()) {
        checks |= P2_BOUNDS;
    }
    return checks;
}

void ie_main(void) {
    volatile uint32_t* status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;
    uint32_t phase;
    uint32_t checks = 0;

    platform_log("MK64-IE68 STAGE3 SAVE smoke");

    if (!platform_save_probe() || !platform_save_read(0, save_buf, sizeof(save_buf))) {
        platform_fatal("save storage unavailable");
    }

    phase = (be32_of(save_buf) == SAVE_MAGIC) ? 2 : 1;
    checks = (phase == 1) ? run_phase1() : run_phase2();

    platform_log("save smoke phase %u checks 0x%02x", phase, checks);

    status[4] = phase;
    status[5] = checks;
    status[1] = 0;
    status[2] = 0;
    status[0] = IE_BOOT_MAGIC;

    for (;;) {
    }
}
