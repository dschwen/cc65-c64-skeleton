#ifndef SAVELOAD_ABI_H
#define SAVELOAD_ABI_H

#include <stdint.h>

/* Mutable state shared by the in-place SL/SV services, borrowed from
 * tile_data/tile_properties (src/assets_data.c) while save/load is
 * running rather than given dedicated resident bytes - the "main"
 * resident region (memmap.h) has no room to spare for a second,
 * permanently-resident ~1.3 KB copy that is only ever needed during this
 * one synchronous, modal UI session. The room isn't drawn while save/load
 * is up, so nothing else touches this memory meanwhile;
 * game_load_show()/game_save_show() (modules/saveload.c) call
 * platform_tiles_load() again before returning to restore it.
 *
 * SL needs 136 bytes for decoded slot names; SV uses the full 146 bytes for
 * the raw index - both fit in tile_properties's 256 bytes. The maximum-size
 * (1,146-byte) save record and the 146-byte slot index are used at
 * disjoint times (browsing vs. viewing/writing one record), so they share
 * this one buffer too - and both fit in tile_data's 2048 bytes. */
#define SAVELOAD_WORKSPACE_BYTES 146u
extern uint8_t tile_properties[];
extern uint8_t tile_data[];
#define saveload_workspace tile_properties
#define SAVELOAD_RECORD_BYTES 1146u
#define saveload_record_buffer tile_data

/* Banked code must never expand far_call_bank() itself: it writes $DE00
 * inline, and doing that from inside $8000-$BFFF swaps the window under the
 * next instruction fetch. SL/SV reach other banks only through these
 * resident __noinline trampolines (src/disk_io.c for bank 14, and
 * saveload_save_show() in modules/saveload.c for the SV bank). The workers
 * they wrap (platform_disk_*_block) are private to src/disk_io.c. */
void platform_disk_read(void);
void platform_disk_write(void);
void platform_disk_index_read(void);
void platform_disk_index_write(void);
void saveload_save_show(void);

#endif
