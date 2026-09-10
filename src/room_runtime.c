#include <stdint.h>
#include <string.h>

#include "game.h"

#define ROOM_CODE_BASE             0x9900u
#define ROOM_CODE_STAGE            ((uint8_t*)0xb000)
#define ROOM_CODE_MAX_BYTES        0x0400u
#define ROOM_CODE_HEADER_BYTES     24u
#define ROOM_CODE_ABI              4u
#define ROOM_CODE_DIRECTORY_BANK   3u
#define ROOM_CODE_DIRECTORY_ENTRY  8u

void platform_easyflash_copy_romh(void);
extern uint8_t platform_ef_copy_bank;
extern uint16_t platform_ef_copy_offset;
extern uint16_t platform_ef_copy_destination;
extern uint16_t platform_ef_copy_size;

uint8_t game_room_code_active;
static uint8_t room_code_prepared;
static uint16_t room_code_size;
static uint16_t room_code_bss_offset;
static uint16_t room_code_bss_size;

#pragma code-name (push, "HIGHCODE")

static uint16_t header_word(uint8_t offset) {
    return (uint16_t)ROOM_CODE_STAGE[offset] |
           ((uint16_t)ROOM_CODE_STAGE[offset + 1u] << 8);
}

static uint8_t validate_room_code(uint8_t room_id, uint16_t expected_size,
                                  uint16_t expected_checksum) {
    uint16_t enter_address;
    uint16_t look_address;
    uint16_t room_address;
    uint16_t use_address;
    uint16_t checksum;
    uint16_t i;

    if (expected_size < ROOM_CODE_HEADER_BYTES ||
        expected_size > ROOM_CODE_MAX_BYTES ||
        ROOM_CODE_STAGE[0] != 0x4cu || ROOM_CODE_STAGE[3] != 0x4cu ||
        ROOM_CODE_STAGE[6] != 0x4cu ||
        ROOM_CODE_STAGE[9] != 0x52u || ROOM_CODE_STAGE[10] != 0x43u ||
        ROOM_CODE_STAGE[11] != ROOM_CODE_ABI || ROOM_CODE_STAGE[12] != room_id ||
        ROOM_CODE_STAGE[21] != 0x4cu ||
        header_word(13u) != expected_size) return PLATFORM_ERR_FORMAT;

    enter_address = header_word(1u);
    look_address = header_word(4u);
    room_address = header_word(7u);
    use_address = header_word(22u);
    if (enter_address < ROOM_CODE_BASE ||
        enter_address >= ROOM_CODE_BASE + expected_size ||
        look_address < ROOM_CODE_BASE ||
        look_address >= ROOM_CODE_BASE + expected_size ||
        room_address < ROOM_CODE_BASE ||
        room_address >= ROOM_CODE_BASE + expected_size ||
        use_address < ROOM_CODE_BASE ||
        use_address >= ROOM_CODE_BASE + expected_size) {
        return PLATFORM_ERR_FORMAT;
    }
    room_code_bss_offset = header_word(15u);
    room_code_bss_size = header_word(17u);
    if (room_code_bss_offset < expected_size ||
        room_code_bss_offset + room_code_bss_size > ROOM_CODE_MAX_BYTES) {
        return PLATFORM_ERR_FORMAT;
    }
    checksum = 0u;
    for (i = ROOM_CODE_HEADER_BYTES; i < expected_size; ++i) {
        checksum += ROOM_CODE_STAGE[i];
    }
    if (checksum != header_word(19u) ||
        (expected_checksum != 0u && checksum != expected_checksum)) {
        return PLATFORM_ERR_FORMAT;
    }
    room_code_size = expected_size;
    return PLATFORM_OK;
}

static uint8_t room_code_prepare_easyflash(uint8_t room_id) {
    uint8_t bank;
    uint16_t offset;
    uint16_t size;
    uint16_t checksum;

    platform_ef_copy_bank = ROOM_CODE_DIRECTORY_BANK;
    platform_ef_copy_offset = (uint16_t)room_id * ROOM_CODE_DIRECTORY_ENTRY;
    platform_ef_copy_destination = (uint16_t)ROOM_CODE_STAGE;
    platform_ef_copy_size = ROOM_CODE_DIRECTORY_ENTRY;
    platform_easyflash_copy_romh();
    if (ROOM_CODE_STAGE[0] == 0xffu) return PLATFORM_ERR_NOT_FOUND;

    bank = ROOM_CODE_STAGE[0];
    offset = (uint16_t)ROOM_CODE_STAGE[2] |
             ((uint16_t)ROOM_CODE_STAGE[3] << 8);
    size = (uint16_t)ROOM_CODE_STAGE[4] |
           ((uint16_t)ROOM_CODE_STAGE[5] << 8);
    checksum = (uint16_t)ROOM_CODE_STAGE[6] |
               ((uint16_t)ROOM_CODE_STAGE[7] << 8);
    if (ROOM_CODE_STAGE[1] != 1u || offset + size > 0x2000u ||
        size > ROOM_CODE_MAX_BYTES) return PLATFORM_ERR_FORMAT;

    platform_ef_copy_bank = bank;
    platform_ef_copy_offset = offset;
    platform_ef_copy_destination = (uint16_t)ROOM_CODE_STAGE;
    platform_ef_copy_size = size;
    platform_easyflash_copy_romh();
    return validate_room_code(room_id, size, checksum);
}

uint8_t game_room_code_prepare(uint8_t room_id) {
    uint8_t status;
    room_code_prepared = 0u;
    status = room_code_prepare_easyflash(room_id);
    if (status == PLATFORM_OK) room_code_prepared = 1u;
    return status;
}

void game_room_code_activate(void) {
    game_room_code_active = 0u;
    if (!room_code_prepared) return;
    memcpy((void*)ROOM_CODE_BASE, ROOM_CODE_STAGE, room_code_size);
    memset((void*)(ROOM_CODE_BASE + room_code_bss_offset), 0,
           room_code_bss_size);
    game_room_code_active = 1u;
    room_code_prepared = 0u;
}

uint8_t game_room_code_load_current(void) {
    uint8_t status;
    status = game_room_code_prepare(platform_current_room);
    if (status == PLATFORM_OK) game_room_code_activate();
    return status;
}

#pragma code-name (pop)
