#include <stdint.h>

#include "platform.h"
#include "game.h"
#include "world.h"

#pragma code-name ("HIGHCODE")
#pragma rodata-name ("HIGHRODATA")

static uint8_t key;
static uint8_t command;
static uint8_t look_x;
static uint8_t look_y;
static uint8_t cursor_min_x;
static uint8_t cursor_max_x;
static uint8_t cursor_min_y;
static uint8_t cursor_max_y;

int main(void) {
    platform_init();
    game_state_init();
    game_world_init();
    (void)game_room_code_load_current();
    platform_room_draw(&platform_room, platform_player);
    game_enter_room();
    game_enter_tile();
    command = 0u;

    for (;;) {
        platform_wait_frame();
        (void)game_process_pending_transition();
        key = platform_input_poll();
        if (command == PLATFORM_KEY_LOOK) {
            platform_look_cursor_tick();
            switch (key) {
                case PLATFORM_KEY_CURSOR_UP:
                    if (look_y > 0u) {
                        --look_y;
                        (void)platform_look_cursor_move(look_x, look_y);
                    } else {
                        (void)platform_look_exit(&platform_room, platform_player,
                                                 look_x, look_y,
                                                 PLATFORM_DIRECTION_NORTH, 1u);
                    }
                    break;
                case PLATFORM_KEY_CURSOR_RIGHT:
                    if (look_x + 1u < PLATFORM_MAP_WIDTH) {
                        ++look_x;
                        (void)platform_look_cursor_move(look_x, look_y);
                    } else {
                        (void)platform_look_exit(&platform_room, platform_player,
                                                 look_x, look_y,
                                                 PLATFORM_DIRECTION_EAST, 1u);
                    }
                    break;
                case PLATFORM_KEY_CURSOR_LEFT:
                    if (look_x > 0u) {
                        --look_x;
                        (void)platform_look_cursor_move(look_x, look_y);
                    } else {
                        (void)platform_look_exit(&platform_room, platform_player,
                                                 look_x, look_y,
                                                 PLATFORM_DIRECTION_WEST, 1u);
                    }
                    break;
                case PLATFORM_KEY_CURSOR_DOWN:
                    if (look_y + 1u < PLATFORM_MAP_HEIGHT) {
                        ++look_y;
                        (void)platform_look_cursor_move(look_x, look_y);
                    } else {
                        (void)platform_look_exit(&platform_room, platform_player,
                                                 look_x, look_y,
                                                 PLATFORM_DIRECTION_SOUTH, 1u);
                    }
                    break;
                case PLATFORM_KEY_ENTER:
                    platform_look_cursor_hide();
                    if (platform_look_tile_check(&platform_room, platform_player,
                                                 look_x, look_y, 1u) == PLATFORM_OK &&
                        game_look_at(look_x, look_y) == GAME_LOOK_DEFAULT) {
                        (void)platform_look_tile(&platform_room, look_x, look_y, 1u);
                    }
                    command = 0u;
                    break;
                case PLATFORM_KEY_LOOK:
                    platform_look_cursor_hide();
                    platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
                    platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
                    command = 0u;
                    break;
            }
            continue;
        }
        if (command == PLATFORM_KEY_TAKE || command == PLATFORM_KEY_USE) {
            platform_look_cursor_tick();
            switch (key) {
                case PLATFORM_KEY_CURSOR_UP:
                    if (look_y > cursor_min_y) {
                        --look_y;
                        (void)platform_look_cursor_move(look_x, look_y);
                    }
                    break;
                case PLATFORM_KEY_CURSOR_RIGHT:
                    if (look_x < cursor_max_x) {
                        ++look_x;
                        (void)platform_look_cursor_move(look_x, look_y);
                    }
                    break;
                case PLATFORM_KEY_CURSOR_LEFT:
                    if (look_x > cursor_min_x) {
                        --look_x;
                        (void)platform_look_cursor_move(look_x, look_y);
                    }
                    break;
                case PLATFORM_KEY_CURSOR_DOWN:
                    if (look_y < cursor_max_y) {
                        ++look_y;
                        (void)platform_look_cursor_move(look_x, look_y);
                    }
                    break;
                case PLATFORM_KEY_ENTER:
                    if (platform_look_tile_check(&platform_room, platform_player,
                                                 look_x, look_y, 1u) == PLATFORM_OK) {
                        if (command == PLATFORM_KEY_TAKE) {
                            (void)game_take_tile(look_x, look_y);
                        } else if (game_use_at(look_x, look_y) == GAME_USE_DEFAULT) {
                            game_text_write(PLATFORM_TEXT_LINE_TOP,
                                            "Nothing happens.", 1u);
                        }
                    }
                    platform_look_cursor_hide();
                    command = 0u;
                    break;
                default:
                    if (key == command) {
                        platform_look_cursor_hide();
                        platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
                        platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
                        command = 0u;
                    }
            }
            continue;
        }
        switch (key) {
            case PLATFORM_KEY_CURSOR_UP:
                game_player_step(0, -1);
                break;
            case PLATFORM_KEY_CURSOR_DOWN:
                game_player_step(0, 1);
                break;
            case PLATFORM_KEY_CURSOR_LEFT:
                game_player_step(-1, 0);
                break;
            case PLATFORM_KEY_CURSOR_RIGHT:
                game_player_step(1, 0);
                break;
            case PLATFORM_KEY_LIGHT_DOWN:
                if (platform_global_light > PLATFORM_LIGHT_NONE) {
                    platform_lighting_set_global(platform_global_light - 1u);
                }
                break;
            case PLATFORM_KEY_LIGHT_UP:
                if (platform_global_light < PLATFORM_LIGHT_FULL) {
                    platform_lighting_set_global(platform_global_light + 1u);
                }
                break;
            case PLATFORM_KEY_LOOK:
                look_x = platform_player->x >> 1;
                look_y = platform_player->y >> 1;
                game_text_write(PLATFORM_TEXT_LINE_TOP, "Looking...", 1u);
                (void)platform_look_cursor_show(look_x, look_y);
                command = PLATFORM_KEY_LOOK;
                break;
            case PLATFORM_KEY_TAKE:
            case PLATFORM_KEY_USE:
                look_x = platform_player->x >> 1;
                look_y = platform_player->y >> 1;
                cursor_min_x = look_x == 0u ? 0u : look_x - 1u;
                cursor_max_x = look_x + 1u < PLATFORM_MAP_WIDTH
                                   ? look_x + 1u : look_x;
                cursor_min_y = look_y == 0u ? 0u : look_y - 1u;
                cursor_max_y = look_y + 1u < PLATFORM_MAP_HEIGHT
                                   ? look_y + 1u : look_y;
                game_text_write(PLATFORM_TEXT_LINE_TOP,
                                key == PLATFORM_KEY_TAKE ? "Taking..." : "Using...",
                                1u);
                (void)platform_look_cursor_show(look_x, look_y);
                command = key;
                break;
            case PLATFORM_KEY_INVENTORY:
                game_inventory_show();
                break;
            case PLATFORM_KEY_SAVE:
                game_save_show();
                break;
            case PLATFORM_KEY_LOAD:
                game_load_show();
                break;
        }
    }

    return 0;
}
