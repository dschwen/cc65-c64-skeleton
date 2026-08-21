#include <stdint.h>

#include "platform.h"
#include "game.h"
#include "world.h"

#pragma code-name ("HIGHCODE")
#pragma rodata-name ("HIGHRODATA")

int main(void) {
    uint8_t key;
    uint8_t command;
    uint8_t direction;
    uint8_t look_x;
    uint8_t look_y;

    platform_init();
    if (platform_storage == PLATFORM_STORAGE_DISK) {
        (void)platform_object_types_load("OBJECTS.COBJ", platform_storage_device);
    }
    game_state_init();
    game_world_init();
    (void)game_room_code_load_current();
    platform_room_draw(&platform_room, platform_player);
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
                    }
                    break;
                case PLATFORM_KEY_CURSOR_RIGHT:
                    if (look_x + 1u < PLATFORM_MAP_WIDTH) {
                        ++look_x;
                        (void)platform_look_cursor_move(look_x, look_y);
                    }
                    break;
                case PLATFORM_KEY_CURSOR_LEFT:
                    if (look_x > 0u) {
                        --look_x;
                        (void)platform_look_cursor_move(look_x, look_y);
                    }
                    break;
                case PLATFORM_KEY_CURSOR_DOWN:
                    if (look_y + 1u < PLATFORM_MAP_HEIGHT) {
                        ++look_y;
                        (void)platform_look_cursor_move(look_x, look_y);
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
        if (command == PLATFORM_KEY_TAKE) {
            direction = 0xffu;
            switch (key) {
                case PLATFORM_KEY_CURSOR_UP:
                    direction = PLATFORM_DIRECTION_NORTH;
                    break;
                case PLATFORM_KEY_CURSOR_RIGHT:
                    direction = PLATFORM_DIRECTION_EAST;
                    break;
                case PLATFORM_KEY_CURSOR_LEFT:
                    direction = PLATFORM_DIRECTION_WEST;
                    break;
                case PLATFORM_KEY_CURSOR_DOWN:
                    direction = PLATFORM_DIRECTION_SOUTH;
                    break;
                case PLATFORM_KEY_TAKE:
                    platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
                    platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
                    command = 0u;
                    break;
            }
            if (direction != 0xffu) {
                (void)game_take_direction(direction);
                command = 0u;
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
            case PLATFORM_KEY_LIGHTNING:
                platform_lightning();
                break;
            case PLATFORM_KEY_LOOK:
                look_x = platform_player->x >> 1;
                look_y = platform_player->y >> 1;
                game_text_write(PLATFORM_TEXT_LINE_TOP, "Looking...", 1u);
                (void)platform_look_cursor_show(look_x, look_y);
                command = PLATFORM_KEY_LOOK;
                break;
            case PLATFORM_KEY_TAKE:
                game_text_write(PLATFORM_TEXT_LINE_TOP, "Taking...", 1u);
                command = PLATFORM_KEY_TAKE;
                break;
            case PLATFORM_KEY_INVENTORY:
                game_inventory_show();
                break;
        }
    }

    return 0;
}
