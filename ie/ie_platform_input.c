/*
 * IE backend for the platform input contract (platform_poll_controller
 * from src/platform/platform.h), on the engine's keyboard scancode
 * FIFO. The backend owns the key mapping policy, per the Stage 2
 * contract: state is reported already translated to the N64 button
 * layout and stick range.
 *
 * Scancodes are XT/set-1 style make codes with break = make | 0x80
 * (the engine's keys.* table; revalidated from the ie68-port salvage
 * branch, commit 99507c62, ie/ie_input.c). Reading IE_SCAN_CODE pops
 * the FIFO; IE_SCAN_STATUS bit 0 reports availability.
 *
 * Mapping (controller port 0 only):
 *   Arrows            analog stick (+/-80)
 *   X / Z             A / B
 *   Space             Z trigger
 *   Enter             Start
 *   Q / E             L / R triggers
 *   I / K / J / L     C-up / C-down / C-left / C-right
 *   W / S / A / D     D-pad up / down / left / right
 */

#include <stdint.h>

#include "platform/platform.h"

#include "ie_mmio.h"

/* N64 button bits (include/PR/os_cont.h). */
#define BTN_A 0x8000u
#define BTN_B 0x4000u
#define BTN_Z 0x2000u
#define BTN_START 0x1000u
#define BTN_DPAD_UP 0x0800u
#define BTN_DPAD_DOWN 0x0400u
#define BTN_DPAD_LEFT 0x0200u
#define BTN_DPAD_RIGHT 0x0100u
#define BTN_L 0x0020u
#define BTN_R 0x0010u
#define BTN_C_UP 0x0008u
#define BTN_C_DOWN 0x0004u
#define BTN_C_LEFT 0x0002u
#define BTN_C_RIGHT 0x0001u

/* XT/set-1 make codes (engine keys.* table). */
#define SC_X 0x2D
#define SC_Z 0x2C
#define SC_SPACE 0x39
#define SC_ENTER 0x1C
#define SC_Q 0x10
#define SC_E 0x12
#define SC_I 0x17
#define SC_J 0x24
#define SC_K 0x25
#define SC_L 0x26
#define SC_W 0x11
#define SC_A 0x1E
#define SC_S 0x1F
#define SC_D 0x20
#define SC_UP 0x48
#define SC_DOWN 0x50
#define SC_LEFT 0x4B
#define SC_RIGHT 0x4D

#define IE_STICK_MAG 80

static uint16_t held_buttons;
static uint8_t stick_up, stick_down, stick_left, stick_right;

/* Number of make (press) events consumed; exposed for the smoke. */
uint32_t ie_input_press_events;

static uint16_t button_for_scancode(uint8_t code) {
    switch (code) {
        case SC_X:
            return BTN_A;
        case SC_Z:
            return BTN_B;
        case SC_SPACE:
            return BTN_Z;
        case SC_ENTER:
            return BTN_START;
        case SC_Q:
            return BTN_L;
        case SC_E:
            return BTN_R;
        case SC_I:
            return BTN_C_UP;
        case SC_K:
            return BTN_C_DOWN;
        case SC_J:
            return BTN_C_LEFT;
        case SC_L:
            return BTN_C_RIGHT;
        case SC_W:
            return BTN_DPAD_UP;
        case SC_S:
            return BTN_DPAD_DOWN;
        case SC_A:
            return BTN_DPAD_LEFT;
        case SC_D:
            return BTN_DPAD_RIGHT;
        default:
            return 0;
    }
}

static void drain_scancodes(void) {
    while ((ie_mmio_read32(IE_SCAN_STATUS) & IE_SCAN_STATUS_READY) != 0) {
        uint32_t raw = ie_mmio_read32(IE_SCAN_CODE);
        uint8_t code = (uint8_t)(raw & 0x7Fu);
        int released = (raw & 0x80u) != 0;
        uint16_t button = button_for_scancode(code);

        if (!released) {
            ie_input_press_events++;
        }

        if (button != 0) {
            if (released) {
                held_buttons &= (uint16_t) ~button;
            } else {
                held_buttons |= button;
            }
            continue;
        }

        switch (code) {
            case SC_UP:
                stick_up = !released;
                break;
            case SC_DOWN:
                stick_down = !released;
                break;
            case SC_LEFT:
                stick_left = !released;
                break;
            case SC_RIGHT:
                stick_right = !released;
                break;
            default:
                break;
        }
    }
}

bool platform_poll_controller(int port, struct PlatformControllerState* state) {
    if (state == NULL) {
        return false;
    }
    if (port != 0) {
        state->connected = false;
        state->buttons = 0;
        state->stick_x = 0;
        state->stick_y = 0;
        return true;
    }

    drain_scancodes();

    state->connected = true;
    state->buttons = held_buttons;
    state->stick_x = (int8_t)((stick_right ? IE_STICK_MAG : 0) - (stick_left ? IE_STICK_MAG : 0));
    state->stick_y = (int8_t)((stick_up ? IE_STICK_MAG : 0) - (stick_down ? IE_STICK_MAG : 0));
    return true;
}
