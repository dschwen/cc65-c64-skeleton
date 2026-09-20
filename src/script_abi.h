#ifndef SCRIPT_ABI_H
#define SCRIPT_ABI_H

#include <stdint.h>

/* Mutable state for the banked script interpreter (modules/script.c, bank
 * BANK_SCRIPT - see src/banks.h). Plain resident global (src/script_
 * runtime.c) under oscar64, unlike the cc65-era design's ROOMSTAGE-tail
 * placement - modules/script.c owns it only for the duration of a
 * synchronous script call, same as before. */
typedef struct ScriptWorkspace {
    uint8_t* buffer;
    uint16_t capacity;
    uint16_t window_base;
    uint16_t window_length;
    uint16_t resource_length;
} ScriptWorkspace;

extern ScriptWorkspace script_workspace;

#endif
