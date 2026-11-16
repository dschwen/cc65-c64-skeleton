#include <stdint.h>
#include <stdio.h>
#include <c64/vic.h>

#include "waltz_state.h"
#include "waltz_player.h"

/*
 * Standalone title-music preview (not linked into the game): `make
 * title-music` builds build/title_music.prg, `make run-title-music` plays
 * it in VICE. No IRQ - the main loop waits for each frame and ticks the
 * player. The cartridge plays the same score on its title screen
 * (modules/title.c).
 */

int main(void) {
    printf("\n  THE MOONFEN WALTZ\n\n  TITLE MUSIC PREVIEW\n");
    waltz_init();
    for (;;) {
        vic_waitFrame();
        waltz_tick();
    }
    return 0;
}
