#ifndef GAME_ROOM_TABLE_H
#define GAME_ROOM_TABLE_H

/*
 * Room dispatch table - one row per compiled room (see
 * tools/compile_room.py and build/rooms/roomNN.c). Included by
 * src/room_dispatch.c. Hand-maintained: there are only a handful of
 * rooms, and each one's *.rc source already needs its own Makefile rule
 * (see the ROOMS list there), so keeping this in sync is a one-line
 * addition alongside that, not worth generating.
 */

#define ROOM_DECLARE(id) \
    void room##id##_enter_room(void); \
    void room##id##_enter_tile(void); \
    uint8_t room##id##_look_at(uint8_t tile_x, uint8_t tile_y); \
    uint8_t room##id##_use_at(uint8_t tile_x, uint8_t tile_y);

ROOM_DECLARE(00)
ROOM_DECLARE(01)
ROOM_DECLARE(02)
ROOM_DECLARE(03)
ROOM_DECLARE(04)
ROOM_DECLARE(05)

#undef ROOM_DECLARE

#define ROOM_ENTRY(id, num) \
    { 0x##id, BANK_ROOM_FIRST + num, room##id##_enter_room, room##id##_enter_tile, \
      room##id##_look_at, room##id##_use_at }

#define ROOM_TABLE_ENTRIES \
    ROOM_ENTRY(00, 0), \
    ROOM_ENTRY(01, 1), \
    ROOM_ENTRY(02, 2), \
    ROOM_ENTRY(03, 3), \
    ROOM_ENTRY(04, 4), \
    ROOM_ENTRY(05, 5)

#endif
