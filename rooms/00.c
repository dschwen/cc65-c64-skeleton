#include "game.h"

void enter_tile(void) {
    ++game_state.flags[0];
}

uint8_t __fastcall__ look_at(uint8_t direction) {
    if (direction == PLATFORM_DIRECTION_NORTH &&
        (game_state.player_x >> 1) == 8u &&
        (game_state.player_y >> 1) == 4u) {
        game_text_write(PLATFORM_TEXT_LINE_TOP,
                        "Smoke curls through a gap in the roof.", 1u);
        platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
        return GAME_LOOK_HANDLED;
    }
    return GAME_LOOK_DEFAULT;
}
