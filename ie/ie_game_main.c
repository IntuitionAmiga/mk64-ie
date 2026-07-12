/*
 * Full-game entry point for the IE backend (subsystem 9).
 *
 * Runs from the high-linked game image (ie/game.ld, boots via
 * ie/loader_main.c). Installs the IE backends behind the neutral
 * platform contracts and hands control to the game's own main().
 *
 * Scheduling contract (src/platform/platform.h): the game is
 * single-threaded and loops inside thread5_game_loop; the audio pump
 * must run once per 60Hz vblank. The window backend calls the frame
 * hook from swap_buffers_end, which the game reaches exactly once per
 * frame via display_and_vsync, so the pump is registered there.
 */

#include <stdint.h>

#include "gfx/gfx_rendering_api.h"
#include "gfx/gfx_window_manager_api.h"
#include "audio/audio_api.h"

#include "platform/platform.h"

extern struct GfxRenderingAPI ie_gfx_voodoo_api;
extern struct GfxWindowManagerAPI ie_gfx_window_api;
extern struct AudioAPI ie_audio_api;

void ie_gfx_window_set_frame_hook(void (*hook)(void));
void ie_gfx_voodoo_set_flush_hook(void (*hook)(void));
void ie_asset_set_io_hook(void (*hook)(void));
void ie_audio_set_pump(void (*pump)(void));
void ie_audio_poll(void);
void game_audio_pump(void);

extern int main(int argc, char** argv);

/* main() runs setup_audio_data (which initialises the audio backend)
 * before thread5_game_loop produces the first frame, so the pump is
 * always safe by the time the first poll fires. Audio pumping runs
 * through ie_audio_poll (renderer flushes, asset loads, and the
 * per-frame hook all call it), keeping the ring fed even while the
 * game loop is busy inside one frame. */
void ie_game_main(void) {
    platform_log("MK64-IE68 game image booting");

#ifdef IE_TNL_COPROC
    /* COSTART the x86 T&L coprocessor. On failure (service file absent,
     * engine without coprocessor support) every dispatch falls back to
     * the local vertex path, so this is best-effort by design. */
    {
        extern int ie_coproc_init(void);
        platform_log(ie_coproc_init() ? "TnL coprocessor online"
                                      : "TnL coprocessor unavailable, local path");
    }
#endif

    ie_audio_set_pump(game_audio_pump);
    ie_gfx_window_set_frame_hook(ie_audio_poll);
    /* NOT the renderer flush hook: draw-heavy frames (the title
     * background is ~1200 tile rectangles) flush per tile, and a poll
     * window per flush turns one frame into minutes of interleaved
     * synthesis. Slow frames instead take an audio dropout (the poll
     * resyncs its baseline at the next frame boundary). */
    ie_asset_set_io_hook(ie_audio_poll);
    platform_install_apis(&ie_gfx_window_api, &ie_gfx_voodoo_api, &ie_audio_api);

    main(0, (char**) 0);
    platform_fatal("game main returned");
}
