#include <stdint.h>
#include "platform.h"
#include "banks.h"

/*
 * Cold object-type records (PlatformObjectTypeInfo: name[14], flags -
 * Look/Take text and actor-flag checks only, never a per-frame path).
 * Lives entirely in its own bank as a compile-time #embed'd const table -
 * replaces modules/typeinfo.c's old FAR_CALL-reached EasyFlash copy.
 *
 * Source: build/assets/objects-cold.bin (tools/prepare_c64_assets.py
 * objects-split), 256 * 15 = 3840 bytes.
 */

#pragma section( typeinfo_code, 0 )
#pragma section( typeinfo_data, 0 )
#pragma region( typeinfo_bank, 0x8000, 0xc000, , BANK_TYPEINFO,
                 { typeinfo_data, typeinfo_code } )
#pragma code(typeinfo_code)
#pragma data(typeinfo_data)

static const PlatformObjectTypeInfo cold_table[PLATFORM_OBJECT_TYPE_COUNT] = {
#embed 3840 "../build/assets/objects-cold.bin"
};

extern uint8_t typeinfo_query_type_id;
extern PlatformObjectTypeInfo platform_object_type_info_scratch;

__noinline void typeinfo_entry(void) {
    platform_object_type_info_scratch = cold_table[typeinfo_query_type_id];
}

#pragma code(code)
#pragma data(data)
