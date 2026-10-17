# Ruleset 2: budgets, lanes, HP, counterplay (2026-10-17)

Founder real-time, after playing the web client: *"it should matter where you put stuff and as far as I can tell it doesn't ... you don't have enough components for it to matter! why is there a limit to the amount of components? all components need to have a cost and a weight so the weight can only be so much and the cost can only be so much and the position of all of the components needs to be significant and all of it needs to read to the player without you having to explain it really ... I created a strategy, 4 lasers and the rest bulwarks, it wins every time ... there needs to be counter play ... dig into the source conversation for DEADWEIGHT 1, that original design document ... there were a lot more components ... cutting doesn't make any sense."*

Everything below is in `core/` (`items.[ch]`, `combat.[ch]`, `protocol.[ch]`), is what `dw2_server` runs, and is what the browser client (`PARENA/stdlib/d2/web/`) predicts and animates. `tests/test_core_loop.c` pins it with about 1.75 million exact checks.

## What changed, and why

| Before (ruleset 1) | Now | Why |
|---|---|---|
| at most 16 parts | **no limit on parts** except the 36 cells; every part has a **cost** and a **weight**; a ship may carry **1000 credits** and **40 tons** | the limit was a rule the player could not see a reason for; a budget is one they can feel, and it is what makes position the only free variable |
| one scalar of armour; damage was a number that dropped | **every cell has HP**; shots hit a *part*; a part at 0 HP is destroyed (it no longer conducts power, fires, heals or blocks) | "I go from full health to dead and I don't know why": now the player can watch which part broke and what it took with it |
| enemy fire hit the ship in general | shots fly down **lanes**: the enemy's column of the same number, from the bow (row 0) to the stern | where a part sits decides what shields it, what it shields and what hits it first |
| Panic Cut (fold a part over a second copy) | **SPLIT in place**: a Titanium Beam becomes two Titanium Shards, an Aether Ore four Ore Chips, on exactly the cells it held | the cut was unreadable; a split is two ordinary parts you can see and place |
| 7 parts, one weapon | **24 parts**, three weapon families, six materials | "there were a lot more components" in the founding transcript (Quantum Cores, Cryo-Vents, Xeno-Biomass, Titanium, Aether) |
| one best layout | no dominant layout: a counter exists for each strategy (see the matrix below) | "4 lasers and the rest bulwarks wins every time" |

## The parts (cost credits / weight tons / HP per cell)

| id | part | cost | tons | HP | what it is |
|---|---|---|---|---|---|
| 0 | Generator | 100 | 4 | 20 | 10 W (5 V x 2 A) out of its east side |
| 1 | Conductor | 10 | 1 | 10 | a wire |
| 2 | Splitter Node | 25 | 1 | 10 | one in, two out |
| 3 | Railgun | 240 | 9 | 30 | 30 damage every 30 charge; fires **through** its own ship and **pierces** |
| 4 | Bulwark Plate | 55 | 5 | 45 | the all-round wall; lasers do x0.5 |
| 5 | Titanium Beam | 90 | 8 | 20 x4 | 1x4 bar; slugs and shells x0.5; every gun touching it hits **x1.5** |
| 6 | Aether Ore | 100 | 6 | 15 x4 | 2x2 crystal; lasers x0.2, shells x1.5 (it shatters); an Aegis touching it works x1.5 |
| 7 | Pulse Laser | 120 | 3 | 35 | 7 damage every 10 charge (every second on 10 W); **needs a clear lane**; cannot be dodged |
| 8 | Siege Cannon | 230 | 10 | 30 x2 | 45 damage every 30 charge; lands on the **deepest** part of the lane; 50% splash to what touches it |
| 9 | Amplifier | 90 | 2 | 10 | x1.5 power through it |
| 10 | Mender | 130 | 3 | 15 | 2 hull per second per 10 W |
| 11 | Aegis Emitter | 140 | 3 | 15 | 4 HP per second per 10 W to the weakest part in its 3x3 |
| 12-14 | Resistor 1R / 2R / 4R | 20 | 1 | 10 | passes 0.75 / 0.5 / 0.25 of the power |
| 15 | Capacitor | 70 | 2 | 15 | banks power, releases it in a burst |
| 16 | Battery | 110 | 4 | 20 | starts half full, gives up to 15 W a second |
| 17 | Transistor | 80 | 1 | 10 | conducts only while its gate is fed |
| 18 | Delayer | 60 | 1 | 10 | passes power one second late |
| 19 | Quantum Core | 320 | 10 | 25 x4 | 2x2 reactor, **40 W**; every neighbouring cell takes **2 heat damage a second** unless cooled |
| 20 | Cryo-Vent | 60 | 3 | 20 x2 | cools every cell it touches (and itself) |
| 21 | Xeno-Biomass | 90 | 4 | 15 x4 | grows back **+2 HP a second** on every touching cell of another part (+1 on its own); needs no power |
| 22 | Titanium Shard | 55 | 5 | 20 x2 | half a Beam; boosts touching guns only x1.25 |
| 23 | Ore Chip | 30 | 2 | 15 | a quarter of an Ore; an Aegis touching it works x1.25 |

A split costs: two Shards are 110 credits and 10 tons against a Beam's 90 and 8; four Chips are 120 credits and 8 tons against an Ore's 100 and 6. The tax is real and the pieces boost less, but they fit round other parts, and a hit does not have to go through all of a Beam to break it.

## A second of fight (`dw2_match_tick`)

1. **Power**: generators put out watts, wires carry them, splitters divide, amplifiers and resistors scale, capacitors / batteries / delayers bank and release, transistors gate. A weapon charges on what reaches it. A closed loop of wire grows Back-EMF and burns out. (The circuit is ruleset 1's, unchanged.) A weapon at its threshold fires, at most three shots a second.
2. **Fire**: every ship's shots are decided first, then they land (a gun destroyed this second still fires this second; the order they land in alternates by tick parity so neither side always shoots first).
   - **Pulse Laser**: hits the first part in the enemy lane. Holds fire (and keeps its charge, up to two shots' worth) if anything of yours is alive in front of it.
   - **Railgun**: hits the first part; what it did not spend carries on (x0.8) into the next part, and on to the hull.
   - **Siege Cannon**: the shell comes down on the **deepest** part in the lane, over any wall; the parts touching it take half.
   - Damage to a part = the weapon's damage x the part's multiplier x any boost from a Titanium neighbour. A shot that finds nothing left hits the hull for **half** (`DW2_HULL_FACTOR`). A Brace shield soaks damage before anything else.
   - Slugs and shells can be **dodged**: chance = (40 - the ship's tons) x 1.5%, at most 50%. Weight is also what you pay for armour, so the lightest ship is the hardest to hit.
3. **Upkeep**: a Quantum Core's heat burns every uncooled neighbour; Biomass and Aegis repair; a Mender mends hull.

A match ends when a hull reaches 0; at 30 seconds the higher hull wins, then the larger share of the ship's structure still standing, then the cheaper ship.

Round breaks (Surge Timing, Overcharge / Brace) are unchanged: `docs/COMBAT_REDESIGN.md`. Brace now adds to a shield pool.

## The wire

- `OPP_SHIP` (0x90): 36 x `{item, row, col, rot}` (the cut flag is gone); `PANIC_CUT` is always rejected.
- `EVENTS` (0x92), after every `TICK`: `[tick][n][dropped][structure_you u16][structure_opp u16]` then `n` x `[type side a b c d e]` (n at most 60, sides already turned so 0 is the recipient). Types: FIRE, HIT, HULL, EVADE, SHIELD, HEAL, REPAIR, HEAT, BURN (`core/combat.h`).
- `PLACE_REJECT` reason 3 = over budget (credits or tons).
- `MATCH_FOUND` carries the packing time as a u32 of ms (30 bytes); the 120 s clock does not fit a u16.

A client does not have to trust itself about damage: it runs only the circuit locally (the web client's port is checked against the C core tick by tick, `PARENA/scripts/d2_rules_parity.sh`) and takes every hit, repair and destruction from `EVENTS`.

## Balance: how it was checked, and what it says

The search (`tools/dw2_loadout_search.c`) co-evolves ships against the real combat core in **species** (laser / rail / cannon) so that one family cannot crowd the others out; `--archetypes` plays the hand-drawn human strategies against each other; `tools/dw2_trace.c` prints a fight tick by tick for two ASCII-drawn ships (`dw2_trace A.txt B.txt`, `--ops` to price one). The bot pool is those human strategies plus the best evolved ship of each family, so a person meets the same kinds of ship they might build.

Row's result against column, full 30-tick fights, deterministic, round breaks *not* played (that is the human's skill):

| | Laser x4 | Laser x3 | Ti wall + rails | Aether wall + rails | Cannon battery | Cannons + rail | Plated cannons |
|---|---|---|---|---|---|---|---|
| **Laser line x4** (990 cr) | | win | win | **loss** | **loss** | win | **loss** |
| **Titanium wall + rails** (825 cr) | loss | loss | | win | win | loss | loss |
| **Aether wall + rails** (880 cr) | **win** | win | loss | | loss | loss | loss |
| **Cannon battery** (770 cr) | **win** | loss | loss | **win** | | loss | win |
| **Cannons + rail** (780 cr) | **loss** | win | win | win | win | | win |
| **Plated cannons** (770 cr) | **win** | loss | win | **win** | loss | loss | |

(Laser x3, Ti wall, Aether wall etc. are the pool's strategies 1, 2, 4, 7, 8, 9; the full 11-strategy matrix is `dw2_loadout_search --archetypes`.)

What it says, honestly: the founder's "4 lasers and the rest Bulwarks" now **loses** to an Aether wall (lasers do 0.2 to Ore), to a cannon battery and to plated cannons (a shell lands on the *deepest* part of a lane, and in a laser line that is often a Generator, so the line loses power while its Bulwarks are untouched). It still beats the Titanium wall (Titanium turns slugs and shells, not beams) and the three-laser line. The loop is not closed neatly: *Cannons + a Railgun* beats nine of the ten drawn strategies and loses only to the Laser line, so it is the strongest generalist and the first thing to try to break. Nothing about Mender / Aegis ships, Quantum Core builds or Biomass is in the drawn set (the evolved ships in the bot pool do use them), and those are where we expect the next counters. Real human play is the missing evidence (the search ignores the round-break duel, which is the player's skill), so read the matrix as "no strategy is unanswerable", not as "balanced".
