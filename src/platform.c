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
#define INITIAL_OBJECT_TYPE_COUNT 2u
#define EF_FIRST_ROOM_BANK 3u
#define EF_ROOMS_PER_BANK  6u
#define EF_TYPE_BANK_0     46u
#define EF_TYPE_BANK_1     47u
#define EF_PORTRAIT_FIRST_BANK  49u
#define EF_PORTRAITS_PER_BANK   32u
#define EF_RESOURCE_DIRECTORY_BANK   57u
#define RESOURCE_DIRECTORY_ENTRY_BYTES 8u
/* One kind's directory (256 entries); three sit back to back at the head of
 * EF_RESOURCE_DIRECTORY_BANK's ROML half, in PLATFORM_RESOURCE_KIND_* order
 * - keep in sync with tools/pack_easyflash.py's RESOURCE_DIRECTORY_BYTES/
 * RESOURCE_KIND_* ordering. */
#define RESOURCE_DIRECTORY_BYTES (256u * RESOURCE_DIRECTORY_ENTRY_BYTES)

/*
 * Hot object-type records (see PlatformObjectType) are packed at a 35-byte
 * stride across the same three resident zones the old 64-byte records used:
 * zone A (types 0-116) always visible at $C000, zone B (117-233) beneath
 * I/O at $D000, zone C (234-255) beneath KERNAL at $E000. Cold records
 * (PlatformObjectTypeInfo, 15 bytes) are never resident; they stay on
 * EasyFlash bank 47 right after zone C's hot bytes. tools/pack_easyflash.py
 * must lay bank 46/47 out exactly this way.
 */
#define OBJECT_TYPE_RECORD_BYTES     35u
#define OBJECT_TYPE_ZONE_A_COUNT     117u
#define OBJECT_TYPE_ZONE_B_COUNT     117u
#define OBJECT_TYPE_ZONE_AB_COUNT    (OBJECT_TYPE_ZONE_A_COUNT + OBJECT_TYPE_ZONE_B_COUNT)
#define OBJECT_TYPE_ZONE_C_COUNT     (PLATFORM_OBJECT_TYPE_COUNT - OBJECT_TYPE_ZONE_AB_COUNT)
#define OBJECT_TYPE_COLD_BYTES       15u
#define OBJECT_TYPE_COLD_BASE        (OBJECT_TYPE_ZONE_C_COUNT * OBJECT_TYPE_RECORD_BYTES)

#define PORTRAIT_FIRST_SPRITE   1u
#define PORTRAIT_BG_SPRITE      5u
#define PORTRAIT_SPRITE_WIDTH   24u
#define PORTRAIT_SPRITE_HEIGHT  21u
#define PORTRAIT_MARGIN         16u
#define PORTRAIT_SLIDE_STEP     4u
#define PORTRAIT_SCREEN_X       23u
#define PORTRAIT_SCREEN_Y       49u
#define PORTRAIT_SPRITE_DATA \
    ((uint8_t*)(0x3a00u + PORTRAIT_FIRST_SPRITE * 64u))
#define PORTRAIT_BG_DATA \
    ((uint8_t*)(0x3a00u + PORTRAIT_BG_SPRITE * 64u))
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
void platform_easyflash_copy_roml(void);
void platform_easyflash_copy_romh(void);
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
static PlatformObjectType object_types_a[OBJECT_TYPE_ZONE_A_COUNT];
#pragma bss-name (pop)
#pragma bss-name (push, "OBJECTTYPES_B")
static PlatformObjectType object_types_b[OBJECT_TYPE_ZONE_B_COUNT];
#pragma bss-name (pop)
#pragma bss-name (push, "OBJECTTYPES_C")
static PlatformObjectType object_types_c[OBJECT_TYPE_ZONE_C_COUNT];
#pragma bss-name (pop)
PlatformObjectType platform_object_type_scratch;
static PlatformObjectTypeInfo object_type_info_scratch;
static uint8_t resource_directory_entry[RESOURCE_DIRECTORY_ENTRY_BYTES];
uint8_t platform_overlay_magic0;
uint8_t platform_overlay_magic1;
const PlatformObjectType* native_object_type;
uint8_t native_object_source;
#pragma bss-name (push, "ZEROPAGE")
uint8_t native_object_columns;
#pragma bss-name (pop)
uint8_t native_object_rows;
#pragma bss-name (push, "ZEROPAGE")
uint8_t native_object_row_skip;
#pragma bss-name (pop)
uint16_t native_object_screen_offset;
#pragma bss-name (push, "ZEROPAGE")
uint8_t native_light_source_x;
uint8_t native_light_source_y;
#pragma bss-name (pop)
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
uint8_t platform_brightness[PLATFORM_MAP_TILE_COUNT];
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
#pragma rodata-name (pop)

static uint8_t dirty_cells[DIRTY_BYTES];
static uint8_t dirty_x[DIRTY_CELL_LIMIT];
static uint8_t dirty_y[DIRTY_CELL_LIMIT];
static uint8_t dirty_count;
/* Non-static: read/written by the room-helpers overlay (modules/room_helpers.c)
 * via the resolver, same as any other resident symbol an overlay binds to. */
const PlatformRoom* rendered_room;
const PlatformObject* rendered_player;
uint16_t rendered_object_limit;
static uint8_t look_cursor_visible;
#pragma bss-name (push, "ROOMSTAGE")
static PlatformRoom room_stage;
#pragma bss-name (pop)

/* The current room's own script resource (resource ID == room ID; see
 * game_room_script_entry() in src/script_runtime.c and modules/script.c's
 * KIND_ROOM handling), fetched into room_stage's own memory - not a
 * separate buffer. room_stage is only "the pending room" for the brief span
 * between room_stage_load() and room_commit()'s memcpy out of it; every
 * actual use of room_stage ends before that memcpy (checked: both callers
 * of room_commit() never touch room_stage again afterward). So the instant
 * the memcpy completes, its memory is free until the *next* transition's
 * room_stage_load(), and room_commit() below claims it immediately for this
 * instead - the same "one buffer, temporally exclusive uses" pattern $A4E9
 * already uses across staging/overlays. Sized via sizeof(room_stage), not a
 * hardcoded constant, so it tracks PlatformRoom automatically if that
 * struct's own size ever changes. */
#define PLATFORM_ROOM_SCRATCH_BUFFER ((uint8_t*)&room_stage)
#define PLATFORM_ROOM_SCRATCH_MAX_BYTES ((uint16_t)sizeof(room_stage))
#pragma bss-name (push, "WORKBSS")
static uint8_t wall_light_cache[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_tile_offsets[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_x[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_y[PLATFORM_MAP_TILE_COUNT];
static uint8_t wall_count;
static const PlatformRoom* wall_cache_room;
#pragma bss-name (pop)
static PlatformRoomStoreHook room_store_hook;
static PlatformRoomRestoreHook room_restore_hook;
static uint8_t player_spawn_room;
static uint8_t player_spawn_slot;
static uint8_t player_spawn_type;

#pragma code-name (push, "CODE")
static void write_screen_cell(uint8_t x, uint8_t y,
                              uint8_t ch, uint8_t color) {
    uint16_t offset;
    uint16_t tile_offset;

    if (x >= PLATFORM_MAP_CHAR_WIDTH || y >= 25u) return;
    offset = (uint16_t)y * PLATFORM_MAP_CHAR_WIDTH + x;
    P_SCREEN_RAM[offset] = ch;
    color &= 0x0f;
    if (y < PLATFORM_MAP_CHAR_HEIGHT) {
        platform_base_colors[offset] = color;
        tile_offset = (uint16_t)(y >> 1) * PLATFORM_MAP_WIDTH + (x >> 1);
        color = platform_light_colors[
            ((platform_brightness[tile_offset] & 0x03u) << 4) | color];
        if (platform_view_tiles[tile_offset] == 0u) color = 0u;
    }

    P_COLOR_RAM[offset] = color;
}
#pragma code-name (pop)

static uint8_t object_type_is_valid(const PlatformObjectType* type) {
    uint8_t width;
    uint8_t height;

    width = OBJECT_WIDTH(type);
    height = OBJECT_HEIGHT(type);
    return width > 0u && height > 0u &&
           (uint16_t)width * height <= PLATFORM_OBJECT_CELL_COUNT &&
           HOTSPOT_X(type) < width && HOTSPOT_Y(type) < height;
}

#pragma code-name (push, "HIGHCODE")
const PlatformObjectType* platform_object_type_get(uint8_t type_id) {
    uint8_t irq_status;

    if (type_id < OBJECT_TYPE_ZONE_A_COUNT) {
        return &object_types_a[type_id];
    }
    irq_status = platform_irq_save_disable();
    platform_memory_all_ram();
    if (type_id < OBJECT_TYPE_ZONE_AB_COUNT) {
        memcpy(&platform_object_type_scratch,
               &object_types_b[type_id - OBJECT_TYPE_ZONE_A_COUNT],
               sizeof(platform_object_type_scratch));
    } else {
        memcpy(&platform_object_type_scratch,
               &object_types_c[type_id - OBJECT_TYPE_ZONE_AB_COUNT],
               sizeof(platform_object_type_scratch));
    }
    platform_memory_game();
    platform_irq_restore(irq_status);
    return &platform_object_type_scratch;
}
#pragma code-name (pop)

#pragma code-name (push, "HIGHCODE")
const PlatformObjectTypeInfo* platform_object_type_info_get(uint8_t type_id) {
    platform_ef_copy_bank = EF_TYPE_BANK_1;
    platform_ef_copy_offset = OBJECT_TYPE_COLD_BASE +
        (uint16_t)type_id * OBJECT_TYPE_COLD_BYTES;
    platform_ef_copy_destination = (uint16_t)&object_type_info_scratch;
    platform_ef_copy_size = OBJECT_TYPE_COLD_BYTES;
    platform_easyflash_copy_roml();
    return &object_type_info_scratch;
}
#pragma code-name (pop)

/*
 * Generic sparse resource directories: three read-only, variable-size-blob
 * directories (one per PLATFORM_RESOURCE_KIND_*, see platform.h), each 256
 * entries, back to back at the head of bank 57's ROML half, across EasyFlash
 * banks 57-63. tools/pack_easyflash.py places entries first-fit into
 * ROML/ROMH halves in bank order (all three kinds sharing one packing pass)
 * and never splits a resource across a bank switch (see
 * EASYFLASH_CARTRIDGE.md). Entry layout: bank, mode (0 = ROML half, 1 =
 * ROMH half), offset within that half (little-endian), length
 * (little-endian), 16-bit sum-of-bytes checksum (little-endian).
 */
/* Looks up kind/resource_id's directory entry into bank/mode/offset/size.
 * Returns PLATFORM_OK, or PLATFORM_ERR_NOT_FOUND for an absent/corrupt
 * entry. Shared by platform_resource_fetch() and
 * platform_resource_fetch_range(). */
#pragma code-name (push, "HIGHCODE")
static uint8_t resource_directory_lookup(uint8_t kind, uint8_t resource_id,
                                         uint8_t* bank, uint8_t* mode,
                                         uint16_t* offset, uint16_t* size) {
    platform_ef_copy_bank = EF_RESOURCE_DIRECTORY_BANK;
    platform_ef_copy_offset = (uint16_t)kind * RESOURCE_DIRECTORY_BYTES +
        (uint16_t)resource_id * RESOURCE_DIRECTORY_ENTRY_BYTES;
    platform_ef_copy_destination = (uint16_t)resource_directory_entry;
    platform_ef_copy_size = RESOURCE_DIRECTORY_ENTRY_BYTES;
    platform_easyflash_copy_roml();
    if (resource_directory_entry[0] == 0xffu) return PLATFORM_ERR_NOT_FOUND;

    *bank = resource_directory_entry[0];
    *mode = resource_directory_entry[1];
    *offset = (uint16_t)resource_directory_entry[2] |
              ((uint16_t)resource_directory_entry[3] << 8);
    *size = (uint16_t)resource_directory_entry[4] |
            ((uint16_t)resource_directory_entry[5] << 8);
    if (*mode > 1u || *size == 0u || *offset + *size > PLATFORM_RESOURCE_MAX_BYTES) {
        return PLATFORM_ERR_NOT_FOUND;
    }
    return PLATFORM_OK;
}

uint16_t platform_resource_fetch(uint8_t kind, uint8_t resource_id,
                                  uint8_t* destination, uint16_t capacity) {
    uint8_t bank;
    uint8_t mode;
    uint16_t offset;
    uint16_t size;
    uint16_t checksum;
    uint16_t sum;
    uint16_t i;

    if (resource_directory_lookup(kind, resource_id, &bank, &mode, &offset,
                                  &size) != PLATFORM_OK) {
        return 0u;
    }
    checksum = (uint16_t)resource_directory_entry[6] |
               ((uint16_t)resource_directory_entry[7] << 8);
    if (size > capacity) return 0u;

    platform_ef_copy_bank = bank;
    platform_ef_copy_offset = offset;
    platform_ef_copy_destination = (uint16_t)destination;
    platform_ef_copy_size = size;
    if (mode == 0u) platform_easyflash_copy_roml();
    else platform_easyflash_copy_romh();

    sum = 0u;
    for (i = 0u; i < size; ++i) sum += destination[i];
    if (sum != checksum) return 0u;
    return size;
}

/* Fetches at most `capacity` bytes starting at byte `start` of
 * resource_id's data (not from the top of the resource, unlike
 * platform_resource_fetch()) - for a resource bigger than any single
 * resident buffer can hold in one piece (up to PLATFORM_RESOURCE_MAX_BYTES,
 * 8 KiB - see modules/script.c's windowed reader). Returns the number of
 * bytes actually copied (0 for start >= the resource's size, or a lookup
 * failure). Unlike platform_resource_fetch(), this does not verify the
 * resource's checksum - that checksum covers the whole resource, not an
 * arbitrary sub-range, so a full-resource integrity check would need a
 * separate pass over every byte before a caller could use any of it; a
 * corrupt directory entry (the actual failure mode this guards against)
 * is still caught by resource_directory_lookup()'s own bounds checks. */
uint16_t platform_resource_fetch_range(uint8_t kind, uint8_t resource_id,
                                       uint16_t start, uint8_t* destination,
                                       uint16_t capacity) {
    uint8_t bank;
    uint8_t mode;
    uint16_t offset;
    uint16_t size;
    uint16_t remaining;
    uint16_t chunk;

    if (resource_directory_lookup(kind, resource_id, &bank, &mode, &offset,
                                  &size) != PLATFORM_OK) {
        return 0u;
    }
    if (start >= size) return 0u;

    remaining = size - start;
    chunk = remaining < capacity ? remaining : capacity;

    platform_ef_copy_bank = bank;
    platform_ef_copy_offset = offset + start;
    platform_ef_copy_destination = (uint16_t)destination;
    platform_ef_copy_size = chunk;
    if (mode == 0u) platform_easyflash_copy_roml();
    else platform_easyflash_copy_romh();

    return chunk;
}

/* Total size of the resource most recently looked up by
 * platform_resource_fetch() or platform_resource_fetch_range(), regardless
 * of how much of it actually fit in the caller's destination buffer that
 * call. Reads resource_directory_lookup()'s already-populated static
 * buffer rather than needing a dedicated global (BSS here has no spare
 * bytes - see MEMORY_MAP.md). Only meaningful right after one of those two
 * calls; a fetch failure leaves it holding whatever the last successful
 * lookup found. */
uint16_t platform_resource_last_size(void) {
    return (uint16_t)resource_directory_entry[4] |
           ((uint16_t)resource_directory_entry[5] << 8);
}
#pragma code-name (pop)

/*
 * Generic loaded-overlay fetch, shared by every $A4E9-$B4FF overlay (the
 * inventory/story overlay and the save/load overlay): copy the fixed
 * 16-byte header from the given EasyFlash bank/half, read its declared
 * size, copy the complete payload, then hand off to the native validator
 * (src/inventory_api.s) with the requested magic bytes. Resident code
 * budget is too tight to duplicate this loader/validator per overlay.
 */
#define OVERLAY_BASE          ((uint8_t*)0xa4e9)
#define OVERLAY_HEADER_BYTES  16u
#define OVERLAY_MAX_BYTES     0x1017u

uint8_t platform_overlay_validate_native(uint16_t loaded_size);

#pragma code-name (push, "HIGHCODE")
uint8_t __fastcall__ platform_overlay_load(uint8_t bank, uint8_t use_romh,
                                           uint16_t offset, uint8_t magic0,
                                           uint8_t magic1) {
    uint16_t size;

    platform_overlay_magic0 = magic0;
    platform_overlay_magic1 = magic1;

    platform_ef_copy_bank = bank;
    platform_ef_copy_offset = offset;
    platform_ef_copy_destination = (uint16_t)OVERLAY_BASE;
    platform_ef_copy_size = OVERLAY_HEADER_BYTES;
    if (use_romh) platform_easyflash_copy_romh();
    else platform_easyflash_copy_roml();
    size = (uint16_t)OVERLAY_BASE[6] | ((uint16_t)OVERLAY_BASE[7] << 8);
    if (size < OVERLAY_HEADER_BYTES || size > OVERLAY_MAX_BYTES) {
        return PLATFORM_ERR_FORMAT;
    }

    platform_ef_copy_bank = bank;
    platform_ef_copy_offset = offset;
    platform_ef_copy_destination = (uint16_t)OVERLAY_BASE;
    platform_ef_copy_size = size;
    if (use_romh) platform_easyflash_copy_romh();
    else platform_easyflash_copy_roml();
    return platform_overlay_validate_native(size);
}
#pragma code-name (pop)

/*
 * Room-helpers overlay ("RH"): platform_room_object_remove/platform_room_
 * neighbor's actual bodies (modules/room_helpers.c). Not every $A4E9 overlay
 * gets its own EasyFlash bank - both halves of every bank through 48 are
 * already spoken for (rooms, object types, inventory, save/load), so this
 * one shares TYPE_BANK_1's ROML half with the object-type Zone C table
 * (src/platform.c's OBJECT_TYPE_ZONE_C_COUNT, 770 bytes) at a fixed offset
 * comfortably past it, instead of getting a bank of its own - see
 * tools/pack_easyflash.py's ROOM_HELPERS_EF_OFFSET.
 */
#define ROOM_HELPERS_EF_BANK    47u
#define ROOM_HELPERS_EF_OFFSET  1024u
#define ROOM_HELPERS_MAGIC_0    0x52u /* 'R' */
#define ROOM_HELPERS_MAGIC_1    0x48u /* 'H' */
#define ROOM_HELPERS_OP_NEIGHBOR 0u
#define ROOM_HELPERS_OP_REMOVE   1u

void platform_overlay_run_native(void);

/* Explicitly zero-initialized (not plain BSS): BSSRAM has no margin left,
 * while PROGRAM (where cc65 places initialized DATA) does. See banking.s's
 * ef_shadow_bank for the same trick and why it matters here too - these are
 * both written and read from ordinary resident code, never from a banked
 * window, so plain RAM placement is all that's needed, just not out of the
 * full BSSRAM budget. */
PlatformRoom* room_helpers_room = 0;
uint8_t room_helpers_slot = 0;
const PlatformObject* room_helpers_player = 0;
uint8_t room_helpers_direction = 0;
uint8_t room_helpers_room_id = 0;
uint8_t room_helpers_op = 0;
uint8_t room_helpers_result = 0;

#pragma code-name (push, "HIGHCODE")
uint8_t platform_room_neighbor(const PlatformRoom* room, uint8_t direction,
                               uint8_t* room_id) {
    uint8_t status;

    if (room == 0 || room_id == 0) return PLATFORM_ERR_ARGUMENT;
    room_helpers_op = ROOM_HELPERS_OP_NEIGHBOR;
    room_helpers_room = (PlatformRoom*)room;
    room_helpers_direction = direction;
    status = platform_overlay_load(ROOM_HELPERS_EF_BANK, 0u,
                                   ROOM_HELPERS_EF_OFFSET,
                                   ROOM_HELPERS_MAGIC_0, ROOM_HELPERS_MAGIC_1);
    if (status != PLATFORM_OK) return status;
    platform_overlay_run_native();
    if (room_helpers_result == PLATFORM_OK) *room_id = room_helpers_room_id;
    return room_helpers_result;
}

uint8_t platform_room_object_remove(PlatformRoom* room, uint8_t slot,
                                    const PlatformObject* player) {
    uint8_t status;

    if (room == 0) return PLATFORM_ERR_ARGUMENT;
    room_helpers_op = ROOM_HELPERS_OP_REMOVE;
    room_helpers_room = room;
    room_helpers_slot = slot;
    room_helpers_player = player;
    status = platform_overlay_load(ROOM_HELPERS_EF_BANK, 0u,
                                   ROOM_HELPERS_EF_OFFSET,
                                   ROOM_HELPERS_MAGIC_0, ROOM_HELPERS_MAGIC_1);
    if (status != PLATFORM_OK) return status;
    platform_overlay_run_native();
    return room_helpers_result;
}
#pragma code-name (pop)

/*
 * Look-helpers overlay ("LH"): platform_look_tile/_check/object_take_prompt/
 * object_taken_message's actual bodies (modules/look_helpers.c). Same
 * shared TYPE_BANK_1 ROML half as room-helpers/script above, at a further
 * offset past both - see tools/pack_easyflash.py's LOOK_HELPERS_EF_OFFSET.
 * platform_look_exit() stays here (not moved): it calls
 * platform_room_neighbor() above, itself an overlay in this same window,
 * so it must stay resident to call that sequentially without overwriting
 * its own still-executing code if it were overlay content too.
 */
#define LOOK_HELPERS_EF_BANK    47u
#define LOOK_HELPERS_EF_OFFSET  4864u
#define LOOK_HELPERS_MAGIC_0    0x4Cu /* 'L' */
#define LOOK_HELPERS_MAGIC_1    0x48u /* 'H' */
#define LOOK_HELPERS_OP_TILE         0u
#define LOOK_HELPERS_OP_TILE_CHECK   1u
#define LOOK_HELPERS_OP_TAKE_PROMPT  2u
#define LOOK_HELPERS_OP_TAKEN_MSG    3u

/* Explicitly zero-initialized (not plain BSS) - see room_helpers_room's own
 * comment above for why. */
const PlatformRoom* look_helpers_room = 0;
const PlatformObject* look_helpers_viewer = 0;
uint8_t look_helpers_tile_x = 0;
uint8_t look_helpers_tile_y = 0;
uint8_t look_helpers_color = 0;
uint8_t look_helpers_type_id = 0;
uint8_t look_helpers_op = 0;
uint8_t look_helpers_result = 0;

#pragma code-name (push, "HIGHCODE")
uint8_t platform_look_tile_check(const PlatformRoom* room,
                                 const PlatformObject* viewer,
                                 uint8_t tile_x, uint8_t tile_y,
                                 uint8_t color) {
    uint8_t status;

    look_helpers_op = LOOK_HELPERS_OP_TILE_CHECK;
    look_helpers_room = room;
    look_helpers_viewer = viewer;
    look_helpers_tile_x = tile_x;
    look_helpers_tile_y = tile_y;
    look_helpers_color = color;
    status = platform_overlay_load(LOOK_HELPERS_EF_BANK, 0u,
                                   LOOK_HELPERS_EF_OFFSET,
                                   LOOK_HELPERS_MAGIC_0, LOOK_HELPERS_MAGIC_1);
    if (status != PLATFORM_OK) return status;
    platform_overlay_run_native();
    return look_helpers_result;
}

uint8_t platform_look_tile(const PlatformRoom* room,
                           uint8_t tile_x, uint8_t tile_y, uint8_t color) {
    uint8_t status;

    look_helpers_op = LOOK_HELPERS_OP_TILE;
    look_helpers_room = room;
    look_helpers_tile_x = tile_x;
    look_helpers_tile_y = tile_y;
    look_helpers_color = color;
    status = platform_overlay_load(LOOK_HELPERS_EF_BANK, 0u,
                                   LOOK_HELPERS_EF_OFFSET,
                                   LOOK_HELPERS_MAGIC_0, LOOK_HELPERS_MAGIC_1);
    if (status != PLATFORM_OK) return status;
    platform_overlay_run_native();
    return look_helpers_result;
}

void platform_object_take_prompt(uint8_t type_id, uint8_t color) {
    look_helpers_op = LOOK_HELPERS_OP_TAKE_PROMPT;
    look_helpers_type_id = type_id;
    look_helpers_color = color;
    if (platform_overlay_load(LOOK_HELPERS_EF_BANK, 0u, LOOK_HELPERS_EF_OFFSET,
                              LOOK_HELPERS_MAGIC_0, LOOK_HELPERS_MAGIC_1) !=
        PLATFORM_OK) return;
    platform_overlay_run_native();
}

void platform_object_taken_message(uint8_t type_id, uint8_t color) {
    look_helpers_op = LOOK_HELPERS_OP_TAKEN_MSG;
    look_helpers_type_id = type_id;
    look_helpers_color = color;
    if (platform_overlay_load(LOOK_HELPERS_EF_BANK, 0u, LOOK_HELPERS_EF_OFFSET,
                              LOOK_HELPERS_MAGIC_0, LOOK_HELPERS_MAGIC_1) !=
        PLATFORM_OK) return;
    platform_overlay_run_native();
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

/* Non-static: called by the room-helpers overlay via the resolver. */
void dirty_clear(void) {
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

/* Non-static: called by the room-helpers overlay via the resolver. */
void mark_object_cells(const PlatformObject* object) {
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

uint16_t platform_room_object_limit(const PlatformRoom* room) {
    uint16_t limit;

    if (room == rendered_room) return rendered_object_limit;
    limit = PLATFORM_ROOM_OBJECT_COUNT;
    while (limit > 0u && room->objects[limit - 1u].type == 0u) {
        --limit;
    }
    return limit;
}

/* Non-static: called by the room-helpers overlay via the resolver. */
void redraw_dirty(const PlatformRoom* room,
                  const PlatformObject* player) {
    uint8_t i;
    uint8_t x;
    uint8_t y;
    uint8_t ch;
    uint8_t color;
    uint16_t offset;
    uint8_t visible_color;
    uint16_t object_limit;
    uint16_t tile_offset;

    object_limit = platform_room_object_limit(room);
    for (i = 0; i < dirty_count; ++i) {
        x = dirty_x[i];
        y = dirty_y[i];
        compose_cell(room, x, y, player, object_limit, &ch, &color);
        offset = (uint16_t)y * PLATFORM_MAP_CHAR_WIDTH + x;
        tile_offset = (uint16_t)(y >> 1) * PLATFORM_MAP_WIDTH + (x >> 1);
        platform_base_colors[offset] = color;
        visible_color = platform_light_colors[
            ((platform_brightness[tile_offset] & 0x03u) << 4) | (color & 0x0fu)];
        if (platform_view_tiles[tile_offset] == 0u) visible_color = 0u;
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
    P_VIC(0x15) = 0;
    memset(platform_base_colors, 0, sizeof(platform_base_colors));
    memset(platform_brightness, PLATFORM_LIGHT_FULL, sizeof(platform_brightness));
    memset(platform_view_tiles, 1, sizeof(platform_view_tiles));
    platform_global_light = PLATFORM_LIGHT_FULL;
    /* look_cursor_visible/wall_count/wall_cache_room are plain BSS globals,
     * already 0 from the C runtime's zero-init - platform_init() runs
     * exactly once, first thing in main(), so nothing could have dirtied
     * them yet. */
    (void)platform_irq_save_disable();
    if (!cartridge) {
        platform_object_types_clear();
        memcpy(object_types_a, initial_object_type_data,
               INITIAL_OBJECT_TYPE_COUNT * sizeof(PlatformObjectType));
    }
    platform_memory_game();
    memcpy(&platform_room, initial_room_data, sizeof(platform_room));
    if (cartridge) {
        (void)platform_object_types_load();
        (void)platform_room_load(&platform_room, 0u);
    }
    /* platform_current_room/platform_player_slot/player_spawn_room/
     * player_spawn_slot are plain BSS globals, already 0 - see above. */
    platform_player = &platform_room.objects[0];
    player_spawn_type = platform_player->type;
    /* Must happen before the IRQ is installed: weather_animate (src/irq.s)
     * calls into ENVCODE_TICK unconditionally every frame, and the first
     * real environment module isn't fetched until game_enter_room() (called
     * later, in main()) - so the raster IRQ would otherwise jump into
     * whatever garbage happens to be sitting in that reserved RAM the very
     * first time it fires. */
    env_install_null();
    raster_irq_install();
}

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
    status = room_load_easyflash(room_id);
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
        /* room_stage's memcpy out is done above, so its memory is free to
         * reuse as scratch for this room's own script resource now (see
         * PLATFORM_ROOM_SCRATCH_BUFFER) - the same resource ID as the room
         * itself. Nothing pre-fetches it here: game_room_script_entry()
         * (src/script_runtime.c) fetches whatever window it needs, fresh,
         * every time it runs, via modules/script.c's windowed reader -
         * a room's script resource can be up to PLATFORM_RESOURCE_MAX_BYTES
         * now, too big to usefully hold here in one piece anyway. */
    }
}

uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id) {
    uint8_t status;
    status = room_stage_load(room_id);
    if (status != PLATFORM_OK) return status;
    room_commit(room, room_id);
    return PLATFORM_OK;
}

uint8_t platform_object_types_load(void) {
    uint8_t irq_status;

    /* Zone A: types 0-116, bank 46 offset 0, direct to $C000 (never shadowed). */
    platform_ef_copy_bank = EF_TYPE_BANK_0;
    platform_ef_copy_offset = 0u;
    platform_ef_copy_destination = (uint16_t)object_types_a;
    platform_ef_copy_size = OBJECT_TYPE_ZONE_A_COUNT * OBJECT_TYPE_RECORD_BYTES;
    platform_easyflash_copy_roml();

    /* Zone B: types 117-233, bank 46 offset 4095, staged via $A4E9 then to $D000. */
    platform_ef_copy_offset = OBJECT_TYPE_ZONE_A_COUNT * OBJECT_TYPE_RECORD_BYTES;
    platform_ef_copy_destination = (uint16_t)P_OBJECT_TYPE_STAGE;
    platform_ef_copy_size = OBJECT_TYPE_ZONE_B_COUNT * OBJECT_TYPE_RECORD_BYTES;
    platform_easyflash_copy_roml();
    irq_status = platform_irq_save_disable();
    platform_memory_all_ram();
    memcpy(object_types_b, P_OBJECT_TYPE_STAGE,
           OBJECT_TYPE_ZONE_B_COUNT * OBJECT_TYPE_RECORD_BYTES);
    platform_memory_game();
    platform_irq_restore(irq_status);

    /* Zone C: types 234-255, bank 47 offset 0, direct to $E000 (KERNAL hidden). */
    platform_ef_copy_bank = EF_TYPE_BANK_1;
    platform_ef_copy_offset = 0u;
    platform_ef_copy_destination = (uint16_t)object_types_c;
    platform_ef_copy_size = OBJECT_TYPE_ZONE_C_COUNT * OBJECT_TYPE_RECORD_BYTES;
    platform_easyflash_copy_roml();
    raster_irq_vectors_restore();
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

/* Lets a one-shot overlay (e.g. the script/conversation interpreter) borrow
 * this same buffer as scratch RAM while it runs, the same "temporally
 * exclusive" reuse room_commit() already does with room_stage itself. Only
 * safe between room_commit() calls (i.e. while no transition is staging a
 * new room) - true for every current borrower, since they all run from
 * resident code between transitions, never from inside one. Nothing
 * resident reads it between borrows (each borrower fetches whatever it
 * needs, fresh), so unlike $A4E9's overlays there's no "restore it before
 * returning" contract to honor. */
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

/* platform_look_tile_check()'s real body now lives in the look-helpers
 * overlay (see the platform_look_tile_check wrapper earlier in this file);
 * platform_look_exit() calls that wrapper like any other caller. Its own
 * two messages are fixed literals - unlike look_tile/_check's dynamically
 * assembled ones, they need none of the moved look_buffer/look_append_*
 * machinery, just a direct write. */
static void look_exit_write(const char* text, uint8_t color) {
    platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
    platform_text_output_color = color & 0x0fu;
    platform_text_output_native(text);
}

#pragma code-name (push, "UPPERCODE")
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

    /* Custom per-exit descriptions aren't wired up yet - room->north_text
     * etc. are currently unread (see the PlatformRoom doc comment); this
     * always shows the generic message until that's built on top of the
     * room-script mechanism (game_room_script_entry()). */
    look_exit_write("An exit.", color);
    return PLATFORM_OK;
}
#pragma code-name (pop)

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

static uint8_t portrait_load_easyflash(uint8_t portrait_id) {
    platform_ef_copy_bank =
        (uint8_t)(EF_PORTRAIT_FIRST_BANK + portrait_id / EF_PORTRAITS_PER_BANK);
    platform_ef_copy_offset =
        (uint16_t)(portrait_id % EF_PORTRAITS_PER_BANK) * PLATFORM_PORTRAIT_BYTES;
    platform_ef_copy_destination = (uint16_t)PORTRAIT_SPRITE_DATA;
    platform_ef_copy_size = PLATFORM_PORTRAIT_BYTES;
    platform_easyflash_copy_roml();
    return PLATFORM_OK;
}

/* left_x/right_x share the 9th (MSB) bit across sprites 1/3/5 and 2/4. */
#pragma code-name (push, "UPPERCODE")
static void portrait_position(uint16_t left_x, uint8_t top_y) {
    uint16_t right_x;
    uint8_t bottom_y;
    uint8_t msb;

    right_x = left_x + PORTRAIT_SPRITE_WIDTH;
    bottom_y = (uint8_t)(top_y + PORTRAIT_SPRITE_HEIGHT);

    P_VIC(2u) = (uint8_t)left_x;  P_VIC(3u) = top_y;
    P_VIC(4u) = (uint8_t)right_x; P_VIC(5u) = top_y;
    P_VIC(6u) = (uint8_t)left_x;  P_VIC(7u) = bottom_y;
    P_VIC(8u) = (uint8_t)right_x; P_VIC(9u) = bottom_y;
    P_VIC(10u) = (uint8_t)left_x; P_VIC(11u) = top_y;

    msb = 0u;
    if (left_x & 0x100u) msb |= 0x2au;
    if (right_x & 0x100u) msb |= 0x14u;
    P_VIC(0x10) = (P_VIC(0x10) & 0xc1u) | msb;
}
#pragma code-name (pop)

#pragma code-name (push, "HIGHCODE")
uint8_t platform_portrait_show(uint8_t portrait_id, uint8_t side) {
    uint16_t left_x;
    uint8_t target_y;
    uint8_t y;
    uint8_t status;

    if (side != PLATFORM_PORTRAIT_LEFT && side != PLATFORM_PORTRAIT_RIGHT) {
        return PLATFORM_ERR_ARGUMENT;
    }

    status = portrait_load_easyflash(portrait_id);
    if (status != PLATFORM_OK) return status;

    env_disable();

    memset(PORTRAIT_BG_DATA, 0xffu, 63u);
    PORTRAIT_BG_DATA[63] = 0u;

    P_SPRITE_POINTERS[1] = (uint8_t)(0x3a40u / 64u);
    P_SPRITE_POINTERS[2] = (uint8_t)(0x3a80u / 64u);
    P_SPRITE_POINTERS[3] = (uint8_t)(0x3ac0u / 64u);
    P_SPRITE_POINTERS[4] = (uint8_t)(0x3b00u / 64u);
    P_SPRITE_POINTERS[5] = (uint8_t)(0x3b40u / 64u);

    P_VIC(0x17) = (P_VIC(0x17) & 0xc1u) | 0x20u; /* Y-expand: bg sprite only */
    P_VIC(0x1b) &= 0xc1u;                        /* priority: all in front */
    P_VIC(0x1c) &= 0xc1u;                        /* hires, no multicolor */
    P_VIC(0x1d) = (P_VIC(0x1d) & 0xc1u) | 0x20u; /* X-expand: bg sprite only */
    P_VIC(0x28) = 1u; /* sprite 1 (top-left) white */
    P_VIC(0x29) = 1u; /* sprite 2 (top-right) white */
    P_VIC(0x2a) = 1u; /* sprite 3 (bottom-left) white */
    P_VIC(0x2b) = 1u; /* sprite 4 (bottom-right) white */
    P_VIC(0x2c) = 0u; /* sprite 5 (background) black */

    left_x = side == PLATFORM_PORTRAIT_LEFT
                 ? PORTRAIT_SCREEN_X + PORTRAIT_MARGIN
                 : PORTRAIT_SCREEN_X + PLATFORM_MAP_CHAR_WIDTH * 8u -
                       PORTRAIT_MARGIN - PLATFORM_PORTRAIT_WIDTH;
    target_y = PORTRAIT_SCREEN_Y + PORTRAIT_MARGIN;
    y = (uint8_t)(PORTRAIT_SCREEN_Y - PLATFORM_PORTRAIT_HEIGHT);
    portrait_position(left_x, y);
    P_VIC(0x15) |= 0x3eu;

    while (y < target_y) {
        platform_wait_frame();
        y = (uint8_t)(target_y - y > PORTRAIT_SLIDE_STEP
                          ? y + PORTRAIT_SLIDE_STEP : target_y);
        portrait_position(left_x, y);
    }
    return PLATFORM_OK;
}
#pragma code-name (pop)

#pragma code-name (push, "UPPERCODE")
void platform_portrait_hide(void) {
    P_VIC(0x15) &= 0xc1u;
    env_enable();
}
#pragma code-name (pop)
