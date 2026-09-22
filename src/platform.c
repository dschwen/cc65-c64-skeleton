#include <stdint.h>
#include <string.h>

#include "memmap.h"
#include "banks.h"
#include "platform.h"
#include "platform_native.h"
#include "game.h"
#include "world.h"
#include "look_helpers_abi.h"

/*
 * Native (oscar64 C) port of src/platform.c. Structural translation of the
 * original: game logic, object/lighting math and the public API are
 * unchanged. What changed is how banked/resident data is reached - see
 * memmap.h's header comment for the memory model this assumes.
 *
 * Object-type storage keeps the original's three-zone split (hot data
 * resident, cold data banked) since it's a sound design independent of
 * the compiler, but the zones no longer need mode-switching to read: Zone
 * B now lives at $C000-CFFF (never shadowed by I/O or KERNAL) and Zone C
 * at $E000-E7FF (visible in this program's steady memory-mapping state -
 * see src/banking.c), so platform_object_type_get() is a plain array
 * index, not a staged copy.
 */

#define P_OBJECT_TYPE_STAGE ((uint8_t*)0xb000u)

#define WALL_CACHE_QUADRANT_NW 0x01u
#define WALL_CACHE_QUADRANT_NE 0x02u
#define WALL_CACHE_QUADRANT_SW 0x04u
#define WALL_CACHE_QUADRANT_SE 0x08u

#define OBJECT_WIDTH(t)  ((uint8_t)((t)->dimensions >> 4))
#define OBJECT_HEIGHT(t) ((uint8_t)((t)->dimensions & 0x0fu))
#define HOTSPOT_X(t)     ((uint8_t)((t)->hotspot >> 4))
#define HOTSPOT_Y(t)     ((uint8_t)((t)->hotspot & 0x0fu))

/* Tile bitmaps/properties - loaded from a bank at boot, see
 * src/assets_data.c's header comment. */
extern uint8_t tile_data[];
extern uint8_t tile_properties[];
void platform_tiles_load(void);

void raster_irq_install(void);
void platform_weather_service(void);
void raster_irq_vectors_restore(void);
void raster_irq_suspend(void);
void raster_irq_resume(void);
extern volatile uint8_t platform_raster_irq_active;
void platform_memory_game(void);
void platform_memory_kernal(void);
void platform_memory_all_ram(void);
void platform_easyflash_copy_roml(uint8_t bank, uint16_t offset, void* destination, uint16_t size);
void platform_easyflash_copy_romh(uint8_t bank, uint16_t offset, void* destination, uint16_t size);
uint8_t platform_irq_save_disable(void);
void platform_irq_restore(uint8_t status);

/* render.c */
void platform_map_draw_native(const PlatformRoom* room);
void platform_object_draw_native(void);
void platform_lighting_apply_native(void);
void platform_lightning_native(void);
void platform_light_source_apply_native(void);
void platform_visibility_build_native(void);
void platform_color_clear_native(void);
void platform_color_copy_native(void);
void platform_text_area_clear_native(void);
void platform_text_output_native(const char* text);

/* dirty_redraw.c */
void dirty_clear(void);
void mark_object_cells(const PlatformObject* object);
void redraw_dirty(const PlatformRoom* room, const PlatformObject* player);

/* Per-room bank-switched services (modules/*.c) - see src/banks.h. Each
 * takes its arguments via resident globals and (where it returns
 * anything) writes into a resident scratch buffer, the same convention
 * FAR_CALL's cc65 A/X passthrough used to give for free - a banked
 * function's return value must never be a pointer into its own bank-local
 * data, which becomes invalid the instant the bank switches back. */
void room_helpers_entry(void);
void look_helpers_entry(void);
void script_entry(void);
void typeinfo_entry(void);
extern uint8_t typeinfo_query_type_id;

/* Room code dispatch (src/room_dispatch.c) - retargeted from the old
 * $9900 copied-overlay scheme to per-room banks, one function table entry
 * per room. */
void game_room_enter_tile_native(void);
uint8_t game_room_look_at_native(uint8_t tile_x, uint8_t tile_y);
void game_room_enter_room_native(void);
uint8_t game_room_use_at_native(uint8_t tile_x, uint8_t tile_y);

/* Resource fetch (src/resources.c) - room/script/portrait/environment/
 * asset data, all bank-switched reads. */
uint16_t platform_resource_fetch(uint8_t kind, uint8_t resource_id,
                                  uint8_t* destination, uint16_t capacity);
uint16_t platform_resource_fetch_range(uint8_t kind, uint8_t resource_id,
                                       uint16_t start, uint8_t* destination,
                                       uint16_t capacity);
uint16_t platform_resource_last_size(void);
uint8_t platform_room_data_fetch(uint8_t room_id, void* destination);

extern uint8_t platform_text_output_color;
extern uint8_t platform_text_output_line;

PlatformRoom platform_room;
uint8_t platform_current_room;
uint8_t platform_player_slot;
PlatformObject* platform_player;

/* object_types_a/b/c are defined (with their real #embed-initialized
 * content) in src/object_types_data.c and already declared extern via
 * platform_native.h, included above. Declaring them again here, even
 * without an initializer, created a second, uninitialized instance that
 * oscar64 silently accepted instead of erroring on the duplicate
 * definition - confirmed live as one of the causes of the whole program
 * hanging before main() ever ran. */

/* Filled in by the banked TypeInfo service (bank 1) - see
 * platform_object_type_info_get() below. */
PlatformObjectTypeInfo platform_object_type_info_scratch;
uint8_t typeinfo_query_type_id;

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

uint8_t platform_base_colors[PLATFORM_MAP_CHAR_WIDTH * PLATFORM_MAP_CHAR_HEIGHT];
uint8_t platform_brightness[PLATFORM_MAP_TILE_COUNT];
uint8_t platform_global_light;
uint8_t platform_room_light_level;
uint8_t platform_room_los_enabled;
uint8_t platform_light_visibility[PLATFORM_MAP_TILE_COUNT];
uint8_t platform_view_tiles[PLATFORM_MAP_TILE_COUNT];

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

const PlatformRoom* rendered_room;
const PlatformObject* rendered_player;
uint16_t rendered_object_limit;
static uint8_t look_cursor_visible;
static PlatformRoom room_stage;

#define PLATFORM_ROOM_SCRATCH_BUFFER ((uint8_t*)&room_stage)
#define PLATFORM_ROOM_SCRATCH_MAX_BYTES ((uint16_t)sizeof(room_stage))

static uint8_t wall_light_cache[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_tile_offsets[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_x[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_y[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_count;
static const PlatformRoom* wall_cache_room;
static uint8_t player_spawn_room;
static uint8_t player_spawn_slot;
static uint8_t player_spawn_type;

static void write_screen_cell(uint8_t x, uint8_t y, uint8_t ch, uint8_t color) {
    uint16_t offset;
    uint16_t tile_offset;

    if (x >= PLATFORM_MAP_CHAR_WIDTH || y >= 25u) return;
    offset = (uint16_t)y * PLATFORM_MAP_CHAR_WIDTH + x;
    SCREEN_RAM[offset] = ch;
    color &= 0x0fu;
    if (y < PLATFORM_MAP_CHAR_HEIGHT) {
        platform_base_colors[offset] = color;
        tile_offset = (uint16_t)(y >> 1) * PLATFORM_MAP_WIDTH + (x >> 1);
        color = platform_light_colors[
            ((platform_brightness[tile_offset] & 0x03u) << 4) | color];
        if (platform_view_tiles[tile_offset] == 0u) color = 0u;
    }

    COLOR_RAM[offset] = color;
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

const PlatformObjectType* platform_object_type_get(uint8_t type_id) {
    if (type_id < OBJECT_TYPE_ZONE_A_COUNT) {
        return &object_types_a[type_id];
    }
    if (type_id < OBJECT_TYPE_ZONE_AB_COUNT) {
        return &object_types_b[type_id - OBJECT_TYPE_ZONE_A_COUNT];
    }
    /* Zone C is cold, banked storage (src/object_types_data.c) - most
     * per-frame draw calls re-request the same object across several
     * cells/rows, so a one-entry cache turns the common case into a
     * resident-only read and reserves the far_call_bank() for genuine
     * type changes. */
    if (object_type_c_cache_tag != type_id) {
        object_type_c_query_id = type_id;
        far_call_bank(BANK_OBJECT_TYPE_C, object_type_c_fetch);
        object_type_c_cache_tag = type_id;
    }
    return &object_type_c_scratch;
}

/* platform_object_type_info_get() runs the banked TypeInfo service (bank
 * 1) in place, fetching the cold name/flags record directly from
 * EasyFlash - a real cartridge bank switch (heavier than
 * platform_object_type_get()) that must only be called from discrete,
 * human-input-paced code paths, never per frame or per rendered cell. */
const PlatformObjectTypeInfo* platform_object_type_info_get(uint8_t type_id) {
    typeinfo_query_type_id = type_id;
    far_call_bank(BANK_TYPEINFO, typeinfo_entry);
    return &platform_object_type_info_scratch;
}

/*
 * In-place room-helpers service ("RH"): platform_room_object_remove/
 * platform_room_neighbor's bodies (modules/room_helpers.c), bank
 * BANK_ROOM_HELPERS.
 */
#define ROOM_HELPERS_OP_NEIGHBOR 0u
#define ROOM_HELPERS_OP_REMOVE   1u

void redraw_dirty(const PlatformRoom* room, const PlatformObject* player);

PlatformRoom* room_helpers_room = 0;
uint8_t room_helpers_slot = 0;
uint8_t room_helpers_direction = 0;
uint8_t room_helpers_room_id = 0;
uint8_t room_helpers_op = 0;
uint8_t room_helpers_result = 0;
/* Computed resident-side before entry; see platform_room_object_remove(). */
uint8_t room_helpers_emitted_light = 0;

uint8_t platform_room_neighbor(const PlatformRoom* room, uint8_t direction,
                               uint8_t* room_id) {
    if (room == 0 || room_id == 0) return PLATFORM_ERR_ARGUMENT;
    room_helpers_op = ROOM_HELPERS_OP_NEIGHBOR;
    room_helpers_room = (PlatformRoom*)room;
    room_helpers_direction = direction;
    far_call_bank(BANK_ROOM_HELPERS, room_helpers_entry);
    if (room_helpers_result == PLATFORM_OK) *room_id = room_helpers_room_id;
    return room_helpers_result;
}

uint8_t platform_room_object_remove(PlatformRoom* room, uint8_t slot,
                                    const PlatformObject* player) {
    PlatformObject* object;

    if (room == 0) return PLATFORM_ERR_ARGUMENT;
    object = &room->objects[slot];
    room_helpers_op = ROOM_HELPERS_OP_REMOVE;
    room_helpers_room = room;
    room_helpers_slot = slot;
    if (object->type != 0u) {
        room_helpers_emitted_light = PLATFORM_OBJECT_LIGHT(
            platform_object_type_get(object->type));
        dirty_clear();
        mark_object_cells(object);
    } else {
        room_helpers_emitted_light = 0u;
    }
    far_call_bank(BANK_ROOM_HELPERS, room_helpers_entry);
    if (room_helpers_result == PLATFORM_OK) {
        redraw_dirty(room, player);
        if (room_helpers_emitted_light != 0u && room == rendered_room) {
            rendered_player = player;
            platform_lighting_rebuild(room, player);
        }
    }
    return room_helpers_result;
}

/*
 * In-place Look Helpers service ("LH"): platform_look_tile/_check/object_
 * take_prompt/object_taken_message's bodies (modules/look_helpers.c), bank
 * BANK_LOOK_HELPERS.
 */
#define LOOK_HELPERS_OP_TILE         0u
#define LOOK_HELPERS_OP_TILE_CHECK   1u
#define LOOK_HELPERS_OP_TAKE_PROMPT  2u
#define LOOK_HELPERS_OP_TAKEN_MSG    3u

const PlatformRoom* look_helpers_room = 0;
const PlatformObject* look_helpers_viewer = 0;
uint8_t look_helpers_tile_x = 0;
uint8_t look_helpers_tile_y = 0;
uint8_t look_helpers_color = 0;
uint8_t look_helpers_type_id = 0;
uint8_t look_helpers_op = 0;
uint8_t look_helpers_result = 0;
LookHelpersWorkspace* look_helpers_workspace = 0;
uint8_t look_helpers_light = 0;

uint8_t platform_look_tile_check(const PlatformRoom* room,
                                 const PlatformObject* viewer,
                                 uint8_t tile_x, uint8_t tile_y,
                                 uint8_t color) {
    look_helpers_op = LOOK_HELPERS_OP_TILE_CHECK;
    look_helpers_workspace = (LookHelpersWorkspace*)platform_room_scratch();
    look_helpers_room = room;
    look_helpers_viewer = viewer;
    look_helpers_tile_x = tile_x;
    look_helpers_tile_y = tile_y;
    look_helpers_color = color;
    look_helpers_light = (tile_x < PLATFORM_MAP_WIDTH && tile_y < PLATFORM_MAP_HEIGHT)
                             ? platform_brightness[(uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x]
                             : 0u;
    far_call_bank(BANK_LOOK_HELPERS, look_helpers_entry);
    return look_helpers_result;
}

uint8_t platform_look_tile(const PlatformRoom* room,
                           uint8_t tile_x, uint8_t tile_y, uint8_t color) {
    look_helpers_op = LOOK_HELPERS_OP_TILE;
    look_helpers_workspace = (LookHelpersWorkspace*)platform_room_scratch();
    look_helpers_room = room;
    look_helpers_tile_x = tile_x;
    look_helpers_tile_y = tile_y;
    look_helpers_color = color;
    far_call_bank(BANK_LOOK_HELPERS, look_helpers_entry);
    return look_helpers_result;
}

void platform_object_take_prompt(uint8_t type_id, uint8_t color) {
    look_helpers_op = LOOK_HELPERS_OP_TAKE_PROMPT;
    look_helpers_workspace = (LookHelpersWorkspace*)platform_room_scratch();
    look_helpers_type_id = type_id;
    look_helpers_color = color;
    far_call_bank(BANK_LOOK_HELPERS, look_helpers_entry);
}

void platform_object_taken_message(uint8_t type_id, uint8_t color) {
    look_helpers_op = LOOK_HELPERS_OP_TAKEN_MSG;
    look_helpers_workspace = (LookHelpersWorkspace*)platform_room_scratch();
    look_helpers_type_id = type_id;
    look_helpers_color = color;
    far_call_bank(BANK_LOOK_HELPERS, look_helpers_entry);
}

uint16_t platform_room_object_limit(const PlatformRoom* room) {
    uint16_t limit;

    if (room == rendered_room) return rendered_object_limit;
    limit = PLATFORM_ROOM_OBJECT_COUNT;
    while (limit > 0u && room->objects[limit - 1u].type == 0u) {
        --limit;
    }
    return limit;
}

/* Oscar64 expands main only. Initialize KERNAL/CIA state and explicitly
 * load high-RAM assets before drawing or enabling the raster split.
 * Room 0 uses the same banked resource path as subsequent rooms. */
volatile uint8_t boot_trace;
#define TRACE(n) (boot_trace = (n))

void platform_init(void) {
    /* EasyFlash enters without a KERNAL reset. Initialize hardware,
     * KERNAL vectors and editor/keyboard state, but do NOT call RAMTAS:
     * it would clear the live Oscar64 zero page and software stack. */
    __asm {
        sei
        jsr $ff84
        jsr $ff8a
        lda #$04
        sta $0288
        jsr $ff81
        sei
    }
    /* CINT enables the CIA timer interrupt. Keep it masked until our
     * own raster handler and all display assets are ready. */
    *((volatile uint8_t*)0xdc0du) = 0x7fu;
    *((volatile uint8_t*)0xdd0du) = 0x7fu;
    __asm {
        lda $dc0d
        lda $dd0d
    }
    *((volatile uint8_t*)0x0098u) = 0u; /* no open KERNAL files */
    *((volatile uint8_t*)0x0099u) = 0u; /* keyboard input */
    *((volatile uint8_t*)0x009au) = 3u; /* screen output */
    platform_cart_bank = 0u;
    TRACE(1u);
    *((volatile uint8_t*)0xdd00u) &= 0xfcu;
    ((volatile uint8_t*)0xd000u)[0x20] = 0u;
    ((volatile uint8_t*)0xd000u)[0x21] = 0u;
    ((volatile uint8_t*)0xd000u)[0x18] = 0xeau;
    ((volatile uint8_t*)0xd000u)[0x11] = 0x0bu; /* blank, text, scroll 3 */
    ((volatile uint8_t*)0xd000u)[0x16] = 0x08u; /* 40-column hires text */
    ((volatile uint8_t*)0xd000u)[0x15] = 0u;
    memset(platform_base_colors, 0, sizeof(platform_base_colors));
    memset(platform_brightness, PLATFORM_LIGHT_FULL, sizeof(platform_brightness));
    memset(platform_view_tiles, 1, sizeof(platform_view_tiles));
    platform_global_light = PLATFORM_LIGHT_FULL;
    platform_room_light_level = PLATFORM_ROOM_LIGHT_LEVEL_STATIC_FULL;
    platform_room_los_enabled = 1u;
    TRACE(2u);
    (void)platform_irq_save_disable();
    TRACE(3u);
    platform_memory_game();
    platform_charsets_load();
    far_call_bank(BANK_OBJECT_TYPE_C, object_types_b_load);
    TRACE(4u);
    platform_tiles_load();
    TRACE(5u);
    (void)platform_room_load(&platform_room, 0u);
    TRACE(6u);
    platform_player = &platform_room.objects[0];
    player_spawn_type = platform_player->type;
    TRACE(7u);
    env_install_null();
    TRACE(8u);
    raster_irq_install();
    TRACE(9u);
}

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

static uint8_t room_validate(const PlatformRoom* room, uint8_t room_id) {
    if (room->width != PLATFORM_MAP_WIDTH || room->height != PLATFORM_MAP_HEIGHT ||
        room->id != room_id || room->format != PLATFORM_ROOM_FORMAT ||
        (room->exit_mask & 0xf0u) != 0u) {
        return PLATFORM_ERR_FORMAT;
    }
    return PLATFORM_OK;
}

static uint8_t room_stage_load(uint8_t room_id) {
    if (!platform_room_data_fetch(room_id, &room_stage)) return PLATFORM_ERR_NOT_FOUND;
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
        platform_room_light_level = room->north_text;
        platform_room_los_enabled = room->east_text;
        if (platform_room_light_level <= PLATFORM_LIGHT_FULL) {
            platform_global_light = platform_room_light_level;
        }
    }
}

uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id) {
    uint8_t status;
    status = room_stage_load(room_id);
    if (status != PLATFORM_OK) return status;
    room_commit(room, room_id);
    return PLATFORM_OK;
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
    native_object_source = source_y == 0u
        ? source_x
        : (uint8_t)(source_y * OBJECT_WIDTH(type) + source_x);
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
        player->y >= PLATFORM_MAP_CHAR_HEIGHT ||
        platform_room_los_enabled == 0u) {
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
    if (delta_x > (PLATFORM_LIGHT_MAX_RADIUS >> 1) ||
        delta_y > (PLATFORM_LIGHT_MAX_RADIUS >> 1)) return PLATFORM_LIGHT_NONE;
    distance = platform_light_distance[(uint16_t)delta_y * 16u + delta_x] << 1;
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

    wall_count = 0u;
    tile_offset = 0u;
    for (y = 0u; y < PLATFORM_MAP_HEIGHT; ++y) {
        for (x = 0u; x < PLATFORM_MAP_WIDTH; ++x, ++tile_offset) {
            if (tile_properties[room->tiles[tile_offset]] & PLATFORM_TILE_BLOCKS_VIEW) {
                wall_tile_offsets[wall_count] = tile_offset;
                wall_x[wall_count] = x;
                wall_y[wall_count] = y;
                wall_light_cache[wall_count] = 0u;
                ++wall_count;
            }
        }
    }
    wall_cache_room = room;
}

static void wall_cache_add_source(void) {
    uint8_t i;
    uint8_t quadrants;
    uint8_t level;

    for (i = 0u; i < wall_count; ++i) {
        if (platform_light_visibility[wall_tile_offsets[i]] == 0u) continue;
        quadrants = wall_quadrants(native_light_source_x,
                                   native_light_source_y,
                                   wall_x[i], wall_y[i]);
        level = light_level_at(wall_x[i], wall_y[i]);
        if (level != PLATFORM_LIGHT_NONE) {
            wall_light_cache[i] = packed_light_add(
                wall_light_cache[i], quadrants, level);
        }
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
        level = packed_light_max(wall_light_cache[i], quadrants);
        platform_brightness[wall_tile_offsets[i]] =
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
    uint8_t tile_radius;

    if (object == 0 || object->type == 0u ||
        object->x >= PLATFORM_MAP_CHAR_WIDTH ||
        object->y >= PLATFORM_MAP_CHAR_HEIGHT) return;
    type = platform_object_type_get(object->type);
    radius = PLATFORM_OBJECT_LIGHT(type);
    if (radius == 0u) return;
    if (radius > PLATFORM_LIGHT_MAX_RADIUS) radius = PLATFORM_LIGHT_MAX_RADIUS;
    tile_radius = (radius + 1u) >> 1;

    native_light_source_x = object->x >> 1;
    native_light_source_y = object->y >> 1;
    min_x = native_light_source_x > tile_radius
                ? native_light_source_x - tile_radius : 0u;
    max_x = (uint16_t)native_light_source_x + tile_radius < PLATFORM_MAP_WIDTH
                ? native_light_source_x + tile_radius : PLATFORM_MAP_WIDTH - 1u;
    min_y = native_light_source_y > tile_radius
                ? native_light_source_y - tile_radius : 0u;
    max_y = (uint16_t)native_light_source_y + tile_radius < PLATFORM_MAP_HEIGHT
                ? native_light_source_y + tile_radius : PLATFORM_MAP_HEIGHT - 1u;
    native_light_radius = radius;
    native_light_min_x = min_x;
    native_light_min_y = min_y;
    native_light_columns = max_x - min_x + 1u;
    native_light_rows = max_y - min_y + 1u;
    native_light_screen_offset =
        (uint16_t)min_y * PLATFORM_MAP_WIDTH + min_x;
    native_light_visibility_offset = min_y * PLATFORM_MAP_WIDTH;
    visibility_build(room, native_light_source_x, native_light_source_y,
                     min_x, max_x, min_y, max_y,
                     platform_light_visibility);
    wall_cache_add_source();
    platform_light_source_apply_native();
}

void platform_lighting_rebuild(const PlatformRoom* room,
                               const PlatformObject* player) {
    uint16_t i;
    uint16_t limit;

    if (room != 0 &&
        platform_room_light_level == PLATFORM_ROOM_LIGHT_LEVEL_STATIC_FULL) {
        memset(platform_brightness, PLATFORM_LIGHT_FULL,
               sizeof(platform_brightness));
        wall_cache_room = 0;
        if (platform_room_los_enabled == 0u) {
            platform_color_copy_native();
        } else {
            platform_lighting_apply();
        }
        return;
    }

    memset(platform_brightness, platform_global_light,
           sizeof(platform_brightness));
    if (room != 0) {
        wall_cache_prepare(room);
        limit = platform_room_object_limit(room);
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

static void redraw_map_and_objects(const PlatformRoom* room,
                                   const PlatformObject* player) {
    uint16_t i;
    uint8_t y;
    rendered_room = room;
    rendered_player = player;
    rendered_object_limit = PLATFORM_ROOM_OBJECT_COUNT;
    while (rendered_object_limit > 0u &&
           room->objects[rendered_object_limit - 1u].type == 0u) {
        --rendered_object_limit;
    }
    platform_map_draw_native(room);
    for (y = 0u; y < PLATFORM_MAP_CHAR_HEIGHT; ++y) {
        for (i = 0; i < rendered_object_limit; ++i) {
            if (player == &room->objects[i]) continue;
            if (room->objects[i].y == y) object_draw_base(&room->objects[i]);
        }
        if (player->y == y) object_draw_base(player);
    }
}

void platform_room_draw(const PlatformRoom* room, const PlatformObject* player) {
    if (room == 0) return;
    platform_look_cursor_hide();
    platform_text_area_clear_native();
    platform_color_clear_native();
    redraw_map_and_objects(room, player);
    view_rebuild(room, player);
    platform_lighting_rebuild(room, player);
}

void platform_lighting_apply(void) {
    platform_lighting_apply_native();
}

void platform_lighting_set_global(uint8_t level) {
    if (level >= PLATFORM_LIGHT_LEVEL_COUNT) level = PLATFORM_LIGHT_FULL;
    platform_global_light = level;
    platform_lighting_rebuild(rendered_room, rendered_player);
}

void platform_lightning(void) {
    platform_lightning_native();
}

void platform_object_move(PlatformRoom* room, PlatformObject* object,
                          uint8_t new_x, uint8_t new_y,
                          const PlatformObject* player) {
    uint8_t light_changed;
    uint8_t view_changed;
    if (room == 0 || object == 0 || object->type == 0u) return;
    light_changed = PLATFORM_OBJECT_LIGHT(platform_object_type_get(object->type)) != 0u &&
                    ((object->x >> 1) != (new_x >> 1) ||
                     (object->y >> 1) != (new_y >> 1));
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
    if (light_changed && room == rendered_room) {
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

void platform_player_set_type(uint8_t type_id) {
    if (platform_player == 0 || platform_player->type == type_id) return;
    dirty_clear();
    mark_object_cells(platform_player);
    platform_player->type = type_id;
    mark_object_cells(platform_player);
    redraw_dirty(&platform_room, platform_player);
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
    return game_transition_request(room_id, (uint8_t)new_x, (uint8_t)new_y);
}

void platform_wait_frame(void) {
    uint8_t frame;
    if (!platform_raster_irq_active) {
        while (!(((volatile uint8_t*)0xd000u)[0x11] & 0x80u)) {
        }
        while (((volatile uint8_t*)0xd000u)[0x11] & 0x80u) {
        }
        return;
    }
    frame = platform_frame_counter;
    while (platform_frame_counter == frame) {
    }
    /* See src/irq.c's platform_weather_service() comment: the raster IRQ
     * only flags that a weather tick is due; this runs it from ordinary
     * foreground code once per frame, right after the frame boundary that
     * flagged it. Not reached in the !platform_raster_irq_active branch
     * above, matching the original's "no weather ticks while suspended". */
    platform_weather_service();
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
        actor = platform_object_type_info_get(room->objects[i].type)->flags &
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
    is_actor = platform_object_type_info_get(type)->flags & PLATFORM_OBJECT_FLAG_ACTOR;
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

/* See platform.c's original comment history (MEMORY_MAP.md) on why every
 * $D011 write here masks bit 7 off first. */
void platform_screen_blank(void) {
    ((volatile uint8_t*)0xd000u)[0x11] &= 0x6fu;
}

void platform_screen_unblank(void) {
    ((volatile uint8_t*)0xd000u)[0x11] = (((volatile uint8_t*)0xd000u)[0x11] & 0x7fu) | 0x10u;
}

void platform_sprites_hide_all(void) {
    ((volatile uint8_t*)0xd000u)[0x15] = 0u;
}

uint8_t platform_room_enter(uint8_t room_id, uint8_t actor_type,
                            uint8_t new_x, uint8_t new_y) {
    PlatformObject actor;
    uint8_t slot;
    uint8_t result;
    uint8_t removed_actor;
    uint8_t tile;

    if (actor_type == 0u || new_x >= PLATFORM_MAP_CHAR_WIDTH ||
        new_y >= PLATFORM_MAP_CHAR_HEIGHT) return PLATFORM_ERR_ARGUMENT;
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
        goto transition_done;
    }
    if (world_store_enabled) {
        world_state_room = &platform_room;
        world_state_op = WORLD_STATE_OP_STORE;
        far_call_bank(BANK_WORLD_STATE, world_state_entry);
        result = world_state_result;
        if (result != PLATFORM_OK) {
            goto transition_done;
        }
    }
    world_state_room = &room_stage;
    world_state_op = WORLD_STATE_OP_RESTORE;
    far_call_bank(BANK_WORLD_STATE, world_state_entry);
    result = world_state_result;
    if (result != PLATFORM_OK) {
        goto transition_done;
    }
    result = platform_room_object_add(&room_stage, actor.type,
                                      actor.x, actor.y, &slot);
    if (result != PLATFORM_OK) {
        goto transition_done;
    }

    if (removed_actor) {
        platform_player->type = 0;
    }
    platform_player_slot = slot;
    room_commit(&platform_room, room_id);
    platform_player = &platform_room.objects[slot];
    game_room_code_activate();
    result = PLATFORM_OK;
transition_done:
    return result;
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

uint8_t* platform_room_scratch(void) {
    return PLATFORM_ROOM_SCRATCH_BUFFER;
}

uint16_t platform_room_scratch_bytes(void) {
    return PLATFORM_ROOM_SCRATCH_MAX_BYTES;
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

static void look_exit_write(const char* text, uint8_t color) {
    platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
    platform_text_output_color = color & 0x0fu;
    platform_text_output_native(text);
}

uint8_t platform_look_exit(const PlatformRoom* room,
                           const PlatformObject* viewer,
                           uint8_t edge_x, uint8_t edge_y,
                           uint8_t direction, uint8_t color) {
    uint8_t neighbor;
    uint8_t result;

    if (direction > PLATFORM_DIRECTION_SOUTH) return PLATFORM_ERR_ARGUMENT;
    result = platform_look_tile_check(room, viewer, edge_x, edge_y, color);
    if (result != PLATFORM_OK) return result;
    if (platform_room_neighbor(room, direction, &neighbor) != PLATFORM_OK) {
        look_exit_write("There is no exit that way.", color);
        return PLATFORM_ERR_NOT_FOUND;
    }

    look_exit_write("An exit.", color);
    return PLATFORM_OK;
}

static void look_cursor_position(uint8_t tile_x, uint8_t tile_y) {
    volatile uint8_t* vic = (volatile uint8_t*)0xd000u;
    uint16_t sprite_x;

    sprite_x = 23u + (uint16_t)tile_x * 16u;
    vic[0x00] = (uint8_t)sprite_x;
    vic[0x01] = (uint8_t)(49u + (uint16_t)tile_y * 16u);
    if (sprite_x & 0x100u) vic[0x10] |= 0x01u;
    else vic[0x10] &= 0xfeu;
}

uint8_t platform_look_cursor_show(uint8_t tile_x, uint8_t tile_y) {
    volatile uint8_t* vic = (volatile uint8_t*)0xd000u;
    uint8_t row;

    if (tile_x >= PLATFORM_MAP_WIDTH || tile_y >= PLATFORM_MAP_HEIGHT) {
        return PLATFORM_ERR_ARGUMENT;
    }
    memset(SPRITE_DATA, 0, 64u);
    SPRITE_DATA[0] = 0xffu;
    SPRITE_DATA[1] = 0xffu;
    SPRITE_DATA[2] = 0xc0u;
    SPRITE_DATA[51] = 0xffu;
    SPRITE_DATA[52] = 0xffu;
    SPRITE_DATA[53] = 0xc0u;
    for (row = 1u; row < 17u; ++row) {
        SPRITE_DATA[(uint8_t)(row * 3u)] = 0x80u;
        SPRITE_DATA[(uint8_t)(row * 3u + 2u)] = 0x40u;
    }
    SPRITE_POINTERS[0] = (uint8_t)((0xfc00u - 0xc000u) / 64u);
    look_cursor_position(tile_x, tile_y);
    vic[0x17] &= 0xfeu;
    vic[0x1b] &= 0xfeu;
    vic[0x1c] &= 0xfeu;
    vic[0x1d] &= 0xfeu;
    look_cursor_visible = 1u;
    vic[0x15] |= 0x01u;
    platform_look_cursor_tick();
    return PLATFORM_OK;
}

uint8_t platform_look_cursor_move(uint8_t tile_x, uint8_t tile_y) {
    if (!look_cursor_visible || tile_x >= PLATFORM_MAP_WIDTH ||
        tile_y >= PLATFORM_MAP_HEIGHT) return PLATFORM_ERR_ARGUMENT;
    look_cursor_position(tile_x, tile_y);
    return PLATFORM_OK;
}

void platform_look_cursor_tick(void) {
    if (look_cursor_visible) {
        ((volatile uint8_t*)0xd000u)[0x27] =
            look_cursor_colors[(platform_frame_counter >> 2) & 0x07u];
    }
}

void platform_look_cursor_hide(void) {
    ((volatile uint8_t*)0xd000u)[0x15] &= 0xfeu;
    look_cursor_visible = 0u;
}

#define PORTRAIT_FIRST_SPRITE   1u
#define PORTRAIT_BG_SPRITE      5u
#define PORTRAIT_SPRITE_WIDTH   24u
#define PORTRAIT_SPRITE_HEIGHT  21u
#define PORTRAIT_MARGIN         16u
#define PORTRAIT_SLIDE_STEP     4u
#define PORTRAIT_SCREEN_X       23u
#define PORTRAIT_SCREEN_Y       49u
#define PORTRAIT_SPRITE_DATA \
    ((uint8_t*)(0xfc00u + PORTRAIT_FIRST_SPRITE * 64u))
#define PORTRAIT_BG_DATA \
    ((uint8_t*)(0xfc00u + PORTRAIT_BG_SPRITE * 64u))

static void portrait_position(uint16_t left_x, uint8_t top_y) {
    volatile uint8_t* vic = (volatile uint8_t*)0xd000u;
    uint16_t right_x;
    uint8_t bottom_y;
    uint8_t msb;

    right_x = left_x + PORTRAIT_SPRITE_WIDTH;
    bottom_y = (uint8_t)(top_y + PORTRAIT_SPRITE_HEIGHT);

    vic[2u] = (uint8_t)left_x;  vic[3u] = top_y;
    vic[4u] = (uint8_t)right_x; vic[5u] = top_y;
    vic[6u] = (uint8_t)left_x;  vic[7u] = bottom_y;
    vic[8u] = (uint8_t)right_x; vic[9u] = bottom_y;
    vic[10u] = (uint8_t)left_x; vic[11u] = top_y;

    msb = 0u;
    if (left_x & 0x100u) msb |= 0x2au;
    if (right_x & 0x100u) msb |= 0x14u;
    vic[0x10] = (vic[0x10] & 0xc1u) | msb;
}

uint8_t platform_portrait_show(uint8_t portrait_id, uint8_t side) {
    volatile uint8_t* vic = (volatile uint8_t*)0xd000u;
    uint16_t left_x;
    uint8_t target_y;
    uint8_t y;

    if (side != PLATFORM_PORTRAIT_LEFT && side != PLATFORM_PORTRAIT_RIGHT) {
        return PLATFORM_ERR_ARGUMENT;
    }

    /* An unpopulated/missing portrait_id fetches 0 bytes and leaves
     * PORTRAIT_SPRITE_DATA whatever it last held - same "renders as
     * garbage, not a crash" contract as any other resource fetch. */
    (void)platform_resource_fetch(PLATFORM_RESOURCE_KIND_PORTRAIT, portrait_id,
                                  PORTRAIT_SPRITE_DATA, PLATFORM_PORTRAIT_BYTES);

    env_disable();

    memset(PORTRAIT_BG_DATA, 0xffu, 63u);
    PORTRAIT_BG_DATA[63] = 0u;

    SPRITE_POINTERS[1] = (uint8_t)((0xfc40u - 0xc000u) / 64u);
    SPRITE_POINTERS[2] = (uint8_t)((0xfc80u - 0xc000u) / 64u);
    SPRITE_POINTERS[3] = (uint8_t)((0xfcc0u - 0xc000u) / 64u);
    SPRITE_POINTERS[4] = (uint8_t)((0xfd00u - 0xc000u) / 64u);
    SPRITE_POINTERS[5] = (uint8_t)((0xfd40u - 0xc000u) / 64u);

    vic[0x17] = (vic[0x17] & 0xc1u) | 0x20u;
    vic[0x1b] &= 0xc1u;
    vic[0x1c] &= 0xc1u;
    vic[0x1d] = (vic[0x1d] & 0xc1u) | 0x20u;
    vic[0x28] = 1u;
    vic[0x29] = 1u;
    vic[0x2a] = 1u;
    vic[0x2b] = 1u;
    vic[0x2c] = 0u;

    left_x = side == PLATFORM_PORTRAIT_LEFT
                 ? PORTRAIT_SCREEN_X + PORTRAIT_MARGIN
                 : PORTRAIT_SCREEN_X + PLATFORM_MAP_CHAR_WIDTH * 8u -
                       PORTRAIT_MARGIN - PLATFORM_PORTRAIT_WIDTH;
    target_y = PORTRAIT_SCREEN_Y + PORTRAIT_MARGIN;
    y = (uint8_t)(PORTRAIT_SCREEN_Y - PLATFORM_PORTRAIT_HEIGHT);
    portrait_position(left_x, y);
    vic[0x15] |= 0x3eu;

    while (y < target_y) {
        platform_wait_frame();
        y = (uint8_t)(target_y - y > PORTRAIT_SLIDE_STEP
                          ? y + PORTRAIT_SLIDE_STEP : target_y);
        portrait_position(left_x, y);
    }
    return PLATFORM_OK;
}

void platform_portrait_hide(void) {
    ((volatile uint8_t*)0xd000u)[0x15] &= 0xc1u;
    env_enable();
}
