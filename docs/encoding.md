# Encoding: states and commands as arrays

The spec for turning game states into network input, and network output back into commands. It's the contract between polyengine and stockNavalon's network ([`stockNavalon.md`](stockNavalon.md)): `src/stockNavalon/encoding.h`, with Python access through `polyengine.Encoding`.

## 1. Configuration and versioning

```cpp
struct EncodingConfig {
    int mapSize = 11;       // N: every map is padded to N x N
    int players = 2;        // P: seats encoded, the player to move first
};
constexpr int ENCODING_VERSION = 16;
```

- **Sizes** all follow from the config; the counts shown are for N = 11, P = 2. A larger N only changes the padding (the trunk is fully convolutional with pooled heads); more players changes the first and last layers.
- **Versioning:** `ENCODING_VERSION` goes up whenever the meaning of any plane, slot or target changes. A run's saved held-out set records it, and is encoded again from its games when it differs (game records hold commands, so they always encode with the current version). New inputs go last, so older networks still load (`load_weights` in `model.py`: zero weights for new inputs, fresh weights for new or reshaped heads in `NEWER_HEADS`). What each version added:
  - 2: opponents' tribes hidden until met; starting scores. 3: climate planes, possible tribes, climates seen. 4: opening changes, capital position. 5: the `unexplored` belief head.
  - 6: map type. 7: stars and income side targets. 8: short-term value side targets. 9: shore planes. 10: reach planes, city-danger globals. 11: lethal plane, doomed-defender globals. 12: turns-left side target.
  - 13: head start. 14: capital income bonus. 15: reach and danger follow the current movement rules (no layout change). 16: the AquaCrop plane and AquaCrop in the `unexplored` head.

## 2. Conventions

- **Seen from the player to move.** The input is built from `makeView(game, player)` only, never from hidden state. A test checks this (section 7).
- **Relative players:** `rel 0` is the player to move, and `rel k` is seat `(player + k) % P`. So "mine" is always slot 0, and one network plays any seat.
- **Padding:** a map smaller than N sits in the top-left corner, with tile `(x, y)` at row `y`, column `x`. Padding has `on_map` = 0 and every other plane 0.
- **Unexplored tiles:** `explored` = 0 and every other per-tile plane 0, apart from `on_map`, shore and reach.
- **Scaling:** counts and amounts are divided by a fixed constant, so typical values fall roughly between 0 and 1. The constants are part of the spec, and changing one bumps the version.
- **Types:** planes and the global vector are `float32`; masks are `uint8`.

## 3. Spatial planes: `[C, N, N]`

C = 100 + 3P, which is **106** for P = 2.

### Board (50 planes)

| Planes | Count | Value |
| --- | --- | --- |
| `on_map` | 1 | 1 on real tiles, 0 on padding |
| `explored` | 1 | 1 if the player has explored the tile |
| terrain | 5 | one-hot: field, forest, mountain, shallow water, ocean |
| resource | 6 | one-hot: fruit, crop, game, metal, fish, starfish. Only resources the player can see (`TileView.resource`). AquaCrop has its own plane, last. |
| road | 1 | road or bridge |
| building | 22 | one-hot: village, ruin, lighthouse, farm, mine, lumber hut, windmill, sawmill, forge, market, port, temple, forest temple, mountain temple, water temple, then the 7 monuments. Cities have their own planes. |
| building level | 1 | Windmill, Sawmill or Forge level ÷ `MAX_BUILDING_LEVEL`, or temple level ÷ `Rules::MAX_TEMPLE_LEVEL`. The building planes say which it is. |
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
| HP | 1 | hp ÷ the most HP any unit can have, veterans included (`maxHp()` in `encoding.cc`), so it stays in [0, 1] |
| veteran | 1 | |
| can move, can attack, can act | 3 | own units only; 0 for others |
| hidden | 1 | own units only: hidden from enemies |
| hidden enemy near | 1 | own units only: the eye warning |

### Threats (3 planes)

Computed from the view alone, on explored tiles. Enemy reach assumes the worst case where the view is missing: the opponent owns every movement tech, and unexplored tiles are open ground. Moves follow the engine's movement search (`searchMoves`).

| Planes | Count | Value |
| --- | --- | --- |
| enemy attackers | 1 | how many visible enemy units could attack this tile on their next turn (move, then attack), ÷ 4 |
| enemy damage | 1 | the most damage any one of them could deal there next turn: to the player's unit on the tile, or else to a full-HP Warrior without defence bonus. In HP, ÷ 15. |
| own attackers | 1 | how many of the player's units can attack this tile this turn, with the moves and attacks they have left (Dash included), ÷ 4 |

### Shore (2 planes)

| Planes | Count | Value |
| --- | --- | --- |
| known land | 1 | 1 on an unexplored tile directly beside explored shallow water whose shore shows it's land (`PlayerView::TileView::knownLand`) |
| known water | 1 | the same where it shows water |

Both are 0 everywhere else, including every tile with full information (section 10).

### Reach (2 planes)

A city is captured by a unit that starts its turn on it, so an empty city an enemy can step onto falls the turn after unless it's defended; the threat planes, about attacks, don't show it.

| Planes | Count | Value |
| --- | --- | --- |
| enemy reach | 1 | how many visible enemy units could move onto this tile on their next turn, under the same worst case as the threats, ÷ 4 |
| own reach | 1 | how many of the player's units could move onto this tile this turn, ÷ 4 |

### Lethal (1 plane)

| Planes | Count | Value |
| --- | --- | --- |
| lethal | 1 | on each of the player's units: the total damage every visible enemy that could hit it next turn would deal (each as the threats' damage, against this unit on this tile), over its HP, capped at 1. At 1 it dies if they all attack it: the worst case, ignoring retaliation and order. |

### AquaCrop (1 plane)

| Planes | Count | Value |
| --- | --- | --- |
| aquacrop | 1 | 1 on a visible tile with AquaCrop (`ResourceTypes::AQUACROP`) |

## 4. Global vector: `[G]`

G = 70 + 17P + 45(P − 1), which is **149** for P = 2.

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
| starting score | P − 1 | per opponent, their first published score ÷ 1000. Public from turn 0, and the main evidence about a tribe not met yet, since starting scores differ by tribe (GAME.md section 3). |
| explained score | P − 1 | per opponent, the part of their published score that comes from things the player can see (section 4.1), ÷ 1000 |
| unexplained score | P − 1 | per opponent, (published score + the player's own visible effects on them since) − explained score: the score sitting in things the player can't see, ÷ 1000 |
| last change | P − 1 | per opponent, their latest published change, with the player's own effects on them since their previous publication taken out, ÷ 500. 0 before their first turn ends. |
| opening changes | 6(P − 1) | per opponent, their first 6 published changes, own effects taken out the same way, ÷ 500 (section 4.2) |
| opening seen | 6(P − 1) | per opponent, 1 for each of those changes that has happened, since a +0 change is evidence too |
| capital position | 12(P − 1) | per opponent, for each tribe they could still be, the tiles that tribe would have started with explored ÷ 25, 0 for a tribe ruled out (section 4.2) |
| techs | 25 | 1 per tech owned; Basic, which everyone owns, is left out |
| tasks done | 7 | |
| monuments built | 7 | |
| task progress | 3 | kills, peaceful turns and lighthouses found, each ÷ its task's threshold (`Rules::KILLER_KILLS`, `PACIFIST_TURNS`, `LIGHTHOUSE_COUNT`) |
| diplomacy | 5(P − 1) | per opponent: at peace, they asked for peace, we asked, our embassy there, their embassy here |
| reward pending | 1 | 1 if a city level-up reward must be chosen now |
| map type | 6 | one-hot: drylands, lakes, pangea, continents, archipelago, waterworld. Public from the start, and what the unexplored tiles are likely to be. |
| cities in danger | 6 | the player's cities with an enemy unit on them ÷ 4; their empty cities an enemy could step onto next turn ÷ 4; each as 1 or 0 for the capital alone; then their cities held by a defender the visible enemies could kill (lethal 1) ÷ 4, and 1 or 0 for the capital |
| head start | P | per relative player, the extra stars they started with in a handicap game (`GameSettings::headStart`, public), ÷ 50; 0 in ordinary games |
| capital income | P | per relative player, their capital income above `Rules::CAPITAL_INCOME` (`GameSettings::capitalIncome`, public; the game's bot handicap), ÷ 30, as income is |

### 4.1 Score features: exact numbers, not raw history

Reading a score change is exact arithmetic: breaking a number into a sum of known amounts. Networks are bad at that, especially on scaled-down floats. So the engine does the arithmetic, and the network gets exact summaries. The full score history isn't encoded. It stays in `PlayerView.scoreHistory` for the parts of stockNavalon that calculate score exactly, such as tribe inference ([`tribeInference.md`](tribeInference.md)): the network says which explanations are likely, and the exact side rules out the impossible.

**Explained score** (`explainedScore`): the score the player can account for from what they see of that opponent, computed with the engine's score formula:
- visible cities: level, population, parks
- visible units
- border tiles on explored ground
- visible temples, at their actual level (temple levels are public), and monuments

**Unexplained score:** the rest of their score. It's the score in hidden things: techs, hidden units, unseen cities and territory, and their exploration. Being one number, it needs no history window, because the published score already adds up the whole game so far.

Their published score is from the end of their last turn, but the explained score is from the view now. Anything the player has done to them since, like killing a unit or capturing a city, is gone from what's visible but still in the published score. Without a correction, killing their unit would make their unexplained score jump by that unit's value. So the player's own visible effects on them since that publication are applied to the published score first: `unexplained = (published + own effects) − explained`. Own effects are negative: units of theirs seen dying, cities captured.

**Last change:** a "how active were they last turn" signal. A +300 spike just now means something different from a steady build-up. The player's own effects (units of theirs seen dying, cities captured) are taken out, counting only what the player could see, so nothing hidden leaks in.

All three use the published score, never the live one. Opponents can't change anything during the player's turn, so with the own-effects correction the split is exact, apart from one thing. Effects the player couldn't see, like splash damage killing a hidden unit, stay in the unexplained score. That's deliberate: counting them would leak that a hidden unit was there.

Temple levels count at their real value, since they're public. A temple's score is calculated at its level as of the opponent's last turn end, to match their published score.

### 4.2 The opening

The exact side ([`tribeInference.md`](tribeInference.md)) only rules out what's impossible; most of what an opening says is likelihood. The belief head learns that, inside the exact set, from the tribe target (section 8.2), so the first `OPENING_CHANGES` changes are encoded as they are, not only summarized.

**Capital position:** for each tribe, the starting score fixes how many tiles of the starting square were explored (`startingExplored`), which says how near an edge or corner the capital is. That depends on the tribe hypothesis, so it's given per possible tribe.

## 5. Commands: `[A]`

A = 218·N² + 26 + 6(P − 1), which is **26,410** for N = 11, P = 2.

Each legal command has exactly one index. Indices that don't match a legal command are masked out.

### Per-tile slots: index = `slot · N² + y · N + x`

The source tile is `(x, y)`: the unit's tile, the city's tile, or the tile being built on. Slot-major order means a convolutional head's `[218, N, N]` output flattens straight into the index space.

| Slots | Count | Command |
| --- | --- | --- |
| 0–168 | 169 | **target offset**: Move, Attack or Disembark, to `(x + dx, y + dy)` with `slot = (dy + 6) · 13 + (dx + 6)`, for `dx, dy` in −6…6 (`OFFSET_RADIUS`). The middle slot (no offset) is never legal. |
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

// Writes the player's input. Reads only what the player knows: their view,
// the public score history, attack previews between units they can see, and
// their own units' moves.
void encodeState(const Game &game, int player, const EncodingConfig &,
                 float *planes, float *globals, bool fullInformation = false);

// -1 if the command has no slot (Resign, Stay, or a tile outside N)
int encodeCommand(const Game &game, const Command &command, const EncodingConfig &);
// the legal command at this index; throws if none is
Command decodeCommand(const Game &game, int index, const EncodingConfig &);
// 1 for each legal command's index
void legalMask(const Game &game, const EncodingConfig &, std::uint8_t *mask);
void legalMask(const std::vector<Command> &legal, const Game &game,
               const EncodingConfig &, std::uint8_t *mask);
```

From Python, the batched forms fill NumPy arrays in C++, without copying:

```python
enc = pe.Encoding(map_size=11, players=2)
planes, globals_ = enc.encode_states(games, players)   # [B, C, N, N], [B, G] float32
masks = enc.legal_masks(games)                         # [B, A] uint8
examples = enc.encode_examples(records, positions)     # dict of training arrays
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
- **In search,** build the mask from the node's legal list with the second `legalMask` overload, which skips regenerating it.

## 7. Tests

In `tests/stockNavalon/encoding_test.cc` and `tests/python/test_engine.py`. They run over random games on generated Tiny maps of every type, and Small maps padded to 16.

1. **Commands round-trip:**
   - every legal command encodes to an index, and decodes back to itself
   - no two legal commands share an index
   - the mask has exactly as many ones as there are legal commands
2. **No leaks:** two games that differ only in what the player can't see (fog contents including climate, hidden units, the opponent's stars, techs, kills, exploration, and their tribe if not met) encode to identical input.
3. **Feature checks:** in hand-built positions, specific features land where they should: units, score features, the opening, shore, reach, city danger, lethal, symmetries, full information, the belief targets and their decoding.
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
- **The trained target** can blend in the position's search value, a λ-return through later values, and a city lead, with cut-off games weighted down (`ValueTarget` in `record.h`).

### 8.2 Beliefs

Per opponent, so `P − 1` of each in general; one in 1v1. A belief head's target is about the true game, but it's only trained where the player doesn't already know the answer (the mask), and not at all for a dead opponent. Counts come as exact integers up to a cap, or as a distribution with no bins (section 8.3).

| Head | Output | Target | Trained when |
| --- | --- | --- | --- |
| tribe | softmax over 12 | their tribe | not met; tribes ruled out ([`tribeInference.md`](tribeInference.md)) are masked to −∞ before the softmax |
| techs | sigmoid per tech, 25 (Basic left out) | each tech owned | always |
| stars | mixture of 3 negative binomials: a weight, mean and spread each (9 numbers) | their stars | always |
| hidden unit here | `[N, N]`, sigmoid | an enemy unit on the tile that the player can't see | tiles without a unit the player sees, on the map. Extra weight on the rare positives (`LossWeights.hidden_unit_positive`). |
| hidden unit type | `[T, N, N]`, softmax over the T = 18 unit types per tile | that unit's type, as it is now (a boat is its boat type) | tiles that have a hidden unit |
| hidden unit count | softmax over 0 to `HIDDEN_UNIT_COUNT_CAP` (20), the last meaning that or more | how many of their units the player can't see | always |
| opponent's exploration | softmax over 0 to N² | how many tiles they've explored | always |
| unexplored (the map's, not per opponent: opponent 0, after the others) | `[16, N, N]` (`UnexploredHead`): per tile a softmax over the 5 terrains, one over the 8 resources (none first, AquaCrop last), one over the features (none, village, ruin) | what the tile holds now | tiles the player hasn't explored, except corners (always a lighthouse) and cities (determinization places those) |

- **"Can't see"** means on an unexplored tile, or hidden (a Cloak or Dagger) anywhere.
- **The joint for one tile:** P(a Rider here) = P(unit here) × P(Rider | unit here). The two are separate heads so the "here" loss can weight the rare tiles with units without distorting the type proportions ([`notes/design.md`](../notes/design.md) section 5).
- **No heads** for enemy cities in the fog or a boat's passenger: determinization fills those from simple priors.
- **Read back** by `decodeBeliefs` (section 8.4), for determinization in self-play and matches (`network_beliefs`).

### 8.3 Counts

Exact integers with a cap for the unit count and exploration; a mixture of `STAR_COMPONENTS` negative binomials for stars, which covers any count without bins and can have two peaks ("spent everything" against "saving up").

### 8.4 Interfaces

```cpp
// The targets as one flat array per position: each head's part, per
// opponent, with its name (layout as in the table above)
std::vector<TargetHead> targetHeads(const EncodingConfig &);   // name, opponent, offset, size
int targetCount(const EncodingConfig &);                       // 4,417 for N = 11, P = 2

// The belief targets for `player`, from the true game, and a mask of where
// each is trained ([targetCount] each). Stars are the count itself; the
// other counts are one-hot.
void encodeTargets(const Game &game, int player, const EncodingConfig &,
                   float *targets, std::uint8_t *masks, bool fullInformation = false);
```

From Python: `enc.target_count`, `enc.target_heads` (a list of `(name, opponent, offset, size)`), and `enc.encode_targets(games, players)`, which returns `(targets [B, T], masks [B, T])`.

**Reading the heads back:** `decodeBeliefs(view, config, outputs)` turns the network's raw belief outputs into `Beliefs` for `determinize()` to sample from. The outputs are one flat array per position, laid out like the targets (`beliefOutputHeads`, `enc.belief_output_heads`; 4,425 for N = 11, P = 2), except that each head holds its logits, and the stars head the mixture's `3 × STAR_COMPONENTS` raw parameters (weights, means, spreads, before the softmax and softplus). Decoding applies the softmaxes and sigmoids and turns the mixture into a weight for each count from 0 to `STAR_COUNT_CAP`, the same distribution the loss trains. In Python, `belief_outputs` in `model.py` builds this array from the network's outputs. Ruling out the impossible (tribes, proven techs) is left to `determinize()`.

**Tested** (`tests/stockNavalon/encoding_test.cc`) over random games: the hidden-unit masks are 0 exactly where the player sees a unit, so no target repeats what the input already shows. The type target is one-hot exactly where a hidden unit is, the per-tile targets add up to the count, and the tribe target is only trained while unmet, over the possible set.

An output layout change bumps `ENCODING_VERSION` like an input change, since a checkpoint is only valid against both.


### 8.5 Side targets

For side heads, from how the game went: denser targets than one result per game, for the shared trunk to learn from (`encodeSideTargets` in `record.h`, layout in `encoding.h`'s `SideTargets`). The search reads only two of them, the final score lead and turns left, as tie-breaks (`tie_breaks` in `model.py`; `SearchConfig::scoreUtility`, `winSooner`). A separate array per example, `[S]` with S = 8 + 3N² (371 for N = 11):

| Offset | Target | Mask |
| --- | --- | --- |
| 0 | the score lead 5 turns later: `tanh((the player's score − the best opponent's) / 500)`, from each player's latest published score at that turn, or the final scores if the game ends sooner | the record has a score table |
| 1 | the same with the final scores | always |
| 2 | the player's own stars 10 turns later (their latest turn by then, or their last if the game ended first; 0 once captured) ÷ 50 | the record has the economy tables |
| 3 | the same for their income ÷ 30 | the same |
| 4, 5, 6 | short-term values (KataGo's), for horizons of 3, 10 and 30 turns: the λ-return of the player's own search values with λ = 1 − 1/horizon, from the game's value at the end (the result, or a cut-off game's final value) back to this turn: G = (1 − λ) × v + λ × G' each turn, v the mean of the player's search values that turn, a turn without one passing G' on | the record has the per-turn values |
| 7 | turns until the game ended, min(turns, 100) ÷ 50 (Leela Chess Zero's moves left) | games that ended in a capture: a cut-off game says nothing about when its end would have come |
| 8 … | `[3][N][N]`: each map tile's owner when the game ended, one-hot over mine, an opponent's, nobody's | map tiles, where the record has the final territory |

- **From the record:** self-play stores the final territory (`finalTerritory`) and each player's latest published score at every turn (`turnScores`) when a game ends, and each player's stars and income as each of their turns starts (`turnStars`, `turnIncome`) and the mean of their search values in each turn (`turnValues`; none for the score-evaluator bootstrap, whose values are score leads) as it goes, so no extra replay is needed. Older records lack some or all of them, and train only what they have.
- **The short-term values** give the trunk lower-variance value signals than the game's one result; the search reads the main value head, not these.
- **The economy targets** are for planning the one-turn search can't see: what's built now pays off in stars and income over the next turns, a denser signal than winning.
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

For training without fog ([`stockNavalon.md`](stockNavalon.md) section 4), `encodeState(..., fullInformation = true)` encodes from the full view: every tile's terrain, resources, buildings and units, and every opponent's techs, stars, live score and tribe.
- **`explored` keeps its meaning:** the tiles the player has actually explored, since exploring still counts for score and the rules don't change. Every other per-tile plane shows the tile's true contents, explored or not.
- **Belief targets:** `encodeTargets(..., fullInformation = true)` masks everything (nothing is hidden).
- **Per game:** a record says whether the game was played with fog (`GameRecord::fog`), and training encodes its positions the same way.

