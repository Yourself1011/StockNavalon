# To do

The working checklist. Designs behind these items are in [`design.md`](design.md), run history in [`runs.md`](runs.md). When something is built, tick it here and describe it in `docs/`.

## Ready to scale: before more compute

What should work well on this machine first, so more compute is spent well.

- [ ] **Training reliably improves:**
  - [ ] the value's overconfidence after the city term faded: the search-value blend, built, to evaluate (Experiments)
  - [ ] the fog schedule seen through to 100% with gates passing again
- [ ] **Efficiency per evaluation:**
  - [ ] Gumbel search at the root ([`design.md`](design.md), "Gumbel search, for small budgets")
  - [x] half-precision inference: float16 by default (network 2× faster on MPS, self-play +10%: the CPU side now dominates)
  - [x] the CPU side of self-play: bigger batches (`--parallel` 128), and each session's games advanced and encoded on a persistent thread pool (`--session-threads`, 4: about 2× the C++ side's throughput)
  - [ ] check the thread pool's gain on a real run's games per minute (the stand-in measure only times the C++ side)
  - [x] tree reuse within a turn
- [ ] **The rules are right:** differential testing against the real game (Playing the real game)
- [ ] **Progress on one scale:** a rating (Elo-style) from all gate and anchor results
- [ ] **A loop that can use more hardware:** several self-play workers feeding one trainer on its own GPU; designed now, built when the hardware is there

## Next: the revised training plan

Why: [`runs.md`](runs.md), second run. Details: [`design.md`](design.md), "Training plan".

- [x] **Measure game lengths** with a 150-turn limit (step 10,001, 48 games; [`runs.md`](runs.md)).
- [x] **A stalemate cutoff:** once anyone has fought, a game stops after 25 turns without an attack or capture (`--stall-turns`), truncated like the turn limit, now 150. The clock starts at the first fight, since openings are quiet. The 25 comes from games capped at 50: revisit with data from longer games, and shorten it once the bot fights in sustained bursts (in a third of decided games, there was a 10-turn lull after the first fight).
- [ ] **A. Wins-only value:**
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
  - [ ] run it: a new run with the schedule from `step_10001`
  - [ ] phase 2: the fogged share rising from 0 to 100%, once the prerequisites below are done
- [ ] **Start a new run** from `runs/second/checkpoints/step_10001.pt`, augmentation off.

## Before fog comes back (phase 2)

- [x] **`decodeBeliefs`:** the belief heads' raw outputs decoded into `Beliefs`; self-play and matches ask the network once per turn per player before searching (`--network-beliefs`, on by default).
  - [ ] the CLI bot (`stocknavalon.play`) still uses `priorBeliefs`
- [x] **A terrain head:** the map-wide `unexplored` belief head (terrain, resource, village/ruin per tile; encoding version 5), and determinization samples unexplored tiles from it within the generator's rules, now public in `mapgen.h`.
- [ ] **Player memory** (`PlayerMemory`, `updateMemory`), for what the game doesn't show again: road builders, the score history, own effects on others' scores, attacks, conversions, first occupation. With a test that it matches the engine's fields after every command.
- [ ] **Peace treaties back on, as a rules curriculum** like fog: off at first, then a growing share of games with treaties (a latched trigger, then a ramp by training steps; records already keep the setting per game), once the bot wins games reliably. Measure the churn (treaty breaks per player-turn) as the share rises. Possibly one trigger for both, treaties ramping before or after fog so the two don't arrive at once. Needed for more than two players.
  - [ ] if it still churns: an input feature for what a break would cost now (the player's units inside the other's borders), like the threat map
- [x] **First-play urgency** in the search: untried moves count as the node's value less a reduction, not 0.
- [ ] **An opponent pool for self-play:** a share of games (say 20%) against earlier best networks instead of the current one. With fog, self-play with determinization doesn't converge to an equilibrium and strategies can cycle; a mix of opponents is the usual answer (fictitious self-play, AlphaStar's league). It also keeps the belief model from assuming every opponent plays like the bot. Once training reliably improves.
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

- [x] **Value targets blended with the search's value** (built: `--search-value-weight` 0.25; to evaluate in a run) (overconfidence; [`design.md`](design.md), "Value targets from the search"): store the root value at every searched move, train the value toward (1 − w) × result + w × that position's search value, w about 0.25 to start; watch held-out value MSE and calibration (mean |prediction| against accuracy). Fallback without new data: shrink the result toward 0 by turns remaining (result × 0.98^turns left).

- [ ] **Augmentation, offline:** from `step_10001` on a frozen buffer, train with augmentation on and off (and the score weight at 0.3 and 0) for about 2,000 steps each, then play each against the random bot on fixed seeds. Decides whether augmentation itself caused the passive play.
- [ ] **Normalizing instead of augmenting:** transform every position so the player's own capital is in one fixed region.
- [ ] **Count heads:** exact integers with a cap and a mixture of negative binomials now; compare fixed-width buckets, a single negative binomial, and exact integers for stars, by held-out log-likelihood and by how realistic determinized samples are.
- [ ] **The search horizon:** compare horizons against each other at equal compute.
- [ ] **The map type as input** (encoding version 6): self-play results by map type before and after (decided %, game length, the held-out unexplored loss), and whether water maps lag.
- [ ] **Learned beliefs against the baseline,** once `decodeBeliefs` exists: calibration, accuracy, the rejection rate, and playing strength.

## Later

- [ ] **Stage 2 search:** one shared tree with observation branching, and the particle filter between turns.
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
