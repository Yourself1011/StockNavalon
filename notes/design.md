# Design notes

Designs not built yet, and the reasoning behind decisions. What the code does now is in [`docs/stockNavalon.md`](../docs/stockNavalon.md); the checklist is [`todo.md`](todo.md), and run history is [`runs.md`](runs.md).

Contents:
1. [Training plan](#1-training-plan) (next)
2. [Fog: stage 2 search](#2-fog-stage-2-search)
3. [Exploration and the value of information](#3-exploration-and-the-value-of-information)
4. [The search horizon](#4-the-search-horizon)
5. [The belief model](#5-the-belief-model)
6. [Determinization: the full design](#6-determinization-the-full-design)
7. [Player memory](#7-player-memory)
8. [Smaller alternatives](#8-smaller-alternatives)
9. [Planning turns ahead: a redesign](#9-planning-turns-ahead-a-redesign) (brainstorm)
10. [The next run](#10-the-next-run): what the restart should get right

## 1. Training plan

The second run improved for 10,000 steps (78% against the random bot at step 10,001, a third of games won by capture), then got worse twice ([`runs.md`](runs.md)). Most of the trouble came from three design choices rather than bugs:

1. **A weak, moving value signal.** 75–90% of games hit the turn limit and got a target near 0, so the value head had little to learn and memorized games instead. The score weight added to compensate made a feedback loop: captures rise, the weight drops to 0, the signal goes, play degrades, captures fall, the weight comes back.
2. **Nothing stopped a regression from feeding on itself.** Self-play always took the latest weights, so a worse network played the next games and trained on them.
3. **Thin searches for the decisions involved** (16–64 simulations a command, about 60 legal moves, 10–30 commands a turn), on one guess at the hidden state. Policy targets stay close to the network's own prior, which makes 1 and 2 worse.

### A. The value is about winning only

- **Wins, not score.** Even a down-weighted score term rewards being ahead at the limit, so the value target has no score term and no schedule.
- **A game cut off at the limit is truncated, not drawn.** Its positions take an estimate of the final position as their target ("how likely was I to win from here"). Built: each seat's root value at its last network search, stored in the record when the game ends. A search value includes the opponent's reply, so it's better than the raw network output, needs no extra network call, and comes from the network that played (the best network, once C is in), fixed when the game is played rather than chasing the latest weights. A capture is ±1. This is the standard treatment of time limits in reinforcement learning (Pardo et al. 2018, "Time Limits in Reinforcement Learning"), close to MuZero's bootstrapped value targets and Leela Chess Zero's blends of result and search value. AlphaZero didn't need it: chess and Go games almost always end on their own.
- **Circular at first:** the estimate starts near 0, so it acts like a draw until decided games teach the network what winning looks like; that then carries into truncated games. It learns more slowly than the score term did early on, the price of not optimizing score. The side heads (B) and a higher limit win most of that back.
- **A higher turn limit with a stalemate cutoff** (built): 150 turns at most, but a game also stops once nobody has attacked or captured for 25 turns, counting from the first fight. In human games there's a quiet stretch before tribes meet, then constant fighting; this bot doesn't fight that way yet (first combat around turn 8, then lulls: in decided games, a third had a 10-turn lull after the first fight, 90% stayed under 17 turns, and all under 31). 25 turns from the first fight would have cut about 1–2% of games heading for a capture. Score was considered as the stall signal and rejected: it keeps creeping up from techs, exploration and growth even when nobody fights. Since a stalemate is a truncation, not a draw, a generous window costs only compute.
- **Fallback,** if value learning is still too slow: a score term fading by training steps alone (0.3 to 0 over the first 20,000 steps, say). With no dependence on the capture rate, it can't loop.

### B. Side heads: score lead and territory (built)

Extra outputs, trained alongside the others, never read by the search: they change what the network understands, not what it plays for. The trunk is shared, so the features they need (economy, armies, fronts, weak cities) feed the value too, from many targets per position instead of one result per game. KataGo's ownership and score heads are the model.

| Head | Output | Target | Trained when |
| --- | --- | --- | --- |
| score lead soon | 1, tanh | `tanh(lead / 500)` 5 turns later, or at the game's end if sooner: the player's score minus the best opponent's | always |
| score lead at the end | 1, tanh | the same, at the game's end (a capture or the turn limit) | always |
| final territory | `[N, N]`, softmax over 3 per tile | whose borders the tile is in when the game ends: the player's, an opponent's, nobody's | map tiles, where the record has the final territory |

- **Relative to the player to move.** In 1v1, "mine, theirs, neutral" is exact.
- **Scores** come from the published score history (every player's score at every turn end), so one replay of a game gives the lead at any turn.
- **Final territory** isn't in records yet: self-play stores the map (N² small numbers) when a game ends; records without it leave the head masked.
- **Spatial,** so it moves with the map under a symmetry, like the per-tile belief targets.
- **Loss weights:** about 0.25 for the score pair, 0.5 for territory, tuned by the held-out value loss.
- **Memorization:** the final-game targets can be memorized per game like the result; the 5-turn lead resists it best.

### C. Self-play only plays the best network

- **Averaged weights (EMA):** the trainer keeps an exponential moving average of its weights (decay about 0.999, roughly the last 1,000 steps), BatchNorm statistics included. A minibatch averages one step's gradient, but with a fixed learning rate the weights keep bouncing around a good solution, and a sync takes whatever snapshot the moment gives (step 10,152 played passively, 10,228 normally, 76 steps apart). The average sits near the middle, and usually plays better than any single snapshot (Polyak averaging). It doesn't stop a slow drift; gating does.
- **Gating** (AlphaGo Zero, Leela Zero, KataGo's gatekeeper), from the evaluation thread: every 1,500–2,000 steps (about 20–30 minutes), the EMA weights play about 128 games against the **best network**, and become the new best only if they win. Self-play uses only the best network, saved as `best.pt` so restarts resume with it. Training never stops or rewinds: a failed gate only means self-play keeps the best a while longer. The anchors (random, the score search) keep running every 5,000 steps, on the best network.

  ```
  training (live weights, every step)
     │
     ├─► EMA copy (every step)
     │      └─► every ~2,000 steps: candidate ──► gate match vs best
     │                                               ├─ pass ──► best := candidate
     │                                               └─ fail / inconclusive ──► best unchanged
     │
  best network ──► self-play ──► replay buffer ──► training
  ```

- **Against the best, not the last candidate:** otherwise each candidate only has to beat a slightly worse predecessor, and a slow decline gets through.
- **Judged on decisive games:** with about 20% of games decided, 55% over all games with cut-off games as half points would need about 75% of the decisive ones, and good candidates would rarely pass. So: pass at 55–60% of decided games, with at least 20 of them; cut-off games count for neither side, so score never enters. Too few decisive games is inconclusive: keep the best, try again or extend the match. A sequential test (SPRT) could stop early. Gate games can use a higher turn limit than self-play.
- **A diagnostic too:** several failed gates in a row means training has gone wrong, while self-play stays at a known good level. Step 15,001 would have failed against 10,001.
- **The cost:** self-play can lag the trainer by up to one gate interval.

### D. A curriculum by map type: dropped

The idea was to start on land-heavy maps, assuming they have more captures. On 11×11 maps they don't (the table is in [`runs.md`](runs.md): Archipelago 26% decided, Pangea 25%, Drylands 19%, Continents 7%). Restricting map types would cost generality for little gain. All map types stay; the decided rate per type can go in the log.

### E. No fog first, then fog (built; phases to run)

Train with full information first, then bring fog in gradually: Suphx's "oracle guiding" (Mahjong), and training with privileged information generally.
- **Why:** with fog, every search runs on a guessed hidden state, today from the prior beliefs, one guess per search; much of the noise in the targets may come from searching a game that isn't the real one. Without fog the search runs on the true game, and the value is easier to learn. It also separates two problems: if the bot can't learn to capture without fog, the training loop is at fault; if it can, adding fog is a smaller, separate step.
- **Only information changes, not the rules.** Both players see everything; exploring still scores and units still walk to villages.
- **The encoding:** from `makeView(game, player, everything = true)`, so every tile's contents show, and every opponent's techs, stars and tribe. `explored` keeps meaning the tiles the player has actually explored, since that affects score. The belief heads aren't trained (nothing hidden, every mask 0), and the score features are exact (unexplained score 0). A per-game setting, so fogged and fog-free games can be mixed.
- **Phases, as a schedule** (built, `fog.py`): fog off until the bot reliably captures (a latched trigger: 50% of the latest 512 games decided); then a ramp by training steps, 0 to 100% over 25,000 steps; then fog only. The schedule mustn't follow a live measure of play or it can loop like the score weight did, so performance only decides when the ramp starts, once. The only feedback is a brake: 3 failed gates in a row pause the ramp until a gate passes (pausing can't oscillate). The buffer lags the schedule by about an hour of games, another reason for a gradual ramp. The belief heads train from phase 2. Skills that only exist with fog (scouting, the value of information, Cloaks, reading score changes) come then.
- **Gate matches follow the phase:** fog-free in phase 1, so the gate measures what's being trained, with a fogged anchor evaluation to track fogged play.
- **Engineering:** a per-game fog setting in self-play; fog-free searches use the real game instead of a determinized one.
- **Prerequisites for phase 2** ([`todo.md`](todo.md)): `decodeBeliefs`, a terrain head, player memory. Today the belief heads train but nothing reads them (every search uses `priorBeliefs`), so fogged searches run on generic guesses.

### Gumbel search, for small budgets

Danihelka et al., "Policy improvement by planning with Gumbel" (ICLR 2022). Our searches are thin (64 simulations, 16 quick, over about 60 legal moves), the regime where AlphaZero's root (PUCT, Dirichlet noise, visit counts as the policy target) works worst: most moves are never tried, the visits mostly echo the prior, the noise is crude, and the target isn't reliably better than the prior. Gumbel search changes the root:

1. **Candidates by sampling:** Gumbel noise added to each move's logit, the top k taken (say 16): exactly sampling k moves from the policy without replacement. Replaces the Dirichlet noise.
2. **Sequential halving:** the budget split evenly over the candidates, then the better half kept (scored by noise + logit + σ(searched value)), and again (16 × 1, 8 × 2, 4 × 4, 2 × 8 for 64 simulations). Every candidate is evaluated; the budget goes to the contenders.
3. **Play the last one standing:** an improvement on the prior in expectation even with few simulations, given informative values.
4. **The policy target is an improved policy over every legal move:** softmax(logit(a) + σ(Q(a))), Q "completed" for unsearched moves from the root's value and the searched ones, σ weighting values more as more simulations are spent. Dense and informative where visit counts aren't.

Below the root, a deterministic rule (visit the move whose share of visits lags furthest behind the improved policy), or PUCT as now.

**What changes here:** the root selection and move choice in self-play (quick searches too: 16 simulations still make a tournament over about 8 moves); records store the improved policy as probabilities per root move rather than visit counts (older records still read); the policy loss is unchanged; with fog, the samples' Q values are averaged per move; tree reuse still works; first-play urgency is unneeded at the root (completed Q replaces it). In the paper, with 2–16 simulations, the standard search barely improved while Gumbel still did.

### Value targets from the search

After 35,885 in the ninth run the value got overconfident (predictions averaging ±0.7–0.75 with the winner right 74–78% of the time) once the city term had faded and 85% of games were decided: every position of a decided game has the same ±1 target, turn 5 and turn 45 alike, which pushes towards certainty and is easy to learn by heart.

**The idea:** train the value partly towards the search's own value at each position: `(1 − w) × result + w × root value`. Leela Chess Zero trains on a blend of result and search value (a configurable ratio); MuZero bootstraps its value targets from later search values (n-step TD); AlphaZero used the result alone; KataGo keeps the result for its value and adds auxiliary short-term value heads trained on future search values.

**Why it isn't simply circular:** the real result is always part of the target, so real outcomes keep pulling; and the search value looks ahead (forced captures, exact values at game ends), so it's better than the raw network, the same principle as training the policy on search visits. **The caveat here:** at 64 simulations (16 quick) the root value is mostly the network's own estimate, so it adds less and inherits the network's biases; hence a modest w, about 0.25, with the result dominating.

**Needs:** the root value stored at every searched move (records keep only each seat's last today).

**Non-circular fallback:** shrink the result towards 0 for positions far from the game's end (result × 0.98^turns remaining). Softer early targets, no bootstrapping; but not a true win probability, and a fixed bias.

### Varied starting positions (for later)

Self-play always starts from turn 0, so every game pays for its own opening and the bot mostly sees the strategies it already plays. Starting a share of games elsewhere would help in several ways:
- **Different strategies:** games forced into lines the bot rarely chooses (rider rushes on roads, defenders with catapults, naval play, early expansion) so it learns what they're worth and how to meet them, instead of only refining its current style. Ways to get there: fork from positions where a different plan was available; or a short scripted or biased opening for a few turns (a research order, a unit mix) before normal play takes over.
- **More of the mid and late game per GPU-hour,** and less of the opening, where the value memorizes.
- **Rare situations on purpose:** forks from moments like an enemy next to the capital, to practise defending, the weakness seen in watched games.
- **Learning from mistakes:** a different move at the fork shows the value what the alternative leads to.

What others do (from memory): AlphaZero and Leela start from the initial position with randomness in the opening; KataGo varies starts (random opening moves, board sizes, handicaps) and forks games from positions in earlier ones with a different move; Stockfish's NNUE data comes from many varied starting positions; some work uses reverse curricula, starting near the end and moving earlier.

**Sketch:** a share of games (about 15%) start from a position in a recent game, with a different move there, or after a forced opening plan. The record carries the earlier moves up to the start without policy targets, so replay works, and only positions after it train. With fog the start is a real state, so nothing changes for determinization. To test: records, the buffer, the held-out split, and that forked games don't crowd out ordinary ones.

### Augmentation: on (settled)

It was off for a while, suspected of causing passive play, but the live runs never separated it from the score weight, self-play and restarts. It has been on by default ever since without the passive play returning, so the offline test planned for it was dropped. It matters for memorization: rotated and reflected maps make individual games harder to learn by heart.

### Self-play opponents

Self-play is the best network against itself, as in AlphaGo Zero (AlphaZero dropped gating and played the latest weights). In perfect-information games that's sound: inside the search, the opponent's moves are explored rather than assumed, so the policy learns moves that hold up against good replies, and the target is the unique minimax value. Two things change that here:
- **Fog:** self-play with determinization doesn't converge to an equilibrium, and strategies can cycle (A beats B beats C beats A). The usual answer is a mix of opponents: fictitious self-play, AlphaStar's league.
- **Gating against one opponent:** a candidate only has to beat the current best, which every step of a cycle can do; the fixed anchors catch regressions against simple play, not cycles among strong strategies.

Plan, for the fog phase: a share of self-play games (say 20%) against earlier best networks, and gates against several past bests. The pool also serves the belief model, which otherwise learns that every opponent plays like the bot.

### Treaties, and more than two players

Peace treaties are off in training for now (a bot that can't use them yet makes and breaks them every few turns, freezing its own army: [`runs.md`](runs.md), seventh run). With three or more players they're real strategy (peace with one neighbour to fight another, ganging up on the leader, breaking a treaty when an ally becomes the threat), so they come back, in steps, each with a check:

1. **Treaties in 1v1, by a curriculum** like fog: a share of games with treaties, rising by training steps after a latched trigger (the bot reliably wins games). Check: treaty breaks per player-turn fall toward 0 as the share rises, and gates and anchors don't weaken. Fallback if it keeps churning: an input feature for what a break would cost now (the player's units inside the other's borders, which a break disbands).
2. **Three or more players, still without treaties**, so the multiplayer machinery is debugged on its own:
   - **a value per seat** (my chance of winning is no longer minus yours): the value head outputs one estimate per player, and the search backs up each player's own at their moves (max^n);
   - **a target by final placement:** 1st +1, last −1, the rest spread evenly; denser than one winner;
   - **gates in mixed seats:** one candidate against copies of the best, judged on winning more than its fair share (above 1/3 with three players);
   - **a search horizon** spanning several players' turns, to see what a treaty buys.
3. **Treaties in multiplayer, by the same curriculum, with the opponent pool from the start.** If every seat is the same network, everyone agreeing to peace and stalling is an obvious equilibrium. Against it: a share of seats played by earlier best networks and scripted aggressive bots (a pact has to pay against players who aren't copies of you); stalemates stay truncations, not draws (a stalled game still teaches each seat its own chances); gates against several past bests.
4. **Measures:** treaty and break rates per opponent; how often a treaty precedes an attack on a third player (the strategic use); the stalemate rate (rising would mean collusion); win share against pool opponents against self-play.

No reward for diplomacy itself, for the reason there's no exploration bonus: it would set the rate of treaties by the bonus, not by what wins.

Nearer-term choices this affects: keep the per-opponent diplomacy features in the encoding, and build the opponent pool before multiplayer.

### Playing the real game

Hooking the bot up to The Battle of Polytopia itself, through a mod. Not built; nothing read in depth yet.

**What it's for:**
1. **Engine correctness**, by differential testing: the same commands in the real game and in polyengine, the states compared after every one (units, HP, cities, stars, score, legal moves). Any mismatch is a bug or a rule we got wrong; today the rules are only as right as `GAME.md`, and some mid-game score rules are unverified.
2. **Map generation:** many real maps against ours (terrain shares, villages and ruins, resources by terrain, capital spacing). Determinization and the terrain head depend on it.
3. **The built-in bots** as an honest, fixed strength scale beyond the random bot and the score search.
4. **The real-game gaps:** playing the actual game the bot sees only what the screen shows, so player memory becomes necessary, and the fogged view gets checked against the game's.

**Stages:**
1. **Read-only:** a mod writing the game state and each command played to a file or socket. Enough for 1 and 2, from human games or the built-in bots playing each other.
2. **Control:** the mod takes commands from the bot over a socket, playing offline against the built-in bots.
3. **The translation layer** between the game's representation (tiles, unit identities, commands) and ours: where most of the work and most of the bugs will be.

**Leads, from search summaries only:**
- [PolyMod](https://github.com/PolyModdingTeam/PolyMod) ([polymod.dev](https://polymod.dev/)): the game's standard mod loader, open source, with a mod API, on Windows, Linux and macOS (x86). The natural base.
- bengreff's PolytopAI ([polytopia-ai](https://github.com/bengreff/polytopia-ai), [polytopia](https://github.com/bengreff/polytopia)): described as trained on the game's real rules with an exact headless engine, from human tournament games then self-play, in the championship format (1v1, Might, Lakes, small map). `GAME.md` already cites bengreff for rule details.
- [Polyfish](https://github.com/HenBOMB/Polyfish): describes a real-time game-state extractor in C++, alongside an AlphaZero-style bot.
- [superhuman-polytopia-bot](https://github.com/JackYBT/superhuman-polytopia-bot): claims to beat the built-in Crazy AI.

**Care needed:**
- **Offline only:** a bot in online multiplayer would likely break the game's terms and be unfair to other players.
- **Chance won't match:** ruin rewards and Explorer paths come from the game's random state, so differential testing takes the real outcome at chance events and re-syncs, rather than expecting a match.
- **Game updates** can break the mod; the rules living in one place (`rules.h`, the data tables) keeps our side of an update small.
- **A different stack:** the mod is C#/.NET against the game's assemblies; check licenses before reusing anyone's code.

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

Each turn brings hard facts: tiles revealed, units seen, every published score. The belief model proposes candidate hidden states, and the filter rejects any that contradict a fact (a candidate whose opponent couldn't have produced this turn's +300 is out). Candidates are generated fresh each turn rather than carried over, since the state is too large for a fixed set to survive many turns of evidence. The model makes guesses plausible; the filter makes them consistent.

### Stage 1 choices that keep stage 2 from being a rewrite

- `determinize()` must be cheap enough to call once per simulation (60 to 250 µs now, measured).
- Child nodes are looked up through one function, keyed by the move; adding the observation to the key is then local.
- The `Beliefs` interface stays as it is: the belief model is stage 2's particle generator as-is.

**Order:** once stage 1 works end to end, measured against it at equal compute, same network and beliefs.

## 3. Exploration and the value of information

Exploring is worth two things:
1. **What you find:** villages, ruins, lighthouses, a meeting bonus, +5 score a tile. Handled fairly well even in stage 1: an Explorer reveals tiles at once, so the search sees its results, on sampled terrain at sampled rates, with the Explorer's path from the reseeded random state. Roughly right if the sampler puts villages and ruins in the fog at realistic rates.
2. **What you learn:** where their capital and army are, their tribe, the terrain between you.

| | Stage 1 | Stage 2 |
| --- | --- | --- |
| what you find | roughly right, given a good sampler | right |
| what you learn, within the horizon | **undervalued** | valued properly |
| what you learn, beyond the horizon | learned by the value head | learned by the value head |

- **Stage 1 undervalues information:** each sample acts as if it already knew, so a line without exploring plays as if it knew where the villages and enemy are. Strategy fusion as concrete behaviour: too little scouting, the Explorer undervalued.
- **Beyond the horizon it's learned,** from self-play where information decides results, which needs opponents that exploit it.
- **Making sure it's learned:** root noise and early visit-proportional choices make the bot sometimes explore when it otherwise wouldn't. **No exploration bonus:** a reward per tile would set the rate by the bonus, not by what wins.
- **Diagnostics:** Explorer against Workshop picks and scouting rates over training; test positions where scouting clearly pays or doesn't; stage 1 against stage 2 on exploration choices; value accuracy split by how much of the map is explored.

## 4. The search horizon

The horizon is a setting (built: the number of turn changes a line may cross). The options considered:
1. **Own turn only:** simple, almost unaffected by fog, but the opponent's reply is only judged by the value head, and replies like "their knight kills this archer, then takes the city" are where search adds strength.
2. **Through turn boundaries** (the default, horizon 1): standard, MCTS spends depth where it pays, but a round is 20 to 40 commands deep, and the opponent's turn runs on sampled hidden state.
3. **Whole turns as moves:** sample candidate turns from the policy and search over complete turns. Several rounds of lookahead, but only as good as the candidates.
4. **Quick opponent replies at the leaves:** let the policy play the opponent's turn a few times without a tree. Cheap, catches obvious punishments.

To be measured by strength per unit of compute.

**What's true now** (2026-11-13): at 64 simulations the search rarely gets past the player's own turn. At the first command of a turn the opponent's reply is about 6 commands deep, beyond the budget; only the last few commands of a turn, with End Turn among the candidates, search the reply. Counter-moves come from the threat planes and the value. Raising the horizon changes nothing without far more simulations: two turns ahead is about 20 commands deep, and per command, so it would cost something like 30× the self-play time. Fog makes deep lines worth less (one sampled hidden state, the opponent played by our own policy).

**The cutoff removed** (2026-11-14): the horizon is now unlimited by default. It rarely came into play (lines mostly end long before the opponent's turn is over), and where it did, in deep forcing lines, seeing further is what's wanted. Watch for End Turn's branch, which reaches the opponent's turn soonest, being searched deeper than the rest and so judged differently.

**Moves in different orders** (transpositions): 20.6% of network evaluations repeat an input already evaluated with fog, 24.7% without (first ~10 turns, the ninth run's best network, the run's settings), almost all within about one decision. Probably mostly the next decision re-evaluating what the last one saw when tree reuse couldn't continue, rather than true transpositions: the search is too shallow to try both orders of two moves often. An evaluation cache would save them. Sharing statistics between orders matters only with much bigger budgets.

**If the bot gets stuck on tactics** (not overlooking the opponent's next turn, the way a player thinks "if I move here, they attack there"), in rough order of cost:
1. Sharper threat planes from the rules: would my unit here die, could a city fall next turn, what would they lose to retaliation.
2. A counter-attack check at the leaves where the turn ends (like quiescence search in chess): play the opponent's forcing replies (kills, captures), then score.
3. More simulations where End Turn is a serious candidate, normal budgets elsewhere.
4. Whole turns as moves (option 3 above).

**How other long-turn games handle it** (from memory; check the papers before building on them):
- *Online Evolutionary Planning* (Justesen et al., Hero Academy): evolves whole turns as one candidate, scored at the end of the turn; beat MCTS on multi-action turns, which drowns in orderings. *Rolling Horizon Evolutionary Algorithms* do the same over a few turns.
- *Portfolio Greedy Search* (Churchill and Buro, RTS combat): each unit picks among a few scripted behaviours, improved one unit at a time with the rest fixed, the way a player settles one area then the next.
- *Naive MCTS* (Ontañón, microRTS): combinatorial bandits over units' actions instead of branching on every joint action.
- *Elastic MCTS* (Xu and Perez-Liebana): merges similar states so they share statistics; evaluated, I believe, on **Tribes** (Perez-Liebana et al.), a research framework modelled on Polytopia whose baseline agents include MCTS and rolling-horizon evolution. The closest prior work: read its papers and competition results before building any of the above.
- AlphaStar and OpenAI Five: no search, a learned policy trained at huge scale.
- Card games (Hearthstone): determinization, searched a turn at a time.

The most transferable for this design: sample several complete turns from the policy, score each end-of-turn position after the opponent's forcing replies, play the best.

## 5. The belief model

### Reading the score

Every player's score is public: others' update when their turn ends, your own is live. Score is a fixed formula, so a change is a sum of known amounts (+100/200/300 a tech by tier, +5 a tile explored, +20 a tile of territory, +5 × cost a unit trained, +5 a population plus level-up bonuses, +100 a temple, +400 a monument, negative for losses). Pros read the first turn or two this way; a bot can do it every turn.
- A published change covers everything since that player's last turn, including what *you* did to them; you know your own effects, so subtract them.
- Evidence accumulates: a tech, once bought, has to stay consistent with every later turn.
- It reveals techs (the strongest signal), stars (estimated spending against visible income), units trained in the fog, and expansion. It only works if the engine's score formula matches the real game's (the mid-game rules are still to be checked).

### Ambiguity is expected

The score never pins the opponent down. The bot keeps a realistic spread and decides across it: many samples, in the proportions the belief model gives (calibration matters, not one answer), each matching the published score. Most of the uncertainty doesn't change the right move; the value head was trained on the same uncertainty; and uncertainty shrinks as units are seen, tiles explored and scores published.

### What the model predicts

Built (the first version): tribe, techs, stars, where hidden units are, their types, how many, and the opponent's exploration ([`docs/encoding.md`](../docs/encoding.md) section 8). Planned: enemy cities in the fog (per tile, plus level), unexplored tiles (terrain, resource, village/ruin/none, on unexplored tiles only).

**Why "where" and "what type" are separate:** a per-type plane is nearly all zeros (sparse, rare types get few examples); one "is a unit here" plane learns from every hidden unit and is what the search cares about most; the type softmax only learns where units are, and shares across tiles; boats need no special case; grouping types was dropped (most groups would hold one or two types, and it loses distinctions like Defender against Swordsman). The planes amount to a heat map of the opponent in the fog, which suits a convolutional trunk.

### How it learns

- **Supervised from games where the truth is known:** every position is a labelled example. The score features give it the exact arithmetic; its job is which explanations are likely. Cross-entropy pushes towards true frequencies, so it's trained to be calibrated.
- **Reflecting good players:** it learns what players *in its training data* have, so bootstrap data from simple bots only gets it started; self-play, refreshed as the bot improves, is the main source; real games, if available, would be the best labels.
- **Not assuming everyone plays like the bot:** a varied opponent pool (past versions, different settings, scripted rushers, turtles, expanders); let the history adapt it within a game; check calibration per opponent style.
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
- **The filter:** after many rejections in a row (say 50), fall back to a looser sample from the exact side alone, and count it: a high fallback rate means the beliefs are off.
- **Later:** the exact search could list the actual explanations of the opening, weighted by the policy network's view of how likely a strong player is to have played each.

### How to tell it works

Calibration on held-out games (bucket the "70%" predictions, check how often right), per opponent style; accuracy against the baseline; the rejection rate; and playing strength, the same search with learned against baseline beliefs. The last is the test that counts.

## 6. Determinization: the full design

Built as a first version ([`docs/stockNavalon.md`](../docs/stockNavalon.md)): copy the real game, wipe what the player doesn't know, fill it back in from the beliefs, repair, check the score. What the full design adds:

- **Enemy cities:** the number of other cities, their locations and levels sampled from a city head, borders over unexplored tiles only.
- **Unexplored terrain** from a terrain head rather than a fresh map, so coastlines continue into the fog and villages keep their spacing.
- **Repair:** city connections recomputed, unit caps respected.
- **Fewer rejections:** draw every part within the remaining score budget (done only for techs when the score is too low).
- **Speed:** stop generating a fresh map for every draw.
- **More than two players.**

**The `Beliefs` interface** is the boundary: the learned model replaces the baseline one part at a time behind it. Baseline values, where there's no head: unexplored terrain at the map generator's rates per climate region; villages and ruins by the generator's spacing and counts; an unseen capital where the starting score allows; techs from the start tech, everything proven, and what fits the score; a few hidden units near their cities.

**Hidden units and the threat map:** the threat map (an input) is exact, from visible units; hidden units become real units in the sample, and the search sees their attacks itself. A possible refinement: an "expected hidden threat" input plane from the heat map, only if the value head is shown to be blind to unseen units (it costs a second network pass).

## 7. Player memory

Some `PlayerView` fields aren't on the real game's screen; they're the player's memory. In self-play the engine fills them in (a perfect memory); playing the real game, the bot has to keep them:

| Field | What it is | Kept from |
| --- | --- | --- |
| `tiles[t].ownRoad` | roads the player built | their own Build commands |
| `scoreHistory` | every published score | the scores, read after every turn |
| `effectsSincePublished`, `effectsInLastChange`, `effectsInChanges` | what the player's actions did to others' scores | their own kills and captures, as seen |
| `attackedOnTurns` | turns they were attacked | HP their units and cities lost on the opponent's turn |
| `convertedAway` | their units taken by conversion | one of their units turning into the opponent's on the same tile |
| `firstOccupied` | the first turn their units or cities stood on each tile | their own units' positions after each command |

Task progress and units' kill counts are shown by the game, so they aren't memory.

**Plan:** a `PlayerMemory` part of the view, with `updateMemory(memory, previous view, view, own commands)` built only from what the player sees and does. A test plays self-play games and checks after every command that it equals what the engine gives, which proves the bot can keep it in the real game and catches any field that secretly needs hidden information.

## 8. Smaller alternatives

- **A transformer over tiles** instead of the convolutional trunk, if long-range structure (roads, ports) matters more than expected.
- **Separate policy and value networks,** if a much cheaper policy turns out useful.
- **Normalizing instead of augmenting:** transform every position so the player's own capital is in one fixed region, as chess engines do for the side to move. One orientation saves learning eight and may give an "enemy is that way" prior in the fogged opening; but it adds no data, a fixed orientation still identifies a game's map, and every search evaluation would need its input transformed and its policy transformed back. The symmetry code serves both.
- **Count heads:** exact integers with a cap (unit count, exploration) put weight on several values at once; a mixture of negative binomials (stars) covers any size with no bins, and two peaks ("spent everything" against "saving up"). Alternatives: fixed-width buckets, a single negative binomial, exact integers for stars too. Compare by held-out log-likelihood and by how realistic determinized samples are.
- **Running the model inside C++** (TorchScript or ONNX export) for fast play without Python; the search wouldn't change.

## 9. Planning turns ahead: a redesign

A brainstorm (2026-11-15/16), open to anything: a bot that plans a couple of turns ahead the way a player does would set this one apart, and the measurements say today's search can't. Section 4 has the earlier options and prior work (Tribes, a Polytopia research framework, is the closest: read it first).

### Why the search can't look ahead

- **Depth is in commands, and a turn is 10–30 of them.** Seeing the player's next turn means 30–60 levels of 50–150 moves each. At 64 simulations 27% of simulations reach the opponent's turn, mean depth 2.8 commands; at 512, 33% and 4.0 (`searches.py --side-simulations 64 512`, 64 games): eight times the budget buys about one command of depth, because it spreads across candidates and orderings. 512 won 60% ± 13 of decided games.
- **Payoffs come a turn later.** A unit must start its turn on a village or ruin to take it, so the search almost never sees the capture; it's down to the value, which barely sees it (+0.02 for a captured village; notes/runs.md). In self-play, a village or ruin in reach goes untaken in 74% of turns (68% even in turns 0–10).
- **Why a player can:** plans, not commands (the order of a turn's moves rarely matters); distant units thought of separately (additive, not multiplicative); attention only where something is happening; the opponent's reply as threats, not a full turn; reasoning back from goals.

### What doesn't get there

- **More simulations:** see above.
- **Front-loading** (most of a turn's budget on its first decision, the rest reusing the tree): tree reuse already counts kept visits toward the next decision (`search.cc`), so this is only uneven phases. Its ceiling is "512 at every decision" above; an efficiency gain (maybe 2× instead of 8× for similar strength), not lookahead. Visits along the played line thin out within a few commands.
- **Bridge-burning MCTS** (Justesen et al.: lock in the best move a level down, phase by phase) is what playing a command and reusing the tree already does. **Non-exploring MCTS** (exploration 0): our exploration comes from the prior, and Gumbel narrows the root; narrower exploration tied (55–56). Both, like OEP, planned only the current turn, scored at its end: a lesson for planning one turn well, not for lookahead.
- **Splitting the board into regions:** on 11×11 maps, all of a player's units join into one region (units within 2 tiles, chained) in 79–85% of turns, 96–100% at 3–4 tiles; the largest holds 17–19 units late. Partitions don't exist here.

### Ideas worth trying

1. **Command-by-command, then turn-by-turn** (the user's ideas, first to try): the player's own turn searched command by command, for granularity; past the turn's end, whole turns as moves, sampled from the policy, for depth. A variant: command by command only up to a budget, the policy completing the rest of the turn, then turn by turn. One setting covers all of them, and full turn-by-turn: a node only gets command-by-command children once visited enough (say 8 times); until then a simulation reaching it has the policy complete the turn. So well-visited lines get precision and the rest a quick completion; very high means turn-by-turn throughout, 1 means commands until End Turn. Every line then ends at a turn boundary, so moves are compared at the same kind of point (today, End Turn's lines end in the opponent's turn and other moves' mid-turn, judged from different views). Turn nodes start with a couple of sampled turns and add more as they're visited, so the opponent's punishing reply can be found, not just its habit. A sampled turn costs 10–30 network evaluations, so budgets count evaluations; compare the setting's range with today's search, at 512–2,048 evaluations a decision, reporting win rates, turns reached, and villages taken.
2. **Opponent replies at turn-ending leaves:** where a line ends the turn, sample a few opponent reply turns and evaluate after them. The smallest step towards 1.
3. **Plan, then refine:** candidate whole turns from the policy, scored against a few opponent replies (the most punishing found kept in the set); then one unit at a time, try its alternatives with the rest fixed, keep improvements (Online Evolutionary Planning's mutations), searching for new punishing replies as the plan changes. Granularity at units × alternatives, not alternatives^units.
4. **Focused local search:** not partitions but focus areas: around each point of interest (unclaimed village or ruin, a city of either side), the units of either side close enough to matter. Measured: within 2 tiles, median 2 / 3 / 5 units (turns 0–10 / 10–25 / 25+), the busiest area 2 / 5 / 8 (90th percentile 4 / 8 / 12); within 3 tiles, busiest 3 / 7 / 12. Search only those units' commands, both sides, 2–3 turns, the rest held (frozen, or the policy's moves), judged by the whole-board value with the line applied (the capture now happens inside the line); combine areas by settling stars, unit cap and shared units. Early on the whole board is about 4 units, and the branching is the economy's commands: unit-only focus search is small there.
5. **Learned focus:** (a) *unit intents*, a head predicting where each unit will be 2–3 turns later (trained from records like the side heads): plans from intents, focus areas where intents meet; (b) attention between units (an architecture change) defining focus areas; (c) a learned turn model (MuZero-style, a turn at a time) searched in the network's own representation; (d) no search, AlphaStar-style (far more compute). Bootstrapping: learned focus learns from games that ignore villages, so rules propose candidates (every village, ruin, city, contact) and the network ranks them.

How to judge any of them: strength at equal network evaluations per turn (`searches.py`-style matches), and the measures above (villages taken, depth reached).

**Status** (2026-11-17): parked until the network is stronger, the value especially. Turn search played even with today's search at 512 evaluations a decision (52% over four matches; notes/runs.md), likely because the value barely rewards what the lookahead finds (a captured village: +0.022). Retest it, and pursue the rest of this section, once the value judges economy better (the λ-return value target is next). The localization ideas (4 and 5: focused local search, learned focus and unit intents) are the ones to explore first then: they're the closest to how a player looks ahead, and nothing here has tested them yet.

**Built** (2026-11-16): idea 1 as `SearchConfig::turnSearch` (docs/stockNavalon.md, search), off by default, compared with `searches.py --turn-search A B --evaluations N`. Full turn-by-turn (the root's own candidates whole turns) isn't: a very large commandVisits comes close (only the first command searched, the policy completing the rest of each turn). A first CPU check (4 games to turn 10, 96 evaluations a decision): 98% of simulations reached the opponent's turn against 52%, 1.02 turns ahead on average against 0.64, 84 evaluations a decision against 76.

## 10. The next run

The ninth run is a testbed: training will restart at some point (more players, larger maps, or something else, not decided yet). Changes are judged by whether they belong in that run. What the ninth run taught (notes/runs.md has the measurements):

1. **The value is the bottleneck.** A captured village is worth +0.02 to the best network, so neither today's search nor turn search can act on what it finds (turn search played even; villages left untaken in 74% of turns). Build the run around learning a good value: λ-return targets from the start (reanalysed with the training network: its raw values beat the best network's search values at predicting results, 0.572 against 0.594), a value share chosen for that, and checks such as a captured village's value tracked from early on.
2. **Gating let self-play go stale.** Nothing was promoted for 26,000 steps; the gates were right (a full match: the training network won 31% ± 8), and its strength swung 24–57% from gate to gate. Whether to gate at all is open: KataGo plays the latest network. If gating stays, its noise has to be measured (the best network against itself on different map sets).
3. **Self-play is CPU-bound.** The GPU sits mostly idle; more players or larger maps make each game dearer.
4. **Search should co-evolve with the network.** A new search tested against a network trained for the old one starts at a disadvantage, so a search that's to matter (localized or turn-level, section 9) should be settled before the restart and trained with from the start.
5. **Cheap controlled experiments.** Most questions were answered on one long run with noisy gates. Short runs from scratch, A against B, at fixed compute, would test design choices properly.
6. **Generality:** determinization and tribe inference are two-player only, the search's backup assumes two sides, and the encoding is padded to a fixed `mapSize` (11) with commands reaching 6 tiles. New code shouldn't add to that where it can be avoided.

Already general, to keep: the opponent pool; single-source rules (`moveSearch.h`); the diagnostics (`versus`, `searches.py`, `--explain`, `pe.search` leaf breakdowns, held-out `bias`, the value-source comparison).
