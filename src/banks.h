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
#define BANK_LOOK_HELPERS   3u
#define BANK_SCRIPT         4u
#define BANK_INVENTORY      5u
#define BANK_SAVELOAD       6u
#define BANK_SAVELOAD_SAVE  7u

/* Rooms 00-05 today; one bank each, contiguous from BANK_ROOM_FIRST. */
#define BANK_ROOM_FIRST     8u

/* KERNAL serial-bus disk I/O (src/disk_io.c) - banked rather than resident
 * because oscar64's krnio_* library (c64/kernalio.h) it's built on pulls in
 * enough code to overflow the resident "main" region (see memmap.h)
 * alongside everything else that now needs to be resident. Called from the
 * SL/SV banks through resident trampolines (platform_disk_read() etc. in
 * src/disk_io.c) - far_call_bank() itself may not be expanded inside a bank. */
#define BANK_DISK_IO        14u

/* Tile/text charsets and source copy of tile_data/tile_properties
 * (src/assets_data.c) - the
 * *resident* copies are plain (non-const) BSS, deliberately reused as
 * scratch space by the save/load UI (SL/SV) while it is running, then
 * reloaded from here afterward. Keeping the source in its own bank instead
 * of a compile-time #embed initializer directly into the resident arrays
 * is what makes that reuse possible without a second, permanently-resident
 * copy - see src/assets_data.c's platform_tiles_load(). */
#define BANK_ASSET_TILES    15u

/* Boot source for zone B plus banked PlatformObjectType records for zone C
 * (type IDs 223-255, see
 * src/platform_native.h's OBJECT_TYPE_ZONE_* split) - src/object_types_data.c.
 * Keeping the cartridge window visible also maps KERNAL over the former
 * hote000 region ($E000-$E7FF). Unlike BANK_TYPEINFO's cold
 * records, this *is* on the per-frame object-draw path, so
 * platform_object_type_get() (src/platform.c) keeps a one-entry resident
 * cache in front of this bank and only far_call_bank()s in on a cache miss. */
#define BANK_OBJECT_TYPE_C  16u

/* World-state store/restore logic (src/world.c's world_store()/
 * world_restore(), the per-room object-delta snapshot taken on every room
 * transition) - 800+ bytes of code that's only ever invoked from
 * platform_room_enter(), a natural banking candidate once the resident
 * budget got tight again porting modules/script.c back in (see src/
 * resources.c's header comment). The delta/baseline *data* these operate
 * on (game_world_deltas[], room_object_baseline[]) stays resident - a
 * banked function reading/writing resident data is fine and cheap (see
 * memmap.h's header comment); only the *code* moves. Previously reached
 * through a registered function-pointer hook (platform_room_state_hooks())
 * since world.c was the only module that ever implemented it - replaced
 * with a direct, literal far_call_bank() (see memmap.h's far_call_bank()
 * comment on why an indirect call through a stored pointer while banked is
 * unsafe) plus a plain enabled/disabled flag for the one case that used to
 * register/unregister the store hook. */
#define BANK_WORLD_STATE 17u

/* Environment modules (rooms/env/*.c) - one bank per distinct module;
 * several rooms may share a bank when their module content is identical
 * (see rooms/env_banks.h's room-to-bank lookup). Each module is small
 * (well under one 16 KiB bank) and copied whole to $7E00 at room entry -
 * see memmap.h's ENVCODE_* macros. */
#define BANK_ENV_RAIN            20u /* rooms 00, 01 */
#define BANK_ENV_TAVERN          21u /* room 02 */
#define BANK_ENV_TAVERN_MUFFLED  22u /* rooms 03, 05 */
#define BANK_ENV_CELLAR          23u /* room 04 */

/* Portraits: 32 x 256-byte sprite images per bank, same packing as the
 * original (see EF_PORTRAITS_PER_BANK in the old src/platform.c). */
#define BANK_PORTRAIT_FIRST     24u
#define PORTRAITS_PER_BANK      32u

/* Resource banks (scripts, conversations, room scripts) start here.
 * Compiled bytecode (tools/compile_script.py) for each PLATFORM_RESOURCE_
 * KIND_* directory (src/platform.h) gets one *shared* bank per kind -
 * simpler and cheaper (one far_call_bank() call site regardless of
 * resource count - see src/resources.c) than either the cc65-era design's
 * shared ROML/ROMH directories with checksums, or one bank per resource;
 * 64 banks are available and this game's total banked content is well
 * under that (see OSCAR64_MIGRATION.md), but *resident* code calling in is
 * the scarce resource (src/memmap.h), not banks. All six rooms' script
 * content (assets/scripts/00-05.script) fits comfortably in one bank -
 * see tools/generate_script_bank.py. */
#define BANK_RESOURCE_FIRST 32u
#define BANK_SCRIPT_ROOM_FIRST BANK_RESOURCE_FIRST

/* Conversations (assets/scripts/conversations/<id>.script) get their own
 * shared bank, one far_call_bank() call site of their own in
 * src/resources.c - a separate bank, not sharing BANK_SCRIPT_ROOM_FIRST,
 * because conversations have their own independent 0-255 ID space (see
 * tools/compile_script.py's module docstring); reusing the room bank's ID
 * switch would silently collide a conversation and a room that happen to
 * share a numeric ID. */
#define BANK_SCRIPT_CONVERSATION_FIRST 33u

#endif
