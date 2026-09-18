; Hand-written replacement for six mutually-calling static C functions in
; src/platform.c (dirty_clear, dirty_set, mark_object_cells, base_cell,
; object_cell, compose_cell, redraw_dirty - one extra, object_cell, folded
; into a shared internal routine below). Measured 43-50% pure calling-
; convention overhead in this cluster: every one of those functions passed
; PlatformRoom*/PlatformObjectType*/PlatformObject* pointers through cc65's
; software-stack parameter convention, and cc65 reloads a stack-resident
; pointer parameter from (sp),y on every single field access rather than
; caching it - so a chain of small functions calling each other with
; pointer arguments pays that reload cost at every hop. Merged into one
; module sharing state through fixed BSS locations, the inter-function
; calls (and their reloads) disappear entirely; only calls to genuinely
; external routines (platform_object_type_get, for the bank-switched
; object-type lookup) remain.
;
; Public entry points (same names/signatures as the C functions they
; replace, so the three call sites in platform.c - platform_object_move,
; platform_player_set_type, platform_room_object_remove - need no changes,
; just an extern declaration instead of a static prototype):
;   void dirty_clear(void)
;   void mark_object_cells(const PlatformObject* object)
;   void redraw_dirty(const PlatformRoom* room, const PlatformObject* player)
;
; ptr1 (a shared cc65 runtime zeropage scratch pointer, already used this
; way by src/brazier.s and others) is reloaded from a BSS "logical
; pointer" variable every time this code switches which pointer it's
; dereferencing, or after any call to platform_object_type_get (which
; uses ptr1 itself for the bank-switched Zone B/C copy path) - never
; assumed to survive a call. Object records are always Zone A/B/C via
; platform_object_type_get(), so no zone-specific handling is needed here.
.setcpu "6502"

.include "platform.inc"

.import _platform_object_type_get
.import _platform_room_object_limit
.import _rendered_room
.import _rendered_object_limit
.import _platform_base_colors
.import _platform_brightness
.import _platform_light_colors
.import _platform_view_tiles
.importzp ptr1
.importzp sp
.import incsp2

.export _dirty_clear
.export _mark_object_cells
.export _redraw_dirty

DIRTY_BYTES       = 110

.segment "BSS"

dirty_cells:  .res DIRTY_BYTES

; shared object-type fetch+validate scratch (fetch_type_and_validate),
; used by both mark_object_cells and object_cell_try - never live across
; both at once, so sharing is safe (see this file's header comment).
tv_typeptr:   .res 2
tv_dims:      .res 1
tv_hotspot:   .res 1
tv_width:     .res 1
tv_height:    .res 1
tv_hotx:      .res 1
tv_hoty:      .res 1
tv_ok:        .res 1

; redraw_dirty()/compose_cell()/object_cell_try()/base_cell() scratch -
; the largest live set in this module. dirty_set()/mark_object_cells()'s
; own scratch is aliased on top of the first 12 bytes of it below instead
; of getting separate storage: mark_object_cells() always runs to
; completion before redraw_dirty() is ever called (see the three call
; sites in platform.c - dirty_clear/mark_object_cells/[mutate]/
; mark_object_cells/redraw_dirty happen strictly in that order), so the
; two groups are never live at the same time. Likewise base_cell()'s own
; three bytes are aliased onto object_cell_try()'s (rd_oleft/rd_otop/
; rd_localx) - compose_cell() calls base_cell() to completion before it
; ever calls object_cell_try().
rd_room:      .res 2
rd_player:    .res 2
rd_limit:     .res 1
rd_curx:      .res 1
rd_cury:      .res 1
rd_i:         .res 1
rd_ch:        .res 1
rd_color:     .res 1
rd_objptr:    .res 2
rd_objcount:  .res 1
rd_oleft:     .res 1
rd_otop:      .res 1
rd_localx:    .res 1
rd_localy:    .res 1
rd_tileoff:   .res 1
rd_offlo:     .res 1
rd_offhi:     .res 1
rd_vcindex:   .res 1
rd_vcolor:    .res 1
; redraw_dirty()'s own bitmap-scan cursor (see that function) - only ever
; live in its outermost scope, so aliased alongside the rest of this pool.
rd_byteidx:   .res 1
rd_y:         .res 1
rd_q:         .res 1
rd_bits:      .res 1
rd_basex:     .res 1
rd_bit:       .res 1

; compose_cell()/object_cell_try()/compose_accept()'s depth-order state -
; see compose_accept()'s own comment. rd_besty tracks the current
; winner's y across a whole compose_cell() call; the cc_cand_* trio is
; object_cell_try()'s per-attempt candidate output.
rd_besty:     .res 1
cc_hit:       .res 1
cc_cand_ch:   .res 1
cc_cand_color: .res 1
cc_cand_y:    .res 1

mo_objptr     = rd_room       ; 2 bytes
mo_left       = rd_player
mo_top        = rd_player+1
mo_index      = rd_limit
mo_x          = rd_curx
mo_y          = rd_cury
ds_x          = rd_i
ds_tmp        = rd_ch
ds_byteidx    = rd_color

bc_tile       = rd_oleft
bc_hi         = rd_otop
bc_tmp1       = rd_localx

.segment "HIGHRODATA"
bit_masks: .byte $01,$02,$04,$08,$10,$20,$40,$80

.segment "HIGHCODE"

; void dirty_clear(void)
_dirty_clear:
    lda #0
    ldx #0
@loop:
    sta dirty_cells,x
    inx
    cpx #DIRTY_BYTES
    bne @loop
    rts

; Internal: X = world_x, Y = world_y (already known in-bounds range for
; the caller's math, but re-checked here exactly like the original
; dirty_set()). Sets bit (world_x,world_y) in the dirty bitmap - unlike
; the original, there's no parallel dirty_x/dirty_y coordinate list to
; also append to (capped at a fixed size): redraw_dirty() below scans the
; bitmap directly instead, which needs no such list and, as a side
; benefit, has no cap to silently drop cells beyond.
; cell = world_y*MAP_WIDTH_CHARS+world_x; MAP_WIDTH_CHARS (40) is a
; multiple of 8, so cell>>3 = world_y*5 + (world_x>>3) and cell&7 =
; world_x&7 - avoids a real multiply for the bitmap index.
dirty_set:
    cpx #MAP_WIDTH_CHARS
    bcs @done
    cpy #MAP_HEIGHT_CHARS
    bcs @done
    stx ds_x
    tya
    sta ds_tmp
    asl a
    asl a
    clc
    adc ds_tmp          ; A = y*5
    sta ds_byteidx
    lda ds_x
    lsr a
    lsr a
    lsr a               ; A = x>>3
    clc
    adc ds_byteidx
    tay                 ; Y = byte index (0..109)
    lda ds_x
    and #7
    tax
    lda bit_masks,x
    ora dirty_cells,y
    sta dirty_cells,y
@done:
    rts

; Input: A = object type id. Fetches the type record via
; platform_object_type_get() and validates it exactly like the original
; object_type_is_valid(): width>0, height>0, width*height<=16 (the fixed
; chars[]/colors[] cell count), hotspot_x<width, hotspot_y<height. Sets
; tv_typeptr/tv_width/tv_height/tv_hotx/tv_hoty and tv_ok (1 valid /
; 0 invalid). Shared by mark_object_cells and object_cell_try so this
; check exists exactly once rather than twice.
fetch_type_and_validate:
    jsr _platform_object_type_get
    sta tv_typeptr
    stx tv_typeptr+1
    sta ptr1
    stx ptr1+1
    ldy #0
    lda (ptr1),y
    sta tv_dims
    and #$0f
    sta tv_height
    lda tv_dims
    lsr a
    lsr a
    lsr a
    lsr a
    sta tv_width
    lda #0
    sta tv_ok
    lda tv_width
    beq @done
    lda tv_height
    beq @done
    lda #0
    ldx tv_height
@wh_loop:
    cpx #0
    beq @wh_done
    clc
    adc tv_width
    dex
    bne @wh_loop
@wh_done:
    cmp #17
    bcs @done               ; width*height > 16 -> invalid
    ldy #1
    lda (ptr1),y
    sta tv_hotspot
    and #$0f
    sta tv_hoty
    lda tv_hotspot
    lsr a
    lsr a
    lsr a
    lsr a
    sta tv_hotx
    lda tv_hotx
    cmp tv_width
    bcs @done
    lda tv_hoty
    cmp tv_height
    bcs @done
    inc tv_ok
@done:
    rts

; void mark_object_cells(const PlatformObject* object) - object arrives in
; A/X (cc65 always passes a single parameter this way, fastcall or not).
; No null-object check: unlike the original, every current caller (all
; three in platform.c) already guarantees a real pointer - see this
; file's header comment.
_mark_object_cells:
    sta mo_objptr
    sta ptr1
    stx mo_objptr+1
    stx ptr1+1
    ldy #0
    lda (ptr1),y            ; object->type
    beq @done
    jsr fetch_type_and_validate
    lda tv_ok
    beq @done
    lda mo_objptr
    sta ptr1
    lda mo_objptr+1
    sta ptr1+1
    ldy #1
    lda (ptr1),y            ; object->x
    sec
    sbc tv_hotx
    sta mo_left
    ldy #2
    lda (ptr1),y            ; object->y
    sec
    sbc tv_hoty
    sta mo_top
    lda tv_typeptr
    sta ptr1
    lda tv_typeptr+1
    sta ptr1+1
    lda #0
    sta mo_index
    sta mo_y
@yloop:
    lda mo_y
    cmp tv_height
    bcs @done
    lda #0
    sta mo_x
@xloop:
    lda mo_x
    cmp tv_width
    bcs @xdone
    ldy mo_index
    iny
    iny                     ; chars[index] is at record offset OBJECT_TYPE_CHARS+index
    lda (ptr1),y
    beq @xnext              ; transparent cell - still advance index/x below
    lda mo_left
    clc
    adc mo_x
    bmi @xnext
    tax                     ; X = world_x
    lda mo_top
    clc
    adc mo_y
    bmi @xnext
    tay                     ; Y = world_y
    jsr dirty_set
@xnext:
    inc mo_index
    inc mo_x
    jmp @xloop
@xdone:
    inc mo_y
    jmp @yloop
@done:
    rts

; Internal: tests the object at rd_objptr against (rd_curx,rd_cury).
; Returns A=1 and cc_cand_ch/cc_cand_color/cc_cand_y set on a hit
; (transparency-checked per cell, not just the bounding box), or A=0 on a
; miss (cc_cand_* left untouched) - compose_cell()/compose_accept() below
; decide whether to actually use the candidate, based on depth order
; rather than unconditionally as the original object_cell() did. No null-
; object check: only ever called here with rd_objptr pointing into a live
; room->objects[] slot or at platform_player.
object_cell_try:
    lda rd_objptr
    sta ptr1
    lda rd_objptr+1
    sta ptr1+1
    ldy #0
    lda (ptr1),y            ; object->type
    bne @has_type
    jmp @miss
@has_type:
    jsr fetch_type_and_validate
    lda tv_ok
    bne @valid
    jmp @miss
@valid:
    lda rd_objptr
    sta ptr1
    lda rd_objptr+1
    sta ptr1+1
    ldy #1
    lda (ptr1),y            ; object->x
    sec
    sbc tv_hotx
    sta rd_oleft
    ldy #2
    lda (ptr1),y            ; object->y - also this object's depth key
    sta cc_cand_y
    sec
    sbc tv_hoty
    sta rd_otop
    lda rd_curx
    sec
    sbc rd_oleft
    bpl @localx_ok
    jmp @miss
@localx_ok:
    cmp tv_width
    bcc @localx_inrange
    jmp @miss
@localx_inrange:
    sta rd_localx
    lda rd_cury
    sec
    sbc rd_otop
    bpl @localy_ok
    jmp @miss
@localy_ok:
    cmp tv_height
    bcc @localy_inrange
    jmp @miss
@localy_inrange:
    sta rd_localy
    ; index = local_y*width + local_x
    lda #0
    ldx rd_localy
    beq @idx_done
@idx_loop:
    clc
    adc tv_width
    dex
    bne @idx_loop
@idx_done:
    clc
    adc rd_localx
    tay
    iny
    iny                     ; Y = index + OBJECT_TYPE_CHARS
    lda tv_typeptr
    sta ptr1
    lda tv_typeptr+1
    sta ptr1+1
    lda (ptr1),y
    beq @miss               ; transparent
    sta cc_cand_ch
    tya
    clc
    adc #(OBJECT_TYPE_COLORS - OBJECT_TYPE_CHARS)
    tay
    lda (ptr1),y
    and #$0f
    sta cc_cand_color
    lda #1
    rts
@miss:
    lda #0
    rts

; Internal: given object_cell_try's just-returned hit flag in A, commits
; its candidate ch/color/y into rd_ch/rd_color/rd_besty if it should win -
; the first hit always does; after that, only a candidate whose y is at
; least the current winner's (>=, so a later-considered object wins a tie)
; does. Matches redraw_map_and_objects()'s ascending-y bucket-sorted full
; redraw (src/platform.c) so a room looks the same whether it was last
; drawn there or repainted here.
compose_accept:
    cmp #0
    beq @reject
    lda cc_hit
    beq @accept
    lda cc_cand_y
    cmp rd_besty
    bcc @reject
@accept:
    lda cc_cand_ch
    sta rd_ch
    lda cc_cand_color
    sta rd_color
    lda cc_cand_y
    sta rd_besty
    lda #1
    sta cc_hit
@reject:
    rts

; Internal: sets rd_ch/rd_color from the room tile under (rd_curx,rd_cury).
base_cell:
    lda rd_cury
    lsr a
    sta bc_tmp1             ; y_tile
    asl a
    asl a
    clc
    adc bc_tmp1             ; *5
    asl a
    asl a                   ; *20 (max 200)
    sta bc_tmp1
    lda rd_curx
    lsr a
    clc
    adc bc_tmp1             ; tile_offset (0..219)
    tay
    lda rd_room
    clc
    adc #<ROOM_TILES
    sta ptr1
    lda rd_room+1
    adc #>ROOM_TILES
    sta ptr1+1
    lda (ptr1),y
    sta bc_tile
    lda #0
    sta bc_hi
    lda bc_tile
    asl a
    rol bc_hi
    asl a
    rol bc_hi
    asl a
    rol bc_hi               ; bc_hi:A = tile*8
    clc
    adc #<TILE_DATA
    sta ptr1
    lda bc_hi
    adc #>TILE_DATA
    sta ptr1+1
    lda rd_cury
    and #1
    asl a
    sta bc_tmp1
    lda rd_curx
    and #1
    ora bc_tmp1
    asl a
    tay                     ; Y = quadrant*2
    lda (ptr1),y
    sta rd_ch
    iny
    lda (ptr1),y
    and #$0f
    sta rd_color
    rts

; Internal: composes rd_ch/rd_color for (rd_curx,rd_cury) - tile, then
; every object covering this cell (room objects in slot order, then the
; player), keeping whichever has the highest y (see compose_accept()
; above for the exact tie rule) rather than always the last one tried.
compose_cell:
    jsr base_cell
    lda #0
    sta cc_hit
    lda rd_room
    clc
    adc #<ROOM_OBJECTS
    sta rd_objptr
    lda rd_room+1
    adc #>ROOM_OBJECTS
    sta rd_objptr+1
    lda #0
    sta rd_objcount
@obj_loop:
    lda rd_objcount
    cmp rd_limit
    bcs @obj_loop_done
    lda rd_objptr
    cmp rd_player
    bne @obj_try
    lda rd_objptr+1
    cmp rd_player+1
    beq @obj_next
@obj_try:
    jsr object_cell_try
    jsr compose_accept
@obj_next:
    lda rd_objptr
    clc
    adc #3
    sta rd_objptr
    bcc @obj_nocarry
    inc rd_objptr+1
@obj_nocarry:
    inc rd_objcount
    jmp @obj_loop
@obj_loop_done:
    lda rd_player
    sta rd_objptr
    lda rd_player+1
    sta rd_objptr+1
    jsr object_cell_try
    jsr compose_accept
    rts

; void redraw_dirty(const PlatformRoom* room, const PlatformObject* player)
; room is pushed by the caller (first param); player arrives in A/X (last
; param, cc65's own convention regardless of fastcall). Does not touch sp
; - the caller cleans up the pushed room pointer after this returns.
_redraw_dirty:
    sta rd_player
    stx rd_player+1
    ldy #0
    lda (sp),y
    sta rd_room
    iny
    lda (sp),y
    sta rd_room+1
    ; object_limit = (room==rendered_room) ? rendered_object_limit
    ;                                      : platform_room_object_limit(room)
    ; Low byte only, matching src/brazier.s's own documented assumption -
    ; no room in this game gets remotely close to 256 objects.
    lda rd_room
    cmp _rendered_room
    bne @need_call
    lda rd_room+1
    cmp _rendered_room+1
    bne @need_call
    lda _rendered_object_limit
    jmp @limit_ready
@need_call:
    lda rd_room
    ldx rd_room+1
    jsr _platform_room_object_limit
@limit_ready:
    sta rd_limit
    ; Scan the dirty bitmap directly (byte-index = y*5+q, bit = x&7, x =
    ; q*8+bit - see dirty_set()'s own comment for why) rather than a
    ; separate coordinate list: three nested loops (y 0..21, q 0..4 i.e.
    ; the 5 bitmap bytes per row, bit 0..7), skipping an entire all-zero
    ; byte (the common case) in one test.
    lda #0
    sta rd_byteidx
    sta rd_y
@y_loop:
    lda rd_y
    cmp #MAP_HEIGHT_CHARS
    bcc @y_continue
    jmp @all_done
@y_continue:
    lda #0
    sta rd_q
@q_loop:
    lda rd_q
    cmp #5
    bcc @q_continue
    jmp @q_done
@q_continue:
    ldy rd_byteidx
    lda dirty_cells,y
    bne @byte_has_bits
    jmp @q_next
@byte_has_bits:
    sta rd_bits
    lda rd_q
    asl a
    asl a
    asl a
    sta rd_basex
    lda #0
    sta rd_bit
@bit_loop:
    lda rd_bit
    cmp #8
    bcc @bit_continue
    jmp @bit_done
@bit_continue:
    ldx rd_bit
    lda bit_masks,x
    and rd_bits
    bne @bit_set
    jmp @bit_next
@bit_set:
    lda rd_basex
    clc
    adc rd_bit
    sta rd_curx
    lda rd_y
    sta rd_cury
    jsr compose_cell
    ; offset (16-bit) = cury*MAP_WIDTH_CHARS + curx, via repeated add
    ; (cury is at most 21, so this is a short loop, not a hot path)
    lda #0
    sta rd_offlo
    sta rd_offhi
    ldx rd_cury
    beq @rows_done
@rows_loop:
    lda rd_offlo
    clc
    adc #MAP_WIDTH_CHARS
    sta rd_offlo
    lda rd_offhi
    adc #0
    sta rd_offhi
    dex
    bne @rows_loop
@rows_done:
    lda rd_offlo
    clc
    adc rd_curx
    sta rd_offlo
    lda rd_offhi
    adc #0
    sta rd_offhi
    ; tile_offset = (cury>>1)*MAP_WIDTH_TILES + (curx>>1)
    lda rd_cury
    lsr a
    sta rd_tileoff
    asl a
    asl a
    clc
    adc rd_tileoff
    asl a
    asl a
    sta rd_tileoff
    lda rd_curx
    lsr a
    clc
    adc rd_tileoff
    sta rd_tileoff
    ; platform_base_colors[offset] = rd_color
    lda rd_offlo
    clc
    adc #<_platform_base_colors
    sta ptr1
    lda rd_offhi
    adc #>_platform_base_colors
    sta ptr1+1
    ldy #0
    lda rd_color
    sta (ptr1),y
    ; visible_color = platform_light_colors[((brightness[tile]&3)<<4)|(color&0xf)]
    ldy rd_tileoff
    lda _platform_brightness,y
    and #3
    asl a
    asl a
    asl a
    asl a
    sta rd_vcindex
    lda rd_color
    and #$0f
    ora rd_vcindex
    tay
    lda _platform_light_colors,y
    sta rd_vcolor
    ldy rd_tileoff
    lda _platform_view_tiles,y
    bne @view_ok
    lda #0
    sta rd_vcolor
@view_ok:
    lda rd_offlo
    clc
    adc #<SCREEN_RAM
    sta ptr1
    lda rd_offhi
    adc #>SCREEN_RAM
    sta ptr1+1
    ldy #0
    lda (ptr1),y
    cmp rd_ch
    beq @skip_screen
    lda rd_ch
    sta (ptr1),y
@skip_screen:
    lda rd_offlo
    clc
    adc #<COLOR_RAM
    sta ptr1
    lda rd_offhi
    adc #>COLOR_RAM
    sta ptr1+1
    ldy #0
    lda (ptr1),y
    cmp rd_vcolor
    beq @skip_color
    lda rd_vcolor
    sta (ptr1),y
@skip_color:
@bit_next:
    inc rd_bit
    jmp @bit_loop
@bit_done:
@q_next:
    inc rd_byteidx
    inc rd_q
    jmp @q_loop
@q_done:
    inc rd_y
    jmp @y_loop
@all_done:
    ; room was pushed onto the software stack by the caller (it's not the
    ; last parameter, so cc65 never puts it in A/X) - cc65's calling
    ; convention makes the CALLEE responsible for deallocating any
    ; stack-passed parameter, so this must pop those 2 bytes itself rather
    ; than falling through to a plain rts. Skipping this leaked 2 bytes of
    ; sp on every call, corrupting the caller's own locals (read via
    ; (sp),y right after this returns) starting with its very next call -
    ; this was the cause of a real bug (rain sprites disabled after one
    ; step: platform_object_move()'s post-redraw code misread a stale
    ; offset into an unrelated flag/pointer and ran code that zeroed
    ; platform_rain_active).
    jmp incsp2
