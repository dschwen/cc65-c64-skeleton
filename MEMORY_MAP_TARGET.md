# Memory-layout maintenance checklist

The target layout is implemented. [MEMORY_MAP.md](MEMORY_MAP.md) is the current
allocation, not this checklist. Compiler guidance is in
[GUIDE_OSCAR64_C64.md](GUIDE_OSCAR64_C64.md) and project banking details in
[EASYFLASH_CARTRIDGE.md](EASYFLASH_CARTRIDGE.md).

## Preserve these invariants

- Main code, mutable data and both compiler stacks end before `$7E00`.
- The environment owns `$7E00-$7FFF`; its entry section is first.
- `$8000-$BFFF` is a selected ROM bank, not always-readable workspace.
- VIC assets remain in bank-3 RAM beneath KERNAL. A successful store does not
  prove that a CPU load with the same map will read the stored bytes.
- Banked operations restore the exact caller bank through the software shadow.
  Never read `$DE00` to discover it.
- Explicitly initialize RAM outside main and restore borrowed tile storage
  after save/load, before any redraw.
- Interrupt code never switches banks or follows a pointer into a selected
  bank. Environment ticks are foreground work in the current engine.
- Every transition error path must resume raster timing. Do not call frame
  waits or paged-text input while the raster source is suspended.
- Full-screen menus must retain the text charset on both normal and late IRQ
  paths; input resynchronization must schedule the next event without waiting
  a full frame with interrupts masked.

## Evidence required for future layout changes

Inspect map/listing output and test cold startup, keyboard, map/text scanlines,
nested bank restoration and all affected room transitions. Test save/load if
changing shared workspace; test NTSC separately before claiming it works.
Use explicit monitor memory spaces: CPU view, underlying RAM and I/O can
disagree at the same numeric address.

For sound reports, capture actual WAV audio (`-sounddev wav -soundarg out.wav`
under Xvfb); `-sounddev dump` emits SID register text, not PCM. A correct SID
register trace is insufficient to establish audible output because envelope
and oscillator state matters. Gate timing on room changes deserves an audio
round-trip test, not just register inspection. Do not patch old fixed jump
offsets into the current native-pointer environment table during diagnosis.
