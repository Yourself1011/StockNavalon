# Design notes

Designs not built yet, and the reasoning behind decisions. What the code does now is in [`docs/stockNavalon.md`](../docs/stockNavalon.md); the checklist is [`todo.md`](todo.md), run history is [`runs.md`](runs.md), and the real-game plan is [`integration.md`](integration.md).

Contents:
1. [Training plan](#1-training-plan): mostly built; the reasoning, and what's still to build
2. [Fog: stage 2 search](#2-fog-stage-2-search)
3. [Exploration and the value of information](#3-exploration-and-the-value-of-information)
4. [The search horizon](#4-the-search-horizon)
5. [The belief model](#5-the-belief-model)
6. [Determinization: the full design](#6-determinization-the-full-design)
7. [Player memory](#7-player-memory) (not needed for now)
8. [Smaller alternatives](#8-smaller-alternatives)
9. [Planning turns ahead: a redesign](#9-planning-turns-ahead-a-redesign) (brainstorm; turn search built)
10. [The next run](#10-the-next-run): what the restart should get right
11. [Transpositions](#11-transpositions): one node per position, for a deeper search (next)

## 1. Training plan

The second run improved for 10,000 steps, then got worse twice ([`runs.md`](runs.md)), mostly from design choices: a weak value signal that a fading score weight turned into a feedback loop, nothing stopping a regression from feeding on itself, and thin searches. A to E below answered them; all but D are built and described in [`docs/stockNavalon.md`](../docs/stockNavalon.md) sections 5 and 6. The reasoning that still guides changes:

- **The value is about winning only** (A). No score term and no schedule: even a down-weighted score rewards being ahead at the limit. A schedule that follows a live measure of play can loop, as the score weight did (captures rise, weight drops, play degrades, weight returns); anything scheduled follows training steps, with a measure of play at most latching a start once.
- **A game cut off is truncated, not drawn** (Pardo et al. 2018, "Time Limits in Reinforcement Learning"): its positions take an estimate of the final position. The stalemate cutoff counts from the first fight; score was rejected as the stall signal, since it keeps creeping up without fighting. A generous window costs only compute.
- **Side heads** (B), never read by the search, change what the trunk understands, not what the bot plays for (KataGo's ownership and score heads).
- **Self-play only plays the best network** (C): EMA weights, and gates against the best rather than the last candidate, so a slow decline can't get through; judged on decided games, cut-off ones counting for neither side, so score never enters. Several failed gates in a row are a diagnostic too.
- **A curriculum by map type** (D): dropped. On 11×11 maps land-heavy types aren't more decisive, and restricting types costs generality.
- **No fog first, then fog** (E, Suphx's oracle guiding): run to 100%. Only information changes, never the rules.

### Gumbel search (built)

Danihelka et al., "Policy improvement by planning with Gumbel" (ICLR 2022): at thin budgets (64 simulations over about 60 legal moves) AlphaZero's root mostly echoes the prior, while Gumbel's sampled candidates, sequential halving and completed-Q policy target still improve on it. The default; docs/stockNavalon.md section 3.

### Value targets from the search (built as the λ-return)

After 35,885 in the ninth run the value got overconfident: every position of a decided game has the same ±1 target, turn 5 and turn 45 alike. Built as a λ-return of the training network's own values, reanalysed (`--value-lambda`; docs/stockNavalon.md section 6): its raw values predicted results better than the best network's search values (0.572 against 0.594). Not simply circular: the result always ends the return. Non-circular fallback, unused: shrink the result toward 0 for positions far from the end (a fixed bias, not a win probability).

### Varied starting positions (for later)

Self-play always starts from turn 0, so every game pays for its own opening and the bot mostly sees the strategies it already plays. Starting a share of games elsewhere would help in several ways:
- **Different strategies:** games forced into lines the bot rarely chooses (rider rushes on roads, defenders with catapults, naval play, early expansion) so it learns what they're worth and how to meet them. Ways to get there: fork from positions where a different plan was available; or a forced opening plan (below).
- **More of the mid and late game per GPU-hour,** and less of the opening, where the value memorizes.
- **Rare situations on purpose:** forks from moments like an enemy next to the capital, to practise defending.
- **Learning from mistakes:** a different move at the fork shows the value what the alternative leads to.

What others do (from memory): AlphaZero and Leela start from the initial position with randomness in the opening; KataGo varies starts (random opening moves, board sizes, handicaps) and forks games from positions in earlier ones with a different move; some work uses reverse curricula, starting near the end and moving earlier.

**Sketch:** a share of games (about 15%) start from a position in a recent game, with a different move there, or after a forced opening plan. The record carries the earlier moves up to the start without policy targets, so replay works, and only positions after it train. With fog the start is a real state, so nothing changes for determinization. To test: records, the buffer, the held-out split, and that forked games don't crowd out ordinary ones.

### Forced openings: a plan library (in matches only)

**Why:** on continents and waterworld the bot plays passively and builds a mind-bender army (2026-11-19: first attack around turn 16 on continents, against turn 3 on land maps). Self-play can't find aggression: both sides play the same style, attacking needs a plan over many turns (a bridge on continents, boats on waterworld), and the value puts a bridge or port at about 0. A local minimum.

**Built for matches** (`plans.h`, `SelfPlayConfig::sidePlans`; docs/stockNavalon.md section 3): a side follows a plan's stages as constraints on what it may choose, the engine deciding legality and costs. Tested from turn 0 (runs.md): the bridge rush lost, naval even, so the early caution is about right; the open question is converting once ahead (todo.md).

**In training (if the test says so):** the plan library replaces the draw of ordinary self-play games, the normal game its first entry. Each plan weighs the network's loss rate against it beyond an even game, (loss rate − 0.5)⁺, from its recent results (smoothed toward the last measurement, so an unplayed plan doesn't look solved); the normal game weighs 0.5. A plan the network can't answer gets about half the games; one it answers fades toward 0, the noise near 50% keeping it in play now and then. No share setting. The forced side's moves in the forced phase train nothing; after it everything trains; the defending side trains throughout. Forced games stay out of the held-out set and the self-play measures.

### Augmentation: on (settled)

It was off for a while, suspected of causing passive play, but the live runs never separated it from other changes. It has been on ever since without the passive play returning. It matters for memorization: rotated and reflected maps make individual games harder to learn by heart.

### Self-play opponents (built)

With fog, self-play with determinization doesn't converge to an equilibrium and strategies can cycle, and a gate against one opponent can pass every step of a cycle. Hence the opponent pool (20% of games against earlier bests, prioritized) and gates against several past bests. The pool also keeps the belief model from learning that every opponent plays like the bot.

### Treaties, and more than two players

Peace treaties are off in training for now (a bot that can't use them yet makes and breaks them every few turns, freezing its own army: [`runs.md`](runs.md), seventh run). With three or more players they're real strategy, so they come back, in steps, each with a check:

1. **Treaties in 1v1, by a curriculum** like fog: a share of games with treaties, rising by training steps after a latched trigger (the bot reliably wins games). Check: treaty breaks per player-turn fall toward 0 as the share rises, and gates and anchors don't weaken. Fallback if it keeps churning: an input feature for what a break would cost now.
2. **Three or more players, still without treaties**, so the multiplayer machinery is debugged on its own:
   - **a value per seat** (my chance of winning is no longer minus yours): one estimate per player, the search backing up each player's own at their moves (max^n);
   - **a target by final placement:** 1st +1, last −1, the rest spread evenly;
   - **gates in mixed seats:** one candidate against copies of the best, judged on winning more than its fair share;
   - **a search horizon** spanning several players' turns, to see what a treaty buys.
3. **Treaties in multiplayer, by the same curriculum, with the opponent pool from the start.** If every seat is the same network, everyone agreeing to peace and stalling is an obvious equilibrium. Against it: a share of seats played by earlier best networks and scripted aggressive bots; stalemates stay truncations, not draws; gates against several past bests.
4. **Measures:** treaty and break rates per opponent; how often a treaty precedes an attack on a third player; the stalemate rate (rising would mean collusion); win share against pool opponents against self-play.

No reward for diplomacy itself, for the reason there's no exploration bonus: it would set the rate of treaties by the bonus, not by what wins. Keep the per-opponent diplomacy features in the encoding.

### Playing the real game

Replaced by the oracle ([`integration.md`](integration.md), [`docs/oracle.md`](../docs/oracle.md)): the game's own rules library run headlessly, rather than a PolyMod mod in the running app. It covers differential testing (mirrored games), map comparison and matches against the built-in Crazy bot. Offline only.

## 2. Fog: stage 2 search

### Where it comes from

POMCP and ISMCTS are both MCTS extended to hidden information:

| | Information fully visible | Hidden information |
| --- | --- | --- |
| **one agent** (planning) | MDP: MCTS (UCT) | POMDP: **POMCP** |
| **several players** (a game) | perfect-information game: MCTS (AlphaZero) | imperfect-information game: **ISMCTS** |

Both sample a hidden state for every simulation and share one tree across all of them. ISMCTS adds the opponent: in their part of the tree, their moves are chosen from what *they* could know. With an own-turn horizon the opponent never moves inside the tree, so POMCP applies almost directly; crossing into their turn makes it ISMCTS.

### Why: strategy fusion

Stage 1 (plain determinization, built) searches each sample as if it knew what's behind the fog, and plans that only work in that sample, like "explore here, then take the village there". Averaging across samples blurs this but doesn't fix it.

### What changes from stage 1

1. **One tree, a fresh sample per simulation,** drawn from the belief model through the filter below; the tree's statistics average over everything that might be behind the fog.
2. **The tree branches on what was observed.** A move that reveals tiles leads to different children depending on what it revealed, so "explore here" is judged by its average outcome, and later moves are planned per outcome. This fixes strategy fusion.
3. **Coarse observations:** branch only on what matters for planning ("found a village", "found enemy units", "found a ruin", "nothing important"), and add observation branches only as visits grow (progressive widening, as in POMCPOW).
4. **The opponent's turn** (if the horizon reaches it) is chosen from the opponent's information: priors from *their* view of the sample, nodes keyed by what they can see.

### Particle filtering between turns

Each turn brings hard facts: tiles revealed, units seen, every published score. The belief model proposes candidate hidden states, and the filter rejects any that contradict a fact. Candidates are generated fresh each turn rather than carried over, since the state is too large for a fixed set to survive many turns of evidence. The model makes guesses plausible; the filter makes them consistent.

### Stage 1 choices that keep stage 2 from being a rewrite

- `determinize()` must be cheap enough to call once per simulation (60 to 250 µs now, measured).
- Child nodes are looked up through one function, keyed by the move; adding the observation to the key is then local (section 11's position lookup sits at the same place).
- The `Beliefs` interface stays as it is: the belief model is stage 2's particle generator as-is.

**Order:** once stage 1 works end to end, measured against it at equal compute, same network and beliefs.

## 3. Exploration and the value of information

Exploring is worth two things:
1. **What you find:** villages, ruins, lighthouses, a meeting bonus, score. Handled fairly well even in stage 1: an Explorer reveals tiles at once, so the search sees its results, on sampled terrain at sampled rates. Roughly right if the sampler puts villages and ruins in the fog at realistic rates.
2. **What you learn:** where their capital and army are, their tribe, the terrain between you.

| | Stage 1 | Stage 2 |
| --- | --- | --- |
| what you find | roughly right, given a good sampler | right |
| what you learn, within the horizon | **undervalued** | valued properly |
| what you learn, beyond the horizon | learned by the value head | learned by the value head |

- **Stage 1 undervalues information:** each sample acts as if it already knew, so a line without exploring plays as if it knew where the villages and enemy are. Strategy fusion as concrete behaviour: too little scouting, the Explorer undervalued.
- **Beyond the horizon it's learned,** from self-play where information decides results, which needs opponents that exploit it.
- **No exploration bonus:** a reward per tile would set the rate by the bonus, not by what wins.
- **Diagnostics:** Explorer against Workshop picks and scouting rates over training; test positions where scouting clearly pays or doesn't; stage 1 against stage 2 on exploration choices; value accuracy split by how much of the map is explored.

## 4. The search horizon

The horizon is a setting (the number of turn changes a line may cross), unlimited by default since 2026-11-14: it rarely came into play, and where it did, in deep forcing lines, seeing further is what's wanted. Whole turns as moves became turn search (section 9). Depth is the limit, not the horizon: at 1,024 simulations the search reaches 4.3 commands deep and the opponent's turn in 34% of simulations, partly because the budget is split across transpositions (section 11; the earlier guess that repeated evaluations were mostly re-searches rather than true transpositions was wrong).

**If the bot gets stuck on tactics** (overlooking the opponent's next turn), in rough order of cost:
1. Sharper threat planes from the rules: would my unit here die, could a city fall next turn, what would they lose to retaliation.
2. A counter-attack check at the leaves where the turn ends (like quiescence search in chess): play the opponent's forcing replies (kills, captures), then score.
3. More simulations where End Turn is a serious candidate, normal budgets elsewhere.

**How other long-turn games handle it** (from memory; check the papers before building on them):
- *Online Evolutionary Planning* (Justesen et al., Hero Academy): evolves whole turns as one candidate, scored at the end of the turn; beat MCTS on multi-action turns, which drowns in orderings. *Rolling Horizon Evolutionary Algorithms* do the same over a few turns.
- *Portfolio Greedy Search* (Churchill and Buro, RTS combat): each unit picks among a few scripted behaviours, improved one unit at a time with the rest fixed.
- *Naive MCTS* (Ontañón, microRTS): combinatorial bandits over units' actions instead of branching on every joint action.
- *Elastic MCTS* (Xu and Perez-Liebana): merges similar states so they share statistics; evaluated, I believe, on **Tribes** (Perez-Liebana et al.), a research framework modelled on Polytopia. The closest prior work: read its papers before building more of section 9.
- AlphaStar and OpenAI Five: no search, a learned policy trained at huge scale.

## 5. The belief model

### Reading the score

Every player's score is public: others' update when their turn ends, your own is live. Score is a fixed formula (the engine's), so a change is a sum of known amounts. Pros read the first turn or two this way; a bot can do it every turn.
- A published change covers everything since that player's last turn, including what *you* did to them; you know your own effects, so subtract them.
- Evidence accumulates: a tech, once bought, has to stay consistent with every later turn.
- It reveals techs (the strongest signal), stars (estimated spending against visible income), units trained in the fog, and expansion. It only works if the engine's score formula matches the real game's.

### Ambiguity is expected

The score never pins the opponent down. The bot keeps a realistic spread and decides across it: many samples, in the proportions the belief model gives (calibration matters, not one answer), each matching the published score. Most of the uncertainty doesn't change the right move; the value head was trained on the same uncertainty; and uncertainty shrinks as units are seen, tiles explored and scores published.

### What the model predicts

Built (the first version, used by self-play's determinization): tribe, techs, stars, where hidden units are, their types, how many, and the opponent's exploration ([`docs/encoding.md`](../docs/encoding.md) section 8). Planned: enemy cities in the fog (per tile, plus level), unexplored tiles (terrain, resource, village/ruin/none).

**Why "where" and "what type" are separate:** a per-type plane is nearly all zeros (sparse, rare types get few examples); one "is a unit here" plane learns from every hidden unit and is what the search cares about most; the type softmax only learns where units are, and shares across tiles. The planes amount to a heat map of the opponent in the fog, which suits a convolutional trunk.

### How it learns

- **Supervised from games where the truth is known:** every position is a labelled example. The score features give it the exact arithmetic; its job is which explanations are likely. Cross-entropy pushes towards true frequencies, so it's trained to be calibrated.
- **Reflecting good players:** it learns what players *in its training data* have; self-play, refreshed as the bot improves, is the main source.
- **Not assuming everyone plays like the bot:** a varied opponent pool (past versions, scripted rushers, turtles, expanders); check calibration per opponent style.
- **How facts relate:** the heads predict facts separately; sampling covers the important relationships (the tech tree, proven techs, the score check). If not enough, a head that samples one fact at a time, conditioned on earlier ones.

### Combining the exact side with the belief model

The exact side (tribe inference, proven techs, score arithmetic) decides what's *possible*; the belief model, how *likely* each possibility is. They meet three times:

```
public information (view, score history, climates)
        │
        ├──► exact side: possible tribes, proven techs, explained/unexplained score
        ▼         ▼
   network input ◄── 1. exact results as input features         (built)
        ▼
   belief heads ──► 2. mask: anything impossible gets 0, the rest renormalized
        ▼
   sample a hidden state ──► 3. filter: tech tree, forced techs, the exact score check
        ▼
   determinized game ──► search
```

- **The mask** applies in training too (the loss on the masked distribution), so the network only learns to weight the possibilities; since the exact side never rules out the truth, the loss is always defined.
- **The filter:** after many rejections in a row, fall back to a looser sample from the exact side alone, and count it: a high fallback rate means the beliefs are off.

### How to tell it works

Calibration on held-out games, per opponent style; accuracy against the baseline; the rejection rate; and playing strength, the same search with learned against baseline beliefs. The last is the test that counts.

## 6. Determinization: the full design

Built as a first version ([`docs/stockNavalon.md`](../docs/stockNavalon.md) section 4): copy the real game, wipe what the player doesn't know, fill it back in from the beliefs, repair, check the score. What the full design adds:

- **Enemy cities:** the number of other cities, their locations and levels sampled from a city head, borders over unexplored tiles only.
- **Unexplored terrain** from a terrain head rather than a fresh map, so coastlines continue into the fog and villages keep their spacing.
- **Repair:** city connections recomputed, unit caps respected.
- **Fewer rejections:** draw every part within the remaining score budget (done only for techs when the score is too low).
- **Speed:** stop generating a fresh map for every draw.
- **More than two players.**

**The `Beliefs` interface** is the boundary: the learned model replaces the baseline one part at a time behind it.

**Hidden units and the threat map:** the threat map (an input) is exact, from visible units; hidden units become real units in the sample, and the search sees their attacks itself. A possible refinement: an "expected hidden threat" input plane from the heat map, only if the value head is shown to be blind to unseen units (it costs a second network pass).

## 7. Player memory

Not needed for now. Some `PlayerView` fields aren't on the real game's screen (own roads, the score history, own effects on others' scores, attacks suffered, conversions, first occupation): the player's memory, which the engine fills in perfectly. It would only matter for a bot reading the real game's screen; the oracle plays the real game in lockstep with ours, so our engine holds the full state. If it's ever needed: a `PlayerMemory` part of the view, updated only from what the player sees and does, with a test that it equals the engine's fields after every command.

## 8. Smaller alternatives

- **A transformer over tiles** instead of the convolutional trunk, if long-range structure (roads, ports) matters more than expected.
- **Separate policy and value networks,** if a much cheaper policy turns out useful (todo.md's small network for quick searches).
- **Normalizing instead of augmenting:** transform every position so the player's own capital is in one fixed region, as chess engines do for the side to move. One orientation saves learning eight and may give an "enemy is that way" prior in the fogged opening; but it adds no data, a fixed orientation still identifies a game's map, and every search evaluation would need its input transformed and its policy transformed back.
- **Count heads:** exact integers with a cap (unit count, exploration) put weight on several values at once; a mixture of negative binomials (stars) covers any size with no bins, and two peaks ("spent everything" against "saving up"). Alternatives: fixed-width buckets, a single negative binomial, exact integers for stars too.
- **Running the model inside C++** (TorchScript or ONNX export) for fast play without Python; the search wouldn't change.

## 9. Planning turns ahead: a redesign

A brainstorm (2026-11-15/16): a bot that plans a couple of turns ahead the way a player does, since the measurements say command search can't. Section 4 has prior work (Tribes is the closest: read it first).

### Why the search can't look ahead

- **Depth is in commands, and a turn is 7–30 of them.** More budget buys little depth: 1,024 simulations reach 4.3 commands and the opponent's turn in 34% of simulations, and win 60% against 64 (runs.md, "From 320,000"). Part of that is transpositions splitting the budget (section 11), which is cheaper to fix than any idea below.
- **Payoffs come a turn later.** A unit must start its turn on a village or ruin to take it, so the search almost never sees the capture; it's down to the value, which barely sees it. In self-play, a village or ruin in reach goes untaken in about 74% of turns.
- **Why a player can:** plans, not commands (the order of a turn's moves rarely matters); distant units thought of separately (additive, not multiplicative); attention only where something is happening; the opponent's reply as threats, not a full turn; reasoning back from goals.

### What doesn't get there

- **More simulations:** see above.
- **Front-loading** (most of a turn's budget on its first decision, the rest reusing the tree): tree reuse already counts kept visits toward the next decision, so this is only uneven phases; an efficiency gain, not lookahead.
- **Bridge-burning MCTS** (Justesen et al.) is what playing a command and reusing the tree already does. **Non-exploring MCTS:** narrower exploration tied (runs.md, "Narrower search"). Both, like OEP, planned only the current turn.
- **Splitting the board into regions:** on 11×11 maps, all of a player's units join into one region (units within 2 tiles, chained) in 79–85% of turns. Partitions don't exist here.

### Ideas worth trying

1. **Command-by-command, then turn-by-turn: built** as turn search (`SearchConfig::turnSearch`, docs/stockNavalon.md section 3), with likely, sampled and discrepancy replies and the π̄ backup.
2. **Opponent replies at turn-ending leaves:** where a line ends the turn, sample a few opponent reply turns and evaluate after them. Turn search's replies cover it.
3. **Plan, then refine:** candidate whole turns from the policy, scored against a few opponent replies; then one unit at a time, try its alternatives with the rest fixed, keep improvements (Online Evolutionary Planning's mutations). Granularity at units × alternatives, not alternatives^units.
4. **Focused local search:** not partitions but focus areas: around each point of interest (unclaimed village or ruin, a city of either side), the units of either side close enough to matter. Measured: within 2 tiles, median 2 / 3 / 5 units (turns 0–10 / 10–25 / 25+), the busiest area 2 / 5 / 8; within 3 tiles, busiest 3 / 7 / 12. Search only those units' commands, both sides, 2–3 turns, the rest held, judged by the whole-board value with the line applied (the capture now happens inside the line); combine areas by settling stars, unit cap and shared units.
5. **Learned focus:** (a) *unit intents*, a head predicting where each unit will be 2–3 turns later (trained from records like the side heads): plans from intents, focus areas where intents meet; (b) attention between units defining focus areas; (c) a learned turn model (MuZero-style, a turn at a time) searched in the network's own representation; (d) no search, AlphaStar-style (far more compute). Bootstrapping: rules propose candidates (every village, ruin, city, contact) and the network ranks them.

How to judge any of them: strength at equal network evaluations per turn (`searches.py`), and villages taken, depth reached.

**Status** (2026-11-26): turn search played even with command search at 512 evaluations (52% over four matches), and as self-play's target it hurt: runs from 320,000 with it got worse, while command search's first gate was the only improvement (runs.md, "From 320,000"). Transpositions come first (section 11); turn search is retested after, and once the value judges the economy better. Localization (4 and 5) is the untested direction worth exploring then.

## 10. The next run

The ninth run is a testbed: training will restart at some point (more players, larger maps, or something else, not decided yet). Changes are judged by whether they belong in that run. What the ninth run taught (notes/runs.md has the measurements):

1. **The value is weak on the economy.** A captured village is worth about +0.02 to the best network, so no search can act on what it finds. λ-return targets from the start (built: reanalysed with the training network, whose raw values beat the best network's search values at predicting results), and a captured village's value tracked from early on (`stocknavalon.diagnose`).
2. **Self-play's search barely improves on the network.** More budget buys little (1,024 against 64: 60%), so training from 320,000 had little to learn and drifted. A search whose targets carry an improvement comes first; transpositions (section 11) are the measured cause to fix.
3. **Gating let self-play go stale** at a 55% threshold (nothing promoted for 26,000 steps; the gates were right). Now 50% against three past bests. Whether to gate at all is open: KataGo plays the latest network. Gate noise still has to be measured (the best network against itself on different map sets).
4. **Search should co-evolve with the network.** A new search tested against a network trained for the old one starts at a disadvantage, so the search to train with is settled before the restart. Turn search as self-play's target hurt; command search, with transpositions if they pay off, is the candidate.
5. **Cheap controlled experiments.** Most questions were answered on one long run with noisy gates. Short runs at fixed compute, A against B (`--no-promote`, `--stop-after-gates`), test design choices properly.
6. **Self-play's cost:** CPU and GPU now about even (todo.md); more players or larger maps make each game dearer.
7. **Generality:** determinization and tribe inference are two-player only, the search's backup assumes two sides, and the encoding is padded to a fixed `mapSize` (11) with commands reaching 6 tiles. New code shouldn't add to that where it can be avoided.

Already general, to keep: the opponent pool; single-source rules (`moveSearch.h`); the diagnostics (`versus`, `searches.py`, `diagnose`, `--explain`, `pe.search` leaf breakdowns, `pe.search_positions`, held-out `bias`, the value-source comparison); the oracle's rules checks.

## 11. Transpositions

**Built** (2026-11-26): `Game::positionHash()` (`src/engine/position.cc`) and `SearchConfig::transpositions` as below, off by default (docs/stockNavalon.md). The hash's property test: of 1,837 pairs of commands tried both ways in random games, 1,647 gave the same position, and the hash agreed with a full comparison on every one. With transpositions on, the 281 positions at 256 simulations hold no exact repeats (0%, from 40%); 8% of nodes still look the same to the network (a unit's facing after a different route and the like, kept apart). Results against off are next.

**Why** (measured 2026-11-26, notes/runs.md "From 320,000"): a turn is 7–8 commands, mostly independent, so the same position is reached by many orders, and the tree keeps a separate node for each. With the best network, 21% of a search's nodes repeat another's position at 64 simulations, 40% at 256 and 54% at 1,024, nearly all within the searcher's own turn. Each copy takes its own visits and subtree, so the budget buys little depth: 1,024 simulations reach 4.3 commands deep and the opponent's turn in 34% of simulations, and play only 60% against 64. The evaluation cache (`SelfPlaySession`) saves the copies' network calls, not their simulations. Training from 320,000 stalled because self-play's search barely improves on the raw network; a search that goes deeper at the same budget is the way to targets worth learning from.

**The idea:** one node per position within a tree, whichever order reached it (Monte-Carlo graph search; Czech, Korus and Kersting 2021, "Improving AlphaZero Using Monte-Carlo Graph Search"). A simulation that reaches a position already in the tree moves into that node and carries on below it: no new node, no evaluation, so it gets further for the same simulation.

### The search

- **Finding a position:** each node keeps its position's hash. A tree keeps a map from hash to node. When a simulation takes an edge with no child yet, it applies the command as now, then hashes the result: a known position becomes that edge's child (the copy just made is thrown away); a new one becomes a new node as now. Per tree only: each tree is its own determinization, so positions don't repeat across trees.
- **No cycles:** within a turn every command uses something up (a unit's move or action, stars, a tech), so no line returns to an earlier position; turns only go forward. The graph is acyclic without any check. A guard all the same: a hash matching a node on the current path makes a new node (and counts it, so a test can assert it never happens).
- **Values:** edges keep their own visits (what the exploration term and the root's policy target use) and totals. What an edge is worth becomes its child's own average: its evaluation plus everything backed up through it, over 1 + its visits, from whichever parent (`edgeUtility`, MEAN backup). For a child with one parent this equals today's edge average exactly (its first visit is its evaluation, and every later one passes the same edge), so a search where nothing merges plays exactly as now. A merged child shares what every line into it found. The other backups (PBAR) already read the child's `backedValue`; a shared child's parents see its update the next time they're passed through (`refresh` runs along the path only), as in Czech et al.
- **Backup:** unchanged, along the path the simulation took.
- **Budget:** a simulation still ends in one evaluation or a finished line, so `simulations` keeps its meaning, and at most one new node a simulation (the node vector's reserve stays valid).
- **Tree reuse:** `reuse` already renumbers the kept subtree breadth first with a visited check, which handles shared children; the map is rebuilt from the kept nodes' hashes.
- **Turn search:** left out at first (`turns`/`turnChild` stay a tree); `transpositions` applies to command search only.
- **Gumbel root:** unchanged; two root moves leading to one position is possible but rare, and handled the same way.
- A setting, `SearchConfig::transpositions`, off by default, so everything else stays as it is until measured.

### The position hash

`Game::positionHash()`, in the engine next to the state it covers (one place knows the fields): 64 bits over everything that decides what can happen next, in a canonical order, so two orders of the same commands give the same hash:
- per tile, by index: terrain, resource, building, its level and turn built, road (and builder), owner and territory as the city's *tile*, explored and occupied bits per player, and the unit standing there (all its fields, with its home as its city's tile), so unit and city indices, which depend on the order things were made, never enter;
- cities by tile: owner, level, population, rewards pending, connection, wall, workshop, parks, robbed and the rest;
- players: stars, techs, met, peace and cooldowns, embassies, tasks and their counts, Pacifist, attacked flags, everything `Player` holds;
- turn, current player, winner, the population a level-up holds back (by city tile), connections due, the random state (a draw from a copy, enough to tell two states apart), and the forced outcomes (`Game::forced`).
- Left out: what only records the past and can't change what's legal or how it plays out, such as the score history, if it really can't. Each such exclusion is a decision, written next to the hash.

The risk is a field left out: two different positions merged, a wrong value shared. Checks:
1. **A property test:** random games, many pairs of positions, hash equal exactly when a canonical comparison of the whole state says so (units and cities matched by tile), and equal hashes with equal legal commands. The comparison is written separately from the hash, so a field missing from one shows up.
2. **The same test on purpose-built transpositions:** two independent units moved in either order, research before or after a build, a city reward then a move: equal hashes. And near misses: the same moves with a different unit's facing, health or `attacked` flag: different hashes.
3. **A checked mode** for tests: on every merge, the canonical comparison runs too.

Cost: a hash per new node, a pass over the tiles (121 on 11×11), small next to an evaluation; measured with the profiler's `NEW_NODE` laps.

### Measuring it

1. `pe.search_positions` (built) on the same 281 positions with transpositions on: repeats should drop to about none.
2. `searches.py` with transpositions on against off (a per-side flag), the same network and budget: depth below the root and the share of simulations reaching the opponent's turn first, then the result at 64, 256 and 1,024 simulations.
3. The scaling matches again (256 and 1,024 against 64): if transpositions are what was holding depth back, more budget should now buy more strength. That's the condition for self-play's targets to carry an improvement, and for training from 320,000 to climb again.

### Decided

- Merging goes on past the searcher's own turn too (the hash covers the turn and the player to move), though the measured repeats were nearly all within it (2026-11-26).
- No extra "catch-up" backups at first: edges read the child's average directly, the simpler variant; added only if the measurements call for them.

### Open questions

- Self-play with it: a network trained under the old search starts at a small disadvantage under a new one (section 10's lesson). This one changes only how far the same search gets, not what it chooses between, so the gap should be small; if it helps, the next restart trains with it from the start.

