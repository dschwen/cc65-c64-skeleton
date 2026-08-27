#include <stdint.h>

#include "game.h"
#include "world.h"

/* Independently linked save/load browse overlay ("SL"): slot list, cursor
 * navigation, and the complete Load flow. See SAVE_GAME.md for the on-disk
 * record format and the "Save/load overlay" architecture section, and
 * modules/saveload_save.c for the Save flow (name entry + encode + write),
 * a separate overlay because the two together do not fit this overlay's
 * shared 4080-byte RAM window. This file does not link against the cc65
 * runtime library (same constraint as modules/inventory.c): no
 * memcpy/memset, no library string/number formatting, no division/modulo
 * by a non-power-of-two.
 */

#define SAVE_SLOT_COUNT      8u
#define SAVE_NAME_BYTES      16u
#define SAVE_HEADER_BYTES    16u
#define SAVE_PREFIX_BYTES    130u
#define SAVE_DELTA_BYTES     5u
/* Must match modules/saveload_save.c's SAVE_MAX_DELTAS (the encoder there
 * enforces the cap; this decoder just rejects anything larger). */
#define SAVE_MAX_DELTAS      26u
#define SAVE_MAX_BYTES       (SAVE_HEADER_BYTES + SAVE_PREFIX_BYTES + \
                              SAVE_MAX_DELTAS * SAVE_DELTA_BYTES)
#define SAVE_PEEK_BYTES      (SAVE_HEADER_BYTES + SAVE_NAME_BYTES + 6u)
#define SAVE_FORMAT_VERSION  1u

#define SCREEN        ((uint8_t*)0x0400)
#define COLOR         ((uint8_t*)0xd800)
#define SCREEN_COLS   40u
#define SCREEN_ROWS   25u
#define VIC_CTRL1     (*(volatile uint8_t*)0xd011)

void platform_memory_kernal(void);
void platform_memory_game(void);
void raster_irq_suspend(void);
void raster_irq_resume(void);

/* modules/disk_io.s: hand-written block I/O, not a byte-loop over small C
 * wrappers -- the C version was too large for this overlay's 4080-byte
 * window. See that file for the parameter/result convention. */
extern uint8_t platform_disk_slot;
extern uint8_t platform_disk_side;
extern uint8_t* platform_disk_buffer;
extern uint16_t platform_disk_want;
extern uint16_t platform_disk_got;
void platform_disk_read_block(void);

static uint8_t record_buffer[SAVE_MAX_BYTES];

static uint8_t slot_present[SAVE_SLOT_COUNT];
static uint8_t slot_side[SAVE_SLOT_COUNT];
static uint8_t slot_display_name[SAVE_SLOT_COUNT][SAVE_NAME_BYTES];
static uint16_t slot_day[SAVE_SLOT_COUNT];

static uint8_t selected_slot;

/* Plain global, not a pointer out-parameter: cc65 does not link the
 * runtime library into this overlay, and storing a 16-bit value through a
 * stack-relative pointer parameter pulls in the unresolved staxspidx
 * helper from it. */
static uint16_t peek_generation;
static uint16_t loaded_payload_len;

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

/* ---- screen helpers (direct $0400/$D800 poke, matching modules/inventory.c) */

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

/* Fixed-width unsigned decimal, no '/'/'%' (avoids the runtime division
 * helper the overlay link does not provide). */
static void put_decimal(uint8_t x, uint8_t y, uint16_t value, uint8_t color) {
    static const uint16_t PLACES[5] = {10000u, 1000u, 100u, 10u, 1u};
    uint8_t i;
    uint8_t started;
    uint8_t digit;

    started = 0u;
    for (i = 0u; i < 5u; ++i) {
        digit = 0u;
        while (value >= PLACES[i]) {
            value -= PLACES[i];
            ++digit;
        }
        if (digit != 0u || started || i == 4u) {
            started = 1u;
            put_char(x++, y, (uint8_t)('0' + digit), color);
        }
    }
}

/* ---- disk I/O (device fixed at 8 in modules/disk_io.s; caller brackets
 * with platform_memory_kernal()/platform_memory_game()) */

static uint16_t disk_read_bytes(uint8_t slot, uint8_t side, uint16_t want) {
    platform_disk_slot = slot;
    platform_disk_side = side;
    platform_disk_buffer = record_buffer;
    platform_disk_want = want;
    platform_disk_read_block();
    return platform_disk_got;
}

/* ---- record header parsing shared by list-peek and full load */

static uint8_t header_valid(uint16_t got) {
    return got >= SAVE_HEADER_BYTES &&
           record_buffer[0] == 'C' && record_buffer[1] == '6' &&
           record_buffer[2] == '4' && record_buffer[3] == 'S' &&
           record_buffer[4] == SAVE_FORMAT_VERSION;
}

/* Peek just enough of one candidate file to list it: header + name + day.
 * Does not validate the full payload checksum (the complete record is not
 * read); a corrupt file may show a stale/wrong summary here, but a genuine
 * load always re-validates the complete record before touching game state. */
static uint8_t peek_side(uint8_t slot, uint8_t side) {
    uint16_t got;

    got = disk_read_bytes(slot, side, SAVE_PEEK_BYTES);
    if (!header_valid(got) || got < SAVE_PEEK_BYTES) return 0u;
    peek_generation = get16(record_buffer + 6);
    return 1u;
}

static void build_slot_list(void) {
    uint8_t slot;
    uint8_t side;
    uint8_t valid_a;
    uint8_t valid_b;
    uint16_t gen_a;
    uint16_t gen_b;
    uint8_t i;

    platform_memory_kernal();
    for (slot = 0u; slot < SAVE_SLOT_COUNT; ++slot) {
        gen_a = 0u;
        gen_b = 0u;
        valid_a = peek_side(slot, 'A');
        if (valid_a) gen_a = peek_generation;
        valid_b = peek_side(slot, 'B');
        if (valid_b) gen_b = peek_generation;
        if (!valid_a && !valid_b) {
            slot_present[slot] = 0u;
            continue;
        }
        side = (valid_a && (!valid_b || gen_a >= gen_b)) ? 'A' : 'B';
        peek_side(slot, side);
        slot_present[slot] = 1u;
        slot_side[slot] = side;
        {
            uint8_t* row = slot_display_name[slot];
            for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
                row[i] = record_buffer[SAVE_HEADER_BYTES + i];
            }
        }
        put16((uint8_t*)&slot_day[slot],
             get16(record_buffer + SAVE_HEADER_BYTES + SAVE_NAME_BYTES + 4u));
    }
    platform_memory_game();
}

/* ---- full record load + validate (does not touch game state) */

static uint8_t load_full(uint8_t slot, uint8_t side) {
    uint16_t got;
    uint16_t payload_len;
    uint16_t checksum;
    uint16_t computed;
    uint16_t i;

    got = disk_read_bytes(slot, side, SAVE_MAX_BYTES);
    if (!header_valid(got)) return 0u;
    payload_len = get16(record_buffer + 10);
    if ((uint16_t)(SAVE_HEADER_BYTES + payload_len) != got) return 0u;
    checksum = get16(record_buffer + 12);
    computed = 0u;
    for (i = SAVE_HEADER_BYTES; i < got; ++i) computed += record_buffer[i];
    if (computed != checksum) return 0u;
    if (payload_len < SAVE_PREFIX_BYTES) return 0u;
    loaded_payload_len = payload_len;
    return 1u;
}

/* ---- apply a validated, buffered record to live game state */

static uint8_t apply_loaded_record(uint16_t payload_len) {
    const uint8_t* p;
    uint16_t delta_count;
    uint16_t i;
    uint8_t room;
    uint8_t ptype;
    uint8_t px;
    uint8_t py;
    uint8_t status;
    const uint8_t* d;

    p = record_buffer + SAVE_HEADER_BYTES;
    delta_count = get16(p + 128);
    if (delta_count > SAVE_MAX_DELTAS) return PLATFORM_ERR_FORMAT;
    if (SAVE_PREFIX_BYTES + delta_count * SAVE_DELTA_BYTES != payload_len) {
        return PLATFORM_ERR_FORMAT;
    }
    if (p[28] > p[29] || p[30] > p[31]) return PLATFORM_ERR_FORMAT;

    room = p[24];
    ptype = p[25];
    px = p[26];
    py = p[27];

    game_world_disable_store_hook();
    game_world_delta_count = 0u;
    {
        GameWorldDelta* dst = game_world_deltas;
        for (i = 0u; i < delta_count; ++i) {
            d = record_buffer + SAVE_HEADER_BYTES + SAVE_PREFIX_BYTES +
                i * SAVE_DELTA_BYTES;
            dst->room = d[0];
            dst->slot = d[1];
            dst->object.type = d[2];
            dst->object.x = d[3];
            dst->object.y = d[4];
            ++dst;
        }
    }
    game_world_delta_count = delta_count;

    status = platform_room_enter(room, ptype, px, py);
    game_world_enable_store_hook();
    if (status != PLATFORM_OK) return status;

    copy_bytes((uint8_t*)&game_state.turn, p, 4u);
    game_state.day = get16(p + 4);
    game_state.hour = p[6];
    game_state.minute = p[7];
    game_state.health = p[28];
    game_state.maximum_health = p[29];
    game_state.mana = p[30];
    game_state.maximum_mana = p[31];
    {
        GameInventorySlot* dst = game_state.inventory;
        for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
            dst->type = p[32u + i * 2u];
            dst->quantity = p[33u + i * 2u];
            ++dst;
        }
    }
    {
        uint8_t* dst = game_state.flags;
        for (i = 0u; i < GAME_FLAG_BYTES; ++i) *dst++ = p[96u + i];
    }
    game_player_sync_from_platform();
    game_entry_reason = GAME_ENTRY_LOAD;
    game_enter_room();
    game_enter_tile();
    return PLATFORM_OK;
}

/* ---- slot-list screen ---------------------------------------------- */

static void draw_slot_list(void) {
    uint8_t slot;
    uint8_t y;
    uint8_t color;

    clear_screen();
    put_string(13u, 1u,
              saveload_overlay_mode == SAVELOAD_MODE_SAVE ? "SAVE GAME"
                                                            : "LOAD GAME",
              1u);
    for (slot = 0u; slot < SAVE_SLOT_COUNT; ++slot) {
        y = (uint8_t)(3u + slot);
        color = slot == selected_slot ? 7u : 1u;
        put_char(2u, y, (uint8_t)('>'), (uint8_t)(slot == selected_slot ? 7u : 0u));
        put_char(4u, y, (uint8_t)('0' + slot), color);
        put_char(5u, y, (uint8_t)':', color);
        if (slot_present[slot]) {
            put_string(7u, y, "                ", color);
            {
                uint8_t i;
                const uint8_t* row = slot_display_name[slot];
                for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
                    put_char((uint8_t)(7u + i), y, row[i], color);
                }
            }
            put_string(25u, y, "day", color);
            put_decimal(29u, y, slot_day[slot], color);
        } else {
            put_string(7u, y, "(empty)", color);
        }
    }
    put_string(2u, 20u,
              saveload_overlay_mode == SAVELOAD_MODE_SAVE
                  ? "RETURN save   RUN/STOP cancel"
                  : "RETURN load   RUN/STOP cancel",
              1u);
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

    selected_slot = 0u;
    platform_text_screen_enter();
    build_slot_list();
    draw_slot_list();
    VIC_CTRL1 |= 0x10u;
    wait_key_release();

    for (;;) {
        platform_wait_frame();
        key = platform_input_poll();
        if (key == 0u) continue;

        if (key == 3u) break; /* RUN/STOP cancels */
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
            ok = load_full(selected_slot, slot_side[selected_slot]);
            platform_memory_game();
            if (ok && apply_loaded_record(loaded_payload_len) == PLATFORM_OK) {
                VIC_CTRL1 &= 0xefu;
                return;
            }
            draw_slot_list();
            put_string(2u, 22u, "Load failed.", 2u);
            wait_key_release();
            continue;
        } else {
            wait_key_release();
            continue;
        }

        draw_slot_list();
        wait_key_release();
    }
    VIC_CTRL1 &= 0xefu;
}
