#include <stdint.h>
#include "memmap.h"
#include "banks.h"
#include "platform_native.h"

/*
 * Environment module: an ominous cellar ambience instead of music - a
 * slow low drone, a sparse minor-key pluck at irregular intervals, and an
 * occasional noise "creak". Used by room 04. Ported from the cc65-era
 * hand-written assembly module.
 *
 * Voice 1 (drone, triangle): holds one note for a long stretch, then
 * glides to the next - alternates D3/A2 (a bare fifth), never re-gated
 * once started, so it reads as continuous rather than re-plucked.
 * Voice 2 (pluck, triangle): sustain 0, so every gate-on decays back to
 * silence on its own - interval/pitch both jittered from voice 3's own
 * oscillator output. Voice 3 (creak, noise): kept running silently at all
 * times purely so its oscillator output is a free hardware noise source;
 * occasionally gated on for a short burst as the audible "creak".
 */

#pragma section( env_cellar_code, 0 )
#pragma section( env_cellar_data, 0 )
#pragma section( env_cellar_entry, 0 )
#pragma region( env_cellar_bank, 0x8000, 0x8200, , BANK_ENV_CELLAR,
                 { env_cellar_entry, env_cellar_data, env_cellar_code }, 0x7e00 )
#pragma code(env_cellar_code)
#pragma data(env_cellar_data)

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
#define V3_AD      SID[19]
#define V3_SR      SID[20]
#define V3_OSC     SID[27]
#define MODE_VOL   SID[24]

/* D3 and A2 (a bare fifth) - same equal-tempered SID values room 02's
 * tavern tune uses for its bass, reused here for tonal continuity. */
static const uint8_t drone_lo[2] = {0xC4, 0x51};
static const uint8_t drone_hi[2] = {0x09, 0x07};
#define DRONE_HOLD_FRAMES 220u /* ~4.4s at 50Hz between drone changes */

/* A small D-minor-ish pluck palette (D4, E4, F4, A3). */
static const uint8_t pluck_lo[4] = {0x89, 0xED, 0x3B, 0xA2};
static const uint8_t pluck_hi[4] = {0x13, 0x15, 0x17, 0x0E};
#define PLUCK_COUNT 4u

static uint8_t drone_timer;
static uint8_t drone_index;
static uint8_t pluck_timer;
static uint8_t creak_timer;
static uint8_t randbyte;

__noinline void env_cellar_init(void) {
    uint8_t i;
    uint16_t delay;

    for (i = 0u; i <= 0x18u; ++i) SID[i] = 0u;
    for (i = 0u; i < 7u; ++i) platform_sid_v3[i] = 0u;

    for (delay = 0u; delay < 0x2000u; ++delay) {
    }

    V1_AD = 0x08u; /* attack 0, decay 8 - slow fade into each note */
    V1_SR = 0x84u; /* sustain 8 (quieter than full), release 4 */
    V1_FREQ_LO = drone_lo[0]; V1_FREQ_HI = drone_hi[0];
    V1_CTRL = 0x11u; /* triangle + gate, held for the whole visit */

    V2_AD = 0x01u; /* quick pluck attack */
    V2_SR = 0x06u; /* sustain 0 (self-silences), release 6 */
    V2_CTRL = 0x10u; /* triangle, gate off until the first pluck */

    SID_V3_WRITE(19u, 0x00u);
    SID_V3_WRITE(20u, 0x0au); /* sustain 0, release 10 - short creak */
    SID_V3_WRITE(14u, 0xffu); /* high, hiss-like - also the free noise source */
    SID_V3_WRITE(15u, 0xffu);
    SID_V3_WRITE(18u, 0x80u); /* noise, gate off - keeps the oscillator running */

    MODE_VOL = 0x07u; /* quiet, but present */

    drone_timer = DRONE_HOLD_FRAMES;
    drone_index = 0u;
    pluck_timer = 90u;
    creak_timer = 150u;
}

__noinline void env_cellar_tick(void) {
    randbyte = V3_OSC;

    if (--drone_timer == 0u) {
        drone_timer = DRONE_HOLD_FRAMES;
        drone_index ^= 1u;
        V1_FREQ_LO = drone_lo[drone_index];
        V1_FREQ_HI = drone_hi[drone_index];
    }

    if (--pluck_timer == 0u) {
        uint8_t index;
        pluck_timer = (uint8_t)((randbyte & 0x7fu) + 0x50u); /* ~1.6-4.1s */
        index = (uint8_t)((randbyte >> 2) & (PLUCK_COUNT - 1u));
        V2_FREQ_LO = pluck_lo[index];
        V2_FREQ_HI = pluck_hi[index];
        /* Gate low first - the envelope generator only retriggers attack
         * on a 0-to-1 gate edge, and the previous pluck's gate bit is
         * still set (sustain 0 only silences the output, not the latch). */
        V2_CTRL = 0x10u;
        V2_CTRL = 0x11u;
    }

    if (--creak_timer == 0u) {
        /* Stays under 256 so this 8-bit add can't wrap. */
        creak_timer = (uint8_t)((randbyte & 0x7fu) + 0x64u); /* ~2-4.5s */
        SID_V3_WRITE(18u, 0x80u); /* gate low first, same 0-to-1 edge requirement */
        SID_V3_WRITE(18u, 0x81u); /* noise + gate: brief burst, sustain 0 self-decays */
    }
}

__noinline void env_cellar_enable(void) {
    MODE_VOL = 0x07u;
}

__noinline void env_cellar_disable(void) {
    MODE_VOL = 0x00u;
}

/* Fixed ABI: the entry table precedes all music/constants in the blob. */
#pragma data(env_cellar_entry)
static const EnvTable env_table = { env_cellar_init, env_cellar_tick, env_cellar_enable, env_cellar_disable };
#pragma reference(env_table)

#pragma code(code)
#pragma data(data)
