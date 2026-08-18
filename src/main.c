#include <stdint.h>

#include "platform.h"

#pragma code-name ("HIGHCODE")
#pragma rodata-name ("HIGHRODATA")

int main(void) {
    uint8_t key;
    uint8_t looking;
    uint8_t direction;

    platform_init();
    if (platform_storage == PLATFORM_STORAGE_DISK) {
        (void)platform_object_types_load("OBJECTS.COBJ", platform_storage_device);
    }
    platform_room_draw(&platform_room, platform_player);
    platform_overlay_show(&platform_room, 8, 16,
                          0x01, 0x16, 0x36, 1);

    do {
        platform_wait_frame();
        key = platform_input_poll();
    } while (key == 0u);
    platform_overlay_hide();
    looking = 0u;

    for (;;) {
        platform_wait_frame();
        key = platform_input_poll();
        if (looking) {
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
                    platform_overlay_hide();
                    looking = 0u;
                    break;
            }
            if (direction != 0xffu) {
                platform_overlay_hide();
                (void)platform_look_direction(&platform_room, platform_player,
                                              direction, 1u);
                looking = 0u;
            }
            continue;
        }
        switch (key) {
            case PLATFORM_KEY_CURSOR_UP:
                platform_player_step(0, -1);
                break;
            case PLATFORM_KEY_CURSOR_DOWN:
                platform_player_step(0, 1);
                break;
            case PLATFORM_KEY_CURSOR_LEFT:
                platform_player_step(-1, 0);
                break;
            case PLATFORM_KEY_CURSOR_RIGHT:
                platform_player_step(1, 0);
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
                looking = 1u;
                break;
        }
    }

    return 0;
}
