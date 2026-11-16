#include <stdint.h>
#include "memmap.h"
#include "banks.h"

/*
 * Environment module: a looping 3-voice medieval-tavern melody (D Dorian,
 * no rain/no sprites). Used by room 02. See rooms/env/rain.c's header
 * comment for the shared module conventions; ported from the cc65-era
 * hand-written assembly module.
 *
 * Voice 1 (lead, triangle) plays a 12-step melody, one note per beat;
 * voice 2 (bass, sawtooth) and voice 3 (pad, triangle, an open fifth
 * above the bass root) change once per 3-beat bar. All three voices hold
 * their gate on continuously from env_tavern_init onward - pitch changes are
 * legato (no per-note re-attack).
 */

#pragma section( env_tavern_code, 0 )
#pragma section( env_tavern_data, 0 )
#pragma section( env_tavern_entry, 0 )
#pragma region( env_tavern_bank, 0x8000, 0x8200, , BANK_ENV_TAVERN,
                 { env_tavern_entry, env_tavern_data, env_tavern_code }, 0x7e00 )
#pragma code(env_tavern_code)
#pragma data(env_tavern_data)

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
#define MODE_VOL   SID[24]

#define STEP_COUNT 12u
#define BAR_COUNT 4u
#define STEP_FRAMES_SHORT 40u
#define STEP_FRAMES_LONG 80u

static uint8_t step_timer;
static uint8_t step_index;
static uint8_t beat_in_bar;
static uint8_t bar_index;

/* D Dorian 4-bar phrase, 12 melody steps (D4 F4 A4 | G4 F4 E4 | D4 E4 F4 |
 * E4 D4 D4-held). SID 16-bit frequencies at PAL clock, equal temperament. */
static const uint8_t melody_lo[12] = {0x89,0x3B,0x45,0x13,0x3B,0xED,0x89,0xED,0x3B,0xED,0x89,0x89};
static const uint8_t melody_hi[12] = {0x13,0x17,0x1D,0x1A,0x17,0x15,0x13,0x15,0x17,0x15,0x13,0x13};
static const uint8_t bass_lo[4] = {0xC4,0xB4,0xC4,0x51};
static const uint8_t bass_hi[4] = {0x09,0x08,0x09,0x07};
static const uint8_t pad_lo[4] = {0xA2,0x0A,0xA2,0xF7};
static const uint8_t pad_hi[4] = {0x0E,0x0D,0x0E,0x0A};

__noinline void env_tavern_init(void) {
    uint8_t i;
    for (i = 0u; i <= 0x18u; ++i) SID[i] = 0u;

    /* A SID voice left gated on and held at sustain (as this room's own
     * V1/V2 do, and as rooms 03/05's muffled melody - reached from here,
     * and reachable back here - also does for its V1/V2) does not restart
     * from a gate low/high pair too close together: the clrsid loop above
     * and each voice's CTRL write below are otherwise only ~100 cycles
     * apart, not enough - the envelope stays at zero and that voice is
     * silent until the next room switch re-gates it. Hold the gate low
     * across a couple of frames first. Real-hardware-measured; see
     * rooms/env/rain.c's sound_init() comment for the isolated-audio-capture
     * finding. This environment never had the delay at all (nor did the cc65-era
     * assembly it was ported from) - the
     * intermittent "one voice disappears" moving from the kitchen back to
     * the tavern is this bug, not a port regression.
     *
     * Plain C (`for (delay = 0u; delay < 0x2000u; ++delay) { }`, even with
     * `delay` declared `volatile`) does not survive Oscar64 -Os here: with
     * nothing else observing `delay`, the whole loop is dead code and gets
     * removed - confirmed live (disassembly showed the clrsid loop
     * immediately followed by the first voice's register writes, no delay
     * at all) despite `volatile` normally being exactly the tool for this.
     * rooms/env/rain.c and rooms/env/tavern_muffled.c had the same plain-C
     * loop and were *also* silently losing it - fixed alongside this one.
     * Raw asm, unrelated to any C-level dead-code analysis, is what
     * actually survives: 0x20 * 0x100 = 0x2000 iterations, the
     * cc65-era rain module's own gatelow/gatelow_inner loop. */
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
    V1_CTRL = 0x11u; /* triangle + gate */

    V2_AD = 0x28u; V2_SR = 0xc0u;
    V2_FREQ_LO = bass_lo[0]; V2_FREQ_HI = bass_hi[0];
    V2_CTRL = 0x21u; /* sawtooth + gate */

    V3_AD = 0x38u; V3_SR = 0xa0u;
    V3_FREQ_LO = pad_lo[0]; V3_FREQ_HI = pad_hi[0];
    V3_CTRL = 0x11u; /* triangle + gate */

    MODE_VOL = 0x0fu;

    step_timer = STEP_FRAMES_SHORT;
    step_index = 0u;
    beat_in_bar = 0u;
    bar_index = 0u;
}

__noinline void env_tavern_tick(void) {
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
        V3_FREQ_LO = pad_lo[bar_index];
        V3_FREQ_HI = pad_hi[bar_index];
    }
}

__noinline void env_tavern_enable(void) {
    MODE_VOL = 0x0fu;
}

__noinline void env_tavern_disable(void) {
    MODE_VOL = 0x00u;
}

/* Fixed ABI: the entry table precedes all music/constants in the blob. */
#pragma data(env_tavern_entry)
static const EnvTable env_table = { env_tavern_init, env_tavern_tick, env_tavern_enable, env_tavern_disable };
#pragma reference(env_table)

#pragma code(code)
#pragma data(data)
