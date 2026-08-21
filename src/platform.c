#include <cbm.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"
#include "game.h"

#pragma code-name ("HIGHCODE")
#pragma rodata-name ("HIGHRODATA")

#define P_SCREEN_RAM       ((uint8_t*)0x0400)
#define P_COLOR_RAM        ((volatile uint8_t*)0xd800)
#define P_SPRITE_POINTERS  ((uint8_t*)0x07f8)
#define P_SPRITE_DATA      ((uint8_t*)0x3a00)
#define P_OBJECT_TYPE_STAGE ((uint8_t*)0xa4e9)
#define P_VIC(reg)         (((volatile uint8_t*)0xd000)[(reg)])
#define DIRTY_BYTES        110u
#define DIRTY_CELL_LIMIT   32u
#define ROOM_LFN           2u
#define INITIAL_OBJECT_TYPE_COUNT 2u
#define EF_FIRST_ROOM_BANK 3u
#define EF_ROOMS_PER_BANK  6u
#define EF_TYPE_BANK_0     46u
#define EF_TYPE_BANK_1     47u
#define WALL_CACHE_QUADRANT_NW 0x01u
#define WALL_CACHE_QUADRANT_NE 0x02u
#define WALL_CACHE_QUADRANT_SW 0x04u
#define WALL_CACHE_QUADRANT_SE 0x08u

#define OBJECT_WIDTH(t)  ((uint8_t)((t)->dimensions >> 4))
#define OBJECT_HEIGHT(t) ((uint8_t)((t)->dimensions & 0x0f))
#define HOTSPOT_X(t)     ((uint8_t)((t)->hotspot >> 4))
#define HOTSPOT_Y(t)     ((uint8_t)((t)->hotspot & 0x0f))

extern const uint8_t tile_data[];
extern const uint8_t tile_properties[];
extern const uint8_t initial_room_data[];
extern const uint8_t initial_object_type_data[];
void raster_irq_install(void);
void raster_irq_vectors_restore(void);
void raster_irq_suspend(void);
void raster_irq_resume(void);
void platform_memory_game(void);
void platform_memory_kernal(void);
void platform_memory_all_ram(void);
void __fastcall__ platform_object_type_stage(uint8_t type_id);
void platform_easyflash_copy_roml(void);
extern uint8_t platform_ef_copy_bank;
extern uint16_t platform_ef_copy_offset;
extern uint16_t platform_ef_copy_destination;
extern uint16_t platform_ef_copy_size;
uint8_t platform_irq_save_disable(void);
void __fastcall__ platform_irq_restore(uint8_t status);
void platform_object_types_clear(void);
uint8_t platform_boot_is_easyflash(void);
void __fastcall__ platform_map_draw_native(const PlatformRoom* room);
void platform_object_draw_native(void);
void platform_lighting_apply_native(void);
void platform_lightning_native(void);
void platform_light_source_apply_native(void);
void platform_visibility_build_native(void);
void platform_color_clear_native(void);
void platform_text_area_clear_native(void);
void __fastcall__ platform_text_output_native(const char* text);
extern uint8_t platform_text_output_color;
extern uint8_t platform_text_output_line;

#pragma bss-name (push, "ROOMBSS")
PlatformRoom platform_room;
#pragma bss-name (pop)
uint8_t platform_current_room;
uint8_t platform_player_slot;
PlatformObject* platform_player;
#pragma bss-name (push, "OBJECTTYPES")
PlatformObjectType platform_object_types[PLATFORM_OBJECT_TYPE_COUNT];
#pragma bss-name (pop)
PlatformObjectType platform_object_type_scratch;
PlatformStorage platform_storage;
uint8_t platform_storage_device;
const PlatformObjectType* native_object_type;
uint8_t native_object_source;
uint8_t native_object_columns;
uint8_t native_object_rows;
uint8_t native_object_row_skip;
uint16_t native_object_screen_offset;
uint8_t native_light_source_x;
uint8_t native_light_source_y;
uint8_t native_light_radius;
uint8_t native_light_min_x;
uint8_t native_light_min_y;
uint8_t native_light_columns;
uint8_t native_light_rows;
uint16_t native_light_screen_offset;
uint8_t native_light_visibility_offset;
const PlatformRoom* native_visibility_room;
uint8_t* native_visibility_mask;
uint8_t native_visibility_origin_x;
uint8_t native_visibility_origin_y;
uint8_t native_visibility_origin_offset;
uint8_t native_visibility_max_ring;
#pragma bss-name (push, "WORKBSS")
uint8_t platform_base_colors[PLATFORM_MAP_CHAR_WIDTH * PLATFORM_MAP_CHAR_HEIGHT];
uint8_t platform_brightness[PLATFORM_MAP_CHAR_WIDTH * PLATFORM_MAP_CHAR_HEIGHT];
#pragma bss-name (pop)
uint8_t platform_global_light;
uint8_t platform_light_visibility[PLATFORM_MAP_TILE_COUNT];
uint8_t platform_view_tiles[PLATFORM_MAP_TILE_COUNT];

#pragma rodata-name (push, "RODATA")
const uint8_t platform_light_colors[PLATFORM_LIGHT_LEVEL_COUNT * 16u] = {
    /* no light */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* dim: only bright colors remain visible, as blue */
    0, 6, 0, 6, 0, 0, 0, 6, 0, 0, 6, 0, 6, 6, 6, 6,
    /* twilight: hue-preserving darker C64 palette entries */
    0, 12, 9, 6, 11, 11, 0, 8, 9, 11, 2, 0, 11, 5, 6, 12,
    /* full light */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
/* Ceiling Euclidean distance for abs(y),abs(x) in the first 16x16 quadrant. */
const uint8_t platform_light_distance[16u * 16u] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    2, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    4, 5, 5, 5, 6, 7, 8, 9, 9, 10, 11, 12, 13, 14, 15, 16,
    5, 6, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 13, 14, 15, 16,
    6, 7, 7, 7, 8, 8, 9, 10, 10, 11, 12, 13, 14, 15, 16, 255,
    7, 8, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 14, 15, 16, 255,
    8, 9, 9, 9, 9, 10, 10, 11, 12, 13, 13, 14, 15, 16, 255, 255,
    9, 10, 10, 10, 10, 11, 11, 12, 13, 13, 14, 15, 15, 16, 255, 255,
    10, 11, 11, 11, 11, 12, 12, 13, 13, 14, 15, 15, 16, 255, 255, 255,
    11, 12, 12, 12, 12, 13, 13, 14, 14, 15, 15, 16, 255, 255, 255, 255,
    12, 13, 13, 13, 13, 13, 14, 14, 15, 15, 16, 255, 255, 255, 255, 255,
    13, 14, 14, 14, 14, 14, 15, 15, 16, 16, 255, 255, 255, 255, 255, 255,
    14, 15, 15, 15, 15, 15, 16, 16, 255, 255, 255, 255, 255, 255, 255, 255,
    15, 16, 16, 16, 16, 16, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};
static const uint8_t look_cursor_colors[8] = {0, 11, 12, 15, 1, 15, 12, 11};
static const uint8_t look_range_by_light[PLATFORM_LIGHT_LEVEL_COUNT] = {
    0, 2, 6, 0xff
};
#pragma rodata-name (pop)

static uint8_t dirty_cells[DIRTY_BYTES];
static uint8_t dirty_x[DIRTY_CELL_LIMIT];
static uint8_t dirty_y[DIRTY_CELL_LIMIT];
static uint8_t dirty_count;
static const PlatformRoom* rendered_room;
static const PlatformObject* rendered_player;
static uint16_t rendered_object_limit;
static uint8_t look_cursor_visible;
#pragma bss-name (push, "ROOMSTAGE")
static PlatformRoom room_stage;
#pragma bss-name (pop)
#pragma bss-name (push, "WORKBSS")
static uint8_t wall_light_cache[PLATFORM_MAP_CHAR_WIDTH * PLATFORM_MAP_CHAR_HEIGHT];
static uint8_t wall_tile_offsets[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_x[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_y[PLATFORM_MAP_TILE_COUNT];
static uint16_t wall_char_offsets[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_count;
static const PlatformRoom* wall_cache_room;
static uint8_t look_counts[PLATFORM_OBJECT_TYPE_COUNT];
static char look_buffer[81];
#pragma bss-name (pop)
static PlatformRoomStoreHook room_store_hook;
static PlatformRoomRestoreHook room_restore_hook;
static uint8_t player_spawn_room;
static uint8_t player_spawn_slot;
static uint8_t player_spawn_type;
static uint8_t look_length;
static uint8_t look_truncated;
#pragma rodata-name (push, "RODATA")
static const char hex_digits[] = "0123456789ABCDEF";
#pragma rodata-name (pop)
#pragma rodata-name (push, "UPPERRODATA")
static const char take_prompt_prefix[] = "Take: ";
static const char take_prompt_arrows[] = "   < >";
#pragma rodata-name (pop)

#pragma code-name (push, "CODE")
static void write_screen_cell(uint8_t x, uint8_t y,
                              uint8_t ch, uint8_t color) {
    uint16_t offset;

    if (x >= PLATFORM_MAP_CHAR_WIDTH || y >= 25u) return;
    offset = (uint16_t)y * PLATFORM_MAP_CHAR_WIDTH + x;
    P_SCREEN_RAM[offset] = ch;
    color &= 0x0f;
    if (y < PLATFORM_MAP_CHAR_HEIGHT) {
        platform_base_colors[offset] = color;
        color = platform_light_colors[
            ((platform_brightness[offset] & 0x03u) << 4) | color];
        if (platform_view_tiles[(uint16_t)(y >> 1) * PLATFORM_MAP_WIDTH +
                                (x >> 1)] == 0u) color = 0u;
    }

    P_COLOR_RAM[offset] = color;
}
#pragma code-name (pop)

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

#pragma code-name (push, "UPPERCODE")
const PlatformObjectType* platform_object_type_get(uint8_t type_id) {
    if (type_id >= 64u && type_id < 128u) {
        platform_object_type_stage(type_id);
        return &platform_object_type_scratch;
    }
    return &platform_object_types[type_id];
}
#pragma code-name (pop)

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
    type = platform_object_type_get(object->type);
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
                         uint16_t object_limit,
                         uint8_t* ch, uint8_t* color) {
    uint16_t i;
    uint8_t object_ch;
    uint8_t object_color;

    base_cell(room, x, y, ch, color);
    for (i = 0; i < object_limit; ++i) {
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
    dirty_count = 0;
}

static void dirty_set(uint8_t x, uint8_t y) {
    uint16_t cell;
    uint8_t mask;
    if (x >= PLATFORM_MAP_CHAR_WIDTH || y >= PLATFORM_MAP_CHAR_HEIGHT) return;
    cell = (uint16_t)y * PLATFORM_MAP_CHAR_WIDTH + x;
    mask = (uint8_t)(1u << (cell & 7u));
    if (dirty_cells[cell >> 3] & mask) return;
    dirty_cells[cell >> 3] |= mask;
    if (dirty_count < DIRTY_CELL_LIMIT) {
        dirty_x[dirty_count] = x;
        dirty_y[dirty_count] = y;
        ++dirty_count;
    }
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
    type = platform_object_type_get(object->type);
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
    uint8_t i;
    uint8_t x;
    uint8_t y;
    uint8_t ch;
    uint8_t color;
    uint16_t offset;
    uint8_t visible_color;
    uint16_t object_limit;

    if (room == rendered_room) {
        object_limit = rendered_object_limit;
    } else {
        object_limit = PLATFORM_ROOM_OBJECT_COUNT;
        while (object_limit > 0u && room->objects[object_limit - 1u].type == 0u) {
            --object_limit;
        }
    }
    for (i = 0; i < dirty_count; ++i) {
        x = dirty_x[i];
        y = dirty_y[i];
        compose_cell(room, x, y, player, object_limit, &ch, &color);
        offset = (uint16_t)y * PLATFORM_MAP_CHAR_WIDTH + x;
        platform_base_colors[offset] = color;
        visible_color = platform_light_colors[
            ((platform_brightness[offset] & 0x03u) << 4) | (color & 0x0fu)];
        if (platform_view_tiles[(uint16_t)(y >> 1) * PLATFORM_MAP_WIDTH +
                                (x >> 1)] == 0u) visible_color = 0u;
        if (P_SCREEN_RAM[offset] != ch) P_SCREEN_RAM[offset] = ch;
        if (P_COLOR_RAM[offset] != visible_color) P_COLOR_RAM[offset] = visible_color;
    }
}

void platform_init(void) {
    uint8_t cartridge;

    cartridge = platform_boot_is_easyflash();
    *((volatile uint8_t*)0xdd00) |= 0x03;
    P_VIC(0x20) = 0;
    P_VIC(0x21) = 0;
    P_VIC(0x18) = 0x18;
    look_cursor_visible = 0;
    P_VIC(0x15) = 0;
    memset(platform_base_colors, 0, sizeof(platform_base_colors));
    memset(platform_brightness, PLATFORM_LIGHT_FULL, sizeof(platform_brightness));
    memset(platform_view_tiles, 1, sizeof(platform_view_tiles));
    platform_global_light = PLATFORM_LIGHT_FULL;
    wall_count = 0u;
    wall_cache_room = 0;
    (void)platform_irq_save_disable();
    if (!cartridge) {
        platform_object_types_clear();
        platform_memory_game();
        memcpy(platform_object_types, initial_object_type_data,
               INITIAL_OBJECT_TYPE_COUNT * sizeof(PlatformObjectType));
    } else {
        platform_memory_game();
    }
    memcpy(&platform_room, initial_room_data, sizeof(platform_room));
    platform_storage_init(cartridge ? PLATFORM_STORAGE_EASYFLASH : PLATFORM_STORAGE_DISK, 8u);
    if (cartridge) {
        (void)platform_object_types_load(0, 0);
        (void)platform_room_load(&platform_room, 0u);
    }
    platform_current_room = 0;
    platform_player_slot = 0;
    platform_player = &platform_room.objects[platform_player_slot];
    player_spawn_room = platform_current_room;
    player_spawn_slot = platform_player_slot;
    player_spawn_type = platform_player->type;
    raster_irq_install();
}

#pragma code-name (push, "LOWCODE")
void platform_storage_init(PlatformStorage storage, uint8_t device) {
    platform_storage = storage;
    platform_storage_device = device;
}
#pragma code-name (pop)

#pragma code-name (push, "LOWCODE")
void platform_room_state_hooks(PlatformRoomStoreHook store_hook,
                               PlatformRoomRestoreHook restore_hook) {
    room_store_hook = store_hook;
    room_restore_hook = restore_hook;
}
#pragma code-name (pop)

#pragma code-name (push, "MIDCODE")
void platform_room_clear(PlatformRoom* room, uint8_t room_id) {
    if (room == 0) return;
    if (room == rendered_room) {
        rendered_room = 0;
        rendered_player = 0;
    }
    memset(room, 0, sizeof(*room));
    room->width = PLATFORM_MAP_WIDTH;
    room->height = PLATFORM_MAP_HEIGHT;
    room->id = room_id;
    room->format = PLATFORM_ROOM_FORMAT;
}
#pragma code-name (pop)

static uint8_t room_validate(const PlatformRoom* room, uint8_t room_id) {
    if (room->width != PLATFORM_MAP_WIDTH || room->height != PLATFORM_MAP_HEIGHT ||
        room->id != room_id || room->format != PLATFORM_ROOM_FORMAT ||
        (room->exit_mask & 0xf0u) != 0u) {
        return PLATFORM_ERR_FORMAT;
    }
    return PLATFORM_OK;
}

static uint8_t room_load_disk(uint8_t room_id) {
    char filename[3];
    uint8_t status;

    filename[0] = hex_digits[room_id >> 4];
    filename[1] = hex_digits[room_id & 0x0f];
    filename[2] = '\0';
    platform_memory_kernal();
    if (cbm_open(ROOM_LFN, platform_storage_device, CBM_READ, filename) != 0u) {
        platform_memory_game();
        return PLATFORM_ERR_IO;
    }
    status = read_exact(ROOM_LFN, &room_stage, sizeof(room_stage));
    cbm_close(ROOM_LFN);
    platform_memory_game();
    return status;
}

static uint8_t room_load_easyflash(uint8_t room_id) {
    platform_ef_copy_bank =
        (uint8_t)(EF_FIRST_ROOM_BANK + room_id / EF_ROOMS_PER_BANK);
    platform_ef_copy_offset =
        (uint16_t)(room_id % EF_ROOMS_PER_BANK) * PLATFORM_ROOM_FILE_BYTES;
    platform_ef_copy_destination = (uint16_t)&room_stage;
    platform_ef_copy_size = sizeof(room_stage);
    platform_easyflash_copy_roml();
    return PLATFORM_OK;
}

static uint8_t room_stage_load(uint8_t room_id) {
    uint8_t status;
    status = platform_storage == PLATFORM_STORAGE_EASYFLASH
                 ? room_load_easyflash(room_id) : room_load_disk(room_id);
    if (status != PLATFORM_OK) return status;
    return room_validate(&room_stage, room_id);
}

static void room_commit(PlatformRoom* room, uint8_t room_id) {
    if (room == rendered_room) {
        rendered_room = 0;
        rendered_player = 0;
    }
    if (room == wall_cache_room) wall_cache_room = 0;
    memcpy(room, &room_stage, sizeof(*room));
    if (room == &platform_room) {
        platform_current_room = room_id;
        platform_player = &platform_room.objects[platform_player_slot];
    }
}

uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id) {
    uint8_t status;
    if (room == 0) return PLATFORM_ERR_ARGUMENT;
    status = room_stage_load(room_id);
    if (status != PLATFORM_OK) return status;
    room_commit(room, room_id);
    return PLATFORM_OK;
}

uint8_t platform_room_neighbor(const PlatformRoom* room, uint8_t direction,
                               uint8_t* room_id) {
    uint8_t mask;
    uint8_t neighbor;
    if (room == 0 || room_id == 0) return PLATFORM_ERR_ARGUMENT;
    switch (direction) {
        case PLATFORM_DIRECTION_NORTH:
            mask = PLATFORM_ROOM_EXIT_NORTH;
            neighbor = room->north;
            break;
        case PLATFORM_DIRECTION_EAST:
            mask = PLATFORM_ROOM_EXIT_EAST;
            neighbor = room->east;
            break;
        case PLATFORM_DIRECTION_WEST:
            mask = PLATFORM_ROOM_EXIT_WEST;
            neighbor = room->west;
            break;
        case PLATFORM_DIRECTION_SOUTH:
            mask = PLATFORM_ROOM_EXIT_SOUTH;
            neighbor = room->south;
            break;
        default:
            return PLATFORM_ERR_ARGUMENT;
    }
    if ((room->exit_mask & mask) == 0u) return PLATFORM_ERR_NOT_FOUND;
    *room_id = neighbor;
    return PLATFORM_OK;
}

static uint8_t object_types_load_easyflash(void) {
    uint8_t irq_status;

    platform_ef_copy_bank = EF_TYPE_BANK_0;
    platform_ef_copy_offset = 0u;
    platform_ef_copy_destination = 0xc000u;
    platform_ef_copy_size = 4096u;
    platform_easyflash_copy_roml();

    platform_ef_copy_offset = 4096u;
    platform_ef_copy_destination = (uint16_t)P_OBJECT_TYPE_STAGE;
    platform_ef_copy_size = 4096u;
    platform_easyflash_copy_roml();
    irq_status = platform_irq_save_disable();
    platform_memory_all_ram();
    memcpy(&platform_object_types[64], P_OBJECT_TYPE_STAGE, 4096u);
    platform_memory_game();
    platform_irq_restore(irq_status);

    platform_ef_copy_bank = EF_TYPE_BANK_1;
    platform_ef_copy_offset = 0u;
    platform_ef_copy_destination = 0xe000u;
    platform_ef_copy_size = 8192u;
    platform_easyflash_copy_roml();
    raster_irq_vectors_restore();
    return PLATFORM_OK;
}

uint8_t platform_object_types_load(const char* filename, uint8_t device) {
    uint8_t status;
    uint8_t irq_status;
    uint16_t i;
    if (platform_storage == PLATFORM_STORAGE_EASYFLASH) {
        return object_types_load_easyflash();
    }
    if (filename == 0) return PLATFORM_ERR_ARGUMENT;
    platform_memory_kernal();
    if (cbm_open(ROOM_LFN, device, CBM_READ, filename) != 0u) {
        platform_memory_game();
        return PLATFORM_ERR_IO;
    }
    status = PLATFORM_OK;
    for (i = 0; i < PLATFORM_OBJECT_TYPE_COUNT; ++i) {
        status = read_exact(ROOM_LFN, &platform_object_type_scratch,
                            sizeof(platform_object_type_scratch));
        if (status != PLATFORM_OK) break;
        irq_status = platform_irq_save_disable();
        if (i >= 64u && i < 128u) platform_memory_all_ram();
        else platform_memory_game();
        memcpy(&platform_object_types[i], &platform_object_type_scratch,
               sizeof(platform_object_type_scratch));
        platform_memory_kernal();
        platform_irq_restore(irq_status);
    }
    cbm_close(ROOM_LFN);
    raster_irq_vectors_restore();
    platform_memory_game();
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
    if (room == 0) return;
    platform_look_cursor_hide();
    platform_map_draw_native(room);
    platform_lighting_rebuild(room, room == rendered_room ? rendered_player : 0);
}

static void object_draw_base(const PlatformObject* object) {
    const PlatformObjectType* type;
    int16_t left;
    int16_t top;
    uint8_t source_x;
    uint8_t source_y;
    uint8_t end_x;
    uint8_t end_y;

    if (object == 0 || object->type == 0u) return;
    type = platform_object_type_get(object->type);
    if (!object_type_is_valid(type)) return;
    left = (int16_t)object->x - HOTSPOT_X(type);
    top = (int16_t)object->y - HOTSPOT_Y(type);
    source_x = left < 0 ? (uint8_t)-left : 0u;
    source_y = top < 0 ? (uint8_t)-top : 0u;
    end_x = OBJECT_WIDTH(type);
    end_y = OBJECT_HEIGHT(type);
    if (left + end_x > PLATFORM_MAP_CHAR_WIDTH) {
        end_x = (uint8_t)(PLATFORM_MAP_CHAR_WIDTH - left);
    }
    if (top + end_y > PLATFORM_MAP_CHAR_HEIGHT) {
        end_y = (uint8_t)(PLATFORM_MAP_CHAR_HEIGHT - top);
    }
    if (source_x >= end_x || source_y >= end_y) return;
    native_object_type = type;
    native_object_source = (uint8_t)(source_y * OBJECT_WIDTH(type) + source_x);
    native_object_columns = (uint8_t)(end_x - source_x);
    native_object_rows = (uint8_t)(end_y - source_y);
    native_object_row_skip = (uint8_t)(OBJECT_WIDTH(type) - native_object_columns);
    native_object_screen_offset =
        (uint16_t)(top + source_y) * PLATFORM_MAP_CHAR_WIDTH + left + source_x;
    platform_object_draw_native();
}

void platform_object_draw(const PlatformObject* object) {
    platform_look_cursor_hide();
    object_draw_base(object);
    platform_lighting_apply();
}

static void visibility_build(const PlatformRoom* room,
                             uint8_t origin_x, uint8_t origin_y,
                             uint8_t min_x, uint8_t max_x,
                             uint8_t min_y, uint8_t max_y,
                             uint8_t* mask) {
    uint8_t max_ring;
    uint8_t distance;

    native_visibility_room = room;
    native_visibility_mask = mask;
    native_visibility_origin_x = origin_x;
    native_visibility_origin_y = origin_y;
    native_visibility_origin_offset =
        origin_y * PLATFORM_MAP_WIDTH + origin_x;
    max_ring = origin_x - min_x;
    distance = max_x - origin_x;
    if (distance > max_ring) max_ring = distance;
    distance = origin_y - min_y;
    if (distance > max_ring) max_ring = distance;
    distance = max_y - origin_y;
    if (distance > max_ring) max_ring = distance;
    native_visibility_max_ring = max_ring;
    platform_visibility_build_native();
}

static void view_rebuild(const PlatformRoom* room,
                         const PlatformObject* player) {
    uint8_t tile_x;
    uint8_t tile_y;
    if (room == 0 || player == 0 || player->type == 0u ||
        player->x >= PLATFORM_MAP_CHAR_WIDTH ||
        player->y >= PLATFORM_MAP_CHAR_HEIGHT) {
        memset(platform_view_tiles, 1, sizeof(platform_view_tiles));
        return;
    }
    tile_x = player->x >> 1;
    tile_y = player->y >> 1;
    visibility_build(room, tile_x, tile_y, 0u, PLATFORM_MAP_WIDTH - 1u,
                     0u, PLATFORM_MAP_HEIGHT - 1u, platform_view_tiles);
}

static uint8_t wall_quadrants(uint8_t point_x, uint8_t point_y,
                              uint8_t tile_x, uint8_t tile_y) {
    uint8_t quadrants;
    quadrants = 0u;
    if (point_x <= tile_x) {
        if (point_y <= tile_y) quadrants |= WALL_CACHE_QUADRANT_NW;
        if (point_y >= tile_y) quadrants |= WALL_CACHE_QUADRANT_SW;
    }
    if (point_x >= tile_x) {
        if (point_y <= tile_y) quadrants |= WALL_CACHE_QUADRANT_NE;
        if (point_y >= tile_y) quadrants |= WALL_CACHE_QUADRANT_SE;
    }
    return quadrants;
}

static uint8_t light_level_at(uint8_t x, uint8_t y) {
    uint8_t delta_x;
    uint8_t delta_y;
    uint8_t distance;
    uint8_t remaining;

    delta_x = x < native_light_source_x ? native_light_source_x - x
                                        : x - native_light_source_x;
    delta_y = y < native_light_source_y ? native_light_source_y - y
                                        : y - native_light_source_y;
    if (delta_x > PLATFORM_LIGHT_MAX_RADIUS ||
        delta_y > PLATFORM_LIGHT_MAX_RADIUS) return PLATFORM_LIGHT_NONE;
    if (delta_y == PLATFORM_LIGHT_MAX_RADIUS) {
        if (delta_x != 0u) return PLATFORM_LIGHT_NONE;
        distance = PLATFORM_LIGHT_MAX_RADIUS;
    } else if (delta_x == PLATFORM_LIGHT_MAX_RADIUS) {
        if (delta_y != 0u) return PLATFORM_LIGHT_NONE;
        distance = PLATFORM_LIGHT_MAX_RADIUS;
    } else {
        distance = platform_light_distance[(uint16_t)delta_y * 16u + delta_x];
    }
    if (distance > native_light_radius) return PLATFORM_LIGHT_NONE;
    remaining = native_light_radius - distance;
    if (remaining >= 4u) return PLATFORM_LIGHT_FULL;
    if (remaining >= 2u) return PLATFORM_LIGHT_TWILIGHT;
    return PLATFORM_LIGHT_DIM;
}

static uint8_t packed_light_max(uint8_t packed, uint8_t quadrants) {
    uint8_t level;
    uint8_t candidate;
    level = PLATFORM_LIGHT_NONE;
    if (quadrants & WALL_CACHE_QUADRANT_NW) level = packed & 0x03u;
    if (quadrants & WALL_CACHE_QUADRANT_NE) {
        candidate = (packed >> 2) & 0x03u;
        if (candidate > level) level = candidate;
    }
    if (quadrants & WALL_CACHE_QUADRANT_SW) {
        candidate = (packed >> 4) & 0x03u;
        if (candidate > level) level = candidate;
    }
    if (quadrants & WALL_CACHE_QUADRANT_SE) {
        candidate = packed >> 6;
        if (candidate > level) level = candidate;
    }
    return level;
}

static uint8_t packed_light_add(uint8_t packed, uint8_t quadrants,
                                uint8_t level) {
    uint8_t candidate;
    candidate = packed & 0x03u;
    if ((quadrants & WALL_CACHE_QUADRANT_NW) && level > candidate) {
        packed = (packed & 0xfcu) | level;
    }
    candidate = (packed >> 2) & 0x03u;
    if ((quadrants & WALL_CACHE_QUADRANT_NE) && level > candidate) {
        packed = (packed & 0xf3u) | (level << 2);
    }
    candidate = (packed >> 4) & 0x03u;
    if ((quadrants & WALL_CACHE_QUADRANT_SW) && level > candidate) {
        packed = (packed & 0xcfu) | (level << 4);
    }
    candidate = packed >> 6;
    if ((quadrants & WALL_CACHE_QUADRANT_SE) && level > candidate) {
        packed = (packed & 0x3fu) | (level << 6);
    }
    return packed;
}

static void wall_cache_prepare(const PlatformRoom* room) {
    uint8_t x;
    uint8_t y;
    uint8_t tile_offset;
    uint16_t char_row;
    uint16_t char_offset;

    wall_count = 0u;
    tile_offset = 0u;
    char_row = 0u;
    for (y = 0u; y < PLATFORM_MAP_HEIGHT; ++y) {
        char_offset = char_row;
        for (x = 0u; x < PLATFORM_MAP_WIDTH; ++x, ++tile_offset) {
            if (tile_properties[room->tiles[tile_offset]] & PLATFORM_TILE_BLOCKS_VIEW) {
                wall_tile_offsets[wall_count] = tile_offset;
                wall_x[wall_count] = x;
                wall_y[wall_count] = y;
                wall_char_offsets[wall_count] = char_offset;
                memset(&wall_light_cache[(uint16_t)wall_count * 4u], 0, 4u);
                ++wall_count;
            }
            char_offset += 2u;
        }
        char_row += PLATFORM_MAP_CHAR_WIDTH * 2u;
    }
    wall_cache_room = room;
}

static void wall_cache_add_source(void) {
    uint8_t i;
    uint8_t cell;
    uint8_t quadrants;
    uint8_t level;
    uint8_t char_x;
    uint8_t char_y;
    uint16_t cache_offset;

    for (i = 0u; i < wall_count; ++i) {
        if (platform_light_visibility[wall_tile_offsets[i]] == 0u) continue;
        quadrants = wall_quadrants(native_light_source_x >> 1,
                                   native_light_source_y >> 1,
                                   wall_x[i], wall_y[i]);
        cache_offset = (uint16_t)i * 4u;
        for (cell = 0u; cell < 4u; ++cell) {
            char_x = (wall_x[i] << 1) + (cell & 1u);
            char_y = (wall_y[i] << 1) + (cell >> 1);
            level = light_level_at(char_x, char_y);
            if (level != PLATFORM_LIGHT_NONE) {
                wall_light_cache[cache_offset + cell] = packed_light_add(
                    wall_light_cache[cache_offset + cell], quadrants, level);
            }
        }
        /* The normal source blitter now updates open cells only. */
        platform_light_visibility[wall_tile_offsets[i]] = 0u;
    }
}

static void wall_cache_apply(const PlatformRoom* room,
                             const PlatformObject* player) {
    uint8_t i;
    uint8_t quadrants;
    uint8_t level;
    uint8_t player_x;
    uint8_t player_y;
    uint16_t cache_offset;
    uint16_t char_offset;

    if (room != wall_cache_room) return;
    if (player != 0 && player->type != 0u &&
        player->x < PLATFORM_MAP_CHAR_WIDTH &&
        player->y < PLATFORM_MAP_CHAR_HEIGHT) {
        player_x = player->x >> 1;
        player_y = player->y >> 1;
    } else {
        player_x = 0xffu;
        player_y = 0xffu;
    }
    for (i = 0u; i < wall_count; ++i) {
        quadrants = player_x == 0xffu ? 0x0fu
            : wall_quadrants(player_x, player_y, wall_x[i], wall_y[i]);
        cache_offset = (uint16_t)i * 4u;
        char_offset = wall_char_offsets[i];
        level = packed_light_max(wall_light_cache[cache_offset], quadrants);
        platform_brightness[char_offset] = level > platform_global_light
                                               ? level : platform_global_light;
        level = packed_light_max(wall_light_cache[cache_offset + 1u], quadrants);
        platform_brightness[char_offset + 1u] = level > platform_global_light
                                                    ? level : platform_global_light;
        level = packed_light_max(wall_light_cache[cache_offset + 2u], quadrants);
        platform_brightness[char_offset + PLATFORM_MAP_CHAR_WIDTH] =
            level > platform_global_light ? level : platform_global_light;
        level = packed_light_max(wall_light_cache[cache_offset + 3u], quadrants);
        platform_brightness[char_offset + PLATFORM_MAP_CHAR_WIDTH + 1u] =
            level > platform_global_light ? level : platform_global_light;
    }
}

static void light_source_apply(const PlatformRoom* room,
                               const PlatformObject* object) {
    const PlatformObjectType* type;
    uint8_t radius;
    uint8_t min_x;
    uint8_t max_x;
    uint8_t min_y;
    uint8_t max_y;

    if (object == 0 || object->type == 0u ||
        object->x >= PLATFORM_MAP_CHAR_WIDTH ||
        object->y >= PLATFORM_MAP_CHAR_HEIGHT) return;
    type = platform_object_type_get(object->type);
    radius = PLATFORM_OBJECT_LIGHT(type);
    if (radius == 0u) return;
    if (radius > PLATFORM_LIGHT_MAX_RADIUS) radius = PLATFORM_LIGHT_MAX_RADIUS;

    min_x = object->x > radius ? object->x - radius : 0u;
    max_x = (uint16_t)object->x + radius < PLATFORM_MAP_CHAR_WIDTH
                ? object->x + radius : PLATFORM_MAP_CHAR_WIDTH - 1u;
    min_y = object->y > radius ? object->y - radius : 0u;
    max_y = (uint16_t)object->y + radius < PLATFORM_MAP_CHAR_HEIGHT
                ? object->y + radius : PLATFORM_MAP_CHAR_HEIGHT - 1u;
    native_light_source_x = object->x;
    native_light_source_y = object->y;
    native_light_radius = radius;
    native_light_min_x = min_x;
    native_light_min_y = min_y;
    native_light_columns = max_x - min_x + 1u;
    native_light_rows = max_y - min_y + 1u;
    native_light_screen_offset =
        (uint16_t)min_y * PLATFORM_MAP_CHAR_WIDTH + min_x;
    native_light_visibility_offset = (min_y >> 1) * PLATFORM_MAP_WIDTH;
    visibility_build(room, object->x >> 1, object->y >> 1,
                     min_x >> 1, max_x >> 1,
                     min_y >> 1, max_y >> 1,
                     platform_light_visibility);
    wall_cache_add_source();
    platform_light_source_apply_native();
}

void platform_lighting_rebuild(const PlatformRoom* room,
                               const PlatformObject* player) {
    uint16_t i;
    uint16_t limit;

    memset(platform_brightness, platform_global_light,
           sizeof(platform_brightness));
    if (room != 0) {
        wall_cache_prepare(room);
        limit = room == rendered_room ? rendered_object_limit
                                      : PLATFORM_ROOM_OBJECT_COUNT;
        for (i = 0; i < limit; ++i) {
            if (player == &room->objects[i]) continue;
            light_source_apply(room, &room->objects[i]);
        }
        light_source_apply(room, player);
        wall_cache_apply(room, player);
    } else {
        wall_cache_room = 0;
    }
    platform_lighting_apply();
}

void platform_room_draw(const PlatformRoom* room, const PlatformObject* player) {
    uint16_t i;
    if (room == 0) return;
    rendered_room = room;
    rendered_player = player;
    rendered_object_limit = PLATFORM_ROOM_OBJECT_COUNT;
    while (rendered_object_limit > 0u &&
           room->objects[rendered_object_limit - 1u].type == 0u) {
        --rendered_object_limit;
    }
    platform_look_cursor_hide();
    platform_text_area_clear_native();
    platform_color_clear_native();
    platform_map_draw_native(room);
    for (i = 0; i < rendered_object_limit; ++i) {
        if (player == &room->objects[i]) continue;
        object_draw_base(&room->objects[i]);
    }
    object_draw_base(player);
    view_rebuild(room, player);
    platform_lighting_rebuild(room, player);
}

#pragma code-name (push, "LOWCODE")
void platform_lighting_apply(void) {
    platform_lighting_apply_native();
}
#pragma code-name (pop)

#pragma code-name (push, "MIDCODE")
void platform_lighting_set_global(uint8_t level) {
    if (level >= PLATFORM_LIGHT_LEVEL_COUNT) level = PLATFORM_LIGHT_FULL;
    platform_global_light = level;
    platform_lighting_rebuild(rendered_room, rendered_player);
}
#pragma code-name (pop)

void platform_lightning(void) {
    platform_lightning_native();
}

void platform_object_move(PlatformRoom* room, PlatformObject* object,
                          uint8_t new_x, uint8_t new_y,
                          const PlatformObject* player) {
    uint8_t emits_light;
    uint8_t view_changed;
    if (room == 0 || object == 0 || object->type == 0u) return;
    emits_light = PLATFORM_OBJECT_LIGHT(platform_object_type_get(object->type)) != 0u;
    view_changed = object == player &&
                   ((object->x >> 1) != (new_x >> 1) ||
                    (object->y >> 1) != (new_y >> 1));
    dirty_clear();
    mark_object_cells(object);
    object->x = new_x;
    object->y = new_y;
    mark_object_cells(object);
    redraw_dirty(room, player);
    if (view_changed && room == rendered_room) {
        rendered_player = player;
        view_rebuild(room, player);
    }
    if (emits_light && room == rendered_room) {
        rendered_player = player;
        platform_lighting_rebuild(room, player);
    } else if (view_changed && room == rendered_room) {
        rendered_player = player;
        if (wall_cache_room == room) {
            wall_cache_apply(room, player);
            platform_lighting_apply();
        } else {
            platform_lighting_rebuild(room, player);
        }
    }
}

uint8_t platform_player_step(int8_t delta_x, int8_t delta_y) {
    int16_t new_x;
    int16_t new_y;
    uint8_t direction;
    uint8_t room_id;
    uint8_t tile;

    if (platform_player == 0 || platform_player->type == 0u) {
        return PLATFORM_ERR_ARGUMENT;
    }
    new_x = (int16_t)platform_player->x + delta_x;
    new_y = (int16_t)platform_player->y + delta_y;
    if (new_y < 0) {
        direction = PLATFORM_DIRECTION_NORTH;
        new_y = PLATFORM_MAP_CHAR_HEIGHT - 1u;
    } else if (new_x < 0) {
        direction = PLATFORM_DIRECTION_WEST;
        new_x = PLATFORM_MAP_CHAR_WIDTH - 1u;
    } else if (new_x >= PLATFORM_MAP_CHAR_WIDTH) {
        direction = PLATFORM_DIRECTION_EAST;
        new_x = 0;
    } else if (new_y >= PLATFORM_MAP_CHAR_HEIGHT) {
        direction = PLATFORM_DIRECTION_SOUTH;
        new_y = 0;
    } else {
        tile = platform_room.tiles[(uint16_t)((uint8_t)new_y >> 1) *
                                   PLATFORM_MAP_WIDTH + ((uint8_t)new_x >> 1)];
        if ((tile_properties[tile] & PLATFORM_TILE_SOLID_LAND) == 0u) {
            return PLATFORM_ERR_BLOCKED;
        }
        platform_object_move(&platform_room, platform_player,
                             (uint8_t)new_x, (uint8_t)new_y, platform_player);
        return PLATFORM_OK;
    }
    if (platform_room_neighbor(&platform_room, direction, &room_id) != PLATFORM_OK) {
        return PLATFORM_ERR_BLOCKED;
    }
    return platform_room_enter(room_id, platform_player->type,
                               (uint8_t)new_x, (uint8_t)new_y);
}

#pragma code-name (push, "LOWCODE")
void platform_wait_frame(void) {
    uint8_t frame;
    frame = platform_frame_counter;
    while (platform_frame_counter == frame) {
    }
}
#pragma code-name (pop)

uint16_t platform_room_object_count(const PlatformRoom* room,
                                    uint8_t actor_only) {
    uint16_t i;
    uint16_t count;
    uint8_t actor;
    if (room == 0) return 0;
    count = 0;
    for (i = 0; i < PLATFORM_ROOM_OBJECT_COUNT; ++i) {
        if (room->objects[i].type == 0u) continue;
        actor = platform_object_type_get(room->objects[i].type)->reserved[0] &
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

    const PlatformObjectType* definition;
    if (room == 0 || type == 0u) {
        return PLATFORM_ERR_ARGUMENT;
    }
    definition = platform_object_type_get(type);
    if (!object_type_is_valid(definition)) return PLATFORM_ERR_ARGUMENT;
    is_actor = definition->reserved[0] & PLATFORM_OBJECT_FLAG_ACTOR;
    if (!is_actor && platform_room_object_count(room, 2u) >= PLATFORM_NON_ACTOR_LIMIT) {
        return PLATFORM_ERR_LIMIT;
    }
    for (i = 0; i < PLATFORM_ROOM_OBJECT_COUNT; ++i) {
        if (room->objects[i].type == 0u) {
            room->objects[i].type = type;
            room->objects[i].x = x;
            room->objects[i].y = y;
            if (room == rendered_room && i >= rendered_object_limit) {
                rendered_object_limit = i + 1u;
            }
            if (out_slot != 0) *out_slot = (uint8_t)i;
            return PLATFORM_OK;
        }
    }
    return PLATFORM_ERR_FULL;
}

#pragma code-name (push, "LOWCODE")
uint8_t platform_room_enter(uint8_t room_id, uint8_t actor_type,
                            uint8_t new_x, uint8_t new_y) {
    PlatformObject actor;
    uint8_t slot;
    uint8_t result;
    uint8_t removed_actor;
    uint8_t tile;

    if (actor_type == 0u || new_x >= PLATFORM_MAP_CHAR_WIDTH ||
        new_y >= PLATFORM_MAP_CHAR_HEIGHT) return PLATFORM_ERR_ARGUMENT;
    raster_irq_suspend();
    actor.type = actor_type;
    actor.x = new_x;
    actor.y = new_y;
    removed_actor = platform_player != 0 && platform_player->type == actor_type;

    result = room_stage_load(room_id);
    if (result != PLATFORM_OK) goto transition_done;
    if (removed_actor && room_id == player_spawn_room &&
        room_stage.objects[player_spawn_slot].type == player_spawn_type) {
        memset(&room_stage.objects[player_spawn_slot], 0, sizeof(PlatformObject));
    }
    tile = room_stage.tiles[(uint16_t)(new_y >> 1) * PLATFORM_MAP_WIDTH +
                            (new_x >> 1)];
    if ((tile_properties[tile] & PLATFORM_TILE_SOLID_LAND) == 0u) {
        result = PLATFORM_ERR_BLOCKED;
        goto transition_done;
    }
    result = game_room_code_prepare(room_id);
    if (result != PLATFORM_OK) {
        platform_room_draw(&platform_room, platform_player);
        goto transition_done;
    }
    if (room_store_hook != 0) {
        result = room_store_hook(&platform_room);
        if (result != PLATFORM_OK) {
            platform_room_draw(&platform_room, platform_player);
            goto transition_done;
        }
    }
    if (room_restore_hook != 0) {
        result = room_restore_hook(&room_stage);
        if (result != PLATFORM_OK) {
            platform_room_draw(&platform_room, platform_player);
            goto transition_done;
        }
    }
    result = platform_room_object_add(&room_stage, actor.type,
                                      actor.x, actor.y, &slot);
    if (result != PLATFORM_OK) {
        platform_room_draw(&platform_room, platform_player);
        goto transition_done;
    }

    if (removed_actor) {
        platform_player->type = 0;
    }
    platform_player_slot = slot;
    room_commit(&platform_room, room_id);
    platform_player = &platform_room.objects[slot];
    game_room_code_activate();
    platform_room_draw(&platform_room, platform_player);
    result = PLATFORM_OK;
transition_done:
    raster_irq_resume();
    return result;
}
#pragma code-name (pop)

uint8_t platform_room_object_remove(PlatformRoom* room, uint8_t slot,
                                    const PlatformObject* player) {
    PlatformObject* object;
    uint8_t emitted_light;
    if (room == 0) return PLATFORM_ERR_ARGUMENT;
    object = &room->objects[slot];
    if (object->type == 0u) return PLATFORM_ERR_ARGUMENT;
    emitted_light = PLATFORM_OBJECT_LIGHT(
        platform_object_type_get(object->type));
    dirty_clear();
    mark_object_cells(object);
    object->type = 0;
    object->x = 0;
    object->y = 0;
    if (room == rendered_room && (uint16_t)slot + 1u == rendered_object_limit) {
        while (rendered_object_limit > 0u &&
               room->objects[rendered_object_limit - 1u].type == 0u) {
            --rendered_object_limit;
        }
    }
    redraw_dirty(room, player);
    if (emitted_light != 0u && room == rendered_room) {
        rendered_player = player;
        platform_lighting_rebuild(room, player);
    }
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
    if (leaving == rendered_room &&
        (uint16_t)leaving_slot + 1u == rendered_object_limit) {
        while (rendered_object_limit > 0u &&
               leaving->objects[rendered_object_limit - 1u].type == 0u) {
            --rendered_object_limit;
        }
    }
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

static void look_append_char(char ch) {
    if (look_length < 80u) {
        look_buffer[look_length++] = ch;
    } else {
        look_truncated = 1u;
    }
}

static void look_append_string(const char* text) {
    while (*text != '\0') look_append_char(*text++);
}

static void look_append_count(uint8_t count) {
    if (count >= 100u) look_append_char((char)('0' + count / 100u));
    if (count >= 10u) look_append_char((char)('0' + (count / 10u) % 10u));
    look_append_char((char)('0' + count % 10u));
    look_append_char(' ');
}

static void look_append_type_name(uint8_t type_id) {
    const PlatformObjectType* type;
    uint8_t ch;
    uint8_t i;
    type = platform_object_type_get(type_id);
    for (i = 0u; i < sizeof(type->name) && type->name[i] != '\0'; ++i) {
        ch = (uint8_t)type->name[i];
        /* Editor assets use ASCII; cc65 C literals use PETSCII. Normalize
         * names to the latter before platform_text_screen_code(). */
        if (ch >= 0x41u && ch <= 0x5au) ch += 0x80u;
        else if (ch >= 0x61u && ch <= 0x7au) ch -= 0x20u;
        look_append_char((char)ch);
    }
}

static uint8_t room_exit_text(const PlatformRoom* room, uint8_t direction) {
    switch (direction) {
        case PLATFORM_DIRECTION_NORTH: return room->north_text;
        case PLATFORM_DIRECTION_EAST: return room->east_text;
        case PLATFORM_DIRECTION_WEST: return room->west_text;
        default: return room->south_text;
    }
}

const char* platform_room_exit_description(const PlatformRoom* room,
                                           uint8_t direction) {
    uint8_t offset;
    if (room == 0 || direction > PLATFORM_DIRECTION_SOUTH) return 0;
    offset = room_exit_text(room, direction);
    if (offset == 0u || room->text[offset] == 0u) return 0;
    return (const char*)&room->text[offset];
}

static void look_write_buffer(uint8_t color) {
    if (look_truncated) {
        look_buffer[77] = '.';
        look_buffer[78] = '.';
        look_buffer[79] = '.';
        look_length = 80u;
    }
    look_buffer[look_length] = '\0';
    platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
    platform_text_output_color = color & 0x0fu;
    platform_text_output_native(look_buffer);
}

uint8_t platform_object_intersects_tile(const PlatformObject* object,
                                        uint8_t tile_x, uint8_t tile_y) {
    const PlatformObjectType* type;
    int16_t left;
    int16_t top;
    int16_t world_x;
    int16_t world_y;
    uint8_t x;
    uint8_t y;
    uint8_t index;
    uint8_t tile_left;
    uint8_t tile_top;

    if (object == 0 || object->type == 0u) return 0u;
    type = platform_object_type_get(object->type);
    if (!object_type_is_valid(type)) return 0u;
    left = (int16_t)object->x - HOTSPOT_X(type);
    top = (int16_t)object->y - HOTSPOT_Y(type);
    tile_left = tile_x << 1;
    tile_top = tile_y << 1;
    index = 0u;
    for (y = 0u; y < OBJECT_HEIGHT(type); ++y) {
        world_y = top + y;
        if (world_y < tile_top || world_y > (int16_t)(tile_top + 1u)) {
            index += OBJECT_WIDTH(type);
            continue;
        }
        for (x = 0u; x < OBJECT_WIDTH(type); ++x, ++index) {
            world_x = left + x;
            if (type->chars[index] != 0u && world_x >= tile_left &&
                world_x <= (int16_t)(tile_left + 1u)) return 1u;
        }
    }
    return 0u;
}

static void look_write_message(const char* text, uint8_t color) {
    look_length = 0u;
    look_truncated = 0u;
    look_append_string(text);
    look_write_buffer(color);
}

uint8_t platform_look_tile_check(const PlatformRoom* room,
                                 const PlatformObject* viewer,
                                 uint8_t tile_x, uint8_t tile_y,
                                 uint8_t color) {
    uint16_t offset;
    uint8_t light;
    uint8_t value;
    uint8_t distance_x;
    uint8_t distance_y;
    uint8_t distance;

    if (room == 0 || room != rendered_room || viewer == 0 ||
        tile_x >= PLATFORM_MAP_WIDTH || tile_y >= PLATFORM_MAP_HEIGHT) {
        return PLATFORM_ERR_ARGUMENT;
    }
    offset = (uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x;
    if (platform_view_tiles[offset] == 0u) {
        look_write_message("I cannot see that.", color);
        return PLATFORM_ERR_BLOCKED;
    }

    offset = (uint16_t)(tile_y << 1) * PLATFORM_MAP_CHAR_WIDTH +
             (tile_x << 1);
    light = platform_brightness[offset] & 0x03u;
    value = platform_brightness[offset + 1u] & 0x03u;
    if (value > light) light = value;
    value = platform_brightness[offset + PLATFORM_MAP_CHAR_WIDTH] & 0x03u;
    if (value > light) light = value;
    value = platform_brightness[offset + PLATFORM_MAP_CHAR_WIDTH + 1u] & 0x03u;
    if (value > light) light = value;

    distance_x = (viewer->x >> 1) > tile_x
                     ? (uint8_t)((viewer->x >> 1) - tile_x)
                     : (uint8_t)(tile_x - (viewer->x >> 1));
    distance_y = (viewer->y >> 1) > tile_y
                     ? (uint8_t)((viewer->y >> 1) - tile_y)
                     : (uint8_t)(tile_y - (viewer->y >> 1));
    distance = distance_x > distance_y ? distance_x : distance_y;
    if (light == PLATFORM_LIGHT_NONE || distance > look_range_by_light[light]) {
        look_write_message(
            "It is too dark to make anything out. I need to get closer!", color);
        return PLATFORM_ERR_BLOCKED;
    }
    return PLATFORM_OK;
}

#pragma code-name (push, "UPPERCODE")
static void look_append_room_text(const PlatformRoom* room,
                                  uint8_t text_offset) {
    uint8_t ch;
    do {
        ch = room->text[text_offset++];
        if (ch == 0u) break;
        if (ch >= 0x41u && ch <= 0x5au) ch += 0x80u;
        else if (ch >= 0x61u && ch <= 0x7au) ch -= 0x20u;
        look_append_char((char)ch);
    } while (text_offset != 0u);
}

uint8_t platform_look_exit(const PlatformRoom* room,
                           const PlatformObject* viewer,
                           uint8_t edge_x, uint8_t edge_y,
                           uint8_t direction, uint8_t color) {
    uint8_t neighbor;
    uint8_t text_offset;
    uint8_t result;

    if (direction > PLATFORM_DIRECTION_SOUTH) return PLATFORM_ERR_ARGUMENT;
    result = platform_look_tile_check(room, viewer, edge_x, edge_y, color);
    if (result != PLATFORM_OK) return result;
    if (platform_room_neighbor(room, direction, &neighbor) != PLATFORM_OK) {
        look_write_message("There is no exit that way.", color);
        return PLATFORM_ERR_NOT_FOUND;
    }

    look_length = 0u;
    look_truncated = 0u;
    text_offset = room_exit_text(room, direction);
    if (text_offset != 0u && room->text[text_offset] != 0u) {
        look_append_room_text(room, text_offset);
    } else {
        look_append_string("An exit.");
    }
    look_write_buffer(color);
    return PLATFORM_OK;
}

void platform_object_take_prompt(uint8_t type_id, uint8_t color) {
    look_length = 0u;
    look_truncated = 0u;
    look_append_string(take_prompt_prefix);
    look_append_type_name(type_id);
    look_append_string(take_prompt_arrows);
    look_write_buffer(color);
}
#pragma code-name (pop)

uint8_t platform_look_tile(const PlatformRoom* room,
                           uint8_t tile_x, uint8_t tile_y, uint8_t color) {
    uint16_t i;
    uint16_t limit;
    uint8_t type_id;
    uint8_t count;
    uint8_t found;

    if (room == 0 || tile_x >= PLATFORM_MAP_WIDTH ||
        tile_y >= PLATFORM_MAP_HEIGHT) {
        return PLATFORM_ERR_ARGUMENT;
    }
    look_length = 0u;
    look_truncated = 0u;
    look_append_string("You see: ");

    memset(look_counts, 0, sizeof(look_counts));
    limit = room == rendered_room ? rendered_object_limit : PLATFORM_ROOM_OBJECT_COUNT;
    for (i = 0u; i < limit; ++i) {
        type_id = room->objects[i].type;
        if (type_id == 0u ||
            !platform_object_intersects_tile(&room->objects[i], tile_x, tile_y)) continue;
        if (look_counts[type_id] != 0xffu) ++look_counts[type_id];
    }
    found = 0u;
    for (i = 0u; i < limit; ++i) {
        type_id = room->objects[i].type;
        count = look_counts[type_id];
        if (type_id == 0u || count == 0u ||
            !platform_object_intersects_tile(&room->objects[i], tile_x, tile_y)) continue;
        if (found) look_append_string(", ");
        if (count > 1u) look_append_count(count);
        look_append_type_name(type_id);
        look_counts[type_id] = 0u;
        found = 1u;
    }
    if (!found) look_append_string("nothing");
    look_append_char('.');
    look_write_buffer(color);
    return PLATFORM_OK;
}

static void look_cursor_position(uint8_t tile_x, uint8_t tile_y) {
    uint16_t sprite_x;

    sprite_x = 23u + (uint16_t)tile_x * 16u;
    P_VIC(0x00) = (uint8_t)sprite_x;
    P_VIC(0x01) = (uint8_t)(49u + (uint16_t)tile_y * 16u);
    if (sprite_x & 0x100u) P_VIC(0x10) |= 0x01u;
    else P_VIC(0x10) &= 0xfeu;
}

#pragma code-name (push, "LOWCODE")
uint8_t platform_look_cursor_show(uint8_t tile_x, uint8_t tile_y) {
    uint8_t row;

    if (tile_x >= PLATFORM_MAP_WIDTH || tile_y >= PLATFORM_MAP_HEIGHT) {
        return PLATFORM_ERR_ARGUMENT;
    }
    memset(P_SPRITE_DATA, 0, 64u);
    P_SPRITE_DATA[0] = 0xffu;
    P_SPRITE_DATA[1] = 0xffu;
    P_SPRITE_DATA[2] = 0xc0u;
    P_SPRITE_DATA[51] = 0xffu;
    P_SPRITE_DATA[52] = 0xffu;
    P_SPRITE_DATA[53] = 0xc0u;
    for (row = 1u; row < 17u; ++row) {
        P_SPRITE_DATA[(uint8_t)(row * 3u)] = 0x80u;
        P_SPRITE_DATA[(uint8_t)(row * 3u + 2u)] = 0x40u;
    }
    P_SPRITE_POINTERS[0] = (uint8_t)(0x3a00u / 64u);
    look_cursor_position(tile_x, tile_y);
    P_VIC(0x17) &= 0xfeu;
    P_VIC(0x1b) &= 0xfeu;
    P_VIC(0x1c) &= 0xfeu;
    P_VIC(0x1d) &= 0xfeu;
    look_cursor_visible = 1u;
    P_VIC(0x15) |= 0x01u;
    platform_look_cursor_tick();
    return PLATFORM_OK;
}
#pragma code-name (pop)

#pragma code-name (push, "MIDCODE")
uint8_t platform_look_cursor_move(uint8_t tile_x, uint8_t tile_y) {
    if (!look_cursor_visible || tile_x >= PLATFORM_MAP_WIDTH ||
        tile_y >= PLATFORM_MAP_HEIGHT) return PLATFORM_ERR_ARGUMENT;
    look_cursor_position(tile_x, tile_y);
    return PLATFORM_OK;
}

void platform_look_cursor_tick(void) {
    if (look_cursor_visible) {
        P_VIC(0x27) = look_cursor_colors[(platform_frame_counter >> 2) & 0x07u];
    }
}
#pragma code-name (pop)

#pragma code-name (push, "UPPERCODE")
void platform_look_cursor_hide(void) {
    P_VIC(0x15) &= 0xfeu;
    look_cursor_visible = 0u;
}
#pragma code-name (pop)
