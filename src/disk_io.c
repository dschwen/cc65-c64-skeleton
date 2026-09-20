#include <stdint.h>

#include "saveload_abi.h"
#include "banks.h"
#include "memmap.h"

/* DOS command/filename literals use low PETSCII (ASCII letter values),
 * independently of the game's mixed-case display string mapping. */
#pragma charmap(65, 65, 26)
#pragma charmap(97, 97, 26)

/*
 * Native (oscar64 C) replacement for modules/disk_io.s: block-oriented
 * KERNAL sequential-file I/O for save/load. Hand-written __asm KERNAL
 * calls, not oscar64's built-in c64/kernalio.h wrappers: that header's
 * krnio_pstatus[] global and its several non-BANKINLINE functions are
 * emitted by their own separately-compiled source file (kernalio.c, pulled
 * in via a `#pragma compile()` in the header), which has no knowledge of
 * this file's bank pragma below - confirmed live, ~240 bytes of it always
 * land in the resident "main" region (memmap.h) regardless, which the
 * resident budget cannot spare. The KERNAL vectors and register
 * conventions here are the exact same ones oscar64's own library uses
 * internally, just written directly into this bank instead.
 *
 * Banked (BANK_DISK_IO) rather than resident for the same budget reason.
 * The SL/SV banks (modules/saveload.c, modules/saveload_save.c) must NOT
 * expand far_call_bank() themselves: that macro writes $DE00 inline, and
 * doing so from code inside $8000-$BFFF swaps the window out from under the
 * very next instruction fetch. They call the four resident __noinline
 * trampolines below (platform_disk_read() etc.) instead, which do the
 * switch from resident code. What *is* KERNAL-call-
 * specific is the memory map bracket: platform_memory_kernal() exposes
 * KERNAL ROM for the calls below and platform_memory_game() restores our
 * steady-state map (RAM at $E000-$FFFF, EasyFlash-bankable $8000-$BFFF)
 * once done - both ordinary resident functions, callable from any bank.
 * raster_irq_suspend()/resume() keep the raster IRQ from touching $D018/
 * $D011 while the display and CPU map are in whatever state the KERNAL
 * driver leaves them, matching the original.
 *
 * The on-disk filenames and command sequences are unchanged byte-for-byte
 * from the original modules/disk_io.s.
 */

void platform_memory_kernal(void);
void platform_memory_game(void);
void raster_irq_suspend(void);
void raster_irq_resume(void);

#define DISK_DEVICE 8u

/* Resident, not bank-local: the SL/SV banks set these before far_call_bank()
 * switches to this bank, so they must stay visible across that switch (see
 * memmap.h's header comment). */
uint8_t platform_disk_slot;
uint8_t* platform_disk_buffer;
uint16_t platform_disk_want;
uint16_t platform_disk_got;

/* Bank-14 workers, defined below the region pragma. */
void platform_disk_read_block(void);
void platform_disk_write_block(void);
void platform_disk_index_read_block(void);
void platform_disk_index_write_block(void);

/* Resident trampolines: the only way banked code (SL/SV) reaches bank 14.
 * Each names its target literally - never through a pointer. */
__noinline void platform_disk_read(void) {
    far_call_bank(BANK_DISK_IO, platform_disk_read_block);
}

__noinline void platform_disk_write(void) {
    far_call_bank(BANK_DISK_IO, platform_disk_write_block);
}

__noinline void platform_disk_index_read(void) {
    far_call_bank(BANK_DISK_IO, platform_disk_index_read_block);
}

__noinline void platform_disk_index_write(void) {
    far_call_bank(BANK_DISK_IO, platform_disk_index_write_block);
}

#pragma section( disk_io_code, 0 )
#pragma section( disk_io_data, 0 )
#pragma region( disk_io_bank, 0x8000, 0xc000, , BANK_DISK_IO,
                 { disk_io_data, disk_io_code } )
#pragma code(disk_io_code)
#pragma data(disk_io_data)

/* KERNAL vectors and register conventions below match oscar64's own
 * include/c64/kernalio.c exactly (SETNAM/SETLFS/OPEN/CLOSE/CHKIN/CHKOUT/
 * CLRCHN/CHRIN/CHROUT/READST) - see that file for the (BSD-licensed)
 * original this was adapted from. */
static void k_setnam(const uint8_t* name, uint8_t len) {
    __asm {
        lda len
        ldx name
        ldy name + 1
        jsr $ffbd
    }
}

static uint8_t k_open(uint8_t fnum, uint8_t device, uint8_t channel) {
    return __asm {
        lda #0
        sta accu
        lda fnum
        ldx device
        ldy channel
        jsr $ffba
        jsr $ffc0
        bcc W1
        lda fnum
        jsr $ffc3
        jmp E2
    W1:
        lda #1
        sta accu
    E2:
    };
}

static void k_close(uint8_t fnum) {
    __asm {
        lda fnum
        jsr $ffc3
    }
}

static uint8_t k_status(void) {
    return __asm {
        jsr $ffb7 : ->a
        sta accu
    };
}

static uint8_t k_chkin(uint8_t fnum) {
    return __asm {
        ldx fnum
        jsr $ffc6 : x->axy
        lda #0
        rol
        eor #1
        sta accu
    };
}

static uint8_t k_chkout(uint8_t fnum) {
    return __asm {
        ldx fnum
        jsr $ffc9 : x->ax
        lda #0
        rol
        eor #1
        sta accu
    };
}

static void k_clrchn(void) {
    __asm {
        jsr $ffcc : ->ax
    }
}

static uint8_t k_chrin(void) {
    return __asm {
        jsr $ffcf : a->a
        sta accu
    };
}

static void k_chrout(uint8_t ch) {
    __asm {
        lda ch
        jsr $ffd2 : a->a
    }
}

/* CBM DOS command/type/mode letters are explicit low-PETSCII byte values,
 * matching the original's comment: an oscar64 char literal is plain ASCII
 * (no C64 charmap translation the way cl65's was), so 'S'/'W'/etc. here
 * are already the correct DOS bytes - no equivalent hazard to guard
 * against, but the literal byte values are kept as comments for clarity
 * against the original. */
static uint8_t filename[11];

static void build_slot_filename(void) {
    filename[3] = 'S';
    filename[4] = (uint8_t)('0' + platform_disk_slot);
    filename[5] = ',';
    filename[6] = 'W';
}

static void build_index_filename(void) {
    filename[3] = 'S';
    filename[4] = 'I';
    filename[5] = 'N';
    filename[6] = 'D';
    filename[7] = 'E';
    filename[8] = 'X';
    filename[9] = ',';
    filename[10] = 'W';
}

static void scratch_named(uint8_t name_len) {
    filename[0] = 'S';
    filename[1] = '0';
    filename[2] = ':';
    k_setnam(filename, (uint8_t)(3u + name_len));
    if (k_open(15u, DISK_DEVICE, 15u)) {
        k_close(15u);
    }
}

static void scratch_slot(void) {
    build_slot_filename();
    scratch_named(4u);
}

static void scratch_index(void) {
    build_index_filename();
    scratch_named(8u);
}

static void read_named(uint8_t name_len) {
    platform_disk_got = 0u;
    k_setnam(filename + 3, name_len);
    if (!k_open(2u, DISK_DEVICE, 2u) || k_status() != 0u) {
        k_close(2u);
        return;
    }
    if (!k_chkin(2u) || k_status() != 0u) {
        k_clrchn();
        k_close(2u);
        return;
    }
    while (platform_disk_got < platform_disk_want) {
        platform_disk_buffer[platform_disk_got] = k_chrin();
        ++platform_disk_got;
        if (k_status() != 0u) break;
    }
    k_clrchn();
    k_close(2u);
}

/* Read names exclude the ",W" suffix (length 2 / 6, as in the original
 * modules/disk_io.s): opening "Sn,W" for reading would open it for WRITE,
 * creating/truncating the file - including the save just written, which
 * made every save's readback verification fail. */
static void read_slot_block(void) {
    build_slot_filename();
    read_named(2u);
}

static void read_index_block(void) {
    build_index_filename();
    read_named(6u);
}

/* Waits for DOS to finish the preceding write and reads its two status
 * digits over the command channel. Only "00" (OK) leaves platform_disk_got
 * untouched; any other status turns the write into a caller-visible
 * failure by zeroing it. A 1541 can still be writing the final data block
 * and directory entry after KERNAL CLOSE has returned, so this drains the
 * command channel before the caller trusts the result. */
static void wait_drive_ready(void) {
    uint8_t d0;
    uint8_t d1;

    k_setnam(filename, 0u);
    if (!k_open(15u, DISK_DEVICE, 15u) || k_status() != 0u) {
        k_close(15u);
        platform_disk_got = 0u;
        return;
    }
    if (!k_chkin(15u) || k_status() != 0u) {
        k_clrchn();
        k_close(15u);
        platform_disk_got = 0u;
        return;
    }
    d0 = k_chrin();
    d1 = k_chrin();
    k_clrchn();
    k_close(15u);
    if (d0 != '0' || d1 != '0') {
        platform_disk_got = 0u;
    }
}

static void write_named(uint8_t name_len) {
    platform_disk_got = 0u;
    k_setnam(filename + 3, name_len);
    if (!k_open(2u, DISK_DEVICE, 2u)) {
        k_close(2u);
        return;
    }
    if (!k_chkout(2u) || k_status() != 0u) {
        k_clrchn();
        k_close(2u);
        return;
    }
    while (platform_disk_got < platform_disk_want) {
        k_chrout(platform_disk_buffer[platform_disk_got]);
        if (k_status() != 0u) {
            k_clrchn();
            k_close(2u);
            return;
        }
        ++platform_disk_got;
    }
    k_clrchn();
    k_close(2u);
    wait_drive_ready();
}

static void write_slot_block(void) {
    scratch_slot();
    build_slot_filename();
    write_named(4u);
}

static void write_index_block(void) {
    scratch_index();
    build_index_filename();
    write_named(8u);
}

/* Reads up to platform_disk_want bytes into platform_disk_buffer, stopping
 * early at EOF/error. platform_disk_got is the actual count (0 if the file
 * could not be opened at all). */
__noinline void platform_disk_read_block(void) {
    platform_memory_kernal();
    raster_irq_suspend();
    read_slot_block();
    raster_irq_resume();
    platform_memory_game();
}

__noinline void platform_disk_index_read_block(void) {
    platform_memory_kernal();
    raster_irq_suspend();
    read_index_block();
    raster_irq_resume();
    platform_memory_game();
}

/* Scratches the target file, then writes exactly platform_disk_want bytes
 * from platform_disk_buffer. platform_disk_got is the actual count sent;
 * compare it to platform_disk_want for success. */
__noinline void platform_disk_write_block(void) {
    platform_memory_kernal();
    raster_irq_suspend();
    write_slot_block();
    raster_irq_resume();
    platform_memory_game();
}

__noinline void platform_disk_index_write_block(void) {
    platform_memory_kernal();
    raster_irq_suspend();
    write_index_block();
    raster_irq_resume();
    platform_memory_game();
}

#pragma code(code)
#pragma data(data)
