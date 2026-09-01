#include "game.h"
#include "story.h"

void enter_room(void) {
    platform_rain_enable();
}

void enter_tile(void) {
    ++game_state.flags[STORY_STATE_ROOM_00_TILE_ENTRY_COUNT];
}

uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 18u && tile_y == 3u) {
        game_text_write(PLATFORM_TEXT_LINE_TOP,
                        "The tree has a knot hole.", 1u);
        return GAME_LOOK_HANDLED;
    }
    return GAME_LOOK_DEFAULT;
}

uint8_t __fastcall__ use_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 18u && tile_y == 3u) {
        game_text_write_room(PLATFORM_TEXT_LINE_TOP, 0x23u, 1u);
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}
