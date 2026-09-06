#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdint.h>

#include "platform.h"

#define GAME_INVENTORY_SLOTS 32u
#define GAME_FLAG_BYTES      32u

#define GAME_LOOK_DEFAULT    0u
#define GAME_LOOK_HANDLED    1u
#define GAME_USE_DEFAULT     0u
#define GAME_USE_HANDLED     1u

#define GAME_ENTRY_STARTUP    0u
#define GAME_ENTRY_MOVEMENT   1u
#define GAME_ENTRY_TRANSITION 2u
#define GAME_ENTRY_LOAD       3u

typedef struct GameInventorySlot {
    uint8_t type;
    uint8_t quantity;
} GameInventorySlot;

/* Resident, saveable state. x/y are player-hotspot half-tile coordinates. */
typedef struct GameState {
    uint32_t turn;
    uint16_t day;
    uint8_t hour;
    uint8_t minute;

    uint8_t current_room;
    uint8_t player_type;
    uint8_t player_x;
    uint8_t player_y;

    uint8_t health;
    uint8_t maximum_health;
    uint8_t mana;
    uint8_t maximum_mana;

    uint8_t pending_transition;
    uint8_t pending_room;
    uint8_t pending_x;
    uint8_t pending_y;

    GameInventorySlot inventory[GAME_INVENTORY_SLOTS];
    uint8_t flags[GAME_FLAG_BYTES];
} GameState;

extern GameState game_state;
extern uint8_t game_entry_reason;

/* Set (with the message already shown - see game_transition_show_message()
 * below) when a loading message is up and should keep the screen black
 * through the next transition's whole load, then wait for a keypress before
 * revealing the new room; 0 for today's immediate-reveal behavior. Set by
 * modules/script.c's OP_ROOM_TRANSITION/OP_ROOM_TRANSITION_HERE handling.
 * Transient - not part of GameState, never saved, always cleared after one
 * use (see game_transition_reveal()). */
extern uint8_t game_transition_pending_message;

/* Resident (unlike platform_text_output_native, not a banked overlay - the
 * script interpreter overlay calls this directly on its own windowed string
 * data, the same way it already calls game_transition_request()). Blanks,
 * clears the map area (rows 0-22; game_text_write clears the status rows
 * itself), writes text into the top status row, unblanks, and sets
 * game_transition_pending_message - all synchronously, right when the
 * script opcode that specified the message runs (long before the deferred
 * transition itself is applied). */
void game_transition_show_message(const char* text);

/* Resident engine API. Room handlers normally use the room-overlay API below. */
void game_state_init(void);
void game_player_sync_from_platform(void);
/* Move one half-tile; calls enter_tile() only after the hotspot tile changes. */
uint8_t game_player_step(int8_t delta_x, int8_t delta_y);
/* Execute a transition queued by game_transition_request() on a safe frame. */
uint8_t game_process_pending_transition(void);
void game_enter_room(void);
void game_enter_tile(void);
/* Phase A/C of a message-aware transition bracket - see
 * game_transition_pending_message's own comment. game_transition_message_show()
 * replaces a bare platform_screen_blank() before raster_irq_suspend(): a
 * no-op (today's exact behavior) if no message is pending, since the
 * message (if any) was already shown - see game_transition_show_message() -
 * and should stay up through the whole load rather than being blanked away
 * now. game_transition_reveal() replaces the final platform_room_draw()+
 * platform_screen_unblank() after raster_irq_resume(): waits for a keypress
 * first if a message was shown, then blanks, draws, clears the message out
 * of the status rows, restores the new room's environment module's visual
 * state, and unblanks. */
void game_transition_message_show(void);
void game_transition_reveal(void);
uint8_t __fastcall__ game_look_at(uint8_t tile_x, uint8_t tile_y);
uint8_t __fastcall__ game_use_at(uint8_t tile_x, uint8_t tile_y);
uint8_t game_room_code_prepare(uint8_t room_id);
void game_room_code_activate(void);
uint8_t game_room_code_load_current(void);
extern uint8_t game_room_code_active;

/* Resident inventory API, callable by main and room overlays. Type 0 is invalid. */
uint8_t game_inventory_count(uint8_t type);
uint8_t game_inventory_has(uint8_t type, uint8_t quantity);
uint8_t game_inventory_add(uint8_t type, uint8_t quantity);
uint8_t game_inventory_remove(uint8_t type, uint8_t quantity);

/* Saturating health changes; mana spend returns false without changing mana. */
uint8_t game_health(void);
uint8_t game_mana(void);
void game_heal(uint8_t amount);
void game_damage(uint8_t amount);
uint8_t game_spend_mana(uint8_t amount);

/* Write word-wrapped, paged text in the two bottom lines. */
void game_text_write(uint8_t line, const char* text, uint8_t color);
/* Queue an overlay-safe room change. x/y are half-tile coordinates. */
uint8_t game_transition_request(uint8_t room, uint8_t x, uint8_t y);
/* Add a non-actor room object to inventory and remove it from the room. */
uint8_t game_take_object(uint8_t slot);
/* Select and take a non-actor whose rendered footprint intersects a tile. */
uint8_t game_take_tile(uint8_t tile_x, uint8_t tile_y);
/* Show all inventory slots on a temporary full-screen text display. */
void game_inventory_show(void);

/* Save/load overlay (src/saveload_runtime.c, modules/saveload.c). Opens a
 * temporary full-screen slot-select display; see SAVE_GAME.md. */
#define SAVELOAD_MODE_SAVE 0u
#define SAVELOAD_MODE_LOAD 1u
#define SAVELOAD_SLOT_NONE 0xffu
extern uint8_t saveload_overlay_mode;
/* Hand-off between the two save-flow overlays (SAVELOAD_SLOT_NONE = no
 * slot chosen / cancelled): the "SL" browse overlay sets this and returns
 * when the user picks a slot to save into, then the resident wrapper loads
 * the separate "SV" overlay (modules/saveload_save.c) to do name entry and
 * the actual write. Kept resident because loading a new overlay blob
 * overwrites the previous one's own static variables. */
extern uint8_t saveload_selected_slot;
/* The load overlay sets this after placing a validated record at $A000.
 * Resident code consumes the record only after the overlay has returned;
 * room loading reuses and overwrites the overlay/staging memory. */
extern uint8_t saveload_load_pending;
void game_save_show(void);
void game_load_show(void);

/* Runs a compiled standalone cutscene ("script" declaration -
 * tools/compile_script.py) by its resource ID. Independent ID space from
 * game_conversation_play() and game_room_script_entry() below - each kind
 * has its own full 0-255 range (see PLATFORM_RESOURCE_KIND_* in
 * platform.h). See src/script_runtime.c and modules/script.c. */
void game_script_play(uint8_t resource_id);

/* Runs a compiled standalone conversation ("conversation" declaration) by
 * its resource ID - matches the player's typed input against the
 * conversation's topic keywords. Independent ID space from
 * game_script_play() above. */
void game_conversation_play(uint8_t resource_id);

/* Runs the current room's own script entry keyed `key`, if it has one - see
 * tools/compile_script.py's `room` declaration. Returns 1 if an entry was
 * found and run, 0 otherwise (the common case: no matching entry). Always
 * pays the interpreter overlay's load cost, same as game_script_play() -
 * see src/script_runtime.c's doc comment for why that's fine given current
 * usage. Room code calls this from enter_room(), enter_tile(), look_at(),
 * use_at() wherever it used to read the room's text pool directly; the
 * entry key is whatever numbering convention the room's own script/DSL
 * source picked (e.g. tile index for tile-entry hooks). */
uint8_t game_room_script_entry(uint8_t key);

/* Implemented independently by every rooms/XX.c. Read coordinates from state. */
void enter_room(void);
void enter_tile(void);
uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y);
uint8_t __fastcall__ use_at(uint8_t tile_x, uint8_t tile_y);

#endif
