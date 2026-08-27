# Story-specific item-use code

Map interactions and inventory-item interactions have separate extension
points:

- `rooms/XX.c:use_at(tile_x, tile_y)` handles using a location in one room.
- `story/story.c:story_use_inventory(slot)` handles using a carried item in
  every room.

The global hook is declared in `src/story.h`:

```c
uint8_t __fastcall__ story_use_inventory(uint8_t inventory_slot);
```

The argument is an index into `game_state.inventory`, not merely a type ID.
This preserves access to both the selected type and its quantity:

```c
uint8_t __fastcall__ story_use_inventory(uint8_t slot) {
    GameInventorySlot* item = &game_state.inventory[slot];

    if (item->type == STORY_OBJECT_HEALTH_POTION) {
        game_heal(20u);
        (void)game_inventory_remove(item->type, 1u);
        game_text_write(PLATFORM_TEXT_LINE_TOP, "You feel better.", 1u);
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}
```

Keep story-owned object IDs, room IDs, and flag indices in `src/story.h`.
Reusable engine constants remain in `game.h` or `platform.h`.

## Inventory overlay contract

`modules/inventory.c`, `modules/inventory.s`, and `story/story.c` are linked as
one overlay at `$A4E9-$B4D8`. EasyFlash stores its loadable bytes in bank 48
ROMH. The resident loader checks its ABI, load size, entry vector, BSS
bounds, and payload checksum before execution.

The overlay replaces rebuildable rendering and lighting buffers. While
`story_use_inventory()` runs, it may safely use `GameState`, inventory,
health/mana, flags, and bottom-text APIs. It must not call room/map drawing,
lighting rebuilds, room transitions, or any API that expects `WORKBSS` to hold
valid render state. Queue map-affecting consequences in game state and apply
them after returning when such behavior is needed.

The overlay returns before the resident engine restores map mode. Its wrapper
clears the inventory screen, restores the raster charset split, redraws the
room, and rebuilds lighting/visibility. Therefore inventory mutations made by
story code are visible immediately after closing the inventory.

The overlay header is 16 bytes (`IU`, ABI 1) followed by loadable code/data and
linked BSS. `build/inventory.map` is the authoritative size report. The current
module occupies only about 1.1 KiB including its PRG load header, leaving most
of the 4,080-byte overlay window available for story logic.
