#include <stdint.h>
#include <string.h>

#include "platform.h"

/* Independently linked look-helpers overlay ("LH"): the bodies of
 * platform_look_tile(), platform_look_tile_check(), platform_object_take_
 * prompt(), and platform_object_taken_message(), moved out of the always-
 * resident engine (src/platform.c), along with their private look_append
 * and look_write text-buffer helpers and supporting statics - none of
 * these are used anywhere else. All four are confirmed called only on a
 * discrete confirm action (Look/Take's Enter key in src/main.c, the Take
 * handler in src/game_support.c), never per-keypress during cursor
 * movement and never from a room's own banked-in code, so there is no risk
 * of a load stall on a hot path and no risk of this overlay's load
 * overwriting code that is still executing.
 *
 * Not here: platform_look_exit() (src/platform.c) - it calls
 * platform_room_neighbor(), itself an overlay in this same $A4E9 window
 * (modules/room_helpers.c), so it must stay resident to call that overlay
 * sequentially without overwriting its own still-executing code. It no
 * longer shares the look_buffer/look_append machinery either - its two
 * messages are fixed literals, written directly.
 *
 * Entry/parameters: the resident wrappers in src/platform.c stage their
 * arguments into look_helpers_* globals and set look_helpers_op before
 * loading and running this overlay (there is only one native entry point,
 * look_helpers_overlay_run(), since overlay code is always called
 * parameterless - see platform_overlay_run_native()); the result goes back
 * out through look_helpers_result.
 */

#define LOOK_HELPERS_OP_TILE         0u
#define LOOK_HELPERS_OP_TILE_CHECK   1u
#define LOOK_HELPERS_OP_TAKE_PROMPT  2u
#define LOOK_HELPERS_OP_TAKEN_MSG    3u

extern const PlatformRoom* look_helpers_room;
extern const PlatformObject* look_helpers_viewer;
extern uint8_t look_helpers_tile_x;
extern uint8_t look_helpers_tile_y;
extern uint8_t look_helpers_color;
extern uint8_t look_helpers_type_id;
extern uint8_t look_helpers_op;
extern uint8_t look_helpers_result;

extern const PlatformRoom* rendered_room;
extern uint16_t rendered_object_limit;
extern uint8_t platform_brightness[PLATFORM_MAP_TILE_COUNT];
extern uint8_t platform_view_tiles[PLATFORM_MAP_TILE_COUNT];

extern uint8_t platform_object_intersects_tile(const PlatformObject* object,
                                               uint8_t tile_x, uint8_t tile_y);
extern const PlatformObjectTypeInfo* platform_object_type_info_get(uint8_t type_id);
extern void __fastcall__ platform_text_output_native(const char* text);
extern uint8_t platform_text_output_color;
extern uint8_t platform_text_output_line;

static const uint8_t look_range_by_light[PLATFORM_LIGHT_LEVEL_COUNT] = {
    0, 2, 6, 0xff
};
static const char take_prompt_prefix[] = "Take: ";
static const char take_prompt_arrows[] = "   < >";
static const char taken_suffix[] = " taken.";

static uint8_t look_counts[PLATFORM_OBJECT_TYPE_COUNT];
static char look_buffer[81];
static uint8_t look_length;
static uint8_t look_truncated;

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
    const PlatformObjectTypeInfo* info;
    uint8_t i;
    info = platform_object_type_info_get(type_id);
    for (i = 0u; i < sizeof(info->name) && info->name[i] != '\0'; ++i) {
        look_append_char(info->name[i]);
    }
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

static void look_write_message(const char* text, uint8_t color) {
    look_length = 0u;
    look_truncated = 0u;
    look_append_string(text);
    look_write_buffer(color);
}

static uint8_t look_helpers_tile_check(void) {
    const PlatformRoom* room;
    const PlatformObject* viewer;
    uint8_t tile_x;
    uint8_t tile_y;
    uint8_t color;
    uint16_t offset;
    uint8_t light;
    uint8_t distance_x;
    uint8_t distance_y;
    uint8_t distance;

    room = look_helpers_room;
    viewer = look_helpers_viewer;
    tile_x = look_helpers_tile_x;
    tile_y = look_helpers_tile_y;
    color = look_helpers_color;

    if (room == 0 || room != rendered_room || viewer == 0 ||
        tile_x >= PLATFORM_MAP_WIDTH || tile_y >= PLATFORM_MAP_HEIGHT) {
        return PLATFORM_ERR_ARGUMENT;
    }
    offset = (uint16_t)tile_y * PLATFORM_MAP_WIDTH + tile_x;
    if (platform_view_tiles[offset] == 0u) {
        look_write_message("I cannot see that.", color);
        return PLATFORM_ERR_BLOCKED;
    }

    light = platform_brightness[offset] & 0x03u;

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

static uint8_t look_helpers_tile(void) {
    const PlatformRoom* room;
    uint8_t tile_x;
    uint8_t tile_y;
    uint8_t color;
    uint16_t i;
    uint16_t limit;
    uint8_t type_id;
    uint8_t count;
    uint8_t found;

    room = look_helpers_room;
    tile_x = look_helpers_tile_x;
    tile_y = look_helpers_tile_y;
    color = look_helpers_color;

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

static uint8_t look_helpers_take_prompt(void) {
    look_length = 0u;
    look_truncated = 0u;
    look_append_string(take_prompt_prefix);
    look_append_type_name(look_helpers_type_id);
    look_append_string(take_prompt_arrows);
    look_write_buffer(look_helpers_color);
    return PLATFORM_OK;
}

static uint8_t look_helpers_taken_msg(void) {
    look_length = 0u;
    look_truncated = 0u;
    look_append_type_name(look_helpers_type_id);
    look_append_string(taken_suffix);
    look_write_buffer(look_helpers_color);
    return PLATFORM_OK;
}

void look_helpers_overlay_run(void) {
    switch (look_helpers_op) {
        case LOOK_HELPERS_OP_TILE_CHECK:
            look_helpers_result = look_helpers_tile_check();
            break;
        case LOOK_HELPERS_OP_TAKE_PROMPT:
            look_helpers_result = look_helpers_take_prompt();
            break;
        case LOOK_HELPERS_OP_TAKEN_MSG:
            look_helpers_result = look_helpers_taken_msg();
            break;
        default:
            look_helpers_result = look_helpers_tile();
            break;
    }
}
