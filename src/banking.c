#include <stdint.h>
#include "memmap.h"
#include "platform.h"
#include "platform_native.h"

/*
 * Native (oscar64 C) replacement for src/banking.s. Under oscar64's model,
 * EasyFlash banking affects only $8000-$BFFF (see memmap.h's header
 * comment), so the elaborate cc65-era CPU_PORT dance to make that window
 * plain RAM ("gameplay" mode) no longer applies - only KERNAL/I-O
 * visibility above $C000 remains relevant, and far calls are just
 * eflash.bank switches (memmap.h's far_call_bank()), not a self-modifying
 * trampoline.
 */

#define CPU_PORT (*(volatile uint8_t*)0x0001u)
#define CPU_DDR  (*(volatile uint8_t*)0x0000u)

/* Bits 0-2 of $01: LORAM, HIRAM, CHAREN. KERNAL/BASIC never actually cover
 * anything this program reads once booted (EasyFlash owns $8000-$BFFF
 * entirely; nothing here uses BASIC), so the only two states that matter
 * are "KERNAL out, I/O in" (the steady state - exposes the $E000-$E7FF hot
 * region as RAM) and "KERNAL in" (brief, for real KERNAL disk-driver
 * calls). CPU_MAP_ALL_RAM additionally hides I/O, exposing RAM beneath
 * $D000-$DFFF too, for the rare case that needs it.
 */
#define CPU_MAP_GAME     0x05u /* LORAM=1 HIRAM=0 CHAREN=1: KERNAL/BASIC out, I/O visible */
#define CPU_MAP_KERNAL   0x06u /* LORAM=0 HIRAM=1 CHAREN=1: KERNAL in, I/O visible */
#define CPU_MAP_ALL_RAM  0x04u /* LORAM=0 HIRAM=0 CHAREN=1: KERNAL/BASIC/I-O all out */
#define CPU_PORT_MASK    0x07u

static void set_map(uint8_t value) {
    __asm { php sei }
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
    uint8_t status;
    __asm {
        php
        pla
        sta status
        sei
    }
    return status;
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
