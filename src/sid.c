#include "sid.h"

#pragma code-name (push, "PRETEXT")

void sid_set_volume(uint8_t vol0_15) {
    /* Keep only low 4 bits; leave filter bits at 0 */
    SID(SID_VOL_FILT) = (vol0_15 & 0x0F);
}

void sid_voice1_tone(uint16_t freq, uint16_t pulse_width) {
    SID(SID_V1_FREQ_LO) = (uint8_t)(freq & 0xFF);
    SID(SID_V1_FREQ_HI) = (uint8_t)(freq >> 8);

    SID(SID_V1_PW_LO)   = (uint8_t)(pulse_width & 0xFF);
    SID(SID_V1_PW_HI)   = (uint8_t)((pulse_width >> 8) & 0x0F);

    /* AD/SR: simple envelope */
    SID(SID_V1_AD) = 0x12; /* attack=1, decay=2 */
    SID(SID_V1_SR) = 0xF2; /* sustain=F, release=2 */
}

void sid_voice1_gate_square(uint8_t on) {
    if (on) {
        SID(SID_V1_CTRL) = SID_GATE | SID_SQUARE;
    } else {
        SID(SID_V1_CTRL) = 0x00;
    }
}

#pragma code-name (pop)
