#include <stdint.h>
#include <c64/memmap.h>
#include "memmap.h"
#include "platform.h"
#include "platform_native.h"

/*
 * Split-screen raster IRQ (ported from the cc65-era hand-written 6502):
 * switches between the tile and text charsets partway down the frame,
 * drives the frame counter, and animates water/rain. Structural translation
 * (not a rewrite) of the original hand-written 6502 - this code runs with
 * interrupts masked on real hardware timing, so behavior must match
 * exactly, not just "equivalently".
 */

#define VIC_CTRL1      (*(volatile uint8_t*)0xd011u)
#define VIC_RASTER     (*(volatile uint8_t*)0xd012u)
#define VIC_MEMPTR     (*(volatile uint8_t*)0xd018u)
#define VIC_IRQ_STATUS (*(volatile uint8_t*)0xd019u)
#define VIC_IRQ_ENABLE (*(volatile uint8_t*)0xd01au)
#define VIC_SPR_ENABLE (*(volatile uint8_t*)0xd015u)
#define CIA1_IRQ       (*(volatile uint8_t*)0xdc0du)

#define TILE_MEMPTR    0xeau  /* bank 3 screen $F800, charset $E800 */
#define TEXT_MEMPTR    0xecu  /* bank 3 screen $F800, charset $F000 */
#define TEXT_RASTER    226u   /* one line before row 22's badline */
#define WATER_CHAR     ((uint8_t*)(0xe800u + 14u * 8u))
#define RAIN_BITMAP    ((uint8_t*)0xfd80u)
#define RAIN_MASK      0xfeu  /* sprites 1-7; portraits borrow 1-5 */
#define RAIN_COUNT     7u
#define RAIN_STEP      20u
#define RAIN_X_OFFSET  23u
#define RAIN_X_MAX_LO  87u
#define RAIN_Y_MAX     176u

/* Every byte touched by the IRQ must stay outside $8000-$BFFF (the
 * EasyFlash bank window) so it stays visible regardless of which bank is
 * paged in - matches the original's LOWBSS placement intent. Ordinary
 * globals here already satisfy that: they compile into `main`
 * ($0901-$8000), never a banked region. */
static uint8_t text_screen_active;
volatile uint8_t platform_raster_irq_active;
volatile uint8_t platform_frame_counter;
static uint8_t rain_active;
static uint8_t rain_x_lo[RAIN_COUNT + 1u];
static uint8_t rain_x_hi[RAIN_COUNT + 1u];
static uint8_t rain_y[RAIN_COUNT + 1u];
static uint8_t rain_seed;
static void (*rain_setup_hook)(void);
static uint8_t water_bitmap[8];
static uint8_t raster_irq_suspend_depth;
static uint8_t raster_irq_saved_enable;

static void weather_animate(void);

/* Rewrite the RAM vectors after loading type 255 (platform_object_types_
 * load()'s KERNAL disk gate temporarily maps KERNAL ROM back in, which
 * shadows these RAM vectors at $FFFA-$FFFF while it's mapped, but stores
 * still reach the underlying RAM). */
static void raster_irq_vectors_restore_impl(void);

/* 8-bit Galois LFSR; cheap PRNG for streak respawn placement. */
static uint8_t rain_prng(void) {
    uint8_t carry = (rain_seed & 0x80u) != 0u;
    rain_seed = (uint8_t)(rain_seed << 1);
    if (carry) rain_seed ^= 0x1du;
    return rain_seed;
}

/* Returns a uniformly random sprite-X position in [23,342] (the map's full
 * pixel width): *out_hi = 0/1 (9th bit), returns rain_x_lo. 320 isn't a
 * power of two, so this draws a 9-bit value from two independent sources
 * and rejects the 192 values outside the target range. The coin flip
 * comes from platform_frame_counter, not a second rain_prng() call, so the
 * two sources stay independent. */
static uint8_t rain_pick_x(uint8_t* out_hi) {
    for (;;) {
        uint8_t candidate = rain_prng();
        if (platform_frame_counter & 1u) {
            if (candidate < RAIN_X_OFFSET + 64u) {
                *out_hi = 1u;
                return candidate;
            }
        } else {
            if (candidate >= RAIN_X_OFFSET) {
                *out_hi = 0u;
                return candidate;
            }
        }
    }
}

static void rain_hide_sprites(void) {
    VIC_SPR_ENABLE &= (uint8_t)~RAIN_MASK;
}

/* Advance all seven streaks by (RAIN_STEP,RAIN_STEP) and write the result
 * straight to sprite X's own VIC registers. Any streak leaving the screen
 * respawns on the top or left edge. */
static void rain_advance(void) {
    volatile uint8_t* vic = (volatile uint8_t*)0xd000u;
    uint8_t x;
    static const uint8_t bit_mask[8] = {0x00u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u};

    if (!rain_active) return;
    for (x = 1u; x <= RAIN_COUNT; ++x) {
        uint8_t new_y = (uint8_t)(rain_y[x] + RAIN_STEP);
        uint8_t respawn = new_y < rain_y[x]; /* carry out of the 8-bit add */
        rain_y[x] = new_y;
        if (!respawn && new_y >= RAIN_Y_MAX) respawn = 1u;
        if (!respawn) {
            uint8_t new_lo = (uint8_t)(rain_x_lo[x] + RAIN_STEP);
            if (new_lo < rain_x_lo[x]) rain_x_hi[x] = 1u; /* carry into hi bit */
            rain_x_lo[x] = new_lo;
            if (rain_x_hi[x] != 0u && rain_x_lo[x] >= RAIN_X_MAX_LO) respawn = 1u;
        }
        if (respawn) {
            uint8_t roll = rain_prng();
            if (roll & 0x80u) {
                /* Spawn on the left edge. */
                rain_y[x] = roll & 0x7fu;
                rain_x_lo[x] = 0u;
                rain_x_hi[x] = 0u;
            } else {
                uint8_t hi;
                uint8_t lo = rain_pick_x(&hi);
                rain_x_hi[x] = hi;
                rain_x_lo[x] = lo;
                rain_y[x] = 0u;
            }
        }
        if (rain_x_hi[x]) {
            vic[0x10] |= bit_mask[x];
        } else {
            vic[0x10] &= (uint8_t)~bit_mask[x];
        }
        vic[x * 2u] = rain_x_lo[x];
        vic[x * 2u + 1u] = rain_y[x];
    }
}

/* Set by raster_body() at the top-of-text-phase raster event, consumed by
 * platform_weather_service() from foreground code - see that function's
 * own comment for why this indirection exists. */
static volatile uint8_t weather_tick_pending;

/* Advance the visual rain, then tail-call the current room's environment
 * module's per-frame tick. Always safe unconditionally: game_enter_room()
 * installs a no-op stub there for any room without one. */
static void rain_advance_and_tick(void) {
    rain_advance();
    ENVCODE_TICK();
}

/* Animate water once every other frame and advance rain every frame. */
static void weather_animate(void) {
    if (!(platform_frame_counter & 1u)) {
        uint8_t i;
        for (i = 0u; i < 8u; ++i) {
            uint8_t carry = (water_bitmap[i] & 0x80u) != 0u;
            water_bitmap[i] = (uint8_t)(water_bitmap[i] << 1) | (carry ? 1u : 0u);
            WATER_CHAR[i] = water_bitmap[i];
        }
    }
    rain_advance_and_tick();
}

/* Runs weather_animate() from foreground code once per frame, called from
 * platform_wait_frame() right after it returns. weather_animate() ends in
 * ENVCODE_TICK(), an indirect call through a function pointer whose target
 * (whichever room's env module is currently loaded) oscar64's static
 * interrupt-safety analysis cannot prove non-recursive when reached from
 * the raster IRQ - confirmed live ("No recursive functions in interrupt"),
 * even with every function in the chain marked __interrupt. Deferring the
 * actual tick to foreground code, with the IRQ only setting a flag, avoids
 * the restriction entirely rather than fighting it. Timing effect: the
 * tick now runs at most one frame's worth of foreground work after the
 * raster event that used to trigger it directly, rather than exactly at
 * that raster line - immaterial here, since every env module's own timers
 * already count in whole frames, not raster lines, and platform_wait_
 * frame() is what every frame-paced loop in this codebase already blocks
 * on. Not called while suspended (raster_irq_suspend()) or before the
 * first frame after boot, matching the original's behavior of no weather
 * ticks running during a room transition. */
void platform_weather_service(void) {
    if (weather_tick_pending) {
        weather_tick_pending = 0u;
        weather_animate();
    }
}

/* $D011 bit 7 means two different things depending on direction: on a READ
 * it's the raster's bit 8; on a WRITE it's bit 8 of the raster-IRQ compare
 * value. Every $D011 write here masks that bit off first, matching the
 * original's explicit discipline (see MEMORY_MAP.md's write-up of the bug
 * this avoids). */
/* __interrupt (not a plain function): called from two different interrupt
 * entry points (direct_irq_entry, kernal_irq_entry), and oscar64's static
 * frame allocator refuses to share an ordinary function's frame across
 * two interrupt contexts it can't prove are mutually exclusive ("No
 * recursive functions in interrupt" - confirmed live). __interrupt makes
 * every call save/restore its own zeropage usage, which is what actually
 * makes sharing this safe. */
static __interrupt void raster_body(void) {
    VIC_IRQ_STATUS = 0x01u;

    if (VIC_CTRL1 & 0x80u) {
        goto switch_to_text;
    }
    if (VIC_RASTER == 0u) {
        goto top_of_frame;
    }
    if (VIC_RASTER < TEXT_RASTER) {
        goto top_of_frame;
    }
    if (VIC_RASTER >= TEXT_RASTER + 1u) {
        goto switch_to_text;
    }
    /* Entering partway through line 226: wait for the start of 227. */
    while (VIC_RASTER != TEXT_RASTER + 1u) {
    }

switch_to_text:
    VIC_MEMPTR = TEXT_MEMPTR;
    VIC_CTRL1 = (uint8_t)(VIC_CTRL1 & 0x7fu);
    VIC_RASTER = 0u;
    if (rain_active) rain_hide_sprites();
    ++platform_frame_counter;
    weather_tick_pending = 1u;
    return;

top_of_frame:
    if (text_screen_active) {
        VIC_MEMPTR = TEXT_MEMPTR;
    } else {
        VIC_MEMPTR = TILE_MEMPTR;
        if (rain_active) VIC_SPR_ENABLE |= RAIN_MASK;
    }
    VIC_CTRL1 = (uint8_t)(VIC_CTRL1 & 0x7fu);
    VIC_RASTER = TEXT_RASTER;
}

/* Hardware IRQ entry ($FFFE). A second entry point at $0314 supports
 * periods when KERNAL disk code temporarily maps KERNAL ROM back in and
 * chains through the KERNAL's own IRQ exit instead of RTI directly. */
__hwinterrupt void direct_irq_entry(void) {
    raster_body();
}

__hwinterrupt void direct_nmi_entry(void) {
    /* Acknowledge a possible CIA2 NMI. A `volatile` read whose value is
     * discarded still gets dead-code-eliminated by oscar64's optimizer
     * (confirmed live: the LDA never made it into the compiled output),
     * so the CIA's latched interrupt flag was never actually cleared by
     * reading its ICR - raw asm is the only way to force the load to
     * happen for its hardware side effect. */
    __asm { lda $dd0d }
}


__interrupt void kernal_irq_entry(void) {
    raster_body();
    /* Fall through to the KERNAL's own IRQ exit ($EA81): restores A/X/Y
     * from its own stack frame (already pushed by the KERNAL's hardware
     * IRQ entry before it dispatched through $0314). This exit does not
     * scan the keyboard or update the jiffy clock.
     *
     * Must be a real JMP, not a call: $EA81 ends in RTI, using the stack
     * frame the original hardware interrupt pushed. A `jmp` mnemonic here
     * gets silently rewritten to `jsr` by the compiler (verified - even
     * under `__asm volatile`) since a JMP can't reach this function's own
     * implied RTS epilogue; that epilogue is unreachable here regardless
     * (this call never returns), so the raw JMP opcode bytes below are the
     * correct, deliberate escape hatch, not a workaround for a mistake.
     */
    __asm {
        byt 0x4c
        byt 0x81
        byt 0xea
    }
}

/* Installs only the NMI handler, through the KERNAL's RAM vector at $0318
 * (its hardware NMI entry at $FE43 jumps through it). main() calls this
 * before the title screen, which runs ahead of platform_init() with the
 * KERNAL's own NMI handler otherwise still live: RUN/STOP+RESTORE there
 * would warm-start into BASIC. The game's own handler just acknowledges
 * CIA2 and returns. KERNAL RESTOR in platform_init() resets $0318 again;
 * raster_irq_install() re-points it with the rest of the vectors. */
void platform_nmi_install(void) {
    *(void**)0x0318u = direct_nmi_entry;
}

static void raster_irq_vectors_restore_impl(void) {
    *(void**)0xfffau = direct_nmi_entry;
    /* $FFFC (reset vector) is intentionally left as the KERNAL's own -
     * matches the original, which restores NMI/IRQ but resets through the
     * KERNAL cold-start ($FCE2) directly rather than a custom entry. */
    *(void**)0xfffeu = direct_irq_entry;
    /* KERNAL's hardware NMI entry SEIs and jumps through $0318 before
     * pushing any registers. The hardware entry works for both mappings. */
    *(void**)0x0318u = direct_nmi_entry;
}

static void initialize_vic_assets(void) {
    uint8_t i, x, mask;
    platform_copy_vic_ram(water_bitmap, WATER_CHAR, 8u);
    for (i = 0u; i < 64u; ++i) RAIN_BITMAP[i] = 0u;
    x = 0u;
    mask = 0x80u;
    for (i = 0u; i < 21u; ++i) {
        RAIN_BITMAP[x] = mask;
        x = (uint8_t)(x + 3u);
        mask = (uint8_t)(mask >> 1);
        if (mask == 0u) {
            mask = 0x80u;
            ++x;
        }
    }
}

void raster_irq_install(void) {
    __asm { sei }
    initialize_vic_assets();
    platform_frame_counter = 0u;
    text_screen_active = 0u;
    rain_active = 0u;
    raster_irq_suspend_depth = 0u;
    raster_irq_saved_enable = 0u;
    rain_seed = 0xacu; /* an all-zero LFSR state never escapes zero */
    platform_raster_irq_active = 1u;

    /* Own the IRQ source. This intentionally stops the KERNAL jiffy clock.
     * The read-to-ack must be raw asm, not a discarded `volatile` C read -
     * oscar64's optimizer dead-code-eliminates the latter (confirmed live:
     * the LDA never made it into the compiled output - see
     * direct_nmi_entry's comment for the same issue on CIA2). Without it,
     * the KERNAL timer's already-latched CIA1 ICR flag never actually
     * clears; harmless here since the mask write above already stops new
     * CIA1 sources from reaching the CPU, but a real bug in general for
     * any future code that re-enables a CIA1 source and relies on this
     * read to have cleared the stale latch first. */
    CIA1_IRQ = 0x7fu;
    __asm { lda $dc0d }

    *(void**)0x0314u = kernal_irq_entry;

    raster_irq_vectors_restore_impl();

    VIC_CTRL1 = (uint8_t)(VIC_CTRL1 & 0x7fu);
    VIC_RASTER = TEXT_RASTER;

    VIC_IRQ_STATUS = 0x01u;
    VIC_IRQ_ENABLE |= 0x01u;
    __asm { cli }
}

/* Full-screen text users prepare/restore screen RAM themselves; the IRQ
 * keeps using the text charset at line zero until leave restores the map
 * split. */
void platform_text_screen_enter(void) {
    text_screen_active = 1u;
    VIC_MEMPTR = TEXT_MEMPTR;
}

void platform_text_screen_leave(void) {
    text_screen_active = 0u;
    VIC_MEMPTR = TILE_MEMPTR;
}

/* Re-establish the split after a deliberately suspended transaction.
 * Chooses the next event from the VIC's full 9-bit raster position so a
 * pending interrupt cannot leave the text charset over the map for a
 * frame. */
void raster_irq_resync(void) {
    __asm {
        php
        sei
    }
    /* If the text boundary has already passed, schedule raster zero
     * immediately. Waiting for line 227 again here would mask IRQs for
     * an entire frame and leave the text charset over the whole map. */
    if ((VIC_CTRL1 & 0x80u) || VIC_RASTER >= TEXT_RASTER) {
        if (!(VIC_CTRL1 & 0x80u)) {
            while (VIC_RASTER == TEXT_RASTER) {
            }
        }
        VIC_MEMPTR = TEXT_MEMPTR;
        VIC_RASTER = 0u;
    } else {
        VIC_MEMPTR = text_screen_active ? TEXT_MEMPTR : TILE_MEMPTR;
        VIC_RASTER = TEXT_RASTER;
    }
    VIC_CTRL1 = (uint8_t)(VIC_CTRL1 & 0x7fu);
    VIC_IRQ_STATUS = 0x01u;
    __asm { plp }
}

/* Nestable raster-source suspension. Synchronization only: callers own any
 * text clearing or screen blanking. */
void raster_irq_suspend(void) {
    __asm {
        php
        sei
    }
    if (raster_irq_suspend_depth == 0u) {
        raster_irq_saved_enable = VIC_IRQ_ENABLE;
        VIC_IRQ_ENABLE = (uint8_t)(raster_irq_saved_enable & 0xfeu);
        platform_raster_irq_active = 0u;
        VIC_MEMPTR = text_screen_active ? TEXT_MEMPTR : TILE_MEMPTR;
        VIC_IRQ_STATUS = 0x01u;
    }
    ++raster_irq_suspend_depth;
    __asm { plp }
}

void raster_irq_resume(void) {
    __asm {
        php
        sei
    }
    if (raster_irq_suspend_depth != 0u) {
        --raster_irq_suspend_depth;
        if (raster_irq_suspend_depth == 0u) {
            platform_raster_irq_active = (uint8_t)(raster_irq_saved_enable & 0x01u);
            if (platform_raster_irq_active) raster_irq_resync();
            VIC_IRQ_ENABLE = raster_irq_saved_enable;
        }
    }
    __asm { plp }
}

/* setup is a room's rain_setup() callback, or NULL to reuse whichever was
 * last registered (the portrait-resume path's case). */
void platform_rain_enable(void (*setup)(void)) {
    if (setup != 0) rain_setup_hook = setup;
    if (rain_setup_hook != 0) {
        rain_setup_hook();
    }
    platform_rain_activate();
}

void platform_rain_activate(void) {
    uint8_t i;
    rain_active = 1u;
    for (i = 1u; i <= RAIN_COUNT; ++i) {
        rain_y[i] = 255u; /* past the respawn threshold: seeds a fresh position */
    }
    rain_advance();
}

void platform_rain_disable(void) {
    rain_active = 0u;
    VIC_SPR_ENABLE &= (uint8_t)~RAIN_MASK;
}
