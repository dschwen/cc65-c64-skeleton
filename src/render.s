.setcpu "6502"

.include "platform.inc"

.export _platform_map_draw_native
.export _platform_object_draw_native

.import _native_object_type
.import _native_object_source
.import _native_object_columns
.import _native_object_rows
.import _native_object_row_skip
.import _native_object_screen_offset

.segment "LOWCODE"

; fastcall: A/X = PlatformRoom*. The tile array fits in one 256-byte indexed
; window (offsets 4-223), so Y can stream the complete map without pointer math.
_platform_map_draw_native:
    sta @room_read+1
    stx @room_read+2

    lda #<SCREEN_RAM
    sta @screen_top+1
    lda #>SCREEN_RAM
    sta @screen_top+2
    lda #<(SCREEN_RAM+MAP_WIDTH_CHARS)
    sta @screen_bottom+1
    lda #>(SCREEN_RAM+MAP_WIDTH_CHARS)
    sta @screen_bottom+2
    lda #<COLOR_RAM
    sta @color_top+1
    lda #>COLOR_RAM
    sta @color_top+2
    lda #<(COLOR_RAM+MAP_WIDTH_CHARS)
    sta @color_bottom+1
    lda #>(COLOR_RAM+MAP_WIDTH_CHARS)
    sta @color_bottom+2

    lda #MAP_HEIGHT_TILES
    sta @row_count+1
    ldy #ROOM_TILES

@tile_row:
    ldx #0
@tile:
@room_read:
    lda $ffff,y
    sta @tile_id+1
    asl
    asl
    asl
    sta @char_0+1
    clc
    adc #2
    sta @char_1+1
    clc
    adc #2
    sta @char_2+1
    clc
    adc #2
    sta @char_3+1
@tile_id:
    lda #0
    lsr
    lsr
    lsr
    lsr
    lsr
    clc
    adc #>TILE_DATA
    sta @char_0+2
    sta @char_1+2
    sta @char_2+2
    sta @char_3+2

@char_0:
    lda TILE_DATA
@screen_top:
    sta SCREEN_RAM,x
@char_1:
    lda TILE_DATA+2
    inx
@screen_top_right:
    sta SCREEN_RAM,x
    dex
    ; Color bytes immediately follow their corresponding character bytes.
    inc @char_0+1
    bne :+
    inc @char_0+2
:
@color_0:
    lda TILE_DATA+1
    ; Patch color reads from the already calculated character addresses.
    lda @char_0+1
    sta @color_read_0+1
    lda @char_0+2
    sta @color_read_0+2
    lda @char_1+1
    clc
    adc #1
    sta @color_read_1+1
    lda @char_1+2
    adc #0
    sta @color_read_1+2
    lda @char_2+1
    clc
    adc #1
    sta @color_read_2+1
    lda @char_2+2
    adc #0
    sta @color_read_2+2
    lda @char_3+1
    clc
    adc #1
    sta @color_read_3+1
    lda @char_3+2
    adc #0
    sta @color_read_3+2
@color_read_0:
    lda TILE_DATA+1
    and #$0f
@color_top:
    sta COLOR_RAM,x

    inx
@color_read_1:
    lda TILE_DATA+3
    and #$0f
@color_top_right:
    sta COLOR_RAM,x
    dex

@char_2:
    lda TILE_DATA+4
@screen_bottom:
    sta SCREEN_RAM+MAP_WIDTH_CHARS,x
    inx
@char_3:
    lda TILE_DATA+6
@screen_bottom_right:
    sta SCREEN_RAM+MAP_WIDTH_CHARS,x
    dex
@color_read_2:
    lda TILE_DATA+5
    and #$0f
@color_bottom:
    sta COLOR_RAM+MAP_WIDTH_CHARS,x
    inx
@color_read_3:
    lda TILE_DATA+7
    and #$0f
@color_bottom_right:
    sta COLOR_RAM+MAP_WIDTH_CHARS,x

    inx
    iny
    cpx #MAP_WIDTH_CHARS
    beq @advance_rows
    jmp @tile

    ; Advance each top/bottom pair by two character rows (80 bytes).
@advance_rows:
    clc
    lda @screen_top+1
    adc #80
    sta @screen_top+1
    sta @screen_top_right+1
    lda @screen_top+2
    adc #0
    sta @screen_top+2
    sta @screen_top_right+2
    clc
    lda @screen_bottom+1
    adc #80
    sta @screen_bottom+1
    sta @screen_bottom_right+1
    lda @screen_bottom+2
    adc #0
    sta @screen_bottom+2
    sta @screen_bottom_right+2
    clc
    lda @color_top+1
    adc #80
    sta @color_top+1
    sta @color_top_right+1
    lda @color_top+2
    adc #0
    sta @color_top+2
    sta @color_top_right+2
    clc
    lda @color_bottom+1
    adc #80
    sta @color_bottom+1
    sta @color_bottom_right+1
    lda @color_bottom+2
    adc #0
    sta @color_bottom+2
    sta @color_bottom_right+2

@row_count:
    lda #MAP_HEIGHT_TILES
    sec
    sbc #1
    sta @row_count+1
    beq @map_done
    jmp @tile_row
@map_done:
    rts

; C prepares a clipped rectangle and source index. This routine performs the
; transparent screen/color transfer without general multiplication or calls.
_platform_object_draw_native:
    lda _native_object_type
    clc
    adc #OBJECT_TYPE_CHARS
    sta @char_read+1
    lda _native_object_type+1
    adc #0
    sta @char_read+2
    lda _native_object_type
    clc
    adc #OBJECT_TYPE_COLORS
    sta @color_read+1
    lda _native_object_type+1
    adc #0
    sta @color_read+2

    lda _native_object_screen_offset
    clc
    adc #<SCREEN_RAM
    sta @screen_write+1
    lda _native_object_screen_offset+1
    adc #>SCREEN_RAM
    sta @screen_write+2
    lda _native_object_screen_offset
    clc
    adc #<COLOR_RAM
    sta @color_write+1
    lda _native_object_screen_offset+1
    adc #>COLOR_RAM
    sta @color_write+2

    ldy _native_object_source
    lda _native_object_rows
    sta @object_rows+1
@object_row:
    ldx #0
@object_cell:
@char_read:
    lda $ffff,y
    beq @transparent
@screen_write:
    sta SCREEN_RAM,x
@color_read:
    lda $ffff,y
    and #$0f
@color_write:
    sta COLOR_RAM,x
@transparent:
    iny
    inx
    cpx _native_object_columns
    bne @object_cell
    tya
    clc
    adc _native_object_row_skip
    tay

    clc
    lda @screen_write+1
    adc #MAP_WIDTH_CHARS
    sta @screen_write+1
    lda @screen_write+2
    adc #0
    sta @screen_write+2
    clc
    lda @color_write+1
    adc #MAP_WIDTH_CHARS
    sta @color_write+1
    lda @color_write+2
    adc #0
    sta @color_write+2
@object_rows:
    lda #1
    sec
    sbc #1
    sta @object_rows+1
    bne @object_row
    rts
