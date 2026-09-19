#ifndef GAME_MEMMAP_H
#define GAME_MEMMAP_H

/*
 * oscar64 memory/bank layout for the EasyFlash cartridge build. Replaces
 * cc65's cfg/*.cfg linker configs and src/platform.inc's hardware constants.
 *
 * Hardware model: EasyFlash banking affects ONLY $8000-$BFFF (ROML/ROMH).
 * Everything outside that window ($0000-$7FFF, $C000-$FFFF minus I/O) is
 * ordinary RAM regardless of which bank is paged in - unlike the old cc65
 * design, which also used $8000-$BFFF as plain RAM during "normal gameplay"
 * and only briefly engaged the cartridge for far calls. Under oscar64 that
 * window is *always* the bank-switch target, so all always-resident state
 * lives below $8000 or above $C000, and calling from banked code into
 * resident code is always safe with an ordinary function call - the
 * resident function's address never falls inside the banked window, so no
 * "host RAM gate" indirection (src/banked_api.s's host_ram_call) is needed.
 *
 * Resident budget: main ($0901-$8000, ~28.4 KiB) + hotc000 ($C000-$CFFF,
 * 4 KiB, below the $D000 I/O hole so it needs no CPU-port dance) +
 * hote000 ($E000-$E7FF, 2 KiB, visible whenever KERNAL is banked out, our
 * steady state - see memory_kernal()/memory_game() below). VIC bank 3
 * assets (charsets/screen/sprites) occupy $E800-$FDFF, same addresses as
 * the original cc65 design (an arbitrary but proven-good hardware choice,
 * unrelated to the compiler).
 */

#include <stdint.h>
#include <c64/easyflash.h>

#pragma section( hotc000, 0, , , bss )
#pragma region( hotc000region, 0xc000, 0xd000, , , { hotc000 } )

#pragma section( hote000, 0, , , bss )
#pragma region( hote000region, 0xe000, 0xe800, , , { hote000 } )

/* VIC bank 3 fixed layout: the VIC-II reads charset/screen memory from
 * exact physical addresses selected by $D018 (see src/irq.c's
 * TILE_MEMPTR/TEXT_MEMPTR), so these need precise placement, not just
 * "somewhere in a region" - two separate regions rather than one shared
 * one. Screen RAM/sprite data are addressed directly via SCREEN_RAM/
 * SPRITE_DATA below (no C array is declared there, so no region/section
 * is needed for them). */
#pragma section( charsettile, 0, , , data )
#pragma region( charsettileregion, 0xe800, 0xf000, , , { charsettile } )
#pragma section( charsettext, 0, , , data )
#pragma region( charsettextregion, 0xf000, 0xf800, , , { charsettext } )

/* Fixed hardware addresses, same choices as the original cc65 build. */
#define SCREEN_RAM          ((uint8_t*)0xf800u)
#define COLOR_RAM           ((volatile uint8_t*)0xd800u)
#define SPRITE_POINTERS     ((uint8_t*)0xfbf8u)
#define SPRITE_DATA         ((uint8_t*)0xfc00u)
#define OBJECT_TYPE_CHARS   2u
#define OBJECT_TYPE_COLORS  18u

#define ROOM_TILES          13u
#define MAP_WIDTH_TILES     20u
#define MAP_HEIGHT_TILES    11u
#define MAP_WIDTH_CHARS     40u
#define MAP_HEIGHT_CHARS    22u
#define ROOM_OBJECTS        (ROOM_TILES + MAP_WIDTH_TILES * MAP_HEIGHT_TILES)
#define ROOM_FILE_BYTES     (ROOM_OBJECTS + 256u * 3u)

/* The current room's environment module (weather + ambient sound) - see
 * PLATFORM_API.md's "Room environment module". Copied in from EasyFlash at
 * room entry, called every frame from the resident raster IRQ, so it must
 * live in reserved, always-RAM space, never a banked window. Carved out of
 * `main` as its own region so the compiler's allocator never places
 * anything else there. Fixed jump-table layout, 3 bytes per vector (a
 * plain JMP): offset 0 init, 3 tick, 6 enable, 9 disable - same convention
 * as before.
 */
#pragma section( envcode, 0, , , bss )
#pragma region( envcoderegion, 0x7e00, 0x8000, , , { envcode } )
#define ENVCODE_BASE    ((uint8_t*)0x7e00u)
#define ENVCODE_SIZE    0x0200u

/* Every compiled environment module (see rooms/env/*.c) starts with this
 * table of four function pointers - not raw JMP instructions like the old
 * ca65 design's fixed-offset jump table, since oscar64 places a module's
 * own init/tick/enable/disable wherever the compiler chooses. Each
 * module's own `const EnvTable env_table = {...}` is placed first in its
 * data section (see rooms/env/*.c) and kept alive with #pragma
 * reference(), so it always lands at the copied blob's byte 0 - exactly
 * the address these four macros read through once platform_resource_
 * fetch()/game_enter_room() has copied that module here. */
typedef struct EnvTable {
    void (*init)(void);
    void (*tick)(void);
    void (*enable)(void);
    void (*disable)(void);
} EnvTable;
#define ENVCODE_TABLE ((const EnvTable*)0x7e00u)
#define ENVCODE_INIT()    (ENVCODE_TABLE->init())
#define ENVCODE_TICK()    (ENVCODE_TABLE->tick())
#define ENVCODE_ENABLE()  (ENVCODE_TABLE->enable())
#define ENVCODE_DISABLE() (ENVCODE_TABLE->disable())

/*
 * Switch to `bank`, call `fn`, then restore whatever bank was active before -
 * the native oscar64 replacement for the old FAR_CALL self-modifying
 * trampoline (src/banking.s). Reentrant: a banked function that itself
 * calls far_call_bank() nests correctly, because each call saves/restores
 * its own caller's bank around the inner call, exactly like the original's
 * bank_stack array, but using an ordinary call stack instead of a fixed-
 * depth array.
 *
 * EVERY function passed here (and every function placed in a bank's own
 * #pragma code() section in general) MUST be declared __noinline. Verified
 * live: without it, oscar64's whole-program optimizer can and does inline
 * a small single-call-site "banked" function straight into its resident
 * caller, silently placing its code in the resident region instead of the
 * bank - the bank switch then does nothing useful, and (for a function
 * that reads bank-local data) reads whatever bank happens to be active at
 * the call site instead of the intended one. __noinline forces a real,
 * separately-addressable function that actually lives in its own bank.
 */
static inline void far_call_bank(uint8_t bank, void (*fn)(void)) {
    uint8_t saved = eflash.bank;
    eflash.bank = bank;
    fn();
    eflash.bank = saved;
}

#endif
