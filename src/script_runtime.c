#include <stdint.h>

#include "easyflash_layout.h"
#include "game.h"
#include "script_format.h"

/* Resident trigger for the script/conversation/room interpreter overlay
 * (modules/script.c). Every bank through 48 is already spoken for, so like
 * room-helpers this shares TYPE_BANK_1's ROML half (with the object-type
 * Zone C table and the room-helpers overlay) at a fixed offset past both.
 * The packer and runtime both consume cfg/easyflash_layout.json.
 *
 * Script/conversation/room *content* (the compiled bytecode - see
 * tools/compile_script.py) is separate from this overlay's own code: it's
 * fetched by the overlay itself, at run time, from the generic sparse
 * resource directories - one independent 0-255 ID space per kind (see
 * PLATFORM_RESOURCE_KIND_* in platform.h), not a single space split by
 * range. See modules/script.c.
 */
void platform_overlay_run_native(void);
uint8_t __fastcall__ platform_overlay_load(uint8_t bank, uint8_t use_romh,
                                           uint16_t offset, uint8_t magic0,
                                           uint8_t magic1);
void raster_irq_resume(void);
extern volatile uint8_t platform_raster_irq_active;

/* Set by the caller before triggering the overlay: which resource kind and
 * ID it should fetch and run (script_resource_kind selects which of the
 * three PLATFORM_RESOURCE_KIND_* directories script_resource_id is looked
 * up in), and - for a KIND_ROOM resource only - which entry key
 * game_room_script_entry() is asking for. The overlay looks the key up in
 * the entry table itself (see modules/script.c's find_room_entry()) and
 * reports back through script_entry_found, since the resident side doesn't
 * pre-scan it - resident memory here is fully saturated (see MEMORY_MAP.md;
 * this project routinely fights single-byte overflows), and per measured
 * usage (room text is only ever read on a deliberate player action, never
 * on every tile step) an overlay-load-per-lookup isn't a hot path today. If
 * tile-entry narration becomes common enough that this matters, a resident
 * pre-scan (skip the overlay load entirely when no entry matches) is the
 * place to revisit - it would need its own resident memory budget.
 *
 * script_resource_kind is placed in ROOMSTAGE, not the default BSS segment
 * - BSSRAM has no spare bytes (see MEMORY_MAP.md), while ROOMSTAGE's
 * region is still sized for the pre-room-script-removal PlatformRoom
 * (1,257 bytes) against the struct's current 1,001, leaving 256 bytes of
 * already-reserved, otherwise-unclaimed address space room_stage doesn't
 * use - see platform.c's room_stage. */
#pragma bss-name (push, "ROOMSTAGE")
uint8_t script_resource_kind;
#pragma bss-name (pop)
uint8_t script_resource_id;
uint8_t script_entry_key;
uint8_t script_entry_found;
/* Set by modules/script.c's say() (the OP_TEXT handler) whenever it writes
 * room/standalone-script text - not for a conversation's own topic text,
 * which manages its own pacing (see run_loaded_overlay()'s comment below).
 * Read once, right after platform_overlay_run_native() returns. */
uint8_t script_text_shown;

#pragma code-name (push, "LOWCODE")
static void run_loaded_overlay(void) {
    uint8_t result;

    platform_look_cursor_hide();
    script_text_shown = 0u;
    /* Keep the existing load presentation conservative. The bank wrapper now
     * restores the caller's interrupt state while copying, so the raster split
     * continues across this multi-frame load; blanking still avoids exposing
     * an intermediate UI state and can be reevaluated visually later. */
    platform_screen_blank();
    result = platform_overlay_load(EF_LAYOUT_SCRIPT_BANK,
                                   EF_LAYOUT_SCRIPT_USE_ROMH,
                                   EF_LAYOUT_SCRIPT_OFFSET,
                                   EF_LAYOUT_SCRIPT_MAGIC_0,
                                   EF_LAYOUT_SCRIPT_MAGIC_1);
    platform_screen_unblank();
    /* If we got here with the raster IRQ still suspended, the caller is a
     * room's enter_room()/enter_tile() hook, itself called from inside
     * game_process_pending_transition()'s whole-switch suspend bracket
     * (src/game.c) - the only way to reach a room's own code at all. From
     * this point on, platform_overlay_run_native() may write real, readable
     * text to the status rows (a room's arrival/tile-entry narration), and
     * that needs the ordinary per-frame map/text charset split actually
     * running to render as text instead of tile-charset garbage - suspend
     * pins the display on one static charset (tile, in normal gameplay)
     * for its whole duration, precisely because nothing was expected to
     * need the split *during* a transition before this. Resuming here is
     * safe despite still being mid-transition: platform_room_enter()'s own
     * EasyFlash work, and the environment module's checksum-race-sensitive
     * fetch (see game_enter_room()'s comment in src/game.c), are both
     * already fully complete by the time any room-entry/tile-entry hook
     * runs at all. Deliberately not re-suspending afterward: the pager's
     * own between-pages wait and this function's own game_wait_fresh_key()
     * below both need the split alive too, for exactly the same reason, for
     * as long as text might still be on screen; game_process_pending_
     * transition()'s own raster_irq_resume() call, later, simply becomes a
     * harmless no-op once this has already run. Found live: room 01's
     * arrival narration was unreadable (tile-charset garbage) the entire
     * time it was shown, precisely because it runs from here while still
     * suspended. */
    if (!platform_raster_irq_active) raster_irq_resume();
    if (result == PLATFORM_OK) platform_overlay_run_native();

    /* The overlay borrowed the room-staging scratch buffer (see
     * platform_room_scratch() in platform.h) - nothing else reads it
     * between calls, so it's left as-is - and any portrait it showed;
     * put that back the way normal gameplay expects to find it. */
    platform_portrait_hide();
    /* Give the player a chance to read room/standalone-script text before
     * wiping it - previously this cleared and redrew unconditionally and
     * immediately (no platform_wait_frame() in between), so a Look/Use
     * description's last page was up for, at most, a handful of CPU cycles
     * before being erased - visually "flashes and vanishes", reported live.
     * Every earlier page already got this same pause for free (src/text.s's
     * next_text_line() calls wait_for_fresh_key() between pages); only the
     * final page - the one nothing else waits for - was being skipped.
     * Gated on script_text_shown so this adds no new keypress where there
     * is nothing to protect: a conversation manages its own pacing (each
     * loop iteration's "Ask about..." prompt already overwrites the
     * previous topic's answer only once the player has typed something, and
     * exiting via RUN/STOP is itself the acknowledgment - an extra wait
     * here would demand a second, redundant keypress after that exit), and
     * a script that only ran effects/a transition (e.g. the lightning/
     * room_transition_here mystery sequence) never sets the flag. Also
     * skipped when a transition message is pending: that path already has
     * its own, better wait in game_transition_reveal(), called once the
     * queued transition actually runs - waiting twice would demand two
     * keypresses for one message. */
    if (script_text_shown && !game_transition_pending_message) {
        game_wait_fresh_key();
    }
    platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
    platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
    /* Not platform_room_draw(): the map itself was never touched by this
     * overlay (it only ever writes the two status rows, cleared above, plus
     * - for a conversation - sprites, already handled by
     * platform_portrait_hide()), so a full clear-and-redraw is pure visible
     * cost for no visible benefit - the "before" black flash on entry has an
     * honest reason (see this function's earlier comment); this one didn't.
     * platform_lighting_repair() is not a no-op either, though:
     * platform_base_colors/platform_brightness live in WORKBSS, the same
     * `$B000` memory this overlay's own code just occupied, so both arrays are
     * now holding the overlay's leftover bytes, not real lighting/color data
     * (the same hazard fixed for Take/Look's darkness check this session -
     * see MEMORY_MAP_TARGET.md's "Color RAM corruption" section). Color RAM
     * itself is still fine right now (a separate address range the overlay
     * never touched), so this repairs both caches - silently, with no
     * visible clear or flash, see its own comment in src/platform.c for why
     * - before anything else reads them. */
    platform_lighting_repair();
}

void game_script_play(uint8_t resource_id) {
    script_resource_kind = PLATFORM_RESOURCE_KIND_SCRIPT;
    script_resource_id = resource_id;
    run_loaded_overlay();
}

void game_conversation_play(uint8_t resource_id) {
    script_resource_kind = PLATFORM_RESOURCE_KIND_CONVERSATION;
    script_resource_id = resource_id;
    run_loaded_overlay();
}

/* Look up `key` in the current room's own script resource (resource ID ==
 * platform_current_room) and, only if a matching entry exists, run it.
 * Returns 1 if an entry was found and run, 0 otherwise (a normal outcome -
 * most Look/Use/tile-entry hooks won't have a matching entry). */
uint8_t game_room_script_entry(uint8_t key) {
    script_resource_kind = PLATFORM_RESOURCE_KIND_ROOM;
    script_resource_id = platform_current_room;
    script_entry_key = key;
    script_entry_found = 0u;
    run_loaded_overlay();
    return script_entry_found;
}
#pragma code-name (pop)
