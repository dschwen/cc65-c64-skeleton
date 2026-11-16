#include <stdint.h>
#include "memmap.h"
#include "platform.h"
#include "platform_native.h"

/* Gameplay keeps the 16 KiB EasyFlash window and KERNAL visible ($37).
 * Clearing HIRAM also hides cartridge ROM: only resident code may do so,
 * and it must restore the mapping before returning to a banked caller. */
#define CPU_PORT (*(volatile __memmap uint8_t*)0x0001u)
#define CPU_DDR  (*(volatile uint8_t*)0x0000u)
#define CPU_MAP_GAME     0x07u
#define CPU_MAP_KERNAL   0x07u
#define CPU_PORT_MASK   0x07u

uint8_t platform_cart_bank;

/* Short resident-only read gate for VIC RAM beneath KERNAL. Keep I/O
 * visible, mask IRQs, and preserve both the CPU port and interrupt state.
 * No calls or bank-window data accesses are allowed inside this interval. */
__noinline void platform_copy_vic_ram(uint8_t* destination,
                                      const uint8_t* source, uint8_t size) {
    __asm {
        php
        sei
        lda $01
        pha
        and #$f8
        ora #$05
        sta $01
    }
    while (size != 0u) {
        *destination++ = *source++;
        --size;
    }
    __asm {
        pla
        sta $01
        plp
    }
}

static void set_map(uint8_t value) {
    __asm {
        php
        sei
    }
    CPU_DDR |= CPU_PORT_MASK;
    CPU_PORT = (uint8_t)((CPU_PORT & (uint8_t)~CPU_PORT_MASK) | value);
    __asm { plp }
}

/* Restart the game from the title: select the startup bank again (a
 * software reset does not touch the EasyFlash register) and go through the
 * KERNAL reset, which finds the cartridge signature in bank 0 and cold
 * starts it. Never returns. Resident, so it may switch banks. */
void platform_restart(void) {
    __asm { sei }
    platform_cart_bank = 0u;
    eflash.bank = 0u;
    __asm {
        jmp ($fffc)
    }
}

void platform_memory_game(void) {
    set_map(CPU_MAP_GAME);
}

void platform_memory_kernal(void) {
    set_map(CPU_MAP_KERNAL);
}

uint8_t platform_irq_save_disable(void) {
    return __asm {
        php
        pla
        sta accu
        sei
    };
}

/* Copy `size` bytes from EasyFlash bank `bank`, offset `offset` within the
 * ROML (8000-9FFF) half, to `destination`. far_call_bank()'s software
 * shadow restores whichever bank the caller had selected. */
static uint16_t copy_offset;
static uint16_t copy_destination;
static uint16_t copy_size;
static const uint8_t* copy_window;

static void copy_window_body(void) {
    uint16_t i;
    const uint8_t* src = copy_window + copy_offset;
    uint8_t* dst = (uint8_t*)copy_destination;
    for (i = 0u; i < copy_size; ++i) dst[i] = src[i];
}

void platform_easyflash_copy_roml(uint8_t bank, uint16_t offset,
                                  void* destination, uint16_t size) {
    copy_window = (const uint8_t*)0x8000u;
    copy_offset = offset;
    copy_destination = (uint16_t)destination;
    copy_size = size;
    far_call_bank(bank, copy_window_body);
}

