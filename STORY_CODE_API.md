# Story-specific item-use code

Map interactions and inventory-item interactions have separate extension
points:

- Room DSL/C fragments implementing `use_at(tile_x, tile_y)` handles using a location in one room.
- `story/story.c:story_use_inventory(slot)` handles using a carried item in
  every room.

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

Keep story-owned object IDs, room IDs, and flag indices in `src/story.h`.
Reusable engine constants remain in `game.h` or `platform.h`.

## In-place Inventory service contract

`modules/inventory.c` and `story/story.c` put code/constants in bank 5 via
Oscar64 section pragmas. Mutable UI state and `GameState` remain resident.
Exact placement is in `build/game.map`; there is no separate link, copied
overlay or runtime header.

The main loop selects the bank with `far_call_bank` before entering
`game_inventory_show()`. Story code may use resident state/APIs and other
functions within this same bank. Cross-bank calls require a resident gate;
item-name lookup uses the type-info service in bank 1 and restores bank 5.
Never assume a declaration or function pointer selects its target bank.

ROM code must not self-modify or read hidden RAM in `$8000-$BFFF`.
Queue room transitions instead of changing the active room during a callback.
The inventory service restores text/split state and redraws the map on exit,
so inventory mutations are visible when returning to gameplay.

See [GUIDE_OSCAR64_C64.md](GUIDE_OSCAR64_C64.md) and
[EASYFLASH_CARTRIDGE.md](EASYFLASH_CARTRIDGE.md) for compiler/banking rules.
