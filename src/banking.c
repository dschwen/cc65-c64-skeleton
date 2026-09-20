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
#define CPU_MAP_ALL_RAM  0x04u
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

void platform_memory_game(void) {
    set_map(CPU_MAP_GAME);
}

void platform_memory_kernal(void) {
    set_map(CPU_MAP_KERNAL);
}

void platform_memory_all_ram(void) {
    set_map(CPU_MAP_ALL_RAM);
}

uint8_t platform_irq_save_disable(void) {
    return __asm {
        php
        pla
        sta accu
        sei
    };
}

void platform_irq_restore(uint8_t status) {
    __asm {
        lda status
        pha
        plp
    }
}

/* Copy `size` bytes from EasyFlash bank `bank`, offset `offset` within the
 * ROML (8000-9FFF) or ROMH (A000-BFFF) half, to `destination`. Replaces
 * src/banking.s's easyflash_copy_roml/romh, which used the FAR_CALL
 * bank-stack machinery to make this safe to call from already-banked
 * code; far_call_bank() gives the same reentrancy natively. */
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

void platform_easyflash_copy_romh(uint8_t bank, uint16_t offset,
                                  void* destination, uint16_t size) {
    copy_window = (const uint8_t*)0xa000u;
    copy_offset = offset;
    copy_destination = (uint16_t)destination;
    copy_size = size;
    far_call_bank(bank, copy_window_body);
}
