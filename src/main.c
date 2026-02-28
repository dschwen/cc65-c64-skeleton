#include <c64.h>
#include <conio.h>
#include <stdint.h>

#include "vic.h"
#include "sid.h"

/*
 * Minimal demo:
 * - sets border/background
 * - prints a message
 * - plays a simple tone on voice 1
 *
 * Build: make
 * Run: open build/game.prg in VICE
 */

static void wait_vblank(void) {
    /* Very simple wait loop: poll raster and wait for wrap.
       For real projects, use a raster IRQ. */
    uint8_t r;
    do { r = VIC_RASTER; } while (r < 250);
    do { r = VIC_RASTER; } while (r >= 250);
}

int main(void) {
    clrscr();

    vic_set_border(6);
    vic_set_bg(0, 0);

    cputsxy(2, 2, "cc65 + C64 skeleton");
    cputsxy(2, 4, "VIC @ $D000, SID @ $D400");
    cputsxy(2, 6, "Press any key...");

    /* Simple beep */
    sid_set_volume(15);
    sid_voice1_tone(0x1000, 0x0800); /* frequency, pulse width */
    sid_voice1_gate_square(1);

    while (!kbhit()) {
        wait_vblank();
    }

    sid_voice1_gate_square(0);
    sid_set_volume(0);

    cputsxy(2, 8, "Done.");

    return 0;
}
