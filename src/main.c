#include <stdint.h>

#include "platform.h"

int main(void) {
    platform_init();
    platform_room_draw(&platform_room, platform_player);
    platform_overlay_show(&platform_room, 8, 16,
                          0x01, 0x16, 0x36, 1);

    for (;;) {
    }

    return 0;
}
