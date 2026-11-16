#include <stdint.h>
#include "platform.h"
#include "platform_native.h"
#include "game.h"

/* The player's own light. Object type 1 ("The Hero") is the player's
 * resident Zone A type record; its light byte is rewritten in place and
 * re-derived from the inventory whenever that changes: a carried item
 * lights the player with its own type's light radius (the brightest one
 * wins), so a "lit torch" item needs no engine knowledge beyond its light
 * byte in assets/objects.cobj. */

#define HERO_TYPE 1u

/* Overwrite the Hero's light radius directly. Does not itself trigger a
 * lighting rebuild - the caller must call platform_lighting_rebuild(). */
void platform_hero_light_set(uint8_t light) {
    object_types_a[HERO_TYPE].light = light;
}

uint8_t game_player_light_sync(void) {
    uint8_t i;
    uint8_t best = 0u;
    for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
        uint8_t type = game_state.inventory[i].type;
        if (type != 0u) {
            uint8_t light = PLATFORM_OBJECT_LIGHT(platform_object_type_get(type));
            if (light > best) best = light;
        }
    }
    if (object_types_a[HERO_TYPE].light == best) return 0u;
    platform_hero_light_set(best);
    return 1u;
}
