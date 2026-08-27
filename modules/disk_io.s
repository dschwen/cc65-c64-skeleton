.setcpu "6502"

; Block-oriented KERNAL sequential-file I/O for the save/load overlay,
; hand-written in assembly because the C equivalent (a byte loop calling
; small CHRIN/CHROUT wrappers) was too large for the overlay's 4080-byte
; window. Callers must map KERNAL in (platform_memory_kernal()) before
; using either block routine and restore gameplay mapping
; (platform_memory_game()) afterward.
;
; Parameters/results pass through shared globals (matching the
; platform_ef_copy_* convention), not the C stack:
;   platform_disk_slot    0-7
;   platform_disk_side    'A' or 'B'
;   platform_disk_buffer  pointer into record_buffer
;   platform_disk_want    read: max bytes to accept; write: exact byte count
;   platform_disk_got     read: bytes actually stored (0 on open/chkin
;                         failure); write: bytes actually sent (compare to
;                         platform_disk_want for success)
;
; KNOWN ISSUE: writes report success (READST clean, byte count matches)
; but the file does not end up on disk -- confirmed by an immediate
; readback within the same session failing to find it, and the drive's
; own error channel reporting 62 (FILE NOT FOUND) after a write that the
; data-channel status claimed succeeded. The identical KERNAL call
; sequence (scratch, then SETLFS/SETNAM"...,W"/OPEN/CHKOUT/CHROUT/CLOSE)
; works correctly in a plain BASIC-booted VICE session against the same
; device/image, so this is specific to running from this game's cartridge
; overlay context; root cause not yet found. Reads (this file's other
; routine) work correctly in both contexts. See SAVE_GAME.md.

.export _platform_disk_slot
.export _platform_disk_side
.export _platform_disk_buffer
.export _platform_disk_want
.export _platform_disk_got
.export _platform_disk_read_block
.export _platform_disk_write_block

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
_platform_disk_side:    .res 1
_platform_disk_buffer:  .res 2
_platform_disk_want:    .res 2
_platform_disk_got:     .res 2
; "S0:SnX,W" -- the trailing "SnX" (offset 3) is also the plain 3-byte
; filename used to open the same slot/side for read; ",W" (offset 6)
; extends it to the write-open form CBM DOS requires to create a new file
; (a bare name defaults to read intent and fails immediately, creating
; nothing, when the file does not already exist).
filename: .res 8

.segment "CODE"

build_filename:
    lda #'S'
    sta filename+3
    lda _platform_disk_slot
    clc
    adc #'0'
    sta filename+4
    lda _platform_disk_side
    sta filename+5
    lda #','
    sta filename+6
    lda #'W'
    sta filename+7
    rts

scratch_block:
    jsr build_filename
    lda #'S'
    sta filename+0
    lda #'0'
    sta filename+1
    lda #':'
    sta filename+2
    lda #15
    ldx #DISK_DEVICE
    ldy #15
    jsr SETLFS
    lda #6
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
    jsr build_filename
    lda #2
    ldx #DISK_DEVICE
    ldy #2
    jsr SETLFS
    lda #3
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

    lda _platform_disk_buffer
    sta ptr1
    lda _platform_disk_buffer+1
    sta ptr1+1
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
    ldy #0
    sta (ptr1),y
    inc ptr1
    bne :+
    inc ptr1+1
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
; compare it to platform_disk_want for success. See the KNOWN ISSUE note
; at the top of this file: this currently does not persist the write.
_platform_disk_write_block:
    jsr scratch_block
    jsr build_filename
    lda #2
    ldx #DISK_DEVICE
    ldy #2
    jsr SETLFS
    lda #5
    ldx #<(filename+3)
    ldy #>(filename+3)
    jsr SETNAM
    jsr OPEN_
    ldx #2
    jsr CHKOUT_
    jsr READST
    bne @wclose_fail

    lda _platform_disk_buffer
    sta ptr1
    lda _platform_disk_buffer+1
    sta ptr1+1
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
    ldy #0
    lda (ptr1),y
    jsr CHROUT_
    jsr READST
    bne @wclose_fail
    inc ptr1
    bne :+
    inc ptr1+1
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
    rts
@wclose_fail:
    jsr CLRCHN_
@wopen_fail:
    lda #2
    jsr CLOSE_
    rts
