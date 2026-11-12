# Runs: what happened and what we learned

Working notes on training runs and measurements, newest lessons last. The code as it is now is described in [`docs/stockNavalon.md`](../docs/stockNavalon.md); plans are in [`todo.md`](todo.md) and [`design.md`](design.md).

## Speed: profiling the pipeline (Apple Silicon, MPS)

- **Recording, before the network** (10 cores, 100 simulations × 2 samples): about 2,700 positions a second of self-play on 8 threads; encoding 256 examples about 36 ms. A record is about 180 KB, mostly root visits (uniform priors visit nearly every root command). With the score evaluator, almost every game reaches the turn limit.
- **The network** evaluates about 20,000 positions a second from batches of 64 up; self-play with it about 10,000.
- **The first run's iterations took 30 to 50 minutes:** the network's games ran to about 1,000 decisions, twice the bootstrap's, and each training step spent nearly all of its ~290 ms replaying games on the CPU. What came of profiling it:
  - **The C++ side isn't the bottleneck:** one thread supplies about 49,000 positions a second. Spreading a session's games over threads made it slower (starting threads every batch costs more than the work), so a session defaults to one.
  - **Heavy-tailed game lengths:** waiting for a whole set of games spent most of an iteration on the last few, with tiny batches (128 games: 94 done in 10 minutes, the last few taking another 20). Hence continuous self-play.
  - **CPU and GPU in turn:** run one after the other, the GPU sat idle about half the time. Hence two sessions that take turns.
  - **Full policy logits** were about 10 MB a batch off the GPU; gathering only the moves' logits made it a few KB.
  - **Training batches:** 309 ms a batch became about 30 ms (one replay per game for its sampled positions, no legality checks on replay, parallel replays, the next batch encoded in the background). With one position per game, about 40 ms against 20 ms for 4.
- **Playout cap randomization:** at 25% full searches and 25 quick simulations, 83 evaluations a decision on average instead of 188. But with training throttled to 9 samples a recorded position, 25% left training idle about 90% of the time at about 50 steps a minute, so the default became 50%: games about 40% slower, twice the recorded positions each.
- **Cheaper searches, more games:** at 100 simulations × 2 samples (25 quick), self-play made about 8 games a minute and training waited over 90% of the time. 64 × 1 (16 quick) made 24–48 games a minute.
- **A 128 × 1 search with 25% full** (stronger policy targets, same cost) cut captures from 23% to 18%: three moves in four were played by the 16-simulation quick search. Captures are the value head's only win/loss signal, so that trade wasn't worth it.

## The score evaluator as a baseline

- **Against the random bot** (`examples/search_vs_random`): about as good as the one-ply greedy bot, ahead on score in most games by turn 20, rarely winning, at 1 to 3 ms a decision. Two limits: with uniform priors a turn's commands spread the simulations thin and mid-turn positions are scored before the opponent moves (the horizon effect within a turn), and the score rewards a capture only once it happens.
- **The random bot scores well:** the score search at 50 simulations won only 44% against it on score (mean 4,392 against 5,291). Random spends every star every turn and wanders around exploring. So the score lead at the turn limit says little in matches against random; captures are the number to watch.
- **The score search's train-then-disband cycle:** about 87 Train and 86 Disband a game, swapping cheap units for pricier ones (unit score follows cost). Within the rules, but a poor use of stars.

## First run (`runs/first`)

- **The value target, version 1:** the score lead alone, games at the limit weighted by it. Being ahead at the limit looked nearly as good as winning, and captures fell from 22% to 13% in five iterations.
- **Version 2:** mostly the result, `w` = 0.1 on the score lead, fading out between 10% and 60% decided, with games at the limit counted as draws. With about 90% of games capped, the value target was nearly 0 everywhere and the value head learned nothing (loss about 0.002).
- **The boat-swap bug:** the engine let a Scout, Rammer or Bomber "upgrade" to another for free (Scout and Rammer both cost 5), without using the unit's turn. With every move looking alike to the value, the search drifted into the free no-op. Swaps went from 0 a game to over 1,300; about 800 of ~1,100 commands a game in the buffer; 4.5% of turns hit the 200-command cap. Best snapshot step 7,336 (65.6% against random, 70.3% against the score search), then collapse (24–28% against random). Fixed: only a Raft upgrades (`Rules::UPGRADABLE_BOAT`).

## Second run (`runs/second`)

- **Value target version 3:** `w` = 0.3, fading between 10% and 25% decided.
- **Value overfitting:** with reuse 9 and 4 positions per game a batch, the value head picked the eventual winner in 96% of training positions from a game's first third, but 64% of held-out ones; value loss 0.009 training against 0.05 held out. Each game's ~250 positions share one result, and the buffer held about 1,000 games, so the network learned games (probably by their maps) rather than positions. Reuse 4 and one position per game didn't fix it: at step 11,000 the held-out value loss (0.137) was no better than predicting a constant (0.129), though the sign was right in 85% of decided positions (correlation 0.31).
- **Restart artifacts:**
  - **The warm-up was granted at every restart:** 500 extra steps on the same buffer each time. Fixed: only for a new run.
  - **The held-out set was redrawn at every start:** value loss jumped from 0.05 to 0.13 with nothing else changed. Fixed: saved in `heldout.npz`.
  - **The restart wave:** all games start together, so those ending in a capture finish first (decided 29–40%) and those reaching the limit arrive together (8–11%). With the score weight reading the last 256 games, it swung from 0 to 0.29 in ten minutes. The window became 512 games.
- **The score weight read the whole buffer at first,** which turns over in about two hours; it lagged far behind (13% buffer against 20% recent). It reads the latest games now.
- **Evaluations:** step 5,001: 39% against random, 70% against the score search, 80% against step 1. Step 10,001: **78% against random (34% captured, 0% was captured)**, 73% against the score search, 54% against step 5,001. The best network so far.
- **Passive play after augmentation was turned on (13:13):** within about 1,700 steps, commands per turn fell from about 5–6 to under 3, builds and trains collapsed (from 64 and 47 a game to 22 and 13), captures from 23% to 8%. Reproduced on CPU with fixed seeds (step 10,001: 604 commands a game and 38% captured; step 12,902: 420 and 12%). Not a bug in the transform as far as checked (consistent targets, no policy mass on illegal slots, passing tests), not a value bias towards ending turns, not unspent stars or spent units. Rolled back to step 10,001, cutting the 450 games after 13:13.
- **After the rollback:** checkpoints a hundred steps apart played very differently (step 10,152, trained without augmentation, passive; 10,228 near normal), so short live comparisons by restart were confounded. Possible causes: a fresh optimizer after the rollback, a learning rate of 1e-3 too high. Then step 10,001 to 15,001 (augmentation on, later `--lr 3e-4`): commands per turn rose to 6.2, then fell back to 4.4 as captures fell from 26% to 11% and the score weight swung from 0 back to 0.27. **Step 15,001: 34% against random, 8% captured.** The run was stopped; that led to the revised plan ([`todo.md`](todo.md)).
- **Captures by map type** (about 1,000 games, steps 5,000–11,500), which showed land maps aren't easier:

  | Map type | Decided | Capture on turn (mean) |
  | --- | --- | --- |
  | Archipelago | 26% | 31 |
  | Pangea | 25% | 31 |
  | Waterworld | 21% | 31 |
  | Drylands | 19% | 23 |
  | Lakes | 15% | 25 |
  | Continents | 7% | 37 |

- **Commands per turn** (both players' commands ÷ 2 × turns): about 6 for an active network, under 3 for a passive one. Healthy games that ended in a capture by turn 20 averaged 4.0 (early turns have few commands), so the figure reads low right after a restart.

## Game lengths without the 50-turn cap

Step 10,001 playing itself, 48 games, self-play settings (64 × 1, 50% full), a 150-turn limit, on CPU (35 minutes):
- **Decided by turn** 30: 12%, 50: 17%, 75: 21%, then no more up to 150. Captures came on turns 7, 7, 16, 17, 21, 21, 34, 36, 51 and 59.
- **Past turn 50 the network goes passive:** about 2.5 commands per player per turn. It never trained beyond turn 50, and its turn input (turn ÷ 50) leaves the range it has seen. Still, 16 of the 38 undecided games had fighting after turn 50.
- **With the stalemate cutoff** (25 turns without an attack or capture, from the first fight): games would average 73 turns instead of 124, no capture would have been lost, and the undecided games would have stopped between turns 29 and 100, except 10 that kept fighting to 150.

## Third run (`runs/third`): A–C with fog

- **The first gate** (step 12,001 against 10,001) hit the shared 10-minute evaluation limit with 30 of 128 games finished, 32 at a time: games now run to 150 turns. Only 6 were decided (2 won, 4 lost), so it was inconclusive. Its progress lines showed a 57% win rate, which counts games at the limit by score lead, while the gate counts only captures. Fixed: gates play all their games at once with their own 30-minute limit, and report decided games.

## Fourth and fifth runs: trained networks lose to step 10,001

Both from step 10,001, fog-free (the fog trigger never came: about 18% decided), wins-only value with final values, side heads; the fourth with augmentation, the fifth without.
- **Gates:** every candidate lost to 10,001, more so with training. Fourth: 9–12, then 2–14. Fifth: 8–17, 8–9, 2–9, 1–4, 1–12, 2–15, 3–8 (candidate's wins–losses in decided games). Gating kept self-play on 10,001 throughout, healthy at 7–8 commands per turn, so augmentation wasn't the cause, or not the only one.
- **The value targets were nearly empty:** 14% of buffer games decided, about 70% ended by stalemate. The final values (10,001's search values) averaged 0.08 in size (median 0.07), so 68% of training value targets were within 0.1 of 0 and only 9% were ±1.
- **And the network memorized the real results:** on decided games it trained on, the winner right 100% of the time at about ±0.94; on held-out decided games, 88% at about ±0.16, no better than 10,001 (91%, ±0.16). 10,001 had been trained with a score term, so its values carried a sense of being ahead; the new targets mostly said "0 everywhere", a likely reason the candidates played worse.
- **Changes:** the score term back early in a run, fading by steps alone; cut-off positions weigh 0.3 in the value loss; only a quarter of samples train the value; the value loss averaged over the batch. To watch: the gap between training and held-out accuracy on decided positions (100% against 88%), and gates.

## Sixth run, and the learning rate

- **Sixth run** (value fixes, augmentation on): the first gate, at step 12,001, lost 4–12, like the runs before. Step 10,001 against itself with the gate's settings came out 3–4, so the gate is fair.
- **What training did in 2,600 steps:** the weights moved 55% (relative to 10,001; the average 44%), and the policy fit got worse on its own training data (KL to the search targets 0.430 to 0.463).
- **Offline, from 10,001 on the sixth run's games (frozen), 400 steps:** at lr 1e-3, the policy fit on unseen positions went from 0.647 to 0.686 (worse), weights moving 18%; at 2e-4, from 0.647 to 0.630 (better), weights moving 7%. 1e-3 was fine for training from scratch, but past step 10,001 its steps were mostly noise, and every run from 10,001 started a fresh Adam at the full rate. That fits the second run peaking at 10,001 and declining, the 10,152 and 10,228 checkpoints playing so differently, and every first gate failing before value-target effects could build up.
- **Changes:** the learning rate peaks at 2e-4, warms up over 1,000 steps whenever the optimizer is fresh, then cosines down to 5e-5 over 100,000 steps and holds.

## Seventh run: the first passing gate, and the peace churn

- **Gates** (candidate won–lost in decided games, against 10,001): 10–7 and 13–12 at first (the lower learning rate: no early collapse), then a dip from 16,001 to 26,001 (5–14 down to 3–17), recovering to 13–13 and 15–19; **34,001 passed, 22–12**, and self-play switched to it. Then 36,001 and 38,001 lost to 34,001 (12–19, 14–22). Since the switch: decided games up from about 17–22% to 25%, stalemates down from about 73% to 63%, commands per turn about 8.8.
- **Gates fell behind:** 20–30 minutes each against about 21 minutes of training per 2,000 steps, so a waiting request was replaced and some candidates (32,001, 40,001) were never gated. And **anchors never ran** after the first: gates came first and there was almost always one waiting.
- **The peace churn:** games ending undecided had about 34 peace proposals, 33 answers and 29 breaks each in 69 turns; 24% of player-turns broke a treaty (15% in decided games), each freezing the breaker's army and disbanding its units inside the other's borders, the units placed to attack. Turns at peace themselves were only 9–12%.
- **Changes:** peace treaties off in training games; a city term in place of the score term early in a run; gates to 75 turns, stopping as soon as their verdict is settled; anchors before gates.
- **First-play urgency:** the search counted untried moves as 0. In a test with a network that likes one move (prior 0.5) and calls every position −0.6, that move got 4% of 200 visits: every untried move beat every tried one until each had had its turn. With first-play urgency (untried moves at the node's value less 0.2 × √(prior explored)) it got 30%. Most positions in these games are valued below 0 for one side, so the policy was being ignored on half the decisions, and moves always on offer, like diplomacy, got visits.

- **Treaty churn with and without first-play urgency** (34,001 against itself, fog-free, treaties on, the run's settings, 24 games each): treaty breaks per player-turn 31.7% without it and 36.9% with it, proposals 37.8% and 47.0%, attacks per player-turn 0.04 and 0.16, decided 29% and 33%. First-play urgency follows the policy more faithfully, and 34,001's policy, trained with treaties on, has learned the churn itself; so treaties stay off until the curriculum, and come back to a policy that doesn't favour them. It did make the bot attack four times as often.

## Eighth run: treaties off

- **Self-play decided 55–65% of games** (from 17–25%), stalemates about 30% (from about 70%). Gates got 60–75 decided games and took 11–17 minutes: 12,001 passed against 10,001 (44–31), 18,001 against 12,001 (35–27), two promotions in under three hours. Anchors: 12,001 captured 31 of 64 against random and 30 against the score search, losing almost none.
- **Fogged anchors in a fog-free run** sampled from belief heads that hadn't trained (their targets are masked without fog): the anchor against random took 80 minutes instead of at most 10, and 18,001 lost to 12,001 7–11 with fog while the fog-free gate had it ahead 35–27. Fixed: until fog is in training, anchors use the fixed priors.
- **Training policy KL rose** from 0.45 to about 1.0 over the run: to look into (sharper targets, or the policy falling behind).
- **Why policy KL rose:** not a worse fit (training cross-entropy fell from 2.48 to 2.28) but sharper targets (their entropy fell from 2.02 to about 1.3, in steps as gates finished). Gate games were 43% of the buffer's games and 60% of its training positions: every move a full search without root noise. The network getting stronger sharpened self-play's targets too (entropy 1.32). Changed: a quarter of each gate's games is added, thinned to half its moves as training positions, so gate games are roughly 15–20% of the data.


## Ninth run: from 18,001, first-play urgency, thinned gate games

- **Decided games 80–94%** of self-play, so the fog trigger (50% decided) fired at step 20,341 and fog began ramping in.
- **Memorization again,** measured on equal footing (pure win targets, full weights): in 3,500 steps from 18,001 the value MSE on training games fell from 0.58 to 0.14 (winner right 97%), while on held-out games it went from 0.38 to 0.44 (constant baseline 0.68). The buffer held only 670 games: at about 370 training positions a game, 250,000 positions is few results.
- **The held-out set was broken:** drawn as soon as there were 512 held-out positions, 4 per game, it came from a handful of early games; even the starting network scored worse than a constant on it (0.69 against 0.53), and the reported held-out value swung between 0.7 and 1.3. The training value loss (0.02) wasn't comparable to it either: a tenth of samples (then a quarter), cut-off games at 0.3, blended targets.
- **Changes:** the held-out set is drawn from at least 100 held-out games, one position per game sampled (older sets are redrawn); the buffer is 500,000 positions (about 1,300 games); a tenth of samples trains the value.

## Speed: where the time goes, and tree reuse

- **The engine isn't the bottleneck:** with a stand-in network answering at once, one thread plays 65,600 positions a second without fog, 50,100 with fog and prior beliefs, 29,300 with network beliefs (97% of it advancing games and encoding). The run evaluates about 8,000–10,000 a second: the network on the GPU, shared by self-play, training and gates, is the limit.
- **Tree reuse within a turn**, with the ninth run's best network, the run's settings, 3 minutes each: without fog 39.4 evaluations per move without reuse, 31.9 with (5,239 moves against 4,240 in the same time, 24% more); with fog 38.4 and 32.2 (19% more moves). With a stand-in network's uniform priors only 10–14%: visits spread evenly, so the played move's subtree holds little.

- **The overnight stretch (steps 35,885–49,450):** 35,885 promoted (60–39) with the best anchors yet (48 of 64 captured against random, 46–1 against the score search, 29–14 against 25,505 with fog); then six failed gates as fog rose from 52% to 71%, and the brake held fog at 70.7% from about step 42,000 (its log line was missing: the brake's state was compared within one update, while gate results arrive from the other thread; fixed). The belief heads learned slowly (held-out unexplored 0.93 to 0.84, hidden unit count 0.91 to 0.72, opponent's exploration 4.7 to 3.5; stars and techs flat).
- **The value got worse after 35,885, with and without fog alike:** on 3,000 positions from all 299 held-out games, MSE fog-free 0.63 (35,885) against 0.71 (49,450), fogged 0.61 against 0.66 (constant baselines 0.80 and 0.72). Overconfident: mean |prediction| about 0.7–0.75 with the winner right only 74–78% of the time. The city term reached 0 at about step 48,000, leaving pure ±1 targets with 85% of games decided.
- **Inference precision on MPS** (measure.py): float32 / float16 / bfloat16, network alone at batch 256: 19,300 / 40,200 / 40,600 positions a second; at batch 64: 17,900 / 30,000 / 6,100. Self-play with 64 games a session: 13,100 / 14,500 / 12,800; with 16: 8,300 / 6,300 / 5,700. float16 is the default: the network doubled, self-play gained 10%, since the CPU side (one thread a session, transfers, Python) now takes most of a batch's time.
- **A thread pool for each session's C++ work** (2026-11-12): the C++ side alone (stand-in network answering at once, 128 games a session, 64 / 16 simulations, half full searches, 20 seconds each). With fog and network beliefs: 41,000 positions a second on one thread; 66,000 / 84,000 / 89,000 with a pool of 2 / 4 / 8; threads started for every batch (the old way) gave 27,000 with 4. Without fog and with prior beliefs: 71,000 on one thread, 152,000 with 4. `--session-threads` defaults to 4: two sessions take turns, and a gate's match runs alongside them, on a 10-core Mac.
- **The map type wasn't an input** (found 2026-11-12): the network never saw it, though it's public; only the fixed-prior determinization used it, generating unexplored tiles with the real type. With network beliefs, the unexplored head (blind to the type) overwrote those tiles. Encoding version 6 adds it to the end of the global vector; older networks load with zero weights for it (identical outputs), the optimizer starts fresh with the warm-up, and the held-out set is redrawn (a new baseline).
- **Ratings** (2026-11-12, `rating.py`, from every gate and anchor of the ninth run): random 0, score search 202, then the best networks 18,001: 562, 20,799: 576, 23,505: 604, 25,505: 642, 35,885: 723, 54,000: 749, 66,000: 834, 84,000: 900. A steady climb through the fog ramp.
- **The held-out jump after encoding version 6** (0.62 to 0.77) was the set, not the network: redrawn from all 299 held-out games, most of them fully fogged, it's harder (constant baseline 0.845); 84,000, from before the change, scores 0.748 on it and 66,000 0.818. Held-out sets now keep their positions and are encoded again when the encoding changes.
- **Gumbel search against PUCT** (`searches.py`, best network 92,000 on both sides, fog and network beliefs, 64 games each, decided games only): 16 simulations, Gumbel 26 to 27 (49% ± 13); 64 simulations, Gumbel 33 to 23 (59% ± 13). Even at the smallest budget, possibly better at the run's; neither significant alone. Its main gain should be the policy target, which a match can't show, so the test is a training run.
- **NaN weights at about step 99,000** (2026-11-12): the embassy rule (diplomacy introduces the sender) changed how old records replayed. The meeting stars depend on the score when tribes meet, so meeting earlier gave the host fewer stars than it really had, and its later purchases took its stars below zero; a negative star target made the stars loss infinite in about 40% of batches, and one step turned every weight NaN (checkpoint and its average; `best.pt`, 98,000, was clean). Fixed: records keep the rule (`diplomacyMeets`, off for older records), the 486 training and 25 held-out games played under the new rule before it was recorded were marked by where they sit in the files (every game checked to replay legally under its rule), non-finite steps are skipped, and a NaN checkpoint resumes from `best.pt`. Also found: Gumbel targets put weight on moves legal only in the determinized game in about 0.1% of positions (losses of about 1,000, the spiky training KL); that weight is now dropped.
