#include "game.h"
#include "story.h"

/* Sprite pointer/color/VIC-attribute setup for rain's 7 dedicated sprites
 * (1-7), pointed here rather than kept resident - see platform_rain_enable()
 * in platform.h for why this is safe to run from room code. */
#define P_VIC(reg) (((volatile uint8_t*)0xd000)[(reg)])
#define P_SPRITE_POINTERS ((uint8_t*)0x07f8)
#define RAIN_BITMAP_POINTER (uint8_t)(0x3b80u / 64u)
#define RAIN_SPRITE_MASK 0xfeu /* sprites 1-7 */

static void rain_setup(void) {
    uint8_t i;
    for (i = 1u; i <= 7u; ++i) {
        P_SPRITE_POINTERS[i] = RAIN_BITMAP_POINTER;
        P_VIC(0x27u + i) = 6u; /* dark blue */
    }
    P_VIC(0x17) &= (uint8_t)~RAIN_SPRITE_MASK; /* Y-expand off */
    P_VIC(0x1b) |= RAIN_SPRITE_MASK;           /* priority: behind */
    P_VIC(0x1d) &= (uint8_t)~RAIN_SPRITE_MASK; /* X-expand off: true 45-degree diagonal */
    P_VIC(0x15) |= RAIN_SPRITE_MASK;           /* enable */
}

void enter_room(void) {
    platform_rain_enable(rain_setup);
}

void enter_tile(void) {
    ++game_state.flags[STORY_STATE_ROOM_00_TILE_ENTRY_COUNT];
}

uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 18u && tile_y == 3u) {
        (void)game_room_script_entry(STORY_ROOM00_LOOK_TREE);
        return GAME_LOOK_HANDLED;
    }
    return GAME_LOOK_DEFAULT;
}

uint8_t __fastcall__ use_at(uint8_t tile_x, uint8_t tile_y) {
    if (tile_x == 18u && tile_y == 3u) {
        (void)game_room_script_entry(STORY_ROOM00_USE_TREE);
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}
