#include <stdint.h>
#include "memmap.h"
#include "platform.h"
#include "platform_native.h"
#include "banks.h"

/* Zone A is initialized in main. Zone B at $C000 is explicitly loaded
 * from bank 16 because Oscar64 only expands main at cartridge startup.
 * Zone C remains banked and is fetched through a resident one-entry cache.
 * Each hot record is 35 bytes: A=106 records, B=117, C=33. */
PlatformObjectType object_types_a[OBJECT_TYPE_ZONE_A_COUNT] = {
#embed 3710 "../build/assets/objects-hot.bin"
};

#pragma bss(hotc000)
PlatformObjectType object_types_b[OBJECT_TYPE_ZONE_B_COUNT];
#pragma bss(bss)

/* Zero is a sentinel: zone C starts at type 223. Main BSS is cleared by CRT. */
PlatformObjectType object_type_c_scratch;
uint8_t object_type_c_cache_tag = 0u;
uint8_t object_type_c_query_id;

#pragma section( object_type_c_code, 0 )
#pragma section( object_type_c_data, 0 )
#pragma region( object_type_c_bank, 0x8000, 0xc000, , BANK_OBJECT_TYPE_C,
                 { object_type_c_data, object_type_c_code } )
#pragma code(object_type_c_code)
#pragma data(object_type_c_data)

static const PlatformObjectType object_type_c_cold[OBJECT_TYPE_ZONE_C_COUNT] = {
#embed 1155 7805 "../build/assets/objects-hot.bin"
};

static const uint8_t object_types_b_source[4095] = {
#embed 4095 3710 "../build/assets/objects-hot.bin"
};

__noinline void object_types_b_load(void) {
    uint16_t i;
    for (i = 0u; i < sizeof(object_types_b_source); ++i) {
        ((uint8_t*)object_types_b)[i] = object_types_b_source[i];
    }
}

__noinline void object_type_c_fetch(void) {
    object_type_c_scratch =
        object_type_c_cold[object_type_c_query_id - OBJECT_TYPE_ZONE_AB_COUNT];
}

#pragma code(code)
#pragma data(data)
