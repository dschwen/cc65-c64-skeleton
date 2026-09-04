# Lighting architecture

Lighting is a Color RAM effect. It never changes screen character codes, so a
lighting update does not redraw the map or any object graphics.

## Runtime buffers

- `platform_base_colors[880]` stores the original color selected by the tile
  and object compositor for every cell in the 40x22 map area.
- `platform_brightness[220]` stores brightness 0-3 for each 2x2-character map
  tile. All four character colors in a tile use the same brightness.
- The assembly `platform_lighting_apply_native` pass combines both buffers and
  writes `$D800-$DB6F`. It reads one brightness byte and applies it to four
  base-color bytes. The two bottom status rows are outside this range.
- The sprite Look cursor does not alter Color RAM. Showing, moving, blinking,
  and hiding it therefore require no lighting-buffer restoration.

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
which is expressed in character-cell/half-tile coordinates. Lighting snaps the
hotspot to its containing tile, while object graphics retain half-tile motion.
The stored radius remains in half-tile units for asset compatibility.

The implemented room-light rebuild is:

1. Fill the brightness buffer with the room ambient/global level.
2. Build the compact list of opaque room tiles and clear their packed
   four-quadrant wall-light cache.
3. Visit populated room objects and the player in the same type-aware manner as
   drawing. Skip types whose emitted-light byte is zero.
4. Propagate from each hotspot using ceiling Euclidean tile distance, converted
   back to half-tile units before comparing with the stored radius. For radius
   `R`, remaining radius 4 or more is full, 2-3 is twilight, and 0-1 is dim.
5. Max-combine open-tile light directly into `platform_brightness`. For every
   opaque tile reached by the source, max-combine its tile level into the wall
   cache quadrant(s) containing the source.
6. Select each cached wall level using the quadrant(s) containing the player.
7. Apply the color lookup once after all sources have contributed.

Distance uses one 16x16 first-quadrant lookup table, indexed as
`(abs_y << 4) | abs_x`, where the deltas are now full tiles. The lookup result
is doubled before applying the unchanged half-tile radius and band thresholds.
A radius-16 source therefore examines at most eight tiles in each direction.
The source bounds are clipped to 20x11 before visiting tiles.

The hot propagation loop is implemented by
`platform_light_source_apply_native` in `src/render.s`. C performs bank-aware
type lookup and rectangle clipping once per source. Assembly then patches the
brightness and distance-table row addresses once per scanline and max-combines
each open tile without multiplication or C calls. Opaque tile entries are
removed from that native pass after their tile value has been stored in the
cache. The final assembly pass expands the complete 220-byte result over Color
RAM.

Each cached wall tile uses one byte: four 2-bit maxima for northwest,
northeast, southwest, and southeast viewers. A source on a wall axis belongs to
both adjacent quadrants. Selecting the maximum of the viewer's applicable
quadrants is equivalent to the strict viewer-relative rule: a wall is rejected
when its X or Y lies strictly between viewer and source. This retains exact
tile-level falloff and correct max composition across multiple lights.

## Occlusion and player vision

Tile-property bit 1 already means `blocks view`; it is independent of passage
and solid-land policy. Light occlusion and distance falloff both operate at the
20x11 tile level.

Visibility uses a one-parent outward ring propagation:

1. Clear a 220-entry state mask and mark the viewer visible.
2. Initialize the eight cells of ring 1 as visible unless the viewer tile is
   opaque.
3. Process each square perimeter outward. Corners write one diagonal child,
   ordinary edge cells write one axial child, and the eight near-corner cells
   write two children. This partitions ring `r+1` without duplicate writers.
4. A visible opaque tile remains visible but writes `OCCLUDED`; an occluded tile
   continues writing `OCCLUDED`. There is no state merging.
5. During a full emitter pass, cache the opaque tile's light in the source-side
   quadrant(s), then exclude that tile from the open-cell propagation loop. It
   still writes `OCCLUDED` to its children.
6. Normalize occluded states to zero after the last ring.
7. Let the native tile propagation loop write only when the target tile is
   marked visible. Overlapping emitters still max-combine.

The complete ring builder is native assembly and uses states 0=unprocessed,
1=visible, and 2=occluded in the same byte-per-tile buffer. It needs no dynamic
allocation or merge rule. The 220-byte mask can later be packed if RAM pressure
becomes more important than direct indexing.

Player vision uses the same `blocks view` semantics and a separate persistent
220-byte mask. The native lighting renderer writes either the final lit colors
or four black cells for each tile in one pass; it never exposes a fully lit
intermediate map. Illumination and visibility remain independent: a visible
area may be dark, and a lit area outside the player's view is black.
The current view is 360 degrees with no range limit. Because the origin is a
tile, half-tile player movement recomputes LOS only when it crosses a tile edge.
A later facing cone can restrict the target bounds/angles without changing the
final Color RAM mask pass.

Because emission is tile-based, moving an emitting object within its current
tile redraws only its changed graphics. Lighting is rebuilt only when its
hotspot crosses into another tile.

The separating-wall cache is populated independently for every emitter. A wall
aligned with either endpoint belongs to both adjacent quadrants and is not
considered between them. Another light on the player's side may therefore
illuminate the same wall through the normal max-composition rule. The player's
360-degree visibility mask still includes the first opaque tile in each
propagated path.

Rebuild lighting after a room load, when an emitter moves, or when an emitter's
state changes. When a non-emitting player crosses a tile boundary, rebuild only
the player visibility mask, select new wall levels from the cache, and run the
native Color RAM pass. Open-cell brightness and emitter visibility masks are
not recomputed. A later optimization can compare the old and new Color RAM
result, but a scanned dirty pass was measured slower for the current room and
is deliberately not retained.

A deterministic VICE benchmark of five half-cell player moves crossed tile
boundaries on moves 2, 3, and 5. Those complete `platform_player_step()` calls
fell from 496,959/472,491/480,698 cycles to 370,651/348,972/357,209 cycles, a
25.4-26.1% reduction. Moves that stayed within the same tile were effectively
unchanged. These figures include object redraw, player LOS, wall-cache
selection, and the final Color RAM pass.

## Lightning flash

`platform_lightning()` invokes a native assembly effect, triggered by the
`lightning` room-script statement (see `tools/compile_script.py`'s module
docstring) - there is no manual test keybinding for it. It sets `$D020/$D021`
to white, clears the 880 map Color RAM cells,
waits for two changes of `platform_frame_counter`, sets `$D020/$D021` back to
black, and jumps into the normal native lighting pass. The restore therefore
respects both the offscreen base colors and the current brightness map without
redrawing characters, tiles, or objects. This wait depends on the installed
raster IRQ remaining enabled.
