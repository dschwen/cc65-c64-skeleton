; Preflight one actor slot in the staged destination room. The C implementation
; was disproportionately large; this routine exploits the fixed room address,
; three-byte object records, and the <=200-entry delta journal.

.setcpu "6502"

.import _game_world_delta_count
.import _game_world_deltas
.importzp ptr1, ptr2, sreg

.export _world_room_has_actor_slot

ROOM_STAGE_OBJECTS = $a000 + 233
DELTA_BYTES = 5

.segment "BSS"
preflight_room: .res 1
preflight_free: .res 1

.segment "SAVECODE"

; fastcall: A = room ID. Returns A=1 when the effective staged object list has
; a free slot after journal deltas, otherwise A=0. X is returned as zero.
_world_room_has_actor_slot:
    sta preflight_room
    lda #<ROOM_STAGE_OBJECTS
    sta ptr2
    lda #>ROOM_STAGE_OBJECTS
    sta ptr2+1
    lda #0
    sta preflight_free
    ldx #0

@count_base_free:
    ldy #0
    lda (ptr2),y
    bne @advance_base
    inc preflight_free
    bne @advance_base
    dec preflight_free
@advance_base:
    clc
    lda ptr2
    adc #3
    sta ptr2
    bcc :+
    inc ptr2+1
:
    inx
    bne @count_base_free

    lda #<_game_world_deltas
    sta ptr1
    lda #>_game_world_deltas
    sta ptr1+1
    ldx _game_world_delta_count
    beq @done

@adjust_delta:
    ldy #0
    lda (ptr1),y
    cmp preflight_room
    bne @next_delta

    iny
    lda (ptr1),y
    sta sreg
    lda #0
    sta sreg+1
    asl sreg
    rol sreg+1
    clc
    lda sreg
    adc (ptr1),y
    sta sreg
    bcc :+
    inc sreg+1
:
    clc
    lda sreg
    adc #<ROOM_STAGE_OBJECTS
    sta sreg
    lda sreg+1
    adc #>ROOM_STAGE_OBJECTS
    sta sreg+1

    iny
    lda (ptr1),y
    pha
    ldy #0
    lda (sreg),y
    beq @base_was_free

    pla
    bne @next_delta
    inc preflight_free
    beq @saturate_deleted
    bne @next_delta
@saturate_deleted:
    dec preflight_free
    bne @next_delta

@base_was_free:
    pla
    beq @next_delta
    dec preflight_free

@next_delta:
    clc
    lda ptr1
    adc #DELTA_BYTES
    sta ptr1
    bcc :+
    inc ptr1+1
:
    dex
    bne @adjust_delta

@done:
    lda preflight_free
    beq :+
    lda #1
:
    ldx #0
    rts
