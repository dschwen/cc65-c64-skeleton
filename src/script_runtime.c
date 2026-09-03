#include <stdint.h>

#include "game.h"
#include "script_format.h"

/* Resident trigger for the script/conversation/room interpreter overlay
 * (modules/script.c). Every bank through 48 is already spoken for, so like
 * room-helpers this shares TYPE_BANK_1's ROML half (with the object-type
 * Zone C table and the room-helpers overlay) at a fixed offset past both -
 * see tools/pack_easyflash.py's SCRIPT_EF_OFFSET.
 *
 * Script/conversation/room *content* (the compiled bytecode - see
 * tools/compile_script.py) is separate from this overlay's own code: it's
 * fetched by the overlay itself, at run time, from the generic sparse
 * resource directory. Standalone scripts/conversations use resource IDs
 * 240-255 (reserved for this); a room's own script uses resource ID ==
 * room ID (0-239) - the same resource a room's text pool used to be. See
 * modules/script.c.
 */
#define SCRIPT_EF_BANK    47u
#define SCRIPT_EF_OFFSET  2048u
#define SCRIPT_MAGIC_0    0x53u /* 'S' */
#define SCRIPT_MAGIC_1    0x43u /* 'C' */

void raster_irq_suspend(void);
void raster_irq_resume(void);
void platform_overlay_run_native(void);
uint8_t __fastcall__ platform_overlay_load(uint8_t bank, uint8_t use_romh,
                                           uint16_t offset, uint8_t magic0,
                                           uint8_t magic1);

/* Set by the caller before triggering the overlay: which resource it should
 * fetch and run, and - for a KIND_ROOM resource only - which entry key
 * game_room_script_entry() is asking for. The overlay looks the key up in
 * the entry table itself (see modules/script.c's find_room_entry()) and
 * reports back through script_entry_found, since the resident side doesn't
 * pre-scan it - resident memory here is fully saturated (see MEMORY_MAP.md;
 * this project routinely fights single-byte overflows), and per measured
 * usage (room text is only ever read on a deliberate player action, never
 * on every tile step) an overlay-load-per-lookup isn't a hot path today. If
 * tile-entry narration becomes common enough that this matters, a resident
 * pre-scan (skip the overlay load entirely when no entry matches) is the
 * place to revisit - it would need its own resident memory budget. */
uint8_t script_resource_id;
uint8_t script_entry_key;
uint8_t script_entry_found;

#pragma code-name (push, "LOWCODE")
static void run_loaded_overlay(void) {
    uint8_t result;

    platform_look_cursor_hide();
    result = platform_overlay_load(SCRIPT_EF_BANK, 0u, SCRIPT_EF_OFFSET,
                                   SCRIPT_MAGIC_0, SCRIPT_MAGIC_1);
    if (result == PLATFORM_OK) platform_overlay_run_native();

    /* The overlay borrowed the room-staging scratch buffer (see
     * platform_room_scratch() in platform.h) - nothing else reads it
     * between calls, so it's left as-is - and any portrait it showed;
     * put that back the way normal gameplay expects to find it. */
    platform_portrait_hide();
    platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
    platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
    raster_irq_suspend();
    platform_room_draw(&platform_room, platform_player);
    raster_irq_resume();
}

void game_script_play(uint8_t resource_id) {
    script_resource_id = resource_id;
    run_loaded_overlay();
}

/* Look up `key` in the current room's own script resource (resource ID ==
 * platform_current_room) and, only if a matching entry exists, run it.
 * Returns 1 if an entry was found and run, 0 otherwise (a normal outcome -
 * most Look/Use/tile-entry hooks won't have a matching entry). */
uint8_t game_room_script_entry(uint8_t key) {
    script_resource_id = platform_current_room;
    script_entry_key = key;
    script_entry_found = 0u;
    run_loaded_overlay();
    return script_entry_found;
}
#pragma code-name (pop)
