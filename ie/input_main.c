/*
 * Stage 3 input backend smoke program.
 *
 * Polls controller port 0 continuously and publishes the observed
 * state so the smoke script can inject scancodes (term.scancode) and
 * assert clean pressed/released transitions through the neutral
 * platform contract.
 *
 * Status block payload (common fields in ie/ie_mmio.h):
 *   +16  u32 current buttons (N64 layout)
 *   +20  u32 stick: (stick_x + 128) << 8 | (stick_y + 128)
 *   +24  u32 press-event counter (make codes consumed)
 *   +28  u32 disconnected-port check: 1 when ports 1-3 report
 *            not-connected with zeroed state
 */

#include <stdint.h>

#include "platform/platform.h"

#include "ie_mmio.h"

extern uint32_t ie_input_press_events;

void ie_main(void) {
    volatile uint32_t* status = (volatile uint32_t*) (uintptr_t) IE_BOOT_STATUS_ADDR;
    struct PlatformControllerState state;
    struct PlatformControllerState other;
    uint32_t other_ports_ok = 1;
    uint32_t beat = 0;
    int port;

    platform_log("MK64-IE68 STAGE3 INPUT smoke");

    for (port = 1; port <= 3; port++) {
        if (!platform_poll_controller(port, &other) || other.connected || other.buttons != 0 ||
            other.stick_x != 0 || other.stick_y != 0) {
            other_ports_ok = 0;
        }
    }

    status[1] = 0;
    status[4] = 0;
    status[5] = 0;
    status[6] = 0;
    status[7] = other_ports_ok;
    status[0] = IE_BOOT_MAGIC;

    for (;;) {
        if (platform_poll_controller(0, &state) && state.connected) {
            status[4] = state.buttons;
            status[5] = ((uint32_t)(uint8_t)(state.stick_x + 128) << 8)
                      | (uint32_t)(uint8_t)(state.stick_y + 128);
            status[6] = ie_input_press_events;
        }
        beat++;
        status[2] = beat;
    }
}
