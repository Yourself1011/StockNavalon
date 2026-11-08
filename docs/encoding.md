# Encoding: states and commands as arrays

The spec for turning game states into network input, and network output back into commands. It's the contract between polyengine and stockNavalon's network (design in [`stockNavalon.md`](stockNavalon.md)). Status: **spec, for review**; nothing here is implemented yet.

## 1. Configuration and versioning

```cpp
struct EncodingConfig {
    int mapSize = 11;       // N: every map is padded to N x N
    int players = 2;        // P: seats encoded, the player to move first
};
constexpr int ENCODING_VERSION = 1;
```

- **First target: 11×11 (Tiny), 1v1.** That's the smallest game that still has the whole game in it, so it's the fastest way to find out whether the architecture learns at all. Every number in this document is derived from the config; the concrete counts shown are for N = 11, P = 2.
- **Expanding later:**
  - **A larger N:** nothing changes but the padding. Every per-tile part of the layout is the same at any size. A fully convolutional trunk with pooled heads (section 2 of the design doc) runs at any N, so a model trained at 11 can be fine-tuned at 14 or 16 rather than retrained from scratch.
  - **More players:** the planes and slots that exist once per player grow, so the network's first and last layers change shape. The trunk can still be carried over.
- **Versioning:** `ENCODING_VERSION` goes up whenever the meaning of any plane or slot changes. Saved training data and model checkpoints record the version and the config, so mismatches fail loudly instead of training on misread data.

## 2. Conventions

- **Seen from the player to move.** The input is built from `makeView(game, player)` only, never from hidden state. A test checks this (section 6).
- **Relative players:** `rel 0` is the player to move, and `rel k` is seat `(player + k) % P`. So "mine" is always slot 0, and one network plays any seat.
- **Padding:** a map smaller than N sits in the top-left corner, with tile `(x, y)` at row `y`, column `x`. Padding has `on_map` = 0 and every other plane 0.
- **Unexplored tiles:** `explored` = 0 and every other per-tile plane 0, apart from `on_map`.
- **Scaling:** counts and amounts are divided by a fixed constant, so typical values fall roughly between 0 and 1. The constants are part of the spec, and changing one bumps the version.
- **Types:** planes and the global vector are `float32`; masks are `uint8`.

## 3. Spatial planes: `[C, N, N]`

C = 82 + 3P, which is **88** for P = 2.

### Board (38 planes)

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
| HP | 1 | hp ÷ 400 (HP is ×10 in the engine, so a 40-HP Giant is 1) |
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

G = 46 + 15P + 8(P − 1), which is **84** for P = 2.

| Features | Count | Value |
| --- | --- | --- |
| turn | 1 | turn ÷ 50 |
| stars | 1 | the player's stars ÷ 50 |
| income | 1 | ÷ 30 |
| tribe | 12P | one-hot per relative player |
| alive | P | per relative player |
| met | P | per relative player (1 for the player themselves) |
| score | P | as the player sees them (own live, others published) ÷ 1000 |
| explained score | P − 1 | per opponent, the part of their published score that comes from things the player can see (section 4.1), ÷ 1000 |
| unexplained score | P − 1 | per opponent, (published score + the player's own visible effects on them since) − explained score: the score sitting in things the player can't see, ÷ 1000 |
| last change | P − 1 | per opponent, their latest published change, with the player's own effects on them since their previous publication taken out, ÷ 500. 0 before their first turn ends. |
| techs | 25 | 1 per tech owned; Basic, which everyone owns, is left out |
| tasks done | 7 | |
| monuments built | 7 | |
| task progress | 3 | kills ÷ 10, peaceful turns ÷ 5, lighthouses found ÷ 4 |
| diplomacy | 5(P − 1) | per opponent: at peace, they asked for peace, we asked, our embassy there, their embassy here |
| reward pending | 1 | 1 if a city level-up reward must be chosen now |

### 4.1 Score features: exact numbers, not raw history

Reading a score change is exact arithmetic: breaking a number into a sum of known amounts. Networks are bad at that, especially on scaled-down floats. So the engine does the arithmetic, and the network gets exact summaries. The full score history isn't encoded. It stays in `PlayerView.scoreHistory` for the parts of stockNavalon that calculate score exactly: the score check in determinization, and the particle filter ([`stockNavalon.md`](stockNavalon.md) section 5.5). The two divide the work: the network says which explanations are likely, and the exact check makes samples add up.

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

## 6. Interfaces (**Proposed**)

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

## 7. Tests

Run over the fuzz games: random bots on generated 11×11 maps of every type, plus Tiny and Small maps to check padding.

1. **Commands round-trip:**
   - every legal command encodes to an index, and decodes back to itself
   - no two legal commands share an index
   - the mask has exactly as many ones as there are legal commands
2. **No leaks:** two games that differ only in what the player can't see (fog contents, hidden units, the opponent's stars and techs) encode to identical input. Same idea as the determinization leak test.
3. **Feature checks:** in hand-built positions, specific features land where they should. For example: a Rider of the opponent's at (3, 4) sets unit owner `rel 1` and unit type `RIDER` at row 4, column 3, and a threat value on the tiles it can reach.
4. **Relative players:** the same position encoded for player 0, and for player 1 with the seats swapped, gives the same input.
5. **Padding:** a 11×11 map at N = 16 matches the N = 11 encoding in its top-left corner, and is zero elsewhere apart from `on_map`.
