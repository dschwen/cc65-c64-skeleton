.setcpu "6502"

; Resident ABI entry for the independently loaded bottom-text pager. Keep this
; address synchronized with cfg/text_module.cfg.
.export _platform_text_output_native
_platform_text_output_native = $3a73
