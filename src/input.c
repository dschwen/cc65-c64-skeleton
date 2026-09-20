#include <stdint.h>
#include "platform.h"

void raster_irq_resync(void);

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
    return result;
}
