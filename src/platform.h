#ifndef GAME_PLATFORM_H
#define GAME_PLATFORM_H

#include <stdint.h>

#define PLATFORM_MAP_WIDTH            20u
#define PLATFORM_MAP_HEIGHT           11u
#define PLATFORM_MAP_TILE_COUNT       220u
#define PLATFORM_MAP_CHAR_WIDTH       40u
#define PLATFORM_MAP_CHAR_HEIGHT      22u
#define PLATFORM_ROOM_OBJECT_COUNT    256u
#define PLATFORM_ROOM_OBJECT_BYTES    768u
#define PLATFORM_ROOM_TEXT_BYTES      256u
#define PLATFORM_ROOM_FILE_BYTES      1257u
#define PLATFORM_ROOM_FORMAT          3u
#define PLATFORM_OBJECT_TYPE_COUNT    256u
/* objects.cobj authored/file record size (see tools/asset-editor/README.md).
 * The resident PlatformObjectType struct is smaller; see its comment below. */
#define PLATFORM_OBJECT_TYPE_BYTES    64u
#define PLATFORM_OBJECT_CELL_COUNT    16u
#define PLATFORM_NON_ACTOR_LIMIT      200u

#define PLATFORM_PORTRAIT_BYTES       256u
#define PLATFORM_PORTRAIT_WIDTH       48u
#define PLATFORM_PORTRAIT_HEIGHT      42u
#define PLATFORM_PORTRAIT_LEFT        0u
#define PLATFORM_PORTRAIT_RIGHT       1u

#define PLATFORM_TEXT_LINE_TOP        0u
#define PLATFORM_TEXT_LINE_BOTTOM     1u

/* A PC/NPC actor; also gates conversation once a talk verb exists. Actors
 * are never takeable regardless of PLATFORM_OBJECT_FLAG_NOT_TAKEABLE. */
#define PLATFORM_OBJECT_FLAG_ACTOR    0x01u
/* Excludes a non-actor object from Take: it never appears in the tile's
 * take-candidate list, and game_take_object() rejects it defensively. */
#define PLATFORM_OBJECT_FLAG_NOT_TAKEABLE 0x02u
#define PLATFORM_TILE_BLOCKS_VIEW     0x02u
#define PLATFORM_TILE_SOLID_LAND      0x04u

#define PLATFORM_LIGHT_NONE           0u
#define PLATFORM_LIGHT_DIM            1u
#define PLATFORM_LIGHT_TWILIGHT       2u
#define PLATFORM_LIGHT_FULL           3u
#define PLATFORM_LIGHT_LEVEL_COUNT    4u
#define PLATFORM_LIGHT_MAX_RADIUS     16u

#define PLATFORM_KEY_CURSOR_DOWN      17u
#define PLATFORM_KEY_CURSOR_RIGHT     29u
#define PLATFORM_KEY_CURSOR_UP        145u
#define PLATFORM_KEY_CURSOR_LEFT      157u
#define PLATFORM_KEY_LIGHT_DOWN       45u
#define PLATFORM_KEY_LIGHT_UP         43u
#define PLATFORM_KEY_LIGHTNING        70u
#define PLATFORM_KEY_LOOK             76u
#define PLATFORM_KEY_TAKE             84u
#define PLATFORM_KEY_USE              85u
#define PLATFORM_KEY_INVENTORY        73u
#define PLATFORM_KEY_ENTER            13u

#define PLATFORM_OK                   0u
#define PLATFORM_ERR_IO               1u
#define PLATFORM_ERR_FORMAT           2u
#define PLATFORM_ERR_FULL             3u
#define PLATFORM_ERR_LIMIT            4u
#define PLATFORM_ERR_ARGUMENT         5u
#define PLATFORM_ERR_BLOCKED          6u
#define PLATFORM_ERR_NOT_FOUND        7u

#define PLATFORM_TRANSITION_NONE      0u
#define PLATFORM_TRANSITION_TOP       1u
#define PLATFORM_TRANSITION_LEFT      2u
#define PLATFORM_TRANSITION_BOTTOM    3u
#define PLATFORM_TRANSITION_RIGHT     4u
#define PLATFORM_TRANSITION_TRIGGER   5u

#define PLATFORM_DIRECTION_NORTH      0u
#define PLATFORM_DIRECTION_EAST       1u
#define PLATFORM_DIRECTION_WEST       2u
#define PLATFORM_DIRECTION_SOUTH      3u

#define PLATFORM_ROOM_EXIT_NORTH      0x01u
#define PLATFORM_ROOM_EXIT_EAST       0x02u
#define PLATFORM_ROOM_EXIT_WEST       0x04u
#define PLATFORM_ROOM_EXIT_SOUTH      0x08u

/*
 * A room object occupies exactly three bytes in a room file. Coordinates are
 * in half-tile units, which are also screen character cells. x/y locate the
 * object type's hotspot, not the graphic's top-left corner. Type 0 is empty.
 */
typedef struct PlatformObject {
    uint8_t type;
    uint8_t x;
    uint8_t y;
} PlatformObject;

/*
 * The 64-byte objects.cobj/web-editor record is split at build time
 * (tools/pack_easyflash.py) into two EasyFlash-resident pieces so only the
 * fields the render/collision path needs stay in scarce resident RAM:
 *
 * PlatformObjectType (35 bytes) is fully resident for all 256 IDs.
 * dimensions: high nibble width, low nibble height, in character cells.
 * hotspot:    high nibble x, low nibble y, relative to the graphic origin.
 * chars:      row-major screen character codes; 0 is transparent.
 * colors:     row-major C64 colors corresponding to chars.
 * light:      emitted light; 0 means the object emits no light.
 * Width * height must be <= PLATFORM_OBJECT_CELL_COUNT.
 *
 * PlatformObjectTypeInfo (15 bytes) stays on the cartridge and is fetched on
 * demand with platform_object_type_info_get() only where it is actually
 * needed (Take/Use/Look text, actor-flag checks) -- never on the per-frame
 * render/collision path. name is ASCII in the editor, PETSCII after the
 * build-prepare step. flags holds PLATFORM_OBJECT_FLAG_* bits.
 *
 * The file's remaining 14 bytes per record are unused padding and are
 * dropped entirely rather than carried into either resident form.
 */
typedef struct PlatformObjectType {
    uint8_t dimensions;
    uint8_t hotspot;
    uint8_t chars[PLATFORM_OBJECT_CELL_COUNT];
    uint8_t colors[PLATFORM_OBJECT_CELL_COUNT];
    uint8_t light;
} PlatformObjectType;

typedef struct PlatformObjectTypeInfo {
    char name[14];
    uint8_t flags;
} PlatformObjectTypeInfo;

/*
 * Exact 1,257-byte room file. Files are named 00 through FF.
 * width/height must be 20/11. format is currently 3. exit_mask indicates
 * which of the four neighbor bytes are valid, since every byte value is a
 * usable room ID.
 * text is a pool of zero-terminated strings addressed by byte offset.
 */
typedef struct PlatformRoom {
    uint8_t width;
    uint8_t height;
    uint8_t id;
    uint8_t format;
    uint8_t exit_mask;
    uint8_t north;
    uint8_t east;
    uint8_t west;
    uint8_t south;
    uint8_t north_text;
    uint8_t east_text;
    uint8_t west_text;
    uint8_t south_text;
    uint8_t tiles[PLATFORM_MAP_TILE_COUNT];
    PlatformObject objects[PLATFORM_ROOM_OBJECT_COUNT];
    uint8_t text[PLATFORM_ROOM_TEXT_BYTES];
} PlatformRoom;

typedef uint8_t (*PlatformRoomStoreHook)(const PlatformRoom* room);
typedef uint8_t (*PlatformRoomRestoreHook)(PlatformRoom* room);

extern PlatformRoom platform_room;
/* Current room ID and player ownership inside platform_room.objects. */
extern uint8_t platform_current_room;
extern uint8_t platform_player_slot;
extern PlatformObject* platform_player;
extern volatile uint8_t platform_frame_counter;
/* Unmodified character colors and one brightness level per 2x2 tile. */
extern uint8_t platform_base_colors[PLATFORM_MAP_CHAR_WIDTH * PLATFORM_MAP_CHAR_HEIGHT];
extern uint8_t platform_brightness[PLATFORM_MAP_TILE_COUNT];
extern uint8_t platform_global_light;
extern uint8_t platform_view_tiles[PLATFORM_MAP_TILE_COUNT];
extern const uint8_t platform_light_colors[PLATFORM_LIGHT_LEVEL_COUNT * 16u];
extern const uint8_t platform_light_distance[16u * 16u];

#define PLATFORM_OBJECT_LIGHT(type) ((type)->light)

/*
 * Initialize VIC/banking state. If the EasyFlash cartridge boot marker is
 * absent, the platform falls back to the room-00/types-0-1 data baked into
 * the PRG rather than loading anything further; disk is not a fallback asset
 * source. The current player is room object slot 0, so platform_player
 * points inside platform_room.objects rather than holding a detached copy.
 */
void platform_init(void);

/* Optional transition hooks; either may reject a transition with an error. */
void platform_room_state_hooks(PlatformRoomStoreHook store_hook,
                               PlatformRoomRestoreHook restore_hook);

/* Reset a room to an empty 20x11 room with text offset 0 as an empty string. */
void platform_room_clear(PlatformRoom* room, uint8_t room_id);

/* Load a fixed-size room from EasyFlash. */
uint8_t platform_room_load(PlatformRoom* room, uint8_t room_id);

/* Resolve one enabled cardinal neighbor; returns PLATFORM_ERR_NOT_FOUND otherwise. */
uint8_t platform_room_neighbor(const PlatformRoom* room, uint8_t direction,
                               uint8_t* room_id);
/* Return a room-text exit description, or NULL for an absent/invalid one. */
const char* platform_room_exit_description(const PlatformRoom* room,
                                           uint8_t direction);

/* Load all 256 fixed-size object types from EasyFlash. */
uint8_t platform_object_types_load(void);

/*
 * Resolve the resident hot record. Most IDs point directly into resident
 * RAM; a middle range is staged from RAM beneath I/O and a high range from
 * RAM beneath KERNAL, each into the same scratch record (see platform.c for
 * the exact ID boundaries). The returned pointer is only valid until the
 * next call.
 */
const PlatformObjectType* platform_object_type_get(uint8_t type_id);

/*
 * Fetch the name/flags cold record directly from EasyFlash. This is a real
 * cartridge bank-switch (heavier than platform_object_type_get()) and must
 * only be called from discrete, human-input-paced code paths (Take/Use/Look
 * text, actor-flag checks before adding/counting objects) -- never per
 * frame or per rendered cell. The returned pointer is only valid until the
 * next call.
 */
const PlatformObjectTypeInfo* platform_object_type_info_get(uint8_t type_id);

/*
 * Atomically replace the resident room while carrying one actor. The
 * destination is staged, restored, collision-checked, and allocated a slot
 * before the current player is removed. Persistent mutations other than the
 * runtime player's original spawn remain the storage/save layer's concern.
 */
uint8_t platform_room_enter(uint8_t room_id, uint8_t actor_type,
                            uint8_t new_x, uint8_t new_y);

/* Draw one 2x2 tile at tile coordinates, clipped to the 20x11 map. */
void platform_map_draw_tile(uint8_t tile, uint8_t tile_x, uint8_t tile_y);

/* Draw all 220 room tiles, without objects. */
void platform_map_draw(const PlatformRoom* room);

/* Draw one object using its hotspot and the registered object-type graphics. */
void platform_object_draw(const PlatformObject* object);

/*
 * Draw tiles, room objects in slot order, then player last. player may point
 * at a room slot; that slot is skipped in the object pass and drawn last.
 * Pass NULL when a player should not be drawn. Slot order is object z-order.
 */
void platform_room_draw(const PlatformRoom* room, const PlatformObject* player);

/*
 * Translate platform_base_colors through platform_brightness into Color RAM.
 * rebuild fills ambient light, max-combines room emitters, and applies it.
 */
void platform_lighting_apply(void);
void platform_lighting_set_global(uint8_t level);
void platform_lighting_rebuild(const PlatformRoom* room,
                               const PlatformObject* player);

/* Flash white, blank map colors, then restore the current lighting in assembly. */
void platform_lightning(void);

/*
 * Move an object and redraw only changed cells in its old/new graphic union.
 * Each affected cell is recomposed from tile, room objects, then player; a
 * screen/color write is skipped when both bytes already match.
 */
void platform_object_move(PlatformRoom* room, PlatformObject* object,
                          uint8_t new_x, uint8_t new_y,
                          const PlatformObject* player);

/*
 * Move the global player one half-tile when the destination hotspot is inside
 * the room and its tile has PLATFORM_TILE_SOLID_LAND. Uses minimal redraw.
 */
uint8_t platform_player_step(int8_t delta_x, int8_t delta_y);

/* Wait for the next bottom-of-map IRQ, then scan/read one keyboard event. */
void platform_wait_frame(void);
uint8_t platform_input_poll(void);

/* Select the text charset for the whole screen, or restore the map split. */
void platform_text_screen_enter(void);
void platform_text_screen_leave(void);

/* Add at the first empty slot. out_slot may be NULL. */
uint8_t platform_room_object_add(PlatformRoom* room, uint8_t type,
                                 uint8_t x, uint8_t y, uint8_t* out_slot);

/* Remove a slot and minimally restore the exposed cells. */
uint8_t platform_room_object_remove(PlatformRoom* room, uint8_t slot,
                                    const PlatformObject* player);

/* Count populated slots; actor_only: 0=all, 1=actors, 2=non-actors. */
uint16_t platform_room_object_count(const PlatformRoom* room,
                                    uint8_t actor_only);

/* One past the highest populated slot index; cached for the rendered room. */
uint16_t platform_room_object_limit(const PlatformRoom* room);

/* Move a room-list record between two loaded rooms using the first free slot. */
uint8_t platform_room_object_transfer(PlatformRoom* leaving,
                                      PlatformRoom* entering,
                                      uint8_t leaving_slot,
                                      uint8_t new_x, uint8_t new_y,
                                      uint8_t* entering_slot);

/*
 * Test a proposed half-tile step for an edge exit or trigger tile. This does
 * not move the object or choose a destination room. For an in-room step,
 * stepped_tile receives the tile number when non-NULL. Destination room IDs
 * and arrival coordinates remain game-defined transition-table data.
 */
uint8_t platform_transition_check(const PlatformRoom* room,
                                  const PlatformObject* object,
                                  int8_t delta_x, int8_t delta_y,
                                  uint8_t* stepped_tile);

/* Convert ASCII to the custom lower-bank screen-code convention. */
uint8_t platform_text_screen_code(char ch);

/* Clear one of the two bottom text lines (line 0=row 23, line 1=row 24). */
void platform_text_clear_line(uint8_t line);

/* Write a zero-terminated string to a bottom line, clipped to 40 columns. */
void platform_text_write_line(uint8_t line, uint8_t column,
                              const char* text, uint8_t color);

/* Write a room-text string selected by its byte offset into room.text. */
void platform_text_write_room_line(const PlatformRoom* room, uint8_t line,
                                   uint8_t column, uint8_t text_offset,
                                   uint8_t color);

/*
 * Reject a tile hidden by line of sight or beyond the provisional range for
 * its tile light level. A rejection writes the reason to
 * the bottom pager, so room-specific look hooks must only run after success.
 */
uint8_t platform_look_tile_check(const PlatformRoom* room,
                                 const PlatformObject* viewer,
                                 uint8_t tile_x, uint8_t tile_y,
                                 uint8_t color);

/* Describe an enabled exit after checking visibility/light at its edge tile. */
uint8_t platform_look_exit(const PlatformRoom* room,
                           const PlatformObject* viewer,
                           uint8_t edge_x, uint8_t edge_y,
                           uint8_t direction, uint8_t color);

/* List every object with a nonzero rendered character intersecting a tile. */
uint8_t platform_look_tile(const PlatformRoom* room,
                           uint8_t tile_x, uint8_t tile_y, uint8_t color);

/* True when a nontransparent character from an object overlaps a map tile. */
uint8_t platform_object_intersects_tile(const PlatformObject* object,
                                        uint8_t tile_x, uint8_t tile_y);

/* Display one build-prepared PETSCII object name in the Take selector. */
void platform_object_take_prompt(uint8_t type_id, uint8_t color);
/* Display "<object name> taken.". */
void platform_object_taken_message(uint8_t type_id, uint8_t color);

/* Sprite-0 18x18 tile cursor. Other sprite registers/bits are preserved. */
uint8_t platform_look_cursor_show(uint8_t tile_x, uint8_t tile_y);
uint8_t platform_look_cursor_move(uint8_t tile_x, uint8_t tile_y);
void platform_look_cursor_tick(void);
void platform_look_cursor_hide(void);

/*
 * Fetch portrait_id (from cartridge or disk, matching the active storage
 * backend) into sprites 1-5 and slide it in from the top of the screen to
 * rest 16px from the top and side frame, in the PLATFORM_PORTRAIT_LEFT or
 * PLATFORM_PORTRAIT_RIGHT corner. Sprite 0 (look/take/use cursor) and
 * sprites 6-7 are untouched. Blocks until the slide-in finishes.
 */
uint8_t platform_portrait_show(uint8_t portrait_id, uint8_t side);
/* Hide sprites 1-5 immediately (no animation). */
void platform_portrait_hide(void);

#endif
