# Pell Thimble Puzzle Design

## Moonfen Inn --- Puzzle Branch

### Purpose

Pell Thimble's branch is an **identity and evidence puzzle**, not a
simple fetch quest. The player may find Pell's missing Blue Seal before
meeting him, but Pell will not accept it until the player has
established that it actually belongs to him.

The branch teaches the player that environmental details can be
evidence, guest-room symbols matter, and Vey's interpretation of events
should not automatically be trusted. Completing it rewards the player
with the **Brass Key**, one of the three objects required to open the
cellar ward.

------------------------------------------------------------------------

## Puzzle Tree

``` text
BLUE SEAL found outside
        |
        v
PLAYER meets Pell
        |
        +--> Give Blue Seal immediately
        |       |
        |       +--> Pell refuses:
        |            "Could be anyone's. Vey won't accept
        |             a seal without the invitation."
        |
        v
Inspect Pell's torn invitation
        |
        +--> readable fragment:
        |    "The bearer of the blue..."
        |
        v
Inspect blue-seal guest-room door
        |
        +--> observe matching blue wax residue / symbol
        |
        v
Talk to Pell again
        |
        +--> Player establishes connection:
        |      Pell + invitation + blue seal
        |
        v
Return Blue Seal
        |
        +--> Pell accepts it
        +--> gives Brass Key
        +--> PELL_HELPED
        |
        v
Follow-up dialogue reveals information
about Vey and the inn's guest rules
```

The important feature is that **Pell initially refuses the Blue Seal**.
The player must establish the relationship between Pell, the torn
invitation, the blue-marked guest room, and the loose seal found
outside.

------------------------------------------------------------------------

# 1. Finding the Blue Seal

The **Blue Seal** is found outside at the Storm Crossroads, lying in a
puddle near the inn.

``` text
Room 00 — Storm Crossroads
Blue Seal: (14,6)
```

The player can collect it before knowing what it is for.

### Inventory description

> A round disk of blue wax impressed with a fine silver thread.

The description should be distinctive enough that the player can later
recognize the same symbol elsewhere.

------------------------------------------------------------------------

# 2. Meeting Pell

## Location

``` text
Room 03 — Guest Corridor
Pell Thimble: (16,5)
```

Pell stands near his mailbag. He is a courier who arrived at Moonfen Inn
with an invitation, but the invitation was damaged in the storm and its
wax seal was lost.

## Initial dialogue

> **Pell:** "Thimble. Pell Thimble. Courier, ordinarily. Prisoner of
> hospitality, presently."

### Ask about the inn

> **Pell:** "Vey says none of us leave until the matter of the guests is
> settled. I'd be happier if I understood what that meant."

### Ask why he is in the corridor

> **Pell:** "My invitation was damaged in the storm. Without it, Vey
> claims he cannot prove which room was mine."

### Ask about the invitation

> **Pell:** "Half the writing is gone. And the seal came off somewhere
> outside."

This establishes that Pell is missing a seal, but does **not**
explicitly tell the player that the Blue Seal belongs to him.

------------------------------------------------------------------------

# 3. Pell's Torn Invitation

Suggested location:

``` text
Room 03 — Guest Corridor
Torn invitation: (17,1)
```

The invitation remains fixed scenery rather than becoming another
inventory object.

## Examining it before speaking to Pell

> A torn invitation. Most of the writing has run into blue-grey smears.
>
> Only one line survives:
>
> **"The bearer of the blue..."**

## Examining it after speaking to Pell

> Pell's damaged invitation. The lower corner has been torn away where a
> wax seal might once have been attached.
>
> Only one line remains legible:
>
> **"The bearer of the blue..."**

Set:

``` text
PELL_INVITE_SEEN
```

The clue establishes the importance of the color blue without completely
solving the puzzle.

------------------------------------------------------------------------

# 4. The Blue-Seal Guest Door

Suggested location:

``` text
Room 03 — Guest Corridor
Blue-seal guest door: (2,2)
```

## Initial examination

> A guest-room door. A small brass plaque bears an unfamiliar symbol.

A closer examination reveals:

> The plaque shows a **blue circle crossed by a silver thread**.
>
> There is a smear of blue sealing wax beside the latch.

Set:

``` text
BLUE_DOOR_SEEN
```

The loose Blue Seal, the plaque, and the wax residue share the same
visual identity. Together with the invitation fragment, this lets the
player infer that the seal identifies the bearer assigned to this room.

------------------------------------------------------------------------

# 5. Trying the Seal Too Early

The player can use the Blue Seal on Pell immediately, but Pell refuses
it until enough evidence has been found.

## Early-use dialogue

> **Pell:** "Blue wax, certainly. But that proves very little."
>
> He turns it over in his fingers, then hands it back.
>
> **Pell:** "Vey wants proof. And for once, I think he has a point."

The player keeps the Blue Seal.

This prevents brute-force inventory use while also hinting that another
clue exists.

------------------------------------------------------------------------

# 6. Acceptance Condition

Pell accepts the seal when all three conditions are satisfied:

``` text
HAS_BLUE_SEAL
AND PELL_INVITE_SEEN
AND BLUE_DOOR_SEEN
```

The intended reasoning is:

``` text
Pell's invitation:
"The bearer of the blue..."
          +
Blue guest-room door:
matching symbol + blue wax
          +
Blue Seal:
same wax + same symbol
          |
          v
The seal identifies Pell as the bearer
assigned to the blue-marked room.
```

No dialogue-choice interface is required. Using the Blue Seal on Pell
after discovering the evidence represents the player's deduction.

------------------------------------------------------------------------

# 7. Puzzle Resolution

Once the acceptance conditions are met, using the Blue Seal on Pell
resolves the puzzle.

> Pell compares the seal with the torn invitation, then glances toward
> the blue-marked door.
>
> **Pell:** "There. Same mark. Same wax. I knew I hadn't imagined the
> whole cursed thing."
>
> He presses the seal against the surviving corner of the invitation.
>
> **Pell:** "That ought to satisfy even Vey."

Pell searches his mailbag.

> **Pell:** "I found this beneath the door when I arrived. Thought it
> was a room key, but it fits nothing upstairs."
>
> He gives you a **Brass Key**.

Inventory changes:

``` text
remove BLUE_SEAL
add BRASS_KEY
set PELL_HELPED
```

The Blue Seal leaves the player's inventory because Pell now possesses
it.

------------------------------------------------------------------------

# 8. The Brass Key

The **Brass Key** is Pell's reward and one of the three tokens needed to
open the cellar ward.

### Inventory description

> A heavy brass key with a broad circular bow. Three shallow grooves run
> along one edge.

Pell believes it might be a room key, but it fits nothing upstairs.
Later, the cellar ward contains a matching brass recess rather than a
conventional keyhole.

The three cellar tokens are:

``` text
Brass Key    — Pell Thimble
Silver Shard — Sir Caldus
Moon Brooch  — Lady Orra
```

------------------------------------------------------------------------

# 9. Pell's Story Payoff

After giving the player the Brass Key, Pell reveals a detail from his
invitation that creates the first clear crack in Master Vey's story.

> **Pell:** "Odd thing, though. My invitation never named Vey."
>
> **Pell:** "It said only that *the host of Moonfen would receive the
> bearer before moonset*."
>
> He looks toward the stairs.
>
> **Pell:** "I assumed Vey was the host. Perhaps I assumed too much."

This hints at the game's central mystery: everyone has accepted Vey's
role as host without proving it.

This clue can simply be associated with `PELL_HELPED`. A separate
`PELL_HOST_CLUE` flag is only necessary if later dialogue tests whether
the player heard it.

------------------------------------------------------------------------

# 10. Story Flags

Required:

``` text
PELL_INVITE_SEEN
BLUE_DOOR_SEEN
PELL_HELPED
```

Inventory state provides:

``` text
BLUE_SEAL
BRASS_KEY
```

Optional:

``` text
PELL_MET
PELL_HOST_CLUE
```

------------------------------------------------------------------------

# 11. Interaction Logic

The engine does not require generic item combination for this branch.
Actions can be implemented as coordinate- and NPC-specific `Use` or
`Examine` interactions.

## Torn invitation

``` c
inspect_torn_invitation()
{
    set_flag(PELL_INVITE_SEEN);

    say(
        "Only one line survives: "
        "\"The bearer of the blue...\""
    );
}
```

## Blue guest-room door

``` c
inspect_blue_door()
{
    set_flag(BLUE_DOOR_SEEN);

    say(
        "The plaque shows a blue circle crossed by "
        "a silver thread. Blue sealing wax clings "
        "beside the latch."
    );
}
```

## Using the Blue Seal on Pell

``` c
use_blue_seal_on_pell()
{
    if (flag(PELL_HELPED))
    {
        say("Pell already has his seal.");
        return;
    }

    if (!flag(PELL_INVITE_SEEN) ||
        !flag(BLUE_DOOR_SEEN))
    {
        pell_say(
            "Blue wax, certainly. "
            "But that proves very little."
        );

        pell_say(
            "Vey wants proof. "
            "And for once, I think he has a point."
        );

        return;
    }

    remove_item(BLUE_SEAL);
    give_item(BRASS_KEY);
    set_flag(PELL_HELPED);

    pell_say(
        "There. Same mark. Same wax. "
        "I knew I hadn't imagined the whole cursed thing."
    );

    pell_say(
        "I found this beneath the door when I arrived. "
        "Thought it was a room key, but it fits nothing upstairs."
    );
}
```

The function names are illustrative; the implementation should use the
engine's existing dialogue, inventory, and flag APIs.

------------------------------------------------------------------------

# 12. Hint Progression

The puzzle contains its own escalating hint system.

1.  **Pell:** "The seal came off somewhere outside."\
    Points back toward the opening room.

2.  **Early seal attempt:** "Blue wax, certainly. But that proves very
    little."\
    Tells the player the object alone is insufficient.

3.  **Invitation:** "The bearer of the blue..."\
    Connects Pell to the color blue.

4.  **Guest door:** matching blue wax and silver-thread symbol.\
    Supplies the final evidence.

At that point the intended deduction should be clear without explicitly
telling the player to give the seal to Pell.

------------------------------------------------------------------------

# 13. Intended Player Experience

``` text
"I found a strange blue seal outside."

        ↓

"Pell says he lost the seal from his invitation."

        ↓

"Obviously this might be his."
Player tries it.

        ↓

"He won't take it without proof."

        ↓

"The torn invitation says 'bearer of the blue.'"

        ↓

"That guest-room door has the same blue wax
and the same silver-thread symbol."

        ↓

"Now I can prove the seal belongs to Pell."

        ↓

"Pell gives me a strange key."

        ↓

"Wait — his invitation never said Vey was the host?"
```

The puzzle advances both the **mechanical puzzle tree** and the
**central narrative mystery**.

------------------------------------------------------------------------

# 14. Design Summary

Pell's branch contains:

-   **1 inventory object:** Blue Seal
-   **1 NPC:** Pell Thimble
-   **2 environmental clues:** torn invitation and blue-marked guest
    door
-   **2 evidence flags:** `PELL_INVITE_SEEN` and `BLUE_DOOR_SEEN`
-   **1 successful inventory interaction:** return the Blue Seal
-   **1 reward:** Brass Key
-   **1 narrative revelation:** Pell's invitation does not identify Vey
    as the host

The branch establishes the style of reasoning the rest of *Moonfen Inn*
should reward: **objects matter because of their relationships and
context, not simply because the player can pick them up.**
