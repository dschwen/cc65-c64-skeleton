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

## In-place Inventory service contract

`modules/inventory.c`, `modules/inventory_draw.s`, and `story/story.c` are
linked as one read-only service at `$A000` and executed from EasyFlash bank 48
ROMH. It has no BSS or initialized writable data. Five mutable UI bytes live in
always-visible RAM at `$C174-$C178`; selected slot lookup is derived directly
from `GameState` rather than retained in the old 32-byte cache. Build-time
validators check the module's segments, resident imports, entry vector, packed
bytes, and cartridge-half bounds.

The service does not overwrite rebuildable rendering and lighting buffers. While
`story_use_inventory()` runs, it may safely use `GameState`, inventory,
health/mana, flags, and resident APIs below `$8000`. It must not read or call
anything in `$8000-$BFFF`, because both EasyFlash halves are mapped while it
runs. It also cannot use self-modifying code: writes under cartridge ROM reach
RAM, not the instruction bytes later fetched from ROM. Queue map-affecting
consequences in game state and apply them after returning when needed.

The service returns before the resident engine restores map mode. Its wrapper
clears the inventory screen, restores the raster charset split, redraws the
room, and rebuilds lighting/visibility. Therefore inventory mutations made by
story code are visible immediately after closing the inventory.

There is no overlay header, load-time copy, or run-time checksum. The fixed
location comes from `cfg/easyflash_layout.json`, and `build/inventory.map` is
the authoritative linked-size and entry report. Item-name drawing nests a far
call into the bank-47 type-info service, which in turn fetches cold data from
bank 46; the bank trampoline unwinds back to bank 48 afterward.
