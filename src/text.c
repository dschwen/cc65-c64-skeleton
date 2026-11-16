#include <stdint.h>
#include "memmap.h"
#include "platform.h"
#include "platform_native.h"

/* Word-wrapping pager for the two bottom status rows. KERNAL stays mapped
 * during gameplay: writes reach screen RAM, but scrolling reads need the
 * resident VIC-RAM gate. */

extern uint8_t platform_text_output_color;
extern uint8_t platform_text_output_line;

#define TEXT_ROW_0  (SCREEN_RAM + 23u * MAP_WIDTH_CHARS)
#define TEXT_ROW_1  (SCREEN_RAM + 24u * MAP_WIDTH_CHARS)
#define COLOR_ROW_0 (COLOR_RAM  + 23u * MAP_WIDTH_CHARS)
#define COLOR_ROW_1 (COLOR_RAM  + 24u * MAP_WIDTH_CHARS)

static uint8_t text_column;

/* Blocks until a keypress that wasn't already held when this was called -
 * release (drain whatever's buffered), press, release again. The one
 * resident copy; the pager, the script interpreter and the transition
 * reveal all share it. */
void platform_wait_fresh_key(void) {
    while (platform_input_poll() != 0u) platform_wait_frame();
    while (platform_input_poll() == 0u) platform_wait_frame();
    while (platform_input_poll() != 0u) platform_wait_frame();
}

static void clear_text_rows(void) {
    uint8_t x;
    for (x = 0u; x < MAP_WIDTH_CHARS; ++x) {
        TEXT_ROW_0[x] = ' ';
        TEXT_ROW_1[x] = ' ';
        COLOR_ROW_0[x] = 0u;
        COLOR_ROW_1[x] = 0u;
    }
}

static void next_text_line(void) {
    uint8_t x;
    text_column = 0u;
    if (platform_text_output_line == 0u) {
        ++platform_text_output_line;
        return;
    }
    platform_wait_fresh_key();
    platform_copy_vic_ram(TEXT_ROW_0, TEXT_ROW_1, MAP_WIDTH_CHARS);
    for (x = 0u; x < MAP_WIDTH_CHARS; ++x) {
        COLOR_ROW_0[x] = COLOR_ROW_1[x];
        TEXT_ROW_1[x] = ' ';
        COLOR_ROW_1[x] = 0u;
    }
}

/* Same PETSCII conversion as platform_text_screen_code(). */
static void write_character(uint8_t ch) {
    if (ch >= 0x41u && ch < 0x5bu) {
        ch = (uint8_t)(ch - 0x40u);
    } else if (ch >= 0xc1u && ch < 0xdbu) {
        ch = (uint8_t)(ch - 0x80u);
    }
    if (platform_text_output_line == 0u) {
        TEXT_ROW_0[text_column] = ch;
        COLOR_ROW_0[text_column] = platform_text_output_color;
    } else {
        TEXT_ROW_1[text_column] = ch;
        COLOR_ROW_1[text_column] = platform_text_output_color;
    }
    ++text_column;
}

/* text points at a zero-terminated PETSCII string. Words wrap at 40
 * columns. Once both status rows are occupied, wait for a fresh keypress,
 * scroll the lower row upward, and continue on the cleared lower row. */
void platform_text_output_native(const char* text) {
    const uint8_t* src = (const uint8_t*)text;
    text_column = 0u;
    clear_text_rows();

    while (*src != 0u) {
        uint8_t ch = *src;
        if (ch == ' ') {
            ++src;
            if (text_column != 0u) {
                if (text_column >= MAP_WIDTH_CHARS) {
                    next_text_line();
                } else {
                    write_character(' ');
                }
            }
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            ++src;
            next_text_line();
            continue;
        }

        {
            /* Look ahead to the next delimiter; 40-byte chunks also make a
             * word longer than one screen line wrap without a word buffer. */
            const uint8_t* measure = src;
            uint8_t word_length = 0u;
            while (*measure != 0u && *measure != ' ' && *measure != '\n' &&
                   *measure != '\r' && word_length < MAP_WIDTH_CHARS) {
                ++measure;
                ++word_length;
            }
            if (text_column != 0u && text_column + word_length > MAP_WIDTH_CHARS) {
                next_text_line();
            }
            while (word_length != 0u) {
                write_character(*src++);
                --word_length;
            }
        }
    }
}
