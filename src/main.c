#include <stdint.h>

#include "platform.h"
#include "game.h"
#include "world.h"

#pragma code-name ("UPPERCODE")
#pragma rodata-name ("HIGHRODATA")

int main(void) {
    uint8_t key;
    uint8_t command;
    uint8_t direction;

    platform_init();
    if (platform_storage == PLATFORM_STORAGE_DISK) {
        (void)platform_object_types_load("OBJECTS.COBJ", platform_storage_device);
    }
    game_state_init();
    game_world_init();
    (void)game_room_code_load_current();
    platform_room_draw(&platform_room, platform_player);
    platform_overlay_show(&platform_room, 8, 16,
                          0x01, 0x16, 0x36, 1);

    do {
        platform_wait_frame();
        key = platform_input_poll();
    } while (key == 0u);
    platform_overlay_hide();
    game_enter_tile();
    command = 0u;

    for (;;) {
        platform_wait_frame();
        (void)game_process_pending_transition();
        key = platform_input_poll();
        if (command != 0u) {
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
                case PLATFORM_KEY_LOOK:
                    if (command == PLATFORM_KEY_LOOK) {
                        platform_overlay_hide();
                        command = 0u;
                    }
                    break;
                case PLATFORM_KEY_TAKE:
                    if (command == PLATFORM_KEY_TAKE) {
                        platform_overlay_hide();
                        command = 0u;
                    }
                    break;
            }
            if (direction != 0xffu) {
                platform_overlay_hide();
                if (command == PLATFORM_KEY_LOOK) {
                    if (game_look_at(direction) == GAME_LOOK_DEFAULT) {
                        (void)platform_look_direction(&platform_room, platform_player,
                                                      direction, 1u);
                    }
                } else {
                    (void)game_take_direction(direction);
                }
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
                (void)platform_overlay_show_text(8u, 16u, "Looking...", 0, 0, 1u);
                command = PLATFORM_KEY_LOOK;
                break;
            case PLATFORM_KEY_TAKE:
                (void)platform_overlay_show_text(8u, 16u, "Taking...", 0, 0, 1u);
                command = PLATFORM_KEY_TAKE;
                break;
            case PLATFORM_KEY_INVENTORY:
                game_inventory_show();
                break;
        }
    }

    return 0;
}
