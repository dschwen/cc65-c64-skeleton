#include <stdint.h>
#include "memmap.h"
#include "banks.h"

/*
 * Environment module: rain (visual sprites, existing mechanism) unified
 * with rain-bed/droplet/footstep sound. Used by rooms 00 and 01 (see
 * env_banks.h). Copied verbatim to ENVCODE_BASE by game_enter_room() -
 * see memmap.h's EnvTable comment for the fixed-address function-pointer
 * table every module starts with, and why this must be reserved,
 * always-RAM space rather than a banked call (the raster IRQ calls the
 * tick vector every frame and can never bank-switch to reach it).
 *
 * Structural translation of rooms/env/00.s (verified identical to
 * rooms/env/01.s) - the exact SID register sequences and their ordering/
 * timing comments (gate-low holds, filter avoidance) are preserved
 * verbatim; this is tuned analog-audio behavior, not something to
 * "clean up" while porting.
 */

#pragma section( env_rain_code, 0 )
#pragma section( env_rain_data, 0 )
#pragma section( env_rain_entry, 0 )
#pragma region( env_rain_bank, 0x8000, 0x8200, , BANK_ENV_RAIN,
                 { env_rain_entry, env_rain_data, env_rain_code }, 0x7e00 )
#pragma code(env_rain_code)
#pragma data(env_rain_data)

void platform_rain_enable(void (*setup)(void));
void platform_rain_disable(void);

#define SID ((volatile uint8_t*)0xd400u)
#define V1_FREQ_LO SID[0]
#define V1_FREQ_HI SID[1]
#define V1_CTRL    SID[4]
#define V1_AD      SID[5]
#define V1_SR      SID[6]
#define V2_FREQ_LO SID[7]
#define V2_FREQ_HI SID[8]
#define V2_CTRL    SID[11]
#define V2_AD      SID[12]
#define V2_SR      SID[13]
#define V3_FREQ_LO SID[14]
#define V3_FREQ_HI SID[15]
#define V3_CTRL    SID[18]
#define V3_OSC     SID[27]
#define MODE_VOL   SID[24]

#define VIC_SPRITE_POINTERS ((uint8_t*)0xfbf8u)
#define VIC ((volatile uint8_t*)0xd000u)
#define RAIN_BITMAP_POINTER ((uint8_t)((0xfd80u - 0xc000u) / 64u))
#define RAIN_SPRITE_MASK     0xfeu /* sprites 1-7 */

static uint8_t raintimer;
static uint8_t randbyte;
static uint8_t stepcd;
static uint8_t stepgate;
static uint8_t footstep_pending;

static void sprite_setup(void) {
    uint8_t i;
    for (i = 1u; i < 8u; ++i) {
        VIC_SPRITE_POINTERS[i] = RAIN_BITMAP_POINTER;
        VIC[0x27 + i] = 6u; /* dark blue */
    }
    VIC[0x17] &= (uint8_t)~RAIN_SPRITE_MASK; /* no Y-expand */
    VIC[0x1b] |= RAIN_SPRITE_MASK;           /* priority: in front */
    VIC[0x1d] &= (uint8_t)~RAIN_SPRITE_MASK; /* no X-expand */
    VIC[0x15] |= RAIN_SPRITE_MASK;           /* enable */
}

static void sound_init(void) {
    uint8_t i;
    uint16_t delay;

    for (i = 0u; i <= 0x18u; ++i) SID[i] = 0u;

    /* A SID voice left gated on and held at sustain (as room 02's tavern
     * melody leaves this voice) does not restart from a gate low/high
     * pair too close together - hold the gate low across a couple of
     * frames first. See rooms/env/00.s's original comment for the
     * real-hardware measurement that found this. */
    for (delay = 0u; delay < 0x2000u; ++delay) {
    }

    V3_CTRL = 0x80u;
    V3_FREQ_LO = 0xffu;
    V3_FREQ_HI = 0xffu;

    V1_FREQ_LO = 0x00u;
    V1_FREQ_HI = 0xc0u;
    V1_AD = 0x09u;
    V1_SR = 0x58u;
    V1_CTRL = 0x81u;

    V2_FREQ_LO = 0x00u;
    V2_FREQ_HI = 0xe0u;
    V2_AD = 0x00u;
    V2_SR = 0x00u;
    V2_CTRL = 0x80u;

    /* Never routed through the filter - see the original's comment on
     * 6581 filtered-voice stuck-signal quirks after a room 02 visit. */
    MODE_VOL = 0x0fu;

    raintimer = 0x20u;
    stepcd = 0u;
    stepgate = 0u;
    footstep_pending = 0u;
}

__noinline void env_rain_init(void) {
    platform_rain_enable(sprite_setup);
    sound_init();
}

__noinline void env_rain_tick(void) {
    randbyte = V3_OSC;

    if (--raintimer == 0u) {
        if (stepgate == 0u) V2_CTRL = 0x81u;
        raintimer = (uint8_t)((randbyte & 0x1fu) + 4u);
    } else if (raintimer >= 0x1eu && stepgate == 0u) {
        V2_CTRL = 0x80u;
    }

    if (footstep_pending) {
        footstep_pending >>= 1;
        if (stepcd == 0u) {
            V2_AD = 0x04u;
            V2_SR = 0xc6u;
            V2_CTRL = 0x80u;
            V2_CTRL = 0x81u;
            stepgate = 0x08u;
            stepcd = 0x0cu;
        }
    }

    if (stepcd != 0u) --stepcd;
    if (stepgate != 0u) {
        if (--stepgate == 0u) {
            V2_CTRL = 0x80u;
            V2_AD = 0x00u;
            V2_SR = 0x00u;
        }
    }
}

__noinline void env_rain_enable(void) {
    platform_rain_enable(0);
    MODE_VOL = 0x0fu;
}

__noinline void env_rain_disable(void) {
    platform_rain_disable();
    MODE_VOL = 0x00u;
}

/* Fixed ABI: the entry table precedes all music/constants in the blob. */
#pragma data(env_rain_entry)
static const EnvTable env_table = { env_rain_init, env_rain_tick, env_rain_enable, env_rain_disable };
#pragma reference(env_table)

#pragma code(code)
#pragma data(data)
