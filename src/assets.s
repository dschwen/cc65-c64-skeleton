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
; platform room/type tables. Types 36-255 are currently empty in objects.cobj.
.segment "RODATA"
_initial_room_data:
    .incbin "assets/00", 0, 1248
_initial_object_type_data:
    .incbin "assets/objects.cobj", 0, 2304
