#include <stdint.h>
#include "memmap.h"

/*
 * Native (oscar64 C) replacement for src/assets.s. Charsets and tile
 * data/properties are compile-time #embed-initialized constants instead
 * of PLATFORM_RESOURCE_KIND_ASSET resources fetched at boot - under
 * oscar64's whole-program model this is just program data, and unlike
 * the old cc65 design there is no separate PRG-fallback path where this
 * data might legitimately be absent (see platform.c's platform_init()).
 * charset_tile/charset_text are placed in their own fixed VIC bank 3
 * regions (memmap.h) since the VIC-II reads them from those exact
 * physical addresses; tile_data/tile_properties are ordinary resident
 * data (read by CPU game logic, never by the VIC).
 *
 * Source: assets/charset.cchr (8-byte editor header + 2048B tile charset
 * + 2048B text charset) and assets/tiles.ctil (8-byte header + 2048B tile
 * bitmaps + 256B property table, fetched as one blob historically so the
 * two can never drift out of step - kept as two separate #embeds here
 * only because tile_data/tile_properties are separate C arrays, not
 * because the source split changed).
 */

#pragma data(charsettile)
const uint8_t charset_tile[2048] = {
#embed 2048 8 "../assets/charset.cchr"
};
#pragma data(data)

#pragma data(charsettext)
const uint8_t charset_text[2048] = {
#embed 2048 2056 "../assets/charset.cchr"
};
#pragma data(data)

const uint8_t tile_data[2048] = {
#embed 2048 8 "../assets/tiles.ctil"
};

const uint8_t tile_properties[256] = {
#embed 256 2056 "../assets/tiles.ctil"
};
