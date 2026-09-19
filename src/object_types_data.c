#include <stdint.h>
#include "memmap.h"
#include "platform.h"
#include "platform_native.h"

/*
 * Resident hot object-type records (PlatformObjectType: dimensions,
 * hotspot, chars[16], colors[16], light - see platform.h). Initialized
 * directly from the prepared asset data at compile time instead of being
 * runtime-fetched from an EasyFlash bank at boot: under oscar64's
 * whole-program model this is just program data like any other global,
 * so there is no separate "load" step, and no non-cartridge PRG fallback
 * path is needed either (the embedded data always matches whatever the
 * cartridge itself contains, by construction).
 *
 * Not const: platform_hero_light_set() mutates object_types_a[1].light at
 * runtime (see src/hero_light.c). oscar64 still emits these as ordinary
 * initialized DATA (copied from the cartridge image into RAM at startup,
 * like every other non-BSS global), so runtime mutation works normally.
 *
 * Zone split (A resident/main, B $C000-CFFF, C $E000-E7FF) is unchanged
 * from the original design - see memmap.h's header comment for why those
 * two extra regions no longer need cc65-era CPU-port mode-switching to
 * read.
 *
 * #embed's limit/offset arguments must be literal integers (verified: no
 * macro or arithmetic-expression substitution happens there), so the byte
 * math below is spelled out instead of computed from OBJECT_TYPE_ZONE_*
 * constants - each is (record count) * 35 bytes/record, cumulative:
 *   Zone A: 106 records,  0 .. 3710   (offset 0,    limit 3710)
 *   Zone B: 117 records,  3710 .. 7805 (offset 3710, limit 4095)
 *   Zone C:  33 records,  7805 .. 8960 (offset 7805, limit 1155)
 * Source: build/assets/objects-hot.bin (tools/prepare_c64_assets.py
 * objects-split), 256 * 35 = 8960 bytes total.
 */

PlatformObjectType object_types_a[OBJECT_TYPE_ZONE_A_COUNT] = {
#embed 3710 "../build/assets/objects-hot.bin"
};

#pragma bss(hotc000)
#pragma data(hotc000)
PlatformObjectType object_types_b[OBJECT_TYPE_ZONE_B_COUNT] = {
#embed 4095 3710 "../build/assets/objects-hot.bin"
};
#pragma data(data)
#pragma bss(bss)

#pragma bss(hote000)
#pragma data(hote000)
PlatformObjectType object_types_c[OBJECT_TYPE_ZONE_C_COUNT] = {
#embed 1155 7805 "../build/assets/objects-hot.bin"
};
#pragma data(data)
#pragma bss(bss)
