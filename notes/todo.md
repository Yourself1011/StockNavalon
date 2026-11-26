# To do

The working checklist. Designs behind these items are in [`design.md`](design.md), run history in [`runs.md`](runs.md), the real-game plan in [`integration.md`](integration.md). When something is built, tick it here and describe it in `docs/`. The ninth run is a testbed: work is judged by whether it belongs in the next run ([`design.md`](design.md) section 10).

## Now

Training from the ninth run's best (320,000) stopped improving: every run from it got worse against it, and the search barely beats itself with more budget (1,024 simulations against 64: 60%), so self-play's targets carry little over the raw network ([`runs.md`](runs.md), "From 320,000"). One cause measured: transpositions, 21–54% of a search's nodes.

- [x] **Transpositions** built ([`design.md`](design.md) section 11): `Game::positionHash()` with its property tests, `SearchConfig::transpositions` (`--transpositions`), off by default
- [ ] **Measure them:** `searches.py --transpositions on off` at 64, 256 and 1,024 simulations (depth, then results); then the scaling matches with them on (`--side-simulations 256 64 --transpositions on`)
  - [x] `pe.search_positions`: repeats per search measured
  - [ ] measured as in section 11: repeats with it on, depth and turns reached (`searches.py`, on against off), then the scaling matches (256 and 1,024 against 64)
  - [ ] if more budget then buys strength: a controlled run from 320,000 with it in self-play
- [ ] **Self-play's search back to command search:** `run.py`'s default is still `--turn-search 0 --replies likely`, which hurt as self-play's target (only command search's first gate improved on 320,000)
- [ ] **The value target:** λ 0.95 (23%) and λ 1 (36%) both lost to 320,000, both with turn search in self-play; retest with command search, and keep tracking a captured village's value (`stocknavalon.diagnose`)
- [ ] **Self-play speed** (data is the bottleneck, notes/runs.md "Network size"): self-play's time is CPU 40%, GPU 47%, waiting for the GPU lock 13%. The CPU side is profiled (`profile.h`; left: new nodes ~30%, mostly the game copy; encoding ~24%; tree reuse ~150 µs a call)
  - [ ] a third self-play session, or fetching results without waiting, so the GPU is never idle while a batch is prepared
  - [ ] a small, cheap network for quick searches (half the decisions, never recorded), the main network for full ones
  - [ ] matches on two sessions (`match(sessions=2)`) where nothing else shares the GPU (searches.py, versus); slower during a run
- [ ] **Controlled experiments:** short runs at fixed compute, A against B (`--init`, `--no-promote`, `--stop-after-gates`). Queued:
  - [ ] the value share with λ targets
  - [ ] auto-heal at End Turn off in training (the data says it's minor: damaged units are idle 16–18% of turns, healthy ones 18–23%)
  - [ ] the side-head loss weights, by held-out value loss
  - [ ] network size again with a bigger buffer, once there's much more data (128×10 was no better than 64×6)

## Decisions for the next run ([`design.md`](design.md) section 10)

- [ ] **Gating:** now threshold 0.5 against 3 past bests (`--gate-opponents`); the alternative is no gates (KataGo plays the latest network). Measure gate noise first: the best network against itself on different map sets (`versus --seed`). Optional: SPRT to stop gate matches early.
- [ ] **Value targets:** λ-return from the start, its source decided by measurement (the training network's raw values, reanalysed, now; self-play's recorded search values; MuZero's Reanalyse, about 64× the cost)
- [ ] **The search to train with:** settled before the restart and trained with from the start: command search with transpositions if section 11 pays off; turn-level or localized search only once it beats that
- [ ] **Network size:** small for now (no gain from 128×10 at this data rate, and self-play would be about 3× slower); revisit with much more data
- [ ] **Generality:** what drives the restart (more players, larger maps); new code avoids assuming two players or 11×11 maps

## The real game ([`integration.md`](integration.md))

The oracle runs the real game's rules; mirrored games and matches against Crazy check ours command by command (docs/oracle.md).

- [ ] the Cloak the real game showed and ours hid: the next mirrored batch stops at it
- [ ] `--crazy-games` inside a training run (not run yet)
- [ ] a few games in the real app with the same seeds, to confirm the headless matches play the same way
- [ ] map comparison (integration.md phase 2) and games at scale with every rule covered (phase 4)

## Training, later

- [ ] **Peace treaties back on, as a rules curriculum** like fog: off at first, then a growing share of games with treaties (records already keep the setting per game), once the bot wins games reliably. Measure the churn (treaty breaks per player-turn). Needed for more than two players. If it churns: an input for what a break would cost now.
- [ ] **Forced openings** ([`design.md`](design.md), "Forced openings"): built in matches (`searches.py --plan`); the bridge rush lost from turn 0 (29%), naval even, but once ahead on continents the bot stalls. Next: `searches.py --plan convert none --plan-when-ahead 0.5 --plan-turns 10 --map-types continents`, judged by the "once ahead" lines; if it converts better, the plan library in self-play
- [ ] **Varied starting positions** ([`design.md`](design.md), "Varied starting positions"): games forked from positions in recent games (including where one side's value fell below −0.5) or started after a forced opening plan
- [ ] **Tune the search's settings by play** (once the network is more mature): an SPSA tuner for the tie-breaks and similar settings
- [ ] **A loop that can use more hardware:** several self-play workers feeding one trainer on its own GPU, when the hardware is there; self-play already runs in its own process (`worker.py`). One evaluation server per network shared by every search in a process (KataGo's; the general form of `crazy.Batcher`) once GPU sharing is the measured bottleneck
- [ ] **Scripted opponent styles** (rushers, turtles, expanders) in the opponent pool, for beliefs calibrated to styles the bot doesn't play itself

## Search, later

- [ ] **Planning turns ahead** ([`design.md`](design.md) section 9): turn search is built (`SearchConfig::turnSearch`) but even with command search at 512 evaluations and worse as self-play's target. Retest after transpositions, and once the value is better
  - [ ] explore localization: focused local search around villages, ruins, cities and contact points, and learned focus (unit intents)
  - [ ] retest discrepancy replies and the π̄ backup once the policy is stronger or budgets larger
  - [ ] stop sampling new turns when the budget is nearly spent (commandVisits 8 and 1000 overspent about 15%)
- [ ] **Stage 2 search:** one shared tree with observation branching, and the particle filter between turns (ISMCTS-style, so scouting gets its value: the bot ends turns with units that could have scouted, 5–11% of turns)
- [ ] **Several worlds per search:** one sequential halving per game, shared by its trees, values and the policy target averaged per world; today each tree halves on its own and pooling by visits weighs a move by the worlds where it looks best (inflating End Turn; `stocknavalon.play`'s 4 worlds; self-play and gates use 1)
- [ ] **Tree reuse across turns,** fog-free (the opponent's reply is in the tree too)

## Generality and the belief model

- [ ] **More than two players** ([`design.md`](design.md), "Treaties, and more than two players"): a value per seat, placement targets, mixed-seat gates, a horizon over several players' turns; then treaties in multiplayer, by curriculum, with the opponent pool from the start
- [ ] **Determinization:** more than two players; repair (city connections, unit caps); fewer rejections (draw parts within the remaining score budget: about 60% rejected with the prior, mostly on turn 0); faster (a fresh map is generated for every draw); enemy cities with no evidence and fogged city levels, from a city head
- [ ] **Memory of enemy units seen before** (an encoding change, so for a restart): the input is the current view only, so a unit seen last turn that moved into fog is forgotten. Add the last-seen enemy units to the view and encoding (type, tile, turns since seen), so the beliefs can place them
- [ ] **Surprise attacks, measured:** over held-out games, each city captured or unit killed by a unit hidden at the start of that turn: seen before or not, and the probability the beliefs gave a hidden unit on or near its real tile; hidden-unit placement calibration per tile, especially on enemy cities and near own cities
- [ ] **Beliefs:** which belief heads make the opponent look weaker in sampled worlds (0.17 worse for it than in the real game); learned beliefs against the fixed priors (calibration, accuracy, rejection rate, strength in a match)
- [ ] **Tribe inference:** measure against stronger opponents (the measurements are against the random bot); tighten later turns by tracking where each unit could be; units or buildings a tribe couldn't have yet
- [ ] **Export the model** (TorchScript or ONNX) to run inside C++ for play without Python

## Experiments

- [ ] **Results by map type:** decided %, game length and gate results per type, to see whether water maps lag; and whether ports come earlier on water maps now that bridges can't cross the sea
- [ ] **The stars and income heads:** whether they help (the held-out value in the opening, and where the bot builds)
- [ ] **Decided games:** destroys and disband-retrain cycles per game, and game length in decided positions, since the tie-breaks
- [ ] **Normalizing instead of augmenting:** transform every position so the player's own capital is in one fixed region
- [ ] **Count heads:** compare the current heads with fixed-width buckets, a single negative binomial, and exact integers for stars, by held-out log-likelihood and how realistic determinized samples are

## Done

Details in `docs/` and [`runs.md`](runs.md).

- Training: wins-only value with cut-off games valued by their final search value; side heads; EMA weights, gates against several past bests and anchors; the fog schedule run to 100%; network beliefs; handicap and catch-up games; the opponent pool, prioritized (`--pool-weighting pfsp`); the λ-return value target with reanalysis; gate games trained on the better side's moves; NaN guards; a held-out set kept across encoding changes, with a bias measure; a rating from all matches; `--init`, `--no-promote`, `--stop-after-gates`
- Search: Gumbel at the root (default); first-play urgency; tree reuse within a turn; exploration candidates; tie-breaks for decided games; the horizon cutoff removed; an evaluation cache; turn search with likely and discrepancy replies; per-side settings in matches
- Speed: float16 inference, thread pools for self-play's CPU work, the CPU profile and its fixes (fingerprint, legal-move updates), opponent-pool games on two sessions, self-play in its own process, self-play's time split in the status line
- Encoding: map type, cities in danger (reach and lethal planes), handicap globals, shore and bridges
- Rules: every fix gets a `RuleVersions` number, and records replay as they were played (`GameRecord::replaysLegally`); the fixes from mirrored and Crazy games are in integration.md section 7
- Real game: the oracle, mirrored games, Crazy matches (`stocknavalon.crazy`, `--crazy-games`, `stocknavalon.play --crazy`)
- Tools: `versus`, `searches.py`, `stocknavalon.diagnose`, `stocknavalon.offline`, `stocknavalon.losses`, `stocknavalon.play --explain` and `--turn-search`, `pe.search` with leaf breakdowns, one shared move search (`moveSearch.h`), search and self-play settings as objects (`pe.SearchConfig`, `pe.SelfPlayConfig`, `stocknavalon/settings.py`)

## Open questions

- **The score formula:** verify the mid-game rules against real games (the oracle can now).
- **The goal:** a strong bot under a time limit per move, or mainly a research platform? (Online play and human replays are out of scope: integration.md.)
- **Compute:** this machine (Apple Silicon) for now; anything more decides network size and self-play volume.
