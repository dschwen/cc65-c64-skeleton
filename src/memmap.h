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

#pragma section( vicassets, 0, , , bss )
#pragma region( vicassetsregion, 0xe800, 0xfe00, , , { vicassets } )

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

#define CART_MARKER_ADDR    ((volatile uint8_t*)0x033cu)
#define CART_MARKER_0       0xefu
#define CART_MARKER_1       0x64u

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
#define ENVCODE_INIT    ((void (*)(void))0x7e00u)
#define ENVCODE_TICK    ((void (*)(void))0x7e03u)
#define ENVCODE_ENABLE  ((void (*)(void))0x7e06u)
#define ENVCODE_DISABLE ((void (*)(void))0x7e09u)

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
