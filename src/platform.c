#include <cbm.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#pragma code-name ("HIGHCODE")
#pragma rodata-name ("HIGHRODATA")

#define P_SCREEN_RAM       ((uint8_t*)0x0400)
#define P_COLOR_RAM        ((volatile uint8_t*)0xd800)
#define P_SPRITE_POINTERS  ((uint8_t*)0x07f8)
#define P_SPRITE_DATA      ((uint8_t*)0x3a00)
#define P_VIC(reg)         (((volatile uint8_t*)0xd000)[(reg)])
#define DIRTY_BYTES        110u
#define ROOM_LFN           2u

#define OBJECT_WIDTH(t)  ((uint8_t)((t)->dimensions >> 4))
#define OBJECT_HEIGHT(t) ((uint8_t)((t)->dimensions & 0x0f))
#define HOTSPOT_X(t)     ((uint8_t)((t)->hotspot >> 4))
#define HOTSPOT_Y(t)     ((uint8_t)((t)->hotspot & 0x0f))

extern const uint8_t tile_data[];
extern const uint8_t tile_properties[];
void raster_irq_install(void);

PlatformRoom platform_room;
PlatformObject platform_player;
PlatformObjectType platform_object_types[PLATFORM_OBJECT_TYPE_COUNT];

const uint8_t platform_overlay_gray[16] = {
    0, 12, 11, 12, 11, 11, 0, 12,
    11, 11, 11, 0, 11, 12, 11, 11
};

static uint8_t dirty_cells[DIRTY_BYTES];
static uint8_t overlay_saved_colors[72];
static uint8_t overlay_visible;
static uint8_t overlay_x;
static uint8_t overlay_y;
static const char hex_digits[] = "0123456789ABCDEF";

/* ASCII 32-95. Each byte contains one four-pixel row in its low nibble. */
static const uint8_t font4x7[64][7] = {
    {0,0,0,0,0,0,0}, {4,4,4,4,4,0,4}, {10,10,0,0,0,0,0}, {10,15,10,15,10,0,0},
    {4,15,12,14,3,15,4}, {9,1,2,4,8,9,0}, {6,9,10,4,10,9,6}, {4,4,0,0,0,0,0},
    {2,4,8,8,8,4,2}, {8,4,2,2,2,4,8}, {0,10,4,15,4,10,0}, {0,4,4,15,4,4,0},
    {0,0,0,0,0,4,8}, {0,0,0,15,0,0,0}, {0,0,0,0,0,0,4}, {1,1,2,4,8,8,0},
    {6,9,11,13,9,9,6}, {4,12,4,4,4,4,14}, {6,9,1,2,4,8,15}, {14,1,1,6,1,1,14},
    {2,6,10,15,2,2,2}, {15,8,8,14,1,1,14}, {6,8,8,14,9,9,6}, {15,1,2,2,4,4,4},
    {6,9,9,6,9,9,6}, {6,9,9,7,1,1,6}, {0,4,0,0,4,0,0}, {0,4,0,0,4,4,8},
    {2,4,8,4,2,0,0}, {0,15,0,15,0,0,0}, {8,4,2,4,8,0,0}, {6,9,1,2,4,0,4},
    {6,9,11,11,8,9,6}, {6,9,9,15,9,9,9}, {14,9,9,14,9,9,14}, {6,9,8,8,8,9,6},
    {14,9,9,9,9,9,14}, {15,8,8,14,8,8,15}, {15,8,8,14,8,8,8}, {6,9,8,11,9,9,6},
    {9,9,9,15,9,9,9}, {14,4,4,4,4,4,14}, {1,1,1,1,9,9,6}, {9,10,12,8,12,10,9},
    {8,8,8,8,8,8,15}, {9,15,15,9,9,9,9}, {9,13,13,11,11,9,9}, {6,9,9,9,9,9,6},
    {14,9,9,14,8,8,8}, {6,9,9,9,11,10,5}, {14,9,9,14,12,10,9}, {7,8,8,6,1,1,14},
    {15,4,4,4,4,4,4}, {9,9,9,9,9,9,6}, {9,9,9,9,9,6,6}, {9,9,9,9,15,15,9},
    {9,9,6,6,6,9,9}, {9,9,9,6,4,4,4}, {15,1,2,4,8,8,15}, {6,4,4,4,4,4,6},
    {8,8,4,2,1,1,0}, {6,2,2,2,2,2,6}, {4,10,0,0,0,0,0}, {0,0,0,0,0,0,15}
};

static void write_screen_cell(uint8_t x, uint8_t y,
                              uint8_t ch, uint8_t color) {
    uint16_t offset;
    uint8_t saved_offset;

    if (x >= PLATFORM_MAP_CHAR_WIDTH || y >= 25u) return;
    offset = (uint16_t)y * PLATFORM_MAP_CHAR_WIDTH + x;
    P_SCREEN_RAM[offset] = ch;
    color &= 0x0f;

    if (overlay_visible && x >= overlay_x && x < overlay_x + 24u &&
        y >= overlay_y && y < overlay_y + 3u) {
        saved_offset = (uint8_t)((y - overlay_y) * 24u + (x - overlay_x));
        overlay_saved_colors[saved_offset] = color;
        P_COLOR_RAM[offset] = platform_overlay_gray[color];
    } else {
        P_COLOR_RAM[offset] = color;
    }
}

static uint8_t read_exact(uint8_t lfn, void* data, uint16_t size) {
    uint8_t* out;
    uint16_t total;
    int got;

    out = (uint8_t*)data;
    total = 0;
    while (total < size) {
        got = cbm_read(lfn, out + total, size - total);
        if (got <= 0) return PLATFORM_ERR_IO;
        total += (uint16_t)got;
    }
    return PLATFORM_OK;
}

static uint8_t object_type_is_valid(const PlatformObjectType* type) {
    uint8_t width;
    uint8_t height;

    width = OBJECT_WIDTH(type);
    height = OBJECT_HEIGHT(type);
    return width > 0u && height > 0u &&
           (uint16_t)width * height <= PLATFORM_OBJECT_CELL_COUNT &&
           HOTSPOT_X(type) < width && HOTSPOT_Y(type) < height;
}

static uint8_t object_cell(const PlatformObject* object,
                           uint8_t world_x, uint8_t world_y,
                           uint8_t* ch, uint8_t* color) {
    const PlatformObjectType* type;
    int16_t left;
    int16_t top;
    int16_t local_x;
    int16_t local_y;
    uint8_t index;

    if (object == 0 || object->type == 0u) return 0;
    type = &platform_object_types[object->type];
    if (!object_type_is_valid(type)) return 0;

    left = (int16_t)object->x - HOTSPOT_X(type);
    top = (int16_t)object->y - HOTSPOT_Y(type);
    local_x = (int16_t)world_x - left;
    local_y = (int16_t)world_y - top;
    if (local_x < 0 || local_y < 0 ||
        local_x >= OBJECT_WIDTH(type) || local_y >= OBJECT_HEIGHT(type)) {
        return 0;
    }

    index = (uint8_t)(local_y * OBJECT_WIDTH(type) + local_x);
    if (type->chars[index] == 0u) return 0;
    *ch = type->chars[index];
    *color = type->colors[index] & 0x0f;
    return 1;
}

static void base_cell(const PlatformRoom* room, uint8_t x, uint8_t y,
                      uint8_t* ch, uint8_t* color) {
    uint8_t tile;
    uint8_t quadrant;
    const uint8_t* definition;

    tile = room->tiles[(uint16_t)(y >> 1) * PLATFORM_MAP_WIDTH + (x >> 1)];
    quadrant = (uint8_t)(((y & 1u) << 1) | (x & 1u));
    definition = tile_data + ((uint16_t)tile << 3);
    *ch = definition[quadrant << 1];
    *color = definition[(quadrant << 1) + 1u] & 0x0f;
}

static void compose_cell(const PlatformRoom* room, uint8_t x, uint8_t y,
                         const PlatformObject* player,
                         uint8_t* ch, uint8_t* color) {
    uint16_t i;
    uint8_t object_ch;
    uint8_t object_color;

    base_cell(room, x, y, ch, color);
    for (i = 0; i < PLATFORM_ROOM_OBJECT_COUNT; ++i) {
        if (player == &room->objects[i]) continue;
        if (object_cell(&room->objects[i], x, y, &object_ch, &object_color)) {
            *ch = object_ch;
            *color = object_color;
        }
    }
    if (object_cell(player, x, y, &object_ch, &object_color)) {
        *ch = object_ch;
        *color = object_color;
    }
}

static void dirty_clear(void) {
    memset(dirty_cells, 0, sizeof(dirty_cells));
}

static void dirty_set(uint8_t x, uint8_t y) {
    uint16_t cell;
    if (x >= PLATFORM_MAP_CHAR_WIDTH || y >= PLATFORM_MAP_CHAR_HEIGHT) return;
    cell = (uint16_t)y * PLATFORM_MAP_CHAR_WIDTH + x;
    dirty_cells[cell >> 3] |= (uint8_t)(1u << (cell & 7u));
}

static uint8_t dirty_get(uint16_t cell) {
    return dirty_cells[cell >> 3] & (uint8_t)(1u << (cell & 7u));
}

static void mark_object_cells(const PlatformObject* object) {
    const PlatformObjectType* type;
    int16_t left;
    int16_t top;
    uint8_t x;
    uint8_t y;
    uint8_t index;
    int16_t world_x;
    int16_t world_y;

    if (object == 0 || object->type == 0u) return;
    type = &platform_object_types[object->type];
    if (!object_type_is_valid(type)) return;
    left = (int16_t)object->x - HOTSPOT_X(type);
    top = (int16_t)object->y - HOTSPOT_Y(type);
    index = 0;
    for (y = 0; y < OBJECT_HEIGHT(type); ++y) {
        for (x = 0; x < OBJECT_WIDTH(type); ++x, ++index) {
            world_x = left + (int16_t)x;
            world_y = top + (int16_t)y;
            if (type->chars[index] != 0u && world_x >= 0 && world_y >= 0) {
                dirty_set((uint8_t)world_x, (uint8_t)world_y);
            }
        }
    }
}

static void redraw_dirty(const PlatformRoom* room,
                         const PlatformObject* player) {
    uint16_t cell;
    uint8_t x;
    uint8_t y;
    uint8_t ch;
    uint8_t color;
    uint16_t offset;
    uint8_t visible_color;

    for (cell = 0; cell < PLATFORM_MAP_CHAR_WIDTH * PLATFORM_MAP_CHAR_HEIGHT; ++cell) {
        if (!dirty_get(cell)) continue;
        x = (uint8_t)(cell % PLATFORM_MAP_CHAR_WIDTH);
        y = (uint8_t)(cell / PLATFORM_MAP_CHAR_WIDTH);
        compose_cell(room, x, y, player, &ch, &color);
        offset = cell;
        visible_color = color;
        if (overlay_visible && x >= overlay_x && x < overlay_x + 24u &&
            y >= overlay_y && y < overlay_y + 3u) {
            overlay_saved_colors[(y - overlay_y) * 24u + (x - overlay_x)] = color;
            visible_color = platform_overlay_gray[color];
        }
        if (P_SCREEN_RAM[offset] != ch) P_SCREEN_RAM[offset] = ch;
        if (P_COLOR_RAM[offset] != visible_color) P_COLOR_RAM[offset] = visible_color;
    }
}

void platform_init(void) {
    uint16_t i;

    *((volatile uint8_t*)0xdd00) |= 0x03;
    P_VIC(0x20) = 0;
    P_VIC(0x21) = 0;
    P_VIC(0x18) = 0x18;
    overlay_visible = 0;
    P_VIC(0x15) = 0;
    for (i = 0; i < 512u; ++i) P_SPRITE_DATA[i] = 0;
    platform_room_clear(&platform_room, 0);
    memset(&platform_player, 0, sizeof(platform_player));
    memset(platform_object_types, 0, sizeof(platform_object_types));
    raster_irq_install();
}

void platform_room_clear(PlatformRoom* room, uint8_t room_id) {
    if (room == 0) return;
    memset(room, 0, sizeof(*room));
    room->width = PLATFORM_MAP_WIDTH;
    room->height = PLATFORM_MAP_HEIGHT;
    room->id = room_id;
    room->format = 1;
}

uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id, uint8_t device) {
    char filename[3];
    uint8_t status;

    if (room == 0) return PLATFORM_ERR_ARGUMENT;
    filename[0] = hex_digits[room_id >> 4];
    filename[1] = hex_digits[room_id & 0x0f];
    filename[2] = '\0';
    if (cbm_open(ROOM_LFN, device, CBM_READ, filename) != 0u) return PLATFORM_ERR_IO;
    status = read_exact(ROOM_LFN, room, sizeof(*room));
    cbm_close(ROOM_LFN);
    if (status != PLATFORM_OK) return status;
    if (room->width != PLATFORM_MAP_WIDTH || room->height != PLATFORM_MAP_HEIGHT ||
        room->id != room_id || room->format != 1u) {
        return PLATFORM_ERR_FORMAT;
    }
    return PLATFORM_OK;
}

uint8_t platform_object_types_load(const char* filename, uint8_t device) {
    uint8_t status;
    if (filename == 0) return PLATFORM_ERR_ARGUMENT;
    if (cbm_open(ROOM_LFN, device, CBM_READ, filename) != 0u) return PLATFORM_ERR_IO;
    status = read_exact(ROOM_LFN, platform_object_types,
                        sizeof(platform_object_types));
    cbm_close(ROOM_LFN);
    return status;
}

void platform_map_draw_tile(uint8_t tile, uint8_t tile_x, uint8_t tile_y) {
    const uint8_t* definition;
    uint8_t x;
    uint8_t y;

    if (tile_x >= PLATFORM_MAP_WIDTH || tile_y >= PLATFORM_MAP_HEIGHT) return;
    definition = tile_data + ((uint16_t)tile << 3);
    x = tile_x << 1;
    y = tile_y << 1;
    write_screen_cell(x, y, definition[0], definition[1]);
    write_screen_cell(x + 1u, y, definition[2], definition[3]);
    write_screen_cell(x, y + 1u, definition[4], definition[5]);
    write_screen_cell(x + 1u, y + 1u, definition[6], definition[7]);
}

void platform_map_draw(const PlatformRoom* room) {
    uint8_t x;
    uint8_t y;
    if (room == 0) return;
    for (y = 0; y < PLATFORM_MAP_HEIGHT; ++y) {
        for (x = 0; x < PLATFORM_MAP_WIDTH; ++x) {
            platform_map_draw_tile(room->tiles[(uint16_t)y * PLATFORM_MAP_WIDTH + x], x, y);
        }
    }
}

void platform_object_draw(const PlatformObject* object) {
    const PlatformObjectType* type;
    int16_t left;
    int16_t top;
    uint8_t x;
    uint8_t y;
    uint8_t index;
    int16_t world_x;
    int16_t world_y;

    if (object == 0 || object->type == 0u) return;
    type = &platform_object_types[object->type];
    if (!object_type_is_valid(type)) return;
    left = (int16_t)object->x - HOTSPOT_X(type);
    top = (int16_t)object->y - HOTSPOT_Y(type);
    index = 0;
    for (y = 0; y < OBJECT_HEIGHT(type); ++y) {
        for (x = 0; x < OBJECT_WIDTH(type); ++x, ++index) {
            world_x = left + (int16_t)x;
            world_y = top + (int16_t)y;
            if (type->chars[index] != 0u && world_x >= 0 && world_y >= 0 &&
                world_x < PLATFORM_MAP_CHAR_WIDTH && world_y < PLATFORM_MAP_CHAR_HEIGHT) {
                write_screen_cell((uint8_t)world_x, (uint8_t)world_y,
                                  type->chars[index], type->colors[index]);
            }
        }
    }
}

void platform_room_draw(const PlatformRoom* room, const PlatformObject* player) {
    uint16_t i;
    if (room == 0) return;
    platform_map_draw(room);
    for (i = 0; i < PLATFORM_ROOM_OBJECT_COUNT; ++i) {
        if (player == &room->objects[i]) continue;
        platform_object_draw(&room->objects[i]);
    }
    platform_object_draw(player);
}

void platform_object_move(PlatformRoom* room, PlatformObject* object,
                          uint8_t new_x, uint8_t new_y,
                          const PlatformObject* player) {
    if (room == 0 || object == 0 || object->type == 0u) return;
    dirty_clear();
    mark_object_cells(object);
    object->x = new_x;
    object->y = new_y;
    mark_object_cells(object);
    redraw_dirty(room, player);
}

uint16_t platform_room_object_count(const PlatformRoom* room,
                                    uint8_t actor_only) {
    uint16_t i;
    uint16_t count;
    uint8_t actor;
    if (room == 0) return 0;
    count = 0;
    for (i = 0; i < PLATFORM_ROOM_OBJECT_COUNT; ++i) {
        if (room->objects[i].type == 0u) continue;
        actor = platform_object_types[room->objects[i].type].reserved[0] &
                PLATFORM_OBJECT_FLAG_ACTOR;
        if (actor_only == 0u || (actor_only == 1u && actor) ||
            (actor_only == 2u && !actor)) {
            ++count;
        }
    }
    return count;
}

uint8_t platform_room_object_add(PlatformRoom* room, uint8_t type,
                                 uint8_t x, uint8_t y, uint8_t* out_slot) {
    uint16_t i;
    uint8_t is_actor;

    if (room == 0 || type == 0u || !object_type_is_valid(&platform_object_types[type])) {
        return PLATFORM_ERR_ARGUMENT;
    }
    is_actor = platform_object_types[type].reserved[0] & PLATFORM_OBJECT_FLAG_ACTOR;
    if (!is_actor && platform_room_object_count(room, 2u) >= PLATFORM_NON_ACTOR_LIMIT) {
        return PLATFORM_ERR_LIMIT;
    }
    for (i = 0; i < PLATFORM_ROOM_OBJECT_COUNT; ++i) {
        if (room->objects[i].type == 0u) {
            room->objects[i].type = type;
            room->objects[i].x = x;
            room->objects[i].y = y;
            if (out_slot != 0) *out_slot = (uint8_t)i;
            return PLATFORM_OK;
        }
    }
    return PLATFORM_ERR_FULL;
}

uint8_t platform_room_object_remove(PlatformRoom* room, uint8_t slot,
                                    const PlatformObject* player) {
    PlatformObject* object;
    if (room == 0) return PLATFORM_ERR_ARGUMENT;
    object = &room->objects[slot];
    if (object->type == 0u) return PLATFORM_ERR_ARGUMENT;
    dirty_clear();
    mark_object_cells(object);
    object->type = 0;
    object->x = 0;
    object->y = 0;
    redraw_dirty(room, player);
    return PLATFORM_OK;
}

uint8_t platform_room_object_transfer(PlatformRoom* leaving,
                                      PlatformRoom* entering,
                                      uint8_t leaving_slot,
                                      uint8_t new_x, uint8_t new_y,
                                      uint8_t* entering_slot) {
    PlatformObject object;
    uint8_t result;
    if (leaving == 0 || entering == 0) return PLATFORM_ERR_ARGUMENT;
    object = leaving->objects[leaving_slot];
    if (object.type == 0u) return PLATFORM_ERR_ARGUMENT;
    result = platform_room_object_add(entering, object.type, new_x, new_y,
                                      entering_slot);
    if (result != PLATFORM_OK) return result;
    leaving->objects[leaving_slot].type = 0;
    leaving->objects[leaving_slot].x = 0;
    leaving->objects[leaving_slot].y = 0;
    return PLATFORM_OK;
}

uint8_t platform_transition_check(const PlatformRoom* room,
                                  const PlatformObject* object,
                                  int8_t delta_x, int8_t delta_y,
                                  uint8_t* stepped_tile) {
    int16_t x;
    int16_t y;
    uint8_t tile;

    if (room == 0 || object == 0 || object->type == 0u) {
        return PLATFORM_TRANSITION_NONE;
    }
    x = (int16_t)object->x + delta_x;
    y = (int16_t)object->y + delta_y;
    if (y < 0) return PLATFORM_TRANSITION_TOP;
    if (x < 0) return PLATFORM_TRANSITION_LEFT;
    if (y >= PLATFORM_MAP_CHAR_HEIGHT) return PLATFORM_TRANSITION_BOTTOM;
    if (x >= PLATFORM_MAP_CHAR_WIDTH) return PLATFORM_TRANSITION_RIGHT;

    tile = room->tiles[(uint16_t)(y >> 1) * PLATFORM_MAP_WIDTH + (x >> 1)];
    if (stepped_tile != 0) *stepped_tile = tile;
    if (tile_properties[tile] & 0x10u) return PLATFORM_TRANSITION_TRIGGER;
    return PLATFORM_TRANSITION_NONE;
}

uint8_t platform_text_screen_code(char ch) {
    if (ch >= 'a' && ch <= 'z') return (uint8_t)(ch - 'a' + 1);
    if (ch >= 'A' && ch <= 'Z') return (uint8_t)(ch - 'A' + 65);
    return (uint8_t)ch;
}

void platform_text_clear_line(uint8_t line) {
    uint8_t x;
    uint8_t y;
    if (line > PLATFORM_TEXT_LINE_BOTTOM) return;
    y = (uint8_t)(23u + line);
    for (x = 0; x < PLATFORM_MAP_CHAR_WIDTH; ++x) {
        write_screen_cell(x, y, 32, 0);
    }
}

void platform_text_write_line(uint8_t line, uint8_t column,
                              const char* text, uint8_t color) {
    uint8_t y;
    if (line > PLATFORM_TEXT_LINE_BOTTOM || column >= PLATFORM_MAP_CHAR_WIDTH || text == 0) return;
    y = (uint8_t)(23u + line);
    while (*text != '\0' && column < PLATFORM_MAP_CHAR_WIDTH) {
        write_screen_cell(column, y, platform_text_screen_code(*text++), color);
        ++column;
    }
}

void platform_text_write_room_line(const PlatformRoom* room, uint8_t line,
                                   uint8_t column, uint8_t text_offset,
                                   uint8_t color) {
    uint16_t i;
    uint8_t y;
    if (room == 0 || line > PLATFORM_TEXT_LINE_BOTTOM || column >= PLATFORM_MAP_CHAR_WIDTH) return;
    y = (uint8_t)(23u + line);
    i = text_offset;
    while (i < PLATFORM_ROOM_TEXT_BYTES && room->text[i] != 0u &&
           column < PLATFORM_MAP_CHAR_WIDTH) {
        write_screen_cell(column++, y,
                          platform_text_screen_code((char)room->text[i++]), color);
    }
}

static void sprite_pixel(uint8_t x, uint8_t y) {
    uint8_t sprite;
    uint8_t local_x;
    uint16_t offset;
    sprite = x / 24u;
    local_x = x % 24u;
    offset = (uint16_t)sprite * 64u + (uint16_t)y * 3u + (local_x >> 3);
    P_SPRITE_DATA[offset] |= (uint8_t)(0x80u >> (local_x & 7u));
}

static void overlay_render_line(const PlatformRoom* room,
                                uint8_t line, uint8_t text_offset) {
    uint16_t text_index;
    uint8_t column;
    uint8_t glyph_index;
    uint8_t row;
    uint8_t bits;
    uint8_t pixel;
    uint8_t ch;

    text_index = text_offset;
    for (column = 0; column < 48u && text_index < PLATFORM_ROOM_TEXT_BYTES; ++column) {
        ch = room->text[text_index++];
        if (ch == 0u) break;
        if (ch >= 'a' && ch <= 'z') ch = (uint8_t)(ch - 'a' + 'A');
        glyph_index = (ch >= 32u && ch <= 95u) ? (uint8_t)(ch - 32u) : 31u;
        for (row = 0; row < 7u; ++row) {
            bits = font4x7[glyph_index][row];
            for (pixel = 0; pixel < 4u; ++pixel) {
                if (bits & (uint8_t)(8u >> pixel)) {
                    sprite_pixel((uint8_t)(column * 4u + pixel),
                                 (uint8_t)(line * 7u + row));
                }
            }
        }
    }
}

uint8_t platform_overlay_show(const PlatformRoom* room,
                              uint8_t half_x, uint8_t half_y,
                              uint8_t line0_offset,
                              uint8_t line1_offset,
                              uint8_t line2_offset,
                              uint8_t sprite_color) {
    uint8_t x;
    uint8_t y;
    uint8_t i;
    uint16_t sprite_x;
    uint8_t high_x;
    uint16_t offset;

    if (room == 0 || half_x > 16u || half_y > 19u) return PLATFORM_ERR_ARGUMENT;
    platform_overlay_hide();
    memset(P_SPRITE_DATA, 0, 512u);
    overlay_render_line(room, 0, line0_offset);
    overlay_render_line(room, 1, line1_offset);
    overlay_render_line(room, 2, line2_offset);

    overlay_x = half_x;
    overlay_y = half_y;
    high_x = 0;
    for (y = 0; y < 3u; ++y) {
        for (x = 0; x < 24u; ++x) {
            offset = (uint16_t)(half_y + y) * PLATFORM_MAP_CHAR_WIDTH + half_x + x;
            overlay_saved_colors[(uint16_t)y * 24u + x] = P_COLOR_RAM[offset] & 0x0f;
            P_COLOR_RAM[offset] = platform_overlay_gray[P_COLOR_RAM[offset] & 0x0f];
        }
    }

    for (i = 0; i < 8u; ++i) {
        P_SPRITE_POINTERS[i] = (uint8_t)(0x3a00u / 64u + i);
        sprite_x = (uint16_t)24u + (uint16_t)half_x * 8u + (uint16_t)i * 24u;
        P_VIC(i << 1) = (uint8_t)sprite_x;
        if (sprite_x & 0x100u) high_x |= (uint8_t)(1u << i);
        P_VIC((i << 1) + 1u) = (uint8_t)(51u + (uint16_t)half_y * 8u);
        P_VIC(0x27u + i) = sprite_color & 0x0f;
    }
    P_VIC(0x10) = high_x;
    P_VIC(0x17) = 0;
    P_VIC(0x1b) = 0;
    P_VIC(0x1c) = 0;
    P_VIC(0x1d) = 0;
    overlay_visible = 1;
    P_VIC(0x15) = 0xff;
    return PLATFORM_OK;
}

void platform_overlay_hide(void) {
    uint8_t x;
    uint8_t y;
    uint16_t offset;
    P_VIC(0x15) = 0;
    if (!overlay_visible) return;
    for (y = 0; y < 3u; ++y) {
        for (x = 0; x < 24u; ++x) {
            offset = (uint16_t)(overlay_y + y) * PLATFORM_MAP_CHAR_WIDTH + overlay_x + x;
            P_COLOR_RAM[offset] = overlay_saved_colors[(uint16_t)y * 24u + x];
        }
    }
    overlay_visible = 0;
}

uint8_t platform_overlay_is_visible(void) {
    return overlay_visible;
}
