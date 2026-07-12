#ifndef PLATFORM_H
#define PLATFORM_H

/*
 * Platform-neutral service contracts.
 *
 * This header is the only place shared game code may express a dependency
 * on the underlying platform. No backend implementation lives in this
 * branch: the previous console backend has been removed, and the
 * Intuition Engine backend is added in a later stage behind these same
 * contracts. A minimal host implementation may exist for focused tests
 * only (see tests/).
 *
 * Every function here must fail explicitly (return failure or abort via
 * platform_fatal) rather than silently pretending to succeed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Debug/log output ------------------------------------------------- */

void platform_log(const char* fmt, ...);
void platform_fatal(const char* fmt, ...) __attribute__((noreturn));

/* ---- Timing ------------------------------------------------------------ */

/* Monotonic time since an arbitrary epoch, in nanoseconds. */
uint64_t platform_time_ns(void);

/* ---- Asset/file access -------------------------------------------------- */

/*
 * Read an asset identified by a platform-independent name (for example
 * "audiobanks.bin") into dst, reading at most max_size bytes. Returns true
 * on success and stores the number of bytes read in *size_out when it is
 * non-NULL. Path layout, storage medium, and lookup are backend decisions.
 */
bool platform_asset_read(const char* name, void* dst, size_t max_size, size_t* size_out);

/*
 * Read up to nbytes of an asset starting at byte offset. Returns true on
 * success and stores the number of bytes actually read in *bytes_read when
 * it is non-NULL (short reads at end-of-asset are not an error).
 */
bool platform_asset_read_range(const char* name, size_t offset, void* dst, size_t nbytes, size_t* bytes_read);

/* ---- Persistent save storage (EEPROM-sized records) --------------------- */

bool platform_save_probe(void);
bool platform_save_read(size_t offset, void* dst, size_t nbytes);
bool platform_save_write(size_t offset, const void* src, size_t nbytes);

/* ---- Ghost-data storage (controller-pak replacement) -------------------- */

bool platform_ghost_present(void);
bool platform_ghost_load(size_t offset, void* dst, size_t nbytes);
bool platform_ghost_store(size_t offset, const void* src, size_t nbytes);
bool platform_ghost_delete(void);

/* ---- Input --------------------------------------------------------------
 *
 * The backend reports controller state already translated to the N64
 * button layout (CONT_* bits from <PR/os_cont.h>) and N64 stick range
 * (roughly -80..80 on each axis). Button-mapping decisions belong to the
 * backend, not to shared game code.
 */

struct PlatformControllerState {
    bool connected;
    uint16_t buttons;
    int8_t stick_x;
    int8_t stick_y;
};

bool platform_poll_controller(int port, struct PlatformControllerState* state);

/* ---- Backend API registration -------------------------------------------
 *
 * The platform backend installs its renderer, window-manager, and audio
 * implementations before calling main(). gfx_init/_AudioInit fail
 * explicitly if a backend forgets.
 */

struct GfxWindowManagerAPI;
struct GfxRenderingAPI;
struct AudioAPI;

void platform_install_apis(struct GfxWindowManagerAPI* wm,
                           struct GfxRenderingAPI* rendering,
                           struct AudioAPI* audio);

/* ---- Main loop / audio pump ---------------------------------------------
 *
 * Scheduling contract: the game is single-threaded. The backend drives it
 * by calling, once per video frame:
 *
 *   game_loop_one_iteration();   (defined in src/main.c)
 *
 * and, once per vblank (audio must not starve while the game loop runs):
 *
 *   game_audio_pump();           (defined in src/main.c)
 *
 * The previous console port used a helper thread woken from the vblank
 * interrupt for the audio pump; an equivalent arrangement is the backend's
 * job.
 */

#endif /* PLATFORM_H */
