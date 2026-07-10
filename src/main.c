#include <stdint.h>

#include "platform.h"

#define TILE_COUNT_USED 21u

static uint8_t random_state = 0xa5;

static uint8_t next_random_tile(void) {
    uint8_t value;
    random_state = (random_state >> 1) ^ (-(random_state & 1u) & 0xb8u);
    value = random_state;
    while (value >= TILE_COUNT_USED) value -= TILE_COUNT_USED;
    return value;
}

int main(void) {
    uint16_t i;

    platform_init();
    for (i = 0; i < PLATFORM_MAP_TILE_COUNT; ++i) {
        platform_room.tiles[i] = next_random_tile();
    }
    platform_room_draw(&platform_room, 0);
    platform_text_write_line(PLATFORM_TEXT_LINE_TOP, 1,
                             "RANDOM TILES 0-20", 1);
    platform_text_write_line(PLATFORM_TEXT_LINE_BOTTOM, 1,
                             "PLATFORM API READY", 1);

    for (;;) {
    }

    return 0;
}
