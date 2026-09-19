#ifndef GAME_ENV_BANKS_H
#define GAME_ENV_BANKS_H

/*
 * Room ID -> environment module bank lookup (see rooms/env/*.c and
 * src/banks.h's BANK_ENV_* constants). Hand-maintained alongside
 * rooms/room_table.h - only a handful of rooms exist.
 */

static inline uint8_t env_bank_for_room(uint8_t room_id) {
    switch (room_id) {
        case 0x00u: return BANK_ENV_RAIN;
        case 0x01u: return BANK_ENV_RAIN;
        case 0x02u: return BANK_ENV_TAVERN;
        case 0x03u: return BANK_ENV_TAVERN_MUFFLED;
        case 0x04u: return BANK_ENV_CELLAR;
        case 0x05u: return BANK_ENV_TAVERN_MUFFLED;
        default: return 0xffu; /* no environment module for this room */
    }
}

#endif
