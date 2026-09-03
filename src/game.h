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

/* Resident engine API. Room handlers normally use the room-overlay API below. */
void game_state_init(void);
void game_player_sync_from_platform(void);
/* Move one half-tile; calls enter_tile() only after the hotspot tile changes. */
uint8_t game_player_step(int8_t delta_x, int8_t delta_y);
/* Execute a transition queued by game_transition_request() on a safe frame. */
uint8_t game_process_pending_transition(void);
void game_enter_room(void);
void game_enter_tile(void);
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

/* Runs a compiled standalone cutscene/conversation script
 * (tools/compile_script.py) by its resource ID - 240-255 are reserved for
 * these, since a room's own script claims resource_id == room_id (0-239;
 * see game_room_script_entry() below). See src/script_runtime.c and
 * modules/script.c. */
void game_script_play(uint8_t resource_id);

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
