#pragma once
#include <stdint.h>

/* SID base address: $D400 */
#define SID_BASE ((volatile uint8_t*)0xD400)
#define SID(reg) (SID_BASE[(reg)])

/* Voice 1 registers (offsets from $D400) */
#define SID_V1_FREQ_LO  0x00
#define SID_V1_FREQ_HI  0x01
#define SID_V1_PW_LO    0x02
#define SID_V1_PW_HI    0x03
#define SID_V1_CTRL     0x04
#define SID_V1_AD       0x05
#define SID_V1_SR       0x06

#define SID_VOL_FILT    0x18   /* $D418 */

/* Control bits (common) */
#define SID_GATE  0x01
#define SID_SQUARE 0x40

void sid_set_volume(uint8_t vol0_15);

/* Minimal helpers for a simple tone on voice 1 */
void sid_voice1_tone(uint16_t freq, uint16_t pulse_width);
void sid_voice1_gate_square(uint8_t on);
