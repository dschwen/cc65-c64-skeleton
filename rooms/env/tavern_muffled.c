#include <stdint.h>
#include "memmap.h"
#include "banks.h"

/*
 * Environment module: the tavern's melody (rooms/env/tavern.c) as heard
 * faintly through a wall - same D Dorian tune and tempo, thinner (melody
 * + bass only, no pad voice) and much quieter. Used by rooms 03 (kitchen)
 * and 05 (guest corridor). Ported from the cc65-era
 * hand-written assembly module (rooms 03 and 05 shared one).
 */

#pragma section( env_tavmuf_code, 0 )
#pragma section( env_tavmuf_data, 0 )
#pragma section( env_tavmuf_entry, 0 )
#pragma region( env_tavmuf_bank, 0x8000, 0x8200, , BANK_ENV_TAVERN_MUFFLED,
                 { env_tavmuf_entry, env_tavmuf_data, env_tavmuf_code }, 0x7e00 )
#pragma code(env_tavmuf_code)
#pragma data(env_tavmuf_data)

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
#define MODE_VOL   SID[24]

#define STEP_COUNT 12u
#define BAR_COUNT 4u
#define STEP_FRAMES_SHORT 40u
#define STEP_FRAMES_LONG 80u

static uint8_t step_timer;
static uint8_t step_index;
static uint8_t beat_in_bar;
static uint8_t bar_index;

/* Identical to rooms/env/tavern.c's tables - same tune. */
static const uint8_t melody_lo[12] = {0x89,0x3B,0x45,0x13,0x3B,0xED,0x89,0xED,0x3B,0xED,0x89,0x89};
static const uint8_t melody_hi[12] = {0x13,0x17,0x1D,0x1A,0x17,0x15,0x13,0x15,0x17,0x15,0x13,0x13};
static const uint8_t bass_lo[4] = {0xC4,0xB4,0xC4,0x51};
static const uint8_t bass_hi[4] = {0x09,0x08,0x09,0x07};

__noinline void env_tavern_muffled_init(void) {
    uint8_t i;

    for (i = 0u; i <= 0x18u; ++i) SID[i] = 0u;

    /* Hold gates low for a couple of frames before re-gating - see
     * rooms/env/rain.c's sound_init() for why (a SID voice left gated on
     * and held at sustain, as this room's own V1/V2 do and as room 02's
     * tavern melody also does, does not restart from a gate low/high pair
     * too close together).
     *
     * Raw asm, not a plain C loop: see rooms/env/tavern.c's
     * env_tavern_init() for why a `for` loop here - even over a `volatile`
     * counter - does not survive Oscar64 -Os and was confirmed live to
     * have silently lost this delay entirely. 0x20 * 0x100 = 0x2000
     * iterations, the cc65-era module's own
     * gatelow/gatelow_inner loop. */
    __asm {
        ldx #$20
    gatelow:
        ldy #$00
    gatelow_inner:
        dey
        bne gatelow_inner
        dex
        bne gatelow
    }

    V1_AD = 0x18u; V1_SR = 0xf0u;
    V1_FREQ_LO = melody_lo[0]; V1_FREQ_HI = melody_hi[0];
    V1_CTRL = 0x11u;

    V2_AD = 0x28u; V2_SR = 0xc0u;
    V2_FREQ_LO = bass_lo[0]; V2_FREQ_HI = bass_hi[0];
    V2_CTRL = 0x21u;

    MODE_VOL = 0x03u; /* ~20% of full volume */

    step_timer = STEP_FRAMES_SHORT;
    step_index = 0u;
    beat_in_bar = 0u;
    bar_index = 0u;
}

__noinline void env_tavern_muffled_tick(void) {
    if (--step_timer != 0u) return;

    step_index = (uint8_t)(step_index + 1u == STEP_COUNT ? 0u : step_index + 1u);
    step_timer = step_index == STEP_COUNT - 1u ? STEP_FRAMES_LONG : STEP_FRAMES_SHORT;

    V1_FREQ_LO = melody_lo[step_index];
    V1_FREQ_HI = melody_hi[step_index];

    if (++beat_in_bar == 3u) {
        beat_in_bar = 0u;
        bar_index = (uint8_t)(bar_index + 1u == BAR_COUNT ? 0u : bar_index + 1u);
        V2_FREQ_LO = bass_lo[bar_index];
        V2_FREQ_HI = bass_hi[bar_index];
    }
}

__noinline void env_tavern_muffled_enable(void) {
    MODE_VOL = 0x03u;
}

__noinline void env_tavern_muffled_disable(void) {
    MODE_VOL = 0x00u;
}

/* Fixed ABI: the entry table precedes all music/constants in the blob. */
#pragma data(env_tavmuf_entry)
static const EnvTable env_table = { env_tavern_muffled_init, env_tavern_muffled_tick, env_tavern_muffled_enable, env_tavern_muffled_disable };
#pragma reference(env_table)

#pragma code(code)
#pragma data(data)
