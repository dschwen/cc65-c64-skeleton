# C64 Asset Editor

Browser-based editor for:
- two monochrome charset banks, each with 256 8x8 characters
- 256 tiles where each tile is a 2x2 character arrangement (16x16 pixels)
- per-tile properties (1 byte per tile)
- map editing with compact 4-byte header format

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
- Map mode:
  - Edit map dimensions, map ID, and reserved byte.
  - Paint tiles onto the map grid.
  - Large maps render inside a scrollable viewport.

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

### Map file (`.bin`)

Header (4 bytes):
- Byte 0: width in tiles
- Byte 1: height in tiles
- Byte 2: map ID
- Byte 3: reserved

Payload:
- `width * height` bytes of tile numbers in row-major order.

## Notes

- Tile editing can modify characters shared by multiple tiles.
- Right-click is used for erasing/painting tile `0` on canvases.
- Press `Tab` (when not focused in an input/select/button) to toggle active charset bank.
- Use the `Show map/test grid` checkbox to toggle tile grid overlays for the map and test canvases.
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
- Map/test rendering uses cached tile atlases for faster redraws on large maps.
- Editor state persists across reloads using browser `localStorage`.
- Server-backed asset open/save is available only when served via `server.py`.
- The left-side mode tabs use separate asset dropdowns and save paths for charset, tile, and map files.
- Saving refuses to overwrite an existing asset if the server identifies it as another type or as ambiguous data.
