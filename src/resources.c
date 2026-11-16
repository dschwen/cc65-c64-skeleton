#include <stdint.h>
#include "memmap.h"
#include "banks.h"
#include "platform.h"

/*
 * Generic resource fetch (see src/platform.h's PLATFORM_RESOURCE_KIND_*
 * comment). Replaces the cc65-era design's shared ROML/ROMH directories
 * with checksums (a packer tool deleted in the oscar64 migration): under
 * oscar64 every resource is compile-time #embed'd into one shared
 * EasyFlash bank per kind (see tools/generate_script_bank.py), so there is
 * nothing to look up or verify at runtime beyond "which array" - the
 * embedded data always matches whatever the cartridge itself contains, by
 * construction, same reasoning as src/object_types_data.c's header
 * comment. One shared bank (rather than one bank per resource) keeps the
 * resident cost here to a single far_call_bank() call site regardless of
 * how many resources exist - the `switch` that actually picks a resource
 * lives inside the banked fetch function, where it costs nothing against
 * the tight resident budget (src/memmap.h).
 *
 * PLATFORM_RESOURCE_KIND_ROOM (room/tile-entry script bytecode),
 * _CONVERSATION and _PORTRAIT each have their own shared bank -
 * BANK_SCRIPT_ROOM_FIRST, BANK_SCRIPT_CONVERSATION_FIRST and
 * BANK_PORTRAIT_FIRST (src/banks.h) - because each kind has an independent
 * 0-255 ID space (a conversation, a room and a portrait may all share a
 * numeric ID); one switch-on-kind here picks which bank and fetch function
 * to call, same shape as any one bank's own internal switch-on-ID.
 * Environment modules and static assets don't go through here at all
 * (src/game.c's game_enter_room(); src/assets_data.c/
 * src/object_types_data.c's compile-time embeds).
 *
 * script_room_bank_fetch/script_conversation_bank_fetch/portrait_bank_fetch
 * (the only functions this file ever calls through far_call_bank()) are
 * named literally at their call sites, not read from a table - confirmed
 * live elsewhere this session (see memmap.h's far_call_bank() comment): an
 * *indirect* call made while a non-default EasyFlash bank is paged in
 * corrupts execution.
 */

uint8_t script_fetch_query_kind;
uint8_t script_fetch_query_id;
uint16_t script_fetch_query_start;
uint8_t* script_fetch_query_dest;
uint16_t script_fetch_query_capacity;
uint16_t script_fetch_result_length;
uint16_t script_fetch_copied_length;

void script_room_bank_fetch(void);
void script_conversation_bank_fetch(void);
void portrait_bank_fetch(void);

uint16_t platform_resource_fetch_range(uint8_t kind, uint8_t resource_id,
                                       uint16_t start, uint8_t* destination,
                                       uint16_t capacity) {
    script_fetch_query_kind = kind;
    script_fetch_query_id = resource_id;
    script_fetch_query_start = start;
    script_fetch_query_dest = destination;
    script_fetch_query_capacity = capacity;
    if (kind == PLATFORM_RESOURCE_KIND_ROOM) {
        far_call_bank(BANK_SCRIPT_ROOM_FIRST, script_room_bank_fetch);
    } else if (kind == PLATFORM_RESOURCE_KIND_CONVERSATION) {
        far_call_bank(BANK_SCRIPT_CONVERSATION_FIRST, script_conversation_bank_fetch);
    } else if (kind == PLATFORM_RESOURCE_KIND_PORTRAIT) {
        far_call_bank(BANK_PORTRAIT_FIRST, portrait_bank_fetch);
    } else {
        script_fetch_result_length = 0u;
        return 0u;
    }
    return script_fetch_copied_length;
}

uint16_t platform_resource_fetch(uint8_t kind, uint8_t resource_id,
                                 uint8_t* destination, uint16_t capacity) {
    uint16_t copied = platform_resource_fetch_range(kind, resource_id, 0u,
                                                     destination, capacity);
    if (script_fetch_result_length > capacity) return 0u; /* didn't fit */
    return copied;
}

uint16_t platform_resource_last_size(void) {
    return script_fetch_result_length;
}
