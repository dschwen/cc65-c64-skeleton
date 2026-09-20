# The Last Guest at Moonfen Inn
## Story & Puzzle Design

*A compact fantasy adventure for the C64 adventure/RPG engine*

---

## 1. Design Goals

**The Last Guest at Moonfen Inn** is a small, self-contained fantasy adventure designed as a demonstration game for the C64 engine.

The emphasis is on:

- exploration rather than combat;
- environmental and object-based puzzles;
- NPC dialogue as both characterization and puzzle information;
- a puzzle tree with several parallel threads;
- a small world that feels larger than its five playable rooms;
- revisiting rooms as new information and objects become available.

There are **no character stats, combat encounters, or RPG progression systems** required.

The playable world consists of five 20×11 rooms:

| ID | Room | Function |
|---|---|---|
| `00` | Storm Crossroads | Opening, discovery, outdoor ingredients |
| `01` | Common Room | Main social hub and finale |
| `02` | Kitchen and Pantry | Puzzle workshop and cellar entrance |
| `03` | Guest Corridor | Evidence gallery and Pell |
| `04` | Moonlit Cellar | Supernatural reveal and final information |

Coordinates in this document are zero-based `(x,y)` tile coordinates.

---

## 2. Premise

A violent storm catches the player on a lonely road through Moonfen. The roads become impassable and, during a flash of lightning, an old milestone reveals something that was not visible before: a path to **Moonfen Inn**.

The inn appears to offer shelter, but something is wrong.

Its guests are stranded by more than the weather. Tokens, invitations, seals, and old rules connect them to the inn. The innkeeper, **Master Vey**, presents one interpretation of events, but the evidence around the inn does not quite agree with him.

The player gradually reconstructs what happened by helping the guests, examining their identifying symbols, opening a warded cellar, and discovering **Mara** through a broken moon mirror.

The story culminates in the common room, where the player can expose the truth behind the inn and its supposed "last guest."

---

# 3. Opening

## Room 00 — Storm Crossroads

### Dramatic purpose

The opening room should:

1. establish the storm;
2. prevent the player simply continuing along the road;
3. introduce observation as an important mechanic;
4. reveal Moonfen Inn;
5. place two later puzzle ingredients in view before the player knows they matter.

### Starting position

The player begins on the road near the center of the map, approximately `(10,6)`.

The storm has made both directions along the road unsafe or impassable.

Initial narration should make it clear that continuing the journey is not currently an option.

### The hook

At `(7,5)` stands an old milestone.

At first it appears unimportant. During a lightning flash, however, markings become visible on its face.

Examining or using it reveals the hidden inscription/path and sets:

`INN_REVEALED`

The inn can then be reached through the door/path at `(15,9)`.

This gives the player an active reason to enter the inn: **it is the only available shelter from the storm**, but discovering it requires observation rather than simply walking through a conveniently placed door.

### Important objects

- **Blue Seal** — `(14,6)`, lying in a puddle.
- **Moonmint** — `(13,7)`, growing beside the road.
- **Rain barrel** — `(15,7)`, later used with the Empty Cup.
- **Milestone** — `(7,5)`.
- **Inn entrance** — `(15,9)`.

The Blue Seal and Moonmint can be collected before the player knows their purpose.

That is intentional: later recognition gives the opening room additional meaning.

---

# 4. The Inn and the Initial Mystery

## Room 01 — Common Room

The common room is the game's central hub.

On entering, the player meets **Master Vey**, the innkeeper. Vey welcomes the player but quickly frames the strange situation at the inn through a **false accusation / misleading interpretation**.

The player's initial understanding of the mystery should therefore come from Vey, while later evidence progressively undermines his version.

The room contains three important NPCs:

### Master Vey

**Location:** behind the bar at `(10,1)`

Vey is the apparent authority figure.

His dialogue changes as the player discovers tokens and opens the cellar. He supplies the initial interpretation of the inn's problem but is not a neutral source.

### Sir Caldus

**Location:** beside the hearth at `(4,5)`

Caldus is associated with the **silver-tower sigil**.

Something about his armor/equipment is stuck or seized. Applying **Lamp Oil** frees it.

In gratitude, Caldus gives the player the:

**Silver Shard**

This is both one of the three cellar tokens and later the object needed to repair the cracked moon mirror.

### Lady Orra

**Location:** dim corner at `(16,8)`

Orra is associated with the **pale-rose sigil**.

Her invitation cannot initially be read properly. Once the player reveals it using steam in the kitchen and returns it to her, she gives the player the:

**Moon Brooch**

This is one of the three objects required to open the cellar ward.

---

# 5. The Three Guest Tokens

The cellar entrance is protected by a ward with **three recesses**.

Opening it requires:

1. **Brass Key**
2. **Silver Shard**
3. **Moon Brooch**

Each comes from resolving a different NPC/evidence thread.

This creates three parallel branches rather than one long linear fetch quest.

---

## Branch A — Pell Thimble and the Blue Seal

### Room 03 — Guest Corridor

The guest corridor represents a larger inn without requiring several additional playable bedrooms.

Three guest doors display identifying symbols:

- Blue-seal door — `(2,2)`
- Silver-tower door — `(7,2)`
- Pale-rose door — `(12,2)`

The corridor also contains luggage, plaques, and mail to imply unseen guest rooms.

### Pell Thimble

**Location:** `(16,5)`, with a mailbag.

Pell is connected to the **Blue Seal** found outside in the storm.

Nearby, at `(17,1)`, is Pell's torn invitation. Its surviving text includes:

> “The bearer of the…”

The blue wax residue on the corresponding guest door matches Pell's seal.

Returning the **Blue Seal** to Pell earns the player the:

**Brass Key**

This establishes the idea that the strange tokens are not arbitrary keys: they represent the guests and their right to be present.

---

## Branch B — Sir Caldus

Caldus remains beside the common-room hearth.

The player obtains **Lamp Oil** from the kitchen shelves at `(3,8)`.

Using the oil on Caldus frees the seized mechanism/armor that has immobilized or inconvenienced him.

Caldus then gives the player his:

**Silver Shard**

The silver-tower plaque in the guest corridor confirms his association with the corresponding guest room.

---

## Branch C — Lady Orra

A **Blank Letter** / apparently unreadable invitation is connected with Lady Orra.

In the kitchen, the player can hold/use it at the **steaming kettle** `(10,2)`.

Steam reveals the hidden writing, transforming it into:

**Orra Invite**

Returning the revealed invitation to Lady Orra causes her to give the player her:

**Moon Brooch**

The pale-rose door plaque in the corridor independently confirms her identity.

---

# 6. Kitchen Puzzle Workshop

## Room 02 — Kitchen and Pantry

The kitchen is deliberately designed as a workshop room containing several visually distinct interaction stations.

### Objects

- **Lamp Oil** — `(3,8)`
- **Long Taper** — near `(12,8)`
- **Empty Cup** — near `(12,8)`
- **Steaming kettle** — `(10,2)`
- **Cauldron** — `(5,5)`
- **Cellar ward** — `(18,8)`
- Exit to common room — `(19,5)`

### Kettle

Using the Blank Letter at the steaming kettle reveals Lady Orra's invitation.

`Blank Letter -> Orra Invite`

### Long Taper

The taper can be taken to the common-room hearth.

Using it there creates a lit taper state needed in the cellar.

### Empty Cup

The cup can be taken back outside to the rain barrel.

Using it there creates:

`Empty Cup -> Rainwater`

### Cauldron

The cauldron eventually checks for:

- Moonmint
- Rainwater
- Hearth Ash

These ingredients produce the:

**Guest's Broth**

The player may discover some ingredients long before learning the recipe.

### Cellar Ward

The three-recess ward checks for:

- Brass Key
- Silver Shard
- Moon Brooch

When all three are present, the cellar stairs open and Room `04` becomes accessible.

---

# 7. The Broth Puzzle

The Guest's Broth is a second multi-room puzzle whose pieces overlap the main investigation.

## Ingredient 1 — Moonmint

Found outside at the Storm Crossroads `(13,7)`.

The player can collect it almost immediately.

## Ingredient 2 — Rainwater

Take the **Empty Cup** from the kitchen.

Return outside and use it at the rain barrel `(15,7)`.

Result:

**Rainwater**

## Ingredient 3 — Hearth Ash

The hearth is in the Common Room at `(2,9)`.

The hearth also serves as the place where the player lights the **Long Taper**.

At the appropriate stage, the player can collect:

**Hearth Ash**

## Cooking

Return to the kitchen cauldron `(5,5)`.

When all three ingredients are available, prepare:

**Guest's Broth**

Set:

`BROTH_READY`

The finished broth is ultimately served at the common-room dining table `(9,6)`.

---

# 8. The Moonlit Cellar

## Room 04 — Moonlit Cellar

The cellar is the reveal room.

It should feel darker, older, and more magical than the inn above.

The layout uses an **L-shaped opaque partition** so that the central supernatural object is not visible immediately upon entering.

### Important locations

- Fixed blue witchlight — `(6,2)`
- Cracked moon mirror — `(13,3)`
- Recipe stone — `(4,8)`
- Paired wall sconces — `(14,8)`
- Stairs back to kitchen — `(19,8)`

### Entry

The player arrives around `(18,8)`.

The partition blocks direct line of sight to the moon mirror.

This makes the player physically explore the cellar rather than seeing the reveal immediately.

### Lighting the sconces

The **Lit Taper** can be used at `(14,8)`.

Both wall sconces ignite, increasing visibility in the cellar.

This is primarily an environmental/visibility puzzle rather than an inventory-combination puzzle.

### The cracked moon mirror

Behind the partition is the cracked moon mirror.

The **Silver Shard** from Sir Caldus fits the damaged mirror.

Using it repairs the mirror.

Mara then appears.

---

# 9. Mara and the True Law

**Mara** is the supernatural figure hidden behind the cellar mystery.

She appears only after the moon mirror has been repaired.

Her dialogue provides the information that recontextualizes Vey's accusation and explains the **true law of Moonfen Inn**.

She also explains or confirms the recipe for the Guest's Broth.

After Mara appears, the **Recipe Stone** at `(4,8)` becomes readable/useful, providing a persistent environmental version of the recipe clue.

This avoids making one NPC conversation the sole source of essential puzzle information.

Mara's revelation sets the story toward:

`HOST_REVEALED`

The important dramatic turn is that the player now understands that the identity and obligations of **host and guest** are not what Vey initially claimed.

---

# 10. Finale

The player returns to the Common Room.

The final sequence uses two locations:

### Dining table — `(9,6)`

Once `BROTH_READY` is set, the player serves the Guest's Broth.

This fulfills the hospitality element of the inn's true law.

### Service bell — `(18,5)`

The bell becomes the final interaction once:

`HOST_REVEALED`

has been established.

Ringing/using the bell triggers the confrontation and resolution.

The climax should pay off the recurring ideas of:

- invitation;
- identity;
- guest tokens;
- hospitality;
- the distinction between host and guest;
- Vey's misleading version of the inn's rules.

No combat is required. The player wins by **understanding and correctly enacting the rules of the place**.

---

# 11. Puzzle Tree

```text
START
 |
 v
Storm Crossroads
 |
 +--> Examine milestone during storm
 |      |
 |      +--> INN_REVEALED
 |              |
 |              v
 |          Enter Moonfen Inn
 |
 +--> Take Blue Seal ------------------------------+
 |                                                 |
 +--> Take Moonmint ------------------------+       |
                                           |       |
                                           |       v
COMMON ROOM                               |   GUEST CORRIDOR
 |                                         |       |
 +--> Meet Master Vey                     |       +--> Meet Pell
 |                                         |              |
 +--> Meet Sir Caldus                     |       Blue Seal returned
 |      |                                  |              |
 |      |                                  |              v
 |      |                                  |         Brass Key
 |      |                                  |
 |      +<-- Lamp Oil from Kitchen         |
 |              |                          |
 |              v                          |
 |         Silver Shard                    |
 |                                         |
 +--> Meet Lady Orra                       |
        ^                                  |
        |                                  |
        +-- Revealed invitation            |
              ^                            |
              |                            |
KITCHEN       |                            |
 |            |                            |
 +--> Steam Blank Letter ------------------+
 |
 +--> Take Lamp Oil --> Caldus --> Silver Shard
 |
 +--> Take Long Taper --> Hearth --> Lit Taper
 |
 +--> Take Empty Cup
 |       |
 |       v
 |   Rain barrel outside --> Rainwater
 |
 +-----------------------------------------------+
                                                 |
CELLAR WARD                                      |
requires:                                        |
  Brass Key <------------------------------------+
  Silver Shard
  Moon Brooch
       |
       v
MOONLIT CELLAR
 |
 +--> Lit Taper --> light sconces
 |
 +--> Silver Shard --> repair moon mirror
 |                         |
 |                         v
 |                       Mara
 |                         |
 |                         +--> True law
 |                         +--> Broth recipe
 |                         +--> Recipe stone readable
 |
 +------------------------------------+
                                      |
BROTH PUZZLE                          |
                                      |
Moonmint -----------------------------+
Rainwater ----------------------------+--> Cauldron --> Guest's Broth
Hearth Ash ---------------------------+                    |
                                                           v
COMMON ROOM                                               |
 |                                                        |
 +<-------------------------------------------------------+
 |
 +--> Serve broth at dining table
 |
 +--> HOST_REVEALED
 |
 +--> Ring service bell
 |
 v
FINALE
```

---

# 12. NPC Roster

| NPC | Location | Puzzle role |
|---|---|---|
| **Master Vey** | Common Room `(10,1)` | Innkeeper; supplies misleading initial interpretation; finale |
| **Sir Caldus** | Common Room `(4,5)` | Oil puzzle; gives Silver Shard |
| **Lady Orra** | Common Room `(16,8)` | Invitation puzzle; gives Moon Brooch |
| **Pell Thimble** | Guest Corridor `(16,5)` | Blue Seal puzzle; gives Brass Key |
| **Mara** | Moonlit Cellar, near mirror | Reveals true law and broth recipe |

---

# 13. Inventory / Puzzle Objects

Likely inventory objects include:

| Object | Source | Use |
|---|---|---|
| Blue Seal | Storm Crossroads | Return to Pell |
| Moonmint | Storm Crossroads | Broth ingredient |
| Blank Letter | Story/inn | Reveal over kettle |
| Orra Invite | Kettle result | Return to Lady Orra |
| Lamp Oil | Kitchen | Free Caldus |
| Silver Shard | Caldus | Cellar ward; repair mirror |
| Moon Brooch | Lady Orra | Cellar ward |
| Brass Key | Pell | Cellar ward |
| Long Taper | Kitchen | Light at hearth; ignite cellar sconces |
| Empty Cup | Kitchen | Collect rainwater |
| Rainwater | Rain barrel | Broth ingredient |
| Hearth Ash | Common-room hearth | Broth ingredient |
| Guest's Broth | Kitchen cauldron | Serve in common room |

Because the engine has no generic item-combination system, these transformations should be implemented as **coordinate-driven Use interactions and story flags**, rather than assuming arbitrary inventory-on-inventory combinations.

---

# 14. Story Flag Ledger

A compact implementation could use flags similar to:

| Flag | Meaning |
|---|---|
| `INN_REVEALED` | Milestone has revealed the inn |
| `PELL_HELPED` | Blue Seal returned; Brass Key obtained |
| `CALDUS_HELPED` | Caldus freed; Silver Shard obtained |
| `ORRA_HELPED` | Invitation returned; Moon Brooch obtained |
| `CELLAR_OPEN` | Three-token ward solved |
| `SCONCES_LIT` | Cellar sconces lit |
| `MIRROR_REPAIRED` | Silver Shard placed in moon mirror |
| `MARA_REVEALED` | Mara has appeared |
| `RECIPE_KNOWN` | Player has learned broth recipe |
| `BROTH_READY` | Guest's Broth prepared |
| `BROTH_SERVED` | Broth served at common-room table |
| `HOST_REVEALED` | True host/guest relationship understood/revealed |
| `FINALE_DONE` | Service-bell finale completed |

Several boolean states can be packed into bytes if necessary.

---

# 15. Room Blockouts

The following are logical first-pass 20×11 maps, not final pixel art.

## Room 00 — Storm Crossroads

```text
~~~~~gggggrrrgggg~~~
~~~~ggggggrrrggggg~~
~~~gggggggrrrgggggg~
~~ggggTgggrrrgggggg~
~ggggTTTggrrrgggggg~
rrrrrrrrrrrrrrrrrrrr
rrrrrrrrrrrrrrrrrrrr
~gggggggggrrrgMMggg~
~ggggIIIIgrrrgBBggg~
~~gggIIIIgrrrgDDgg~~
~~~ggIIIIgrrrgggg~~~
```

Important coordinates:

- `(7,5)` milestone
- `(14,6)` Blue Seal
- `(13,7)` Moonmint
- `(15,7)` rain barrel
- `(15,9)` inn door

---

## Room 01 — Common Room

```text
WWWWWWWWWWWWWWWWWWWW
WbbbWWffffWWbbbbbbbDW
WbbbWWffffWWbbbbbbbDW
WbbbWWffffWWbbbbbbbDW
Wbbbbbbbbbbbbbbbbbbbb
WbbbbbbbbTTTbbbbbbBBW
DbbbbbbbTTTbbbbbbbBBW
DbbbbbbbTTTbbbbbbbBBW
Wbbbbbbbbbbbbbbbbbbbb
WbbbbHHHbbbbbbbbbbBBW
WWWWWWWWWWWWWWWWWWWW
```

Important coordinates:

- `(10,1)` Master Vey
- `(4,5)` Sir Caldus
- `(16,8)` Lady Orra
- `(9,6)` dining table
- `(2,9)` hearth
- `(18,5)` service bell
- `(0,6)` front door
- `(19,2)` guest corridor
- `(19,7)` kitchen

---

## Room 02 — Kitchen and Pantry

```text
WWWWWWWWWWWWWWWWWWWW
WSSSbbbbWWKKKbbbPPPPW
WSSSbbbbWWKKKbbbPPPPW
WSSSbbbbWWKKKbbbPPPPW
WbbbbbbbbbbbbbbbbbbbW
WbbbbCCbbbbbbbbbbbbbD
WbbbbCCbbbbbbbbbbbbbD
WbbbbbbbbbbbbbbbbbbbW
WOOOOObbbbbLLLLbbbDDW
WOOOOObbbbbLLLLbbbDDW
WWWWWWWWWWWWWWWWWWWW
```

Important coordinates:

- `(10,2)` steaming kettle
- `(5,5)` cauldron
- `(3,8)` Lamp Oil
- `(12,8)` Long Taper / Empty Cup
- `(18,8)` cellar ward
- `(19,5)` common-room exit

---

## Room 03 — Guest Corridor

```text
WWWWWWWWWWWWWWWWWWWW
WDDWWWDDWWWDDWWWPPPPW
WDDWWWDDWWWDDWWWPPPPW
WbbbbbbbbbbbbbbbbbbbW
WbbbbbbbbbbbbbbbbbbbW
WbbbBBBbbbbSSSbbbbbbW
WbbbBBBbbbbSSSbbbbbbD
WbbbbbbbbbbbbbbbbbbbD
WbbbbbbbbbbbbbbbbbbbW
WbbbbbbbbbbbbbbbbbbbW
WWWWWWWWWWWWWWWWWWWW
```

Important coordinates:

- `(2,2)` Blue-seal guest door
- `(7,2)` Silver-tower guest door
- `(12,2)` Pale-rose guest door
- `(16,5)` Pell Thimble
- `(17,1)` torn invitation
- `(19,6)` common-room exit

---

## Room 04 — Moonlit Cellar

```text
WWWWWWWWWWWWWWWWWWWW
WbbbbbbbbWbbbbbbbbbbW
WbbbbbbbbWbbbMMMMMbbW
WbRRRbbbbWbbbMMMMMbbW
WbRRRbbbbWbbbMMMMMbbW
WbRRRbbbbWbbbbbbbbbbW
WbbbbbbbbWWWWWWbbbbW
WbbbbbbbbbbbbbbbbbbbW
WbbbUUbbbbbbbbVVVbbDW
WbbbUUbbbbbbbbVVVbbDW
WWWWWWWWWWWWWWWWWWWW
```

Important coordinates:

- `(13,3)` cracked moon mirror
- `(4,8)` recipe stone
- `(14,8)` wall sconces
- `(19,8)` stairs
- `(6,2)` fixed blue witchlight

---

# 16. Required Tile Artwork

The demo needs a relatively compact environmental tile set.

## Exterior

- grass / marsh grass;
- muddy road;
- deep or flooded water;
- tree / dense vegetation;
- inn exterior wall;
- inn door;
- milestone;
- moonmint patch;
- puddle / wet ground;
- rain barrel.

## Inn Interior

- wooden floor;
- interior wall;
- doorway/open door;
- bar/counter sections;
- shelves;
- table sections;
- hearth/fireplace;
- guest-room door;
- door plaques / sigil decoration;
- kitchen stone floor;
- stove/kettle area;
- pantry shelving;
- cauldron/hearth;
- cellar ward / recessed door;
- cellar stone wall;
- cellar partition;
- recipe stone;
- moon mirror;
- wall sconce.

Furniture should generally be represented as **fixed scenery tiles** where possible so it cannot accidentally be taken by the generic object system.

---

# 17. Required Object / Character Artwork

Likely object graphics:

- player character;
- Master Vey;
- Sir Caldus — visor closed;
- Sir Caldus — visor open / alternate portrait or state;
- Lady Orra;
- Pell Thimble;
- Mara;
- Blue Seal;
- Moonmint;
- Blank Letter / revealed invitation;
- Lamp Oil;
- Silver Shard;
- Moon Brooch;
- Brass Key;
- Long Taper;
- Lit Taper;
- Empty Cup;
- Rainwater cup;
- Hearth Ash;
- Guest's Broth;
- witchlight;
- lit sconce/light actor;
- cracked and repaired moon mirror states.

Sir Caldus's portrait direction established during development is a **48×42 pixel black-and-white, low-key portrait**, with the face/visor treatment suitable for both closed- and open-visor variants.

---

# 18. Implementation Notes for the C64 Engine

The design deliberately fits the engine's strengths:

- exactly five fixed **20×11** rooms;
- no combat or NPC AI required;
- puzzle state handled through global story flags;
- interactions tied to known coordinates;
- room revisits provide visible progression;
- most furniture is scenery rather than takeable objects;
- essential information is repeated environmentally rather than relying on long dialogue;
- the guest corridor implies multiple bedrooms without requiring more rooms.

The engine currently lacks generic:

- item-on-item combination;
- dropping/equipping;
- inventory-target-map interactions;
- dialogue choice trees.

Accordingly, puzzle actions should be implemented as contextual coordinate-based `Use` operations that check inventory and story flags.

For example:

```text
Use cauldron at (5,5)
    if Moonmint + Rainwater + Hearth Ash:
        remove ingredients
        add Guest's Broth
        set BROTH_READY
    else:
        display missing/incorrect ingredient hint
```

Likewise, the cellar ward should directly check for the three required tokens.

---

# 19. Pacing

A good intended progression is approximately:

**Opening discovery → meet suspicious household → explore freely → solve 2–3 guest problems in parallel → open cellar → supernatural revelation → complete hospitality ritual → confront the apparent host.**

The player should rarely have only one available action.

Early in the game, the player can simultaneously investigate:

- Pell and the Blue Seal;
- Caldus and the Lamp Oil;
- Orra and the hidden invitation;
- the strange cellar ward;
- the Moonmint and rain barrel;
- the hearth and kitchen equipment.

This gives a five-room game the feel of a larger adventure.

---

# 20. Core Design Principle

Moonfen Inn should reward **noticing relationships**.

The player repeatedly encounters an object before understanding it:

- the Blue Seal before meeting Pell;
- Moonmint before learning the recipe;
- the three door sigils before understanding the guest tokens;
- the Silver Shard first as a token, then as part of the moon mirror;
- the hearth first as scenery, then as a source of fire and ash;
- the innkeeper first as an authority, then as an unreliable interpreter of the inn's law.

The final puzzle is therefore not really about collecting three keys or brewing a potion.

It is about reconstructing **who belongs at Moonfen Inn, what a guest is owed, and who is actually responsible for providing it.**
