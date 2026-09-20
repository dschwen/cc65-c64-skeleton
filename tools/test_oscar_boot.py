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
    monitor.command(f"break exec {labels['platform_input_poll']:04x}")
    monitor.run()
    monitor.command("delete")

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
    screen = display.Display()
    window = find_window(screen.screen().root)
    assert window, "VICE window not found"
    window.set_input_focus(X.RevertToParent, X.CurrentTime)
    screen.sync()

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

    first_frame = monitor.memory(labels["platform_frame_counter"], 1)[0]
    press("l", 76)
    assert monitor.memory(labels["command"], 1) == b"L", "Look mode not entered"
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
    press("a", 65)
    press("b", 66)
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
    assert data[16:18] == b"AB", "save name not stored"
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
