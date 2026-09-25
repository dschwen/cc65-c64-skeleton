#include "game.h"
#include "story.h"

/*
 * Global, story-specific inventory dispatch. Add type-specific behavior here;
 * the resident engine intentionally knows nothing about usable item types.
 *
 * The Moon Brooch (type 55) toggles STORY_STATE_MOON_BROOCH_WORN - not
 * consumed, just worn or not - MOONFEN_STORY_DESIGN.md section 5:
 * wearing it is what's needed (alongside the Silver Shard) to break the
 * moon mirror's spell (rooms/c/04_use_at.c). game_text_write() writes to
 * rows 23-24, the same physical rows modules/inventory.c's own message
 * row uses (its INVENTORY_MESSAGE_ROW is 23, and both ultimately target
 * $F800/$D800 - fixed VIC addresses, not bank-dependent memory, so this
 * resident call is safe to make from here even though it's invoked from
 * within BANK_INVENTORY's own code).
 */
uint8_t story_use_inventory(uint8_t inventory_slot) {
    if (inventory_slot >= GAME_INVENTORY_SLOTS ||
        game_state.inventory[inventory_slot].type == 0u) {
        return GAME_USE_DEFAULT;
    }
    if (game_state.inventory[inventory_slot].type == 55u) { /* Moon Brooch */
        game_state.flags[23] ^= 1u; /* STORY_STATE_MOON_BROOCH_WORN */
        game_text_write(PLATFORM_TEXT_LINE_TOP,
                        game_state.flags[23]
                            ? "You pin the moon brooch to your collar."
                            : "You unpin the moon brooch.",
                        1u);
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}
