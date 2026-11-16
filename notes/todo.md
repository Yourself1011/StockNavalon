# To do

The working checklist. Designs behind these items are in [`design.md`](design.md), run history in [`runs.md`](runs.md). When something is built, tick it here and describe it in `docs/`.

## Ready to scale: before more compute

What should work well on this machine first, so more compute is spent well.

- [ ] **Training reliably improves:**
  - [ ] the plateau since 212,000 (gates even, 56–55, 53–55): the learning-rate floor halved to 2.5e-5 (`--lr-floor`); if gates still don't pass, the next lever is scale (a bigger network, more games), not a smaller step
  - [x] the value's overconfidence, pinned to the opening: on the held-out set, positions before turn 20 score worse than a constant (1.0–1.2 against 0.86–0.92) while later ones beat it (0.61–0.65 against 0.79–0.86). Decided: the main value stays the pure result (a win probability, as in AlphaZero and KataGo); the original AlphaGo had the same overfitting and fixed it with data, so it should ease with more games.
    - [x] KataGo's short-term value heads (3, 10, 30 turns; encoding version 8), and the search-value blend off by default
    - [x] watch the held-out value before turn 20: with `--reuse 1 --value-share 0.05` it beats a constant (0.68 against 0.97 for turns 0–10, 0.41 against 0.85 for 10–20), so no regularization needed for now
  - [x] the fog schedule seen through to 100% with gates passing again (92,000 and 98,000 passed at full fog; rating 900 → 980)
  - [x] guards: a step whose loss isn't finite is skipped (`skipped` in the status line), and a checkpoint with non-finite weights resumes from `best.pt` ([`runs.md`](runs.md), NaN weights at about 99,000)
  - [x] the held-out set keeps its positions, and is encoded again when the encoding changes
- [x] **Efficiency per evaluation:** (and the evaluation cache, below)
  - [x] Gumbel search at the root ([`design.md`](design.md), "Gumbel search, for small budgets"): built (`--gumbel`); against PUCT with the same network 49% at 16 simulations, 59% ± 13 at 64 (`searches.py`)
  - [x] Gumbel by default, in runs and `stocknavalon.play` (`--no-gumbel` for PUCT); on in the ninth run from 98,000. The training policy KL isn't comparable across the switch
  - [x] half-precision inference: float16 by default (network 2× faster on MPS, self-play +10%: the CPU side now dominates)
  - [x] the CPU side of self-play: bigger batches (`--parallel` 128), and each session's games advanced and encoded on a persistent thread pool (`--session-threads`, 4: about 2× the C++ side's throughput)
  - [x] where self-play's time goes, in the status line: with the thread pool, GPU 53–56%, waiting for the GPU lock 20–26%, CPU 22–24%. The GPU is now the limit, so more GPU (or fewer evaluations) is what speeds this machine up
  - [x] tree reuse within a turn
- [ ] **The rules are right:** differential testing against the real game (Playing the real game)
  - [x] an embassy or a peace offer introduces the sender to the other tribe (`GameSettings::diplomacyMeets`; GAME.md sections 2 and 15)
  - [x] bridges need land on opposite sides (they were causeways across the sea), and the shore on shallow water shows land from water beside it (encoding version 9)
  - [ ] whether ports come earlier on water maps now that bridges can't cross the sea (first-port turn by map type)
  - [x] a pushed unit (Giant, ruin unit) only goes to a neighbouring tile, in the order push direction, one step counter-clockwise, one clockwise, two each way...; with none free it's destroyed, the "houdini" (`GameSettings::adjacentPushes`). It used to search the whole map, teleporting units
  - [x] records keep the rules they were played under, checked by `GameRecord::replaysLegally`. **Every rule change from now on comes with such a setting**, or old records replay into states that never happened (the NaN weights)
- [x] **Progress on one scale:** a rating (Elo-style) from all gate and anchor results (`rating.py`, logged after gates and anchors). Within our pool only; comparing with anything outside it needs the real game's built-in bots
- [ ] **A loop that can use more hardware:** several self-play workers feeding one trainer on its own GPU; designed now, built when the hardware is there

## Next: the revised training plan

Why: [`runs.md`](runs.md), second run. Details: [`design.md`](design.md), "Training plan".

- [x] **Measure game lengths** with a 150-turn limit (step 10,001, 48 games; [`runs.md`](runs.md)).
- [x] **A stalemate cutoff:** once anyone has fought, a game stops after 25 turns without an attack or capture (`--stall-turns`), truncated like the turn limit, now 150. The clock starts at the first fight, since openings are quiet. The 25 comes from games capped at 50: revisit with data from longer games, and shorten it once the bot fights in sustained bursts (in a third of decided games, there was a 10-turn lull after the first fight).
- [x] **A. Wins-only value:**
  - [x] no score term, no schedule (`score_weight_for`, `--score-weight` and the C++ score weight removed)
  - [x] a game cut off at the limit takes each seat's final value as its target: the root value of its last network search, stored in the record (`finalValues`). Bootstrap and older games have none (0).
  - [x] the higher turn limit (150), with the stalemate cutoff
  - [x] the fallback, needed: a score term fading by training steps alone (0.3 to 0 over 30,000 steps); cut-off positions weigh 0.3 in the value loss and only a quarter of samples train the value ([`runs.md`](runs.md), fourth and fifth runs)
- [x] **B. Side heads:**
  - [x] score lead 5 turns ahead, and at the end (a per-turn score table stored in records)
  - [x] final territory per tile, stored in records when a game ends; masked for older records
  - [x] loss weights 0.25 (score pair) and 0.5 (territory)
  - [ ] tune those weights by the held-out value loss, once the new run has data
- [x] **C. Averaged weights and gating:**
  - [x] an EMA of the trainer's weights (decay 0.999, BatchNorm statistics copied), saved in the checkpoint
  - [x] a gate every 2,000 steps: the EMA against the best network, 128 games, judged on decided games only (pass at 55% with at least 20 decided; otherwise inconclusive)
  - [x] self-play only plays the best network, saved as `best.pt`; restarts resume with it
  - [x] anchors (random, score search) every 5,000 steps on the best network, skipped while it hasn't changed
  - [x] `--init` to start a new run from given weights, without the score-evaluator bootstrap (training waits for `--min-positions`)
  - [ ] optional: SPRT to stop gate matches early; a longer turn limit for gate games
- [x] **E. No fog first** (built; the phases are to run):
  - [x] a per-game fog setting in self-play (`--fog-share`); fog-free searches use the real game, its random state reseeded
  - [x] full-information encoding (`explored` keeps meaning tiles actually explored); belief targets masked; records keep `fog`
  - [x] gates follow `--fog-share`; anchors always with fog
  - [x] a fog schedule (`fog.py`): none until 50% of recent games end in a capture, then a 25,000-step ramp to all fog, paused by 3 failed gates in a row, never reversed
  - [x] run it: the ninth run, from 18,001, reached 100% fog
  - [x] phase 2: the fogged share rising from 0 to 100% (the ninth run, with network beliefs)
- [x] **Start a new run** (done by the later runs)

## Before fog comes back (phase 2)

- [x] **`decodeBeliefs`:** the belief heads' raw outputs decoded into `Beliefs`; self-play and matches ask the network once per turn per player before searching (`--network-beliefs`, on by default).
  - [x] the CLI bot (`stocknavalon.play`) uses them too (`--no-network-beliefs` for the priors)
- [x] **A terrain head:** the map-wide `unexplored` belief head (terrain, resource, village/ruin per tile; encoding version 5), and determinization samples unexplored tiles from it within the generator's rules, now public in `mapgen.h`.
- [ ] **Player memory** (`PlayerMemory`, `updateMemory`), for what the game doesn't show again: road builders, the score history, own effects on others' scores, attacks, conversions, first occupation. With a test that it matches the engine's fields after every command.
- [ ] **Peace treaties back on, as a rules curriculum** like fog: off at first, then a growing share of games with treaties (a latched trigger, then a ramp by training steps; records already keep the setting per game), once the bot wins games reliably. Measure the churn (treaty breaks per player-turn) as the share rises. Possibly one trigger for both, treaties ramping before or after fog so the two don't arrive at once. Needed for more than two players.
  - [ ] if it still churns: an input feature for what a break would cost now (the player's units inside the other's borders), like the threat map
- [x] **First-play urgency** in the search: untried moves count as the node's value less a reduction, not 0.
- [x] **Handicap games** (built): 20% of self-play games give one side an edge, public: three quarters as the game handicaps its bots, extra capital income of +1 or +2 a turn (encoding version 14), the rest a head start of 3, 6 or 10 stars (version 13); tribe inference allows for both. So the bot learns to fight from behind rather than freeze, and meets opponents like the game's Hard bots. Watch: the trailing side's win share in the status line (aim 30–40%), and the skip rate (turns ended with nothing done, 63% before).
  - [x] catch-up games keep the value a pure win probability: before the boost (or with none) their positions train only the policy and beliefs, since the input can't show the rescue coming
  - [x] catch-up games, sharing the 20%: from turn 10, once both have met, the first side whose value falls below −0.5 gets +1 or +2 capital income from then on, for mid- and late-game comebacks; and in every handicap game a side below −0.5 searches with twice the simulations. Watch: the share of turns with units idle while losing (27% before), split by game phase.
  - [ ] later: fork from real positions where one side's value fell below −0.5
- [x] **Exploration candidates:** bridges were possible at 26% of turn starts on continents maps but almost never built: the policy had them at a prior of about 0.000 (rank about 52 of 61), so Gumbel search never tried them and training never corrected it. 2 of 16 root candidates in self-play's full searches are now random legal moves. Watch: bridges built per game on water maps, and other rare moves.
- [ ] **Tune the search's settings by play** (later, once the network is more mature): an SPSA tuner, as Stockfish and Leela Chess Zero tune theirs: batches of game pairs between slightly perturbed settings with the best network, stepping toward whichever wins. For the tie-breaks (score utility, win-sooner, a threshold like Leela's so it only acts in decided positions) and an End Turn prior factor while units can act, so these are set by what wins rather than by hand. Roughly a night of GPU for a first estimate; include handicap games, since the tie-breaks matter where self-play rarely goes. For reference: KataGo weights score about 0.4 against 1 for winning; Leela's moves-left effect is a few percent, only beyond a value of about ±0.8.
- [ ] **Varied starting positions** ([`design.md`](design.md), "Varied starting positions"): a share of games forked from positions in recent games or started after a forced opening plan, so the bot learns strategies it doesn't play yet (rider roads, defender catapults, naval), practises rare situations like defending its capital, and spends less of its data on openings.
- [ ] **Prioritized opponents in the pool** (later, once the pool's results are logged): AlphaStar's prioritized fictitious self-play, drawing pool opponents in proportion to how often they beat the current network (for example weight (1 − win rate)²), so practice goes where it's weakest, instead of uniformly.
- [x] **An opponent pool for self-play:** 20% of games (`--pool-share`) against one of the 8 latest earlier bests (`--pool-size`), uniformly, at random seats; only the current network's moves train; the status line shows how often the current network wins. Watch that win rate: well above 50% and steady is expected; a drop against older networks means a strategy cycle the pool is there to catch
  - [ ] step two: gates against several pool networks, not only the best, so a promotion can't win by exploiting the best alone
- [ ] **Gates against several past bests,** not just the current one, so a candidate must be broadly stronger rather than better at countering its predecessor (one-opponent gates can pass every step of a cycle).

## Playing the real game ([`design.md`](design.md), "Playing the real game")

- [ ] read PolyMod's API, and bengreff's, Polyfish's and superhuman-polytopia-bot's code: what's reusable (state extraction, command mapping), and their licenses
- [ ] stage 1, read-only: a PolyMod mod writing the game state and each command to a file or socket
  - [ ] differential testing of polyengine against the real game, re-syncing at chance events
  - [ ] real maps against our map generator, by their distributions
- [ ] stage 2, control: the bot playing the real game offline through the mod, against the built-in bots (never online)
- [ ] player memory (above) becomes necessary here

## More than two players ([`design.md`](design.md), "Treaties, and more than two players")

- [ ] treaties in 1v1 by curriculum (above)
- [ ] three or more players without treaties: a value per seat, placement targets, mixed-seat gates, a horizon over several players' turns
- [ ] treaties in multiplayer, by curriculum, with the opponent pool from the start; measure treaty, break and stalemate rates

## Experiments

- [x] **Value targets blended with the search's value** (built: `--search-value-weight`; evaluated: it didn't stop the opening's overconfidence, and it's now off by default in favour of short-term value heads) (overconfidence; [`design.md`](design.md), "Value targets from the search"): store the root value at every searched move, train the value toward (1 − w) × result + w × that position's search value, w about 0.25 to start; watch held-out value MSE and calibration (mean |prediction| against accuracy). Fallback without new data: shrink the result toward 0 by turns remaining (result × 0.98^turns left).

- [ ] **Normalizing instead of augmenting:** transform every position so the player's own capital is in one fixed region.
- [ ] **Count heads:** exact integers with a cap and a mixture of negative binomials now; compare fixed-width buckets, a single negative binomial, and exact integers for stars, by held-out log-likelihood and by how realistic determinized samples are.
- [x] **The search horizon:** the cutoff removed (unlimited by default); depth is set by the simulations. A per-side horizon in `searches.py` would still allow a comparison if needed.
- [x] **Cities in danger** (encoding version 10): reach planes (where enemy units could step next turn, where the player's could this turn) and four globals (the player's cities, and capital, with an enemy on them or reachable by one). Prompted by games where the bot let cities, even its capital, be captured. Then (version 11) a lethal plane (total damage the visible enemies could deal each of the player's units, over its HP) and two globals for cities, and the capital, held only by a defender that would die: occupying a city with a 5 HP warrior isn't defending it. Watch: cities lost per game, and how often an enemy steps onto an undefended city.
- [x] **No aimless play when a game is decided:** in won positions the bot destroyed its own buildings and disbanded units (80 games with 10+ destroys). Search tie-breaks: the predicted final score lead and turns left (a new moves-left head, encoding version 12), 0.05 each, in choosing moves only.
  - [ ] check destroys and disband-retrain cycles per game, and game length in decided positions, after the run
- [ ] **If tactics get stuck** (the bot not seeing the opponent's next turn): sharper threat planes, a counter-attack check at the end of the turn, whole-turn candidates; first read the Tribes papers (a Polytopia-like research framework) and the long-turn literature ([`design.md`](design.md) section 4)
- [x] **An evaluation cache:** built; 27% of lookups answered with the ninth run's network, the same games as without it (tested), hit rate in the status line
- [x] **The map type as input** (encoding version 6): built; public information with no downside, so not worth its own evaluation.
- [ ] **Results by map type:** decided %, game length and gate results per type, to see whether water maps (archipelago, waterworld) lag.
- [x] **Stars and income heads:** side targets, the player's own stars and income 10 turns ahead (encoding version 7), for long-term planning the one-turn search can't see. Only new games have the data, so they train as the buffer turns over.
  - [ ] see whether they help: the held-out value in the opening, and where the bot builds
- [ ] **Learned beliefs against the fixed priors:** calibration, accuracy, the determinization rejection rate, and playing strength (`--network-beliefs` against `--no-network-beliefs` in a match).

## Later

- [ ] **The passing bias** (notes/runs.md, "Why the bot passes"):
  - [x] value targets weighted per player turn instead of per move (`--value-per-turn`, now off by default): on the redrawn held-out set the bias is +0.17 for the best network (234,000) but about 0 for the training network (260,000, almost all of it trained per move), so per-move weighting wasn't the main cause
  - [ ] the gates: the training network's average beats the best on held-out value (0.50 against 0.58), is unbiased and values villages twice as much, yet nothing has been promoted since 234,000. `stocknavalon.versus` (built) plays them in full and replays what a gate would have said from the same games: run it, and change the gates if they fail a better network
  - [x] inside End Turn's subtree: per-tree sequential halving, pooled by visits, weighs a move by the worlds where it looks best (notes/runs.md)
  - [ ] the fix (for searches with several worlds: `stocknavalon.play`'s 4; self-play and gates use 1): one sequential halving per game, shared by its trees (the same candidates and visits in every world, survivors by the average over worlds of each world's value), and values and the policy target averaged per world; then `searches.py` against the old search
  - [ ] find which belief heads make the opponent look weaker in sampled worlds
- [ ] **Planning turns ahead** (notes/design.md section 9): first, command-by-command search within the turn, then whole turns past it, compared with today's search and full turn-by-turn at equal network evaluations
- [ ] **Stage 2 search:** one shared tree with observation branching, and the particle filter between turns (ISMCTS-style; the multiple-observer variant, so scouting gets its value: today's search plays one sampled hidden state as if known, so a move that would reveal the map gains nothing, and the bot ends turns with units that could have scouted, 5–11% of turns, winning or not).
- [x] **Tree reuse** within a turn: 16–19% fewer evaluations per move with a trained network.
  - [ ] across turns, fog-free (the opponent's reply is in the tree too), if the turn boundary allows it
- [ ] **Determinization:**
  - [ ] more than two players
  - [ ] repair: city connections, unit caps
  - [ ] fewer rejections: draw parts within the remaining score budget (about 60% rejected with the prior, mostly on turn 0)
  - [ ] faster: a fresh map is generated for every draw
  - [ ] enemy cities with no evidence, and fogged city levels, from a city head
- [ ] **Tribe inference:**
  - [ ] measure against stronger opponents (the measurements are against the random bot)
  - [ ] tighten later turns by tracking where each unit could be
  - [ ] units or buildings a tribe couldn't have yet
- [ ] **Export the model** (TorchScript or ONNX) to run inside C++ for play without Python.
- [ ] **Scripted opponent styles** (rushers, turtles, expanders) in the opponent pool, for beliefs calibrated to styles the bot doesn't play itself.

## Open questions

- **The score formula:** verify the mid-game rules (temple levels, population and level-up bonuses, losses) against real games.
- **Human games:** are any records available? Supervised pre-training or belief fine-tuning on them would help a lot.
- **The goal:** a strong bot under a time limit per move, eventually playing the real game, or mainly a research platform?
- **Compute:** this machine (Apple Silicon) for now; anything more decides network size and self-play volume.
