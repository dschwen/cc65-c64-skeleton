#include <stdint.h>

#include "game.h"
#include "world.h"

/* Independently linked save-detail overlay ("SV"): name entry, record
 * encode, and the disk write for the slot chosen by the browse overlay
 * (modules/saveload.c, "SL") -- a separate overlay because the two
 * together do not fit the shared 4119-byte $A4E9 RAM window. See
 * SAVE_GAME.md. Same constraints as modules/saveload.c: no runtime
 * library, so no memcpy/memset, no library string/number formatting, no
 * division/modulo by a non-power-of-two, and no storing a 16-bit value
 * through a pointer out-parameter (use a plain global instead).
 */

#define SAVE_NAME_BYTES      16u
#define SAVE_HEADER_BYTES    16u
#define SAVE_PREFIX_BYTES    130u
#define SAVE_DELTA_BYTES     5u
#define SAVE_MAX_DELTAS      200u
#define SAVE_MAX_BYTES       (SAVE_HEADER_BYTES + SAVE_PREFIX_BYTES + \
                              SAVE_MAX_DELTAS * SAVE_DELTA_BYTES)
#define SAVE_PEEK_BYTES      (SAVE_HEADER_BYTES + SAVE_NAME_BYTES + 6u)
#define SAVE_FORMAT_VERSION  1u
#define SAVE_SLOT_COUNT      8u
#define SAVE_INDEX_HEADER_BYTES 10u
#define SAVE_INDEX_ENTRY_BYTES  17u
#define SAVE_INDEX_BYTES \
    (SAVE_INDEX_HEADER_BYTES + SAVE_SLOT_COUNT * SAVE_INDEX_ENTRY_BYTES)
#define SAVE_INDEX_RAM       ((uint8_t*)0xa000)
#define SAVE_RECORD_RAM      ((uint8_t*)0xa000)

#define FILE_C 0x43u
#define FILE_I 0x49u
#define FILE_S 0x53u

#define SCREEN        ((uint8_t*)0x0400)
#define COLOR         ((uint8_t*)0xd800)
#define SCREEN_COLS   40u
#define SCREEN_ROWS   25u
#define VIC_CTRL1     (*(volatile uint8_t*)0xd011)
#define NAME_SCREEN   ((uint8_t*)0x059f)

void platform_memory_kernal(void);
void platform_memory_game(void);
void raster_irq_suspend(void);
void raster_irq_resume(void);

extern uint8_t platform_disk_slot;
extern uint8_t* platform_disk_buffer;
extern uint16_t platform_disk_want;
extern uint16_t platform_disk_got;
void platform_disk_read_block(void);
void platform_disk_write_block(void);
void platform_disk_index_write_block(void);

/* The full 1,146-byte record uses room-stage RAM at $A000. Preserve the
 * browser's 146-byte index here before record encoding overwrites it. */
#define record_buffer SAVE_RECORD_RAM
static uint8_t index_buffer[SAVE_INDEX_BYTES];
static uint8_t name_buffer[SAVE_NAME_BYTES];

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

static uint16_t disk_read_bytes(uint8_t slot, uint16_t want) {
    platform_disk_slot = slot;
    platform_disk_buffer = record_buffer;
    platform_disk_want = want;
    platform_disk_read_block();
    return platform_disk_got;
}

static uint8_t disk_write_bytes(uint8_t slot, uint16_t size) {
    platform_disk_slot = slot;
    platform_disk_buffer = record_buffer;
    platform_disk_want = size;
    platform_disk_write_block();
    if (platform_disk_got != size) return 0u;
    return 1u;
}

static uint8_t disk_index_write(void) {
    platform_disk_buffer = index_buffer;
    platform_disk_want = SAVE_INDEX_BYTES;
    platform_disk_index_write_block();
    if (platform_disk_got != SAVE_INDEX_BYTES) return 0u;
    return 1u;
}

static uint8_t header_valid(uint16_t got) {
    return got >= SAVE_HEADER_BYTES &&
           (record_buffer[0] == FILE_C || record_buffer[0] == 0xc3u) &&
           record_buffer[1] == '6' && record_buffer[2] == '4' &&
           (record_buffer[3] == FILE_S || record_buffer[3] == 0xd3u) &&
           record_buffer[4] == SAVE_FORMAT_VERSION;
}

static uint8_t load_full(uint8_t slot) {
    uint16_t got;
    uint16_t payload_len;
    uint16_t checksum;
    uint16_t computed;
    uint16_t i;

    got = disk_read_bytes(slot, SAVE_MAX_BYTES);
    if (!header_valid(got)) return 0u;
    payload_len = get16(record_buffer + 10);
    if ((uint16_t)(SAVE_HEADER_BYTES + payload_len) != got) return 0u;
    checksum = get16(record_buffer + 12);
    computed = 0u;
    for (i = SAVE_HEADER_BYTES; i < got; ++i) computed += record_buffer[i];
    if (computed != checksum) return 0u;
    return 1u;
}

static uint16_t index_checksum(const uint8_t* index) {
    uint16_t sum;
    uint8_t i;

    sum = 0u;
    for (i = SAVE_INDEX_HEADER_BYTES; i < SAVE_INDEX_BYTES; ++i) {
        sum += index[i];
    }
    return sum;
}

static uint8_t* index_entry(uint8_t slot) {
    uint8_t* entry;

    entry = index_buffer + SAVE_INDEX_HEADER_BYTES;
    while (slot-- != 0u) entry += SAVE_INDEX_ENTRY_BYTES;
    return entry;
}

static uint16_t encode_record(const uint8_t* name) {
    uint8_t* p;
    uint8_t* q;
    uint16_t i;
    uint16_t payload_len;
    uint16_t checksum;

    p = record_buffer;
    p[0] = FILE_C;
    p[1] = '6';
    p[2] = '4';
    p[3] = FILE_S;
    p[4] = SAVE_FORMAT_VERSION;
    p[5] = 0u;
    p[6] = 0u;
    p[7] = 0u;
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
    uint8_t i;
    uint8_t ok;
    uint8_t status;
    uint8_t* entry;

    status = game_world_capture_current();
    if (status != PLATFORM_OK) return 0u;
    if (game_world_delta_count > SAVE_MAX_DELTAS) return 0u;
    total = encode_record(name);

    platform_memory_kernal();
    ok = disk_write_bytes(slot, total);
    if (ok) ok = load_full(slot);
    platform_memory_game();
    if (!ok) return 0u;

    entry = index_entry(slot);
    entry[0] = 1u;
    for (i = 0u; i < SAVE_NAME_BYTES; ++i) entry[1u + i] = name[i];
    put16(index_buffer + 8, index_checksum(index_buffer));
    platform_memory_kernal();
    ok = disk_index_write();
    platform_memory_game();
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

    clear_screen();
    put_string(4u, 10u, "SAVE NAME:", 1u);
    for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
        put_char((uint8_t)(15u + i), 10u,
                 i < length ? name[i] : (uint8_t)'.', 7u);
    }
    put_string(4u, 12u, "RETURN confirm   RUN/STOP cancel", 1u);

    for (;;) {
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
            if (length != 0u) {
                --length;
                NAME_SCREEN[length] = platform_text_screen_code('.');
            }
            continue;
        }
        if (name_is_printable(key) && length < SAVE_NAME_BYTES) {
            name[length] = key;
            NAME_SCREEN[length] = platform_text_screen_code((char)key);
            ++length;
        }
    }
}

void saveload_save_overlay_run(void) {
    uint8_t slot;
    uint8_t i;
    uint8_t have_name;
    uint8_t* entry;

    slot = saveload_selected_slot;
    platform_text_screen_enter();
    VIC_CTRL1 |= 0x10u;

    copy_bytes(index_buffer, SAVE_INDEX_RAM, SAVE_INDEX_BYTES);
    index_buffer[0] = FILE_C;
    index_buffer[3] = FILE_I;
    entry = index_entry(slot);
    have_name = entry[0];
    for (i = 0u; i < SAVE_NAME_BYTES; ++i) {
        name_buffer[i] = have_name ? entry[1u + i] : 0u;
    }

    if (!enter_name(name_buffer)) {
        VIC_CTRL1 &= 0xefu;
        return;
    }

    clear_screen();
    raster_irq_suspend();
    i = save_slot(slot, name_buffer);
    raster_irq_resume();

    clear_screen();
    put_string(2u, 10u, i ? "Saved." : "Save failed.", i ? 5u : 2u);
    wait_key_release();
    do {
        platform_wait_frame();
    } while (platform_input_poll() == 0u);
    VIC_CTRL1 &= 0xefu;
}
