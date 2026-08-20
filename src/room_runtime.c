#include <cbm.h>
#include <stdint.h>
#include <string.h>

#include "game.h"

#define ROOM_CODE_BASE             0x9900u
#define ROOM_CODE_STAGE            ((uint8_t*)0xa4e9)
#define ROOM_CODE_MAX_BYTES        0x0400u
#define ROOM_CODE_HEADER_BYTES     20u
#define ROOM_CODE_ABI              1u
#define ROOM_CODE_DIRECTORY_BANK   3u
#define ROOM_CODE_DIRECTORY_ENTRY  8u
#define ROOM_CODE_LFN              3u

void platform_memory_game(void);
void platform_memory_kernal(void);
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
static uint8_t room_code_header[ROOM_CODE_HEADER_BYTES];
static const char room_hex[] = "0123456789ABCDEF";

#pragma code-name (push, "HIGHCODE")

static uint16_t header_word(uint8_t offset) {
    return (uint16_t)ROOM_CODE_STAGE[offset] |
           ((uint16_t)ROOM_CODE_STAGE[offset + 1u] << 8);
}

static uint8_t read_exact(uint8_t lfn, uint8_t* destination, uint16_t size) {
    int got;
    while (size != 0u) {
        got = cbm_read(lfn, destination, size);
        if (got <= 0) return PLATFORM_ERR_IO;
        destination += (uint16_t)got;
        size -= (uint16_t)got;
    }
    return PLATFORM_OK;
}

static uint8_t validate_room_code(uint8_t room_id, uint16_t expected_size,
                                  uint16_t expected_checksum) {
    uint16_t enter_address;
    uint16_t look_address;
    uint16_t checksum;
    uint16_t i;

    if (expected_size < ROOM_CODE_HEADER_BYTES ||
        expected_size > ROOM_CODE_MAX_BYTES ||
        ROOM_CODE_STAGE[0] != 0x4cu || ROOM_CODE_STAGE[3] != 0x4cu ||
        ROOM_CODE_STAGE[6] != 0x52u || ROOM_CODE_STAGE[7] != 0x43u ||
        ROOM_CODE_STAGE[8] != ROOM_CODE_ABI || ROOM_CODE_STAGE[9] != room_id ||
        header_word(10u) != expected_size) return PLATFORM_ERR_FORMAT;

    enter_address = header_word(1u);
    look_address = header_word(4u);
    if (enter_address < ROOM_CODE_BASE ||
        enter_address >= ROOM_CODE_BASE + expected_size ||
        look_address < ROOM_CODE_BASE ||
        look_address >= ROOM_CODE_BASE + expected_size) {
        return PLATFORM_ERR_FORMAT;
    }
    room_code_bss_offset = header_word(12u);
    room_code_bss_size = header_word(14u);
    if (room_code_bss_offset < expected_size ||
        room_code_bss_offset + room_code_bss_size > ROOM_CODE_MAX_BYTES) {
        return PLATFORM_ERR_FORMAT;
    }
    checksum = 0u;
    for (i = ROOM_CODE_HEADER_BYTES; i < expected_size; ++i) {
        checksum += ROOM_CODE_STAGE[i];
    }
    if (checksum != header_word(16u) ||
        (expected_checksum != 0u && checksum != expected_checksum)) {
        return PLATFORM_ERR_FORMAT;
    }
    room_code_size = expected_size;
    return PLATFORM_OK;
}

static uint8_t room_code_prepare_disk(uint8_t room_id) {
    char filename[4];
    uint8_t status;
    uint16_t size;

    filename[0] = 'C';
    filename[1] = room_hex[room_id >> 4];
    filename[2] = room_hex[room_id & 0x0fu];
    filename[3] = '\0';
    platform_memory_kernal();
    if (cbm_open(ROOM_CODE_LFN, platform_storage_device, CBM_READ, filename) != 0u) {
        platform_memory_game();
        return PLATFORM_ERR_IO;
    }
    status = read_exact(ROOM_CODE_LFN, room_code_header, ROOM_CODE_HEADER_BYTES);
    if (status == PLATFORM_OK) {
        size = (uint16_t)room_code_header[10] |
               ((uint16_t)room_code_header[11] << 8);
        if (size < ROOM_CODE_HEADER_BYTES || size > ROOM_CODE_MAX_BYTES) {
            status = PLATFORM_ERR_FORMAT;
        } else {
            memcpy(ROOM_CODE_STAGE, room_code_header, ROOM_CODE_HEADER_BYTES);
            status = read_exact(ROOM_CODE_LFN,
                                ROOM_CODE_STAGE + ROOM_CODE_HEADER_BYTES,
                                size - ROOM_CODE_HEADER_BYTES);
        }
    }
    cbm_close(ROOM_CODE_LFN);
    platform_memory_game();
    if (status != PLATFORM_OK) return status;
    return validate_room_code(room_id, header_word(10u), 0u);
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
    status = platform_storage == PLATFORM_STORAGE_EASYFLASH
                 ? room_code_prepare_easyflash(room_id)
                 : room_code_prepare_disk(room_id);
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
