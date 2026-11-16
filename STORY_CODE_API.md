# Story-specific item-use code

Map interactions and inventory-item interactions have separate extension
points:

- A room's script entries (`assets/scripts/<room>.script`, dispatched from
  `rooms/<room>.rc`) handle using a location in one room, including the
  inventory checks that go with it (`check_item`, `take_item`, ...) - see
  `ROOM_CODE_API.md`.
- `story/story.c:story_use_inventory(slot)` handles using a carried item in
  every room.

Nothing story-specific lives in `src/` or `modules/`: the engine knows no
object types, flags or puzzle rules. `story/` and the scripts own all of it.

The global hook is declared in `src/story.h`:

```c
uint8_t story_use_inventory(uint8_t inventory_slot);
```

The argument is an index into `game_state.inventory`, not merely a type ID.
This preserves access to both the selected type and its quantity:

```c
uint8_t story_use_inventory(uint8_t slot) {
    GameInventorySlot* item = &game_state.inventory[slot];

    if (item->type == STORY_OBJECT_HEALTH_POTION) {
        game_state.health = game_state.maximum_health;
        (void)game_inventory_remove(item->type, 1u);
        game_text_write(PLATFORM_TEXT_LINE_TOP, "You feel better.", 1u);
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}
```

Keep story-owned flag indices, entry keys and tile IDs in `src/story.h`;
object-type IDs come from the generated `build/object_types.h` (`OBJ_*`).
Reusable engine constants remain in `game.h` or `platform.h`.

## In-place Inventory service contract

`modules/inventory.c` and `story/story.c` put code/constants in `BANK_INVENTORY` via
Oscar64 section pragmas. Mutable UI state and `GameState` remain resident.
Exact placement is in `build/game.map`; there is no separate link, copied
overlay or runtime header.

The main loop selects the bank with `far_call_bank` before entering
`game_inventory_show()`. Story code may use resident state/APIs and other
functions within this same bank. Cross-bank calls require a resident gate;
item-name lookup uses the type-info service in `BANK_TYPEINFO` and restores the caller's bank.
Never assume a declaration or function pointer selects its target bank.

ROM code must not self-modify or read hidden RAM in `$8000-$BFFF`.
Queue room transitions instead of changing the active room during a callback.
The inventory service restores text/split state and redraws the map on exit,
so inventory mutations are visible when returning to gameplay.

See [GUIDE_OSCAR64_C64.md](GUIDE_OSCAR64_C64.md) and
[EASYFLASH_CARTRIDGE.md](EASYFLASH_CARTRIDGE.md) for compiler/banking rules.
