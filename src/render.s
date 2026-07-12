.setcpu "6502"

.include "platform.inc"

.export _platform_map_draw_native
.export _platform_object_draw_native
.export _platform_lighting_apply_native
.export _platform_lightning_native
.export _platform_light_source_apply_native
.export _platform_visibility_build_native
.export _platform_color_clear_native

.import _native_object_type
.import _native_object_source
.import _native_object_columns
.import _native_object_rows
.import _native_object_row_skip
.import _native_object_screen_offset
.import _platform_base_colors
.import _platform_brightness
.import _platform_light_colors
.import _platform_light_distance
.import _platform_frame_counter
.import _native_light_source_x
.import _native_light_source_y
.import _native_light_radius
.import _native_light_min_x
.import _native_light_min_y
.import _native_light_columns
.import _native_light_rows
.import _native_light_screen_offset
.import _native_light_visibility_offset
.import _platform_light_visibility
.import _platform_view_tiles
.import _native_visibility_room
.import _native_visibility_mask
.import _native_visibility_origin_x
.import _native_visibility_origin_y
.import _native_visibility_origin_offset
.import _native_visibility_max_ring
.import _native_visibility_filter_walls
.import _native_visibility_viewer_x
.import _native_visibility_viewer_y

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
    lda #<_platform_base_colors
    sta @color_top+1
    lda #>_platform_base_colors
    sta @color_top+2
    lda #<(_platform_base_colors+MAP_WIDTH_CHARS)
    sta @color_bottom+1
    lda #>(_platform_base_colors+MAP_WIDTH_CHARS)
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
    sta _platform_base_colors,x

    inx
@color_read_1:
    lda TILE_DATA+3
    and #$0f
@color_top_right:
    sta _platform_base_colors,x
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
    sta _platform_base_colors+MAP_WIDTH_CHARS,x
    inx
@color_read_3:
    lda TILE_DATA+7
    and #$0f
@color_bottom_right:
    sta _platform_base_colors+MAP_WIDTH_CHARS,x

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
    adc #<_platform_base_colors
    sta @color_write+1
    lda _native_object_screen_offset+1
    adc #>_platform_base_colors
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
    sta _platform_base_colors,x
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

; C resolves the banked object type and prepares one clipped source rectangle.
; This loop patches the distance-table and brightness row addresses once per
; scanline, then max-combines the source without C calls or multiplication.
_platform_light_source_apply_native:
    lda _native_light_screen_offset
    clc
    adc #<_platform_brightness
    sta @brightness_read+1
    sta @brightness_write+1
    lda _native_light_screen_offset+1
    adc #>_platform_brightness
    sta @brightness_read+2
    sta @brightness_write+2
    lda _native_light_visibility_offset
    clc
    adc #<_platform_light_visibility
    sta @visibility_read+1
    lda #>_platform_light_visibility
    adc #0
    sta @visibility_read+2
    lda _native_light_min_y
    sta @world_y+1
    lda _native_light_rows
    sta @light_row_count+1

@light_source_row:
    lda _native_light_min_x
    sta @world_x+1
@world_y:
    lda #0
    cmp _native_light_source_y
    bcs @delta_y_positive
    lda _native_light_source_y
    sec
    sbc @world_y+1
    jmp @delta_y_ready
@delta_y_positive:
    sec
    sbc _native_light_source_y
@delta_y_ready:
    sta @delta_y+1
    cmp #16
    beq @distance_row_ready
    asl
    asl
    asl
    asl
    clc
    adc #<_platform_light_distance
    sta @distance_read+1
    lda #>_platform_light_distance
    adc #0
    sta @distance_read+2
@distance_row_ready:
    ldy #0

@light_source_cell:
@world_x:
    lda #0
    lsr
    tax
@visibility_read:
    lda _platform_light_visibility,x
    beq @light_source_next
    lda @world_x+1
    cmp _native_light_source_x
    bcs @delta_x_positive
    lda _native_light_source_x
    sec
    sbc @world_x+1
    jmp @delta_x_ready
@delta_x_positive:
    sec
    sbc _native_light_source_x
@delta_x_ready:
    tax
@delta_y:
    lda #0
    cmp #16
    beq @delta_y_sixteen
    cpx #16
    beq @delta_x_sixteen
@distance_read:
    lda _platform_light_distance,x
    jmp @distance_ready
@delta_y_sixteen:
    cpx #0
    bne @light_source_next
    lda #16
    jmp @distance_ready
@delta_x_sixteen:
    cmp #0
    bne @light_source_next
    lda #16
@distance_ready:
    cmp _native_light_radius
    bcc @inside_light
    bne @light_source_next
@inside_light:
    sta @distance+1
    lda _native_light_radius
    sec
@distance:
    sbc #0
    cmp #4
    bcc @not_full_light
    lda #3
    bne @max_light
@not_full_light:
    cmp #2
    bcc @dim_light
    lda #2
    bne @max_light
@dim_light:
    lda #1
@max_light:
@brightness_read:
    cmp _platform_brightness,y
    bcc @light_source_next
    beq @light_source_next
@brightness_write:
    sta _platform_brightness,y

@light_source_next:
    inc @world_x+1
    iny
    cpy _native_light_columns
    bne @light_source_cell

    clc
    lda @brightness_read+1
    adc #MAP_WIDTH_CHARS
    sta @brightness_read+1
    sta @brightness_write+1
    lda @brightness_read+2
    adc #0
    sta @brightness_read+2
    sta @brightness_write+2
    inc @world_y+1
    lda @world_y+1
    and #1
    bne @visibility_row_ready
    clc
    lda @visibility_read+1
    adc #MAP_WIDTH_TILES
    sta @visibility_read+1
    lda @visibility_read+2
    adc #0
    sta @visibility_read+2
@visibility_row_ready:
@light_row_count:
    lda #1
    sec
    sbc #1
    sta @light_row_count+1
    beq @light_source_done
    jmp @light_source_row
@light_source_done:
    rts

; Translate base colors and brightness into final, visibility-masked Color RAM
; in one tile-oriented pass. No fully-lit intermediate Color RAM state exists.
_platform_lighting_apply_native:
    lda #<_platform_base_colors
    sta @base_top_read+1
    lda #>_platform_base_colors
    sta @base_top_read+2
    lda #<(_platform_base_colors+MAP_WIDTH_CHARS)
    sta @base_bottom_read+1
    lda #>(_platform_base_colors+MAP_WIDTH_CHARS)
    sta @base_bottom_read+2
    lda #<_platform_brightness
    sta @brightness_top_read+1
    lda #>_platform_brightness
    sta @brightness_top_read+2
    lda #<(_platform_brightness+MAP_WIDTH_CHARS)
    sta @brightness_bottom_read+1
    lda #>(_platform_brightness+MAP_WIDTH_CHARS)
    sta @brightness_bottom_read+2
    lda #<COLOR_RAM
    sta @color_top_write+1
    lda #>COLOR_RAM
    sta @color_top_write+2
    lda #<(COLOR_RAM+MAP_WIDTH_CHARS)
    sta @color_bottom_write+1
    lda #>(COLOR_RAM+MAP_WIDTH_CHARS)
    sta @color_bottom_write+2
    lda #<_platform_view_tiles
    sta @view_read+1
    lda #>_platform_view_tiles
    sta @view_read+2
    lda #MAP_HEIGHT_TILES
    sta @lighting_rows

@lighting_row:
    ldy #0
    ldx #0
@lighting_tile:
@view_read:
    lda _platform_view_tiles,x
    beq @write_hidden_tile
    txa
    pha
    jsr @write_lit_top
    iny
    jsr @write_lit_top
    jsr @write_lit_bottom
    dey
    jsr @write_lit_bottom
    iny
    iny
    pla
    tax
    jmp @lighting_tile_done
@write_hidden_tile:
    lda #0
    jsr @store_top
    jsr @store_bottom
    iny
    jsr @store_top
    jsr @store_bottom
    iny
@lighting_tile_done:
    inx
    cpx #MAP_WIDTH_TILES
    bne @lighting_tile

    clc
    lda @view_read+1
    adc #MAP_WIDTH_TILES
    sta @view_read+1
    lda @view_read+2
    adc #0
    sta @view_read+2

    ; Advance all character-row operands by two rows (80 bytes).
    clc
    lda @base_top_read+1
    adc #80
    sta @base_top_read+1
    lda @base_top_read+2
    adc #0
    sta @base_top_read+2
    clc
    lda @base_bottom_read+1
    adc #80
    sta @base_bottom_read+1
    lda @base_bottom_read+2
    adc #0
    sta @base_bottom_read+2
    clc
    lda @brightness_top_read+1
    adc #80
    sta @brightness_top_read+1
    lda @brightness_top_read+2
    adc #0
    sta @brightness_top_read+2
    clc
    lda @brightness_bottom_read+1
    adc #80
    sta @brightness_bottom_read+1
    lda @brightness_bottom_read+2
    adc #0
    sta @brightness_bottom_read+2
    clc
    lda @color_top_write+1
    adc #80
    sta @color_top_write+1
    lda @color_top_write+2
    adc #0
    sta @color_top_write+2
    clc
    lda @color_bottom_write+1
    adc #80
    sta @color_bottom_write+1
    lda @color_bottom_write+2
    adc #0
    sta @color_bottom_write+2
    dec @lighting_rows
    beq @lighting_done
    jmp @lighting_row

@write_lit_top:
@base_top_read:
    lda _platform_base_colors,y
    sta @top_index+1
@brightness_top_read:
    lda _platform_brightness,y
    asl
    asl
    asl
    asl
@top_index:
    ora #0
    tax
    lda _platform_light_colors,x
@store_top:
@color_top_write:
    sta COLOR_RAM,y
    rts
@write_lit_bottom:
@base_bottom_read:
    lda _platform_base_colors+MAP_WIDTH_CHARS,y
    sta @bottom_index+1
@brightness_bottom_read:
    lda _platform_brightness+MAP_WIDTH_CHARS,y
    asl
    asl
    asl
    asl
@bottom_index:
    ora #0
    tax
    lda _platform_light_colors,x
@store_bottom:
@color_bottom_write:
    sta COLOR_RAM+MAP_WIDTH_CHARS,y
    rts
@lighting_done:
    rts
@lighting_rows:
    .byte 0

; Clear exactly the 40x22 map Color RAM region, preserving both status rows.
_platform_color_clear_native:
    lda #0
    ldy #0
@color_clear_pages:
    sta COLOR_RAM,y
    sta COLOR_RAM+$100,y
    sta COLOR_RAM+$200,y
    iny
    bne @color_clear_pages
    ldy #0
@color_clear_tail:
    sta COLOR_RAM+$300,y
    iny
    cpy #112
    bne @color_clear_tail
    rts

; Produce a short VIC flash without modifying screen RAM or either offscreen
; lighting buffer. The bottom two text rows are outside the affected range.
_platform_lightning_native:
    lda #1
    sta $d020
    sta $d021
    jsr _platform_color_clear_native
    ldx #2
    lda _platform_frame_counter
@lightning_wait:
    cmp _platform_frame_counter
    beq @lightning_wait
    lda _platform_frame_counter
    dex
    bne @lightning_wait
    lda #0
    sta $d020
    sta $d021
    jmp _platform_lighting_apply_native

.segment "HIGHCODE"

; Build visibility as a one-parent ring propagation. States are 0 unprocessed,
; 1 visible, and 2 occluded. Each ring partitions every cell in the next ring;
; there is deliberately no merging or conflict resolution.
_platform_visibility_build_native:
    lda _native_visibility_room
    clc
    adc #ROOM_TILES
    sta @room_tile_read+1
    sta @room_tile_read_process+1
    lda _native_visibility_room+1
    adc #0
    sta @room_tile_read+2
    sta @room_tile_read_process+2
    lda _native_visibility_mask
    sta @mask_clear+1
    sta @mask_read+1
    sta @mask_read_process+1
    sta @mask_write+1
    sta @mask_write_child+1
    sta @mask_write_process+1
    sta @mask_write_normalize+1
    lda _native_visibility_mask+1
    sta @mask_clear+2
    sta @mask_read+2
    sta @mask_read_process+2
    sta @mask_write+2
    sta @mask_write_child+2
    sta @mask_write_process+2
    sta @mask_write_normalize+2

    lda #0
    ldx #0
@clear_mask:
@mask_clear:
    sta $ffff,x
    inx
    cpx #MAP_WIDTH_TILES*MAP_HEIGHT_TILES
    bne @clear_mask

    ; The viewer is always visible.
    ldx _native_visibility_origin_offset
    lda #1
@mask_write:
    sta $ffff,x

    ; An opaque viewer does not initialize ring 1.
@room_tile_read:
    lda $ffff,x
    tax
    lda TILE_PROPERTIES,x
    and #$02
    beq @viewer_open
    jmp @normalize_mask
@viewer_open:
    lda _native_visibility_max_ring
    bne @initialize_ring_one
    jmp @normalize_mask

    ; Initialize all eight in-bounds neighbors as visible.
@initialize_ring_one:
    lda #1
    sta @outgoing_state+1
    lda #$ff
    sta @init_dy+1
@init_row:
    lda #$ff
    sta @init_dx+1
@init_cell:
@init_dx:
    lda #0
    bne @init_write
@init_dy:
    lda #0
    beq @init_next
@init_write:
    lda @init_dx+1
    sta @child_x+1
    lda @init_dy+1
    sta @child_y+1
    jsr @write_child
@init_next:
    inc @init_dx+1
    lda @init_dx+1
    cmp #2
    bne @init_cell
    inc @init_dy+1
    lda @init_dy+1
    cmp #2
    bne @init_row

    lda #1
    sta @ring+1
@ring_loop:
    ; Top edge, including both corners.
    lda #0
    sec
@ring:
    sbc #1
    sta @relative_x+1
    sta @relative_y+1
@top_loop:
    jsr @process_tile
    lda @relative_x+1
    cmp @ring+1
    beq @bottom_start
    inc @relative_x+1
    jmp @top_loop

@bottom_start:
    lda #0
    sec
    sbc @ring+1
    sta @relative_x+1
    lda @ring+1
    sta @relative_y+1
@bottom_loop:
    jsr @process_tile
    lda @relative_x+1
    cmp @ring+1
    beq @sides_start
    inc @relative_x+1
    jmp @bottom_loop

@sides_start:
    lda #0
    sec
    sbc @ring+1
    clc
    adc #1
    sta @relative_y+1
@side_loop:
    lda #0
    sec
    sbc @ring+1
    sta @relative_x+1
    jsr @process_tile
    lda @ring+1
    sta @relative_x+1
    jsr @process_tile
    lda @relative_y+1
    clc
    adc #1
    sta @relative_y+1
    cmp @ring+1
    bne @side_loop

    inc @ring+1
    lda @ring+1
    cmp _native_visibility_max_ring
    bcc @ring_loop
    beq @ring_loop

@normalize_mask:
    ldx #0
@normalize_loop:
@mask_read:
    lda $ffff,x
    cmp #2
    bne @normalize_next
    lda #0
@mask_write_normalize:
    sta $ffff,x
@normalize_next:
    inx
    cpx #MAP_WIDTH_TILES*MAP_HEIGHT_TILES
    bne @normalize_loop
    rts

@process_tile:
    jsr @relative_offset
    bcc @process_done
    stx @process_offset+1
@mask_read_process:
    lda $ffff,x
    beq @process_done
    cmp #2
    beq @process_occluded

    ; Visible tiles remain in the display mask. Opaque ones propagate state 2.
@room_tile_read_process:
    lda $ffff,x
    tax
    lda TILE_PROPERTIES,x
    and #$02
    beq @process_visible
    lda _native_visibility_filter_walls
    beq @process_occluded
    jsr @wall_between_viewer_and_light
    bcc @process_occluded
@process_offset:
    ldx #0
    lda #2
@mask_write_process:
    sta $ffff,x
@process_occluded:
    lda #2
    bne @set_outgoing
@process_visible:
    lda #1
@set_outgoing:
    sta @outgoing_state+1
    jsr @write_children
@process_done:
    rts

; Carry set when either wall coordinate lies strictly between the corresponding
; viewer and light-source coordinates.
@wall_between_viewer_and_light:
    lda _native_visibility_origin_x
    cmp _native_visibility_viewer_x
    bcc @x_origin_first
    beq @check_between_y
    lda @world_x+1
    cmp _native_visibility_viewer_x
    bcc @check_between_y
    beq @check_between_y
    cmp _native_visibility_origin_x
    bcc @wall_is_between
    jmp @check_between_y
@x_origin_first:
    lda @world_x+1
    cmp _native_visibility_origin_x
    bcc @check_between_y
    beq @check_between_y
    cmp _native_visibility_viewer_x
    bcc @wall_is_between

@check_between_y:
    lda _native_visibility_origin_y
    cmp _native_visibility_viewer_y
    bcc @y_origin_first
    beq @wall_not_between
@world_y:
    lda #0
    cmp _native_visibility_viewer_y
    bcc @wall_not_between
    beq @wall_not_between
    cmp _native_visibility_origin_y
    bcc @wall_is_between
    bcs @wall_not_between
@y_origin_first:
    lda @world_y+1
    cmp _native_visibility_origin_y
    bcc @wall_not_between
    beq @wall_not_between
    cmp _native_visibility_viewer_y
    bcc @wall_is_between
@wall_not_between:
    clc
    rts
@wall_is_between:
    sec
    rts

; Convert the current relative coordinate to a tile offset. Carry is clear for
; coordinates outside the room; X is the 0..219 offset when carry is set.
@relative_offset:
    clc
    lda _native_visibility_origin_x
@relative_x:
    adc #0
    cmp #MAP_WIDTH_TILES
    bcs @relative_outside
    sta @world_x+1
    clc
    lda _native_visibility_origin_y
@relative_y:
    adc #0
    cmp #MAP_HEIGHT_TILES
    bcs @relative_outside
    sta @world_y+1
    tay
    lda @tile_row_offsets,y
    clc
@world_x:
    adc #0
    tax
    sec
    rts
@relative_outside:
    clc
    rts

@write_children:
    ; sx and abs(dx)
    lda @relative_x+1
    beq @sign_x_zero
    bmi @sign_x_negative
    lda #1
    bne @sign_x_ready
@sign_x_negative:
    lda #$ff
    bne @sign_x_ready
@sign_x_zero:
    lda #0
@sign_x_ready:
    sta @sign_x+1
    lda @relative_x+1
    bpl @abs_x_ready
    eor #$ff
    clc
    adc #1
@abs_x_ready:
    sta @abs_x+1

    ; sy and abs(dy)
    lda @relative_y+1
    beq @sign_y_zero
    bmi @sign_y_negative
    lda #1
    bne @sign_y_ready
@sign_y_negative:
    lda #$ff
    bne @sign_y_ready
@sign_y_zero:
    lda #0
@sign_y_ready:
    sta @sign_y+1
    lda @relative_y+1
    bpl @abs_y_ready
    eor #$ff
    clc
    adc #1
@abs_y_ready:
    sta @abs_y+1

    ; Corners have one diagonal child.
@abs_x:
    lda #0
    cmp @ring+1
    bne @check_horizontal_edge
@abs_y:
    lda #0
    cmp @ring+1
    bne @vertical_edge
    clc
    lda @relative_x+1
@sign_x:
    adc #0
    sta @child_x+1
    clc
    lda @relative_y+1
@sign_y:
    adc #0
    sta @child_y+1
    jmp @write_child

@check_horizontal_edge:
    lda @abs_y+1
    cmp @ring+1
    bne @vertical_edge
    ; Near the left corner writes the outward-left gap.
    lda #0
    sec
    sbc @ring+1
    clc
    adc #1
    cmp @relative_x+1
    bne @horizontal_axial
    lda @relative_x+1
    sec
    sbc #1
    sta @child_x+1
    clc
    lda @relative_y+1
    adc @sign_y+1
    sta @child_y+1
    jsr @write_child
@horizontal_axial:
    lda @relative_x+1
    sta @child_x+1
    clc
    lda @relative_y+1
    adc @sign_y+1
    sta @child_y+1
    jsr @write_child
    ; Near the right corner writes the outward-right gap.
    lda @ring+1
    sec
    sbc #1
    cmp @relative_x+1
    bne @children_done
    lda @relative_x+1
    clc
    adc #1
    sta @child_x+1
    lda @relative_y+1
    clc
    adc @sign_y+1
    sta @child_y+1
    jmp @write_child

@vertical_edge:
    ; Near the upper corner writes the outward-up gap.
    lda #0
    sec
    sbc @ring+1
    clc
    adc #1
    cmp @relative_y+1
    bne @vertical_axial
    clc
    lda @relative_x+1
    adc @sign_x+1
    sta @child_x+1
    lda @relative_y+1
    sec
    sbc #1
    sta @child_y+1
    jsr @write_child
@vertical_axial:
    clc
    lda @relative_x+1
    adc @sign_x+1
    sta @child_x+1
    lda @relative_y+1
    sta @child_y+1
    jsr @write_child
    ; Near the lower corner writes the outward-down gap.
    lda @ring+1
    sec
    sbc #1
    cmp @relative_y+1
    bne @children_done
    clc
    lda @relative_x+1
    adc @sign_x+1
    sta @child_x+1
    lda @relative_y+1
    clc
    adc #1
    sta @child_y+1
    jmp @write_child
@children_done:
    rts

@write_child:
    ; Reuse the coordinate helper by temporarily selecting the child operands.
    lda @relative_x+1
    pha
    lda @relative_y+1
    pha
@child_x:
    lda #0
    sta @relative_x+1
@child_y:
    lda #0
    sta @relative_y+1
    jsr @relative_offset
    bcc @write_restore
@outgoing_state:
    lda #1
    ; Plain assignment: every destination has exactly one parent.
@mask_write_child:
    sta $ffff,x
@write_restore:
    pla
    sta @relative_y+1
    pla
    sta @relative_x+1
    rts

@tile_row_offsets:
    .byte 0, 20, 40, 60, 80, 100, 120, 140, 160, 180, 200
