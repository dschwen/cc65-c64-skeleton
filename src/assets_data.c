#include <stdint.h>
#include "memmap.h"
#include "banks.h"

/* Save/load borrows this workspace, then reloads it from the bank. */
uint8_t tile_data[2048];
uint8_t tile_properties[256];

/* Only main is initialized by the CRT loader. Keep the VIC asset sources
 * in a ROM bank and explicitly copy them to RAM beneath KERNAL. */
#pragma section( asset_tiles_code, 0 )
#pragma section( asset_tiles_data, 0 )
#pragma region( asset_tiles_bank, 0x8000, 0xc000, , BANK_ASSET_TILES,
                 { asset_tiles_data, asset_tiles_code } )
#pragma code(asset_tiles_code)
#pragma data(asset_tiles_data)

static const uint8_t charset_tile[2048] = {
#embed 2048 8 "../assets/charset.cchr"
};
static const uint8_t charset_text[2048] = {
#embed 2048 2056 "../assets/charset.cchr"
};
static const uint8_t tile_data_source[2048] = {
#embed 2048 8 "../assets/tiles.ctil"
};
static const uint8_t tile_properties_source[256] = {
#embed 256 2056 "../assets/tiles.ctil"
};

__noinline void asset_charsets_entry(void) {
    uint16_t i;
    for (i = 0u; i < 2048u; ++i) {
        TILE_CHARSET[i] = charset_tile[i];
        TEXT_CHARSET[i] = charset_text[i];
    }
}

__noinline void asset_tiles_entry(void) {
    uint16_t i;
    for (i = 0u; i < 2048u; ++i) tile_data[i] = tile_data_source[i];
    for (i = 0u; i < 256u; ++i) tile_properties[i] = tile_properties_source[i];
}

#pragma code(code)
#pragma data(data)

void platform_charsets_load(void) {
    far_call_bank(BANK_ASSET_TILES, asset_charsets_entry);
}

void platform_tiles_load(void) {
    far_call_bank(BANK_ASSET_TILES, asset_tiles_entry);
}
