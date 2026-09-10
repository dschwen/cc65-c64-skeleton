.setcpu "6502"

.include "platform.inc"

.export _platform_memory_game
.export _platform_memory_kernal
.export _platform_memory_all_ram
.export _platform_easyflash_enable
.export _platform_easyflash_disable
.export _platform_easyflash_copy_roml
.export _platform_easyflash_copy_romh
.export _platform_ef_copy_bank
.export _platform_ef_copy_offset
.export _platform_ef_copy_destination
.export _platform_ef_copy_size
.export _platform_irq_save_disable
.export _platform_irq_restore
.export _platform_object_types_clear
.export _platform_boot_is_easyflash
.export _platform_bank_call_enter
.export _platform_bank_call_leave
.export _platform_far_call
.export far_call_bank_operand
.export far_call_target_operand
.export far_call_cpu_map_operand
.export far_call_control_operand

.segment "DATA"
_platform_ef_copy_bank:
    .byte 0
_platform_ef_copy_offset:
    .word 0
_platform_ef_copy_destination:
    .word 0
_platform_ef_copy_size:
    .word 0

; The general bank stack: a real array, not the CPU hardware stack. A "thin
; call wrapper" needs enter/jsr-target/leave as three separate steps, and
; the hardware stack can't carry state across that middle jsr - anything
; bank_call_enter pushes there before its own rts would sit *above* the
; return address rts must pop next, corrupting it. Depth 2 supports one
; level of real nesting beyond the base case - today's only caller
; (easyflash_copy_window) never nests at all - kept this small because
; PROGRAM (where DATA lives, see below) has very little free margin; see
; MEMORY_MAP.md. No overflow guard: a depth-3 nested banked call is not
; reachable by anything in this codebase today, so this trusts the
; invariant rather than spending bytes to check it, matching how the rest
; of this file's low-level routines already trust their preconditions.
; DATA, not BSS, for the same reason as ef_shadow_bank/control below (must
; stay readable as ordinary RAM even when $8000-$BFFF is already banked to
; cart ROM).
.segment "DATA"
BANK_STACK_DEPTH = 2
bank_stack_bank:
    .byte 0, 0
bank_stack_control:
    .byte 0, 0
bank_stack_cpuport:
    .byte 0, 0
; The actual interrupt-flag bit at the moment of each enter - not assumed
; to always be "enabled". A very first call (this file's own boot-time
; object-type load, before the game has ever issued its own cli) runs with
; interrupts still off from CPU reset; a leave that unconditionally
; re-enabled them there jumped into the raster IRQ before it was installed
; and reachable, and hung/crashed on real hardware timing (found live in
; VICE - this exact call site hung solid, only "fixed" by the accident of
; a monitor breakpoint perturbing timing enough to dodge it. That was
; never a real fix). Restoring the exact captured flag per level, the same
; way php/plp would if it could safely cross the jsr/rts boundary here
; (see bank_stack_index's comment), avoids assuming anything about the
; caller's interrupt state.
bank_stack_flags:
    .byte 0, 0
bank_stack_index:
    .byte 0
bank_call_scratch:
    .byte 0

; EASYFLASH_BANK is write-only on real hardware (no readback), so
; easyflash_copy_window below needs a readable shadow of the current
; bank/mode to know what to restore when it's done. Initialized to
; EASYFLASH_OFF (matching what cart/ef_boot.s leaves it as) by
; _platform_boot_is_easyflash below, before anything can bank-switch.
;
; Deliberately in "DATA", NOT "BSS": BSSRAM ($B500-$B7DD currently; see
; MEMORY_MAP.md)
; sits inside the $8000-$BFFF EasyFlash ROML/ROMH banking window. Writes
; there always land in the underlying RAM regardless of banking (true on
; real 6510 hardware), but easyflash_copy_window's *read* of these shadow
; bytes happens after it has already switched CPU_PORT to bank the
; cartridge ROM in for reading - so a BSS-resident shadow byte would read
; back stale flash content instead of the value stored there, not the RAM
; byte. This was a real, since-fixed bug: ef_shadow_bank came back as
; garbage on its very first read (traced live in VICE - $7B instead of the
; $00 confirmed present in RAM moments earlier). DATA loads into PROGRAM,
; well below $8000, so it stays readable as ordinary RAM no matter what's
; banked in above it.
.segment "DATA"
ef_shadow_bank:
    .byte 0
ef_shadow_control:
    .byte 0

.segment "LOWCODE"

; All public mapping functions preserve the interrupt flag and the cassette
; control bits in the upper part of the 6510 port.
.macro SET_MAP value
    php
    sei
    lda CPU_DDR
    ora #CPU_PORT_MASK
    sta CPU_DDR
    lda CPU_PORT
    and #$f8
    ora #value
    sta CPU_PORT
    plp
    rts
.endmacro

_platform_memory_game:
    SET_MAP CPU_MAP_GAME

_platform_memory_kernal:
    SET_MAP CPU_MAP_KERNAL

_platform_memory_all_ram:
    SET_MAP CPU_MAP_ALL_RAM

; fastcall: A = EasyFlash bank. The gameplay map keeps I/O visible.
; Unused by the current codebase (kept as public API); doesn't update
; ef_shadow_bank/control, so don't mix calls to this with
; easyflash_copy_window's bank tracking until it does.
_platform_easyflash_enable:
    tax
    php
    sei
    lda CPU_PORT
    and #$f8
    ora #CPU_MAP_CART_8K
    sta CPU_PORT
    txa
    sta EASYFLASH_BANK
    lda #EASYFLASH_8K
    sta EASYFLASH_CONTROL
    plp
    rts

_platform_easyflash_disable:
    php
    sei
    lda #EASYFLASH_OFF
    sta EASYFLASH_CONTROL
    lda CPU_PORT
    and #$f8
    ora #CPU_MAP_GAME
    sta CPU_PORT
    plp
    rts

; General banked-call building blocks - the "thin call wrapper" primitive:
; every future banked call site is expected to bracket its jsr with these
; two instead of hand-rolling its own push/switch/pop sequence:
;
;     lda #MY_BANK
;     jsr _platform_bank_call_enter
;     jsr my_banked_routine
;     jsr _platform_bank_call_leave
;
; Contract: interrupt code (src/irq.s) never calls these and never touches
; EasyFlash bank state. Its complete per-frame code and data closure is below
; $8000, so the caller's interrupt-enable state is restored after the short
; register/map transition and IRQs may run while the banked operation itself
; executes. This keeps the charset split, frame counter and environment tick
; alive during long copies and far calls.
;
; fastcall: A = target EasyFlash bank number. Always switches 16 KiB mode
; (both ROML and ROMH), matching every current banked use. Nests correctly
; through the bank stack array above: a banked routine that needs to call
; into a *different* bank just wraps its own nested jsr with another
; enter/leave pair, each level unwinding back to exactly the bank/mode/CPU
; map it found on entry.
; No php/plp here (unlike this file's other routines): each is its own
; jsr/rts pair, and a php pushed here would sit on the hardware stack
; *above* the jsr's own return address, so the matching plp - needed in
; _platform_bank_call_leave, a *different* jsr/rts pair - would pop the
; wrong bytes and misdirect the rts (see bank_stack_index's comment above
; for the general problem). Instead the interrupt flag is captured into
; bank_stack_flags (php+pla, safe - both within this one routine) and
; restored explicitly in _platform_bank_call_leave via pha+plp (also both
; within that one routine) - functionally the same save/restore php/plp
; would give, just carried across the jsr/rts boundary through memory
; instead of the hardware stack.
;
; HIGHCODE, not LOWCODE like this file's other simple mapping routines:
; PROGRAM (LOWCODE's memory area) has very little free margin, while HIGH
; has plenty. Both are equally always-resident and reachable by plain
; jsr/rts regardless of EasyFlash banking state - only the DATA above
; (must stay byte-for-byte readable even when $8000-$BFFF is banked to
; cart ROM) needed the specific placement reasoning; this is just code.
.segment "HIGHCODE"

_platform_bank_call_enter:
    sta bank_call_scratch

    php
    pla
    ldx bank_stack_index
    sta bank_stack_flags,x
    sei

    lda CPU_PORT
    sta bank_stack_cpuport,x
    and #$f8
    ora #CPU_MAP_CART_16K
    sta CPU_PORT

    lda ef_shadow_bank
    sta bank_stack_bank,x
    lda ef_shadow_control
    sta bank_stack_control,x
    inx
    stx bank_stack_index

    lda bank_call_scratch
    sta ef_shadow_bank
    sta EASYFLASH_BANK
    lda #EASYFLASH_16K
    sta ef_shadow_control
    sta EASYFLASH_CONTROL
    dex
    lda bank_stack_flags,x
    and #$04
    bne @enter_done
    cli
@enter_done:
    rts

; Unwind one level pushed by _platform_bank_call_enter. Interrupts are masked
; only while restoring the previous bank/mode and CPU map, then the exact
; entry flags are restored.
_platform_bank_call_leave:
    sei
    dec bank_stack_index
    ldx bank_stack_index

    lda bank_stack_control,x
    sta ef_shadow_control
    sta EASYFLASH_CONTROL
    lda bank_stack_bank,x
    sta ef_shadow_bank
    sta EASYFLASH_BANK
    lda bank_stack_cpuport,x
    sta CPU_PORT

    ldx bank_stack_index
    lda bank_stack_flags,x
    pha
    plp
    rts

; Far call dispatcher: one shared, self-modifying trampoline.
;
; The FAR_CALL macro (src/platform.inc) writes the callee's bank, 16-bit
; address, CPU map, and EasyFlash control value into operands below, then
; jsr's here. The two mapping values are separate because ROML needs CPU map
; $37 even when EasyFlash itself is in 8 KiB mode ($06). With that CPU map,
; BASIC covers $A000-$BFFF; 8 KiB mode does not make upper RAM readable.
;
; Reentrant *despite* the self-modification, which is why this needs none of
; the fixed-depth bank_stack array the enter/leave pair above uses: every
; patched byte is consumed before the inner jsr runs, and nothing below that
; jsr is patched. A banked routine may therefore re-patch this trampoline for
; a nested far call of its own without disturbing an outer invocation still in
; flight - the outer call's saved bank/mode/map lives on the CPU stack, which
; nests as deep as the stack allows instead of a hardcoded two. Enter/leave
; needed the array only because they are two separate jsr/rts pairs and so
; cannot keep anything on the hardware stack across the call; this routine
; brackets the call itself, so it can.
;
; The caller must guarantee no interrupt performs a far call between the
; macro's patch and its jsr. That is already the codebase-wide contract -
; see _platform_bank_call_enter above: interrupt code never bank-switches.
;
; Constraint on the callee: an 8 KiB ROML call sees cartridge ROM at
; $8000-$9FFF and BASIC ROM at $A000-$BFFF; a 16 KiB ROMH call sees cartridge
; ROM across both halves. Neither may read underlying RAM anywhere in
; $8000-$BFFF. The cc65 software stack ($C000) and GameState ($C100) remain
; visible in either mode. The mode-aware
; validate_banked_module.py enforces the linked-import side of this ABI. See
; MEMORY_MAP_TARGET.md.
_platform_far_call:
    ; Register contract, so a banked routine can be an ordinary cc65
    ; __fastcall__ function: A and X are passed through to the callee and its
    ; A/X return comes back to the caller. Y is the trampoline's own scratch
    ; (it carries the argument, then the return byte, across the stretch where
    ; A is busy switching banks) and is NOT preserved in either direction.
    ; That covers cc65's 8- and 16-bit argument/return passing, which use A
    ; and A/X.
    tay

    php
    pla
    sta bank_call_scratch
    pha
    sei
    lda ef_shadow_bank
    pha
    lda ef_shadow_control
    pha
    lda CPU_PORT
    pha

    and #$f8
    ora #$00                    ; CPU-map operand patched by FAR_CALL
far_call_cpu_map_operand = * - 1
    sta CPU_PORT

    lda #$00                    ; operand patched by FAR_CALL
far_call_bank_operand = * - 1
    sta ef_shadow_bank
    sta EASYFLASH_BANK
    lda #$00                    ; EasyFlash-control operand patched by FAR_CALL
far_call_control_operand = * - 1
    sta ef_shadow_control
    sta EASYFLASH_CONTROL

    lda bank_call_scratch
    and #$04
    bne @far_call_irq_state_ready
    cli
@far_call_irq_state_ready:
    tya                         ; hand the argument back to the callee in A
    jsr $0000                   ; operand patched by FAR_CALL
far_call_target_operand = * - 2
    tay                         ; stash the callee's return byte
    sei                         ; bank/map restoration is the other critical edge

    ; Nothing below here is patched, so an inner FAR_CALL cannot disturb this
    ; invocation's unwind - the whole basis of the reentrancy argument above.
    ; Keep I/O visible until both EasyFlash registers are restored. Writing
    ; CPU_PORT first would make $DE00/$DE02 disappear if a future caller came
    ; from an all-RAM map such as $34.
    pla
    sta bank_call_scratch
    pla
    sta ef_shadow_control
    sta EASYFLASH_CONTROL
    pla
    sta ef_shadow_bank
    sta EASYFLASH_BANK
    lda bank_call_scratch
    sta CPU_PORT

    tya                         ; A := return byte, before plp restores flags
    plp
    rts

.segment "HIGHCODE"

; Copy ROML/ROMH into underlying RAM without touching C stack or BSS while the
; cartridge mapping hides $8000-$BFFF. Uses the general bank_call_enter/leave
; primitives above to save/restore the previous EasyFlash bank and mode
; around the copy, so it's safe to call from code that is itself already
; running from a banked-in cartridge window (e.g. a nested cold-object-info
; fetch), unwinding back to exactly the bank/mode it found on entry.
_platform_easyflash_copy_roml:
    lda #$80
    bne easyflash_copy_window

_platform_easyflash_copy_romh:
    lda #$a0
easyflash_copy_window:
    sta $f8

    lda _platform_ef_copy_offset
    sta $fb
    lda _platform_ef_copy_offset+1
    clc
    adc $f8
    sta $fc
    lda _platform_ef_copy_destination
    sta $fd
    lda _platform_ef_copy_destination+1
    sta $fe

    lda _platform_ef_copy_bank
    jsr _platform_bank_call_enter

    lda _platform_ef_copy_size
    ora _platform_ef_copy_size+1
    beq @romh_done
@romh_byte:
    ldy #0
    lda ($fb),y
    sta ($fd),y
    inc $fb
    bne @romh_source_ok
    inc $fc
@romh_source_ok:
    inc $fd
    bne @romh_destination_ok
    inc $fe
@romh_destination_ok:
    lda _platform_ef_copy_size
    bne @romh_decrement_low
    dec _platform_ef_copy_size+1
@romh_decrement_low:
    dec _platform_ef_copy_size
    lda _platform_ef_copy_size
    ora _platform_ef_copy_size+1
    bne @romh_byte

@romh_done:
    jsr _platform_bank_call_leave
    rts

.segment "LOWCODE"

; Return the previous status byte in A, then leave IRQs disabled.
_platform_irq_save_disable:
    php
    pla
    sei
    rts

; fastcall: restore a status byte returned by platform_irq_save_disable().
_platform_irq_restore:
    pha
    plp
    rts

; Called once, first thing, from platform_init(): establishes the shadow's
; initial value to match what cart/ef_boot.s leaves EASYFLASH_CONTROL as,
; before anything can call easyflash_copy_window (_platform_easyflash_
; copy_roml/romh), which reads it.
_platform_boot_is_easyflash:
    lda #EASYFLASH_OFF
    sta ef_shadow_control
    lda CART_MARKER
    cmp #CART_MARKER_0
    bne @not_cartridge
    lda CART_MARKER+1
    cmp #CART_MARKER_1
    bne @not_cartridge
    lda #1
    bne @clear_marker
@not_cartridge:
    lda #0
@clear_marker:
    ldx #0
    stx CART_MARKER
    stx CART_MARKER+1
    ldx #0
    rts

; Clear only the hot-object-type arena, including its three linker padding
; bytes: $C180-$E482. The cc65 software stack ($C000-$C0FF) and GameState
; ($C100-$C173) now precede this arena and are live when platform_init() calls
; us, so the old whole-$C000-$FFFF clear corrupted active program state.
_platform_object_types_clear:
    php
    sei
    lda CPU_PORT
    pha
    and #$f8
    ora #CPU_MAP_ALL_RAM
    sta CPU_PORT
    lda #$80
    sta $fb
    lda #$c1
    sta $fc
    lda #$03                    ; $E483-$C180 = $2303 bytes
    sta $fd
    lda #$23
    sta $fe
    ldx #0
    ldy #0
@clear_byte:
    txa
    sta ($fb),y
    inc $fb
    bne :+
    inc $fc
:
    lda $fd
    bne :+
    dec $fe
:
    dec $fd
    lda $fd
    ora $fe
    bne @clear_byte
    pla
    sta CPU_PORT
    plp
    rts
