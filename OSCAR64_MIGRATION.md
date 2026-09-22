# Oscar64 implementation status

The active build is Oscar64 native C targeting EasyFlash. Use
[GUIDE_OSCAR64_C64.md](GUIDE_OSCAR64_C64.md) for compiler guidance,
[MEMORY_MAP.md](MEMORY_MAP.md) for allocation, and
[EASYFLASH_CARTRIDGE.md](EASYFLASH_CARTRIDGE.md) for banking contracts.

## Implemented

- Whole-program compilation and direct CRT output.
- Native C renderer, platform services, room DSL output and hardware wrappers.
- Banked room/services/script code with resident state and nested bank restore.
- High-RAM charsets, screen and sprites; explicit cold-boot asset copies.
- Foreground KERNAL keyboard scanning and raster-safe input polling.
- Relocated environment modules with an entry-first native pointer table.
- Disk save/load implementation using banked KERNAL wrappers.

## Regression scope and remaining work

`make test` cold-boots, compares high-RAM assets, checks KERNAL/CIA state,
sends real Xvfb key events, exercises Look/inventory/movement, verifies the
visible raster split, and enters rooms 02/03/04/05 before returning to rain.
It checks environment pointers and SID state advancement, not audible output.

It also runs an F1 save / F3 load round trip on a disposable D64, a typed-
keyword conversation, and checks Sir Caldus's portrait byte-for-byte
against its source asset. Disk index recovery, most story interactions and
NTSC timing are not covered by that regression. The generic resource
loader supports room scripts, conversations and portraits (`PLATFORM_
RESOURCE_KIND_ROOM/_CONVERSATION/_PORTRAIT`, src/resources.c); the
remaining kind constants are still reserved, unimplemented.
Do not equate successful compilation with complete feature parity.
