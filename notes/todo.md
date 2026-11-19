# To do

The working checklist. Designs behind these items are in [`design.md`](design.md), run history in [`runs.md`](runs.md). When something is built, tick it here and describe it in `docs/`. The ninth run is a testbed: work is judged by whether it belongs in the next run ([`design.md`](design.md) section 10).

## Now

The value is the bottleneck: it barely sees what villages, ruins, ports and bridges are worth (a captured village +0.02 to +0.045), so neither search can act on them ([`runs.md`](runs.md), "Villages and ruins left alone").

- [ ] **The current settings** (running since 2026-11-18: `--gate-threshold 0.5 --gate-opponents 3 --gate-steps 4000 --parallel 256 --full-share 0.25 --eval-steps 10000`, λ 0.95, value share 0.2): about 1.8× the games a minute (62 against 34), 320,000 passed against all three opponents (62%), rating 1091 → 1146, held-out value 0.44–0.47. Watch over a day: the next gates and the rating trend; policy KL creeping up (0.66 → 0.70: fewer full searches per game, or the held-out set ageing; `--full-share` toward 0.35 if it keeps rising while gates stall). If it holds, these become the next run's defaults. (Threshold 0 alone drifted down within a few promotions; one-opponent gates at 0.5 let noise decide, 284,000's 68% was mostly luck.)
- [ ] **Self-play speed** (data is the bottleneck, notes/runs.md "Network size"): self-play's time is now CPU 40%, GPU 47%, waiting for the GPU lock 13%.
  - [x] profile the CPU side (`profile.h`, `pe.profile_start`/`profile_stop`: thread time per phase). Fixed: the cache fingerprint (four lanes, 11 → 4 µs a position, was 17% of CPU time), legal-move updates (no game copies: 12 → 8 µs a new node). Left: new nodes still ~30% (the game copy 6–9 µs: Game is already flat vectors, so only copy-on-write or smaller state would help), encoding ~24% (threats, view, globals about equal), tree reuse ~150 µs a call (likely freeing the dropped nodes), the reuse check encodes the real game and each root (comparing views instead isn't sound: the encoding reads the game's own move targets, which units in fog affect)
  - [x] opponent-pool games (20%) run as two sessions like ordinary self-play (`PoolPlay` shares `SelfPlay`'s loop)
  - [x] gates and anchors run two sessions too (`evaluate.match` on `selfplay.alternate`, the same games for a seed)
  - [ ] a third self-play session, or fetching results without waiting, so the GPU is never idle while a batch is prepared
  - [ ] a small, cheap network for quick searches (half the decisions, never recorded), the main network for full ones
- [ ] **λ-return value target** (`--value-lambda`, built, with reanalysis): the second try drained the inherited bias (+0.107 → about +0.05) and held-out value is back to about 0.50, but a captured village barely moved (+0.034 against +0.025 for the best network). Raising `--value-share` to 0.2 made no clear difference in its first 3,800 steps. Keep measuring the village value as training goes on.
- [x] **`stocknavalon.diagnose runs/NAME`** (built; about 30 s for 200 games): one command for any checkpoint, the economy checks made so far: the value change from capturing a village, examining a ruin, building a port or bridge; how often each is taken when possible; the search's target against the prior for them; the share of idle units by game phase. Tracked from the start of the next run.
- [ ] **Controlled experiments:** short runs from scratch at fixed compute, A against B, so design choices are tested properly rather than on one long noisy run. Queued for it:
  - [x] network size (`stocknavalon.offline`): 128×10 no better than 64×6 on the same data (policy 2.174 against 2.186, value within noise), both below the run's own network: the bottleneck is data, not capacity (notes/runs.md). Retest with a bigger buffer when there's much more data
  - [ ] the value share with λ targets
  - [ ] auto-heal at End Turn off in training (Recover gives the same result explicitly; it would stop End Turn bundling every heal into one move, though the data says it's minor: damaged units are idle 16–18% of turns, healthy ones 18–23%)
  - [ ] the side-head loss weights, by held-out value loss

## Decisions for the next run ([`design.md`](design.md) section 10)

- [ ] **Gating:** a lower threshold (0.5 broke the stall: 278,000 passed at 52%, then 284,000 at 68%, rating 1132 → 1250; now trying 0, gates as measurements), no gates (KataGo plays the latest network), or gates against several past bests (built: `--gate-opponents`, the same games split among them, judged on the total), so a promotion can't win by countering its predecessor alone. A "not measurably worse" rule (fail only below 50% by about 2 standard errors) is a middle way. Measure gate noise first: the best network against itself on different map sets (`versus --seed`). Optional: SPRT to stop gate matches early.
- [ ] **Value targets:** λ-return from the start (its source a setting, decided by measurement: the training network's raw values, reanalysed, now; self-play's recorded search values, fresh enough if self-play always plays the latest network; search values from the latest network, MuZero's Reanalyse, about 64× the cost; the run measuring each against held-out results now and then)
- [ ] **The search to train with:** a search that's to matter (turn-level or localized, below) has to be settled before the restart and trained with from the start
- [ ] **Network size:** small for now (no gain from 128×10 at this data rate, and self-play would be about 3× slower); compute goes into more and more informative games instead: self-play's CPU cost, positions per game, varied starts. Revisit with much more data
- [ ] **Generality:** what drives the restart (more players, larger maps); new code avoids assuming two players or 11×11 maps

## Rules and the real game ([`design.md`](design.md), "Playing the real game")

- [ ] **A checklist of mechanics to try by hand in the real game**, until differential testing exists: zone of control and fog, boats, Stomp, healing at End Turn, pushes, meeting through diplomacy. Every rule fix so far was found by eye (each is a `RuleVersions` number in `game.h`, so old records replay as they were played).
- [ ] read PolyMod's API, and bengreff's, Polyfish's and superhuman-polytopia-bot's code: what's reusable (state extraction, command mapping), and their licenses
- [ ] stage 1, read-only: a PolyMod mod writing the game state and each command to a file or socket
  - [ ] differential testing of polyengine against the real game, re-syncing at chance events
  - [ ] real maps against our map generator, by their distributions
- [ ] stage 2, control: the bot playing the real game offline through the mod, against the built-in bots (never online)
- [ ] **Player memory** (`PlayerMemory`, `updateMemory`), needed for the real game: what the game doesn't show again (road builders, the score history, own effects on others' scores, attacks, conversions, first occupation), with a test that it matches the engine's fields after every command

## Training, later

- [ ] **Peace treaties back on, as a rules curriculum** like fog: off at first, then a growing share of games with treaties (records already keep the setting per game), once the bot wins games reliably. Measure the churn (treaty breaks per player-turn). Needed for more than two players. If it churns: an input for what a break would cost now.
- [ ] **Varied starting positions** ([`design.md`](design.md), "Varied starting positions"): games forked from positions in recent games (including where one side's value fell below −0.5) or started after a forced opening plan, so the bot learns strategies it doesn't play yet (rider roads, defender catapults, naval) and practises rare situations
- [x] **Prioritized opponents in the pool** (built, `--pool-weighting pfsp`, the default for new starts): pool opponents drawn by (1 − the current network's win rate against each)²; the status line shows the hardest
- [ ] **Tune the search's settings by play** (once the network is more mature): an SPSA tuner, as Stockfish and Leela Chess Zero use, for the tie-breaks and similar settings, so they're set by what wins rather than by hand
- [ ] **A loop that can use more hardware:** several self-play workers feeding one trainer on its own GPU; built when the hardware is there
- [ ] **Scripted opponent styles** (rushers, turtles, expanders) in the opponent pool, for beliefs calibrated to styles the bot doesn't play itself

## Search, later (parked until the value is stronger)

- [ ] **Planning turns ahead** ([`design.md`](design.md) section 9; read the Tribes papers first, a Polytopia-like research framework):
  - [x] turn search built (`SearchConfig::turnSearch`, `--turn-search`); even with today's search at 512 evaluations (52% over four matches)
  - [ ] retest turn search once the value is better, also at self-play's budget (`--evaluations 64`)
  - [ ] explore localization: focused local search around villages, ruins, cities and contact points (only the units near enough to matter, both sides, 2–3 turns), and learned focus (unit intents: where each unit will be in 2–3 turns)
  - [ ] stop sampling new turns when the budget is nearly spent (commandVisits 8 and 1000 overspent about 15%)
- [ ] **Stage 2 search:** one shared tree with observation branching, and the particle filter between turns (ISMCTS-style, multiple-observer, so scouting gets its value: the bot ends turns with units that could have scouted, 5–11% of turns)
- [ ] **Several worlds per search:** one sequential halving per game, shared by its trees, values and the policy target averaged per world; today each tree halves on its own and pooling by visits weighs a move by the worlds where it looks best (inflating End Turn; `stocknavalon.play`'s 4 worlds; self-play and gates use 1)
- [ ] **Tree reuse across turns,** fog-free (the opponent's reply is in the tree too)

## Generality and the belief model

- [ ] **More than two players** ([`design.md`](design.md), "Treaties, and more than two players"): a value per seat, placement targets, mixed-seat gates, a horizon over several players' turns; then treaties in multiplayer, by curriculum, with the opponent pool from the start
- [ ] **Determinization:** more than two players; repair (city connections, unit caps); fewer rejections (draw parts within the remaining score budget: about 60% rejected with the prior, mostly on turn 0); faster (a fresh map is generated for every draw); enemy cities with no evidence and fogged city levels, from a city head
- [ ] **Beliefs:** which belief heads make the opponent look weaker in sampled worlds (0.17 worse for it than in the real game); learned beliefs against the fixed priors (calibration, accuracy, rejection rate, strength in a match)
- [ ] **Tribe inference:** measure against stronger opponents (the measurements are against the random bot); tighten later turns by tracking where each unit could be; units or buildings a tribe couldn't have yet
- [ ] **Export the model** (TorchScript or ONNX) to run inside C++ for play without Python

## Experiments

- [ ] **Results by map type:** decided %, game length and gate results per type, to see whether water maps lag; and whether ports come earlier on water maps now that bridges can't cross the sea
- [ ] **The stars and income heads:** whether they help (the held-out value in the opening, and where the bot builds)
- [ ] **Decided games:** destroys and disband-retrain cycles per game, and game length in decided positions, since the tie-breaks
- [ ] **Normalizing instead of augmenting:** transform every position so the player's own capital is in one fixed region
- [ ] **Count heads:** exact integers with a cap and a mixture of negative binomials now; compare fixed-width buckets, a single negative binomial, and exact integers for stars, by held-out log-likelihood and how realistic determinized samples are

## Done

Details in `docs/` and [`runs.md`](runs.md).

- Training: wins-only value with cut-off games valued by their final search value; side heads (score lead, territory, stars and income ahead, short-term values, turns left); EMA weights, gates and anchors; the fog schedule run to 100%; network beliefs; handicap and catch-up games; the opponent pool; the λ-return value target with reanalysis; NaN guards; a held-out set kept across encoding changes, with a bias measure; a rating from all matches; `--init`
- Search: Gumbel at the root (default); first-play urgency; tree reuse within a turn; exploration candidates; tie-breaks for decided games; the horizon cutoff removed; an evaluation cache; turn search; per-side settings in matches
- Speed: float16 inference, thread pools for self-play's CPU work, self-play's time split in the status line
- Encoding: map type, cities in danger (reach and lethal planes), handicap globals, shore and bridges
- Rules: diplomacy introduces the sender, bridges need land on both sides, adjacent pushes (the houdini), the leftover step after a road step, enemies in fog exerting zone of control (hidden units not), a Giant stomping as it embarks; records keep their rules version (`RuleVersions`) and replay as they were played (`GameRecord::replaysLegally`)
- Tools: `versus` (full matches, with a gate replay), `searches.py` (searches at equal cost), `stocknavalon.play --explain` and `--turn-search`, `pe.search` with leaf breakdowns, one shared move search (`moveSearch.h`), search and self-play settings as objects (`pe.SearchConfig`, `pe.SelfPlayConfig`, `stocknavalon/settings.py`)

## Open questions

- **The score formula:** verify the mid-game rules (temple levels, population and level-up bonuses, losses) against real games.
- **Human games:** are any records available? Supervised pre-training or belief fine-tuning on them would help a lot.
- **The goal:** a strong bot under a time limit per move, eventually playing the real game, or mainly a research platform?
- **Compute:** this machine (Apple Silicon) for now; anything more decides network size and self-play volume.
