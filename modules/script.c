#include <stdint.h>

#include "game.h"
#include "world.h"
#include "script_format.h"
#include "script_abi.h"
#include "banks.h"
#include "memmap.h"

/* Banked script/conversation/room interpreter service ("SC") - EasyFlash
 * bank BANK_SCRIPT (src/banks.h). All mutable state is explicitly resident
 * (see script_abi.h). Executes the bytecode format tools/compile_script.py
 * compiles; the opcode numbering itself comes from src/script_format.h,
 * which that tool parses, so the two sides cannot drift apart.
 *
 * Called via game_room_script_entry() in src/script_runtime.c, through
 * far_call_bank(BANK_SCRIPT, script_banked_run) - see script_banked_run()
 * at the bottom of this file. The compiled bytecode is separate from the
 * service: fetched at run time via platform_resource_fetch_range()
 * (src/resources.c), one EasyFlash bank per resource kind, which can be up
 * to PLATFORM_RESOURCE_MAX_BYTES (8 KiB) - far more than fits at once in
 * platform_room_scratch()'s resident buffer (room_stage's memory, borrowed
 * - see platform.h, ~1 KiB). So script_buf holds a *sliding window* into
 * the resource, not the whole thing; see read_byte()/ensure_window() below.
 *
 * For a KIND_ROOM resource, game_room_script_entry() hands this service the
 * requested entry key (script_entry_key) and this file finds it in the
 * entry table itself (find_room_entry()), reporting back through
 * script_entry_found whether anything actually ran.
 *
 * This file is engine code: it knows nothing about any particular story.
 * Everything a puzzle needs - inventory checks, consuming and giving
 * items, swapping or adding room objects, changing a tile - is an opcode
 * here, so the puzzle itself lives in its .script source. */

#define KEY_RETURN          13u
#define KEY_RUN_STOP        3u
#define KEY_DELETE          20u

extern uint8_t script_resource_kind;
extern uint8_t script_resource_id;
extern uint8_t script_entry_key;
extern uint8_t script_entry_found;
extern uint8_t script_text_shown;
extern uint8_t script_conversation_pending;
extern uint8_t script_conversation_id;
extern volatile uint8_t platform_frame_counter;
extern volatile uint8_t platform_raster_irq_active;
void raster_irq_resume(void);

void platform_text_output_native(const char* text);
extern uint8_t platform_text_output_color;
extern uint8_t platform_text_output_line;
extern uint8_t tile_data[];

#pragma section( script_code, 0 )
#pragma section( script_data, 0 )
#pragma region( script_bank, 0x8000, 0xc000, , BANK_SCRIPT,
                 { script_data, script_code } )
#pragma code(script_code)
#pragma data(script_data)

/* script_buf holds a sliding *window* into the resource, not the whole
 * thing. window_win_base is the absolute resource offset script_buf[0]
 * currently represents; window_len is how many bytes from there are
 * actually valid. Every byte access goes through read_byte()/
 * ensure_window(), which re-fetches a fresh window (via
 * platform_resource_fetch_range()) whenever the requested offset falls
 * outside the current one - cheap (one bank-switch+copy) and rare in
 * practice, since bytecode is read mostly sequentially. script_len is the
 * resource's total size, the upper bound exec_block and the table scanners
 * loop against - not how much is currently cached.
 *
 * window_win_base (not window_base) deliberately avoids naming this macro
 * the same as the struct member its own replacement text ends in
 * (`script_workspace.window_base`) - oscar64's preprocessor re-scans a
 * macro's own name inside its just-substituted replacement text. */
#define script_buf  script_workspace.buffer
#define script_cap  script_workspace.capacity
#define window_win_base script_workspace.window_base
#define window_len  script_workspace.window_length
#define script_len  script_workspace.resource_length

/* Unconditionally re-centers the window at `pos`, even if `pos` was already
 * covered by the current window - used by say() so a string gets the full
 * window's worth of contiguous room from its own start. */
static void ensure_window_at(uint16_t pos) {
    window_len = platform_resource_fetch_range(script_resource_kind,
                                               script_resource_id, pos,
                                               script_buf, script_cap);
    window_win_base = pos;
}

static void ensure_window(uint16_t pos) {
    uint16_t window_end = window_win_base + window_len;
    if (pos < window_win_base) { ensure_window_at(pos); return; }
    if (pos >= window_end) { ensure_window_at(pos); return; }
}

static uint8_t read_byte(uint16_t pos) {
    uint16_t window_end;
    ensure_window(pos);
    window_end = window_win_base + window_len;
    if (pos < window_win_base) return 0u; /* before start / bad fetch */
    if (pos >= window_end) return 0u; /* past end / bad fetch */
    return script_buf[pos - window_win_base];
}

static uint16_t read_u16(uint16_t pos) {
    return (uint16_t)read_byte(pos) | ((uint16_t)read_byte(pos + 1u) << 8);
}

/* A script may run while a room transition's raster_irq_suspend()/
 * platform_screen_blank() bracket is still open (a room's enter_room/
 * enter_tile hook - see game_process_pending_transition() in src/game.c).
 * The display stays pinned on one static charset and blanked for the
 * bracket's duration, so nothing this interpreter puts on screen would be
 * readable until the split is running again. Rather than resuming
 * unconditionally on entry (which briefly showed the *old* room's screen
 * for a silent enter_room script, e.g. one that only re-applies a tile
 * swap), the resume is deferred to the first opcode that actually needs
 * the display: text, a portrait, a keypress wait, lightning, the
 * conversation prompt. Resuming mid-transition is safe: platform_room_
 * enter()'s own EasyFlash work and the environment module fetch are both
 * complete before any room hook runs. Deliberately not re-suspended
 * afterward: game_process_pending_transition()'s own raster_irq_resume()
 * simply becomes a harmless no-op, and its game_transition_reveal() still
 * blanks, draws and unblanks unconditionally at the end. */
#define display_deferred script_display_deferred

static void display_ready(void) {
    if (display_deferred) {
        display_deferred = 0u;
        raster_irq_resume();
        platform_screen_unblank();
    }
}

/* Handles OP_ROOM_TRANSITION/OP_ROOM_TRANSITION_HERE's optional trailing
 * message operand. SCRIPT_NO_MESSAGE clears game_transition_pending_
 * message, so a message from an earlier transition can never leak into a
 * later message-less one. Otherwise the string is handed to
 * game_transition_show_message() now, synchronously, before the service
 * returns and script_buf's window is reused. */
static void set_transition_message(uint16_t offset) {
    if (offset == SCRIPT_NO_MESSAGE) {
        game_transition_pending_message = 0u;
        return;
    }
    display_ready();
    ensure_window_at(offset);
    game_transition_show_message((const char*)script_buf);
}

static const uint8_t BIT_MASK[8] = {
    0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u
};

static uint8_t flag_matches(uint8_t index, uint8_t cmp, uint8_t value) {
    uint8_t actual = game_state.flags[index];
    switch (cmp) {
        case SCRIPT_CMP_EQ: return actual == value;
        case SCRIPT_CMP_NE: return actual != value;
        case SCRIPT_CMP_LT: return actual < value;
        case SCRIPT_CMP_GT: return actual > value;
        case SCRIPT_CMP_LE: return actual <= value;
        case SCRIPT_CMP_GE: return actual >= value;
        case SCRIPT_CMP_BIT:
            if (value >= 8u) return 0u;
            return (actual & BIT_MASK[value]) != 0u;
        default: return 0u;
    }
}

static void set_bit(uint8_t index, uint8_t bit) {
    if (bit >= 8u) return;
    game_state.flags[index] |= BIT_MASK[bit];
}

static void clear_bit(uint8_t index, uint8_t bit) {
    if (bit >= 8u) return;
    game_state.flags[index] &= (uint8_t)~BIT_MASK[bit];
}

static void say(uint16_t str_offset) {
    display_ready();
    /* Force the window to start exactly at str_offset, guaranteeing the
     * string gets the window's full capacity of contiguous room from its
     * own start - platform_text_output_native() needs a plain C pointer to
     * walk byte-by-byte until a NUL; a single TEXT string longer than the
     * window capacity (~1 KiB) isn't supported. */
    ensure_window_at(str_offset);
    platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
    platform_text_output_color = 1u;
    platform_text_output_native((const char*)script_buf);
    /* Tell script_banked_run() there is text on screen worth pausing for
     * before its cleanup wipes it. A conversation's topic loop clears the
     * flag itself once it has waited for the player. */
    script_text_shown = 1u;
}

/* ---- room-object opcodes ------------------------------------------------
 *
 * These act on "the context tile": the tile a Look/Use entry was
 * dispatched for, or the player's own tile for an enter_room/enter_tile
 * entry (src/room_dispatch.c sets script_context_x/y before every room
 * hook). An object is "at" the tile when its hotspot lies on it. */

static uint8_t object_on_context_tile(const PlatformObject* object) {
    return (object->x >> 1) == script_context_x &&
           (object->y >> 1) == script_context_y;
}

/* Redraws the room after an object/tile change made it stale. Skipped
 * while the transition bracket is still open (display_deferred): the
 * change is to the staged-and-committed room, and game_transition_reveal()
 * will draw it from scratch anyway. */
static void room_changed(void) {
    (void)game_world_capture_current();
    if (!display_deferred) {
        platform_room_draw(&platform_room, platform_player);
    }
}

static uint8_t check_object(uint8_t type) {
    uint16_t limit = platform_room_object_limit(&platform_room);
    uint16_t i;
    for (i = 0u; i < limit; ++i) {
        const PlatformObject* object = &platform_room.objects[i];
        if (object->type == type && object_on_context_tile(object)) return 1u;
    }
    return 0u;
}

static void replace_object(uint8_t from, uint8_t to) {
    uint16_t limit = platform_room_object_limit(&platform_room);
    uint16_t i;
    uint8_t changed = 0u;
    for (i = 0u; i < limit; ++i) {
        PlatformObject* object = &platform_room.objects[i];
        if (object->type == from && object_on_context_tile(object)) {
            object->type = to;
            changed = 1u;
        }
    }
    if (changed) room_changed();
}

static void add_object(uint8_t type, uint8_t x, uint8_t y) {
    uint8_t slot;
    if (platform_room_object_add(&platform_room, type, x, y, &slot) == PLATFORM_OK) {
        room_changed();
    }
}

/* The tile grid is reloaded from ROM on every room entry and is not part
 * of the world-state journal (src/world.c tracks objects only), so a
 * script that swaps a tile must re-apply it from the room's enter_room
 * entry - see ROOM_CODE_API.md. */
static void set_tile(uint8_t x, uint8_t y, uint8_t tile) {
    if (x >= PLATFORM_MAP_WIDTH || y >= PLATFORM_MAP_HEIGHT) return;
    platform_room.tiles[(uint16_t)y * PLATFORM_MAP_WIDTH + x] = tile;
    if (!display_deferred) {
        platform_room_draw(&platform_room, platform_player);
    }
}

/* Recolors all four cells of one tile *definition* (the shared runtime
 * tile_data table, src/assets_data.c) - a palette patch for a tile that
 * has no spare ID for a second state. Also not persisted: tile_data is
 * reloaded from ROM after every save/load, so re-apply from enter_room. */
static void tile_color(uint8_t tile, uint8_t color) {
    uint8_t* cell = tile_data + ((uint16_t)tile << 3);
    cell[1] = color;
    cell[3] = color;
    cell[5] = color;
    cell[7] = color;
    if (!display_deferred) {
        platform_room_draw(&platform_room, platform_player);
    }
}

/* Runs [pos, end) - or until an END opcode, whichever comes first, so a
 * loose `end` (e.g. script_len) is a safe upper bound. CHECK_FLAG's and
 * CHECK_ITEM's/CHECK_OBJECT's branches recurse into this with their own
 * precise [pos, end); everything else is a flat walk. */
static void exec_block(uint16_t pos, uint16_t end) {
    uint8_t op;
    uint8_t a, b, c;
    uint16_t true_len, false_len, body;

    while (pos < end) {
        op = read_byte(pos);
        switch (op) {
            case SCRIPT_OP_END:
                return;
            case SCRIPT_OP_TEXT:
                say(read_u16(pos + 1u));
                pos += 3u;
                break;
            case SCRIPT_OP_PORTRAIT_SHOW:
                display_ready();
                (void)platform_portrait_show(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case SCRIPT_OP_PORTRAIT_HIDE:
                platform_portrait_hide();
                pos += 1u;
                break;
            case SCRIPT_OP_SET_FLAG:
                game_state.flags[read_byte(pos + 1u)] = read_byte(pos + 2u);
                pos += 3u;
                break;
            case SCRIPT_OP_CHECK_FLAG:
                a = read_byte(pos + 1u);
                b = read_byte(pos + 2u);
                c = read_byte(pos + 3u);
                true_len = read_u16(pos + 4u);
                false_len = read_u16(pos + 6u);
                body = pos + 8u;
                if (flag_matches(a, b, c)) {
                    exec_block(body, body + true_len);
                } else {
                    exec_block(body + true_len, body + true_len + false_len);
                }
                pos = body + true_len + false_len;
                break;
            case SCRIPT_OP_WAIT_KEY:
                display_ready();
                platform_wait_fresh_key();
                pos += 1u;
                break;
            case SCRIPT_OP_ROOM_TRANSITION:
                /* Queued, not applied here: the resident main loop applies
                 * it (game_process_pending_transition()) once this service
                 * has returned, so the switch never happens underneath a
                 * running script. The optional message is shown now. */
                set_transition_message(read_u16(pos + 4u));
                (void)game_transition_request(read_byte(pos + 1u),
                                              read_byte(pos + 2u),
                                              read_byte(pos + 3u));
                pos += 6u;
                break;
            case SCRIPT_OP_SOUND:
                /* No sound-effect table yet; reserved for one. */
                pos += 2u;
                break;
            case SCRIPT_OP_GIVE_OBJECT:
                (void)game_inventory_add(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case SCRIPT_OP_ROOM_TRANSITION_HERE:
                /* game_state.player_x/y are the same half-tile unit
                 * ROOM_TRANSITION's x,y take, so no conversion is needed. */
                set_transition_message(read_u16(pos + 2u));
                (void)game_transition_request(read_byte(pos + 1u),
                                              game_state.player_x,
                                              game_state.player_y);
                pos += 4u;
                break;
            case SCRIPT_OP_SET_BIT:
                set_bit(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case SCRIPT_OP_CLEAR_BIT:
                clear_bit(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case SCRIPT_OP_LIGHTNING:
                display_ready();
                platform_lightning();
                pos += 1u;
                break;
            case SCRIPT_OP_CONVERSE:
                /* Deferred: see script_banked_run(). */
                script_conversation_id = read_byte(pos + 1u);
                script_conversation_pending = 1u;
                pos += 2u;
                break;
            case SCRIPT_OP_CHECK_ITEM:
                a = read_byte(pos + 1u);
                b = read_byte(pos + 2u);
                true_len = read_u16(pos + 3u);
                false_len = read_u16(pos + 5u);
                body = pos + 7u;
                if (game_inventory_has(a, b)) {
                    exec_block(body, body + true_len);
                } else {
                    exec_block(body + true_len, body + true_len + false_len);
                }
                pos = body + true_len + false_len;
                break;
            case SCRIPT_OP_TAKE_ITEM:
                (void)game_inventory_remove(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case SCRIPT_OP_CHECK_OBJECT:
                a = read_byte(pos + 1u);
                true_len = read_u16(pos + 2u);
                false_len = read_u16(pos + 4u);
                body = pos + 6u;
                if (check_object(a)) {
                    exec_block(body, body + true_len);
                } else {
                    exec_block(body + true_len, body + true_len + false_len);
                }
                pos = body + true_len + false_len;
                break;
            case SCRIPT_OP_REPLACE_OBJECT:
                replace_object(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case SCRIPT_OP_ADD_OBJECT:
                add_object(read_byte(pos + 1u), read_byte(pos + 2u), read_byte(pos + 3u));
                pos += 4u;
                break;
            case SCRIPT_OP_SET_TILE:
                set_tile(read_byte(pos + 1u), read_byte(pos + 2u), read_byte(pos + 3u));
                pos += 4u;
                break;
            case SCRIPT_OP_TILE_COLOR:
                tile_color(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            default:
                return; /* malformed bytecode - stop rather than run off */
        }
    }
}

static uint8_t input_char_ok(uint8_t ch) {
    if (ch == 32u) return 1u;
    if (ch >= 48u && ch <= 57u) return 1u;
    if (ch >= 65u && ch <= 90u) return 1u;
    return 0u;
}

/* Keyword entry line: just "> ", what has been typed so far, and a blinking
 * $A0 block cursor - no "Ask about"/RUN-STOP banner (discovery-only UI, see
 * GAME_DESIGNER_GUIDE.md). Written straight to screen RAM. */
#define INPUT_ROW     (SCREEN_RAM + 24u * MAP_WIDTH_CHARS)
#define INPUT_COLORS  (COLOR_RAM + 24u * MAP_WIDTH_CHARS)
#define INPUT_COLUMN  2u
#define INPUT_MAX     20u   /* letters shown; only SCRIPT_KEYWORD_BYTES are matched */
#define CODE_SPACE    0x20u
#define CODE_PROMPT   0x3Eu /* '>' */
#define CODE_CURSOR   0xA0u /* solid block in the text charset */

static void input_put(uint8_t column, uint8_t code) {
    INPUT_ROW[column] = code;
    INPUT_COLORS[column] = 1u;
}

/* Reads a keyword from the player, echoing it: RETURN submits (ignored while
 * nothing is typed), DEL erases the last letter, RUN/STOP cancels. Returns 1
 * with the first SCRIPT_KEYWORD_BYTES letters in `prefix` (uppercase,
 * NUL-padded - exactly tools/compile_script.py's keyword encoding), or 0
 * to leave the conversation. An unshifted C64 keyboard returns uppercase
 * PETSCII for letter keys (65-90); shifted letters (193-218) are folded
 * onto the same codes.
 *
 * The echoed bytes are stored straight into screen RAM, NOT through
 * platform_text_screen_code(): that function expects the project's
 * compile-time string-literal encoding (platform.h's #pragma charmap),
 * where 65-90 means "encoded lowercase". A raw keyboard byte is a
 * different domain - GETIN's uppercase 65-90 already *is* this charset's
 * uppercase glyph code. modules/saveload_save.c's enter_name() has the
 * same rule. */
static uint8_t read_keyword(uint8_t* prefix) {
    uint8_t length;
    uint8_t key;
    uint8_t i;
    uint8_t cursor;

    display_ready();
    for (i = 0u; i < SCRIPT_KEYWORD_BYTES; ++i) prefix[i] = 0u;
    length = 0u;
    platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
    platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
    input_put(0u, CODE_PROMPT);
    input_put(1u, CODE_SPACE);
    while (platform_input_poll() != 0u) platform_wait_frame();
    for (;;) {
        platform_wait_frame();
        cursor = (platform_frame_counter & 0x10u) ? CODE_CURSOR : CODE_SPACE;
        input_put(INPUT_COLUMN + length, cursor);
        key = platform_input_poll();
        if (key == 0u) continue;
        if (key >= 193u && key <= 218u) key -= 128u;
        if (key == KEY_RUN_STOP) return 0u;
        if (key == KEY_RETURN) {
            if (length == 0u) continue;
            input_put(INPUT_COLUMN + length, CODE_SPACE);
            return 1u;
        }
        if (key == KEY_DELETE) {
            if (length > 0u) {
                input_put(INPUT_COLUMN + length, CODE_SPACE);
                --length;
                if (length < SCRIPT_KEYWORD_BYTES) prefix[length] = 0u;
            }
            continue;
        }
        if (!input_char_ok(key) || length >= INPUT_MAX) continue;
        if (key == 32u && length == 0u) continue; /* no leading spaces */
        if (length < SCRIPT_KEYWORD_BYTES) prefix[length] = key;
        input_put(INPUT_COLUMN + length, key);
        ++length;
    }
}

/* fallback: look for the reserved "*" topic (keyword "*\0\0\0") instead of
 * matching prefix. Returns the topic's bytecode offset, or 0 if none. */
static uint16_t find_topic(uint8_t topic_count, const uint8_t* prefix,
                           uint8_t fallback) {
    uint16_t entry = 3u;
    uint8_t i;
    uint8_t j;
    uint8_t want;
    uint8_t match;

    for (i = 0u; i < topic_count; ++i) {
        match = 1u;
        for (j = 0u; j < SCRIPT_KEYWORD_BYTES; ++j) {
            if (fallback) {
                want = j == 0u ? (uint8_t)'*' : 0u;
            } else {
                want = prefix[j];
            }
            if (read_byte(entry + j) != want) { match = 0u; break; }
        }
        if (match) return read_u16(entry + SCRIPT_KEYWORD_BYTES);
        entry += SCRIPT_TOPIC_ENTRY_BYTES;
    }
    return 0u;
}

/* Returns the entry's bytecode offset, or 0 if `key` matches no entry (no
 * fallback here, unlike find_topic - a room script has no "*" concept; an
 * unmatched key is a normal, silent no-op). */
static uint16_t find_room_entry(uint8_t entry_count, uint8_t key) {
    uint16_t entry = 3u;
    uint8_t i;

    for (i = 0u; i < entry_count; ++i) {
        if (read_byte(entry) == key) return read_u16(entry + 1u);
        entry += SCRIPT_ROOM_ENTRY_BYTES;
    }
    return 0u;
}

/* Reserved exit keyword, checked before any topic lookup - typed "bye"
 * (any case, per read_keyword()'s own fold) ends the conversation the same
 * way RUN/STOP does, just with a short farewell line first. Deliberately
 * spelled with *lowercase* source letters: this file's #pragma charmap
 * (src/platform.h) maps lowercase source characters to 65-90 - plain ASCII
 * uppercase, the range read_keyword()'s fold and compile_script.py's
 * keyword encoding both actually use - and uppercase source characters to
 * 193-218, which would never match. */
static const uint8_t bye_keyword[SCRIPT_KEYWORD_BYTES] = {'b', 'y', 'e', 0u};

static uint8_t prefix_is_bye(const uint8_t* prefix) {
    uint8_t j;
    for (j = 0u; j < SCRIPT_KEYWORD_BYTES; ++j) {
        if (prefix[j] != bye_keyword[j]) return 0u;
    }
    return 1u;
}

/* One conversation: prompt, read a keyword, run the matching topic (or the
 * "*" fallback), and let the player read the answer before the prompt comes
 * back. Leaves on RUN/STOP, on typed "bye", or when a topic used CONVERSE
 * to hand over to another conversation. */
static void run_conversation(uint8_t topic_count) {
    uint8_t prefix[SCRIPT_KEYWORD_BYTES];
    uint16_t entry;

    for (;;) {
        if (!read_keyword(prefix)) return;
        if (prefix_is_bye(prefix)) {
            platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
            platform_text_output_color = 1u;
            platform_text_output_native("\"Farewell, then.\"");
            platform_wait_fresh_key();
            return;
        }
        entry = find_topic(topic_count, prefix, 0u);
        if (entry == 0u) entry = find_topic(topic_count, prefix, 1u);
        script_text_shown = 0u;
        if (entry != 0u) {
            exec_block(entry, script_len);
            if (script_conversation_pending) return;
        } else {
            platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
            platform_text_output_color = 1u;
            platform_text_output_native("They don't know anything about that.");
            script_text_shown = 1u;
        }
        if (script_text_shown) platform_wait_fresh_key();
        script_text_shown = 0u;
    }
}

static void exec_resource(void) {
    uint8_t kind;

    script_buf = platform_room_scratch();
    script_cap = platform_room_scratch_bytes();
    window_win_base = 0u;
    window_len = platform_resource_fetch_range(script_resource_kind,
                                               script_resource_id, 0u,
                                               script_buf, script_cap);
    if (window_len < 3u) return; /* unpopulated resource ID, or a lookup failure */
    script_len = platform_resource_last_size();

    kind = read_byte(1u);
    if (kind == SCRIPT_KIND_CONVERSATION) {
        run_conversation(read_byte(2u));
    } else if (kind == SCRIPT_KIND_ROOM) {
        uint16_t entry = find_room_entry(read_byte(2u), script_entry_key);
        if (entry != 0u) {
            script_entry_found = 1u;
            exec_block(entry, script_len);
        }
    }
}

/* Full orchestration around exec_resource(), kept in this bank rather
 * than a resident wrapper: every call here is an ordinary resident
 * function call, safe from a banked caller under oscar64, so there is no
 * reason to spend resident budget (src/memmap.h) on it. */
__noinline void script_banked_run(void) {
    platform_look_cursor_hide();
    script_text_shown = 0u;
    script_conversation_pending = 0u;
    /* Still inside a transition's suspend bracket? See display_ready(). */
    display_deferred = !platform_raster_irq_active;
    exec_resource();

    /* CONVERSE queued a conversation: start it now that the script (and its
     * buffer) is finished, after letting the player read whatever the script
     * said - e.g. a greeting - since the conversation prompt clears the text
     * rows. The portrait the script showed stays up during the conversation. */
    while (script_conversation_pending) {
        script_conversation_pending = 0u;
        if (script_text_shown) {
            platform_wait_fresh_key();
            script_text_shown = 0u;
        }
        script_resource_kind = PLATFORM_RESOURCE_KIND_CONVERSATION;
        script_resource_id = script_conversation_id;
        exec_resource();
    }

    /* exec_resource() borrowed the room-staging scratch buffer (see
     * platform_room_scratch() in platform.h) - nothing else reads it
     * between calls - and any portrait it showed; put that back the way
     * normal gameplay expects to find it. */
    platform_portrait_hide();
    /* Give the player a chance to read room/standalone-script text before
     * wiping it. Gated on script_text_shown so this adds no new keypress
     * where there is nothing to protect: a conversation manages its own
     * pacing, and a script that only ran effects never sets the flag. Also
     * skipped when a transition message is pending: that path has its own
     * wait in game_transition_reveal(). */
    if (script_text_shown && !game_transition_pending_message) {
        platform_wait_fresh_key();
    }
    if (!display_deferred) {
        platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
        platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
    }
    display_deferred = 0u;
}

#pragma code(code)
#pragma data(data)
