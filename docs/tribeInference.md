# Tribe inference: ruling tribes out from the opening

The spec for working out which tribes an unmet opponent could be, from public information. It's part of stockNavalon ([`stockNavalon.md`](stockNavalon.md)), in `src/stockNavalon/tribeInference.h`: a starting-score filter, a climate filter, and a search over score changes. Open items are in [`notes/todo.md`](../notes/todo.md).

## 1. Why exact, and what it guarantees

An opponent's tribe is hidden until met (GAME.md section 16), but their score is public: the starting score, then a published score at the end of each of their turns. Early in a game, small differences in those numbers rule tribes in or out. A learned model would blur exactly these distinctions, so this is done by exact search instead.

**The question:** for each tribe still possible, could a player of that tribe have produced exactly these published scores? If not, that tribe is ruled out.

**The guarantee: it never rules out the real tribe.** Anything that can't be known from outside, like how many tiles their units revealed or what resources they had, is bounded generously. A tribe is ruled out only when *no* sequence of legal actions fits. So the result is a set that always contains the truth, as small as the evidence allows. This holds relative to the engine's rules, and the search is tied to the engine directly rather than to a hand-copied list of rules (section 4).

## 2. Interface and inputs

```cpp
struct TribeInferenceConfig {
    int turns = 1;          // how many of the opponent's score changes the search follows
    int maxStates = 5000;   // states kept per turn
    int maxWork = 50000;    // states made in one turn's stages
};

// the tribes `opponent` could be, as `view.player` sees it
TribeSet possibleTribes(const PlayerView &view, int opponent,
                        const TribeInferenceConfig &config = {});
```

Also exposed: `climatesSeen`, `tribesWithStartingScore`, `startingExplored` (the tiles a tribe started with explored, given its starting score), `possibleCapitalTiles` (where its capital could be), `scoreChangesFit` (the search alone) and `searchModelScore` (for the coverage test). Python: `pe.possible_tribes`, `pe.score_changes_fit`, `pe.climates_seen`, `pe.tribes_with_starting_score`, `pe.starting_explored`.

**Inputs,** all public or the viewer's own knowledge:
- the opponent's published scores (`view.scoreHistory`)
- which changes the viewer's own actions touched (`effectsInChanges`), the turns the viewer was attacked (`attackedOnTurns`), and the first turn the viewer stood on each tile (`firstOccupied`)
- the map size, the opponent's head start and capital income (`GameSettings`, public), and whether the viewer has Roads (their roads are open to the opponent)

**Output:** the set of possible tribes. A met opponent's set is just their tribe.

**Turn horizon:** only the first `turns` changes are searched, by default just the opponent's turn 0 (section 9). From turn 1 on, their units could be anywhere within reach, so later changes are ambiguous, and the search grows each turn.

## 3. The opponent's state, as searched

For each candidate tribe, the search tracks (`OpState`):
- stars: the tribe's starting stars plus any head start, with income added at the start of every turn after turn 0 (`Rules::cityIncome` per city, plus embassy income)
- techs: Basic plus the tribe's start tech
- cities: level, population, parks and workshop each; the capital starts at the tribe's capital level
- units: their score exactly (it depends only on cost), and the counts by type only as an upper bound on what they can do (explore, capture, examine ruins, disband)
- territory and explored tiles, starting from the starting score
- reach: the furthest from their capital (Chebyshev) any of their units could be
- whether they've had the meeting stars, whether they could have a port, and counts of lighthouses, ruins, connections, embassies, monuments and temples (by turn built, for their level)

## 4. Everything that can change their score in a turn

A published change must equal the score of that turn's events plus new territory and exploration. A change the viewer's own actions touched (a kill, a capture) isn't read: the search stops there and the tribe stays possible.

**Linked to the engine, not copied from it.** A missing or wrong event is the one way this could wrongly rule out a tribe, so the search never writes a rule's number itself:
- **Tables** come from `data/`: tech tiers and the tree (`researchableWith`), unit costs, movement and abilities, improvement costs, population, adjacency and techs, monuments, tribe starts.
- **Every other rule** comes from `src/engine/rules.h`: score amounts, tech cost, income, embassies, rewards, ruins, sight and border radii, task thresholds.
- **The engine's own enums** drive the choices: level-up rewards from `CityRewards::rewardsForLevel`, ruin rewards as a switch over `Game::RuinReward`. A new reward type is a compiler warning in the search until it's handled.
- **The coverage test** (section 7) checks the search's score model against `Game::score`.

The events (the code is the authority for their amounts):
- **Bought with stars:** research (any researchable tech); training (any trainable unit with its tech; the unit cap is left out); a Raft's upgrade to Scout, Rammer or Bomber; any improvement their techs allow, with its population, including temples, ports, and a Windmill, Sawmill or Forge at any level or levelled up by a building beside it; embassies, one per other player. Roads, bridges, burning and growing forest score nothing and only cost stars, so they're left out. Resources aren't tracked: stars alone bound what they build.
- **Refunds:** clearing forest, at most one per territory tile; disbanding a unit (with `Rules::DISBAND_TECH`).
- **Free, but limited:** capturing a village and examining a ruin (each needs a unit that started its turn there, so never on turn 0), with every ruin reward; finding lighthouses (corners within sight of their reach); city connections; level-ups, with each reward chosen as the level is reached; monuments, once their task could be done (`taskPossible`); the meeting stars (section 5).
- **Not modelled:** markets and starfish, which give stars. A state that could have either stops the search reading, so the tribe stays possible.

## 5. Bounding exploration and territory

Exploration is the flexible part: from none up to a bound. A loose bound explains almost any change as "they explored some tiles", and then nothing is ruled out, so the bound follows geometry. It's always an upper bound, never a guess.

**Per turn, per unit** (`unitExploration`): units can't move into fog, so each step reveals at most the new edge of its sight square (`stepReveal`), and ending on a mountain with a climbing tech widens the sight. Move lengths come from `mostSteps`, with roads. A kill's move-in and Escape's second move need an attack, and in 1v1 the only thing to attack is the viewer, so they count only on turns the viewer was attacked. Units trained this turn can't move.

**Overall, from the capital:**
- Every unit starts on the capital. The search tracks the furthest any unit could be: each turn adds the longest move any of their units could make (`reachStep`), by boat too once a port stood at the turn's start. Explorers and pushes (a Super Unit or a ruin's unit placed on a taken tile) can put a unit anywhere, and from then on the bound is the whole map.
- Everything they've explored is within that reach plus their sight (`sightRadius`: wider with climbing, the Scout skill, or a second city's borders) of their capital.
- **The capital's position comes from the starting score** (`startShapes`): the number of starting tiles explored says how near the capital is to each edge, and whether a corner (whose lighthouse starts unexplored) is inside the square. The square within reach is clipped by the map's edges (`visibleTiles`, `cornersWithin`).

**Territory:** a captured village and a Border Growth claim tiles, which score as territory and reveal what they claim. The match takes as much territory as fits, which allows the most later.

**The meeting stars** (`Rules::meetingBonus`) pay for anything, and the opponent may have met the viewer without being met back. They come once, when the opponent explores a tile with the viewer's unit or city on it. The opponent's capital is on a tile the viewer hasn't explored whose edge distances fit its starting square (`possibleCapitalTiles`), so meeting by a given turn needs a tile the viewer had stood on by then within the opponent's sight of their reach. The search tries each turn with and without the stars when meeting is possible, then checks it against the techs and reach the opponent actually had by the end of the turn.

## 6. The search

Per candidate tribe, turn by turn: add income (after turn 0), try every combination of events the stars can pay for, and keep the states whose score matches the published change with an exploration amount within the bound. The tribe stays possible if any path matches all `turns` changes.

**Keeping it small:**
- **Stages:** a turn runs in stages (research, training, improvements, events, population, monuments), with merging between them, so a stage runs once per distinct state rather than once per route to it.
- **Pruning** (`tooHigh`): a branch stops once its known score, plus the least the population still to place adds, has passed the change by more than one disband could take back.
- **Merging:** states that agree on everything exact merge, keeping the most stars, territory, reach and units of each type and the fewest explored tiles. Each of those only ever allows more, so merging stays sound.
- **Limits:** past `maxStates` or `maxWork`, the search stops reading and the tribe stays possible. The limits bound the time, never the soundness.
- **Cache:** the opponent's history only grows, so the states after each prefix of it are cached (per thread, keyed by everything they depend on). A new change costs one step, and every position in a turn shares the result.

**Two-player games only:** with more players, others' effects are mixed into each change, so `scoreChangesFit` rules nothing out. The turn numbering follows the opponent's turns.

## 7. Tests

In `tests/stockNavalon/tribeInference_test.cc`:
1. **Never wrong** (a hard test): all 144 matchups, mirrors included, checked every turn from both sides over 20 turns of random play; the true tribe is always in the set. The same with head starts and bigger capital incomes. A failure means a missing event or a bound that's too tight.
2. **Coverage,** linking the search to the engine (section 4): after every command of long random games, the search's model of each player's score built from the true state (`searchModelScore`) must equal `Game::score`. It catches a way of scoring the search doesn't model, even one too rare for the soundness test to reach.
3. **Examples:** the starting-score groups, climate regions, a foreign climate settling the tribe, a mirror match staying open, and a turn-0 change that only some tribes' units can explain.

## 8. Where the result goes

- **Encoding:** the possible tribes and, per possible tribe, the capital position as global features ([`encoding.md`](encoding.md) section 4).
- **Determinization:** the tribe is sampled only from the possible set, and the capital is placed on one of `possibleCapitalTiles`.
- **Belief model:** the tribe head's target is masked to the possible set, so it only has to say which of them is likely.

### 8.1 Climate

Each tile shows its tribe's climate (GAME.md section 16). For an unmet opponent:
- **A foreign climate seen** that the tribes already known can't explain belongs to some unmet opponent. When there's only one, their set narrows to it.
- **Only your own climate seen** proves nothing, since mirror matches are possible.
- **With more unmet players,** a foreign climate means *some* of them is that tribe, not which one, so it isn't applied.

## 9. Measurements

What the default horizon rests on: random bots on generated 11×11 maps of every type, 96 games, 1,312 calls with an unmet opponent. The real tribe was never ruled out. The search shrank 45 of the sets beyond what the starting score and climate had done (mean set size 2.45 to 2.41). Horizons of 1 to 6 turns, and work limits of 50,000 and 200,000, gave exactly the same sets: all the narrowing comes from the opponent's turn 0, where every unit's start is known exactly. About 5 ms per call on average, 270 ms at most. Measuring against stronger opponents is open ([`notes/todo.md`](../notes/todo.md)).
