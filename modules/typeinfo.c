#include <stdint.h>

#include "platform.h"

/* Runs from an EasyFlash bank, in place - it is never copied into RAM. Called
 * through FAR_CALL (see _platform_object_type_info_get in src/banked_api.s),
 * which maps this bank over $8000-$BFFF for the duration.
 *
 * Everything this touches therefore has to live outside that window: the
 * platform_ef_copy_* parameters are in DATA (below $8000),
 * platform_easyflash_copy_romh() is in HIGHCODE (below $8000), and the scratch
 * record it fills is in LOWBSS for exactly this reason. It is also cold - Look
 * and Take only - which matters, because a bank switch waits for the raster to
 * wrap and so is far too expensive for anything on a per-frame path.
 */
#define OBJECT_TYPE_COLD_BYTES 15u
#define EF_TYPE_BANK_0         46u

void platform_easyflash_copy_romh(void);
extern uint8_t platform_ef_copy_bank;
extern uint16_t platform_ef_copy_offset;
extern uint16_t platform_ef_copy_destination;
extern uint16_t platform_ef_copy_size;
extern PlatformObjectTypeInfo platform_object_type_info_scratch;

const PlatformObjectTypeInfo* platform_object_type_info_banked(uint8_t type_id) {
    platform_ef_copy_bank = EF_TYPE_BANK_0;
    platform_ef_copy_offset = (uint16_t)type_id * OBJECT_TYPE_COLD_BYTES;
    platform_ef_copy_destination = (uint16_t)&platform_object_type_info_scratch;
    platform_ef_copy_size = OBJECT_TYPE_COLD_BYTES;
    platform_easyflash_copy_romh();
    return &platform_object_type_info_scratch;
}
