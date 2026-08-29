.setcpu "6502"

; Block-oriented KERNAL sequential-file I/O for the save/load overlay,
; hand-written in assembly because the C equivalent (a byte loop calling
; small CHRIN/CHROUT wrappers) was too large for the overlay's 4119-byte
; window. Callers must map KERNAL in (platform_memory_kernal()) before
; using either block routine and restore gameplay mapping
; (platform_memory_game()) afterward.
;
; Parameters/results pass through shared globals (matching the
; platform_ef_copy_* convention), not the C stack:
;   platform_disk_slot    0-7
;   platform_disk_buffer  pointer into record_buffer
;   platform_disk_want    read: max bytes to accept; write: exact byte count
;   platform_disk_got     read: bytes actually stored (0 on open/chkin
;                         failure); write: bytes actually sent (compare to
;                         platform_disk_want for success)
;
; CBM DOS protocol letters below are explicit low-PETSCII byte values.
; With cl65's C64 target charmap, assembly literals such as 'S' and 'W'
; become $D3/$D7; DOS then treats the supposed mode suffix as part of a
; read filename and reports FILE NOT FOUND instead of creating a file.

.export _platform_disk_slot
.export _platform_disk_buffer
.export _platform_disk_want
.export _platform_disk_got
.export _platform_disk_read_block
.export _platform_disk_write_block
.export _platform_disk_index_read_block
.export _platform_disk_index_write_block

.importzp ptr1

SETLFS = $ffba
SETNAM = $ffbd
OPEN_  = $ffc0
CLOSE_ = $ffc3
CHKIN_ = $ffc6
CHKOUT_ = $ffc9
CLRCHN_ = $ffcc
CHRIN_ = $ffcf
CHROUT_ = $ffd2
READST = $ffb7

DISK_DEVICE = 8

.segment "BSS"
_platform_disk_slot:    .res 1
_platform_disk_buffer:  .res 2
_platform_disk_want:    .res 2
_platform_disk_got:     .res 2
; Shared workspace for "S0:SINDEX,W". Slot files use the shorter views
; "S0:Sn" / "Sn,W" at the same offsets.
filename: .res 11

.segment "CODE"

build_slot_filename:
    ; CBM DOS command/type/mode letters must use low PETSCII. cl65's C64
    ; target charmap encodes source literals such as 'S' as $D3 instead.
    lda #$53
    sta filename+3
    lda _platform_disk_slot
    clc
    adc #'0'
    sta filename+4
    lda #','
    sta filename+5
    lda #$57
    sta filename+6
    rts

build_index_filename:
    lda #$53                 ; S
    sta filename+3
    lda #$49                 ; I
    sta filename+4
    lda #$4e                 ; N
    sta filename+5
    lda #$44                 ; D
    sta filename+6
    lda #$45                 ; E
    sta filename+7
    lda #$58                 ; X
    sta filename+8
    lda #','
    sta filename+9
    lda #$57                 ; W
    sta filename+10
    rts

scratch_slot:
    jsr build_slot_filename
    lda #5
    bne scratch_named

scratch_index:
    jsr build_index_filename
    lda #9

scratch_named:
    pha
    lda #$53
    sta filename+0
    lda #'0'
    sta filename+1
    lda #':'
    sta filename+2
    lda #15
    ldx #DISK_DEVICE
    ldy #15
    jsr SETLFS
    pla
    ldx #<filename
    ldy #>filename
    jsr SETNAM
    jsr OPEN_
    lda #15
    jsr CLOSE_
    rts

; Reads up to platform_disk_want bytes into platform_disk_buffer, stopping
; early at EOF/error. platform_disk_got is the actual count (0 if the file
; could not be opened at all).
_platform_disk_read_block:
    jsr build_slot_filename
    lda #2
    bne read_named

_platform_disk_index_read_block:
    jsr build_index_filename
    lda #6

read_named:
    pha
    lda #2
    ldx #DISK_DEVICE
    ldy #2
    jsr SETLFS
    pla
    ldx #<(filename+3)
    ldy #>(filename+3)
    jsr SETNAM
    jsr OPEN_
    jsr READST
    bne @open_fail
    ldx #2
    jsr CHKIN_
    jsr READST
    bne @close_fail

    lda #0
    sta _platform_disk_got
    sta _platform_disk_got+1

@read_loop:
    lda _platform_disk_got+1
    cmp _platform_disk_want+1
    bcc @do_read
    bne @done
    lda _platform_disk_got
    cmp _platform_disk_want
    bcs @done
@do_read:
    jsr CHRIN_
    pha
    ; KERNAL routines own low zero page and may clobber cc65's ptr1.
    ; Reload it only after CHRIN, from the stable BSS cursor.
    lda _platform_disk_buffer
    sta ptr1
    lda _platform_disk_buffer+1
    sta ptr1+1
    pla
    ldy #0
    sta (ptr1),y
    inc _platform_disk_buffer
    bne :+
    inc _platform_disk_buffer+1
:
    inc _platform_disk_got
    bne :+
    inc _platform_disk_got+1
:
    jsr READST
    beq @read_loop
@done:
    jsr CLRCHN_
    lda #2
    jsr CLOSE_
    rts
@close_fail:
    jsr CLRCHN_
@open_fail:
    lda #2
    jsr CLOSE_
    lda #0
    sta _platform_disk_got
    sta _platform_disk_got+1
    rts

; Scratches the target file, then writes exactly platform_disk_want bytes
; from platform_disk_buffer. platform_disk_got is the actual count sent;
; compare it to platform_disk_want for success. The routine also drains the
; drive command channel after CLOSE: a 1541 can still be writing the final
; data block and directory entry after the KERNAL CLOSE call has returned.
_platform_disk_write_block:
    jsr scratch_slot
    jsr build_slot_filename
    lda #4
    bne write_named

_platform_disk_index_write_block:
    jsr scratch_index
    jsr build_index_filename
    lda #8

write_named:
    pha
    lda #2
    ldx #DISK_DEVICE
    ldy #2
    jsr SETLFS
    pla
    ldx #<(filename+3)
    ldy #>(filename+3)
    jsr SETNAM
    jsr OPEN_
    ldx #2
    jsr CHKOUT_
    jsr READST
    bne @wclose_fail

    lda #0
    sta _platform_disk_got
    sta _platform_disk_got+1

@write_loop:
    lda _platform_disk_got+1
    cmp _platform_disk_want+1
    bcc @do_write
    bne @wdone
    lda _platform_disk_got
    cmp _platform_disk_want
    bcs @wdone
@do_write:
    lda _platform_disk_buffer
    sta ptr1
    lda _platform_disk_buffer+1
    sta ptr1+1
    ldy #0
    lda (ptr1),y
    jsr CHROUT_
    jsr READST
    bne @wclose_fail
    inc _platform_disk_buffer
    bne :+
    inc _platform_disk_buffer+1
:
    inc _platform_disk_got
    bne :+
    inc _platform_disk_got+1
:
    jmp @write_loop
@wdone:
    jsr CLRCHN_
    lda #2
    jsr CLOSE_
    jsr wait_drive_ready
    rts
@wclose_fail:
    jsr CLRCHN_
@wopen_fail:
    lda #2
    jsr CLOSE_
    rts

; Wait for DOS to finish the preceding write and read its two status digits.
; Keep platform_disk_got unchanged only for 00 (OK); any other status turns
; the write into a caller-visible failure. The two digits reuse filename
; bytes after SETNAM no longer needs them.
wait_drive_ready:
    lda #15
    ldx #DISK_DEVICE
    ldy #15
    jsr SETLFS
    lda #0
    tax
    tay
    jsr SETNAM
    jsr OPEN_
    jsr READST
    bne @status_fail
    ldx #15
    jsr CHKIN_
    jsr READST
    bne @status_close_fail

    jsr CHRIN_
    sta filename+0
    jsr CHRIN_
    sta filename+1

@status_done:
    jsr CLRCHN_
    lda #15
    jsr CLOSE_
    lda filename+0
    cmp #$30
    bne @status_bad
    lda filename+1
    cmp #$30
    beq @status_ok
@status_bad:
    lda #0
    sta _platform_disk_got
    sta _platform_disk_got+1
@status_ok:
    rts

@status_close_fail:
    jsr CLRCHN_
@status_fail:
    lda #15
    jsr CLOSE_
    lda #0
    sta _platform_disk_got
    sta _platform_disk_got+1
    rts
