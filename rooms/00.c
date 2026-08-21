#include "game.h"

void enter_room(void) {
}

void enter_tile(void) {
    ++game_state.flags[0];
}

uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 8u && tile_y == 3u) {
        game_text_write(PLATFORM_TEXT_LINE_TOP,
                        "Smoke curls through a gap in the roof.", 1u);
        platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
        return GAME_LOOK_HANDLED;
    }
    return GAME_LOOK_DEFAULT;
}
