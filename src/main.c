#include <stdint.h>

#include "vic.h"

#define SCREEN_WIDTH       40u
#define SCREEN_HEIGHT      25u
#define TILE_AREA_ROWS     22u
#define TILE_COUNT_USED    21u
#define SCREEN_RAM         ((uint8_t*)0x0400)
#define COLOR_RAM          ((uint8_t*)0xd800)

extern const uint8_t tile_data[];
void raster_irq_install(void);

static uint8_t random_state = 0xa5;

static uint8_t next_random_tile(void) {
    uint8_t value;

    /* Eight-bit maximal-period Galois LFSR. Random-looking is enough here. */
    random_state = (random_state >> 1) ^ (-(random_state & 1u) & 0xb8u);
    value = random_state;
    while (value >= TILE_COUNT_USED) {
        value -= TILE_COUNT_USED;
    }
    return value;
}

static void clear_screen(void) {
    uint16_t offset;

    for (offset = 0; offset < SCREEN_WIDTH * SCREEN_HEIGHT; ++offset) {
        SCREEN_RAM[offset] = 32;
        COLOR_RAM[offset] = 0;
    }
}

static void draw_tile(uint8_t tile_number, uint8_t x, uint8_t y) {
    const uint8_t* tile = tile_data + ((uint16_t)tile_number << 3);
    uint16_t offset = (uint16_t)y * SCREEN_WIDTH + x;

    SCREEN_RAM[offset] = tile[0];
    COLOR_RAM[offset] = tile[1] & 0x0f;
    SCREEN_RAM[offset + 1] = tile[2];
    COLOR_RAM[offset + 1] = tile[3] & 0x0f;

    offset += SCREEN_WIDTH;
    SCREEN_RAM[offset] = tile[4];
    COLOR_RAM[offset] = tile[5] & 0x0f;
    SCREEN_RAM[offset + 1] = tile[6];
    COLOR_RAM[offset + 1] = tile[7] & 0x0f;
}

static void fill_tile_area(void) {
    uint8_t x;
    uint8_t y;

    for (y = 0; y < TILE_AREA_ROWS; y += 2) {
        for (x = 0; x < SCREEN_WIDTH; x += 2) {
            draw_tile(next_random_tile(), x, y);
        }
    }
}

static uint8_t text_screen_code(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (uint8_t)(ch - 'a' + 1);
    }
    if (ch >= 'A' && ch <= 'Z') {
        return (uint8_t)(ch - 'A' + 65);
    }
    return (uint8_t)ch;
}

static void write_text(uint8_t x, uint8_t y, const char* text, uint8_t color) {
    uint16_t offset = (uint16_t)y * SCREEN_WIDTH + x;

    while (*text != '\0' && x < SCREEN_WIDTH) {
        SCREEN_RAM[offset] = text_screen_code(*text++);
        COLOR_RAM[offset] = color;
        ++offset;
        ++x;
    }
}

int main(void) {
    /* Select VIC bank 0 without disturbing CIA2's other output bits. */
    *((volatile uint8_t*)0xdd00) |= 0x03;

    vic_set_border(0);
    vic_set_bg(0, 0);
    VIC(0x18) = 0x18;

    clear_screen();
    fill_tile_area();
    write_text(1, 23, "RANDOM TILES 0-20", 1);
    write_text(1, 24, "TEXT CHARSET BANK 1", 1);

    raster_irq_install();

    for (;;) {
        /* Rendering is static; the raster IRQ owns the display split. */
    }

    return 0;
}
