#ifndef GAME_BANKS_H
#define GAME_BANKS_H

/*
 * EasyFlash bank assignments for the oscar64 port. oscar64 gives each bank
 * a full 16 KiB window and needs no byte-exact packing the way the old
 * ld65-config/pack_easyflash.py scheme did (that packed multiple services
 * into ROML/ROMH halves of shared banks to conserve banks; with 64
 * available and this game's total banked content well under that, a
 * simpler one-service/one-room-per-bank scheme is used instead - see
 * OSCAR64_MIGRATION.md).
 */

#define BANK_TYPEINFO       1u  /* cold PlatformObjectTypeInfo records */
#define BANK_ROOM_HELPERS   2u
#define BANK_LOOK_HELPERS   2u
#define BANK_SCRIPT         2u
#define BANK_INVENTORY      3u
#define BANK_SAVELOAD       4u
#define BANK_SAVELOAD_SAVE  5u

/* Rooms 00-05 today; one bank each, contiguous from BANK_ROOM_FIRST. */
#define BANK_ROOM_FIRST     6u

/* Portraits: 32 x 256-byte sprite images per bank, same packing as the
 * original (see EF_PORTRAITS_PER_BANK in the old src/platform.c). */
#define BANK_PORTRAIT_FIRST     16u
#define PORTRAITS_PER_BANK      32u

/* Resource banks (scripts, conversations, room scripts, environment
 * modules) start here - see the resource system's own header once
 * ported. */
#define BANK_RESOURCE_FIRST 24u

#endif
