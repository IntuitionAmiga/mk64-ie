/*
 * IE window-manager backend: implements the neutral
 * GfxWindowManagerAPI (src/gfx/gfx_window_manager_api.h). The engine
 * owns the actual window/compositor; this backend only reports the
 * Voodoo framebuffer dimensions and paces frames on VBlank.
 */

#include <stddef.h>
#include <stdint.h>

#include "gfx/gfx_window_manager_api.h"

#include "ie_mmio.h"
#include "ie_perf_counters.h"

/* Frame heartbeat for harness scripts: swap count at status +16,
 * start-frame count at +20 (see boot status block in ie_mmio.h). */
static volatile uint32_t* const wm_status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;

uint64_t ie_time_usec(void);
void ie_video_wait_vblank(void);

#define IE_GFX_WIDTH 640u
#define IE_GFX_HEIGHT 480u

/* N64 frame period; the game's logic and audio cadence assume 60Hz
 * while the headless VideoChip vblanks at ~120Hz. */
#define IE_FRAME_USEC 16667u

/* Per-frame hook (the game image installs its audio pump here: the
 * scheduling contract wants one game_audio_pump per 60Hz vblank). */
static void (*frame_hook)(void);

void ie_gfx_window_set_frame_hook(void (*hook)(void)) {
    frame_hook = hook;
}

static void wm_init(const char* game_name, uint8_t start_in_fullscreen) {
    (void) game_name;
    (void) start_in_fullscreen;
}

static void wm_set_keyboard_callbacks(uint8_t (*on_key_down)(int scancode),
                                      uint8_t (*on_key_up)(int scancode),
                                      void (*on_all_keys_up)(void)) {
    /* Input flows through platform_poll_controller, not key callbacks. */
    (void) on_key_down;
    (void) on_key_up;
    (void) on_all_keys_up;
}

static void wm_set_fullscreen_changed_callback(void (*cb)(uint8_t is_now_fullscreen)) {
    (void) cb;
}

static void wm_set_fullscreen(uint8_t enable) {
    (void) enable;
}

static void wm_main_loop(void (*run_one_game_iter)(void)) {
    for (;;) {
        run_one_game_iter();
    }
}

static void wm_get_dimensions(uint32_t* width, uint32_t* height) {
    *width = IE_GFX_WIDTH;
    *height = IE_GFX_HEIGHT;
}

static void wm_handle_events(void) {
}

static uint8_t wm_start_frame(void) {
    wm_status[5]++;
    return 1;
}

static void wm_swap_buffers_begin(void) {
}

static void wm_swap_buffers_end(void) {
    static uint64_t next_deadline;
    uint64_t now;

    wm_status[4]++;
    wm_status[0] = IE_BOOT_MAGIC;

    if (frame_hook != NULL) {
        frame_hook();
    }
    ie_perf_frame_end();

    /* Frame pacing: RTC-paced to the 60Hz game cadence, then aligned
     * to the next VBlank so swaps stay tear-consistent. Both apply
     * only when the frame finished inside its 60Hz slot: when the game
     * runs behind, pacing must not add sleep on top of the late frame,
     * and VBlank alignment would quantise the frame time up to the
     * next whole VBlank period, hiding any speed gain smaller than a
     * full period. Behind schedule we swap immediately and resync the
     * deadline to now, so no debt accumulates either way. */
    now = ie_time_usec();
    if (next_deadline != 0 && now < next_deadline + IE_FRAME_USEC) {
        next_deadline += IE_FRAME_USEC;
        if (ie_feature_available(IE_SYSINFO_FEATURE_WAIT)) {
            ie_mmio_write32(IE_WAIT_UNTIL_LO, (uint32_t) next_deadline);
            ie_mmio_write32(IE_WAIT_UNTIL_HI, (uint32_t)(next_deadline >> 32));
            ie_mmio_write32(IE_WAIT_UNTIL_GO, 1);
        } else {
            while (ie_time_usec() < next_deadline) {
            }
        }
        ie_video_wait_vblank();
        /* The frame really ends at the VBlank edge, past the RTC
         * deadline by up to one VBlank period. Pace the next frame
         * from that actual end, or a short next frame would compare
         * against the stale pre-VBlank target, take the late branch,
         * and swap faster than the 60Hz cadence. */
        now = ie_time_usec();
        if (now > next_deadline) {
            next_deadline = now;
        }
    } else {
        next_deadline = now;
    }
}

static double wm_get_time(void) {
    return (double) ie_time_usec() / 1000000.0;
}

struct GfxWindowManagerAPI ie_gfx_window_api = {
    wm_init,
    wm_set_keyboard_callbacks,
    wm_set_fullscreen_changed_callback,
    wm_set_fullscreen,
    wm_main_loop,
    wm_get_dimensions,
    wm_handle_events,
    wm_start_frame,
    wm_swap_buffers_begin,
    wm_swap_buffers_end,
    wm_get_time,
};
