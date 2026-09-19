#include <stdint.h>
#include "platform.h"

/*
 * Native (oscar64 C) replacement for src/input.s. The custom raster IRQ
 * does not run the KERNAL keyboard scanner, so the frame-driven game loop
 * calls SCNKEY/GETIN explicitly, briefly mapping KERNAL ROM back in.
 */

void raster_irq_resync(void);
void platform_memory_kernal(void);
void platform_memory_game(void);

uint8_t platform_input_poll(void) {
    uint8_t result;

    __asm { php sei }
    platform_memory_kernal();
    __asm {
        jsr $ff9f  ; SCNKEY
        jsr $ffe4  ; GETIN
        sta result
    }
    platform_memory_game();
    /* SCNKEY/GETIN run with interrupts disabled for however long the
     * KERNAL's keyboard scan/debounce takes, which can occasionally
     * straddle one of the raster IRQ's two precisely-timed charset-switch
     * points and delay it - this resync is the same cheap self-healing
     * used after every far-call unwind for exactly this class of hazard.
     * See src/irq.c's raster_irq_resync() and MEMORY_MAP.md's write-up of
     * the underlying flicker bug this belongs to. */
    raster_irq_resync();
    __asm { plp }
    return result;
}
