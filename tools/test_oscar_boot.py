#!/usr/bin/env python3
"""Cold-boot/keyboard regression: xvfb-run -a python3 tools/test_oscar_boot.py.

Requires VICE, Xvfb and python3-xlib. Inputs are real XTest key events;
monitor breakpoints synchronize observations with the game's input polls.
"""
import argparse
import re
import select
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from Xlib import X, XK, display
from Xlib.ext import xtest


class Monitor:
    def __init__(self, connection, transcript):
        self.connection = connection
        self.transcript = transcript
        connection.settimeout(15)

    def receive(self):
        data = b""
        while True:
            chunk = self.connection.recv(65536)
            if not chunk:
                raise RuntimeError("VICE closed its monitor")
            data += chunk
            if re.search(rb"\(C:\$[0-9a-fA-F]{4}\)\s*$", data):
                if not select.select([self.connection], [], [], 0.01)[0]:
                    result = data.decode(errors="replace")
                    self.transcript.write(result)
                    self.transcript.flush()
                    return result

    def command(self, command):
        self.transcript.write(command + "\n")
        self.connection.sendall((command + "\n").encode())
        return self.receive()

    def run(self):
        self.connection.sendall(b"x\n")
        return self.receive()

    def memory(self, address, size, bank="cpu"):
        self.command("bank " + bank)
        result = self.command(f"m {address:04x} {address + size - 1:04x}")
        data = bytearray()
        for line in result.splitlines():
            match = re.search(r">C:[0-9a-fA-F]{4}  (.*)", line)
            if match:
                # Each line contains up to 16 bytes, then a PETSCII rendering.
                data.extend(int(x, 16) for x in re.findall(
                    r"\b[0-9a-fA-F]{2}\b", match[1][:51]))
        assert len(data) == size, (address, size, len(data), result)
        self.command("bank cpu")
        return bytes(data)


def find_window(window):
    if "VICE" in (window.get_wm_name() or ""):
        return window
    for child in window.query_tree().children:
        found = find_window(child)
        if found:
            return found


def check(root, monitor, artifacts):
    labels = dict((name, int(address, 16)) for address, name in re.findall(
        r"al ([0-9a-f]+) \.(\S+)", (root / "build/game.lbl").read_text()))
    listing = (root / "build/game.asm").read_text()
    poll = listing.split("\nplatform_input_poll:", 1)[1].split("\n----", 1)[0]
    poll_return = int(re.search(r"^([0-9a-f]+) : .* RTS", poll, re.M)[1], 16)

    screen = display.Display()
    window = find_window(screen.screen().root)
    assert window, "VICE window not found"
    window.set_input_focus(X.RevertToParent, X.CurrentTime)
    screen.sync()

    # Title screen (modules/title.c): runs before platform_init(), polling
    # start_pressed() once per frame. No other banked code has run yet, so
    # a breakpoint on its banked address can only hit the title.
    monitor.command(f"break exec {labels['start_pressed']:04x}")
    for _ in range(60):
        monitor.run()
    monitor.command("delete")
    assert monitor.memory(0xdd00, 1)[0] & 3 == 1, "title: VIC bank 2 not selected"
    heading = bytes([1, 32, 7, 1, 13, 5, 32, 2, 25])  # "a game by"
    assert monitor.memory(0x8000 + 21 * 40 + 15, 9, "ram") == heading, \
        "title: first credit heading not drawn"
    title_art = (root / "assets/title.hires").read_bytes()
    assert monitor.memory(0xa000, 64, "ram") == title_art[:64], "title: bitmap not loaded"
    monitor.command(f'screenshot "{artifacts / "title.png"}" 2')
    # Any key starts the game; the title waits for the release itself.
    monitor.command(f"break exec {labels['platform_input_poll']:04x}")
    space = screen.keysym_to_keycode(XK.string_to_keysym("space"))
    monitor.connection.sendall(b"x\n")
    time.sleep(.5)
    xtest.fake_input(screen, X.KeyPress, space)
    screen.sync()
    time.sleep(.5)
    xtest.fake_input(screen, X.KeyRelease, space)
    screen.sync()
    monitor.receive()
    monitor.command("delete")
    print("PASS: title screen picture, credits and key to start", flush=True)

    charset = (root / "assets/charset.cchr").read_bytes()[8:4104]
    actual = monitor.memory(0xe800, 4096, "ram")
    assert actual[:112] == charset[:112], "tile charset was not loaded"
    assert actual[120:] == charset[120:], "tile/text charset mismatch"
    water = charset[112:120]
    rotations = [bytes(((x << n) | (x >> (8-n))) & 255 for x in water)
                 for n in range(8)]
    assert actual[112:120] in rotations, "water shadow read KERNAL instead of RAM"
    objects = (root / "build/assets/objects-hot.bin").read_bytes()
    assert monitor.memory(0xc000, 4095, "ram") == objects[3710:7805]
    assert monitor.memory(0xdc02, 2) == bytes([255, 0]), "keyboard CIA directions"
    assert monitor.memory(0xdd02, 1)[0] & 3 == 3, "VIC bank port direction"
    assert monitor.memory(0xdd00, 1)[0] & 3 == 0, "VIC bank selection"
    assert monitor.memory(0x0289, 1) == b"\x0a", "keyboard buffer capacity"
    assert monitor.memory(0x032a, 2) == b"\x3e\xf1", "GETIN vector (stock KERNAL)"
    assert monitor.memory(1, 1)[0] & 7 == 7, "cartridge/KERNAL CPU mapping"
    monitor.command(f'screenshot "{artifacts / "boot.png"}" 2')
    print("PASS: cold-boot assets, water, CIA configuration and KERNAL vectors", flush=True)

    monitor.command(f"break exec {poll_return:04x}")

    def polls(count):
        keys = []
        for _ in range(count):
            stopped = monitor.run()
            keys.append(int(re.search(r" A:([0-9a-fA-F]{2})", stopped)[1], 16))
        return keys

    def press(key, expected):
        code = screen.keysym_to_keycode(XK.string_to_keysym(key))
        xtest.fake_input(screen, X.KeyPress, code)
        screen.sync()
        try:
            for _ in range(40):
                if polls(1)[0] == expected:
                    break
            else:
                raise AssertionError(f"{key} did not produce PETSCII ${expected:02x}")
        finally:
            xtest.fake_input(screen, X.KeyRelease, code)
            screen.sync()
        polls(5)

    def press_shifted(key, expected):
        # A real Shift_L press+hold, not a "shift" keysym like "Up" whose
        # own vkm entry auto-applies shift - letter keysyms in VICE's
        # symbolic keymap pass through whatever shift state the host event
        # itself carries (flag 8, "can be shifted or not").
        shift = screen.keysym_to_keycode(XK.string_to_keysym("Shift_L"))
        code = screen.keysym_to_keycode(XK.string_to_keysym(key))
        xtest.fake_input(screen, X.KeyPress, shift)
        screen.sync()
        xtest.fake_input(screen, X.KeyPress, code)
        screen.sync()
        try:
            for _ in range(40):
                if polls(1)[0] == expected:
                    break
            else:
                raise AssertionError(f"shift+{key} did not produce PETSCII ${expected:02x}")
        finally:
            xtest.fake_input(screen, X.KeyRelease, code)
            screen.sync()
            xtest.fake_input(screen, X.KeyRelease, shift)
            screen.sync()
        polls(5)

    first_frame = monitor.memory(labels["platform_frame_counter"], 1)[0]
    press("l", 76)
    assert monitor.memory(labels["command"], 1) == b"L", "Look mode not entered"
    # Cursor sprite (src/platform.c's platform_look_cursor_show(), sprite 0
    # at $FC00): a square outline, solid top/bottom bar plus a one-pixel
    # left and right edge for the 16 rows between. Checked byte-for-byte,
    # not just "Look mode entered" - Oscar64 -Os previously merged the
    # loop's two per-iteration array writes onto one induction variable,
    # silently dropping every left-edge pixel except row 1's (see
    # OSCAR64_BUG_LOOP_INDEX_MERGE.md), which a mode-only check can't catch.
    expected_cursor = bytearray(64)
    expected_cursor[0:3] = bytes([0xFF, 0xFF, 0xC0])
    expected_cursor[51:54] = bytes([0xFF, 0xFF, 0xC0])
    for row in range(1, 17):
        expected_cursor[row * 3] = 0x80
        expected_cursor[row * 3 + 2] = 0x40
    assert monitor.memory(0xfc00, 64, "ram") == bytes(expected_cursor), \
        "Look cursor sprite is not a complete square outline"
    assert monitor.memory(0xfb98, 10, "ram") == bytes([76, 15, 15, 11, 9, 14, 7, 46, 46, 46]), "display string encoding"
    monitor.command(f'screenshot "{artifacts / "look.png"}" 2')
    press("l", 76)
    assert monitor.memory(labels["command"], 1) == b"\0", "Look mode not cancelled"
    press("i", 73)
    assert monitor.memory(labels["text_screen_active"], 1) == b"\1", "inventory not entered"
    monitor.command(f'screenshot "{artifacts / "inventory.png"}" 2')
    press("i", 73)
    assert monitor.memory(labels["text_screen_active"], 1) == b"\0", "inventory not exited"
    player = int.from_bytes(monitor.memory(labels["platform_player"], 2), "little")
    before = monitor.memory(player, 3)
    press("Down", 17)
    assert monitor.memory(player, 3) != before, "player did not move"
    polls(10)
    assert monitor.memory(labels["platform_frame_counter"], 1)[0] != first_frame
    assert monitor.memory(labels["platform_cart_bank"], 1) == b"\0", "nested bank not restored"
    # Observe visible scanlines as well: a whole-frame resync stall can
    # pass foreground logic checks while showing text glyphs over the map.
    monitor.command("delete")
    monitor.command("break exec 0000 ffff if RL == $0064")
    monitor.run()
    assert monitor.memory(0xd018, 1)[0] & 0xfe == 0xea, "map uses wrong charset"
    assert monitor.memory(0xd015, 1)[0] & 0xfe == 0xfe, "rain sprites hidden over map"
    monitor.command("delete")
    monitor.command("break exec 0000 ffff if RL == $00f0")
    monitor.run()
    assert monitor.memory(0xd018, 1)[0] & 0xfe == 0xec, "text uses wrong charset"
    monitor.command(f'screenshot "{artifacts / "moved.png"}" 2')
    print("PASS: Xvfb Look/cancel, text encoding, inventory, movement, rain and raster split", flush=True)

    # Queue normal game transitions (scenario setup, not keyboard injection).
    # This exercises the real room/env loading path and its relocated entries.
    monitor.command("delete")
    monitor.command(f"break exec {poll_return:04x}")
    spawn = (root / "assets/00").read_bytes()[234:236]
    for room, x, y in ((2, 17, 19), (3, 12, 18), (4, 32, 4), (5, 4, 18), (0, *spawn)):
        pending = labels["game_state"] + 16
        monitor.command(f"> {pending:04x} 01 {room:02x} {x:02x} {y:02x}")
        polls(5)
        assert monitor.memory(labels["platform_current_room"], 1) == bytes([room])
        entries = monitor.memory(0x7e00, 8)
        assert all(0x7e08 <= int.from_bytes(entries[i:i+2], "little") < 0x8000
                   for i in range(0, 8, 2)), f"room {room}: invalid environment entry table"
        def sid_registers():
            result = monitor.command("io d400")
            registers = bytearray()
            for line in result.splitlines():
                match = re.search(r">C:d4[01]0  (.*)", line)
                if match:
                    registers.extend(int(x, 16) for x in re.findall(
                        r"\b[0-9a-fA-F]{2}\b", match[1][:51]))
            assert len(registers) == 32, "could not inspect SID registers"
            return registers

        sid_before = sid_registers()
        polls(45)
        sid_after = sid_registers()
        if room in (2, 3, 5):
            assert sid_before[:2] != sid_after[:2], f"room {room}: melody frequency not advancing"
            assert sid_after[4] & 1 and sid_after[24] & 15, f"room {room}: SID gate/volume disabled"
        monitor.command(f'screenshot "{artifacts / ("room-%02d.png" % room)}" 2')
        assert monitor.memory(labels["platform_cart_bank"], 1) == b"\0"
        print(f"PASS: room {room:02d} environment and frame progression", flush=True)

        if room == 2:
            # Sir Caldus's portrait (assets/portraits/00,
            # PLATFORM_RESOURCE_KIND_PORTRAIT - src/resources.c): move the
            # Look cursor to his tile (14,1) and check sprites 1-4 (the
            # 256-byte, four-quadrant portrait, contiguous at $FC40 - see
            # PLATFORM_API.md's Portraits section) against the source asset
            # byte-for-byte. This bank had no embedded content at all before
            # the fix (confirmed absent from build/game.crt), so every
            # portrait rendered as a blank/white sprite regardless of which
            # asset was requested. Done here, inside this room's own already-
            # proven-working visit, rather than a fresh teleport afterward -
            # a second scripted transition immediately following this loop's
            # last one is not reliably picked up by game_process_pending_
            # transition() in this harness, for reasons not pinned down.
            press("l", 76)
            for _ in range(6):
                press("Right", 29)
            for _ in range(8):
                press("Up", 145)
            press("Return", 13)
            polls(20)
            monitor.command(f'screenshot "{artifacts / "caldus-portrait.png"}" 2')
            portrait_sprites = monitor.memory(0xfc40, 256, "ram")
            assert portrait_sprites == (root / "assets/portraits/00").read_bytes(), \
                "Caldus's portrait sprites do not match assets/portraits/00"
            # LOOK_CALDUS (assets/scripts/02.script) blocks in wait_key with
            # the portrait still shown; any fresh key releases it
            # (portrait_hide, then back to the main loop) - but the same
            # keypress can also be the one the *outer* game loop's own key
            # dispatch sees next, re-entering Look mode (main.c dispatches
            # 'l' to open Look mode whenever `command` is 0, and GETIN can
            # still report one held key across more than one poll). Send a
            # second 'l' - the established cancel toggle elsewhere in this
            # file - so Look mode is not left open regardless.
            press("l", 76)
            press("l", 76)
            polls(15)
            print("PASS: Sir Caldus's portrait renders the authored asset", flush=True)

            # Script-driven inventory (modules/script.c's check_item/
            # give_object, dispatched from rooms/02.rc's `at 12,1 -> script
            # STORY_ROOM02_USE_HEARTH` - no C fragment involved): standing
            # one tile below the hearth with nothing to light, Use collects
            # Hearth Ash. Tile (12,2) is half-tile (24,4), walkable floor.
            inventory = labels["game_state"] + 20  # GameState.inventory[32]
            def inventory_has(object_type):
                slots = monitor.memory(inventory, 64)
                return any(slots[i] == object_type and slots[i + 1] >= 1
                           for i in range(0, 64, 2))
            monitor.command(f"> {pending:04x} 01 02 18 04")
            polls(5)
            assert not inventory_has(60), "Hearth Ash carried before using the hearth"
            press("u", 85)
            press("Up", 145)
            press("Return", 13)
            polls(5)
            assert inventory_has(60), "Hearth Ash not given by the hearth script"
            monitor.command(f'screenshot "{artifacts / "hearth-ash.png"}" 2')
            press("space", 32)  # dismiss the script's text
            polls(5)
            # A second Use must not hand out a second scoop (the check_item
            # OBJ_HEARTH_ASH branch), only the "nothing to light" flavor.
            press("u", 85)
            press("Up", 145)
            press("Return", 13)
            polls(5)
            slots = monitor.memory(inventory, 64)
            ash = [slots[i + 1] for i in range(0, 64, 2) if slots[i] == 60]
            assert ash == [1], f"Hearth Ash quantity {ash} after a second Use"
            press("space", 32)
            polls(5)
            print("PASS: script check_item/give_object at the hearth", flush=True)

            # Take (the resident command, src/game_support.c): the Plant at
            # tile (1,6) from the floor tile beside it, (2,6) = half (4,12).
            # Removes the room object, journals a delta, adds the item.
            delta_count = labels["game_world_delta_count"]
            monitor.command(f"> {pending:04x} 01 02 04 0c")
            polls(5)
            before_deltas = monitor.memory(delta_count, 2)
            press("t", 84)
            press("Left", 157)
            press("Return", 13)
            polls(5)
            assert inventory_has(46), "Plant not added to the inventory by Take"
            after_deltas = int.from_bytes(monitor.memory(delta_count, 2), "little")
            assert after_deltas == int.from_bytes(before_deltas, "little") + 1, \
                "Take did not journal exactly one world-state delta"
            monitor.command(f'screenshot "{artifacts / "take-plant.png"}" 2')
            print("PASS: Take removes the object, journals a delta, fills the inventory", flush=True)

    # Kitchen (03) back to the tavern (02) - the reported direction; the
    # loop above only ever goes tavern-to-kitchen. A SID voice left gated
    # on and held at sustain does not restart from a too-close gate-low/
    # gate-high pair (real-hardware-measured; see rooms/env/rain.c and
    # rooms/env/tavern.c's env_tavern_init()) - env_tavern_init() holds the
    # gate low for a couple of frames first to avoid it, but a plain C
    # delay loop for that (even over a `volatile` counter) does not survive
    # Oscar64 -Os, silently losing the delay entirely; fixed with inline
    # asm (confirmed present in build/game.asm; see OSCAR64_BUG_LOOP_INDEX_
    # MERGE.md's sibling finding for the general "-Os can eliminate this"
    # caution).
    #
    # This only checks that env_tavern_init() completes and every voice's
    # gate bit ends up set - it does NOT confirm the envelope genuinely
    # retriggers audibly. ENV3 ($D41C) would be the one voice SID hardware
    # exposes for that, but this suite's shared VICE process is not run
    # with an explicit `-sounddev wav`/real SID engine, and per
    # [[project-vice-audio-capture]] (this project's own prior finding)
    # OSC3/ENV3 reads come back frozen and meaningless without that - tried
    # here, reads 0 even on a first, definitely-working room 02 entry with
    # no prior gate-retrigger hazard at all, confirming the reading itself
    # is untrustworthy in this harness rather than evidence against the fix.
    # A real audio capture (per that memory's recipe) would be the rigorous
    # check; not done here.
    monitor.command("delete")
    monitor.command(f"break exec {poll_return:04x}")
    pending = labels["game_state"] + 16
    monitor.command(f"> {pending:04x} 01 03 0c 12")
    polls(5)
    assert monitor.memory(labels["platform_current_room"], 1) == bytes([3])
    polls(150)  # let room 03's V1/V2 reach sustain
    monitor.command(f"> {pending:04x} 01 02 11 13")
    polls(5)
    assert monitor.memory(labels["platform_current_room"], 1) == bytes([2])
    polls(20)
    result = monitor.command("io d400")
    sid = bytearray()
    for line in result.splitlines():
        match = re.search(r">C:d4[01]0  (.*)", line)
        if match:
            sid.extend(int(x, 16) for x in re.findall(
                r"\b[0-9a-fA-F]{2}\b", match[1][:51]))
    assert len(sid) == 32, "could not inspect SID registers"
    assert sid[0x12] & 1 and sid[0x0b] & 1 and sid[0x04] & 1, \
        "kitchen->tavern: not all three voices show their gate bit set"
    monitor.command(f'screenshot "{artifacts / "kitchen-to-tavern.png"}" 2')
    print("PASS: kitchen-to-tavern SID gate-retrigger delay", flush=True)

    # Pell's conversation (assets/scripts/conversations/05.script, reached
    # from room 05's TALK_PELL entry via `converse`): drives the typed-
    # keyword UI (a bare "> " prompt, no banner text - modules/script.c's
    # read_keyword()) end to end through the same platform_input_poll()
    # breakpoint used for every other key in this test - it works unchanged
    # here because the conversation's own busy-wait loops (modules/script.c)
    # call that same resident function, banked or not.
    monitor.command("delete")
    monitor.command(f"break exec {poll_return:04x}")
    pending = labels["game_state"] + 16
    # Room 5, half-tile (8,16) = tile (4,8): one tile south of Pell (4,7),
    # so the Use cursor (born on the player's own tile) reaches him with a
    # single Up press.
    monitor.command(f"> {pending:04x} 01 05 08 10")
    polls(5)
    assert monitor.memory(labels["platform_current_room"], 1) == bytes([5])
    met_pell = labels["game_state"] + 84 + 10  # flags[10] = STORY_STATE_ROOM_05_MET_PELL
    assert monitor.memory(met_pell, 1) == b"\0", "MET_PELL already set before talking to Pell"

    def input_row(count):
        return monitor.memory(0xf800 + 24 * 40, count, "ram")

    def advance_to_prompt(max_presses):
        # Any text long enough to word-wrap past two lines needs its own
        # internal pager wait (next_text_line() in src/text.c) on top of the
        # script's own explicit wait_key opcodes and the deferred wait
        # CONVERSE adds (script_banked_run() in modules/script.c) - so the
        # number of keypresses needed isn't fixed. Overshooting is harmless:
        # once the "> " prompt is up, read_keyword() discards a space typed
        # at an empty prompt rather than echoing it.
        for _ in range(max_presses):
            press("space", 32)
            if input_row(2) == bytes([0x3E, 0x20]):
                return
        raise AssertionError("conversation prompt never appeared")

    press("u", 85)
    press("Up", 145)
    press("Return", 13)
    advance_to_prompt(10)
    # By the time the prompt is up, TALK_PELL's set_flag has certainly run:
    # it precedes CONVERSE unconditionally in bytecode order.
    assert monitor.memory(met_pell, 1) == b"\1", "MET_PELL not set by the greeting"
    monitor.command(f'screenshot "{artifacts / "conversation-prompt.png"}" 2')

    for letter, code in (("n", 78), ("a", 65), ("m", 77), ("e", 69)):
        press(letter, code)
    # Echoed input is poked straight to screen RAM (raw PETSCII, 65-90 is
    # already this charset's uppercase glyph code) - see read_keyword()'s
    # comment on why platform_text_screen_code() would be the wrong call here.
    assert input_row(6) == bytes([0x3E, 0x20, 78, 65, 77, 69]), \
        "typed keyword not echoed on the input row"
    press("Return", 13)
    advance_to_prompt(5)  # read the NAME answer, back to the prompt
    monitor.command(f'screenshot "{artifacts / "conversation-name.png"}" 2')

    for letter, code in (("z", 90), ("z", 90), ("z", 90), ("z", 90)):
        press(letter, code)
    press("Return", 13)  # no topic starts with ZZZZ -> the "*" fallback
    advance_to_prompt(5)

    press("Escape", 3)  # RUN/STOP leaves the conversation
    polls(10)
    assert monitor.memory(labels["platform_cart_bank"], 1) == b"\0", \
        "bank not restored after the conversation"
    player = int.from_bytes(monitor.memory(labels["platform_player"], 2), "little")
    before = monitor.memory(player, 3)
    press("Down", 17)
    after = monitor.memory(player, 3)
    assert after != before, "player cannot move after leaving the conversation"
    monitor.command(f'screenshot "{artifacts / "conversation-done.png"}" 2')
    print("PASS: Xvfb conversation (Ask about, typed keyword, NAME/JOB, "
          "unmatched fallback, RUN/STOP)", flush=True)

    # Save/load round trip on the disposable D64 attached as device 8. Disk
    # I/O suspends the raster IRQ, so no input polls (the monitor's sync
    # points) happen for as long as the emulated 1541 takes - allow for it.
    # F1 also crosses banks 6 -> 14 -> 7 -> 14, which crashed when banked
    # code expanded far_call_bank() inside the $8000-$BFFF window.
    monitor.connection.settimeout(240)
    monitor.command("delete")
    monitor.command(f"break exec {poll_return:04x}")

    def player_bytes():
        pointer = int.from_bytes(monitor.memory(labels["platform_player"], 2), "little")
        return monitor.memory(pointer, 3)

    saved = player_bytes()
    press("F1", 133)
    assert monitor.memory(labels["text_screen_active"], 1) == b"\1", "save browser not entered"
    assert monitor.memory(labels["saveload_mode"], 1) == b"\0", "not in save mode"
    monitor.command(f'screenshot "{artifacts / "save-browser.png"}" 2')
    press("Down", 17)
    press("Return", 13)
    assert monitor.memory(labels["saveload_selected_slot"], 1) == b"\1", "slot 1 not selected"
    monitor.command(f'screenshot "{artifacts / "save-name.png"}" 2')
    # Mixed-case name, usual C64 convention (unshifted -> lowercase, shift
    # -> uppercase): unshifted m/c/t (petscii 77/67/84, folding to 13/3/20
    # - the exact byte values of RETURN/RUN-STOP/DEL) exercise
    # modules/saveload_save.c's enter_name() checking those control keys
    # against the raw, unfolded key first. If that ordering regressed,
    # plain 'm' would end name entry outright (mistaken for RETURN) instead
    # of typing lowercase 'm', and the assertion below would see a short/
    # wrong name. Shifted a/b then confirm the uppercase side too.
    press("m", 77)
    press("c", 67)
    press("t", 84)
    press_shifted("a", 193)
    press_shifted("b", 194)
    # NAME_SCREEN (modules/saveload_save.c), row 10 col 15: echoed raw, not
    # through platform_text_screen_code() (see AGENTS.md's char-literal note)
    # - 13,3,20 are this charset's lowercase m/c/t glyph codes ($01-$1A),
    # 65,66 are 'A'/'B' as its uppercase glyph codes (identity).
    assert monitor.memory(0xf99f, 5, "ram") == bytes([13, 3, 20, 65, 66]), \
        "mixed-case save name not echoed correctly"
    press("Return", 13)
    monitor.command(f'screenshot "{artifacts / "save-done.png"}" 2')
    # "Saved." at column 2, row 10 of the screen (vs. "Save failed.").
    assert monitor.memory(0xf800 + 10 * 40 + 2, 6, "ram") == bytes([83, 1, 22, 5, 4, 46]), \
        "save did not report success"
    press("space", 32)
    assert monitor.memory(labels["text_screen_active"], 1) == b"\0", "save did not return to gameplay"
    assert monitor.memory(labels["platform_cart_bank"], 1) == b"\0", "bank not restored after save"
    assert player_bytes() == saved, "save changed the player"
    print("PASS: Xvfb F1 save (browser, name entry, write, readback, index)", flush=True)

    press("Down", 17)
    assert player_bytes() != saved, "player did not move after save"
    press("F3", 134)
    assert monitor.memory(labels["text_screen_active"], 1) == b"\1", "load browser not entered"
    assert monitor.memory(labels["saveload_mode"], 1) == b"\1", "not in load mode"
    monitor.command(f'screenshot "{artifacts / "load-browser.png"}" 2')
    press("Down", 17)
    press("Return", 13)
    polls(10)
    assert monitor.memory(labels["text_screen_active"], 1) == b"\0", "load did not return to gameplay"
    assert monitor.memory(labels["platform_cart_bank"], 1) == b"\0", "bank not restored after load"
    assert player_bytes() == saved, "load did not restore the saved player position"
    monitor.command(f'screenshot "{artifacts / "loaded.png"}" 2')
    print("PASS: Xvfb F3 load restores the saved state", flush=True)


def check_save_disk(disk, c1541, artifacts):
    """After VICE exits: the disposable image holds a valid slot 1 and index."""
    listing = subprocess.run([c1541, "-attach", str(disk), "-list"],
                             capture_output=True, text=True, check=True).stdout
    for name in ("s1", "sindex"):
        assert f'"{name}"' in listing.lower(), f"{name} missing from disk:\n{listing}"
    record = artifacts / "s1.bin"
    # Save files are SEQ: c1541 -read defaults to PRG and needs the ",s" type.
    subprocess.run([c1541, "-attach", str(disk), "-read", "s1,s", str(record)],
                   capture_output=True, check=True)
    data = record.read_bytes()
    assert data[:4] == b"C64S" and data[4] == 1, "bad save header"
    length = int.from_bytes(data[10:12], "little")
    checksum = int.from_bytes(data[12:14], "little")
    assert len(data) == 16 + length, "save size does not match its header"
    # v1 layout: 130-byte prefix + five bytes per room delta.
    assert length >= 130 and (length - 130) % 5 == 0, f"bad payload length {length}"
    assert sum(data[16:]) & 0xffff == checksum, "save checksum mismatch"
    # "mctAB" as typed: lowercase m/c/t (screen codes 13/3/20), then
    # uppercase A/B (65/66) - see the mixed-case name entry check above.
    assert data[16:21] == bytes([13, 3, 20, 65, 66]), "save name not stored"
    print("PASS: save disk holds valid S1 record and SINDEX", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vice", default="x64sc")
    parser.add_argument("--c1541", default="c1541")
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    artifacts = (args.artifacts or Path(tempfile.mkdtemp(prefix="oscar-boot-"))).resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    print(f"Artifacts: {artifacts}", flush=True)
    # Disposable save disk: never the user's build/saves.d64.
    disk = artifacts / "test-saves.d64"
    subprocess.run([args.c1541, "-format", "TEST,00", "d64", str(disk)],
                   capture_output=True, check=True)
    with socket.socket() as port_probe:
        port_probe.bind(("127.0.0.1", 0))
        port = port_probe.getsockname()[1]
    with (artifacts / "vice.log").open("w") as log, (artifacts / "monitor.log").open("w") as transcript:
        process = subprocess.Popen([
            args.vice, "-default", "+sound", "-pal", "-jamaction", "1", "-remotemonitor",
            "-remotemonitoraddress", f"ip4://127.0.0.1:{port}",
            "-cartcrt", str(root / "build/game.crt"), "-8", str(disk)],
            stdout=log, stderr=log)
        monitor = None
        try:
            deadline = time.monotonic() + 10
            while True:
                try:
                    connection = socket.create_connection(("127.0.0.1", port), timeout=.3)
                    break
                except OSError:
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise
                    time.sleep(.05)
            with connection:
                monitor = Monitor(connection, transcript)
                monitor.command("r")
                try:
                    check(root, monitor, artifacts)
                except Exception:
                    monitor.command("r")
                    monitor.command(f'screenshot "{artifacts / "failure.png"}" 2')
                    monitor.command(f'dump "{artifacts / "failure.vsf"}"')
                    raise
                connection.sendall(b"quit\n")
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    check_save_disk(disk, args.c1541, artifacts)


if __name__ == "__main__":
    main()
