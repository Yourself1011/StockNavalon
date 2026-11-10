# stockNavalon: bot design

stockNavalon is the bot that polyengine, the game engine in this repository, exists for. This document collects the design discussed so far: what the network is, how the search uses it, how fog is handled, and the order to build things in. Nothing here is built yet, apart from the engine pieces it relies on (see [`cpp.md`](cpp.md) and [`python.md`](python.md)).

Status markers: **Decided** means agreed; **Proposed** is the current plan, still open to change; **Open** is a question still to answer.

## 1. Overview

stockNavalon follows the AlphaZero design, despite the Stockfish-like name: a tree search guided by a neural network.

|                  | Stockfish                                                | AlphaZero / stockNavalon                                                                                |
| ---------------- | -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Search           | alpha-beta, very deep, millions of positions per second  | Monte Carlo tree search (MCTS): fewer positions, guided by the network                                  |
| Network          | NNUE: a cheap evaluation, updated incrementally per move | a **value** head ("how good is this position?") and a **policy** head ("which moves are worth trying?") |
| Fits games where | there are few moves per position (chess: about 35)       | there are very many moves per position (Go: about 250)                                                  |

Polytopia is at the far end of that scale. A turn is 10 to 30 commands, each chosen from about 60 legal ones, so the number of possible turns is astronomical. Brute force gets nowhere; a policy that says which moves are worth looking at is essential. **Decided.**

The pieces:

1. **The network** (section 2): one shared trunk with policy, value and belief heads.
2. **The search** (section 3): MCTS using the PUCT rule.
3. **Fog** (section 4): the search runs on sampled versions of the hidden state. Stage 1 is plain determinization; stage 2 is POMCP/ISMCTS-style search with one shared tree and a particle filter.
4. **The belief model** (section 5): predicts the hidden state, including by reading the public score history, to make those samples realistic.
5. **Batched evaluation** (section 6): the search runs in C++ and asks Python for network evaluations in large batches.

## 2. The network

### One network, several heads (**Proposed**)

One shared trunk, with heads for policy, value and beliefs, rather than separate networks:

- **Cost:** every new position in the search needs both the policy and the value, and a shared network gives both in one forward pass.
- **Better learning:** the heads all need the same understanding of the position (threats, economy, who controls what), and auxiliary predictions like the belief heads usually help the main heads train.

The original AlphaGo used separate policy and value networks. Splitting is still possible later, for example if a much cheaper policy turns out to be useful.

### Input

The exact planes, global features and command slots are specified in [`encoding.md`](encoding.md). The first target is 11×11 (Tiny), 1v1, to find out quickly whether the architecture learns; the layout is built to grow to larger maps and more players.

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
- **Score features:** for each opponent, an exact split of their published score into what the player can account for and what they can't, plus their latest change ([`encoding.md`](encoding.md) section 4.1). The arithmetic is done by the engine, not left for the network to learn.

### Trunk

A convolutional ResNet, perhaps 6 to 10 blocks of 64 to 128 channels for a 14×14 map. Convolutions suit a grid, since a threat looks the same anywhere on the map. A transformer over tiles is the main alternative, if long-range structure like roads and ports matters more than expected. **Proposed.**

### Heads

- **Policy:**
  - convolutions output per-tile logits for the command slots (section 6.1), plus a pooled branch for global commands (research, diplomacy, End Turn)
  - illegal slots are masked out using the engine's legal moves
- **Value:**
  - pooled features into a small MLP, giving the expected result for the player to move
  - no draws: the result is +1 or −1 for the player to move ([`encoding.md`](encoding.md) section 8.1)
  - possibly an auxiliary score head later, if one bit per game turns out too sparse
- **Beliefs:** the hidden state (section 5.3).

## 3. The search

### 3.1 What each node stores

For each command `a` from a position `s`:

|                | Meaning                                              | From                    |
| -------------- | ---------------------------------------------------- | ----------------------- |
| `P(s,a)`       | prior: how promising the move looks before searching | policy head             |
| `N(s,a)`       | how many simulations went through this move          | search                  |
| `W(s,a)`       | the sum of values backed up through it               | value head, via back-up |
| `Q(s,a) = W/N` | average value found below this move                  |                         |

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

| Simulation | √N(root) | A: Q + U    | B: Q + U    | C: Q + U | Picked | Result                                                                  |
| ---------- | -------- | ----------- | ----------- | -------- | ------ | ----------------------------------------------------------------------- |
| 1          | 1        | 0 + 0.90    | 0 + 0.45    | 0 + 0.15 | A      | A evaluates to 0.2                                                      |
| 2          | 1.41     | 0.20 + 0.64 | 0 + 0.64    | 0 + 0.21 | A      | deeper, inside A; a move there evaluates to −0.1, so A: N = 2, Q = 0.05 |
| 3          | 1.73     | 0.05 + 0.52 | 0 + 0.78    | 0 + 0.26 | B      | A disappointed; B evaluates to 0.5                                      |
| 4          | 2        | 0.05 + 0.60 | 0.50 + 0.45 | 0 + 0.30 | B      | B gets deepened                                                         |

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
2. **Through turn boundaries.** The tree continues into the opponent's turn on a determinized game. Their moves are chosen with the same formula, using priors from _their_ view, and the value flips sign at each turn change.
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

### 3.6 Implemented: stage 1 (**Done, first version**)

`src/stockNavalon/search.h`:
- **`BatchedSearch`** runs many searches at once. Each decision gets `samples` determinized games (section 4.3), each with its own tree, and their root visits are added up. Every tree walks down until it needs an evaluation, and `pending()` hands all waiting positions to the evaluator together; `submit()` continues. `run(search, evaluator)` and `search(game, ...)` wrap the loop.
- **Nodes** keep their `Game` and `LegalMoves`. Selection is PUCT (section 3.2), with unvisited moves at `Q` = 0, and a child's legal list comes from its parent's.
- **Across turns:** the horizon (section 3.5) is a setting: the number of turn changes a line may cross before it's scored without looking further. Values are for the player to move, and flip when backed up past a turn change. A finished game is +1 or −1.
- **Determinization detail:** with a horizon past the player's own turn, samples match the opponent's score (`Detail::EVERYTHING`); within it, tiles and units are enough.
- **Self-play options:** Dirichlet noise on the root priors, and `SearchResult::choose` with a temperature.
- **Until there's a network,** `ScoreEvaluator` stands in: uniform priors, and the score lead squashed into [−1, 1].
- **Not yet:** tree reuse between decisions (a new determinization each time), and the Python side of the batched loop, which comes with the model.

**Tests** (`tests/stockNavalon/search_test.cc`): it takes a win on the spot at value 1; one turn further, it sees an unstoppable capture as a loss on every move, which the player's own turn alone can't; it finds the attack that stops that capture; and root visits and seeds behave.

**What the score evaluator shows** (`examples/search_vs_random`): against the random bot, search plays about as well as the one-ply greedy bot (ahead on score in most games by turn 20, rarely winning), at about 1 to 3 ms a decision. Two limits explain it, and both are the network's job:
- **The horizon effect within a turn.** With uniform priors, a turn's dozens of commands spread the simulations thin, and a position mid-turn is scored before the opponent has moved. So a line that hasn't ended its turn can't see a loss coming, and looks better than ending the turn, which does. The value head has to judge mid-turn positions knowing what the opponent can do next, and the policy has to narrow the turn.
- **No sense of attack.** The score rewards capturing a capital only once it happens.

## 4. Fog of war

### 4.1 The problem

The search needs a `Game` to apply commands to, and a `Game` contains everything: hidden tiles, enemy units in fog, the opponent's stars and techs. Searching on the real `Game` leaks information even though the network reads only `PlayerView`:

- a scout's move in the search reveals the _real_ tiles behind the fog, so the search learns "this move finds a village" before the bot could know it
- the opponent's replies are searched with their real units and stars

A bot trained that way learns habits based on information it won't have in a real game.

### 4.2 What hidden information matters when

| Hidden                                 | Matters within your own turn?                  |
| -------------------------------------- | ---------------------------------------------- |
| opponent's stars, techs, plans         | no: they only act on their own turn            |
| what's on tiles your moves will reveal | **yes**                                        |
| hidden enemy units (Cloaks)            | yes, rarely: moving onto one reveals it        |
| ruin rewards                           | yes, but that's chance, not hidden information |

Explored tiles show their live contents, including enemy units (GAME.md section 16), so everything on explored tiles is known. Within your own turn, fog comes down to what you'll find by exploring. Once the search crosses into the opponent's turn (section 3.5), their stars, techs and units matter too.

### 4.3 Stage 1: determinization (**Decided**)

1. **Sample a full game consistent with the player's view:**
    - explored tiles, visible units and your own state are copied exactly
    - unexplored tiles, units in fog, and the opponent's stars and techs are sampled (section 5)
2. **Run MCTS on the sampled game.** Revealed tiles show sampled contents, never real ones.
3. **Repeat with K samples** (say 4 to 8), adding up root visit counts across them.

The value head is trained on `PlayerView` against real outcomes, so it already estimates the result averaged over the hidden states consistent with the view.

**Known weakness: strategy fusion.** Each sampled search behaves as if it knew what's behind the fog, and plans that only work in that sample, like "explore here, then take the village there". Averaging across samples blurs this but doesn't fix it. Stage 2 (section 4.4) fixes it.

**Why stage 1 first:** it's the simplest way to get the whole pipeline working (network, batched evaluation, belief model, self-play) and to have a working bot. It's also the baseline stage 2 has to beat.

**Before any bot is judged or trained for real,** the search runs on determinized games only. Searching the real game is allowed just for getting the pipeline running, and must be clearly labelled as cheating.

### 4.4 Stage 2: one shared tree (**Decided**)

#### Where it comes from

POMCP and ISMCTS are both MCTS extended to hidden information:

|                                    | Information fully visible                  | Hidden information                     |
| ---------------------------------- | ------------------------------------------ | -------------------------------------- |
| **one agent** (a planning problem) | MDP: MCTS (UCT)                            | POMDP: **POMCP**                       |
| **several players** (a game)       | perfect-information game: MCTS (AlphaZero) | imperfect-information game: **ISMCTS** |

Both deal with hidden information the same way: sample a hidden state for every simulation, and share one tree across all of them. ISMCTS adds the opponent. In the opponent's part of the tree, their moves are chosen from what _they_ could know, not from the sampled truth.

stockNavalon is an imperfect-information game. With an own-turn search horizon (section 3.5) the opponent never moves inside the tree, so each search is effectively a POMDP and POMCP applies almost directly. Once the search crosses into the opponent's turn, it's ISMCTS.

#### What changes from stage 1

1. **One tree, a fresh sample per simulation.** Instead of K separate trees, every simulation starts by sampling a hidden state. This is the particle, drawn from the belief model through the filter below. The simulation then plays it out in the shared tree. The tree's statistics average over everything that might be behind the fog.
2. **The tree branches on what was observed.** A move that reveals tiles leads to different children depending on what it revealed. So "explore here" is judged by its average outcome, and later moves are planned per outcome: "if there's a village, take it; if not, do something else". This is what fixes strategy fusion.
3. **Observations are kept coarse.** One move can reveal several tiles at once, each with terrain, resources, units and buildings. Branching on the exact contents would give nearly every simulation its own child, each visited once. So:
    - **Coarse observation keys:** branch only on what matters for planning, like "found a village", "found enemy units", "found a ruin", "nothing important".
    - **Progressive widening:** a node only adds new observation branches as its visit count grows. This is how POMCPOW, POMCP's descendant, handles large observation spaces.
4. **The opponent's turn (if the horizon reaches it)** is chosen from the opponent's information, as in ISMCTS: their priors come from _their_ view of the sampled game, and their tree nodes are keyed by what they can see.

#### Particle filtering between turns

At the start of each turn the bot learns new hard facts:

- tiles revealed
- units seen
- every published score (section 5.1)

A particle filter enforces them exactly:

- **The belief model proposes** candidate hidden states.
- **The filter rejects** any that contradict a hard fact. For example, a candidate whose opponent couldn't have produced this turn's +300 is out.
- **Fresh candidates every turn:** the hidden state is too large for a fixed set of particles to survive many turns of evidence, so they're generated anew each turn rather than carried over. Rejected ones are simply replaced.

So the learned model and the filter divide the work: the model makes the guesses plausible, and the filter makes them consistent with everything known for certain.

#### Stage 1 choices that keep stage 2 from being a rewrite

- **`determinize()` must be cheap enough to call once per simulation,** not just once per tree. It's a copy plus some sampling, so it should be, but measure it when it's built.
- **Child nodes are looked up through one function,** keyed by the move in stage 1. Adding the observation to the key in stage 2 is then a local change.
- **The `Beliefs` interface stays as it is** (section 5.6). The belief model is stage 2's particle generator as-is; the filter sits between the two.

**Order:** stage 2 starts once stage 1 is working end to end. It's measured against stage 1 at equal compute, with the same network and beliefs, so the comparison is about the search alone.

### 4.5 Exploration and the value of information (**Decided**)

Will the bot explore as much as it should, like taking the Explorer reward or scouting with units? Not automatically. Exploring is worth two different things, and the architecture handles them differently.

1. **What you find:** villages, ruins, lighthouses, a meeting bonus, +5 score per tile.
2. **What you learn:** where their capital and army are, their tribe, the terrain between you. Information that improves later decisions.

**What you find** is handled fairly well. An Explorer reveals tiles at once, within the same turn, so the search sees its results directly. In a determinized game it reveals *sampled* terrain, with villages and ruins at sampled frequencies, so Explorer against Workshop is compared on the expected gains of exploring against +1 star a turn. The Explorer's path is random, and comes from the reseeded random state, so nothing real leaks. This is roughly right, provided the sampler puts villages and ruins in the fog at realistic rates.

**What you learn** is harder:

| | Stage 1 | Stage 2 |
| --- | --- | --- |
| what you find | roughly right, given a good sampler | right |
| what you learn, within the search horizon | **undervalued** | valued properly |
| what you learn, beyond the horizon | learned by the value head | learned by the value head |

- **Stage 1 undervalues information.** Each sampled tree acts as if it already knew what's behind the fog, so a line *without* exploring also plays as if it knew where the villages and the enemy are. Exploring then looks less useful than it is. It's the classic weakness of plain determinization, and the strategy fusion problem of section 4.3 showing up as concrete behaviour: too little scouting, and the Explorer undervalued.
- **Stage 2 values it properly within the search horizon.** The tree branches on what a move reveals and plans per outcome, so a move earns credit for the better follow-ups its information enables.
- **Beyond the horizon it's learned.** The value head reads only the view, so training teaches it whether being well informed (more map explored, the opponent's tribe and location known) tends to win. That depends on self-play games where information actually decides results, which needs opponents that exploit it.

**Making sure it's learned:**
- **The bot has to try exploring in training.** AlphaZero's usual training randomness (noise on the root priors, and sampling moves in proportion to visits early in each game) makes it sometimes take the Explorer or scout when it otherwise wouldn't. Without that, it can't learn what exploring is worth.
- **No bonus for exploring.** A reward like "+x per tile explored" would make it explore at whatever rate the bonus sets, not the rate that wins. The value has to come from results.

**Diagnostics to watch:**
- how often the Explorer reward is picked over Workshop, and scouting rates, over training
- a fixed set of test positions where scouting clearly pays, or clearly doesn't, and whether the bot gets them right
- stage 1 against stage 2 at equal compute, specifically on exploration choices, where the difference between them should show up most clearly
- the value head's accuracy, split by how much of the map is explored, which shows whether it understands what being uninformed costs

## 5. The belief model

### 5.1 Reading the score (**Decided**)

Every player's score is public. Others' scores update when their player ends a turn, and your own is live (GAME.md section 3). The engine records every published score in `Game::scoreHistory` (also in `PlayerView`).

Score is a fixed formula, so each change is a sum of known amounts:

| Change                                   | Likely cause                                        |
| ---------------------------------------- | --------------------------------------------------- |
| +100 / +200 / +300                       | a tech of tier 1 / 2 / 3                            |
| +5 per tile                              | exploring                                           |
| +20 per tile                             | territory gained                                    |
| +5 × unit cost                           | a unit trained (Warrior +10, Rider +15, Knight +40) |
| +5 per population, plus a level-up bonus | city growth                                         |
| +100                                     | a temple                                            |
| +400                                     | a monument                                          |
| negative amounts                         | losses                                              |

Pros read scores like this for the first turn or two, after which the possibilities multiply too fast. A bot can do it every turn for the whole game.

- **A published change covers everything since that player's last turn ended,** including what _you_ did to them in between: units you killed, cities you took. You know your own effects, so subtract them; the rest is what they did.
- **Evidence accumulates:** each turn's change is ambiguous on its own, but a tech, once bought, has to stay consistent with every later turn. Turn by turn, the possibilities narrow.

What it reveals:

- **techs:** the strongest signal; tier sizes are distinct, and a visible unit also proves its tech
- **stars:** from estimated spending and visible income
- **units trained in the fog:** +5 × cost
- **expansion:** exploration and territory gains

This only works if the engine's score formula matches the real game's exactly. The starting scores are verified; the mid-game rules (temple levels, population and level-up bonuses, losses) should be checked against real games before the bot relies on them. **Open.**

### 5.2 Ambiguity is expected (**Decided**)

The score never pins the opponent down exactly: many combinations of techs, cities, levels and units give the same number. The bot doesn't try to resolve this to one answer. It keeps a realistic spread of possibilities, and makes decisions that hold up across them:

1. **Many samples, not one.** Each call to `determinize()` (section 5.5) produces one possible opponent, and different seeds produce different ones. The search plays out many (K trees in stage 1, one per simulation in stage 2), so its statistics average over them.
2. **The belief model sets the proportions.** If a +200 could be one tier-2 tech, two tier-1 techs, or a tier-1 tech and 20 explored tiles, and the model puts those at 60%, 25% and 15%, the samples come out in those proportions. What matters is that the proportions are right (calibration), not that one answer is.
3. **The published score rules samples out.** It's one exact number, and every sample must match it (section 5.5, step 8).

Why that's enough:

- **Most of the uncertainty doesn't change the right move.** Two or three cities, 14 or 17 stars, rarely matter this turn. What matters is usually a few specific facts (can they reach this city? do they have Archery?), and the samples cover those in the right proportions.
- **The value head was trained on the same uncertainty.** It never sees hidden state, so it already estimates outcomes averaged over what the opponent might have.
- **Uncertainty shrinks:** every unit seen proves a tech, every explored tile removes a guess, every published score narrows the options.

### 5.3 What the model predicts

The belief heads are part of the shared network (section 2). They output probabilities directly:

| Target                     | Output                                                                                          | Loss                                                                                                        |
| -------------------------- | ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| opponent's tribe, unmet    | a softmax over tribes                                                                           | cross-entropy                                                                                               |
| opponent's techs           | a sigmoid per tech                                                                              | binary cross-entropy                                                                                        |
| opponent's stars           | a distribution over counts: a mixture of negative binomials, no bins ([`encoding.md`](encoding.md) section 8) | negative log-likelihood                                                                                     |
| **where hidden units are** | one plane: per tile, the chance a hidden enemy unit (any type) is there                         | binary cross-entropy, only on tiles the player can't see, with extra weight on the rare tiles that have one |
| **what type each is**      | per tile, a softmax over unit types: "if there's a unit here, it's 50% Warrior, 30% Rider, ..." | cross-entropy, only on tiles that have a hidden unit                                                        |
| **how many hidden units**  | a softmax over the total, 0 to a cap                                                            | cross-entropy                                                                                               |
| enemy cities in fog        | per tile, the chance an enemy city is there, plus its level                                     | binary cross-entropy / cross-entropy                                                                        |
| unexplored tiles           | per tile, a softmax over terrain, resource, village/ruin/none                                   | cross-entropy, on unexplored tiles only                                                                     |
| opponent's exploration     | a softmax over how many tiles they've explored, 0 to the map's tiles                            | cross-entropy                                                                                               |

**Why "where" and "what type" are separate planes,** instead of one plane per unit type:

- **Sparsity:** any single unit type is hidden on very few tiles, so a per-type plane is nearly all zeros, and rare types (Knights, Mind Benders) give it very few examples to learn from.
- **Dense signal:** the single "is a unit here" plane learns from every hidden unit of every type. It's also what the search cares about most: where the threats are.
- **Shared type knowledge:** the type softmax only has to be learned where units actually are, and shares what it learns across tiles ("early on, near their capital, usually a Warrior or Rider").
- **Boats need no special case:** on water, only boat types make sense, and the softmax learns that.
- **No grouping:** grouping unit types was considered and dropped. Most groups would hold one or two types, and grouping loses distinctions that matter, like a Defender against a Swordsman.

The "where" and city planes amount to a heat map of the opponent in the fog. A convolutional trunk suits it: where units are depends on what's near them (their cities, roads, paths between territories, terrain they can't cross). The score features move it too: a +15 last turn means a Rider was probably trained, so more heat around their cities.

**First version (**Proposed**):** techs, stars, and the hidden units (where, what type, how many). Unexplored terrain and fogged cities stay with the baseline (section 5.6), since terrain is the hardest to sample well and matters least within a turn.

### 5.4 How it learns (**Decided**)

#### Supervised, from games where the truth is known

```
input:  the player's view (encoded, with the score features and threat map)
answer: the opponent's real techs, stars, units and cities; the real unexplored terrain
```

In any game the engine knows the full truth, so every position is a labelled example.

The **inputs** are:

- the same encoded view as the other heads
- the score features: per opponent, their explained and unexplained score and their latest change ([`encoding.md`](encoding.md) section 4.1). The engine does the exact arithmetic, and the exact reasoning over the full history happens in the score check and particle filter (section 5.5), not in the network. The network's job is which explanations are likely, not making the numbers add up.
- if that turns out to be too little history, a small sequence model over the full score history can be added later

**Why cross-entropy gives honest odds:** the prediction that minimises it is the true frequency. If in 70% of similar-looking situations the opponent really had Archery, the network is pushed towards exactly 70%. It's trained to be calibrated, not to guess the single most likely answer, which is what the sampler needs.

#### Making it reflect good players

The network learns the hidden states that **the players in its training data** tend to be in. Train it on random-bot games and it learns that opponents buy random techs and disband their units. So "predict what a good player has" means "train on games with good players in them":

1. **Bootstrap data from simple bots** (random, one-ply): only to get started and to test the pipeline. Its beliefs are about weak players.
2. **Self-play, refreshed as the bot improves:** the main source. The belief heads train on the same self-play games as the policy and value heads, from a rolling window of recent games. As stockNavalon gets stronger, so do the opponents in its data, and old data from weak versions ages out.
3. **Real games**, if available: human game records, or full information revealed after a game. The best labels there are; even a few hundred games could fine-tune the beliefs towards real opponents.

#### Not assuming everyone plays like the bot

Pure self-play learns what _the bot_ would have in each situation, so against a different style (a rush, when the bot always builds economy) its beliefs would start out confidently wrong. Three fixes:

- **A varied opponent pool:**
  - past versions
  - versions trained with different settings
  - simple scripted styles (rusher, turtle, expander)
- **Let the history adapt it within a game.** Trained on varied opponents, the network learns to read style from the score features and what it has seen. A +15 every early turn looks like a Rider rush, and its predictions shift mid-game.
- **Check calibration per opponent style,** so a blind spot shows up before it costs games.

#### What it doesn't capture: how facts relate

The heads predict each fact separately ("70% Archery", "60% three or more cities"), not how facts relate ("with Archery, probably no Climbing yet"). Sampling (section 5.5) covers the important relationships:

- it respects the tech tree
- it forces in proven techs
- it rejects combinations that don't fit the score

If that's not enough, the next step is a head that samples one fact at a time, each conditioned on the ones already chosen: techs in tree order, then units given those techs. Only if the simple version's samples prove wrong in ways that matter.

### 5.4.5 Combining the exact side with the belief model (**Decided**)

The exact side (tribe inference, proven techs, the score arithmetic) decides what's *possible*. The belief model decides how *likely* each possibility is. They meet at three points:

```
public information (view, score history, climates)
        │
        ├──► exact side: possible tribes, proven techs, explained/unexplained score
        │         │
        ▼         ▼
   network input ◄── 1. exact results as input features
        │
        ▼
   belief heads ──► 2. mask: anything ruled impossible gets 0, the rest renormalized
        │
        ▼
   sample a hidden state ──► 3. filter: tech tree, forced techs, the exact score check
        │                        (reject and redraw anything inconsistent)
        ▼
   determinized game ──► search
```

1. **Input:** the exact results are network input features: the per-opponent "could be this tribe" flags, and the explained and unexplained score ([`encoding.md`](encoding.md)). The belief model starts from "Vengir or Ai-Mo, 640 points unaccounted for" instead of rediscovering arithmetic. Features alone guarantee nothing, though.
2. **Mask:** before sampling, the belief heads' outputs are restricted to what the exact side allows:
   - **tribes:** tribes outside the possible set get 0, and the rest are renormalized
   - **techs:** proven techs are fixed at 1, and techs shown impossible at 0
   - **units:** tiles the player can see are masked out of the "where" plane

   Impossible things are then never sampled, however wrong the network is. **Training uses the same mask:** the loss is on the masked, renormalized distribution, so the network only learns to weight the possibilities. Since the exact side never rules out the truth, the true answer is always inside the mask, and the loss is always well-defined. That's another reason the soundness tests matter.
3. **Filter:** the heads predict facts separately, so a sample can still combine facts that don't fit together. Determinization enforces that:
   - techs are sampled down the tree, with forced techs included
   - the sample must match the published score exactly (section 5.5, step 8); if it doesn't, it's rejected and redrawn
   - **after many rejections in a row** (say 50), it falls back to a looser sample from the exact side alone, and counts it. A high fallback rate means the beliefs are badly off.

| | Exact side | Belief model |
| --- | --- | --- |
| decides | what's possible | how likely each possibility is |
| guarantees | never rules out the truth | none on its own |
| used as | input features, output mask, sample filter | probabilities to sample from |

**Later:** the exact search can list the actual explanations of the opening, and the policy network can weight each one by how likely a strong player is to have played it. That would replace the belief model's tribe and early-tech probabilities with something more direct, through the same mask and filter.

### 5.5 Determinization: from beliefs to a game (**Proposed**)

```cpp
Game determinize(const Game &real, int player, const Beliefs &beliefs,
                 std::uint64_t seed, Detail detail);
```

The goal: a complete, valid `Game` that agrees exactly with everything the player knows, and in which everything else was sampled.

#### Copy, wipe, fill, repair

1. **Copy** the real game.
2. **Wipe** everything the player doesn't know, driven by an explicit list of hidden fields. This is the risky step: anything not wiped leaks, which the leak test (section 5.7) catches.
3. **Fill it back in** from the beliefs, using a random generator seeded with `seed`.
4. **Repair** the engine's bookkeeping, so the result is a valid game.

#### What's known and what's sampled

|                   | Known: kept exactly                                                            | Hidden: wiped and resampled                                                                     |
| ----------------- | ------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------- |
| **Tiles**         | every tile the player has explored, with its live contents                     | unexplored tiles: terrain, resource, village, ruin, road, building, territory                   |
| **Units**         | units on explored tiles except hidden ones; all the player's own units         | enemy units on unexplored tiles; hidden Cloaks anywhere, explored tiles included                |
| **Cities**        | cities on explored tiles (level, population, wall, owner)                      | enemy cities in fog: location, level, borders                                                   |
| **The player**    | all of it                                                                      | none                                                                                            |
| **Each opponent** | alive, the published score history (including their starting score), peace and embassies with the player, their tribe once met | their tribe until met, stars, techs, tasks and monuments, kills, which tiles they've explored, ties with third parties |
| **The game**      | turn, whose turn, map size, corner lighthouses                                 | **the random-number state**                                                                     |

**The random-number state** decides future ruin rewards and Explorer paths. A copy that kept the real one would let the search know which ruin gives a free tech. It's always reseeded.

#### Filling in, in order

Later steps depend on earlier ones:

1. **Enemy cities.** Their capital is copied if it's been seen (Capital Vision included), otherwise sampled. Then the number of other cities, their locations, and each city's level are sampled, with borders claimed over unexplored tiles only. Explored tiles keep the ownership the player sees.
2. **Unexplored terrain:** terrain and resource per tile, villages and ruins on legal, spaced-out tiles.
3. **The tribe**, if not met yet: sampled from the belief model, among the tribes still possible. That set is worked out exactly from their starting score and published score changes, and from the climates seen ([`tribeInference.md`](tribeInference.md)). A starting score of 730 means Vengir or Ai-Mo, 515 one of five tribes (GAME.md section 3). The tribe fixes their starting tech and unit, so it comes before techs.
4. **Techs:** sampled down the tech tree, parents before children, starting from the tribe's starting tech. Techs the player has proof of are forced in, with their ancestors (owning a tech means its parent chain was owned, back to a tech already held). A proof has to be certain, since nothing can outweigh it. Anything merely likely is left to the belief head, which learns from games how often each explanation happens.
    - **Their units** prove a tech once more of them need it than other explanations can cover. A unit can come without the tech in two ways. It can be converted from the player, who knows exactly which units they lost (`PlayerView::convertedAway`). Or it can be a ruin's soldier: only a veteran Swordsman, or a Rammer carrying a veteran Warrior, and no more of them than the ruins they could have reached (the map's ruins less those the player still sees). So a veteran Archer proves Archery (it was promoted by kills), and more veteran Swordsmen than ruins prove Smithery.
    - **A building in their city** proves its tech, since buildings need their builder's own territory. The exception is a city the player might once have held (they've stood on it).
    - **A road the player didn't build** proves Roads (a bridge too), on a map that started without roads. The player knows which roads are theirs (`TileView::ownRoad`).
    - **With three or more players,** none of these prove anything: a unit could be converted from a third player the player has never seen, a city captured from one, a road or a hidden unit be theirs. Those cases are left to the beliefs.
    - **A monument in their own city** proves its task, and Genius, Wealth and Pacifist prove their techs.
    - **A hidden-unit warning** proves the tech for hidden units, in 1v1 only.
5. **Hidden units:**
    1. Remove every enemy unit the player can't see.
    2. Sample how many hidden units there are, from the count head.
    3. Place each one on a tile picked in proportion to the "where" plane, without reusing a tile, only on legal tiles: terrain the unit can enter, not visible to the player, not occupied. Cloaks may go on explored tiles, since they're invisible there.
    4. Sample each unit's type from that tile's type softmax, restricted to types whose tech the sample owns.
6. **The rest:** stars from their distribution, tasks consistent with the techs and units, and which tiles they've explored (around their cities and units at first, grown to fit the score in step 8).
7. **Repair:**
    - each tile's unit and territory set to match
    - city connections recomputed
    - unit caps respected (level + 1 per city)
    - the random-number state reseeded
8. **Score check, with exploration as the slack:**
    1. Every part of the score is a multiple of 5, and explored tiles (+5 each) are the most flexible part.
    2. Work out what's left: `leftover = published score − (techs + cities + units + territory + temples + monuments)`.
    3. Set their explored tile count to `leftover / 5`.
    4. If that's impossible (negative, fewer tiles than their territory and units already need, or more than the map has), reject the sample and draw another.

    Every accepted sample then matches the published score exactly, and explanations that only fit the score awkwardly get rejected more often. The comparison uses the state at the end of their last turn, so anything the player did to them since (kills, captures) is taken out first.

    **The rejection rate is a health check:** if most samples get rejected, the beliefs are off. Later, it can be a training signal too.

#### Level of detail

In stage 2, `determinize()` runs once per simulation, so it only samples what the search will touch:

- **Tiles and hidden units only:** enough for own-turn search, which never reads the opponent's stars, techs or private state. Those stay at neutral values that nothing looks at.
- **Everything:** needed once the search crosses into the opponent's turn.

#### Implemented: the first version

`src/stockNavalon/determinize.h`: `determinize(real, player, beliefs, seed, detail)`, with `priorBeliefs(view)` standing in for the network until there is one. It follows the steps above, with these simplifications for now:

- **Two-player games only.**
- **Unexplored tiles** come from a freshly generated map of the same type and tribes, ignoring what's been seen: a lake cut off by the fog doesn't continue into it, and villages near the explored edge can break the spacing rules. The tiles at the fog's edge are the ones the search reveals next, so this matters; the unexplored-terrain belief head (section 5.3) is the proper fix. Their climate is the nearest capital's, by the engine's own rule (`Game::nearestCapitalSeat`).
- **Enemy cities in the fog:** the capital is placed where their starting score allows (`possibleCapitalTiles`, from tribe inference). Other fogged cities stay only if the player has seen some of their borders, placed within reach of those. Cities with no evidence are dropped, until there's a city head. A fogged city's level is a rough prior, and its population and parks start at 0.
- **Hidden units:**
  - **On explored tiles** (the Hide skill: Cloaks, Dinghies), the player only gets a warning on their own units next to one. They're resampled under exactly that: a hidden unit next to every warned unit, none next to an unwarned one. A warning also proves the tech for hidden units. Hidden units next to none of the player's units aren't sampled, since nothing shows them.
  - **In the fog:** any type their techs allow on the tile's terrain, and at sea the boat a land unit would be in (a Raft or an upgrade they could have), weighted by the beliefs.
- **Fitting the score:** when the sample's score is further below the published one than exploring every remaining tile could make up, researchable techs are added, weighted by the beliefs, until exploration covers the rest. Without that, the prior's techs missed by far mid-game, and most draws were rejected.
- **Not repaired yet:** city connections aren't recomputed, and unit caps aren't checked.
- **Canonical order:** units and cities are renumbered by tile, so the sample carries nothing of the order things happened in.

**Tests** (`tests/stockNavalon/determinize_test.cc`), on random games of every map type:
- **Valid and consistent:** about 180 samples encode exactly like the real game from the player's side, match the opponent's published score, pass the engine's invariant checks, and survive 100 random commands each.
- **No leaks:** pairs of games differing only in hidden things (the opponent's stars and techs, unexplored terrain, a unit moved within the fog) give identical samples from the same seed. So do two games with a hidden Cloak on different tiles next to the same warned unit, and the warning stays in the sample.

**Speed:** 60 to 250 µs a call, rejected draws included. Fine for stage 1's few samples per move. Stage 2 also evaluates the network once per simulation, which is likely the same order of time, so this isn't a bottleneck, but most of it is waste: a fresh map is generated for every draw.

**Rejection rate with the prior:** about 60% of draws, mostly on turn 0, where the prior's hidden units outscore a starting position. The parts are drawn independently and then checked against the score. Drawing them within the remaining score budget, as already done for techs when the score is too low, would make rejections rare.

#### How hidden units relate to the threat map

- **The threat map** (a network input) is computed exactly from visible units.
- **Hidden units** are estimated by the heat map, and become real units in the sampled game. They threaten tiles like any other unit, and the search sees their attacks itself, with no hand-made conversion.
- **A possible later refinement:** an "expected hidden threat" input plane built from the heat map, so the value head accounts for unseen units without search. Only if there's evidence it's blind to them; it costs a second network pass.

### 5.6 Engine interface and the no-model baseline (**Proposed**)

```cpp
struct Beliefs {   // the model's output, in engine terms
    std::array<float, TechTypes::TECH_TYPE_SIZE> opponentTech;
    std::vector<float> starBuckets;
    std::vector<float> hiddenUnitCount;   // distribution over the total
    std::vector<float> unitHere;          // per tile
    std::vector<std::array<float, UnitTypes::UNIT_TYPE_SIZE>> unitType;   // per tile
    std::vector<float> cityHere;          // per tile, plus levels
    // per tile: terrain and resource distributions
};

enum class Detail { TILES_AND_UNITS, EVERYTHING };
```

A baseline `Beliefs` needs no model. The search, determinization and tests can all be built on it before any training, and the learned model then replaces it one part at a time behind the same interface.

| Hidden             | Baseline                                                                                           |
| ------------------ | -------------------------------------------------------------------------------------------------- |
| unexplored terrain | the map generator's rates, per climate region                                                      |
| villages and ruins | the generator's spacing rules and counts for the map size                                          |
| enemy cities       | an unseen capital in the quadrant opposite the player's; other cities on plausible village spots   |
| techs              | the start tech, everything proven by what's been seen, and techs whose costs fit the score changes |
| stars              | estimated income minus estimated spending                                                          |
| hidden units       | a few per city, near their cities                                                                  |

### 5.7 Tests (**Decided**)

Both run over thousands of games from the existing bots, on every map type, like the fuzz and `LegalMoves` tests:

1. **No leaks, as an invariance test.**
    1. Take a real game, and a second version differing only in hidden things: an enemy unit moved within the fog, different opponent stars, different unexplored terrain.
    2. Determinize both with the same seed.
    3. The results must be identical.

    This tests the property that matters ("the output depends only on what the player knows") directly. It catches leaks that a view comparison would miss, like the random-number state or the opponent's exploration.

1. **Valid and consistent.**
    - The player's view of the sample equals their view of the real game, tile by tile and unit by unit.
    - The sample passes the fuzz test's internal checks.
    - Random commands played from it never crash or break those checks.

### 5.8 How to tell it works

- **Calibration:** bucket predictions on held-out games (all the "70%" ones) and check how often they were right. Check separately per opponent style.
- **Against the baseline:** accuracy on techs, stars, units, cities and terrain.
- **The rejection rate** of the score check (section 5.5).
- **Playing strength:** the same search with learned beliefs against the search with baseline beliefs. This is the test that counts.

## 6. Plumbing

### 6.1 Command numbering (**Proposed**)

The full slot layout is in [`encoding.md`](encoding.md) section 5. The outline:

The policy head needs every possible command to have a fixed slot number. Most slots are per tile:

| Per tile        | Slots         | Covers                                                                      |
| --------------- | ------------- | --------------------------------------------------------------------------- |
| target offset   | 13 × 13 = 169 | Move, Attack, Disembark, by target offset up to 6 tiles (a knight on roads) |
| unit actions    | about 7       | Recover, Heal Others, Promote, Disband, Capture, Examine Ruins, Harvest     |
| upgrade         | 3             | Scout, Rammer, Bomber                                                       |
| build / destroy | about 32      | each improvement type, and Destroy                                          |
| train           | 9             | each unit type                                                              |
| city reward     | 2             | option A or B                                                               |

Plus global slots: research (about 25), diplomacy (6 per opponent), End Turn.

- **One set of target slots for three commands:** a move goes to an empty tile, an attack to an enemy, and a disembark from a boat onto land, so for a given unit the same offset never means two things.
- **Size:** 218 slots per tile, plus 32 global ones: 26,410 on the first target's 11×11 map. Only the legal ones (usually about 60) count, through a mask from the engine.
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

### 6.3 What the game shows, and what the player remembers (**To do**)

Some `PlayerView` fields aren't on the real game's screen: they're the player's memory. In self-play the engine fills them in directly, which matches a perfect memory. Playing the real game, the bot has to keep them itself:

| Field | What it is | Kept from |
| --- | --- | --- |
| `tiles[t].ownRoad` | roads the player built | their own Build commands |
| `scoreHistory` | every published score | the scores, read after every turn |
| `effectsSincePublished`, `effectsInLastChange`, `effectsInChanges` | what the player's actions did to others' scores | their own kills and captures, as seen |
| `attackedOnTurns` | turns they were attacked | HP their units and cities lost on the opponent's turn |
| `convertedAway` | their units taken by conversion | one of their units turning into the opponent's on the same tile |
| `firstOccupied` | the first turn their units or cities stood on each tile | their own units' positions after each command |

Task progress (kills, peaceful turns, lighthouses found) and units' kill counts are shown by the game, so they aren't memory.

**Plan:** move these into a `PlayerMemory` part of the view, with an `updateMemory(memory, previous view, view, own commands)` built only from what the player sees and does. A test then plays self-play games and checks, after every command, that this memory equals what the engine gives. That proves the bot can keep it up in the real game, and catches any memory field that secretly needs hidden information.

## 7. Training (**Proposed**)

1. **Plumbing:** the batched search with a dummy network (uniform priors, value 0), against the random bot, on determinized games with the baseline sampler.
2. **Bootstrap:** before self-play, train the value and belief heads on games from simple bots (random, one-ply). Pure self-play from scratch can take a long time to get started.
3. **Self-play:** the full loop. The search produces visit counts and results, and the network trains on them: the policy towards the visit counts, the value towards the results, and the beliefs towards the true hidden state.
4. **Measure:** the search horizons against each other (section 3.5), and learned beliefs against the baseline (section 5.8).
5. **Stage 2 search** (section 4.4), measured against stage 1 at equal compute.

**Long games:** the rules have no turn limit, so self-play stops at 50 turns on Tiny maps. A game cut off there gets its value from the final score lead, squashed like the score evaluator's (`tanh(lead / 500)`).

### 7.1 Recording games (**Done**)

`src/stockNavalon/record.h` and `selfPlay.h`, with Python access (`pe.play_games`, `save_records`, `load_records`, `Encoding.encode_examples`):
- **A record is a game's setup and commands,** not encoded positions: the map settings, the game seed, every command, and the search's root visits for the ones it chose. The engine is deterministic, so replaying rebuilds every position exactly, and records outlive encoding changes. Positions are encoded when training samples them.
- **Self-play:** the search on both seats, random tribes and map types, root noise, and visits-proportional choices for the first 3 turns, then the most visited. Games run in parallel threads.
- **Examples:** `encodeExamples(records, positions)` gives, for each (record, move), the position just before that move for the player to move: the input, the legal mask, the policy target (visits, normalized), the value, and the belief targets with their masks.
- **Training on them:** a replay buffer of recent games, random minibatches across them, so neighbouring positions of one game don't dominate a batch.

**Measured** on this machine (Apple Silicon, 10 cores), 100 simulations × 2 samples: about 2,700 positions a second of self-play on 8 threads; encoding 256 examples takes about 36 ms. A record is about 180 KB, mostly root visits (every root command, since uniform priors visit nearly all of them). With the score evaluator, almost every game reaches the turn limit.

### 7.2 The network and the training loop (**Done, first version**)

`python/stocknavalon/` (PyTorch; `uv pip install -e ".[train]"`):
- **`model.py`, the network:** a 3×3 convolutional ResNet, 6 blocks of 64 channels by default (about 0.6M parameters), with the global vector added as a per-channel bias, and global pooling (the board's mean and max as a bias) in two blocks. The policy is per-tile 1×1 convolutions plus a pooled branch for the global slots; the value is pooled features into a small MLP. The belief heads are those of [`encoding.md`](encoding.md) section 8, pooled or per tile.
- **`train.py`, losses:** cross-entropy of the policy against the visits, over legal slots; weighted squared error for the value; each belief head over its mask (the stars by the negative log-likelihood of a mixture of 3 negative binomials). The belief heads share a weight of 0.25 together.
- **The value target** is mostly the result, with a small score term: `(1 − w) × result + w × tanh(final score lead / 500)`, where a game cut off by the turn limit counts as a draw (0). `w` is 0.1 (`--score-weight`) while few games end in a capture, fading to 0 as captures become the norm (from 10% to 60% of the buffer's games decided; `score_weight_for`). The small score term breaks ties, but a lead at the limit is worth about +0.08 against a win's +1, so winning stays worth playing for. (The first run started from the score lead alone, with capped games weighted by it: being ahead at the limit looked nearly as good as winning, and the share of games ending in a capture fell from 22% to 13% in five iterations.)
- **The replay buffer** is in C++ (`pe.ReplayBuffer`): it holds the games once, samples positions across them, and encodes minibatches. Keeping the games on the Python side would copy every record into C++ on every batch.
- **Self-play with the network** (`selfplay.py`, `pe.SelfPlaySession`): many games at once in C++; all their waiting positions go to the network in one batch, and Python only runs the model. The softmax of each node's moves' logits gives its priors (`evaluationsFromNetwork`).
- **`run.py`, the loop:** bootstrap games with the score evaluator until the replay buffer is full (more would only push earlier ones out). Then self-play and training run at once, in two threads, with no iterations:
  - **Self-play** keeps its games going continuously with its own copy of the network (a model mid-update can't be read, and the two need different BatchNorm modes), adds each finished game to the buffer and the record file, and takes the trainer's weights every `--sync-steps` (200).
  - **Training** draws minibatches continuously, throttled to `--reuse` (9) samples per position self-play has added: it waits rather than overtrain, and the reuse stays what it's set to whatever the two speeds are.
  - **Sharing the GPU:** a lock around each thread's GPU work, since MPS has had trouble with two threads submitting at once; the CPU work (games, encoding, the C++ search) still overlaps freely.
  - **The buffer** is safe across threads (C++ lock), and training samples and encodes in one step, since adding games can drop old ones and shift the indices.
  - **Record files** hold only what the buffers do: `records.txt` and `holdout.txt` are rewritten from the buffers at every checkpoint (aside, then swapped in), so they stay around the buffer's size instead of growing by gigabytes a day. Old games aren't needed once trained on; the checkpoints keep what they taught.
  - A status line every minute: training steps a minute, milliseconds a step, how much training waited, games a minute, positions a second, recent captures, the reuse so far, and the losses. A checkpoint every 10 minutes, and on Ctrl-C or `--minutes`; running again resumes.
- **Measuring progress** (`evaluate.py`): losses on self-play data can't say whether the network plays better, since it makes its own data. So, as a third thread, every `--eval-minutes` (60):
  - **Evaluation matches:** the latest network (kept in `checkpoints/step_N.pt`) against the random bot and the score-evaluator search (fixed anchors, so progress is absolute) and against the network from the last evaluation (relative progress). 64 games each by default, in pairs with the same map and tribes and the seats swapped; both sides search the same way, without root noise; a game cut off by the turn limit goes to the side ahead on score. No gating: the latest network always plays on. In C++, a session's two sides can each be a network, the score evaluator or the random bot (`SelfPlayConfig::sides`, `paired`).
  - **Held-out loss:** one finished self-play game in 20 (`--holdout-every`) goes to `holdout.txt` instead of training, and each status line has the losses on a fixed sample of them. Rising while the training losses fall would mean overfitting.
- **`measure.py`:** network speed by batch size, and self-play speed with the network.

**Measured on MPS** (Apple Silicon): the network evaluates about 20,000 positions a second from batches of 64 up; self-play with it about 10,000. The first run's iterations took 30 to 50 minutes: the network's games run to about 1,000 decisions, twice the bootstrap's, and each training step spent nearly all of its ~290 ms replaying games on the CPU. What came of profiling it:
- **The C++ side isn't the bottleneck:** one thread supplies about 49,000 positions a second. Spreading a session's games over threads made it slower (starting threads every batch costs more than the work), so a session defaults to one.
- **Continuous self-play:** game lengths are heavy-tailed, and waiting for a whole set of games to finish spent most of an iteration on the last few, with tiny batches (128 games: 94 done in 10 minutes, the last few taking another 20). Now self-play keeps its games going across iterations: each iteration takes the first games to finish, a finished game's slot starts a new one at once, and unfinished games carry on with the next network.
- **CPU and GPU overlap:** self-play splits its games between two sessions that take turns. While the GPU evaluates one session's batch (PyTorch on MPS runs asynchronously until results are copied back), the CPU advances and encodes the other's. Run one after the other, the GPU sat idle about half the time.
- **Only the moves' logits leave the GPU:** each position's move slots are gathered on the GPU (`pending_slots`), a few KB a batch instead of every slot of every position, about 10 MB.
- **Playout cap randomization** (KataGo): a random 25% of decisions get the full search with root noise and are recorded for the policy; the rest a quick 25-simulation search, only to play on. That's 83 evaluations a decision on average instead of 188.
- **Training batches:** a game is replayed once for 4 positions sampled from it (`sample(per_game=4)`), replays skip the legality check, games are replayed in parallel, and the next batch is encoded in the background while the GPU trains. 309 ms a batch became about 30 ms, off the GPU's critical path.

## 8. Open questions

1. **Compute:** what will training run on (one GPU, several, Apple Silicon)? That decides network size and how much self-play is affordable.
2. **The goal:** a strong bot under a time limit per move, eventually playing the real game? Or mainly a research platform?
3. **Human games:** are any records available? Supervised pre-training on them would make the bootstrap much easier.
4. **Map size:** decided: start at 11×11 (Tiny), 1v1, padded to a fixed `N` that can grow later ([`encoding.md`](encoding.md)).
5. **The score formula:** verify the mid-game score rules against real games (section 5.1).
6. **The search horizon:** to be decided by measurement (section 3.5).
7. **How to predict counts** (stars, hidden units, exploration): to experiment with. The first version uses exact integers with a cap, and a mixture of negative binomials for stars; fixed-width buckets and a single negative binomial are the alternatives. Compare by held-out log-likelihood and by how realistic `determinize()`'s samples are ([`encoding.md`](encoding.md) section 8.3).

## 9. Build order

1. ~~Command numbering and state encoding, including the threat map, as specified in [`encoding.md`](encoding.md), with its tests.~~ **Done.**
2. ~~`determinize()` with the baseline beliefs, plus the leak test.~~ **Done**, as a first version (section 5.5). Not yet fast enough to call per simulation (section 4.4).
3. ~~The batched search (stage 1), with a dummy network to test the plumbing: across turn boundaries, with the horizon as a setting.~~ **Done**, with the score evaluator standing in (section 3.6).
4. ~~Recording games, for every head's training targets.~~ **Done** (section 7.1).
5. ~~A first PyTorch model with all heads, and the bootstrap training.~~ **Done** (section 7.2).
6. ~~The self-play loop.~~ **Done** (section 7.2); next is running it, and measuring it on MPS.
7. Stage 2: one shared tree with observation branching, and the particle filter between turns.
