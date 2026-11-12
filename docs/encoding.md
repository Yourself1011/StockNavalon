# Encoding: states and commands as arrays

The spec for turning game states into network input, and network output back into commands. It's the contract between polyengine and stockNavalon's network ([`stockNavalon.md`](stockNavalon.md)). Version 4, in `src/stockNavalon/encoding.h`, with Python access through `polyengine.Encoding`. Planned changes are in [`notes/design.md`](../notes/design.md).

## 1. Configuration and versioning

```cpp
struct EncodingConfig {
    int mapSize = 11;       // N: every map is padded to N x N
    int players = 2;        // P: seats encoded, the player to move first
};
constexpr int ENCODING_VERSION = 6;
```

- **First target: 11×11 (Tiny), 1v1.** That's the smallest game that still has the whole game in it, so it's the fastest way to find out whether the architecture learns at all. Every number in this document is derived from the config; the concrete counts shown are for N = 11, P = 2.
- **Expanding later:**
  - **A larger N:** nothing changes but the padding. Every per-tile part of the layout is the same at any size. A fully convolutional trunk with pooled heads runs at any N, so a model trained at 11 can be fine-tuned at 14 or 16 rather than retrained from scratch.
  - **More players:** the planes and slots that exist once per player grow, so the network's first and last layers change shape. The trunk can still be carried over.
- **Versioning:** `ENCODING_VERSION` goes up whenever the meaning of any plane or slot changes. Saved training data and model checkpoints record the version and the config, so mismatches fail loudly instead of training on misread data.
  - Version 2: an opponent's tribe is hidden until met, and each opponent's starting score was added.
  - Version 3: climate planes, the tribes each opponent could be, and the climates seen.
  - Version 4: each opponent's opening score changes, and where each tribe they could be puts their capital (section 4.2).
  - Version 5: the map-wide `unexplored` belief head (section 8.2).
  - Version 6: the map type, at the end of the global vector. A network saved before it loads with zeros for the new inputs, so it plays exactly as before until it learns them (`load_weights`).

## 2. Conventions

- **Seen from the player to move.** The input is built from `makeView(game, player)` only, never from hidden state. A test checks this (section 6).
- **Relative players:** `rel 0` is the player to move, and `rel k` is seat `(player + k) % P`. So "mine" is always slot 0, and one network plays any seat.
- **Padding:** a map smaller than N sits in the top-left corner, with tile `(x, y)` at row `y`, column `x`. Padding has `on_map` = 0 and every other plane 0.
- **Unexplored tiles:** `explored` = 0 and every other per-tile plane 0, apart from `on_map`.
- **Scaling:** counts and amounts are divided by a fixed constant, so typical values fall roughly between 0 and 1. The constants are part of the spec, and changing one bumps the version.
- **Types:** planes and the global vector are `float32`; masks are `uint8`.

## 3. Spatial planes: `[C, N, N]`

C = 94 + 3P, which is **100** for P = 2.

### Board (50 planes)

| Planes | Count | Value |
| --- | --- | --- |
| `on_map` | 1 | 1 on real tiles, 0 on padding |
| `explored` | 1 | 1 if the player has explored the tile |
| terrain | 5 | one-hot: field, forest, mountain, shallow water, ocean |
| resource | 6 | one-hot: fruit, crop, game, metal, fish, starfish. Only resources the player can see (`TileView.resource`), so it's all zeros until the matching tech is researchable. |
| road | 1 | road or bridge |
| building | 22 | one-hot: village, ruin, lighthouse, farm, mine, lumber hut, windmill, sawmill, forge, market, port, temple, forest temple, mountain temple, water temple, then the 7 monuments. Cities have their own planes. |
| building level | 1 | Windmill, Sawmill or Forge level ÷ 8, or temple level ÷ 5. The building planes say which it is. |
| temple countdown | 1 | temples: turns until the next level ÷ 2; 0 at the top level |
| climate | 12 | one-hot by tribe: whose climate region the tile is in. It's absolute (by tribe), not relative to the player, since what it reveals is *which tribe* is in the game: in 1v1 a foreign climate is the opponent's. |

### Territory (P planes)

| Planes | Count | Value |
| --- | --- | --- |
| border owner | P | one-hot by relative player; all zeros for neutral tiles |

### Cities (P + 4 planes)

| Planes | Count | Value |
| --- | --- | --- |
| city owner | P | one-hot by relative player |
| city level | 1 | level ÷ 10 |
| population | 1 | population ÷ (level + 1): progress to the next level. Can be negative. |
| capital | 1 | 1 if it's some player's original capital |
| wall | 1 | 1 if it has a City Wall |

### Units (P + 37 planes)

| Planes | Count | Value |
| --- | --- | --- |
| unit owner | P | one-hot by relative player, for each unit the player can see |
| unit type | 18 | one-hot, all unit types including boats |
| passenger | 11 | for boats, one-hot over the land types it carries (Warrior through Giant); 0 otherwise |
| HP fraction | 1 | hp ÷ max HP |
| HP | 1 | hp ÷ the most HP any unit can have, veterans included (a Giant's 40, since Giants can't be promoted), so it stays in [0, 1] |
| veteran | 1 | |
| can move, can attack, can act | 3 | own units only; 0 for others |
| hidden | 1 | own units only: hidden from enemies |
| hidden enemy near | 1 | own units only: the eye warning |

### Threats (3 planes)

Computed from the view alone. Enemy reach assumes the opponent owns every movement tech: the worst case, since their techs are hidden.

| Planes | Count | Value |
| --- | --- | --- |
| enemy attackers | 1 | how many visible enemy units could attack this tile on their next turn (move, then attack), ÷ 4 |
| enemy damage | 1 | the most damage any one of them could deal there next turn: to the player's unit on the tile, or else to a full-HP Warrior without defence bonus. In HP, ÷ 15. |
| own attackers | 1 | how many of the player's units can attack this tile this turn, with the moves and attacks they have left, ÷ 4 |

## 4. Global vector: `[G]`

G = 58 + 6 + 15P + 45(P − 1), which is **139** for P = 2.

| Features | Count | Value |
| --- | --- | --- |
| turn | 1 | turn ÷ 50 |
| stars | 1 | the player's stars ÷ 50 |
| income | 1 | ÷ 30 |
| tribe | 12P | one-hot per relative player. All zeros for an opponent not met yet, since tribes are hidden until then. |
| possible tribes | 12(P − 1) | per opponent, 1 for each tribe they could still be: worked out exactly from their starting score, the climates seen and their opening score changes ([`tribeInference.md`](tribeInference.md)). A met opponent has just their own. |
| climates seen | 12 | 1 for each tribe whose climate the player has seen on an explored tile |
| alive | P | per relative player |
| met | P | per relative player (1 for the player themselves) |
| score | P | as the player sees them (own live, others published) ÷ 1000 |
| starting score | P − 1 | per opponent, their first published score ÷ 1000. Public from turn 0, and the main evidence about a tribe not met yet: starting scores differ by tribe (GAME.md section 3), so 730 means Vengir or Ai-Mo. |
| explained score | P − 1 | per opponent, the part of their published score that comes from things the player can see (section 4.1), ÷ 1000 |
| unexplained score | P − 1 | per opponent, (published score + the player's own visible effects on them since) − explained score: the score sitting in things the player can't see, ÷ 1000 |
| last change | P − 1 | per opponent, their latest published change, with the player's own effects on them since their previous publication taken out, ÷ 500. 0 before their first turn ends. |
| opening changes | 6(P − 1) | per opponent, their first 6 published changes, own effects taken out the same way, ÷ 500 (section 4.2) |
| opening seen | 6(P − 1) | per opponent, 1 for each of those changes that has happened, since a +0 change is evidence too |
| capital position | 12(P − 1) | per opponent, for each tribe they could still be, the tiles that tribe would have started with explored ÷ 25, 0 for a tribe ruled out (section 4.2) |
| techs | 25 | 1 per tech owned; Basic, which everyone owns, is left out |
| tasks done | 7 | |
| monuments built | 7 | |
| task progress | 3 | kills ÷ 10, peaceful turns ÷ 5, lighthouses found ÷ 4 |
| diplomacy | 5(P − 1) | per opponent: at peace, they asked for peace, we asked, our embassy there, their embassy here |
| reward pending | 1 | 1 if a city level-up reward must be chosen now |
| map type | 6 | one-hot: drylands, lakes, pangea, continents, archipelago, waterworld. Public from the start, and what the unexplored tiles are likely to be. Last, so older networks load with zero weights for it. |

### 4.1 Score features: exact numbers, not raw history

Reading a score change is exact arithmetic: breaking a number into a sum of known amounts. Networks are bad at that, especially on scaled-down floats. So the engine does the arithmetic, and the network gets exact summaries. The full score history isn't encoded. It stays in `PlayerView.scoreHistory` for the parts of stockNavalon that calculate score exactly: the score check in determinization ([`stockNavalon.md`](stockNavalon.md) section 4), and later a particle filter ([`notes/design.md`](../notes/design.md)). The two divide the work: the network says which explanations are likely, and the exact check makes samples add up.

**Explained score:** the score the player can account for from what they see of that opponent, computed with the engine's score formula:
- visible cities: level, population, parks
- visible units
- border tiles on explored ground
- visible temples, at their actual level (temple levels are public), and monuments

**Unexplained score:** the rest of their score. It's the score in hidden things: techs, hidden units, unseen cities and territory, and their exploration. Being one number, it needs no history window, because the published score already adds up the whole game so far.

Their published score is from the end of their last turn, but the explained score is from the view now. Anything the player has done to them since, like killing a unit or capturing a city, is gone from what's visible but still in the published score. Without a correction, killing their unit would make their unexplained score jump by that unit's value. So the player's own visible effects on them since that publication are applied to the published score first: `unexplained = (published + own effects) − explained`. Own effects are negative: units of theirs seen dying, cities captured.

**Last change:** a "how active were they last turn" signal. A +300 spike just now means something different from a steady build-up. The player's own effects (units of theirs seen dying, cities captured) are taken out, counting only what the player could see, so nothing hidden leaks in.

All three use the published score, never the live one. Opponents can't change anything during the player's turn, so with the own-effects correction the split is exact, apart from one thing. Effects the player couldn't see, like splash damage killing a hidden unit, stay in the unexplained score. That's deliberate: counting them would leak that a hidden unit was there.

Temple levels count at their real value, since they're public. A temple's score is calculated at its level as of the opponent's last turn end, to match their published score.

If the belief network later needs more history than this, a small sequence model over `scoreHistory` can be added then.

### 4.2 The opening

The exact side ([`tribeInference.md`](tribeInference.md)) only rules out what's impossible. Most of what an opening says is likelihood instead. A +25 on turn 0 fits a Rider or a Climbing tribe, and a +15 on turn 1 is more typical of some tribes than others. The belief head learns those patterns, inside the exact set, from the tribe target (section 8.2), but only if it sees the evidence. So the first 6 changes are encoded as they are, not only summarized.

**Capital position:** a tribe's starting score fixes how many tiles of its 5×5 start were on the map: 25 at least 2 tiles from every edge, 20 or 15 next to an edge, fewer near a corner. That depends on the tribe hypothesis, so it's given per possible tribe. It feeds the belief model's guess at where their capital is.

## 5. Commands: `[A]`

A = 218·N² + 26 + 6(P − 1), which is **26,410** for N = 11, P = 2.

Each legal command has exactly one index. Indices that don't match a legal command are masked out.

### Per-tile slots: index = `slot · N² + y · N + x`

The source tile is `(x, y)`: the unit's tile, the city's tile, or the tile being built on. Slot-major order means a convolutional head's `[218, N, N]` output flattens straight into the index space.

| Slots | Count | Command |
| --- | --- | --- |
| 0–168 | 169 | **target offset**: Move, Attack or Disembark, to `(x + dx, y + dy)` with `slot = (dy + 6) · 13 + (dx + 6)`, for `dx, dy` in −6…6. Six is the furthest anything reaches: a Knight's 3 moves along roads. The middle slot (no offset) is never legal. |
| 169–175 | 7 | Recover, Heal Others, Promote, Disband, Capture, Examine Ruins, Harvest |
| 176–178 | 3 | Upgrade to Scout, Rammer, Bomber |
| 179–205 | 27 | Build: farm, mine, lumber hut, windmill, sawmill, forge, market, port, temple, forest temple, mountain temple, water temple, the 7 monuments, harvest fruit, hunting, fishing, clear forest, burn forest, grow forest, road, bridge |
| 206 | 1 | Destroy |
| 207–215 | 9 | Train: warrior, rider, archer, defender, swordsman, mind bender, catapult, knight, cloak |
| 216–217 | 2 | City reward: the first or second option for the level being rewarded |

**Why one set of target slots can stand for three command types:** for a given unit and target tile, at most one of Move, Attack and Disembark is ever legal.
- An attack's target has a visible enemy unit (or, for Infiltrate, an enemy city), and a move's target looks empty to the player.
- A disembark lands on land from water, and a boat's move stays on water.

So decoding picks whichever of the three is legal. A test checks that the uniqueness holds across the fuzz games (section 6).

### Global slots: after the per-tile ones, starting at 218·N²

| Offset | Count | Command |
| --- | --- | --- |
| 0–24 | 25 | Research, one per tech (Basic left out) |
| 25 + 6(k − 1) … | 6 per opponent | for relative opponent `k`: propose peace, accept their request, decline it, break peace, establish an embassy, destroy their embassy |
| last | 1 | End Turn |

Resign and Stay never get a slot: Resign is never offered as a legal command, and Stay is never legal.

## 6. Interfaces

```cpp
// encoding.h
int planeCount(const EncodingConfig &);    // C
int globalCount(const EncodingConfig &);   // G
int actionCount(const EncodingConfig &);   // A

// Writes the player's input. Reads only makeView(game, player), plus the
// public score history.
void encodeState(const Game &game, int player, const EncodingConfig &,
                 float *planes, float *globals);

// -1 if the command has no slot (Resign, or a map larger than N)
int encodeCommand(const Game &game, const Command &command, const EncodingConfig &);
// the legal command at this index; the index must be legal
Command decodeCommand(const Game &game, int index, const EncodingConfig &);
// 1 for each legal command's index
void legalMask(const Game &game, const EncodingConfig &, std::uint8_t *mask);
void legalMask(const LegalMoves &legal, const Game &game, const EncodingConfig &,
               std::uint8_t *mask);
```

From Python, the batched forms fill NumPy arrays in C++, without copying:

```python
enc = pe.Encoding(map_size=11, players=2)
planes, globals_ = enc.encode_states(games, players)   # [B, C, N, N], [B, G] float32
masks = enc.legal_masks(games)                         # [B, A] uint8
index = enc.encode_command(game, command)
command = enc.decode_command(game, index)
```

### Implementation notes

- **Where the score effects come from:** the engine records what each player's actions visibly did to other players' scores (units seen dying, cities captured), per opponent. `PlayerView` exposes it as `effectsSincePublished` and `effectsInLastChange`.
- **Parks** are in `CityView`, assuming they're visible on a city as in the real game.
- **Two small approximations in the explained score:**
  - **Units** count for whoever owns them now, since the view doesn't say who first had a unit. A converted unit's score stays with its original owner, so it's miscounted.
  - **Temples** at level 5 count as level 5 at the opponent's last turn end too. The view can't tell exactly when they got there.

  Both are rare and small.
- **Speed** on a Tiny map: about 8 µs to encode a position, and about 8 µs for a legal mask built from scratch (mostly `legalCommands()`). In search, build the mask from the node's `LegalMoves` list with the second `legalMask` overload, which skips the regeneration.

## 7. Tests

In `tests/stockNavalon/encoding_test.cc` and `tests/python/test_engine.py`. They run over random games on generated Tiny maps of every type, and Small maps padded to 16.

1. **Commands round-trip:**
   - every legal command encodes to an index, and decodes back to itself
   - no two legal commands share an index
   - the mask has exactly as many ones as there are legal commands
2. **No leaks:** two games that differ only in what the player can't see (fog contents including climate, hidden units, the opponent's stars, techs, kills, exploration, and their tribe if not met) encode to identical input. Same idea as the determinization leak test. It was checked by planting a leak (one global reading the opponent's real stars), which it caught.
3. **Feature checks:** in hand-built positions, specific features land where they should. For example: a Rider of the opponent's at (3, 4) sets unit owner `rel 1` and unit type `RIDER` at row 4, column 3, and a threat value on the tiles it can reach.
4. **Relative players:** the same position encoded for player 0, and for player 1 with the seats swapped, gives the same input.
5. **Padding:** a 11×11 map at N = 16 matches the N = 11 encoding in its top-left corner, and is zero elsewhere apart from `on_map`.

## 8. Outputs: value and beliefs

What the network's other heads output, and the training targets the engine builds for them from the true game. Like the input, both sides depend on the layout: C++ writes the targets (`encodeTargets`), Python trains on them, and C++ reads the network's outputs back for `determinize()` (`decodeBeliefs`). The policy is section 5.

### 8.1 Value

| Head | Output | Target |
| --- | --- | --- |
| value | 1, tanh | +1 if the player to move wins, −1 if they lose |

- **Relative to the player to move**, like the input.
- **Known only at the end:** the result isn't part of a position, so `encodeTargets` doesn't write it. Whoever records a game adds it to every position once the game is over.
- **No draws.** In Might, the winner is the player holding every capital, and the game can't end any other way.
- **The turn limit** is a self-play setting, not a rule: early games between weak players can go on forever. A game that hits it has no winner; it's truncated, not drawn, and its positions learn from each player's final search value ([`stockNavalon.md`](stockNavalon.md) section 6).
- **The result, not the score.** The loser keeps their score (techs, other cities, exploration), so score isn't the result.

### 8.2 Beliefs

Per opponent, so `P − 1` of each in general; one in 1v1. A belief head's target is about the true game, but it's only trained where the player doesn't already know the answer (the mask). Counts come as exact integers up to a cap, or as a distribution with no bins (section 8.3).

| Head | Output | Target | Trained when |
| --- | --- | --- | --- |
| tribe | softmax over 12 | their tribe | not met; tribes ruled out ([`tribeInference.md`](tribeInference.md)) are masked to −∞ before the softmax |
| techs | sigmoid per tech, 25 (Basic left out) | each tech owned | always |
| stars | mixture of 3 negative binomials: a weight, mean and spread each (9 numbers) | their stars | always |
| hidden unit here | `[N, N]`, sigmoid | an enemy unit on the tile that the player can't see | tiles without a unit the player sees, on the map. Extra weight on the rare positives. |
| hidden unit type | `[T, N, N]`, softmax over the T = 20 unit types per tile | that unit's type, as it is now (a boat is its boat type) | tiles that have a hidden unit |
| hidden unit count | softmax over 0 to 20, the last meaning 20 or more | how many of their units the player can't see | always |
| opponent's exploration | softmax over 0 to N² | how many tiles they've explored | always |
| unexplored (the map's, not per opponent: opponent 0, after the others) | `[15, N, N]`: per tile a softmax over the 5 terrains, one over the 7 resources (none first), one over the features (none, village, ruin) | what the tile holds now | tiles the player hasn't explored, except corners (always a lighthouse) and cities (determinization places those) |

- **"Can't see"** means on an unexplored tile, or hidden (a Cloak or Dagger) anywhere.
- **The joint for one tile:** P(a Rider here) = P(unit here) × P(Rider | unit here). The two are separate heads so the "here" loss can weight the rare tiles with units without distorting the type proportions ([`notes/design.md`](../notes/design.md) section 5).
- **Not in the first version:** enemy cities in the fog and a boat's passenger. Determinization fills those from simple priors until there are heads for them.
- **Read back** by `decodeBeliefs` (section 8.4), for determinization in self-play and matches (`network_beliefs`).

### 8.3 Counts

Exact integers with a cap for the unit count and exploration; a mixture of 3 negative binomials for stars, which covers any count without bins and can have two peaks ("spent everything" against "saving up"). A first choice, not a measured one: alternatives to compare are in [`notes/todo.md`](../notes/todo.md).

### 8.4 Interfaces

```cpp
// The targets as one flat array per position: each head's part, per
// opponent, with its name (layout as in the table above)
std::vector<TargetHead> targetHeads(const EncodingConfig &);   // name, opponent, offset, size
int targetCount(const EncodingConfig &);                       // 2,722 for N = 11, P = 2

// The belief targets for `player`, from the true game, and a mask of where
// each is trained ([targetCount] each). Stars are the count itself; the
// other counts are one-hot.
void encodeTargets(const Game &game, int player, const EncodingConfig &,
                   float *targets, std::uint8_t *masks);

```

From Python: `enc.target_count`, `enc.target_heads` (a list of `(name, opponent, offset, size)`), and `enc.encode_targets(games, players)`, which returns `(targets [B, T], masks [B, T])`.

**Reading the heads back:** `decodeBeliefs(view, config, outputs)` turns the network's raw belief outputs into `Beliefs` for `determinize()` to sample from. The outputs are one flat array per position, laid out like the targets (`beliefOutputHeads`, `enc.belief_output_heads`), except that each head holds its logits, and the stars head the mixture's `3 × STAR_COMPONENTS` raw parameters (weights, means, spreads, before the softmax and softplus). Decoding applies the softmaxes and sigmoids and turns the mixture into a weight for each count from 0 to `STAR_COUNT_CAP` (60), the same distribution the loss trains. Ruling out the impossible (tribes, proven techs) is left to `determinize()`.

**Tested** (`tests/stockNavalon/encoding_test.cc`) over random games: the hidden-unit masks are 0 exactly where the player sees a unit, so no target repeats what the input already shows. The type target is one-hot exactly where a hidden unit is, the per-tile targets add up to the count, and the tribe target is only trained while unmet, over the possible set.

An output layout change bumps `ENCODING_VERSION` like an input change, since a checkpoint is only valid against both.


### 8.5 Side targets

For side heads the search never reads, from how the game went: denser targets than one result per game, for the shared trunk to learn from (`encodeSideTargets` in `record.h`, layout in `encoding.h`'s `SideTargets`). A separate array per example, `[S]` with S = 2 + 3N² (365 for N = 11):

| Offset | Target | Mask |
| --- | --- | --- |
| 0 | the score lead 5 turns later: `tanh((the player's score − the best opponent's) / 500)`, from each player's latest published score at that turn, or the final scores if the game ends sooner | the record has a score table |
| 1 | the same with the final scores | always |
| 2 … | `[3][N][N]`: each map tile's owner when the game ended, one-hot over mine, an opponent's, nobody's | map tiles, where the record has the final territory |

- **From the record:** self-play stores the final territory (`finalTerritory`) and each player's latest published score at every turn (`turnScores`) when a game ends, so no extra replay is needed. Older records have neither, and train only the final lead.
- **Relative to the player to move**, like everything else.
- **Spatial,** so the territory moves with the map under a symmetry (section 9).

## 9. Symmetries

The rules don't depend on direction: the grid is square, movement and attack ranges count diagonal steps like straight ones, and map generation favours no side. So a position seen through any of the square's 8 symmetries (4 rotations, each optionally mirrored) is just as real, with the same value and the transformed policy. Training uses that to see each position in a random one of the 8 (`ReplayBuffer.sample_examples(augment=True)`).

| What | How it transforms |
| --- | --- |
| planes | each map tile's values move to the transformed tile; padding stays where it is |
| per-tile command slots (legal mask, policy) | move with their tile; a target offset slot `(dx, dy)` becomes the transformed offset's, so it still points at the transformed destination |
| global slots, global features | unchanged: none of them has a direction (the capital position features are counts of tiles) |
| per-tile belief targets and masks (hidden unit here, hidden unit type), the final territory side target | move with their tile |
| other belief targets, the value | unchanged |

Symmetry `s` (0 to 7, 0 the identity) swaps x and y if bit 4 is set, then mirrors x if bit 1 is and y if bit 2 is, within the map's own size (`transformTile`, `transformOffset`, `applySymmetry`). A new feature with a direction, or a command slot naming one, has to be added to the transform.

## 10. Full information

For training without fog first ([`stockNavalon.md`](stockNavalon.md) section 4), `encodeState(..., fullInformation = true)` encodes from `makeView(game, player, everything = true)`: every tile's terrain, resources, buildings and units, and every opponent's techs, stars, live score and tribe.
- **`explored` keeps its meaning:** the tiles the player has actually explored, since exploring still counts for score and the rules don't change. Every other per-tile plane shows the tile's true contents, explored or not.
- **Belief targets:** `encodeTargets(..., fullInformation = true)` masks everything (nothing is hidden).
- **Per game:** a record says whether the game was played with fog (`GameRecord::fog`), and training encodes its positions the same way.

