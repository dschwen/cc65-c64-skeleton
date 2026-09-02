#include <stdint.h>

#include "game.h"

/* Independently linked script/conversation interpreter overlay ("SC").
 * Executes the bytecode format tools/compile_script.py compiles (see that
 * tool's docstring for the exact format and opcode encoding - this file
 * must stay in sync with it by hand, there is no shared source of truth).
 *
 * Loaded and run via game_script_play() in src/script_runtime.c, the same
 * $A4E9 pattern as every other overlay. The compiled bytecode itself is
 * separate from this overlay's own code: fetched at run time from the
 * generic sparse resource directory (resource IDs 240-255, reserved for
 * this - room text already claims 0-239) into platform_room_scratch()'s
 * buffer (room_stage's memory, borrowed - see platform.h). The resident
 * wrapper restores that buffer's real content (the current room's text)
 * once this overlay returns.
 *
 * Like modules/inventory.c and modules/saveload.c, this overlay links
 * against no cc65 runtime library (the resident game binary doesn't export
 * those helpers for the overlay resolver to bind against). Beyond their
 * documented avoid-list (memcpy/memset, division/modulo by a non-power-of-2
 * constant), this file also avoids: multiplying by a non-power-of-2
 * constant (needs mulax6 etc. - see find_topic's manual accumulation
 * instead of table + i*TOPIC_ENTRY_BYTES) and returning or assigning a bare
 * comparison's result as a value (needs booleq/boolne/boolult/boolugt/
 * boolule - see flag_matches/input_char_ok's explicit if/return instead of
 * `return a == b;`). A comparison used only inside an if/while condition is
 * fine either way, since it compiles to a plain branch.
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

#define KIND_CONVERSATION   1u

#define KEYWORD_BYTES       4u
#define TOPIC_ENTRY_BYTES   6u
#define KEY_RETURN          13u
#define KEY_RUN_STOP        3u
#define KEY_DELETE          20u

extern uint8_t script_resource_id;

void __fastcall__ platform_text_output_native(const char* text);
extern uint8_t platform_text_output_color;
extern uint8_t platform_text_output_line;

static uint8_t* script_buf;
static uint16_t script_len;

static uint16_t read_u16(uint16_t pos) {
    return (uint16_t)script_buf[pos] | ((uint16_t)script_buf[pos + 1] << 8);
}

/* Written as explicit if/return, not `return (a == b);` - a bare comparison
 * used as a value (returned, assigned) needs cc65's booleq/boolne/boolult/
 * boolugt/boolule runtime helpers, which this overlay can't link against
 * (see the module doc comment). A comparison used only to pick a branch
 * compiles to a plain conditional jump instead. */
static uint8_t flag_matches(uint8_t index, uint8_t cmp, uint8_t value) {
    uint8_t actual = game_state.flags[index];
    switch (cmp) {
        case 0: if (actual == value) return 1u; break;
        case 1: if (actual != value) return 1u; break;
        case 2: if (actual < value) return 1u; break;
        case 3: if (actual > value) return 1u; break;
        case 4: if (actual <= value) return 1u; break;
        default: if (actual >= value) return 1u; break;
    }
    return 0u;
}

static void wait_fresh_key(void) {
    while (platform_input_poll() != 0u) platform_wait_frame();
    while (platform_input_poll() == 0u) platform_wait_frame();
    while (platform_input_poll() != 0u) platform_wait_frame();
}

static void say(uint16_t str_offset) {
    platform_text_output_line = PLATFORM_TEXT_LINE_TOP;
    platform_text_output_color = 1u;
    platform_text_output_native((const char*)&script_buf[str_offset]);
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
        op = script_buf[pos];
        switch (op) {
            case OP_END:
                return;
            case OP_TEXT:
                say(read_u16(pos + 1u));
                pos += 3u;
                break;
            case OP_PORTRAIT_SHOW:
                (void)platform_portrait_show(script_buf[pos + 1u], script_buf[pos + 2u]);
                pos += 3u;
                break;
            case OP_PORTRAIT_HIDE:
                platform_portrait_hide();
                pos += 1u;
                break;
            case OP_SET_FLAG:
                game_state.flags[script_buf[pos + 1u]] = script_buf[pos + 2u];
                pos += 3u;
                break;
            case OP_CHECK_FLAG:
                index = script_buf[pos + 1u];
                cmp = script_buf[pos + 2u];
                value = script_buf[pos + 3u];
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
                (void)game_transition_request(script_buf[pos + 1u],
                                              script_buf[pos + 2u],
                                              script_buf[pos + 3u]);
                pos += 4u;
                break;
            case OP_SOUND:
                /* No sound-effect table yet; reserved for one. */
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
            if (script_buf[entry + j] != want) { match = 0u; break; }
        }
        if (match) return read_u16(entry + KEYWORD_BYTES);
        entry += TOPIC_ENTRY_BYTES;
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
    uint16_t fetched;

    script_buf = platform_room_scratch();
    fetched = platform_resource_fetch(script_resource_id, script_buf,
                                      platform_room_scratch_bytes());
    if (fetched < 3u) return;
    script_len = fetched;

    if (script_buf[1] == KIND_CONVERSATION) {
        run_conversation(script_buf[2]);
    } else {
        exec_block(3u, script_len);
    }
}
