#include <stdint.h>

#include "platform.h"

int main(void) {
    uint8_t key;

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

    for (;;) {
        platform_wait_frame();
        key = platform_input_poll();
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
        }
    }

    return 0;
}
