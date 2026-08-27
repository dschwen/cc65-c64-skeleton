#include <stdint.h>

#include "game.h"
#include "world.h"

/* Independently linked save-detail overlay ("SV"): name entry, record
 * encode, and the disk write for the slot chosen by the browse overlay
 * (modules/saveload.c, "SL") -- a separate overlay because the two
 * together do not fit the shared 4080-byte $A4E9 RAM window. See
 * SAVE_GAME.md. Same constraints as modules/saveload.c: no runtime
 * library, so no memcpy/memset, no library string/number formatting, no
 * division/modulo by a non-power-of-two, and no storing a 16-bit value
 * through a pointer out-parameter (use a plain global instead).
 */

#define SAVE_NAME_BYTES      16u
#define SAVE_HEADER_BYTES    16u
#define SAVE_PREFIX_BYTES    130u
#define SAVE_DELTA_BYTES     5u
/* Must match modules/saveload.c's decoder cap. */
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

extern uint8_t platform_disk_slot;
extern uint8_t platform_disk_side;
extern uint8_t* platform_disk_buffer;
extern uint16_t platform_disk_want;
extern uint16_t platform_disk_got;
void platform_disk_read_block(void);
void platform_disk_write_block(void);

static uint8_t record_buffer[SAVE_MAX_BYTES];
static uint8_t name_buffer[SAVE_NAME_BYTES];

static uint16_t peek_generation;
static uint8_t write_target_side;
static uint16_t write_target_generation;

static uint16_t get16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void copy_bytes(uint8_t* dst, const uint8_t* src, uint8_t count) {
    while (count-- != 0u) *dst++ = *src++;
}

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

static uint8_t name_is_printable(uint8_t ch) {
    return ch == 32u || (ch >= 48u && ch <= 57u) || (ch >= 65u && ch <= 90u);
}

static uint16_t disk_read_bytes(uint8_t slot, uint8_t side, uint16_t want) {
    platform_disk_slot = slot;
    platform_disk_side = side;
    platform_disk_buffer = record_buffer;
    platform_disk_want = want;
    platform_disk_read_block();
    return platform_disk_got;
}

static uint8_t disk_write_bytes(uint8_t slot, uint8_t side, uint16_t size) {
    platform_disk_slot = slot;
    platform_disk_side = side;
    platform_disk_buffer = record_buffer;
    platform_disk_want = size;
    platform_disk_write_block();
    if (platform_disk_got != size) return 0u;
    return 1u;
}

static uint8_t header_valid(uint16_t got) {
    return got >= SAVE_HEADER_BYTES &&
           record_buffer[0] == 'C' && record_buffer[1] == '6' &&
           record_buffer[2] == '4' && record_buffer[3] == 'S' &&
           record_buffer[4] == SAVE_FORMAT_VERSION;
}

static uint8_t peek_side(uint8_t slot, uint8_t side) {
    uint16_t got;

    got = disk_read_bytes(slot, side, SAVE_PEEK_BYTES);
    if (!header_valid(got) || got < SAVE_PEEK_BYTES) return 0u;
    peek_generation = get16(record_buffer + 6);
    return 1u;
}

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
    return 1u;
}

static void pick_write_target(uint8_t slot) {
    uint16_t gen_a;
    uint16_t gen_b;
    uint8_t valid_a;
    uint8_t valid_b;

    gen_a = 0u;
    gen_b = 0u;
    valid_a = peek_side(slot, 'A');
    if (valid_a) gen_a = peek_generation;
    valid_b = peek_side(slot, 'B');
    if (valid_b) gen_b = peek_generation;
    if (!valid_a) {
        write_target_side = 'A';
        write_target_generation = (uint16_t)((valid_b ? gen_b : 0u) + 1u);
    } else if (!valid_b) {
        write_target_side = 'B';
        write_target_generation = (uint16_t)(gen_a + 1u);
    } else if (gen_a <= gen_b) {
        write_target_side = 'A';
        write_target_generation = (uint16_t)(gen_b + 1u);
    } else {
        write_target_side = 'B';
        write_target_generation = (uint16_t)(gen_a + 1u);
    }
}

static uint16_t encode_record(const uint8_t* name, uint16_t generation) {
    uint8_t* p;
    uint8_t* q;
    uint16_t i;
    uint16_t payload_len;
    uint16_t checksum;

    p = record_buffer;
    p[0] = 'C';
    p[1] = '6';
    p[2] = '4';
    p[3] = 'S';
    p[4] = SAVE_FORMAT_VERSION;
    p[5] = 0u;
    p[6] = (uint8_t)generation;
    p[7] = (uint8_t)(generation >> 8);
    p[8] = 0u;
    p[9] = 0u;
    p[14] = 0u;
    p[15] = 0u;

    q = p + SAVE_HEADER_BYTES;
    for (i = 0u; i < SAVE_NAME_BYTES; ++i) *q++ = name[i];
    copy_bytes(q, (const uint8_t*)&game_state.turn, 4u); q += 4u;
    put16(q, game_state.day); q += 2u;
    *q++ = game_state.hour;
    *q++ = game_state.minute;
    *q++ = game_state.current_room;
    *q++ = game_state.player_type;
    *q++ = game_state.player_x;
    *q++ = game_state.player_y;
    *q++ = game_state.health;
    *q++ = game_state.maximum_health;
    *q++ = game_state.mana;
    *q++ = game_state.maximum_mana;
    {
        const GameInventorySlot* src = game_state.inventory;
        for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
            *q++ = src->type;
            *q++ = src->quantity;
            ++src;
        }
    }
    {
        const uint8_t* src = game_state.flags;
        for (i = 0u; i < GAME_FLAG_BYTES; ++i) *q++ = *src++;
    }
    put16(q, game_world_delta_count); q += 2u;

    {
        const GameWorldDelta* src = game_world_deltas;
        for (i = 0u; i < game_world_delta_count; ++i) {
            *q++ = src->room;
            *q++ = src->slot;
            *q++ = src->object.type;
            *q++ = src->object.x;
            *q++ = src->object.y;
            ++src;
        }
    }

    payload_len = (uint16_t)(q - (p + SAVE_HEADER_BYTES));
    put16(p + 10, payload_len);
    checksum = 0u;
    for (i = SAVE_HEADER_BYTES; i < SAVE_HEADER_BYTES + payload_len; ++i) {
        checksum += p[i];
    }
    put16(p + 12, checksum);
    return (uint16_t)(SAVE_HEADER_BYTES + payload_len);
}

static uint8_t save_slot(uint8_t slot, const uint8_t* name) {
    uint16_t total;
    uint8_t ok;

    (void)game_world_capture_current();
    if (game_world_delta_count > SAVE_MAX_DELTAS) return 0u;
    pick_write_target(slot);
    total = encode_record(name, write_target_generation);
    ok = disk_write_bytes(slot, write_target_side, total);
    if (ok) ok = load_full(slot, write_target_side);
    return ok;
}

static void wait_key_release(void) {
    do {
        platform_wait_frame();
    } while (platform_input_poll() != 0u);
}

static uint8_t enter_name(uint8_t* name) {
    uint8_t length;
    uint8_t key;
    uint8_t i;

    length = 0u;
    while (length < SAVE_NAME_BYTES && name[length] != 0u) ++length;

    for (;;) {
        clear_screen();
        put_string(4u, 10u, "SAVE NAME:", 1u);
        for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
            put_char((uint8_t)(15u + i), 10u,
                    i < length ? name[i] : (uint8_t)'_', 7u);
        }
        put_string(4u, 12u, "RETURN confirm   RUN/STOP cancel", 1u);

        wait_key_release();
        do {
            platform_wait_frame();
            key = platform_input_poll();
        } while (key == 0u);

        if (key == PLATFORM_KEY_ENTER) {
            for (i = length; i < SAVE_NAME_BYTES; ++i) name[i] = 0u;
            return 1u;
        }
        if (key == 3u) return 0u; /* RUN/STOP */
        if (key == 20u) { /* DEL */
            if (length != 0u) --length;
            continue;
        }
        if (name_is_printable(key) && length < SAVE_NAME_BYTES) {
            name[length++] = key;
        }
    }
}

void saveload_save_overlay_run(void) {
    uint8_t slot;
    uint8_t i;
    uint8_t have_name;

    slot = saveload_selected_slot;
    platform_text_screen_enter();
    VIC_CTRL1 |= 0x10u;

    platform_memory_kernal();
    have_name = peek_side(slot, 'A');
    {
        uint16_t gen_a;
        uint8_t valid_b;
        uint16_t gen_b;
        gen_a = have_name ? peek_generation : 0u;
        valid_b = peek_side(slot, 'B');
        gen_b = valid_b ? peek_generation : 0u;
        if (valid_b && (!have_name || gen_b > gen_a)) {
            have_name = peek_side(slot, 'B');
        } else if (have_name) {
            have_name = peek_side(slot, 'A');
        }
    }
    platform_memory_game();

    for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
        name_buffer[i] = have_name ? record_buffer[SAVE_HEADER_BYTES + i] : 0u;
    }

    if (!enter_name(name_buffer)) {
        VIC_CTRL1 &= 0xefu;
        return;
    }

    raster_irq_suspend();
    platform_memory_kernal();
    i = save_slot(slot, name_buffer);
    platform_memory_game();
    raster_irq_resume();

    clear_screen();
    put_string(2u, 10u, i ? "Saved." : "Save failed.", i ? 5u : 2u);
    wait_key_release();
    do {
        platform_wait_frame();
    } while (platform_input_poll() == 0u);
    VIC_CTRL1 &= 0xefu;
}
