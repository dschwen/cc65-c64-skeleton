; Embed only the payloads. The editor's 8-byte file headers are not C64 data.

.export _tile_data
.export _tile_properties

.segment "CHARSETS"
    .incbin "assets/charset.cchr", 8, 4096

.segment "TILESET"
_tile_data:
    .incbin "assets/tiles.ctil", 8, 2048
_tile_properties:
    .incbin "assets/tiles.ctil", 2056, 256
