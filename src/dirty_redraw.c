#include <stdint.h>
#include <string.h>
#include "memmap.h"
#include "platform.h"

/*
 * Native (oscar64 C) replacement for src/dirty_redraw.s. That file existed
 * purely to defeat cc65's software-stack calling convention (measured
 * 43-50% pure calling-convention overhead for a chain of small functions
 * passing pointer parameters - see its own header comment); oscar64's
 * static, whole-program register/frame allocation does not share that
 * weakness, so this is ordinary, portable C using the real struct types
 * instead of hand-tracked offsets.
 */

#define DIRTY_BYTES 110u

static uint8_t dirty_cells[DIRTY_BYTES];

void dirty_clear(void) {
    memset(dirty_cells, 0, sizeof(dirty_cells));
}

/* cell = world_y*MAP_WIDTH_CHARS+world_x; MAP_WIDTH_CHARS (40) is a
 * multiple of 8, so cell>>3 = world_y*5 + (world_x>>3) and cell&7 =
 * world_x&7 - avoids a real multiply for the bitmap index, same trick the
 * original used. */
static void dirty_set(uint8_t world_x, uint8_t world_y) {
    uint8_t byte_index, bit;
    if (world_x >= MAP_WIDTH_CHARS || world_y >= MAP_HEIGHT_CHARS) return;
    byte_index = (uint8_t)(world_y * 5u + (world_x >> 3));
    bit = world_x & 7u;
    dirty_cells[byte_index] |= (uint8_t)(1u << bit);
}

static uint8_t object_type_is_valid(const PlatformObjectType* type) {
    uint8_t width = (uint8_t)(type->dimensions >> 4);
    uint8_t height = (uint8_t)(type->dimensions & 0x0fu);
    uint8_t hotx = (uint8_t)(type->hotspot >> 4);
    uint8_t hoty = (uint8_t)(type->hotspot & 0x0fu);
    return width > 0u && height > 0u &&
           (uint16_t)width * height <= PLATFORM_OBJECT_CELL_COUNT &&
           hotx < width && hoty < height;
}

void mark_object_cells(const PlatformObject* object) {
    const PlatformObjectType* type;
    uint8_t width, height, hotx, hoty;
    int16_t left, top;
    uint8_t x, y;
    uint16_t index;

    if (object->type == 0u) return;
    type = platform_object_type_get(object->type);
    if (!object_type_is_valid(type)) return;

    width = (uint8_t)(type->dimensions >> 4);
    height = (uint8_t)(type->dimensions & 0x0fu);
    hotx = (uint8_t)(type->hotspot >> 4);
    hoty = (uint8_t)(type->hotspot & 0x0fu);
    left = (int16_t)object->x - hotx;
    top = (int16_t)object->y - hoty;

    index = 0u;
    for (y = 0u; y < height; ++y) {
        for (x = 0u; x < width; ++x, ++index) {
            int16_t wx, wy;
            if (type->chars[index] == 0u) continue; /* transparent cell */
            wx = left + x;
            if (wx < 0) continue;
            wy = top + y;
            if (wy < 0) continue;
            dirty_set((uint8_t)wx, (uint8_t)wy);
        }
    }
}

/* Tests the object at `object` against (curx,cury), transparency-checked
 * per cell. Returns 1 and fills *out_ch/*out_color/*out_y on a hit. */
static uint8_t object_cell_try(const PlatformObject* object, uint8_t curx, uint8_t cury,
                               uint8_t* out_ch, uint8_t* out_color, uint8_t* out_y) {
    const PlatformObjectType* type;
    uint8_t width, height, hotx, hoty;
    int16_t left, top, localx, localy;
    uint16_t index;

    if (object->type == 0u) return 0u;
    type = platform_object_type_get(object->type);
    if (!object_type_is_valid(type)) return 0u;

    width = (uint8_t)(type->dimensions >> 4);
    height = (uint8_t)(type->dimensions & 0x0fu);
    hotx = (uint8_t)(type->hotspot >> 4);
    hoty = (uint8_t)(type->hotspot & 0x0fu);
    left = (int16_t)object->x - hotx;
    top = (int16_t)object->y - hoty;

    localx = (int16_t)curx - left;
    if (localx < 0 || localx >= width) return 0u;
    localy = (int16_t)cury - top;
    if (localy < 0 || localy >= height) return 0u;

    index = (uint16_t)localy * width + (uint16_t)localx;
    if (type->chars[index] == 0u) return 0u;

    *out_ch = type->chars[index];
    *out_color = type->colors[index] & 0x0fu;
    *out_y = object->y;
    return 1u;
}

/* Sets *out_ch/*out_color from the room tile under (curx,cury). */
static void base_cell(const PlatformRoom* room, uint8_t curx, uint8_t cury,
                      uint8_t* out_ch, uint8_t* out_color) {
    extern uint8_t tile_data[];
    uint16_t tile_offset = (uint16_t)(cury >> 1) * MAP_WIDTH_TILES + (curx >> 1);
    uint8_t tile = room->tiles[tile_offset];
    const uint8_t* def = tile_data + ((uint16_t)tile << 3);
    uint8_t quadrant = (uint8_t)(((cury & 1u) << 1) | (curx & 1u));
    *out_ch = def[quadrant * 2u];
    *out_color = def[quadrant * 2u + 1u] & 0x0fu;
}

/* Composes *out_ch/*out_color for (curx,cury): tile, then every object
 * covering this cell (room objects in slot order, then the player),
 * keeping whichever has the highest y (a later-considered object wins a
 * tie) - matches redraw_map_and_objects()'s ascending-y full redraw in
 * platform.c, so a room looks the same after a full redraw or a partial
 * one. */
static void compose_cell(const PlatformRoom* room, const PlatformObject* player,
                         uint16_t limit, uint8_t curx, uint8_t cury,
                         uint8_t* out_ch, uint8_t* out_color) {
    uint16_t i;
    uint8_t hit, cand_ch, cand_color, cand_y;
    uint8_t have_hit = 0u;
    uint8_t best_y = 0u;

    base_cell(room, curx, cury, out_ch, out_color);

    for (i = 0u; i < limit; ++i) {
        const PlatformObject* object = &room->objects[i];
        if (object == player) continue;
        hit = object_cell_try(object, curx, cury, &cand_ch, &cand_color, &cand_y);
        if (hit && (!have_hit || cand_y >= best_y)) {
            *out_ch = cand_ch;
            *out_color = cand_color;
            best_y = cand_y;
            have_hit = 1u;
        }
    }
    hit = object_cell_try(player, curx, cury, &cand_ch, &cand_color, &cand_y);
    if (hit && (!have_hit || cand_y >= best_y)) {
        *out_ch = cand_ch;
        *out_color = cand_color;
    }
}

void redraw_dirty(const PlatformRoom* room, const PlatformObject* player) {
    uint8_t y;
    uint8_t byte_index = 0u;
    uint16_t limit = (room == rendered_room) ? rendered_object_limit
                                              : platform_room_object_limit(room);

    for (y = 0u; y < MAP_HEIGHT_CHARS; ++y) {
        uint8_t q;
        for (q = 0u; q < 5u; ++q, ++byte_index) {
            uint8_t bits = dirty_cells[byte_index];
            uint8_t bit;
            if (bits == 0u) continue;
            for (bit = 0u; bit < 8u; ++bit) {
                uint8_t x;
                uint16_t offset, tile_offset;
                uint8_t ch, color, visible_color;

                if (!(bits & (uint8_t)(1u << bit))) continue;
                x = (uint8_t)((q << 3) + bit);

                compose_cell(room, player, limit, x, y, &ch, &color);

                offset = (uint16_t)y * MAP_WIDTH_CHARS + x;
                tile_offset = (uint16_t)(y >> 1) * MAP_WIDTH_TILES + (x >> 1);

                platform_base_colors[offset] = color;
                visible_color = platform_light_colors[
                    (uint16_t)((platform_brightness[tile_offset] & 3u) << 4) | color];
                if (platform_view_tiles[tile_offset] == 0u) visible_color = 0u;

                if (SCREEN_RAM[offset] != ch) SCREEN_RAM[offset] = ch;
                if (COLOR_RAM[offset] != visible_color) COLOR_RAM[offset] = visible_color;
            }
        }
    }
}
