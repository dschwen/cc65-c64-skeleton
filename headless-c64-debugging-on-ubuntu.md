# Headless C64 Debugging on Ubuntu

The most practical setup for coding agents is **VICE running inside Xvfb and controlled through VICE's remote monitor**.

This gives an agent access to:

- CPU breakpoints, watchpoints, stepping, registers, and disassembly
- Memory inspection and modification
- Symbol and label loading
- PRG and disk-image loading
- C64 keyboard-buffer input
- Screenshots written directly to image files
- Emulator snapshots for reproducing failures
- Optional real host-key input through `xdotool`

VICE also has a structured binary-monitor protocol. That is a good foundation for a reusable agent-facing test harness.

## 1. Install the tools

```bash
sudo apt update
sudo apt install vice xvfb netcat-openbsd xdotool
```

Depending on the VICE package, the cycle-accurate C64 executable may be named `x64sc` or `x64`:

```bash
command -v x64sc || command -v x64
```

The examples below use `x64sc`. Substitute `x64` if necessary.

## 2. Start VICE on a virtual display

Start a virtual X server:

```bash
Xvfb :99 -screen 0 1024x768x24 &
export DISPLAY=:99
```

Then start VICE with its text remote monitor enabled:

```bash
x64sc \
  -remotemonitor \
  -remotemonitoraddress "ip4://127.0.0.1:6510" \
  -sounddev dummy \
  -autostart game.prg &
```

If your VICE build rejects that address syntax, inspect the locally installed options:

```bash
x64sc -help | grep -A2 -i remotemonitor
```

Binding the monitor to `127.0.0.1` is intentional. The monitor allows complete control of the emulated machine and should not be exposed to an untrusted network.

## 3. Connect to the monitor

```bash
nc 127.0.0.1 6510
```

Connecting to the monitor stops emulation and presents a monitor prompt. Some useful commands are:

```text
r
d 080d 0850
m 0400 047f
break 1234
watch store 0400 07e7
step
next
keybuf "HELLO\x0d"
screenshot "/tmp/c64-screen.png" 2
dump "/tmp/test.vsf"
x
```

The commands perform the following actions:

| Command | Purpose |
| --- | --- |
| `r` | Display CPU registers |
| `d 080d 0850` | Disassemble an address range |
| `m 0400 047f` | Display memory |
| `break 1234` | Break when the PC reaches `$1234` |
| `watch store 0400 07e7` | Stop on writes to screen RAM |
| `step` | Step into one instruction |
| `next` | Step over one instruction or subroutine |
| `keybuf "..."` | Add PETSCII text to the KERNAL keyboard buffer |
| `screenshot "..." 2` | Write a PNG screenshot; format 2 is PNG |
| `dump "..."` | Save an emulator snapshot |
| `x` | Leave the monitor and resume execution |

VICE can load assembler labels as well. This makes monitor disassembly and breakpoint commands substantially easier for an agent to interpret:

```text
load_labels "game.lbl"
```

The exact accepted label format depends on the assembler. VICE supports monitor-command labels and common ACME-style assignments, while cc65 can generate compatible label information.

## 4. Send commands non-interactively

Simple monitor operations can be scripted through `nc`.

Read the registers:

```bash
printf 'r\n' | nc -q 1 127.0.0.1 6510
```

Take a screenshot and resume execution:

```bash
printf 'screenshot "/tmp/c64.png" 2\nx\n' \
  | nc -q 1 127.0.0.1 6510
```

Queue `RUN` followed by Return:

```bash
printf 'keybuf "RUN\\x0d"\nx\n' \
  | nc -q 1 127.0.0.1 6510
```

For robust automation, use a small client that reads until the monitor prompt rather than depending only on `nc` timeouts. This avoids racing VICE when a command produces a long response or a breakpoint takes time to trigger.

## 5. Keyboard-input limitation

`keybuf` writes into the C64 KERNAL keyboard buffer. It works well for:

- BASIC commands
- Text entry
- Programs that call the normal KERNAL keyboard-input routines

It does **not** faithfully simulate arbitrary key press/release timing on the C64 keyboard matrix. A game that scans the CIA keyboard matrix directly may not see `keybuf` input in the intended way.

For such software, there are three reasonable approaches.

### Use host keyboard events

Find the hidden VICE window:

```bash
DISPLAY=:99 xdotool search --name VICE
```

Send a timed key press, substituting the returned window ID:

```bash
DISPLAY=:99 xdotool \
  keydown --window WINDOW_ID Left \
  sleep 0.15 \
  keyup --window WINDOW_ID Left
```

This depends on VICE's host-key mapping, window focus, and window title. It is useful for end-to-end testing but less deterministic than monitor-controlled state.

### Write joystick or input state through a debug hook

For a program under active development, expose a debug input byte or command queue in C64 memory. The agent can then modify it through the monitor. This is deterministic and makes failures much easier to reproduce.

For example, the program might reserve a byte whose bits represent up, down, left, right, and fire. Test builds read this byte instead of—or in addition to—the real joystick port.

### Use the binary monitor

A dedicated binary-monitor client can combine structured emulator control with other input mechanisms. This is preferable when building a permanent testing tool.

## 6. Text monitor versus binary monitor

### Text remote monitor

The text monitor is the best place to start because it is:

- Human-readable
- Easy for a coding agent to operate
- Available through a simple TCP connection
- Capable of normal debugger operations
- Convenient for experimental automation

Its disadvantage is that responses must be parsed from text.

### Binary monitor

Enable it with VICE's `-binarymonitor` and `-binarymonitoraddress` options. The binary protocol provides structured requests and responses for operations including:

- Reading and writing memory
- Reading and setting registers
- Checkpoints and stepping
- Feeding keyboard text
- Retrieving the current VIC-II display buffer
- Retrieving the palette
- Saving and restoring emulator state

The display request returns indexed framebuffer data rather than requiring a desktop screenshot. Combined with the palette response, a client can convert this directly to PNG.

The binary monitor is therefore the cleaner long-term interface for continuous integration or an agent-driven debugger, although it requires a protocol client.

## 7. Recommended agent workflow

A useful build-and-debug loop is:

1. Assemble or compile the program.
2. Launch or reset VICE and load the new binary.
3. Load its labels into the monitor.
4. Set breakpoints or memory watchpoints.
5. Resume execution and inject test input.
6. Wait for a breakpoint, known memory value, or timeout.
7. Capture registers, relevant memory, disassembly, and a screenshot.
8. Save a VICE snapshot when a failure occurs.
9. Let the coding agent inspect the textual state and screenshot together.

A small wrapper could present an agent-oriented interface such as:

```text
c64 start build/game.prg
c64 load-labels build/game.lbl
c64 wait-pc 0x8420
c64 read 0xc000 256
c64 keys "NORTH"
c64 press joystick1-up 200ms
c64 screenshot artifacts/room.png
c64 assert-byte 0x02a0 5
c64 save-state artifacts/failure.vsf
```

The wrapper should also enforce timeouts and preserve the monitor transcript. On failure, it should retain:

- The program binary and labels
- VICE version and startup arguments
- Monitor transcript
- Screenshot
- Emulator snapshot
- Relevant memory ranges
- Expected and actual test values

That gives a coding agent enough evidence to diagnose rendering, room logic, memory corruption, and input-handling problems without interacting with a visible desktop.

## 8. Practical recommendation

Start with **Xvfb plus the text remote monitor**. It requires very little custom code and already provides most of what a coding agent needs.

Use VICE's own `screenshot` command instead of taking a picture of the Xvfb desktop. Add `xdotool` only for tests that genuinely require host-key events. Once the workflow is proven, replace ad hoc `nc` scripts with a small binary-monitor client that exposes structured commands and returns PNG screenshots directly.

## References

- [VICE monitor manual](https://vice-emu.sourceforge.io/vice_12.html)
- [VICE binary monitor protocol](https://vice-emu.sourceforge.io/vice_13.html)
- [VICE settings and remote-monitor options](https://vice-emu.sourceforge.io/vice_6.html)
