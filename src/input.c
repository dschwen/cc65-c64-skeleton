#include <stdint.h>
#include "platform.h"

void raster_irq_resync(void);

#define CIA1_PRA (*(volatile uint8_t*)0xdc00u)
#define JOY_UP    0x01u
#define JOY_DOWN  0x02u
#define JOY_LEFT  0x04u
#define JOY_RIGHT 0x08u
#define JOY_FIRE  0x10u
#define JOY_REPEAT_DELAY 12u /* frames a held direction waits before repeating */
#define JOY_REPEAT_RATE   4u /* frames between repeats while held */

static uint8_t joy_previous;
static uint8_t joy_repeat;

/* Joystick in port 2, folded onto the same PETSCII codes the keyboard
 * produces so every menu and cursor mode works unchanged: directions are
 * the cursor keys (with key-style auto-repeat while held), fire is RETURN
 * (confirm in Look/Take/Use and the menus; in free movement main.c opens
 * the Use cursor on RETURN for exactly this reason). Port 2 shares CIA1
 * port A with the keyboard's column drive; with all columns driven high
 * the port reads back the joystick switches, and - unlike port 1 - it
 * cannot ghost keys into the keyboard scan. Reads only when no key was
 * typed this frame. */
static uint8_t joystick_poll(void) {
    uint8_t state;
    uint8_t pressed;
    uint8_t result = 0u;

    CIA1_PRA = 0xffu;
    state = (uint8_t)(~CIA1_PRA & 0x1fu);
    pressed = (uint8_t)(state & ~joy_previous);
    if (state & JOY_FIRE) {
        if (pressed & JOY_FIRE) result = PLATFORM_KEY_ENTER;
    } else if (state & (JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT)) {
        uint8_t fire_now = 0u;
        if (pressed & (JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT)) {
            joy_repeat = JOY_REPEAT_DELAY;
            fire_now = 1u;
        } else if (--joy_repeat == 0u) {
            joy_repeat = JOY_REPEAT_RATE;
            fire_now = 1u;
        }
        if (fire_now) {
            if (state & JOY_UP) result = PLATFORM_KEY_CURSOR_UP;
            else if (state & JOY_DOWN) result = PLATFORM_KEY_CURSOR_DOWN;
            else if (state & JOY_LEFT) result = PLATFORM_KEY_CURSOR_LEFT;
            else result = PLATFORM_KEY_CURSOR_RIGHT;
        }
    }
    joy_previous = state;
    return result;
}

/* The raster IRQ replaces the KERNAL timer IRQ, so scan once per frame.
 * platform_init() initializes the editor state and GETIN RAM vector.
 * KERNAL stays mapped throughout gameplay and banked calls. */
uint8_t platform_input_poll(void) {
    uint8_t result;
    result = __asm {
        php
        sei
        jsr $ff9f
        jsr $ffe4
        sta accu
    };
    raster_irq_resync();
    __asm { plp }
    if (result == 0u) result = joystick_poll();
    return result;
}
