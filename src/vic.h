#pragma once
#include <stdint.h>

/* VIC-II base address: $D000 */
#define VIC_BASE ((volatile uint8_t*)0xD000)

/* Convenience macros */
#define VIC(reg) (VIC_BASE[(reg)])

/* Common registers (offsets from $D000) */
#define VIC_RASTER   VIC(0x12)   /* $D012 */
#define VIC_BORDER   VIC(0x20)   /* $D020 */
#define VIC_BG0      VIC(0x21)   /* $D021 */

void vic_set_border(uint8_t color);
void vic_set_bg(uint8_t which, uint8_t color);
