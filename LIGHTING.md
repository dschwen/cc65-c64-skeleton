# Lighting architecture

Lighting is a Color RAM effect. It never changes screen character codes, so a
lighting update does not redraw the map or any object graphics.

## Runtime buffers

- `platform_base_colors[880]` stores the original color selected by the tile
  and object compositor for every cell in the 40x22 map area.
- `platform_brightness[880]` stores brightness 0-3 for the same cells.
- The assembly `platform_lighting_apply_native` pass combines both buffers and
  writes `$D800-$DB6F`. The two bottom status rows are outside this range.
- A sprite text overlay is applied after lighting. Its saved colors therefore
  contain lit colors, and hiding it restores the current lighting result.

The color lookup rows use standard C64 color indices:

| Level | Meaning | Mapping for input colors 0..15 |
|---:|---|---|
| 0 | no light | `0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0` |
| 1 | dim | `0,6,0,6,0,0,0,6,0,0,6,0,6,6,6,6` |
| 2 | twilight | `0,12,9,6,11,11,0,8,9,11,2,0,11,5,6,12` |
| 3 | full | `0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15` |

Level 2 keeps black as black but never maps any other input color to itself.
White becomes gray, red becomes brown, cyan/light blue become blue, yellow
becomes orange, brown becomes dark gray, and light variants fall back to their
darker hue. Level 1 deliberately discards hue and contrast: only colors bright
enough to remain perceptible become blue; everything else is black.

## Object emitters

Object-type record byte 49 is the emitted-light radius. Zero disables emission,
and values above 16 clamp to 16. Emission originates at the object's hotspot,
which is already expressed in character-cell/half-tile coordinates.

The implemented room-light rebuild is:

1. Fill the brightness buffer with the room ambient/global level.
2. Visit populated room objects and the player in the same type-aware manner as
   drawing. Skip types whose emitted-light byte is zero.
3. Propagate from each hotspot using ceiling Euclidean distance. For radius `R`,
   `0..R-4` is full, the next two cells are twilight, and the outer two are dim.
4. Combine overlapping lights with `max`; light never makes a cell darker.
5. Apply the color lookup once after all sources have contributed.

Distance uses one 16x16 first-quadrant lookup table, indexed as
`(abs_y << 4) | abs_x`. Radius-16 axis endpoints are handled separately, while
table positions geometrically beyond distance 16 use an outside sentinel. The
source bounds are clipped to 40x22 before visiting cells.

If walls later block light, add an explicit opaque tile-property bit and replace
the direct footprint pass with a bounded flood fill using a fixed 880-bit visited
buffer. The existing solid-land bit is movement policy and must not implicitly
mean opaque.

Rebuild lighting after a room load, when an emitter moves, or when an emitter's
state changes. A later optimization can compare the old and new brightness
buffers and write only changed Color RAM cells; it does not require changing the
map or object blitters.

## Lightning flash

`platform_lightning()` invokes a native assembly effect, bound to `F` by the
sample game. It sets `$D020/$D021` to white, clears the 880 map Color RAM cells,
waits for two changes of `platform_frame_counter`, sets `$D020/$D021` back to
black, and jumps into the normal native lighting pass. The restore therefore
respects both the offscreen base colors and the current brightness map without
redrawing characters, tiles, or objects. This wait depends on the installed
raster IRQ remaining enabled.
