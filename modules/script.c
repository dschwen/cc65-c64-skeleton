#include <stdint.h>

#include "game.h"
#include "script_format.h"

/* Independently linked script/conversation/room interpreter overlay ("SC").
 * Executes the bytecode format tools/compile_script.py compiles (see that
 * tool's docstring for the exact format and opcode encoding - this file
 * must stay in sync with it by hand, there is no shared source of truth).
 *
 * Loaded and run via game_script_play()/game_room_script_entry() in
 * src/script_runtime.c, the same $A4E9 pattern as every other overlay. The
 * compiled bytecode itself is separate from this overlay's own code:
 * fetched at run time from the generic sparse resource directory
 * (standalone scripts/conversations use resource IDs 240-255; a room's own
 * script uses resource_id == room_id, 0-239), which can be up to
 * PLATFORM_RESOURCE_MAX_BYTES (8 KiB) - far more than fits at once in
 * platform_room_scratch()'s resident buffer (room_stage's memory, borrowed
 * - see platform.h, ~1 KiB). So script_buf holds a *sliding window* into
 * the resource, not the whole thing; see read_byte()/ensure_window() below
 * for how byte access re-fetches a fresh window on demand via
 * platform_resource_fetch_range().
 *
 * For a KIND_ROOM resource, game_room_script_entry() hands this overlay the
 * requested entry key (script_entry_key) and this file finds it in the
 * entry table itself (find_room_entry(), mirroring find_topic() but with a
 * plain numeric key and no fallback - an unmatched key is a normal,
 * silent no-op, not an error), reporting back through script_entry_found
 * whether anything actually ran.
 *
 * Like modules/inventory.c and modules/saveload.c, this overlay links
 * against no cc65 runtime library (the resident game binary doesn't export
 * those helpers for the overlay resolver to bind against). Beyond their
 * documented avoid-list (memcpy/memset, division/modulo by a non-power-of-2
 * constant), this file also avoids: multiplying by a non-power-of-2
 * constant (needs mulax6 etc. - see find_topic's manual accumulation
 * instead of table + i*TOPIC_ENTRY_BYTES), returning or assigning a bare
 * comparison's result as a value (needs booleq/boolne/boolult/boolugt/
 * boolule - see flag_matches/input_char_ok's explicit if/return instead of
 * `return a == b;`), shifting by a runtime (non-constant) count (needs a
 * variable-shift helper - see flag_matches's BIT_MASK table instead of
 * `1u << value`), and combining an array-indexed operand with a binary
 * bitwise/arithmetic op in one expression (needs a 16-bit stack-based
 * helper, e.g. tosanda0 for `&` - see flag_matches's cmp 6 using a
 * compound-assignment `actual &= mask;` on a separately-loaded local
 * instead of `(actual & BIT_MASK[value]) != 0u` inline). A comparison used
 * only inside an if/while condition is fine either way, since it compiles
 * to a plain branch.
 */

#define OP_END              0x00u
#define OP_TEXT             0x01u
#define OP_PORTRAIT_SHOW    0x02u
#define OP_PORTRAIT_HIDE    0x03u
#define OP_SET_FLAG         0x04u
#define OP_CHECK_FLAG       0x05u
#define OP_WAIT_KEY         0x06u
#define OP_ROOM_TRANSITION  0x07u
#define OP_SOUND            0x08u
#define OP_GIVE_OBJECT      0x09u
#define OP_ROOM_TRANSITION_HERE 0x0Au
#define OP_SET_BIT          0x0Bu
#define OP_CLEAR_BIT        0x0Cu
#define OP_LIGHTNING        0x0Du

#define KEYWORD_BYTES       4u
#define TOPIC_ENTRY_BYTES   6u
#define KEY_RETURN          13u
#define KEY_RUN_STOP        3u
#define KEY_DELETE          20u

#define ROOM_ENTRY_BYTES    3u

extern uint8_t script_resource_kind;
extern uint8_t script_resource_id;
extern uint8_t script_entry_key;
extern uint8_t script_entry_found;

void __fastcall__ platform_text_output_native(const char* text);
extern uint8_t platform_text_output_color;
extern uint8_t platform_text_output_line;

/* script_buf holds a sliding *window* into the resource, not the whole
 * thing - a resource can be up to PLATFORM_RESOURCE_MAX_BYTES (8 KiB; see
 * platform.h) now, far more than this overlay's resident buffer
 * (platform_room_scratch(), ~1 KiB) can hold at once. window_base is the
 * absolute resource offset script_buf[0] currently represents; window_len
 * is how many bytes from there are actually valid. Every byte access goes
 * through read_byte()/ensure_window(), which re-fetches a fresh window
 * (via platform_resource_fetch_range()) whenever the requested offset
 * falls outside the current one - cheap (one bank-switch+copy) and rare in
 * practice, since bytecode is read mostly sequentially and a window holds
 * many opcodes/table entries at once. script_len is the resource's total
 * size (platform_resource_last_size()), the upper bound exec_block and the
 * table scanners loop against - not how much is currently cached. */
static uint8_t* script_buf;
static uint16_t script_cap;
static uint16_t window_base;
static uint16_t window_len;
static uint16_t script_len;

/* Unconditionally re-centers the window at `pos`, even if `pos` was already
 * covered by the current window - used by say() so a string gets the full
 * window's worth of contiguous room from its own start, not whatever
 * happened to be left over from wherever the window last was. */
static void ensure_window_at(uint16_t pos) {
    window_len = platform_resource_fetch_range(script_resource_kind,
                                               script_resource_id, pos,
                                               script_buf, script_cap);
    window_base = pos;
}

/* Written as two separate if-statements against a locally-computed
 * window_end, matching the simple-comparison style used everywhere else in
 * this file (see flag_matches's cmp 6 for why combining an operation with a
 * comparison in one expression is avoided here). */
static void ensure_window(uint16_t pos) {
    uint16_t window_end = window_base + window_len;
    if (pos < window_base) { ensure_window_at(pos); return; }
    if (pos >= window_end) { ensure_window_at(pos); return; }
}

static uint8_t read_byte(uint16_t pos) {
    uint16_t window_end;
    ensure_window(pos);
    window_end = window_base + window_len;
    if (pos < window_base) return 0u; /* before start / bad fetch */
    if (pos >= window_end) return 0u; /* past end / bad fetch */
    return script_buf[pos - window_base];
}

static uint16_t read_u16(uint16_t pos) {
    return (uint16_t)read_byte(pos) | ((uint16_t)read_byte(pos + 1u) << 8);
}

/* Written as explicit if/return, not `return (a == b);` - a bare comparison
 * used as a value (returned, assigned) needs cc65's booleq/boolne/boolult/
 * boolugt/boolule runtime helpers, which this overlay can't link against
 * (see the module doc comment). A comparison used only to pick a branch
 * compiles to a plain conditional jump instead. */
/* Bit masks for cmp 6 (bit test), indexed by bit number 0-7 - not computed
 * via a runtime-variable-count shift (`1u << value`), since a non-constant
 * shift count needs a cc65 runtime helper this overlay can't link against
 * (see the module doc comment's avoid-list). */
static const uint8_t BIT_MASK[8] = {
    0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u
};

/* ~BIT_MASK[n], precomputed - a table lookup instead of a runtime `~mask`,
 * since unary complement on a uint8_t promotes to int in C and cc65 may
 * compile that promoted-width complement via a runtime helper this overlay
 * can't link against (same class of pitfall as BIT_MASK's shift avoidance
 * above; not independently confirmed for `~`, but table lookup sidesteps
 * the question entirely at negligible cost). Used by OP_CLEAR_BIT. */
static const uint8_t BIT_CLEAR_MASK[8] = {
    0xFEu, 0xFDu, 0xFBu, 0xF7u, 0xEFu, 0xDFu, 0xBFu, 0x7Fu
};

static uint8_t flag_matches(uint8_t index, uint8_t cmp, uint8_t value) {
    uint8_t actual = game_state.flags[index];
    uint8_t mask;
    switch (cmp) {
        case 0: if (actual == value) return 1u; break;
        case 1: if (actual != value) return 1u; break;
        case 2: if (actual < value) return 1u; break;
        case 3: if (actual > value) return 1u; break;
        case 4: if (actual <= value) return 1u; break;
        case 5: if (actual >= value) return 1u; break;
        case 6:
            /* `actual &= mask;` then a bare `!= 0u` check, not
             * `(actual & BIT_MASK[value]) != 0u` inline - the inline form
             * pulled in cc65's tosanda0 runtime helper (a 16-bit
             * stack-based AND, from combining the array index with the AND
             * in one expression), which this overlay can't link against
             * either. The compound-assignment form compiles to a plain
             * accumulator AND. */
            if (value >= 8u) break;
            mask = BIT_MASK[value];
            actual &= mask;
            if (actual != 0u) return 1u;
            break;
        default: break;
    }
    return 0u;
}

/* Sets/clears bit `bit` (0-7) of flags[index], leaving its other bits
 * untouched; a no-op if bit >= 8. Same compound-assignment-on-a-local
 * pattern as flag_matches's cmp 6, for the same reason: a plain array
 * read, then the bitwise op on a local, then a plain array write - never
 * an array index combined with a binary op in one expression. */
static void set_bit(uint8_t index, uint8_t bit) {
    uint8_t actual;
    uint8_t mask;
    if (bit >= 8u) return;
    mask = BIT_MASK[bit];
    actual = game_state.flags[index];
    actual |= mask;
    game_state.flags[index] = actual;
}

static void clear_bit(uint8_t index, uint8_t bit) {
    uint8_t actual;
    uint8_t mask;
    if (bit >= 8u) return;
    mask = BIT_CLEAR_MASK[bit];
    actual = game_state.flags[index];
    actual &= mask;
    game_state.flags[index] = actual;
}

static void wait_fresh_key(void) {
    while (platform_input_poll() != 0u) platform_wait_frame();
    while (platform_input_poll() == 0u) platform_wait_frame();
    while (platform_input_poll() != 0u) platform_wait_frame();
}

static void say(uint16_t str_offset) {
    /* Force the window to start exactly at str_offset, guaranteeing the
     * string gets the window's full capacity of contiguous room from its
     * own start - platform_text_output_native() needs a plain C pointer to
     * walk byte-by-byte until a NUL, so the whole string must already be
     * contiguous in script_buf by the time it's called; a single TEXT
     * string longer than the window capacity (~1 KiB) isn't supported. */
    ensure_window_at(str_offset);
    platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
    platform_text_output_color = 1u;
    platform_text_output_native((const char*)script_buf);
}

/* Runs [pos, end) - or until an END opcode, whichever comes first, so a
 * loose `end` (e.g. script_len) is a safe upper bound, not a requirement to
 * hit exactly. CHECK_FLAG's branches recurse into this with their own
 * precise [pos, end); everything else is a flat, non-recursive walk. */
static void exec_block(uint16_t pos, uint16_t end) {
    uint8_t op;
    uint8_t index, cmp, value;
    uint16_t true_len, false_len, body;

    while (pos < end) {
        op = read_byte(pos);
        switch (op) {
            case OP_END:
                return;
            case OP_TEXT:
                say(read_u16(pos + 1u));
                pos += 3u;
                break;
            case OP_PORTRAIT_SHOW:
                (void)platform_portrait_show(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case OP_PORTRAIT_HIDE:
                platform_portrait_hide();
                pos += 1u;
                break;
            case OP_SET_FLAG:
                game_state.flags[read_byte(pos + 1u)] = read_byte(pos + 2u);
                pos += 3u;
                break;
            case OP_CHECK_FLAG:
                index = read_byte(pos + 1u);
                cmp = read_byte(pos + 2u);
                value = read_byte(pos + 3u);
                true_len = read_u16(pos + 4u);
                false_len = read_u16(pos + 6u);
                body = pos + 8u;
                if (flag_matches(index, cmp, value)) {
                    exec_block(body, body + true_len);
                } else {
                    exec_block(body + true_len, body + true_len + false_len);
                }
                pos = body + true_len + false_len;
                break;
            case OP_WAIT_KEY:
                wait_fresh_key();
                pos += 1u;
                break;
            case OP_ROOM_TRANSITION:
                /* Queued, not applied here: platform_room_enter() stages the
                 * destination room's code at $A4E9, where this overlay is
                 * itself currently running - calling it directly would
                 * overwrite this code out from under itself. The resident
                 * main loop applies the transition (via
                 * game_process_pending_transition()) once this overlay has
                 * returned and freed $A4E9 - the same reason
                 * saveload_runtime.c's saveload_apply_pending() defers it. */
                (void)game_transition_request(read_byte(pos + 1u),
                                              read_byte(pos + 2u),
                                              read_byte(pos + 3u));
                pos += 4u;
                break;
            case OP_SOUND:
                /* No sound-effect table yet; reserved for one. */
                pos += 2u;
                break;
            case OP_GIVE_OBJECT:
                (void)game_inventory_add(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case OP_ROOM_TRANSITION_HERE:
                /* Same deferred-apply reasoning as OP_ROOM_TRANSITION above.
                 * game_transition_request()'s x/y are half-tile coordinates
                 * (bounds-checked against PLATFORM_MAP_CHAR_WIDTH/HEIGHT,
                 * the character-cell grid) - the same unit game_state.
                 * player_x/y already use, so no conversion is needed. */
                (void)game_transition_request(read_byte(pos + 1u),
                                              game_state.player_x,
                                              game_state.player_y);
                pos += 2u;
                break;
            case OP_SET_BIT:
                set_bit(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case OP_CLEAR_BIT:
                clear_bit(read_byte(pos + 1u), read_byte(pos + 2u));
                pos += 3u;
                break;
            case OP_LIGHTNING:
                platform_lightning();
                pos += 1u;
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

/* Reads up to KEYWORD_BYTES characters typed by the player: RETURN submits,
 * RUN/STOP cancels (leaving prefix[0] as 0). Matches
 * tools/compile_script.py's keyword encoding - uppercase, NUL-padded - so
 * no case conversion is needed for an unshifted C64 keyboard, which already
 * returns uppercase PETSCII for letter keys. */
static void read_keyword(uint8_t* prefix) {
    uint8_t length;
    uint8_t key;
    uint8_t i;

    for (i = 0u; i < KEYWORD_BYTES; ++i) prefix[i] = 0u;
    length = 0u;
    while (platform_input_poll() != 0u) platform_wait_frame();
    for (;;) {
        platform_wait_frame();
        key = platform_input_poll();
        if (key == 0u) continue;
        if (key == KEY_RETURN) return;
        if (key == KEY_RUN_STOP) { prefix[0] = 0u; return; }
        if (key == KEY_DELETE && length > 0u) {
            --length;
            prefix[length] = 0u;
            continue;
        }
        if (input_char_ok(key) && length < KEYWORD_BYTES) {
            prefix[length++] = key;
        }
    }
}

/* fallback: look for the reserved "*" topic (keyword "*\0\0\0") instead of
 * matching prefix. Returns the topic's bytecode offset, or 0 if none.
 *
 * `entry` is advanced by TOPIC_ENTRY_BYTES each pass rather than computed as
 * table + i*TOPIC_ENTRY_BYTES - a non-power-of-2 constant multiply needs
 * cc65's mulax6 runtime helper, unavailable to this overlay (see the module
 * doc comment). Likewise `want` is picked with if/else, not a ternary, to
 * avoid materializing `fallback`/`j == 0u` as boolean values. */
static uint16_t find_topic(uint8_t topic_count, const uint8_t* prefix,
                           uint8_t fallback) {
    uint16_t entry = 3u;
    uint8_t i;
    uint8_t j;
    uint8_t want;
    uint8_t match;

    for (i = 0u; i < topic_count; ++i) {
        match = 1u;
        for (j = 0u; j < KEYWORD_BYTES; ++j) {
            if (fallback) {
                if (j == 0u) want = (uint8_t)'*';
                else want = 0u;
            } else {
                want = prefix[j];
            }
            if (read_byte(entry + j) != want) { match = 0u; break; }
        }
        if (match) return read_u16(entry + KEYWORD_BYTES);
        entry += TOPIC_ENTRY_BYTES;
    }
    return 0u;
}

/* Returns the entry's bytecode offset, or 0 if `key` matches no entry (no
 * fallback here, unlike find_topic - a room script has no "*" concept; an
 * unmatched key is a normal, silent no-op). Same non-power-of-2-multiply
 * avoidance as find_topic - `entry` is advanced by ROOM_ENTRY_BYTES each
 * pass rather than computed as table + i*ROOM_ENTRY_BYTES. */
static uint16_t find_room_entry(uint8_t entry_count, uint8_t key) {
    uint16_t entry = 3u;
    uint8_t i;

    for (i = 0u; i < entry_count; ++i) {
        if (read_byte(entry) == key) return read_u16(entry + 1u);
        entry += ROOM_ENTRY_BYTES;
    }
    return 0u;
}

static void run_conversation(uint8_t topic_count) {
    uint8_t prefix[KEYWORD_BYTES];
    uint16_t entry;

    for (;;) {
        platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
        platform_text_output_color = 1u;
        platform_text_output_native("Ask about (RUN/STOP to leave):");
        read_keyword(prefix);
        if (prefix[0] == 0u) return;
        entry = find_topic(topic_count, prefix, 0u);
        if (entry == 0u) entry = find_topic(topic_count, prefix, 1u);
        if (entry != 0u) {
            exec_block(entry, script_len);
        } else {
            platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
            platform_text_output_color = 1u;
            platform_text_output_native("They don't know anything about that.");
        }
    }
}

void script_overlay_run(void) {
    uint8_t kind;

    script_buf = platform_room_scratch();
    script_cap = platform_room_scratch_bytes();
    window_base = 0u;
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
    } else {
        exec_block(3u, script_len);
    }
}
