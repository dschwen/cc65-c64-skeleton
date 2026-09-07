; Embed only the payloads. The editor's 8-byte file headers are not C64 data.

.include "platform.inc"

.export _charset_tile
.export _charset_text
.export _tile_data
.export _tile_properties
.export _initial_room_data
.export _initial_object_type_data

; Charsets, tile bitmaps and tile properties are no longer linked into the
; program image - they are PLATFORM_RESOURCE_KIND_ASSET resources, fetched
; straight to these reserved destinations by platform_init(). That keeps 6,400
; bytes out of the contiguous blob cart/ef_boot.s copies into RAM at boot, and
; makes them swappable at runtime. See src/platform.h's asset IDs and the
; Makefile's RA%% rules, which slice them out of the same editor files these
; used to .incbin from.
.segment "CHARSETS"
_charset_tile:
    .res 2048
_charset_text:
    .res 2048

; _tile_data and _tile_properties are deliberately adjacent and fetched as one
; blob: tile bitmaps and their property table must never drift out of step, or
; collision silently disagrees with what is drawn.
.segment "TILESET"
_tile_data:
    .res 2048
_tile_properties:
    .res 256

; Startup data is kept in low read-only memory, then copied into the mutable
; platform room and the empty/player type records needed by a standalone PRG
; (no EasyFlash cartridge detected). A real EasyFlash boot loads the complete
; room and object-type data at runtime instead; see platform_init(). The
; initial object-type data is pre-split to the resident hot record layout by
; tools/extract_initial_object_types.py, matching tools/pack_easyflash.py.
.segment "RODATA"
_initial_room_data:
    .incbin "build/assets/00", 0, ROOM_FILE_BYTES
_initial_object_type_data:
    .incbin "build/assets/objects-initial.hot", 0, 70

; A single 45-degree hires diagonal streak (top-left to bottom-right). This
; bitmap is static; hardware sprites 6 and 7 share it and the raster IRQ only
; ever repositions the sprites, never redraws the pattern.
.segment "RAINSPRITE"
    .byte $80,$00,$00, $40,$00,$00, $20,$00,$00
    .byte $10,$00,$00, $08,$00,$00, $04,$00,$00
    .byte $02,$00,$00, $01,$00,$00, $00,$80,$00
    .byte $00,$40,$00, $00,$20,$00, $00,$10,$00
    .byte $00,$08,$00, $00,$04,$00, $00,$02,$00
    .byte $00,$01,$00, $00,$00,$80, $00,$00,$40
    .byte $00,$00,$20, $00,$00,$10, $00,$00,$08
    .byte $00
