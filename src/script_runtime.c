#include <stdint.h>

#include "game.h"
#include "platform.h"
#include "memmap.h"
#include "banks.h"
#include "script_abi.h"
#include "script_format.h"

/*
 * Native (oscar64 C) resident trigger for the banked script/conversation/
 * room interpreter (modules/script.c, bank BANK_SCRIPT - see src/banks.h).
 * Replaces the cc65-era design's shared-bank ROML placement and its
 * cfg/easyflash_layout.json coupling: under oscar64 the interpreter is an
 * ordinary far_call_bank() target like every other banked service (see
 * memmap.h's far_call_bank() comment).
 *
 * Deliberately thin: every call this used to make around far_call_bank()
 * (raster IRQ resume, portrait cleanup, the post-run keypress wait, status-
 * line clearing) now lives in modules/script.c's script_banked_run()
 * instead, in the bank rather than here - see that function's comment for
 * why. This file just sets up the query state (script_resource_kind/_id,
 * and for a room entry, script_entry_key) and triggers the bank.
 *
 * Script/conversation/room *content* (the compiled bytecode - see
 * tools/compile_script.py) is separate from the interpreter's own code: it
 * is fetched by the interpreter at run time from src/resources.c's
 * platform_resource_fetch_range(), one shared EasyFlash bank per resource
 * kind (see BANK_SCRIPT_ROOM_FIRST).
 */

/* Set by the caller before triggering the interpreter: which resource kind
 * and ID it should fetch and run (script_resource_kind selects which of
 * the PLATFORM_RESOURCE_KIND_* directories script_resource_id is looked up
 * in), and - for a KIND_ROOM resource only - which entry key
 * game_room_script_entry() is asking for. The interpreter looks the key up
 * in the entry table itself (see modules/script.c's find_room_entry()) and
 * reports back through script_entry_found, since the resident side doesn't
 * pre-scan it - a banked-call-per-lookup isn't a hot path (room text is
 * only ever read on a deliberate player action, never on every tile step).
 *
 * Plain resident globals now, not cc65's "ROOMSTAGE"/BSSRAM segment
 * juggling - oscar64's resident "main" region (memmap.h) has no equivalent
 * byte-for-byte scarcity forcing state to be squeezed into another
 * struct's unused tail. */
uint8_t script_resource_kind;
ScriptWorkspace script_workspace;
uint8_t script_resource_id;
uint8_t script_entry_key;
uint8_t script_entry_found;
/* Set by modules/script.c's say() (the OP_TEXT handler) whenever it writes
 * room/standalone-script text - not for a conversation's own topic text,
 * which manages its own pacing. Read by script_banked_run() itself now
 * (see its comment), not from here. */
uint8_t script_text_shown;
/* Set by the CONVERSE opcode: run conversation `script_conversation_id`
 * once the current script has finished (see modules/script.c's
 * script_banked_run()). Deferred because a conversation shares the one
 * script buffer with whatever started it. */
uint8_t script_conversation_pending;
uint8_t script_conversation_id;

void script_banked_run(void);

void game_script_play(uint8_t resource_id) {
    script_resource_kind = PLATFORM_RESOURCE_KIND_SCRIPT;
    script_resource_id = resource_id;
    far_call_bank(BANK_SCRIPT, script_banked_run);
}

void game_conversation_play(uint8_t resource_id) {
    script_resource_kind = PLATFORM_RESOURCE_KIND_CONVERSATION;
    script_resource_id = resource_id;
    far_call_bank(BANK_SCRIPT, script_banked_run);
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
    far_call_bank(BANK_SCRIPT, script_banked_run);
    return script_entry_found;
}
