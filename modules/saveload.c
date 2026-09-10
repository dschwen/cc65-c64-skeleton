#include <stdint.h>

#include "game.h"
#include "world.h"

/* Independently linked save/load browse overlay ("SL"): slot list, cursor
 * navigation, and the complete Load flow. See SAVE_GAME.md for the on-disk
 * record format and the "Save/load overlay" architecture section, and
 * modules/saveload_save.c for the Save flow (name entry + encode + write),
 * a separate overlay because the two together do not fit this overlay's
 * shared 4 KiB `$B000` RAM window. This file does not link against the cc65
 * runtime library (same constraint as modules/inventory.c): no
 * memcpy/memset, no library string/number formatting, no division/modulo
 * by a non-power-of-two.
 */

#define SAVE_SLOT_COUNT      8u
#define SAVE_NAME_BYTES      16u
#define SAVE_HEADER_BYTES    16u
#define SAVE_PREFIX_BYTES    130u
#define SAVE_DELTA_BYTES     5u
#define SAVE_MAX_DELTAS      200u
#define SAVE_MAX_BYTES       (SAVE_HEADER_BYTES + SAVE_PREFIX_BYTES + \
                              SAVE_MAX_DELTAS * SAVE_DELTA_BYTES)
#define SAVE_PEEK_BYTES      (SAVE_HEADER_BYTES + SAVE_NAME_BYTES)
#define SAVE_FORMAT_VERSION  1u
#define SAVE_INDEX_HEADER_BYTES 10u
#define SAVE_INDEX_ENTRY_BYTES  17u
#define SAVE_INDEX_BYTES \
    (SAVE_INDEX_HEADER_BYTES + SAVE_SLOT_COUNT * SAVE_INDEX_ENTRY_BYTES)
#define SAVE_INDEX_RAM       ((uint8_t*)0xa000)
#define SAVE_RECORD_RAM      ((uint8_t*)0xa000)

#define FILE_C 0x43u
#define FILE_I 0x49u
#define FILE_S 0x53u

#define SCREEN        ((uint8_t*)0xf800)
#define COLOR         ((uint8_t*)0xd800)
#define SCREEN_COLS   40u
#define SCREEN_ROWS   25u
#define VIC_CTRL1     (*(volatile uint8_t*)0xd011)

void platform_memory_kernal(void);
void platform_memory_game(void);
void raster_irq_suspend(void);
void raster_irq_resume(void);

/* modules/disk_io.s: hand-written block I/O, not a byte-loop over small C
 * wrappers -- the C version was too large for this overlay's 4 KiB
 * window. See that file for the parameter/result convention. */
extern uint8_t platform_disk_slot;
extern uint8_t* platform_disk_buffer;
extern uint16_t platform_disk_want;
extern uint16_t platform_disk_got;
void platform_disk_read_block(void);
void platform_disk_write_block(void);
void platform_disk_index_read_block(void);
void platform_disk_index_write_block(void);

typedef union SaveWorkspace {
    uint8_t record[SAVE_PEEK_BYTES];
    struct {
        uint8_t names[SAVE_SLOT_COUNT][SAVE_NAME_BYTES];
        uint8_t present[SAVE_SLOT_COUNT];
    } slots;
} SaveWorkspace;

static SaveWorkspace save_workspace;
#define record_buffer     save_workspace.record
#define slot_display_name save_workspace.slots.names
#define slot_present      save_workspace.slots.present

#define selected_slot saveload_selected_slot

/* ---- little-endian helpers (no runtime-library division/multiplication) */

static uint16_t get16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* No 32-bit helpers here: the overlay link does not include the cc65
 * runtime library, and any `uint32_t` shift/OR pulls in unresolved eax-*
 * helpers from it. game_state.turn is the only 32-bit field this format
 * carries; copy_bytes() below moves it as raw bytes instead -- valid only
 * because cc65's in-memory layout for multi-byte integers is already
 * little-endian, matching this file format. */
static void copy_bytes(uint8_t* dst, const uint8_t* src, uint8_t count) {
    while (count-- != 0u) *dst++ = *src++;
}

/* ---- screen helpers (direct $F800/$D800 poke, matching modules/inventory.c) */

static void clear_screen(void) {
    uint16_t i;
    uint8_t blank;

    blank = platform_text_screen_code(' ');
    for (i = 0u; i < (uint16_t)SCREEN_COLS * SCREEN_ROWS; ++i) {
        SCREEN[i] = blank;
        COLOR[i] = 1u;
    }
}

static void put_char(uint8_t x, uint8_t y, uint8_t ch, uint8_t color) {
    uint16_t cell;
    if (x >= SCREEN_COLS || y >= SCREEN_ROWS) return;
    if (ch == 0u) ch = (uint8_t)' ';
    cell = (uint16_t)y * SCREEN_COLS + x;
    SCREEN[cell] = platform_text_screen_code((char)ch);
    COLOR[cell] = color;
}

static void put_string(uint8_t x, uint8_t y, const char* text, uint8_t color) {
    while (*text != '\0') {
        put_char(x, y, (uint8_t)*text, color);
        ++text;
        ++x;
    }
}

/* ---- disk I/O (device fixed at 8 in modules/disk_io.s; caller brackets
 * with platform_memory_kernal()/platform_memory_game()) */

static uint16_t disk_read_bytes(uint8_t slot, uint16_t want) {
    platform_disk_slot = slot;
    platform_disk_buffer = record_buffer;
    platform_disk_want = want;
    platform_disk_read_block();
    return platform_disk_got;
}

/* ---- record header parsing shared by list-peek and full load */

static uint8_t header_valid(uint16_t got) {
    return got >= SAVE_HEADER_BYTES &&
           (SAVE_RECORD_RAM[0] == FILE_C || SAVE_RECORD_RAM[0] == 0xc3u) &&
           SAVE_RECORD_RAM[1] == '6' && SAVE_RECORD_RAM[2] == '4' &&
           (SAVE_RECORD_RAM[3] == FILE_S || SAVE_RECORD_RAM[3] == 0xd3u) &&
           SAVE_RECORD_RAM[4] == SAVE_FORMAT_VERSION;
}

/* Peek just enough of one candidate file to list it: header + name.
 * Does not validate the full payload checksum (the complete record is not
 * read); a corrupt file may show a stale/wrong summary here, but a genuine
 * load always re-validates the complete record before touching game state. */
static uint8_t peek_slot(uint8_t slot) {
    uint16_t got;

    got = disk_read_bytes(slot, SAVE_PEEK_BYTES);
    if (got < SAVE_HEADER_BYTES ||
        (record_buffer[0] != FILE_C && record_buffer[0] != 0xc3u) ||
        record_buffer[1] != '6' || record_buffer[2] != '4' ||
        (record_buffer[3] != FILE_S && record_buffer[3] != 0xd3u) ||
        record_buffer[4] != SAVE_FORMAT_VERSION ||
        got < SAVE_PEEK_BYTES) return 0u;
    return 1u;
}

static uint16_t index_checksum(void) {
    uint16_t sum;
    uint8_t i;

    sum = 0u;
    for (i = SAVE_INDEX_HEADER_BYTES; i < SAVE_INDEX_BYTES; ++i) {
        sum += SAVE_INDEX_RAM[i];
    }
    return sum;
}

static uint8_t index_valid(uint16_t got) {
    if (got != SAVE_INDEX_BYTES) return 0u;
    if ((SAVE_INDEX_RAM[0] != FILE_C && SAVE_INDEX_RAM[0] != 0xc3u) ||
        SAVE_INDEX_RAM[1] != '6' || SAVE_INDEX_RAM[2] != '4' ||
        (SAVE_INDEX_RAM[3] != FILE_I && SAVE_INDEX_RAM[3] != 0xc9u)) return 0u;
    if (SAVE_INDEX_RAM[4] != SAVE_FORMAT_VERSION ||
        SAVE_INDEX_RAM[5] != SAVE_INDEX_ENTRY_BYTES ||
        SAVE_INDEX_RAM[6] != SAVE_SLOT_COUNT) return 0u;
    if (get16(SAVE_INDEX_RAM + 8) != index_checksum()) return 0u;
    return 1u;
}

static void decode_index(void) {
    uint8_t slot;
    uint8_t entry;
    uint8_t name;

    entry = SAVE_INDEX_HEADER_BYTES;
    name = 0u;
    for (slot = 0u; slot < SAVE_SLOT_COUNT; ++slot) {
        slot_present[slot] = SAVE_INDEX_RAM[entry];
        if (slot_present[slot]) {
            copy_bytes(record_buffer + name,
                       SAVE_INDEX_RAM + entry + 1u, SAVE_NAME_BYTES);
        }
        entry += SAVE_INDEX_ENTRY_BYTES;
        name += SAVE_NAME_BYTES;
    }
}

static void begin_index(void) {
    SAVE_INDEX_RAM[0] = FILE_C;
    SAVE_INDEX_RAM[1] = '6';
    SAVE_INDEX_RAM[2] = '4';
    SAVE_INDEX_RAM[3] = FILE_I;
    SAVE_INDEX_RAM[4] = SAVE_FORMAT_VERSION;
    SAVE_INDEX_RAM[5] = SAVE_INDEX_ENTRY_BYTES;
    SAVE_INDEX_RAM[6] = SAVE_SLOT_COUNT;
    SAVE_INDEX_RAM[7] = 0u;
}

static void build_slot_list(void) {
    uint8_t slot;
    uint8_t i;
    uint8_t entry;
    uint16_t got;

    platform_memory_kernal();
    platform_disk_buffer = SAVE_INDEX_RAM;
    platform_disk_want = SAVE_INDEX_BYTES;
    platform_disk_index_read_block();
    got = platform_disk_got;
    if (index_valid(got)) {
        decode_index();
        platform_memory_game();
        return;
    }

    /* Missing/corrupt index: inspect each save once, then rebuild the cache. */
    begin_index();
    entry = SAVE_INDEX_HEADER_BYTES;
    for (slot = 0u; slot < SAVE_SLOT_COUNT; ++slot) {
        if (peek_slot(slot)) {
            SAVE_INDEX_RAM[entry] = 1u;
            copy_bytes(SAVE_INDEX_RAM + entry + 1u,
                       record_buffer + SAVE_HEADER_BYTES, SAVE_NAME_BYTES);
        } else {
            SAVE_INDEX_RAM[entry] = 0u;
            for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
                SAVE_INDEX_RAM[entry + 1u + i] = 0u;
            }
        }
        entry += SAVE_INDEX_ENTRY_BYTES;
    }
    put16(SAVE_INDEX_RAM + 8, index_checksum());
    decode_index();
    platform_disk_buffer = SAVE_INDEX_RAM;
    platform_disk_want = SAVE_INDEX_BYTES;
    platform_disk_index_write_block();
    platform_memory_game();
}

/* ---- full record load + validate (does not touch game state) */

static uint8_t load_full(uint8_t slot) {
    uint16_t got;
    uint16_t payload_len;
    uint16_t checksum;
    uint16_t computed;
    uint16_t i;

    platform_disk_slot = slot;
    platform_disk_buffer = SAVE_RECORD_RAM;
    platform_disk_want = SAVE_MAX_BYTES;
    platform_disk_read_block();
    got = platform_disk_got;
    if (!header_valid(got)) return 0u;
    payload_len = get16(SAVE_RECORD_RAM + 10);
    if ((uint16_t)(SAVE_HEADER_BYTES + payload_len) != got) return 0u;
    checksum = get16(SAVE_RECORD_RAM + 12);
    computed = 0u;
    for (i = SAVE_HEADER_BYTES; i < got; ++i) computed += SAVE_RECORD_RAM[i];
    if (computed != checksum) return 0u;
    if (payload_len < SAVE_PREFIX_BYTES) return 0u;
    return 1u;
}

/* ---- slot-list screen ---------------------------------------------- */

static void draw_slot_list(void) {
    uint8_t slot;
    uint8_t y;

    clear_screen();
    put_string(13u, 1u,
              saveload_overlay_mode == SAVELOAD_MODE_SAVE ? "SAVE GAME"
                                                            : "LOAD GAME",
              1u);
    for (slot = 0u; slot < SAVE_SLOT_COUNT; ++slot) {
        y = (uint8_t)(3u + slot);
        put_char(4u, y, (uint8_t)('0' + slot), 1u);
        put_char(5u, y, (uint8_t)':', 1u);
        if (slot_present[slot]) {
            put_string(7u, y, "                ", 1u);
            {
                uint8_t i;
                uint8_t name;
                name = (uint8_t)(slot << 4);
                for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
                    put_char((uint8_t)(7u + i), y,
                             record_buffer[name + i], 1u);
                }
            }
        } else {
            put_string(7u, y, "(empty)", 1u);
        }
    }
    put_string(2u, 20u,
              saveload_overlay_mode == SAVELOAD_MODE_SAVE
                  ? "RETURN save   RUN/STOP cancel"
                  : "RETURN load   RUN/STOP cancel",
              1u);
}

static void draw_slot_cursor(uint8_t slot, uint8_t visible) {
    put_char(2u, (uint8_t)(3u + slot),
             visible ? (uint8_t)'>' : (uint8_t)' ',
             visible ? 7u : 0u);
}

static void wait_key_release(void) {
    do {
        platform_wait_frame();
    } while (platform_input_poll() != 0u);
}

/* ---- top-level dispatch -------------------------------------------- */

void saveload_overlay_run(void) {
    uint8_t key;
    uint8_t ok;
    uint8_t old_slot;

    selected_slot = 0u;
    platform_text_screen_enter();
    build_slot_list();
    draw_slot_list();
    draw_slot_cursor(selected_slot, 1u);
    VIC_CTRL1 |= 0x10u;
    wait_key_release();

    for (;;) {
        platform_wait_frame();
        key = platform_input_poll();
        if (key == 0u) continue;

        if (key == 3u) break; /* RUN/STOP cancels */
        old_slot = selected_slot;
        if (key == PLATFORM_KEY_CURSOR_UP) {
            if (selected_slot != 0u) --selected_slot;
        } else if (key == PLATFORM_KEY_CURSOR_DOWN) {
            if (selected_slot + 1u < SAVE_SLOT_COUNT) ++selected_slot;
        } else if (key == PLATFORM_KEY_ENTER) {
            if (saveload_overlay_mode == SAVELOAD_MODE_SAVE) {
                saveload_selected_slot = selected_slot;
                VIC_CTRL1 &= 0xefu;
                return;
            }

            if (!slot_present[selected_slot]) {
                wait_key_release();
                continue;
            }
            platform_memory_kernal();
            ok = load_full(selected_slot);
            platform_memory_game();
            if (ok) {
                saveload_load_pending = 1u;
                VIC_CTRL1 &= 0xefu;
                return;
            }
            build_slot_list();
            draw_slot_list();
            draw_slot_cursor(selected_slot, 1u);
            put_string(2u, 22u, "Load failed.", 2u);
            wait_key_release();
            continue;
        } else {
            wait_key_release();
            continue;
        }

        if (selected_slot != old_slot) {
            draw_slot_cursor(old_slot, 0u);
            draw_slot_cursor(selected_slot, 1u);
        }
        wait_key_release();
    }
    VIC_CTRL1 &= 0xefu;
}
