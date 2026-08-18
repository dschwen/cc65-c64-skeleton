# C64 Asset Editor

Browser-based editor for:
- two monochrome charset banks, each with 256 8x8 characters
- 256 tiles where each tile is a 2x2 character arrangement (16x16 pixels)
- per-tile properties (1 byte per tile)
- fixed 20x11 room editing with tiles, 256 object slots, and room text
- 256 fixed-size object type definitions with character/color graphics

Open `index.html` directly in a browser for local-only editing.

Run the Python server when you want to open/save files directly from the repository `assets/` directory:

```bash
make asset-editor
```

Or run it directly:

```bash
python3 tools/asset-editor/server.py --host 127.0.0.1 --port 8000
```

Then open `http://127.0.0.1:8000/`.

The server has no third-party dependencies and only exposes file operations under `assets/`.

## Editing modes

- Character mode:
  - Edit one 8x8 character at a time.
  - Toggle active charset bank (bank 0 or bank 1) for editing/preview.
  - Left mouse draws set pixels, right mouse clears pixels.
  - Roll selected character by 1 pixel with wrap-around (`Up`, `Down`, `Left`, `Right`).
  - Trial text preview can render typed text using:
    - `ASCII -> PETSCII -> screen code` conversion
    - direct byte-to-character-index mapping
  - In `ASCII -> PETSCII -> screen code` mode:
    - lowercase `a-z` map to character indices `1-26`
    - uppercase `A-Z` map to character indices `65-90`
  - Import supports both `CCHR` files and raw charset ROM binaries:
    - `2048` bytes (`256 * 8`): one charset bank (import target bank selectable)
    - `4096` bytes (`2 * 256 * 8`): two charset banks (loads both banks)
- Tile mode:
  - Each tile has four character indices and four color values (0-15), one color per quadrant.
  - Draw directly on the 16x16 tile canvas to edit underlying 8x8 character pixel data.
  - Hold `Shift` and click a quadrant on the 16x16 tile canvas to set it to the currently selected character.
  - Includes a tile test canvas for painting selected tiles.
- Object mode:
  - Edits the global list of 256 fixed-size object-type records.
  - Choose width and height; the visual grid immediately resizes to `width * height` cells (maximum 16).
  - Select a character from charset bank 0, choose a color, and click a cell to paint it.
  - Right-click a cell to make it transparent (character 0).
  - Use the hotspot tool to click the cell that anchors the object's room coordinate.
  - Edit the 14-byte name, actor flag, and emitted-light byte; object type 0 remains reserved.
- Room mode:
  - Rooms are fixed at 20x11 tiles (40x22 half-tile/object coordinates).
  - Paint tiles with the tile tool.
  - Place/select objects with the object tool.
  - Shift-click moves the selected object's hotspot.
  - Right-click deletes the object under the pointer.
  - Select an object in the room list to edit its type and half-tile x/y coordinates.
  - Use `Delete selected` beside the object fields to remove the selected room slot.
  - Edit a 256-byte pool of zero-terminated room strings; generated offsets
    are displayed for use by the C API.

## Binary formats

### Charset file (`.cchr`)

Header (8 bytes):
- Byte 0..3: ASCII `CCHR`
- Byte 4: format version (`2`)
- Byte 5: bank count (`2`)
- Byte 6..7: reserved (`0`)

Payload:
- `bankCount * 256 * 8` bytes
- Each character is 8 consecutive bytes (one byte per row, MSB is leftmost pixel).

Compatibility import:
- `CCHR` version `1` is accepted as a single-bank import into selected target bank.

Raw ROM import (headerless):
- `2048` bytes: one 256-char bank
- `4096` bytes: two consecutive 256-char banks

### Tile file (`.ctil`)

Header (8 bytes):
- Byte 0..3: ASCII `CTIL`
- Byte 4: format version (`1`)
- Byte 5: reserved (`0`)
- Byte 6..7: tile count, little-endian (`256`)

Payload part 1: tile definitions (`tileCount * 8` bytes)
- For each tile, 8 bytes in this order:
  - top-left char index, top-left color
  - top-right char index, top-right color
  - bottom-left char index, bottom-left color
  - bottom-right char index, bottom-right color

Payload part 2: tile properties (`tileCount` bytes)
- One property byte per tile.
- Bit layout:
  - bit 0: blocks passage
  - bit 1: blocks view
  - bit 2: solid land
  - bit 3: water
  - bit 4: triggers action
  - bit 5..7: reserved

### Room file (`00` through `FF`)

Header (9 bytes):
- Byte 0: width in tiles (`20`)
- Byte 1: height in tiles (`11`)
- Byte 2: room ID
- Byte 3: format version (`2`)
- Byte 4: valid-exit mask, north/east/west/south in bits 0-3.
- Byte 5: north room ID.
- Byte 6: east room ID.
- Byte 7: west room ID.
- Byte 8: south room ID.

Payload (`1244` bytes):
- `220` tile IDs in row-major order.
- `768` object bytes: 256 slots of type ID, hotspot x, hotspot y.
- `256` bytes containing zero-terminated room strings addressed by offset.

Total room file size: `1253` bytes.

Compatibility import accepts the legacy 224-byte 20x11 map and 1248-byte
format-1 room. Missing adjacency/object/text fields are cleared. Export always
writes format 2.

Existing asset directories can be migrated in place from format 1 and assigned
links at the same time:

```bash
python3 tools/migrate_rooms_v2.py assets \
  --link 00:east:01 --link 01:west:00
```

### Object type list (`.cobj`)

The file contains 256 records of 64 bytes (`16384` bytes total). Type ID is
the record index; type 0 is reserved as an empty room-object slot.

Per record:
- Byte 0: width in high nibble, height in low nibble.
- Byte 1: hotspot x in high nibble, hotspot y in low nibble.
- Byte 2..15: name, up to 14 bytes, zero-padded.
- Byte 16..31: 16 row-major screen character codes; 0 is transparent.
- Byte 32..47: 16 corresponding C64 color indices.
- Byte 48: flags; bit 0 marks a PC/NPC actor.
- Byte 49: emitted light amount; `0` means no light.
- Byte 50..63: reserved.

Width and height must be nonzero and `width * height` must not exceed 16.

## Notes

- Tile editing can modify characters shared by multiple tiles.
- Right-click erases/paints tile `0`, clears an object-type cell, or deletes a room object.
- Press `Tab` (when not focused in an input/select/button) to toggle active charset bank in Character and Tile modes. Object and Room modes are fixed to bank 0.
- Use the `Show room/test grid` checkbox to toggle tile grid overlays for the room and test canvases.
- Use the `Help` button for an in-editor keyboard shortcut reference.
- Character copy/paste:
  - `Copy Char` / `Paste Char` buttons in character mode
  - `Ctrl/Cmd+C` and `Ctrl/Cmd+V` in character mode (outside form fields)
- Tile copy/paste:
  - `Ctrl/Cmd+C` and `Ctrl/Cmd+V` in tile mode copy/paste the selected tile definition and property byte
- Tile-mode quick assign:
  - `1`, `2`, `3`, `4` assign selected character to tile quadrants
  - order: top-left, top-right, bottom-left, bottom-right
  - `Shift` + left-clicking a quadrant on the tile canvas performs the same assignment for that quadrant
  - Left/right dragging on the tile canvas edits pixels in the underlying characters
- Room/test rendering uses cached tile atlases.
- Editor state persists across reloads using browser `localStorage`.
- Server-backed asset open/save is available only when served via `server.py`.
- The left-side mode tabs use separate asset dropdowns and save paths for
  charset, tile, room, and object-type files.
- Saving refuses to overwrite an existing asset if the server identifies it as another type or as ambiguous data.
