#ifndef SCRIPT_ABI_H
#define SCRIPT_ABI_H

#include <stdint.h>

/* Mutable state for the script interpreter that executes directly from
 * EasyFlash ROML.  This record lives in the unused tail of ROOMSTAGE, below
 * the cartridge window; modules/script.c owns it only for the duration of a
 * synchronous script call. */
typedef struct ScriptWorkspace {
    uint8_t* buffer;
    uint16_t capacity;
    uint16_t window_base;
    uint16_t window_length;
    uint16_t resource_length;
} ScriptWorkspace;

extern ScriptWorkspace script_workspace;

/* Resident-RAM gates used by the banked interpreter.  Each target may live
 * under ROML/BASIC and may itself perform nested EasyFlash accesses. */
uint8_t script_host_portrait_show(uint8_t portrait_id, uint8_t side);
void script_host_portrait_hide(void);
uint8_t script_host_inventory_add(uint8_t type, uint8_t quantity);
uint8_t script_host_transition_request(uint8_t room, uint8_t x, uint8_t y);
void __fastcall__ script_host_transition_show_message(const char* text);
void script_host_lightning(void);

#endif
