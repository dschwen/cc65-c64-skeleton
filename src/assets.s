; Embed only the payloads. The editor's 8-byte file headers are not C64 data.

.export _tile_data
.export _tile_properties
.export _initial_room_data
.export _initial_object_type_data

.segment "CHARSETS"
    .incbin "assets/charset.cchr", 8, 4096

.segment "TILESET"
_tile_data:
    .incbin "assets/tiles.ctil", 8, 2048
_tile_properties:
    .incbin "assets/tiles.ctil", 2056, 256

; Startup data is kept in low read-only memory, then copied into the mutable
; platform room and the empty/player type records needed by a standalone PRG
; (no EasyFlash cartridge detected). A real EasyFlash boot loads the complete
; room and object-type data at runtime instead; see platform_init(). The
; initial object-type data is pre-split to the resident hot record layout by
; tools/extract_initial_object_types.py, matching tools/pack_easyflash.py.
.segment "RODATA"
_initial_room_data:
    .incbin "build/assets/00", 0, 1257
_initial_object_type_data:
    .incbin "build/assets/objects-initial.hot", 0, 70
