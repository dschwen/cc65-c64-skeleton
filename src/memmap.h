#ifndef GAME_MEMMAP_H
#define GAME_MEMMAP_H

#include <stdint.h>
#include <c64/easyflash.h>

/* EasyFlash startup expands only main into RAM. Other RAM regions must be
 * populated explicitly. Keep resident code and both stacks below envcode. */
#pragma region( main, 0x0900, 0x7e00, , , { code, data, bss, stack } )
#pragma stacksize(2048)

#pragma section( hotc000, 0, , , bss )
#pragma region( hotc000region, 0xc000, 0xd000, , , { hotc000 } )

/* VIC bank 3 physical RAM. With $01=$37, CPU writes reach RAM beneath
 * KERNAL but reads see ROM. Use platform_copy_vic_ram() for reads. */
#define TILE_CHARSET        ((uint8_t*)0xe800u)
#define TEXT_CHARSET        ((uint8_t*)0xf000u)
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

/* Relocated environment code starts with four native function pointers.
 * Copied here at room entry; this window is excluded from main. */
#define ENVCODE_BASE    ((uint8_t*)0x7e00u)
#define ENVCODE_SIZE    0x0200u
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

/* HIRAM and LORAM must be set to expose the 16 KiB cartridge window.
 * $DE00 is write-only: use a software shadow to restore nested banks.
 * Banked entries must be __noinline to keep bank-local accesses in bank. */
extern uint8_t platform_cart_bank;
#define far_call_bank(bank_, fn_) do { \
    uint8_t saved__ = platform_cart_bank; \
    platform_cart_bank = (bank_); \
    eflash.bank = platform_cart_bank; \
    fn_(); \
    platform_cart_bank = saved__; \
    eflash.bank = saved__; \
} while (0)

#endif
