/*
 * Stage 3 audio sink smoke program.
 *
 * Starts the neutral AudioAPI backend and streams a sustained 440 Hz
 * square tone through it in game-sized chunks, pacing by the ring
 * headroom exactly the way the game's audio pump will. Ring health is
 * published for the smoke script.
 *
 * Status block payload (common fields in ie/ie_mmio.h):
 *   +16  u32 SFX channel status (want playing=1, error=0)
 *   +20  u32 total mono samples written
 *   +24  u32 current buffered estimate (samples)
 *   +28  u32 underrun events (buffered hit zero after start-up)
 */

#include <stdint.h>

#include "audio/audio_api.h"

#include "platform/platform.h"

#include "ie_mmio.h"

extern struct AudioAPI ie_audio_api;
uint32_t ie_audio_total_written(void);
uint32_t ie_audio_status(void);
uint64_t ie_time_usec(void);

#define SAMPLES_PER_CHUNK 448u
#define TONE_HZ 440u
#define RATE_HZ 13400u /* must match IE_AUDIO_RATE in ie_platform_audio.c */

static uint8_t chunk[SAMPLES_PER_CHUNK * 4]; /* interleaved stereo s16 */

static void fill_chunk(void) {
    static uint32_t phase;
    uint32_t i;

    for (i = 0; i < SAMPLES_PER_CHUNK; i++) {
        int16_t v = (phase < RATE_HZ / TONE_HZ / 2) ? 12000 : -12000;

        phase++;
        if (phase >= RATE_HZ / TONE_HZ) {
            phase = 0;
        }
        /* Big-endian stereo s16, as the mixer produces. */
        chunk[i * 4] = (uint8_t)((uint16_t) v >> 8);
        chunk[i * 4 + 1] = (uint8_t) v;
        chunk[i * 4 + 2] = (uint8_t)((uint16_t) v >> 8);
        chunk[i * 4 + 3] = (uint8_t) v;
    }
}

void ie_main(void) {
    volatile uint32_t* status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;
    uint32_t underruns = 0;
    uint32_t warmed_up = 0;
    uint32_t beat = 0;

    platform_log("MK64-IE68 STAGE3 AUDIO smoke");

    if (!ie_audio_api.init()) {
        platform_fatal("audio backend failed to initialise");
    }

    status[1] = 0;
    status[0] = IE_BOOT_MAGIC;

    for (;;) {
        int buffered = ie_audio_api.buffered();

        if (buffered < ie_audio_api.get_desired_buffered()) {
            fill_chunk();
            ie_audio_api.play(chunk, sizeof(chunk));
        }
        if (warmed_up && buffered == 0) {
            underruns++;
        }
        if (!warmed_up && buffered >= ie_audio_api.get_desired_buffered()) {
            warmed_up = 1;
        }

        beat++;
        status[2] = beat;
        status[4] = ie_audio_status();
        status[5] = ie_audio_total_written();
        status[6] = (uint32_t) buffered;
        status[7] = underruns;
    }
}
