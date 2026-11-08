# stockNavalon: bot design

stockNavalon is the bot that polyengine, the game engine in this repository, exists for. This document collects the design discussed so far: what the network is, how the search uses it, how fog is handled, and the order to build things in. Nothing here is built yet, apart from the engine pieces it relies on (see [`cpp.md`](cpp.md) and [`python.md`](python.md)).

Status markers: **Decided** means agreed; **Proposed** is the current plan, still open to change; **Open** is a question still to answer.

## 1. Overview

stockNavalon follows the AlphaZero design, despite the Stockfish-like name: a tree search guided by a neural network.

| | Stockfish | AlphaZero / stockNavalon |
| --- | --- | --- |
| Search | alpha-beta, very deep, millions of positions per second | Monte Carlo tree search (MCTS): fewer positions, guided by the network |
| Network | NNUE: a cheap evaluation, updated incrementally per move | a **value** head ("how good is this position?") and a **policy** head ("which moves are worth trying?") |
| Fits games where | there are few moves per position (chess: about 35) | there are very many moves per position (Go: about 250) |

Polytopia is at the far end of that scale. A turn is 10 to 30 commands, each chosen from about 60 legal ones, so the number of possible turns is astronomical. Brute force gets nowhere; a policy that says which moves are worth looking at is essential. **Decided.**

The pieces:

1. **The network** (section 2): one shared trunk with policy, value and belief heads.
2. **The search** (section 3): MCTS using the PUCT rule.
3. **Fog** (section 4): the search runs on sampled versions of the hidden state (determinization).
4. **The belief model** (section 5): predicts the hidden state, including by reading the public score history, to make those samples realistic.
5. **Batched evaluation** (section 6): the search runs in C++ and asks Python for network evaluations in large batches.

## 2. The network

### One network, several heads (**Proposed**)

One shared trunk, with heads for policy, value and beliefs, rather than separate networks:

- **Cost:** every new position in the search needs both the policy and the value, and a shared network gives both in one forward pass.
- **Better learning:** the heads all need the same understanding of the position (threats, economy, who controls what), and auxiliary predictions like the belief heads usually help the main heads train.

The original AlphaGo used separate policy and value networks. Splitting is still possible later, for example if a much cheaper policy turns out to be useful.

### Input

The input is always built from `PlayerView`, never the full `Game`, so the network only sees what the player knows.

- **Spatial planes, `[C, N, N]`, about 100 channels:**
  - explored or not
  - terrain and resource, one plane per kind
  - building kind and level, road
  - border owner
  - cities: level, population, capital, wall
  - units: type, HP fraction, veteran, whether they can still move, attack or act
  - the hidden-enemy warning
- **Relative to the player to move:** ownership planes are "mine", "opponent 1", "opponent 2", not "player 0", "player 1". One network then plays any seat.
- **Threat map (**Decided**):** for each tile, which visible enemy units could attack it next turn, and how much damage they'd do. The engine computes this directly (`moveTargets`, `attackTargets`, `previewAttack`). It gives the network one turn of tactical lookahead without search, whatever the search horizon (section 3.4) ends up being.
- **A global vector:** stars, income, turn, owned techs, tasks and progress, and per opponent: met or not, score, alive, peace and embassies, tribe.
- **Score history:** for each opponent, the change in their published score each turn (section 5).

### Trunk

A convolutional ResNet, perhaps 6 to 10 blocks of 64 to 128 channels for a 14×14 map. Convolutions suit a grid, since a threat looks the same anywhere on the map. A transformer over tiles is the main alternative, if long-range structure like roads and ports matters more than expected. **Proposed.**

### Heads

- **Policy:**
  - convolutions output per-tile logits for the command slots (section 6.1), plus a pooled branch for global commands (research, diplomacy, End Turn)
  - illegal slots are masked out using the engine's legal moves
- **Value:**
  - pooled features into a small MLP, giving the expected result for the player to move
  - possibly an auxiliary head predicting the final score difference, since win/loss alone is a sparse signal in long games
- **Beliefs:** the hidden state (section 5.2).

## 3. The search

### 3.1 What each node stores

For each command `a` from a position `s`:

| | Meaning | From |
| --- | --- | --- |
| `P(s,a)` | prior: how promising the move looks before searching | policy head |
| `N(s,a)` | how many simulations went through this move | search |
| `W(s,a)` | the sum of values backed up through it | value head, via back-up |
| `Q(s,a) = W/N` | average value found below this move | |

### 3.2 One simulation

1. **Select:** from the root, repeatedly follow the move that maximises
   ```
   Q(s,a) + c · P(s,a) · √N(s) / (1 + N(s,a))
   ```
   until reaching a position not yet in the tree.
2. **Expand and evaluate:** run the network once on the new position. The **value head** gives `v`, and the **policy head** gives priors `P` for its moves, stored on the new node for later.
3. **Back up:** along the path to the root, add 1 to `N` and `v` to `W` on each edge. The sign of `v` follows whose turn it is (section 3.4).

If a position is an actual game end, `v` is the real result rather than the network's guess.

Nothing decides "go deeper" or "try another move" explicitly; the formula balances the two:

- **`Q` exploits:** moves that have turned out well get chosen again, and each time a move is chosen its subtree grows one level deeper.
- **The second term (`U`) explores:** it's large for moves with a good prior and few visits. It shrinks as a move is visited (`1 + N(s,a)`), and grows slowly for every move as the parent is visited (`√N(s)`). So once a line has had its share, or has disappointed, a sibling gets tried.
- **`c`** sets the balance between the two.

### 3.3 A worked example

Three moves, with priors A 0.6, B 0.3, C 0.1, and `c` = 1.5. Unvisited moves count as `Q` = 0.

| Simulation | √N(root) | A: Q + U | B: Q + U | C: Q + U | Picked | Result |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 1 | 0 + 0.90 | 0 + 0.45 | 0 + 0.15 | A | A evaluates to 0.2 |
| 2 | 1.41 | 0.20 + 0.64 | 0 + 0.64 | 0 + 0.21 | A | deeper, inside A; a move there evaluates to −0.1, so A: N = 2, Q = 0.05 |
| 3 | 1.73 | 0.05 + 0.52 | 0 + 0.78 | 0 + 0.26 | B | A disappointed; B evaluates to 0.5 |
| 4 | 2 | 0.05 + 0.60 | 0.50 + 0.45 | 0 + 0.30 | B | B gets deepened |

### 3.4 After the search

- **The move played** is the one with the **most visits**, not the highest `Q`. Visits are more reliable, since a high `Q` from two visits may be luck.
- **The visit counts are the policy's training target.** The network learns to predict where the search spent its effort, so its priors improve and the search needs fewer simulations next time.
- **Tree reuse:** after playing a command, keep the subtree under it. With 10 to 30 commands per turn, that saves a lot of work.
- **Fast legal moves:** each node keeps a `LegalMoves` next to its `Game`, which updates the legal list incrementally after each command. Both are cheap to copy.

### 3.5 The search horizon (**Proposed**)

How far ahead the tree reaches is an open design choice, so the search makes it a setting rather than building one in:

1. **Own turn only.** The tree is your commands for this turn. End Turn leads to a leaf scored by the value head, and every value is from your perspective.
   - Simple, and almost unaffected by fog (section 4.2).
   - But the opponent's reply is never searched, only judged by the value head. The network alone misses some tactics, and replies like "their knight kills this archer, then takes the city" are exactly where search usually adds strength.
2. **Through turn boundaries.** The tree continues into the opponent's turn on a determinized game. Their moves are chosen with the same formula, using priors from *their* view, and the value flips sign at each turn change.
   - The standard approach. MCTS spends depth only where it pays off.
   - But a round of turns is 20 to 40 commands deep, and the opponent's turn is searched on sampled hidden state.
3. **Whole turns as moves.** Sample a few candidate turns from the policy for each player, and search a tree where each level is a complete turn.
   - Several rounds of lookahead.
   - But it's only as good as the candidate turns the policy proposes.
4. **Quick opponent replies at the leaves.** After your turn, let the policy play the opponent's turn quickly a few times, without a tree, and evaluate the results.
   - Cheap, and catches obvious punishments.
   - But the replies are only as good as the policy.

**Plan:** build the search so it can cross turn boundaries:
- the value's sign handled at each turn change
- each player's priors computed from that player's view
- running on determinized games
- the depth as a setting

Then measure which horizon gives the most strength per unit of compute, by playing the options against each other. The threat map (section 2) helps whichever wins.

## 4. Fog of war

### 4.1 The problem

The search needs a `Game` to apply commands to, and a `Game` contains everything: hidden tiles, enemy units in fog, the opponent's stars and techs. Searching on the real `Game` leaks information even though the network reads only `PlayerView`:

- a scout's move in the search reveals the *real* tiles behind the fog, so the search learns "this move finds a village" before the bot could know it
- the opponent's replies are searched with their real units and stars

A bot trained that way learns habits based on information it won't have in a real game.

### 4.2 What hidden information matters when

| Hidden | Matters within your own turn? |
| --- | --- |
| opponent's stars, techs, plans | no: they only act on their own turn |
| what's on tiles your moves will reveal | **yes** |
| hidden enemy units (Cloaks) | yes, rarely: moving onto one reveals it |
| ruin rewards | yes, but that's chance, not hidden information |

Explored tiles show their live contents, including enemy units (GAME.md section 16), so everything on explored tiles is known. Within your own turn, fog comes down to what you'll find by exploring. Once the search crosses into the opponent's turn (section 3.5), their stars, techs and units matter too.

### 4.3 Determinization (**Decided**)

1. **Sample a full game consistent with the player's view:**
   - explored tiles, visible units and your own state are copied exactly
   - unexplored tiles, units in fog, and the opponent's stars and techs are sampled (section 5)
2. **Run MCTS on the sampled game.** Revealed tiles show sampled contents, never real ones.
3. **Repeat with K samples** (say 4 to 8), adding up root visit counts across them.

The value head is trained on `PlayerView` against real outcomes, so it already estimates the result averaged over the hidden states consistent with the view.

**Known weakness: strategy fusion.** Each sampled search behaves as if it knew what's behind the fog, and plans that only work in that sample, like "explore here, then take the village there". Averaging across samples blurs this but doesn't fix it. Information-set MCTS (ISMCTS), which shares one tree across samples, fixes it properly. It's the upgrade path if the weakness shows up in practice.

**Before any bot is judged or trained for real,** the search runs on determinized games only. Searching the real game is allowed just for getting the pipeline running, and must be clearly labelled as cheating.

## 5. The belief model

### 5.1 Reading the score (**Decided**)

Every player's score is public. Others' scores update when their player ends a turn, and your own is live (GAME.md section 3). The engine records every published score in `Game::scoreHistory` (also in `PlayerView`).

Score is a fixed formula, so each change is a sum of known amounts:

| Change | Likely cause |
| --- | --- |
| +100 / +200 / +300 | a tech of tier 1 / 2 / 3 |
| +5 per tile | exploring |
| +20 per tile | territory gained |
| +5 × unit cost | a unit trained (Warrior +10, Rider +15, Knight +40) |
| +5 per population, plus a level-up bonus | city growth |
| +100 | a temple |
| +400 | a monument |
| negative amounts | losses |

Pros read scores like this for the first turn or two, after which the possibilities multiply too fast. A bot can do it every turn for the whole game.

- **A published change covers everything since that player's last turn ended,** including what *you* did to them in between: units you killed, cities you took. You know your own effects, so subtract them; the rest is what they did.
- **Evidence accumulates:** each turn's change is ambiguous on its own, but a tech, once bought, has to stay consistent with every later turn. Turn by turn, the possibilities narrow.

What it reveals:
- **techs:** the strongest signal; tier sizes are distinct, and a visible unit also proves its tech
- **stars:** from estimated spending and visible income
- **units trained in the fog:** +5 × cost
- **expansion:** exploration and territory gains

This only works if the engine's score formula matches the real game's exactly. The starting scores are verified; the mid-game rules (temple levels, population and level-up bonuses, losses) should be checked against real games before the bot relies on them. **Open.**

### 5.2 What the model predicts

| Target | Output | Loss |
| --- | --- | --- |
| opponent's techs | a probability per tech | binary cross-entropy |
| opponent's stars | a distribution over ranges | cross-entropy |
| unexplored tiles | per tile: terrain, resource, village/ruin/city/none | cross-entropy, on unexplored tiles only |
| enemy units in fog | per tile and unit type: the chance one is there | binary cross-entropy, on tiles you can't see |
| opponent's exploration | how many tiles they've explored | regression |

The unit predictions amount to a heat map of where the enemy army probably is.

**First version (**Proposed**):** the opponent's techs, stars and unit positions. Unexplored terrain stays with the baseline sampler (section 5.4), since it's the hardest part to sample well and matters least within a turn.

### 5.3 Inputs and training

- **Inputs:**
  - the same encoded view as the other heads
  - the score history, as per-turn changes per opponent, with the player's own effects subtracted, through a small 1-D convolution or GRU
  - optionally, hand-built breakdowns of each change as extra features, like "divisible by 100"
- **Training data is free:** in self-play the engine knows the full truth, so every position is a labelled example: the player's view as input, the real hidden state from the `Game` as the answer. Data collection can start right away, with the existing random and one-ply bots.
- **Caveat:** beliefs depend on how the opponent plays. A model trained on self-play expects opponents like itself, and real opponents won't be. That's acceptable for a first version.

### 5.4 From predictions to a consistent game

The model gives separate probabilities; a determinized game has to be one coherent state:

- **Techs:** sample down the tech tree, parents before children, so the result is always a valid set of techs.
- **Stars:** sample from the predicted distribution, never below 0.
- **Units:** sample a count per type, then place them using the heat map, only on legal tiles: the right terrain, not a tile you can see, not stacked.
- **Terrain:** sample each tile from its prediction and smooth the result at first. Better later: fill tiles in order so each depends on its neighbours, or steer the map generator with the predictions.
- **Score check:** the opponent's published score is an exact number, and the engine can compute a sample's score. Reject or down-weight samples whose score is far off. Their exploration count is hidden, so the match can't be exact, but it's a strong check on techs and army size.

### 5.5 Engine interface (**Proposed**)

```cpp
struct Beliefs {   // the model's output, in engine terms
    std::array<float, TechTypes::TECH_TYPE_SIZE> opponentTech;
    std::vector<float> starBuckets;
    // per tile: terrain and resource distributions, unit heat by type, ...
};

// a full Game matching everything `player` knows, with the rest sampled from the beliefs
Game determinize(const Game &game, int player, const Beliefs &beliefs, std::uint64_t seed);
```

There's also a baseline `Beliefs` that needs no model:
- terrain from the map generator's rates
- tech guesses from score changes alone
- units near the opponent's known cities

The search, determinization and leak tests can all be built and tested on that before any training happens.

**The leak test:** for any game and player, the sampled game seen from that player must equal the real game seen from that player, tile by tile and unit by unit. It also has to be a valid game.

### 5.6 How to tell it works

- **Accuracy:** predictions of techs, stars, units and terrain on held-out games, against the baseline.
- **Calibration:** when it says 70%, it should be right about 70% of the time.
- **Playing strength:** the same search with learned beliefs against the search with baseline beliefs.

## 6. Plumbing

### 6.1 Command numbering (**Proposed**)

The policy head needs every possible command to have a fixed slot number. Most slots are per tile:

| Per tile | Slots | Covers |
| --- | --- | --- |
| target offset | 13 × 13 = 169 | Move, Attack, Disembark, by target offset up to 6 tiles (a knight on roads) |
| unit actions | about 7 | Recover, Heal Others, Promote, Disband, Capture, Examine Ruins, Harvest |
| upgrade | 3 | Scout, Rammer, Bomber |
| build / destroy | about 32 | each improvement type, and Destroy |
| train | 9 | each unit type |
| city reward | 2 | option A or B |

Plus global slots: research (about 25), diplomacy (6 per opponent), End Turn.

- **One set of target slots for three commands:** a move goes to an empty tile, an attack to an enemy, and a disembark from a boat onto land, so for a given unit the same offset never means two things.
- **Size:** about 220 slots per tile, about 43,000 on a 14×14 map. Only the legal ones (usually about 60) count, through a mask from the engine.
- **Engine functions:** `encodeCommand`, `decodeCommand` and `legalMask`, exposed to Python as NumPy arrays.

### 6.2 Batched evaluation (**Decided**)

The search runs in C++, many games at once, and hands positions to the network in Python in batches:

```python
search = polyengine.BatchedSearch(games=256, simulations=400, ...)
while not search.done():
    planes, globals_, masks = search.pending()   # positions waiting to be scored
    logits, values, beliefs = model(planes, globals_)
    search.submit(logits, values, beliefs)
```

- Each tree walks down until it reaches a position nobody has scored, encodes it, and waits.
- Once all are waiting, Python scores the whole batch in one call, and C++ continues.
- So the cost of crossing between the languages is paid once per batch, and the GPU stays busy.

Later, a trained model can be exported (TorchScript or ONNX) and run inside C++ for fast play without Python. The search wouldn't change.

## 7. Training (**Proposed**)

1. **Plumbing:** the batched search with a dummy network (uniform priors, value 0), against the random bot, on determinized games with the baseline sampler.
2. **Bootstrap:** before self-play, train the value and belief heads on games from simple bots (random, one-ply). Pure self-play from scratch can take a long time to get started.
3. **Self-play:** the full loop. The search produces visit counts and results, and the network trains on them: the policy towards the visit counts, the value towards the results, and the beliefs towards the true hidden state.
4. **Measure:** the search horizons against each other (section 3.5), and learned beliefs against the baseline (section 5.6).

**Long games:** the rules have no turn limit, so training needs one, with a fallback value for games that hit it, such as score difference.

## 8. Open questions

1. **Compute:** what will training run on (one GPU, several, Apple Silicon)? That decides network size and how much self-play is affordable.
2. **The goal:** a strong bot under a time limit per move, eventually playing the real game? Or mainly a research platform?
3. **Human games:** are any records available? Supervised pre-training on them would make the bootstrap much easier.
4. **Map size:** the network needs a fixed `N`. Pad to 16 (Tiny to Normal), or 14 for the first target?
5. **The score formula:** verify the mid-game score rules against real games (section 5.1).
6. **The search horizon:** to be decided by measurement (section 3.5).

## 9. Build order

1. Command numbering and state encoding, including the threat map, with tests (encoding round-trip, mask matches the legal moves).
2. `determinize()` with the baseline beliefs, plus the leak test.
3. The batched search, with a dummy network to test the plumbing, searching across turn boundaries with the horizon as a setting.
4. Recording (view, hidden truth) pairs from bot games, for the belief heads.
5. A first PyTorch model with all heads, and the bootstrap training.
6. The self-play loop.
