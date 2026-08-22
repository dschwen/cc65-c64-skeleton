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
void game_text_write_room(uint8_t line, uint8_t text_offset, uint8_t color);
/* Queue an overlay-safe room change. x/y are half-tile coordinates. */
uint8_t game_transition_request(uint8_t room, uint8_t x, uint8_t y);
/* Add a non-actor room object to inventory and remove it from the room. */
uint8_t game_take_object(uint8_t slot);
/* Select and take a non-actor whose rendered footprint intersects a tile. */
uint8_t game_take_tile(uint8_t tile_x, uint8_t tile_y);
/* Show all inventory slots on a temporary full-screen text display. */
void game_inventory_show(void);

/* Implemented independently by every rooms/XX.c. Read coordinates from state. */
void enter_room(void);
void enter_tile(void);
uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y);
uint8_t __fastcall__ use_at(uint8_t tile_x, uint8_t tile_y);

#endif
