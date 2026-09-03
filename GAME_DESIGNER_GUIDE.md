# RPG Engine Guide for Story and Map Designers

## Who this guide is for

This guide is for the people designing the world, story, puzzles, dialogue,
rooms, and object catalogue. It explains what the engine can express, what its
hard limits are, and what information a programmer needs to implement a story
interaction.

It deliberately avoids source code. When it says that a room "can react" to
an event, the designer defines the desired conditions, result, and text; the
programmer connects that design to the room.

## The engine at a glance

| Feature | Current capability |
|---|---|
| World size | Up to 256 rooms, numbered `00` through `FF` |
| Room size | Always 20 x 11 tiles; rooms do not scroll |
| Movement | Half-tile steps on a 40 x 22 character-cell grid |
| Room exits | North, east, west, and south links, plus scripted transitions |
| Objects | 256 slots per room; at most 200 non-actor objects |
| Object catalogue | 255 usable global types; type 0 means an empty slot |
| Inventory | 32 distinct item stacks, up to 255 of an item type |
| Room text | 256 bytes shared by descriptions and dialogue in each room |
| On-map text | Two 40-character lines with word wrapping and paging |
| Lighting | Four tile-level brightness levels with light-blocking walls |
| Vision | 360-degree line of sight; opaque tiles hide what lies behind them |
| Persistent story state | Global story flags and in-memory room changes |
| Saved games | Data format is designed; disk save writing (the only save path) is not yet implemented |

## A world made of rooms

The game world is a collection of fixed screens. Each screen is a **room**,
whether it represents an indoor chamber, a street, a forest clearing, a ship
deck, or a stretch of coastline. There is no camera scrolling between them.

Every room is exactly **20 tiles wide and 11 tiles high**. A tile is 16 x 16
pixels and is built from four 8 x 8 character graphics. The bottom of the C64
screen is reserved for text and is not part of the room map.

This structure favors places with a clear identity. Treat a room as one
dramatic or tactical beat:

- one recognizable location;
- a small set of exits;
- a few important things to inspect or manipulate;
- a lighting idea;
- one or two state changes that make revisiting it meaningful.

### Connecting rooms

A room may have one linked neighbor in each cardinal direction. Crossing an
enabled edge loads that neighbor. The player appears on the opposite edge and
keeps the other coordinate, so aligned paths and doorways produce the cleanest
transition.

Links can be one-way. Enabling the east exit from one room does not
automatically enable the west exit back. This supports drops, collapsing
passages, prison chutes, and other deliberate one-way travel, but accidental
one-way links are easy to create.

Each edge can have a short exit description. In Look mode, moving the cursor
outward from an edge displays that description without revealing or loading
the neighboring room. Examples include:

- `A narrow path descends toward the coast.`
- `Cold air drifts through an iron archway.`
- `The bridge has collapsed.`

An absent exit reports that there is no exit in that direction. An enabled
exit without a description is shown simply as `An exit.`

The edge tile must be visible and sufficiently lit before its description can
be read. A distant exit in darkness can therefore remain unidentified.

### Scripted transitions

Stairs, ladders, cave mouths, trapdoors, portals, boats, and story events can
send the player to any room and any valid arrival position. These are not
limited to the four edge links.

The tile set includes a **Triggers action** property, but marking a tile with
it does not currently choose a destination or perform a transition by itself.
The story design must still specify:

- the source room and tile coordinate;
- whether the transition happens on entry or after Use;
- any required item or story condition;
- the destination room and arrival position;
- whether it is repeatable;
- failure and success text.

### Room event opportunities

Every room can react at four useful moments:

1. **Entering the room**: runs once on every visit after the room appears.
2. **Entering a tile**: runs when the player's hotspot crosses into a new
   16 x 16 tile, including the arrival tile after a room transition.
3. **Looking at a tile**: supplies environmental description instead of the
   default object list.
4. **Using a tile**: performs a room-specific action at or near the player.

Movement happens in 8-pixel half-tile steps, but tile-entry events happen only
when the hotspot enters a different full tile. A step that remains inside the
same tile does not retrigger the event.

## Tiles, walkability, and walls

The tile catalogue contains up to 256 reusable tile designs. Each tile can
carry several properties.

### Solid land

The player can currently enter a tile only when **Solid land** is set. This is
the active walkability rule. The separate **Blocks passage** property is not
currently consulted by player movement.

Use solid land for ordinary floors, paths, bridges, and other traversable
terrain. Leave it unset for walls, deep water, pits, void, and impassable
scenery. A destination room must also have solid land at the player's arrival
edge or the transition will fail.

### Blocks view

**Blocks view** controls both player sight and light propagation. It is
independent of walkability, so a tile can be:

- walkable and transparent, such as a normal floor;
- impassable and opaque, such as a stone wall;
- impassable but transparent, such as a pit or low fence;
- walkable but opaque, useful for unusual effects but potentially confusing.

An opaque tile itself remains visible, but tiles behind it are hidden. For
natural results, opaque walls should form deliberate, continuous shapes.
Small diagonal gaps and complicated corners should be playtested because the
visibility system uses a deliberately inexpensive C64-friendly propagation
model rather than modern pixel-perfect ray casting.

### Objects do not provide collision

Objects do not automatically block movement. A closed-door graphic, boulder,
table, or NPC can be walked through unless the underlying tile or a custom
story rule prevents it.

For fixed barriers, put the collision in the tile map. For a barrier that can
open or move, define both its closed and open map states and explain how the
state should be reconstructed when the player revisits the room.

### Map art and water animation

Each tile has four character cells, and each cell chooses one of the C64's 16
colors. Character artwork is shared: changing a character can alter many tiles
and objects that reuse it.

The engine provides one very cheap built-in animation. Map character 14 is
rotated by one pixel every second frame, producing flowing water or a similar
repeating texture. Any map or object graphic using character 14 will animate;
setting the tile's Water property alone does not create animation. Other
terrain animation needs additional character tricks or story-specific work.

### Rain

Rooms may opt into a fast rain layer: seven dark-blue diagonal streaks, each
a single static line, each starting at a random position along the top or
left edge and moving quickly down and to the right, respawning on a fresh
random edge position whenever one leaves the screen. It does not change the
map's tiles, object art, colors, lighting, or collision. Room 00 currently
demonstrates the effect. Rain is atmospheric only: it does not make terrain
wet, affect visibility, extinguish lights, or change story state unless the
room's story design adds those consequences. Rain automatically pauses
whenever a conversation portrait is shown (they share hardware sprites) and
resumes when the portrait closes, so room and dialogue authors do not need to
manage this themselves.

## Objects and actors

Objects are reusable visual and story entities placed over the tile map. The
player, NPCs, treasure, furniture, doors, flames, and movable puzzle pieces all
use the same basic object system.

### Position and hotspot

Objects are positioned at **half-tile resolution**: 40 x 22 possible character
cells per room. Their saved coordinate is the object's **hotspot**, not
necessarily its top-left corner.

The hotspot determines:

- the object's logical position;
- the tile occupied by an actor;
- where an object emits light;
- how a moving object crosses room and tile boundaries.

Choose the hotspot carefully. For a person it normally belongs at the feet.
For a brazier it belongs at the center of the flame or base. For a long table
it should be the cell that story instructions consistently treat as its
anchor.

### Size and appearance

An object type is a rectangular arrangement of 8 x 8 characters with one C64
color per character. Width and height may each be as large as 15, but the
rectangle may contain no more than **16 cells total**. Practical shapes include
1 x 1, 2 x 2, 4 x 4, 8 x 2, and 1 x 15.

Character 0 is transparent. Transparent cells do not draw and do not count as
an intersection for Look or Take. A graphic may extend across several tiles,
even when its hotspot lies elsewhere.

Objects are drawn in room-slot order; later slots appear over earlier slots.
The player is always drawn last. Use ordering deliberately when graphics
overlap.

### Names and types

Every object type has one global name of up to **14 characters**. That name is
used by Look, Take, and Inventory, so it should be a useful noun rather than an
internal label. All instances of a type share the same name, graphic, actor
status, and light radius.

Use separate types when an object's visible or mechanical state changes. For
example:

- `Unlit lamp` and `Lit lamp`;
- `Closed door` and `Open door`;
- `Full bucket` and `Empty bucket`;
- `Guard` and `Sleeping guard`.

### Actors and takeable objects

An object type can be marked as a PC/NPC actor. Actors cannot be taken, do
not count against the room's 200 non-actor limit, and (once a talk verb
exists) are the only object types that can be talked to.

A separate **Not takeable** checkbox marks a non-actor object type as fixed
scenery: the Take command's cursor never offers it as a candidate, and
`game_take_object()` also rejects it if something tries anyway. If a tile has
only not-takeable objects on it, pressing Take shows "I cannot take this."
instead of "Nothing to take.". Use this for a visible environmental object
that should never leave the room (a well, a statue, a locked chest) instead
of relying on tile artwork alone.

NPC movement, schedules, conversations, and combat behavior are not generic
engine features yet. NPCs can be represented and room logic can react to them,
but their behavior must be designed and implemented explicitly.

## Lighting and visibility

Lighting changes C64 colors without redrawing map graphics. It is calculated
once per full 16 x 16 tile, so all four character cells in a tile share a
brightness level.

There are four levels:

| Level | Appearance | Look range |
|---:|---|---|
| 3 | Full color | Any distance allowed by line of sight |
| 2 | Twilight; colors become darker versions | Up to 6 tiles |
| 1 | Dim; bright details become blue and dark details become black | Up to 2 tiles |
| 0 | Black | Nothing can be examined, even at close range |

The Look, Take, and map Use commands all respect visibility and these light
ranges. A hidden tile reports `I cannot see that.` A visible tile that is too
dark reports that the player must get closer. Room-specific descriptive text
is not called until these checks pass, so a custom Look response does not
accidentally reveal a secret through a wall or darkness.

Distances use tile steps, with diagonal separation treated like the larger of
the horizontal and vertical separation for examination range. Full light does
not impose a distance limit, but walls still do.

### Ambient light

The room begins with a global ambient level. Light sources can raise a tile's
brightness above ambient but can never lower it. If ambient is full light,
lamps have no visible effect.

Ambient level is not currently stored in the room asset. The sample begins at
full light and exposes `+` and `-` as development controls. A story design that
needs darkness, dawn, a light switch, or room-specific ambience must state
when the level changes so it can be attached to room entry or story state.

### Object light sources

Every object type has one emitted-light radius from 0 to 16. Zero means no
light. The radius is measured in 8 x 8 half-tile units but is rendered on the
full-tile lighting grid. The source snaps to the tile containing its hotspot.

The outer two half-tile units are dim, the next two are twilight, and the
remaining interior is full light. A radius of 10 therefore produces roughly:

- 6 half-tile units of full light;
- 2 more half-tile units of twilight;
- 2 outer half-tile units of dim light.

The maximum radius of 16 reaches roughly eight full tiles. Falloff uses an
approximately circular distance table but is visibly quantized to the tile
grid.

Overlapping lights take the brightest result; their values are not added.
Two dim lights do not combine into twilight. Because light radius belongs to
the global object type, individual instances cannot have different strengths
without using different types.

Only objects present in the room emit light. An emitting object stops lighting
the room when it is taken into inventory. Carried inventory does not emit
light automatically. A torch, lantern, or magical light puzzle therefore needs
an explicit design for how carrying, equipping, placing, lighting, and
extinguishing it should work.

### Light-blocking walls

Tiles marked Blocks view stop both vision and light. The first wall remains
visible, while the area beyond it is occluded.

Walls use a viewer-relative lighting rule. A source on the far side of a wall
does not make that wall appear lit to the player when the wall lies between
them. A lamp inside a house can illuminate the inner face without making the
outside glow at night. A source on the player's side may still light the same
wall, and the brightest valid source wins.

Light does not bounce or reflect. Shadows are tile-shaped, and corners should
be tested in the actual game before a puzzle depends on one exact fringe tile.

### Lightning

The engine has a white lightning flash that lasts two frames and then restores
the existing lighting. The sample binds it to `F`. It is a visual effect, not a
temporary source for Look or Take. It can support storms, magic, alarms, or
traps, but story-triggered timing still needs to be implemented.

## Player commands

### Movement

The cursor keys move the player one half-tile at a time. Only the hotspot's
destination tile is tested. The size of the player graphic and the footprints
of other objects do not enlarge collision.

### Look (`L`)

Pressing `L` places a blinking 18 x 18 frame on the player's tile. The cursor
can move anywhere in the current room. Return examines the selected tile; `L`
cancels.

If the room has a special Look response for that coordinate, it is shown.
Otherwise the engine lists every object's visible graphic that intersects the
tile. It checks the whole graphic, not only the object's hotspot. Identical
types are grouped, producing text such as:

`You see: 5 Gold, Sword, Shield.`

The automatic object list is limited to two screen lines and is shortened with
an ellipsis when necessary. Environmental tile art has no automatic name or
description. Important scenery such as murals, sea, tracks, cracks, statues,
and suspicious walls needs a room-specific Look description.

Moving outward while the Look cursor is already on an edge shows that edge's
exit description. It does not show the neighboring room.

### Take (`T`)

Pressing `T` opens the same tile cursor, restricted to the 3 x 3 tile area
centered on the player. This includes the player's own tile and diagonal tiles.
Return takes a non-actor object whose nontransparent graphic intersects the
selected tile.

If several objects intersect, the player can cycle through their names with
the cursor keys and confirm one with Return. The object is removed from the
room, one unit is added to inventory, and the message names what was taken.

There is no generic weight, carrying mass, ownership, theft reaction, or
takeability rule. There is also no Drop command yet.

### Use on the map (`U`)

On the map, `U` uses the same 3 x 3 nearby-tile cursor as Take. Return sends the
selected **tile coordinate** to that room's story logic. Use does not
automatically select or identify an object, even when several objects overlap
the tile.

This is appropriate for doors, switches, wells, altars, containers, beds,
machinery, and environmental puzzles. The design should identify both the
coordinate and the intended subject. A room with two usable objects on one tile
needs its own disambiguation rule or should be rearranged.

If the room has no Use behavior at that coordinate, the engine says
`Nothing happens.`

### Inventory (`I`) and inventory Use (`U`)

`I` opens a full-screen inventory. Cursor keys select an item stack, `U` uses
the selected item, and `I` returns to the map.

Inventory use is defined globally by item type rather than separately in every
room. It is suitable for food, potions, readable items, wearable or activatable
tools, and objects whose behavior is consistent throughout the game. It may
still check the current room and story state when necessary.

There is currently no built-in command flow for "choose an inventory item,
then target something on the map." Common alternatives are:

- map Use checks whether the player possesses the required item;
- inventory Use changes a story flag, after which map Use completes the action;
- a new combined-use interface is added as an explicit engine feature.

Likewise, item combining, equipment slots, inventory Look, dropping, and
choice menus are not generic features yet.

## Writing descriptions and dialogue

The map reserves two rows of 40 characters for text. Story text wraps at word
boundaries. If it needs more than two lines, the engine pauses for a keypress,
scrolls, and continues. The second line does not begin with leftover whitespace.

A room's content - Look descriptions, Use results, room-entry and tile-entry
narration, and any logic attached to them (checking or setting a story flag,
giving the player an object, branching on a condition) - is authored as a
*room script*: DSL source at `assets/scripts/<hex room id>.script`, written
and edited in the asset editor's Script mode (see `ROOM_CODE_API.md` for the
exact syntax and how room code runs an entry). There is no longer a
fixed-size text budget assigned per room the way there once was; a room
script is compiled like any other asset and only costs cartridge space
for what it actually contains.

Speaker portraits (`portrait_show`/`portrait_hide` inside a script, or
`platform_portrait_show()`/`platform_portrait_hide()` directly from room
code - see `PLATFORM_API.md` and the asset editor's Portrait mode) show
alongside script text, but there is still no generic rule for which portrait
accompanies which text - that association is up to whatever script or room
code shows both.

Standalone cutscenes and NPC conversations - not tied to a specific room -
are also authored as scripts (`assets/scripts/<hex ID>.script`, IDs
`0xF0`-`0xFF`, `game_script_play()`), with a real keyword-matched topic
system: a `conversation` declaration's topics are matched against the first
four letters the player types, with a reserved `"*"` fallback topic for
anything else. See `tools/compile_script.py`'s module docstring for the full
DSL and bytecode format.

Object names have a separate 14-character limit. Use plain ASCII while
authoring; the build converts text for the C64. The visible glyphs ultimately
depend on the custom text charset, so unusual punctuation and symbols should be
checked in the game.

### Dialogue design recommendations

- Put the subject early; the player sees only two lines at once.
- Keep speaker changes obvious in plain text.
- Avoid essential information only in an automatic object list, which may be
  truncated.
- Record whether a line repeats, appears once, or changes after an event -
  a room script can check a story flag to do this directly (`check_flag`/
  `set_flag`).
- Supply both success and failure text for puzzle actions.
- State whether a keypress pause is dramatically acceptable (`wait_key`).

## Story state and persistence

The engine has global story-state bytes that survive room changes. These can
represent facts such as:

- a door was unlocked;
- a warning was heard;
- a power circuit is active;
- an NPC has moved;
- the player knows a password;
- a one-time trap has fired.

Maintain a shared **story flag ledger** with a stable name, meaning, initial
value, and the events that set or clear each fact. The engine reserves 32 bytes
for flags; programmers can pack several yes/no facts into one byte when needed.

Health and mana values exist. Turn count advances with successful movement.
Day, hour, and minute fields exist but game time does not advance yet. There is
no generic combat, spell, rest, status-effect, or NPC schedule system.

Taken items and other captured room-object changes persist while the game is
running and remain changed when the player revisits a room. The current journal
can remember at most **200 changed object slots across the whole world**. This
is separate from the per-room limit of 200 non-actor objects.

The save-file structure has been designed, but writing and loading saves from
disk is not finished (saves are disk-only; there is no cartridge-flash save
path, by design). At present, progress is not expected to survive ending the
emulator or powering off the C64.

## Puzzle patterns that fit the engine

### Light and shadow

- A dark room where a fixed lamp reveals only the center.
- A movable or removable light that changes which clues can be examined.
- Interior lights whose walls remain dark when viewed from outside.
- A switch that changes ambient light or swaps unlit objects for lit types.
- A clue visible through an opening but too dim to examine from far away.
- A light source that must remain in one room, because inventory items do not
  emit light automatically.

Avoid puzzles that depend on sub-tile shadow precision, reflected light, or two
weak lights adding together.

### Environmental interactions

- Use a tree, altar, machine, door, well, or wall at a known coordinate.
- Trigger narration or damage upon entering a tile.
- Reveal a hidden exit after a story flag changes.
- Swap a closed object type for an open one and update the underlying
  walkability.
- Use one-time and repeatable room-entry events to show consequences.

### Inventory puzzles

- A map location checks whether the player carries a key or tool.
- A consumable changes health, mana, or a story fact from inventory.
- Reading an inventory object reveals a clue.
- Using an item in the correct room arms the next map interaction.

Avoid assuming that the player can drag an item onto scenery, combine any two
items, equip arbitrary gear, or drop an item unless that feature is explicitly
added.

### Observation puzzles

- Room-specific Look text for tile artwork that has no object record.
- Large objects whose different sections reveal different details.
- Exit descriptions that change with room state.
- Details hidden by both a wall and darkness until the player changes position.

Remember that Look itself cannot reveal a tile outside line of sight or beyond
the current light range.

## Current limitations to design around

- Rooms are fixed at 20 x 11 tiles and never scroll.
- Only cardinal edge neighbors are stored directly.
- Movement collision is tile-based and uses Solid land only.
- Objects and NPCs do not inherently block movement.
- Object lighting is fixed per global type and evaluated per full tile.
- Inventory objects do not emit room light.
- There is no Drop, Equip, item-combination, or inventory-target-map command.
- There is no generic conversation choice UI, and no automatic link between
  dialogue text and the portrait API.
- NPC schedules, AI, combat, game time, and quests are not generic systems yet.
- A room has only 256 bytes of local text and about 1 KiB for bespoke behavior.
- The automatic Look list is limited to two lines.
- Only 32 distinct inventory stacks can be carried.
- Only 200 room-object differences can be remembered globally during a run.
- Persistent disk save writing (the only planned save path) is not implemented yet.

These are platform boundaries, not prohibitions. A story can request an engine
extension, but it should identify that dependency before maps and dialogue are
built around it.

## Interaction handoff template

Use this template when specifying a puzzle, event, or dialogue beat:

```text
Interaction name:
Room ID:
Tile coordinate:
Activation: enter room / enter tile / Look / map Use / inventory Use

Visible subject:
Required lighting or line of sight:
Required inventory:
Required story facts:

Success result:
Failure result:
Repeatable or one-time:

Objects removed, added, moved, or changed:
Tile/walkability changes:
Ambient or emitted-light changes:
Destination room and arrival position, if any:

Text before action:
Success text:
Failure text:
Later/revisit text:

Story facts set or cleared:
Persistence expectations:
```

For a whole room, also record:

- room name and hexadecimal ID;
- dramatic purpose;
- ambient light level;
- north/east/west/south links and descriptions;
- solid-land and Blocks-view plan;
- placed objects and intended draw order;
- entry and tile-entry events;
- all Look and Use coordinates;
- room-text byte budget;
- expected state on the first visit and on later visits.

## Asset editor workflow

Run the asset editor and use **Room mode** for world building.

1. Open the charset, tiles, object types, and room. When using the project
   asset server, opening a room also fills untouched defaults from the standard
   project assets.
2. Paint the 20 x 11 tile map.
3. Hover the map to read the tile coordinate used in story specifications.
4. Set walkability and Blocks-view properties in Tile mode.
5. Add cardinal room links and exit descriptions.
6. Place objects on the 40 x 22 half-tile grid.
7. Use **Show objects** to compare the composited scene with the underlying
   tile map.
8. Enter room-local text and watch the 256-byte budget.
9. In Object mode, set each type's name, size, hotspot, graphic, actor status,
   and emitted-light radius.
10. Test the room in the game at its intended ambient light level, especially
    walls, corners, overlapping objects, arrival edges, and puzzle targets.

The editor creates the scene and data. Coordinate-driven story behavior still
needs to be included in the interaction handoff.
