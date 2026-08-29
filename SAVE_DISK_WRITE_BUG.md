# Save-game disk write bug

Status as of 2026-08-27, branch `rpg`: **resolved**.

## Symptom

F1 Save appeared to complete its KERNAL calls, but no save file appeared on
the attached disk. KERNAL `READST` initially looked clean, while the 1541
command channel reported `62, FILE NOT FOUND`. Reads and the same logical
write sequence in a plain ca65 test program worked.

## Root cause

`modules/disk_io.s` was assembled through `cl65 -t c64`. The C64 target
charmap does not encode uppercase source literals as low PETSCII:

| Source token | Emitted byte | Required CBM DOS byte |
|---|---:|---:|
| `'S'` | `$D3` | `$53` |
| `'W'` | `$D7` | `$57` |

The write filename therefore ended in comma plus `$D7`, not the CBM DOS
`,W` mode suffix. DOS treated the request as an attempt to open an existing
file and returned error 62. The standalone control program did not use the
same C64 target charmap, which is why apparently identical source behaved
differently.

The decisive diagnostic was to dump the actual filename bytes after
`SETNAM`; they were `$D3 $30 $C1 $2C $D3` in the explicit-type experiment.
The KERNAL logical-file tables and CIA2 state were otherwise correct:
logical file 2, device 8, secondary address `$62`, `$DD02=$3F`.
True-drive emulation reproduced the failure, ruling out VICE fast-drive
traps.

## Fix

`modules/disk_io.s` now uses explicit low-PETSCII bytes for CBM DOS protocol
letters:

```asm
lda #$53  ; DOS S command/name byte
lda #$57  ; DOS W mode
```

The current compact `Sn,W` and `S0:Sn` workspace layout uses one file per
slot. The earlier A/B generation design was removed after the write bug was
fixed because it doubled file count and directory reads without being needed
for the current platform.

During diagnostics, the pointers for those two views of the shared workspace
were temporarily swapped. That made readback request `S0:` and scratch
request `SnX,W...`, which looked like a post-close race until the final diff
review caught it. Production uses `filename` for the six-byte scratch command
and `filename+3` for the three-byte read name.

After `CLOSE`, the save path reads the complete record back and validates its
declared length and checksum. This catches failures that `READST` cannot
report. The code uses drive status rather than an arbitrary timed delay; one
apparent readback failure during the initial investigation was also caused by
the swapped read/scratch filename pointer described above.

A later end-to-end test exposed a second completion issue: KERNAL `CLOSE`
returns after sending the close request, while the 1541 can still be writing
the final block and directory entry. Quitting VICE at the initial success
screen left `SINDEX` as a zero-block splat file. `modules/disk_io.s` now reads
DOS status channel 15 after every write close and treats non-success status as
a failed write. The UI does not report `Saved.` until the drive has finished.

## Verification

The full cartridge save path was exercised headlessly with:

```sh
c1541 -format "TEST,00" d64 /tmp/save-test.d64
xvfb-run -a x64sc -console -default -warp -sounddev dummy \
  -cartcrt build/game.crt -8 /tmp/save-test.d64 \
  -limitcycles 50000000 -exitscreenshot /tmp/save-test.png
c1541 -attach /tmp/save-test.d64 -list
```

The normal UI flow encoded a 146-byte empty-journal record, wrote `S0`, read
it back successfully, updated the one-block `SINDEX` cache, and displayed
`Saved.`. `c1541` extraction confirmed both files' declared lengths and
checksums. Missing-index recovery and the full Load return to gameplay were
also exercised. All temporary bypass and diagnostic code was removed.

## General rule

Do not use target-charmapped character literals for external binary
protocol tokens unless the receiver expects that target encoding. For CBM
DOS commands and mode/type suffixes, spell the required byte values
explicitly and document why.
