# Save-disk encoding and completion checks

A prior failure looked like successful KERNAL output but produced no file:
DOS returned `62, FILE NOT FOUND`. The filename mode byte was high PETSCII
`$D7` rather than the required `$57` (`W`), so DOS did not recognize write
mode. The useful diagnostic is the actual byte sequence passed to SETNAM,
not the appearance of the source literal.

## Oscar64 encoding contract

The game uses mixed-case display literals through `#pragma charmap` in
`src/platform.h`. Disk protocol text must not inherit that mapping.
`src/disk_io.c` explicitly restores identity mappings for both letter ranges:

```c
#pragma charmap(65, 65, 26)
#pragma charmap(97, 97, 26)
```

CBM DOS expects `S=$53`, `W=$57`, comma `$2C`, colon `$3A`.
Slot names are `S0` through `S7`; the directory cache is `SINDEX`.
Inspect filename length and pointer as well as bytes: the scratch-command
view and read/write-name view share a buffer with different offsets.

## Drive completion is separate from KERNAL completion

KERNAL CLOSE finishes sending the close request before the drive necessarily
finishes its final block and directory update. The disk writer reads status
channel 15 after each write close; non-success status fails the operation.
Do not replace this with an arbitrary delay or trust READST alone.

The save workflow reads the complete record back and validates length/checksum
before updating SINDEX and reporting success. There is one file per slot:
an interrupted overwrite can lose that slot. Index corruption triggers the
browser's recovery scan.

## Verification procedure

Use a disposable D64, never the user's save disk:

```sh
c1541 -format "TEST,00" d64 /tmp/save-test.d64
xvfb-run -a x64sc -console -default -sounddev dummy \
  -cartcrt build/game.crt -8 /tmp/save-test.d64
```

Send real Xvfb key events to open Save, enter a name and confirm. Wait for
completion, return to gameplay, alter state, then Load and check restored
state. After VICE exits, inspect the directory with
`c1541 -attach /tmp/save-test.d64 -list`; extract records and check size,
header and checksum. Also test missing/corrupt-index recovery.

These are manual acceptance steps, not evidence of a run in the current
documentation update. `make test` does not cover disk persistence.
See [SAVE_GAME.md](SAVE_GAME.md) for the file format and workspace contract.
