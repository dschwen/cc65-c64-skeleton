#include "vic.h"

#pragma code-name (push, "HIGHCODE")

void vic_set_border(uint8_t color) {
    VIC_BORDER = (color & 0x0F);
}

void vic_set_bg(uint8_t which, uint8_t color) {
    /* $D021..$D024 are BG0..BG3 */
    if (which > 3) return;
    VIC(0x21 + which) = (color & 0x0F);
}

#pragma code-name (pop)
