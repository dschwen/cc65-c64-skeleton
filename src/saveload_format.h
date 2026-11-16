#ifndef SAVELOAD_FORMAT_H
#define SAVELOAD_FORMAT_H

/* The on-disk save record and slot index (SAVE_GAME.md's "Version 1
 * serialized record" and "Disk backend") - the one definition both
 * save/load banks (modules/saveload.c, modules/saveload_save.c) compile
 * against. The format is explicit bytes, never a C struct image; keep
 * these in step with SAVE_GAME.md when changing the format. */

#define SAVE_SLOT_COUNT      8u
#define SAVE_NAME_BYTES      16u
#define SAVE_HEADER_BYTES    16u
/* name[16] turn[4] day[2] hour minute room type x y health maxhealth mana
 * maxmana inventory[64] flags[32] delta_count[2] */
#define SAVE_PREFIX_BYTES    130u
#define SAVE_DELTA_BYTES     5u
#define SAVE_MAX_DELTAS      200u
#define SAVE_MAX_BYTES       (SAVE_HEADER_BYTES + SAVE_PREFIX_BYTES + \
                              SAVE_MAX_DELTAS * SAVE_DELTA_BYTES)
/* Enough of a record to list it in the browser: header plus the name. */
#define SAVE_PEEK_BYTES      (SAVE_HEADER_BYTES + SAVE_NAME_BYTES)
#define SAVE_FORMAT_VERSION  1u

/* Byte offsets inside the payload (after the header). */
#define SAVE_PAYLOAD_NAME        0u
#define SAVE_PAYLOAD_STATE       16u  /* turn..maximum_mana, 16 bytes */
#define SAVE_PAYLOAD_ROOM        24u
#define SAVE_PAYLOAD_PLAYER_TYPE 25u
#define SAVE_PAYLOAD_PLAYER_X    26u
#define SAVE_PAYLOAD_PLAYER_Y    27u
#define SAVE_PAYLOAD_HEALTH      28u
#define SAVE_PAYLOAD_INVENTORY   32u
#define SAVE_PAYLOAD_FLAGS       96u
#define SAVE_PAYLOAD_DELTA_COUNT 128u

/* Header offsets. */
#define SAVE_HEADER_PAYLOAD_LEN  10u
#define SAVE_HEADER_CHECKSUM     12u

/* SINDEX: one disk block caching every slot's presence and name. */
#define SAVE_INDEX_HEADER_BYTES 10u
#define SAVE_INDEX_ENTRY_BYTES  17u
#define SAVE_INDEX_BYTES \
    (SAVE_INDEX_HEADER_BYTES + SAVE_SLOT_COUNT * SAVE_INDEX_ENTRY_BYTES)
#define SAVE_INDEX_CHECKSUM     8u

/* Magic bytes "C64S" (record) / "C64I" (index). The letters are accepted
 * in either PETSCII case on read. */
#define FILE_C 0x43u
#define FILE_I 0x49u
#define FILE_S 0x53u

#endif
