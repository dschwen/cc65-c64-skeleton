#ifndef GAME_VIC_H
#define GAME_VIC_H

#include <stdint.h>

#define VIC_BASE ((volatile uint8_t*)0xd000)
#define VIC(reg) (VIC_BASE[(reg)])

#define VIC_RASTER VIC(0x12)
#define VIC_BORDER VIC(0x20)
#define VIC_BG0    VIC(0x21)

void vic_set_border(uint8_t color);
void vic_set_bg(uint8_t which, uint8_t color);

#endif
