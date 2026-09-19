#include <stdint.h>
#include "platform.h"
#include "platform_native.h"
#include "game.h"

/*
 * Native (oscar64 C) replacement for src/hero_light.s. The original was
 * hand-written only because cc65 insisted on pushing its single uint8_t
 * argument onto the software stack for what should be a 3-byte store, and
 * every fixed code region was at 100% capacity - neither constraint
 * applies under oscar64. See platform.h's comment on the resident Zone A
 * object-type table for the offset-69 rationale this now avoids entirely
 * by indexing the real array.
 */

#define LIT_TAPER_TYPE 53u

/* Overwrite object type 1's ("The Hero") own light radius in the resident
 * Zone A table directly. Does not itself trigger a lighting rebuild - the
 * caller must call platform_lighting_rebuild() afterward. */
void platform_hero_light_set(uint8_t light) {
    object_types_a[1].light = light;
}

/* Re-derive the Hero's light radius from the current inventory - needed
 * anywhere the resident type table could be stale relative to inventory
 * without the ignite path having run in this session (currently just the
 * save/load restore, right after it overwrites game_state.inventory). */
void game_player_light_sync(void) {
    platform_hero_light_set(game_inventory_has_one(LIT_TAPER_TYPE) ? 6u : 0u);
}
