#ifndef GAME_PLATFORM_NATIVE_H
#define GAME_PLATFORM_NATIVE_H

/*
 * Internal engine plumbing shared between src/platform.c and the native
 * rendering/redraw files it drives (src/render.c, src/dirty_redraw.c,
 * src/hero_light.c). Not part of the public platform.h API - these are
 * parameter-passing globals and resident engine state, the oscar64-C
 * equivalent of the old .import/.export pairs between platform.c and its
 * hand-written .s helpers.
 */

#include <stdint.h>
#include "platform.h"

/* Shadow of SID voice 3's registers ($D40E-$D414, write-only hardware) as
 * last written by the room's environment module. Environment modules write
 * voice 3 through SID_V3_WRITE() so a script sound effect (modules/
 * script.c) can borrow the voice and then restore exactly what the module
 * had there. Indexed by register offset minus 14. */
extern uint8_t platform_sid_v3[7];
#define SID_V3_WRITE(reg_, value_) do { \
    platform_sid_v3[(reg_) - 14u] = (value_); \
    ((volatile uint8_t*)0xd400u)[reg_] = (value_); \
} while (0)

#define OBJECT_TYPE_ZONE_A_COUNT     106u
#define OBJECT_TYPE_ZONE_B_COUNT     117u
#define OBJECT_TYPE_ZONE_AB_COUNT    (OBJECT_TYPE_ZONE_A_COUNT + OBJECT_TYPE_ZONE_B_COUNT)
#define OBJECT_TYPE_ZONE_C_COUNT     (PLATFORM_OBJECT_TYPE_COUNT - OBJECT_TYPE_ZONE_AB_COUNT)

extern PlatformObjectType object_types_a[OBJECT_TYPE_ZONE_A_COUNT];
extern PlatformObjectType object_types_b[OBJECT_TYPE_ZONE_B_COUNT];

/* Zone C (type IDs 223-255) is cold, banked storage now - see
 * src/object_types_data.c's header comment. platform_object_type_get()
 * (src/platform.c) is the only intended caller of the fetch/cache below. */
extern PlatformObjectType object_type_c_scratch;
extern uint8_t object_type_c_cache_tag;
extern uint8_t object_type_c_query_id;
void object_type_c_fetch(void);
void object_types_b_load(void);
void platform_charsets_load(void);
/* Resident gate; neither buffer may be inside the cartridge window. */
void platform_copy_vic_ram(uint8_t* destination, const uint8_t* source,
                           uint8_t size);

/* platform_map_draw_native()/platform_object_draw_native() (render.c)
 * argument-passing globals - set by platform.c's object_draw_base()
 * before each call, matching the original ABI. */
extern const PlatformObjectType* native_object_type;
extern uint8_t native_object_source;
extern uint8_t native_object_columns;
extern uint8_t native_object_rows;
extern uint8_t native_object_row_skip;
extern uint16_t native_object_screen_offset;

/* platform_light_source_apply_native() (render.c) argument-passing
 * globals - set by platform.c's light_source_apply(). */
extern uint8_t native_light_source_x;
extern uint8_t native_light_source_y;
extern uint8_t native_light_radius;
extern uint8_t native_light_min_x;
extern uint8_t native_light_min_y;
extern uint8_t native_light_columns;
extern uint8_t native_light_rows;
extern uint16_t native_light_screen_offset;
extern uint8_t native_light_visibility_offset;
extern uint8_t platform_light_visibility[PLATFORM_MAP_TILE_COUNT];

/* platform_visibility_build_native() (render.c) argument-passing globals -
 * set by platform.c's visibility_build(). */
extern const PlatformRoom* native_visibility_room;
extern uint8_t* native_visibility_mask;
extern uint8_t native_visibility_origin_x;
extern uint8_t native_visibility_origin_y;
extern uint8_t native_visibility_origin_offset;
extern uint8_t native_visibility_max_ring;

/* The currently on-screen room/player/object-limit - set by
 * redraw_map_and_objects() and kept in sync by every partial-redraw path.
 * dirty_redraw.c's redraw_dirty() reads these to avoid recomputing the
 * object limit on every call. */
extern const PlatformRoom* rendered_room;
extern const PlatformObject* rendered_player;
extern uint16_t rendered_object_limit;

#endif
