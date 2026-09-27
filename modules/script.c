#include <stdint.h>

#include "game.h"
#include "script_format.h"
#include "script_abi.h"
#include "banks.h"
#include "memmap.h"

/* Banked script/conversation/room interpreter service ("SC") - EasyFlash
 * bank BANK_SCRIPT (src/banks.h). All mutable state is explicitly resident
 * below $8000 (see script_abi.h). Executes the bytecode format tools/
 * compile_script.py compiles (see that tool's docstring for the exact
 * format and opcode encoding - this file must stay in sync with it by
 * hand, there is no shared source of truth).
 *
 * Called via game_script_play()/game_room_script_entry() in
 * src/script_runtime.c, through far_call_bank(BANK_SCRIPT, script_banked_
 * run) - see script_banked_run() at the bottom of this file. The compiled
 * bytecode is separate from the service: fetched at run time via
 * platform_resource_fetch_range() (src/resources.c), one EasyFlash bank
 * per resource (see BANK_SCRIPT_ROOM_FIRST), which can be up to
 * PLATFORM_RESOURCE_MAX_BYTES (8 KiB) - far more than fits at once in
 * platform_room_scratch()'s resident buffer (room_stage's memory, borrowed
 * - see platform.h, ~1 KiB). So script_buf holds a *sliding window* into
 * the resource, not the whole thing; see read_byte()/ensure_window() below
 * for how byte access re-fetches a fresh window on demand via
 * platform_resource_fetch_range().
 *
 * For a KIND_ROOM resource, game_room_script_entry() hands this service the
 * requested entry key (script_entry_key) and this file finds it in the
 * entry table itself (find_room_entry(), mirroring find_topic() but with a
 * plain numeric key and no fallback - an unmatched key is a normal,
 * silent no-op, not an error), reporting back through script_entry_found
 * whether anything actually ran.
 *
 * Written under cc65 to avoid that toolchain's runtime-helper-linking
 * restrictions (multiply by a non-power-of-2 constant, bare-comparison-as-
 * value, runtime-variable shift counts, combining an array index with a
 * binary op in one expression - see find_topic/flag_matches/set_bit's
 * comments below for the specific workarounds). None of that applies to
 * oscar64 - it's a whole-program compiler, so every helper it might need
 * is always available - but the workarounds are harmless and left as-is
 * rather than rewritten back to more natural C for no functional gain. */

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
#define OP_CONVERSE         0x0Eu

#define TRANSITION_MSG_NONE 0xFFFFu

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
extern uint8_t script_text_shown;
extern uint8_t script_conversation_pending;
extern uint8_t script_conversation_id;
extern volatile uint8_t platform_frame_counter;

void platform_text_output_native(const char* text);
extern uint8_t platform_text_output_color;
extern uint8_t platform_text_output_line;

#pragma section( script_code, 0 )
#pragma section( script_data, 0 )
#pragma region( script_bank, 0x8000, 0xc000, , BANK_SCRIPT,
                 { script_data, script_code } )
#pragma code(script_code)
#pragma data(script_data)

/* script_buf holds a sliding *window* into the resource, not the whole
 * thing - a resource can be up to PLATFORM_RESOURCE_MAX_BYTES (8 KiB; see
 * platform.h) now, far more than this service's resident buffer
 * (platform_room_scratch(), ~1 KiB) can hold at once. window_win_base is
 * the absolute resource offset script_buf[0] currently represents;
 * window_len is how many bytes from there are actually valid. Every byte
 * access goes through read_byte()/ensure_window(), which re-fetches a
 * fresh window (via platform_resource_fetch_range()) whenever the
 * requested offset falls outside the current one - cheap (one bank-
 * switch+copy) and rare in practice, since bytecode is read mostly
 * sequentially and a window holds many opcodes/table entries at once.
 * script_len is the resource's total size (platform_resource_last_size()),
 * the upper bound exec_block and the table scanners loop against - not how
 * much is currently cached.
 *
 * window_win_base (not window_base) deliberately avoids naming this macro
 * the same as the struct member its own replacement text ends in
 * (`script_workspace.window_base`) - confirmed live this session (see
 * memmap.h's far_call_bank() comment for the first instance of this exact
 * pitfall) that oscar64's preprocessor, unlike a standard C preprocessor,
 * re-scans a macro's own name inside its just-substituted replacement
 * text, corrupting the expansion. */
#define script_buf  script_workspace.buffer
#define script_cap  script_workspace.capacity
#define window_win_base script_workspace.window_base
#define window_len  script_workspace.window_length
#define script_len  script_workspace.resource_length

/* Unconditionally re-centers the window at `pos`, even if `pos` was already
 * covered by the current window - used by say() so a string gets the full
 * window's worth of contiguous room from its own start, not whatever
 * happened to be left over from wherever the window last was. */
static void ensure_window_at(uint16_t pos) {
    window_len = platform_resource_fetch_range(script_resource_kind,
                                               script_resource_id, pos,
                                               script_buf, script_cap);
    window_win_base = pos;
}

/* Written as two separate if-statements against a locally-computed
 * window_end, matching the simple-comparison style used everywhere else in
 * this file (see flag_matches's cmp 6 for why combining an operation with a
 * comparison in one expression is avoided here). */
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

/* Handles OP_ROOM_TRANSITION/OP_ROOM_TRANSITION_HERE's optional trailing
 * message operand. offset == TRANSITION_MSG_NONE (the DSL's default, no
 * trailing string) clears game_transition_pending_message, so a message
 * from an earlier transition can never leak into a later message-less one.
 * Otherwise, ensure_window_at(offset) re-centers script_buf so the string
 * starts at script_buf[0] with the window's full remaining capacity behind
 * it (same trick say() uses), then hands that contiguous pointer straight
 * to game_transition_show_message() - an ordinary resident call, safe from
 * this bank like any other (see memmap.h's header comment). Must happen
 * now, synchronously, before the service returns and script_buf's window
 * is reused. */
static void set_transition_message(uint16_t offset) {
    if (offset == TRANSITION_MSG_NONE) {
        game_transition_pending_message = 0u;
        return;
    }
    ensure_window_at(offset);
    game_transition_show_message((const char*)script_buf);
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
    /* Tell script_banked_run() there is text on screen worth pausing for
     * before its cleanup wipes it. A conversation's topic loop clears the
     * flag itself once it has waited for the player (see run_conversation). */
    script_text_shown = 1u;
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
                 * destination room's code at `$B000`, where this overlay is
                 * itself currently running - calling it directly would
                 * overwrite this code out from under itself. The resident
                 * main loop applies the transition (via
                 * game_process_pending_transition()) once this overlay has
                 * returned and freed `$B000` - the same reason
                 * saveload_runtime.c's saveload_apply_pending() defers it.
                 * The optional message string must be copied out now too,
                 * for the same reason - see set_transition_message(). */
                set_transition_message(read_u16(pos + 4u));
                (void)game_transition_request(read_byte(pos + 1u),
                                                     read_byte(pos + 2u),
                                                     read_byte(pos + 3u));
                pos += 6u;
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
                set_transition_message(read_u16(pos + 2u));
                (void)game_transition_request(read_byte(pos + 1u),
                                                     game_state.player_x,
                                                     game_state.player_y);
                pos += 4u;
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
            case OP_CONVERSE:
                /* Deferred: see script_banked_run(). */
                script_conversation_id = read_byte(pos + 1u);
                script_conversation_pending = 1u;
                pos += 2u;
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
 * GAME_DESIGNER_GUIDE.md). Written straight to screen RAM (KERNAL stays
 * mapped in gameplay, so writes reach it), the same way
 * platform_text_output_native() draws the two status rows - both of which
 * this clears itself up front, since it no longer relies on that function
 * to do it. */
#define INPUT_ROW     (SCREEN_RAM + 24u * MAP_WIDTH_CHARS)
#define INPUT_COLORS  (COLOR_RAM + 24u * MAP_WIDTH_CHARS)
#define INPUT_COLUMN  2u
#define INPUT_MAX     20u   /* letters shown; only KEYWORD_BYTES are matched */
#define CODE_SPACE    0x20u
#define CODE_PROMPT   0x3Eu /* '>' */
#define CODE_CURSOR   0xA0u /* solid block in the text charset */

static void input_put(uint8_t column, uint8_t code) {
    INPUT_ROW[column] = code;
    INPUT_COLORS[column] = 1u;
}

/* Reads a keyword from the player, echoing it: RETURN submits (ignored while
 * nothing is typed), DEL erases the last letter, RUN/STOP cancels. Returns 1
 * with the first KEYWORD_BYTES letters in `prefix` (uppercase, NUL-padded -
 * exactly tools/compile_script.py's keyword encoding, so no case conversion
 * is needed), or 0 to leave the conversation. The player may type a whole
 * word; only its first four letters are matched. An unshifted C64 keyboard
 * returns uppercase PETSCII for letter keys (65-90); shifted letters
 * (193-218) are folded onto the same codes.
 *
 * The echoed bytes (key itself: space, digits, or 65-90) are stored
 * straight into screen RAM, NOT through platform_text_screen_code() - that
 * function expects the project's compile-time string-literal encoding
 * (lowercase-in-source -> 65-90, uppercase-in-source -> 193-218, via
 * platform.h's #pragma charmap), where 65-90 means "encoded lowercase". A
 * raw keyboard byte is a different, incompatible domain: GETIN's uppercase
 * 65-90 already *is* this charset's uppercase glyph code (confirmed against
 * assets/charset.cchr), so converting it again renders the lowercase
 * glyphs instead. modules/saveload_save.c's enter_name() has the same fix
 * for the same reason. */
static uint8_t read_keyword(uint8_t* prefix) {
    uint8_t length;
    uint8_t key;
    uint8_t i;
    uint8_t cursor;

    for (i = 0u; i < KEYWORD_BYTES; ++i) prefix[i] = 0u;
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
                if (length < KEYWORD_BYTES) prefix[length] = 0u;
            }
            continue;
        }
        if (!input_char_ok(key) || length >= INPUT_MAX) continue;
        if (key == 32u && length == 0u) continue; /* no leading spaces */
        if (length < KEYWORD_BYTES) prefix[length] = key;
        input_put(INPUT_COLUMN + length, key);
        ++length;
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

/* Reserved exit keyword, checked before any topic lookup - typed "bye"
 * (any case, per read_keyword()'s own fold) ends the conversation the same
 * way RUN/STOP does, just with a short farewell line first. Plain ASCII
 * uppercase (66,89,69), matching tools/compile_script.py's own topic-
 * keyword encoding (topic.keyword.upper().encode("ascii")) and
 * read_keyword()'s post-fold output range - no separate encoding step
 * needed. Deliberately spelled with *lowercase* source letters: this
 * file's #pragma charmap (src/platform.h:9-10) reassigns uppercase source
 * characters to 193-218 (this charset's "shifted" glyph range) and
 * lowercase source characters to 65-90 (plain ASCII uppercase, the
 * "unshifted" glyph range read_keyword()'s own fold and compile_script.py
 * both actually use) - writing 'B','Y','E' here would silently compile to
 * the wrong domain and never match. Generic, not Pell-specific: this loop
 * is shared by every conversation. */
static const uint8_t bye_keyword[KEYWORD_BYTES] = {'b', 'y', 'e', 0u};

static uint8_t prefix_is_bye(const uint8_t* prefix) {
    uint8_t j;
    for (j = 0u; j < KEYWORD_BYTES; ++j) {
        if (prefix[j] != bye_keyword[j]) return 0u;
    }
    return 1u;
}

static uint8_t prefix_is(const uint8_t* prefix, const uint8_t* word) {
    uint8_t j;
    for (j = 0u; j < KEYWORD_BYTES; ++j) {
        if (prefix[j] != word[j]) return 0u;
    }
    return 1u;
}

static void topic_say(const char* text) {
    platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
    platform_text_output_color = 1u;
    platform_text_output_native(text);
    wait_fresh_key();
}

/*
 * Trigger-word puzzle resolutions, checked in run_conversation() before
 * its own topic lookup, for every conversation: mentioning the right word
 * while carrying the right item resolves a puzzle immediately, instead of
 * needing to Use the item at the NPC (which now only gives a redirect
 * hint - see rooms/c/02_use_at.c and rooms/c/05_use_at.c). Returns 1 if
 * this call was fully handled (text already shown and dismissed) - the
 * caller must skip its own topic lookup for this input; returns 0
 * (nothing to resolve, or already resolved) to let the normal
 * conversation flow proceed unchanged, which is what shows each topic's
 * ordinary flavor text instead.
 *
 * This is story-specific content (references particular object types,
 * flags and conversation IDs), which would normally live in story/
 * story.c's own hook functions (see story_use_inventory() there) rather
 * than in this generic engine file - but it's defined here instead,
 * specifically to avoid spending resident RAM on it: story.c is entirely
 * resident (called from banked callers like an ordinary function, per
 * memmap.h's far_call_bank() rule), and this function's three separate
 * multi-line resolutions were large enough on their own to overflow that
 * already razor-thin budget (confirmed live: "oscar64: error 3034: Cannot
 * place stack section" the first time this was written in story.c). This
 * file is already banked (BANK_SCRIPT, src/banks.h) with ample headroom,
 * and is the only caller, so keeping it here costs nothing scarce.
 *
 * Calling game_room_script_entry() from here to reuse the room-script
 * text these resolutions used to live in (STORY_ROOM02_CALDUS_FREE etc.,
 * now deleted) was considered and rejected: this function runs from
 * inside run_conversation(), itself already deep inside this same file's
 * exec_resource()/script_banked_run() call chain for the *current*
 * conversation resource. game_room_script_entry() would trigger a nested
 * far_call_bank() back into script_banked_run() for a *different*
 * resource (kind ROOM), clobbering the shared script_resource_kind/
 * script_resource_id/script_buf window state this same conversation is
 * still mid-way through using - exactly the hazard CONVERSE's own
 * deferred-until-return handling (script_banked_run()'s "while
 * (script_conversation_pending)" loop) exists to avoid. So each
 * resolution's text is written directly here instead, in full.
 *
 * Conversation IDs: assets/scripts/conversations/05 (Pell), 20 (Orra), 22
 * (Caldus) - see those files and assets/scripts/02.script's header for
 * the numbering convention. Trigger words are deliberately spelled with
 * lowercase source letters, matching bye_keyword's own comment above
 * ([[project_charmap_source_literal_footgun]]) - uppercase would silently
 * compile to the wrong byte range and never match.
 */
static uint8_t story_topic_resolve(const uint8_t* prefix) {
    static const uint8_t oil_word[KEYWORD_BYTES]    = {'o', 'i', 'l', 0u};
    static const uint8_t seal_word[KEYWORD_BYTES]   = {'s', 'e', 'a', 'l'};
    static const uint8_t letter_word[KEYWORD_BYTES] = {'l', 'e', 't', 't'};

    if (script_resource_id == 22u && prefix_is(prefix, oil_word)) {
        if (game_state.flags[9] || !game_inventory_has_one(51u)) {
            return 0u; /* STORY_STATE_ROOM_02_CALDUS_FREED; Lamp Oil */
        }
        game_inventory_remove_one(51u);
        topic_say("With a screech of metal the seized visor finally gives, and Sir Caldus draws a first unmuffled breath.");
        game_inventory_add_one(54u); /* Silver Shard */
        topic_say("\"You have my thanks - truly,\" he says, pressing something small and cold into your hand - a shard of silver, etched with a tower's sigil.");
        game_state.flags[9] = 1u; /* STORY_STATE_ROOM_02_CALDUS_FREED */
        topic_say("\"Take it. It marked me once as Silver Tower. Perhaps it will open a door for you instead.\"");
        return 1u;
    }

    if (script_resource_id == 5u && prefix_is(prefix, seal_word)) {
        if (game_state.flags[13] || !game_inventory_has_one(43u)) {
            return 0u; /* STORY_STATE_ROOM_05_PELL_HELPED; Blue Seal */
        }
        if (!(game_state.flags[11] && game_state.flags[12])) {
            /* STORY_STATE_ROOM_05_PELL_INVITE_SEEN, _BLUE_DOOR_SEEN */
            topic_say("Pell turns the seal over in his fingers, then hands it back. \"Blue wax, certainly. But that proves very little.\"");
            topic_say("\"Vey wants proof. And for once, I think he has a point.\"");
            return 1u;
        }
        game_inventory_remove_one(43u);
        topic_say("Pell compares the seal with the torn invitation, then glances toward the blue-marked door. \"There. Same mark. Same wax. I knew I hadn't imagined the whole cursed thing.\"");
        game_inventory_add_one(10u); /* Brass Key */
        topic_say("He searches his mailbag. \"I found this beneath the door when I arrived. Thought it was a room key, but it fits nothing upstairs.\"");
        game_state.flags[13] = 1u; /* STORY_STATE_ROOM_05_PELL_HELPED */
        topic_say("\"Odd thing, though. My invitation never named Vey. I assumed he was the host. Perhaps I assumed too much.\"");
        return 1u;
    }

    if (script_resource_id == 20u && prefix_is(prefix, letter_word)) {
        if (game_state.flags[16] || !game_inventory_has_one(57u)) {
            return 0u; /* STORY_STATE_ROOM_02_ORRA_HELPED; Orra Invite */
        }
        game_inventory_remove_one(57u);
        topic_say("Lady Orra takes the letter, and her composure slips for just a moment as she reads it. \"So it was meant for me after all,\" she says quietly.");
        game_inventory_add_one(55u); /* Moon Brooch */
        game_state.flags[16] = 1u; /* STORY_STATE_ROOM_02_ORRA_HELPED */
        topic_say("She unpins a small brooch from her collar, shaped like a crescent moon. \"Take this. Whatever door it was meant for, I'd rather you reach it than I.\"");
        return 1u;
    }

    return 0u;
}

/* One conversation: prompt, read a keyword, run the matching topic (or the
 * "*" fallback), and let the player read the answer before the prompt comes
 * back. No banner text and no RUN/STOP hint - just "> " and a blinking
 * cursor (read_keyword()); RUN/STOP still leaves, discovered the same way
 * as everything else here (see GAME_DESIGNER_GUIDE.md's discovery note).
 * Leaves on RUN/STOP, on typed "bye", or when a topic used CONVERSE to
 * hand over to another conversation. */
static void run_conversation(uint8_t topic_count) {
    uint8_t prefix[KEYWORD_BYTES];
    uint16_t entry;

    for (;;) {
        if (!read_keyword(prefix)) return;
        if (prefix_is_bye(prefix)) {
            platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
            platform_text_output_color = 1u;
            platform_text_output_native("\"Farewell, then.\"");
            wait_fresh_key();
            return;
        }
        /* Trigger-word puzzle resolutions (see story_topic_resolve()'s own
         * comment above) take priority over a conversation's own topic
         * content - mentioning the right word while carrying the right
         * item resolves a puzzle immediately; story_topic_resolve() shows
         * and dismisses its own text when it handles the input, so the
         * ordinary topic lookup below is skipped entirely for that turn. */
        if (story_topic_resolve(prefix)) continue;
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
        if (script_text_shown) wait_fresh_key();
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
    } else {
        exec_block(3u, script_len);
    }
}

extern volatile uint8_t platform_raster_irq_active;
void raster_irq_resume(void);

/* Full orchestration around exec_resource(), not just the bytecode
 * execution - deliberately kept in this bank rather than split back out
 * to a resident wrapper (as it briefly was during this port): every call
 * here is an ordinary resident function call, safe from a banked caller
 * under oscar64 (see memmap.h's header comment), so there is no benefit
 * to spending resident budget (src/memmap.h; already razor-thin - see
 * src/resources.c's header comment) on logic that runs no differently
 * from here. Confirmed live this session: with this logic resident
 * instead, the real src/main.c overflowed the resident region again after
 * being link-clean for the first time all migration. */
__noinline void script_banked_run(void) {
    uint8_t was_suspended;

    platform_look_cursor_hide();
    script_text_shown = 0u;
    script_conversation_pending = 0u;
    was_suspended = !platform_raster_irq_active;
    /* If we got here with the raster IRQ still suspended, the caller is a
     * room's enter_room()/enter_tile() hook, itself called from inside
     * game_process_pending_transition()'s whole-switch suspend bracket
     * (src/game.c) - the only way to reach a room's own code at all. From
     * this point on, the interpreter may write real, readable text to the
     * status rows (a room's arrival/tile-entry narration), and that needs
     * the ordinary per-frame map/text charset split actually running to
     * render as text instead of tile-charset garbage - suspend pins the
     * display on one static charset (tile, in normal gameplay) for its
     * whole duration, precisely because nothing was expected to need the
     * split *during* a transition before this. Resuming here is safe
     * despite still being mid-transition: platform_room_enter()'s own
     * EasyFlash work, and the environment module's checksum-race-sensitive
     * fetch (see game_enter_room()'s comment in src/game.c), are both
     * already fully complete by the time any room-entry/tile-entry hook
     * runs at all. Deliberately not re-suspending afterward: the pager's
     * own between-pages wait and this function's own game_wait_fresh_key()
     * below both need the split alive too, for exactly the same reason,
     * for as long as text might still be on screen; game_process_pending_
     * transition()'s own raster_irq_resume() call, later, simply becomes a
     * harmless no-op once this has already run.
     *
     * The same "still mid-transition" window is also still screen-blanked
     * (DEN off): game_process_pending_transition()'s bracket (src/game.c)
     * calls game_transition_message_show() - which blanks whenever no
     * transition message is pending, i.e. the ordinary case - *before*
     * platform_room_enter()/game_enter_room(), and nothing unblanks again
     * until game_transition_reveal() at the very end of that same bracket.
     * Unblank here for the same reason resume happens here, and leave it
     * unblanked for the same reason interrupts are left resumed:
     * game_transition_reveal()'s own blank()-draw-unblank() cycle, later,
     * still runs unconditionally and will blank again right before it
     * draws the new room regardless of what state this leaves the screen
     * in. */
    if (was_suspended) {
        raster_irq_resume();
        platform_screen_unblank();
    }
    exec_resource();

    /* CONVERSE queued a conversation: start it now that the script (and its
     * buffer) is finished, after letting the player read whatever the script
     * said - e.g. a greeting - since the conversation prompt clears the text
     * rows. The portrait the script showed stays up during the conversation. */
    while (script_conversation_pending) {
        script_conversation_pending = 0u;
        if (script_text_shown) {
            wait_fresh_key();
            script_text_shown = 0u;
        }
        script_resource_kind = PLATFORM_RESOURCE_KIND_CONVERSATION;
        script_resource_id = script_conversation_id;
        exec_resource();
    }

    /* exec_resource() borrowed the room-staging scratch buffer (see
     * platform_room_scratch() in platform.h) - nothing else reads it
     * between calls, so it's left as-is - and any portrait it showed; put
     * that back the way normal gameplay expects to find it. */
    platform_portrait_hide();
    /* Give the player a chance to read room/standalone-script text before
     * wiping it - clearing and redrawing unconditionally and immediately
     * would flash a Look/Use description's last page for, at most, a
     * handful of CPU cycles before erasing it. Gated on script_text_shown
     * so this adds no new keypress where there is nothing to protect: a
     * conversation manages its own pacing (each loop iteration's "Ask
     * about..." prompt already overwrites the previous topic's answer only
     * once the player has typed something, and exiting via RUN/STOP is
     * itself the acknowledgment - an extra wait here would demand a
     * second, redundant keypress after that exit), and a script that only
     * ran effects/a transition (e.g. a lightning/room_transition_here
     * sequence) never sets the flag. Also skipped when a transition
     * message is pending: that path already has its own, better wait in
     * game_transition_reveal(), called once the queued transition
     * actually runs - waiting twice would demand two keypresses for one
     * message. */
    if (script_text_shown && !game_transition_pending_message) {
        game_wait_fresh_key();
    }
    platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
    platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
}

#pragma code(code)
#pragma data(data)
