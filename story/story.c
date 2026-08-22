#include "game.h"
#include "story.h"

/*
 * Global, story-specific inventory dispatch. Add type-specific behavior here;
 * the resident engine intentionally knows nothing about usable item types.
 */
uint8_t __fastcall__ story_use_inventory(uint8_t inventory_slot) {
    if (inventory_slot >= GAME_INVENTORY_SLOTS ||
        game_state.inventory[inventory_slot].type == 0u) {
        return GAME_USE_DEFAULT;
    }
    return GAME_USE_DEFAULT;
}
