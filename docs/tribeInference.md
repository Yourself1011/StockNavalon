# Tribe inference: ruling tribes out from the opening

The spec for working out which tribes an unmet opponent could be, from public information. It's part of stockNavalon ([`stockNavalon.md`](stockNavalon.md)), in `src/stockNavalon/tribeInference.h`: a starting-score filter, a climate filter, and a search over score changes. Section 9 has what's built and the measurements; open items are in [`notes/todo.md`](../notes/todo.md).

## 1. Why exact, and what it guarantees

An opponent's tribe is hidden until met (GAME.md section 16), but their score is public: the starting score, then a published score at the end of each of their turns. Early in a game, small differences in those numbers rule tribes in or out. Luxidoor starts with 2 stars, so a +100 on their first turn is impossible for them. A learned model would blur exactly these distinctions, so this is done by exact search instead.

**The question:** for each tribe still possible, could a player of that tribe have produced exactly these published score changes? If not, that tribe is ruled out.

**The guarantee: it never rules out the real tribe.** Anything that can't be known from outside, like how many tiles their units revealed or what resources they had, is bounded generously: assume the most it could be. A tribe is ruled out only when *no* sequence of legal actions fits. So the result is a set that always contains the truth, and it's as small as the evidence allows.

This holds relative to the engine's rules (`GAME.md`, plus the choices in `ENGINE.md`), and it's tied to the engine directly rather than to a hand-copied list of rules (section 4).

## 2. Inputs and output

```cpp
struct TribeInferenceConfig {
    // how many of the opponent's turns to search; later ones only use the
    // starting score and climate (when added)
    int turns = 5;
};

// the tribes `opponent` could be, as seen by `view.player`: a 12-bit set
std::bitset<TribeTypes::TRIBE_SIZE> possibleTribes(const PlayerView &view, int opponent,
                                                   const TribeInferenceConfig &config);
```

**Inputs,** all public or the viewer's own knowledge:
- the opponent's published scores: the starting score, then one per turn end (`view.scoreHistory`)
- the viewer's own visible effects on that opponent within each change (`effectsInLastChange`, kept per change). In 1v1 nobody else affects their score.
- the map size, which bounds total exploration
- *later, as extra filters:* their units and buildings seen (a seen Rider means Riding, or the Oumaji start), and climates seen

**Output:** the set of possible tribes. A met opponent's set is just their tribe.

**Turn horizon:** only the first `turns` changes are searched, by default just 1: the opponent's turn 0. Measured at 1 to 6 turns, only turn 0 ruled anything out (section 9). From turn 1 on, their units could be anywhere within reach, so later changes are too ambiguous, and the search grows each turn. The search handles longer horizons if a stronger opponent's later turns turn out to say more.

## 3. The opponent's state, as searched

For each candidate tribe, the search tracks the opponent's state turn by turn:

| State | Starts as | Notes |
| --- | --- | --- |
| stars | the tribe's starting stars | income is added at the start of every turn after turn 0 |
| techs | Basic plus the tribe's start tech | |
| cities | 1: the capital | with each city's level and population. Luxidoor's capital starts at level 3. |
| capital extras | none | workshop (+1 income), wall, park; from level-up rewards |
| units | the tribe's start unit | their score (5 × cost, whatever the type) exactly; the counts by type only as an upper bound on what they can do (explore, capture, disband) |
| explored tiles | from the starting score: 9 to 25 | the fewest they could have; total explored ≤ what's within sight of their reach (section 5) |
| reach | 0 | the furthest from their capital (Chebyshev) any of their units could be |
| met the player | no | whether they've had the meeting stars yet (section 5) |
| a port | no | whether they could have one, for boats |
| pending rewards | none | a level-up's reward is chosen within the same turn |

**Income** at the start of each turn after turn 0 is the sum, per city, of:
- its level
- +1 for the capital
- +1 for a workshop, and +1 per park

That's GAME.md section 3. Markets and embassies aren't reachable in 5 turns, and starting resources only add population, not income.

## 4. Everything that can change their score in a turn

A published change, after taking out the viewer's own effects, must equal the sum of the events below over that turn. Each row is one event.

**Linked to the engine, not copied from it.** A missing or wrong event is the one way this could wrongly rule out a tribe, and the game itself changes over time, so the search never writes a rule's number itself:
- **Tables** come from `data/`: tech tiers and the tree (`researchableWith`), unit costs, movement and abilities, improvement costs, population and techs, monuments, tribe starts.
- **Every other rule** comes from `src/engine/rules.h`: score amounts, tech cost, income, embassies, rewards and their amounts, ruins, sight and border radii, task thresholds. The engine reads the same constants, so a change to the game is one edit, and the search follows it.
- **The engine's own enums** drive the choices: level-up rewards come from `CityRewards::rewardsForLevel`, and the ruin rewards are a switch over `Game::RuinReward`. A new reward type is a compiler warning in the search until it's handled.
- **The coverage test** (section 7) checks the search's score model against `Game::score` after every command of long random games.
- **Not modelled:** markets and starfish, which give stars. A state that could have either stops the search reading, so the tribe stays possible. Both need tier-3 techs, so they're out of reach in the opening.

The tables below describe the events for review; the code and the coverage test are the authority.

### Bought with stars

| Event | Score | Stars | Requires |
| --- | --- | --- | --- |
| research a tech | +100 × tier | tier × cities + 4, a third less with Literacy (Ai-Mo starts with it) | parent owned, or a child owned (backwards research) |
| train a unit | +5 × unit cost | the unit's cost | its tech; a city with room under its cap (level + 1); the city tile free |
| harvest fruit / hunting / fishing | +5 (1 population) | 2 | Organization / Hunting / Fishing; a matching resource in their borders |
| farm / mine | +10 (2 population) | 5 | Farming + crop / Mining + metal, in their borders |
| lumber hut | +5 | 3 | Forestry, a forest in their borders |
| port | +5 | 7 | Fishing, shallow water in their borders |
| clear forest | 0 | +1 (a refund) | Forestry |
| road | 0, unless it completes a connection | 3 | Roads |
| temple | +100, +5 population | 20 (15 for a forest temple) | Free Spirit (or Meditation, Spiritualism); not reachable in a few turns, but listed for completeness |

**Resources in their borders** aren't known from outside. The search assumes any of their border tiles could hold any resource, up to the number of border tiles (8 around a level-1 capital, more with captured villages). The tribe's guaranteed starting resources (section 17 of GAME.md) are certainly there; everything beyond them is "possible". That bound is generous, and stays sound.

### Free, but limited

| Event | Score | Limit per turn |
| --- | --- | --- |
| **exploring** | +5 per tile | see section 5 |
| **capturing a village** | +100 (the city) + 20 per new border tile (up to 9) | a unit that started its turn on the village, so never on turn 0; it also raises tech costs from then on |
| **a level-up** | +5 per population point, and +(50 − 5n) on reaching level n | follows from population; each level-up's reward is chosen at once (below) |
| **finding a lighthouse** | +5 (+1 population in the capital) | at most 4 per game |
| **a city connection** | +10 (+1 population in each city) | needs a second city, and a road or port route |
| **a ruin** | depends on the reward, below | a unit that started its turn on it, so never on turn 0 |

**Level-up rewards,** chosen as the level is reached:

| Level | Option A | Option B |
| --- | --- | --- |
| 2 | Workshop: +1 income from next turn | Explorer: exploration (section 5) |
| 3 | City Wall: no score | Resources: +5 stars now |
| 4 | Population Growth: +3 population (+15, may level again) | Border Growth: up to +16 border tiles (+20 each), plus their exploration |
| 5+ | Park: +250, +1 income | Super Unit: a Giant (+50) |

**Ruin rewards** (GAME.md section 13):

| Reward | Score | Stars |
| --- | --- | --- |
| a free tech | +100 × tier | 0 |
| 10 stars | 0 | +10 |
| +3 population in the capital | +15, may level up | 0 |
| an Explorer | exploration (section 5) | 0 |
| a veteran Swordsman, or a Rammer carrying a Warrior | +25 or +10 | 0 |

A ruin can explain score with no stars spent, which is exactly why it has to be in the list. Leaving it out would wrongly rule tribes out.

### Losses

| Event | Score |
| --- | --- |
| disbanding a unit (Free Spirit) | −5 × cost, refunding half the cost in stars |
| a unit killed by the viewer | already taken out, as part of the viewer's own effects |
| a unit killed by anyone else | not possible in 1v1 |

## 5. Bounding exploration

Exploration is the flexible part: +5 per tile, anything from none up to a bound. Every score is a multiple of 5, so a loose bound explains almost any change as "they explored some tiles", and then nothing is ruled out. The first version of the search had exactly that problem: a bound per unit per turn, ignoring where the units were, and it never ruled out a single tribe. The bound has to follow geometry. It's still always an upper bound, never a guess.

**Per turn, per unit:** units reveal tiles along their path (GAME.md section 16), and can't move into fog, so only the ring within sight of the path is new. Each step reveals at most `4r + 1` new tiles for sight `r` (a diagonal step), and ending on a mountain with Climbing, at most 16. Roads double the length. A kill's move-in and Escape's second move need an attack. In 1v1 the only thing to attack is the player, who always knows when that happened (`PlayerView::attackedOnTurns`), so they count only on those turns. Units trained this turn can't move.

**Overall, from the capital:** this is what makes the opening informative.
- Every unit starts on the capital, inside the explored 5×5. The search tracks the furthest any unit could be by now: each turn adds the longest move any of their units could make, which is doubled by roads and longer by boat. A boat needs a port from an earlier turn, since moving onto a port ends a land unit's move. Explorers and pushes (a Giant or a ruin's unit placed on a tile that's already taken) can put a unit anywhere, and from then on the bound is the whole map.
- Everything they've explored is within that reach plus sight of their capital: sight 1, or 2 with Climbing (mountains), the Scout skill, or a second city's borders.
- **The capital's position comes from the starting score.** For each tribe, the starting score gives how many of the 5×5's tiles are on the map, and from that, how many of its rows and columns are: 5 when at least 2 from that edge, 4 one tile in, 3 on the edge. A corner's lighthouse starts unexplored, so a count one short means a corner is inside it. The square within reach is clipped by the map's edges accordingly.
- So a Warrior's turn 0 explores nothing: from 1 tile out, sight 1 stays inside the 5×5. A Rider gets 2 tiles out and up to 5 new tiles, and Climbing sees 2 tiles from a mountain.

**Lighthouses** (+1 population each) are on the corners, so a find needs a corner within that square. With no corner inside the 5×5, the nearest one is at least 3 away.

**The meeting stars** (GAME.md section 3) were the other loophole. The opponent might have met the player without being met back, and those stars pay for anything. They come once, when the opponent explores a tile with one of the player's units or cities on it. (The player's embassy or peace offer also introduces them, but needs the player to have met them first, and by then their tribe is known.) The player knows the first turn they stood on each tile (`PlayerView::firstOccupied`), and the opponent's capital is on a tile the player hasn't explored, with edge distances that fit its 5×5. So meeting by a given turn needs one of those tiles within the opponent's sight of where the player had been by then. The search tries each turn both with and without the stars, whenever meeting is possible. Then, at the end of the turn, it checks against the techs and reach the opponent actually had.

**Bigger reveals:**
- **An Explorer** (level-2 reward or ruin): anywhere, bounded by the rest of the map.
- **Capturing a village:** its 3×3, up to 9.
- **Border Growth:** up to 16.

## 6. The search

Per candidate tribe, turn by turn:

1. **Start of turn** (turns after 0): add income.
2. **Pick the turn's actions:** a combination of the events in section 4 that the stars can pay for (refunds count), respecting techs, unit caps and limits.
3. **Match the score:** the published change, minus the viewer's own effects, must equal the actions' fixed score plus some exploration amount, which must be a multiple of 5 within the bound from section 5.
4. **Carry the resulting state** to the next turn.

The tribe stays possible if any path matches all `turns` changes.

**Keeping it small:**
- **Pruning:** a branch stops as soon as its known score has passed the change. Within a turn the known score only rises (each population still to place adds at least 5), except by one disband.
- **Merging:** states that agree on everything exact merge, keeping the most stars, territory, reach and units of each type and the fewest explored tiles. Each of those only ever allows more, so merging stays sound. A unit's score depends only on its cost, so it's kept separately and exactly.
- **Stages:** a turn runs in stages (research, train, improvements, events, population, monuments), with merging between them, so a stage runs once per distinct state rather than once per route to it.
- **Limits:** at most `maxStates` states per turn and `maxWork` states made in a turn's stages. Past either, the search stops reading and the tribe stays possible. The limits bound the time, never the soundness.
- **Cache:** the opponent's history only grows, so the states after each prefix of it are cached. A new change costs one step, and every position in a turn shares the result.

**Second player:** the opponent's first change comes after the viewer's first turn. Their turn 0 still has no income, and the turn numbering follows their turns, not the viewer's.

## 7. Tests and measurements

1. **Never wrong** (a hard test): across thousands of self-play games on every map type and every tribe matchup, the true tribe is always in the possible set, at every turn. Random bots are good at this, since they do odd things a real player wouldn't. A failure means a missing event or a bound that's too tight.
2. **Coverage,** linking the search to the engine (section 4): across long random games, after every command, the search's model of each player's score, built from the true state (`searchModelScore`), must equal `Game::score`. A way of scoring the search doesn't model breaks it, even one too rare or too late for the soundness test to reach. It covers the score formula. Whether the search's moves cover every action the engine allows is what the soundness test checks.
3. **Usefulness and cost,** per horizon: the average size of the set, how often it's down to one tribe, and the time per call. Measured in section 9; the horizon was chosen from it.

## 8. Where the result goes

- **Encoding:** a per-opponent "could be this tribe" flag for each of the 12 tribes ([`encoding.md`](encoding.md) section 4).
- **Determinization:** the tribe is sampled only from the possible set.
- **Belief model:** the tribe head is masked to the possible set, so it only has to say which of them is likely, learned from its training games.
- **Climate,** as a filter on the same set (section 8.1).

### 8.1 Climate

Each tile shows its tribe's climate, unambiguously (GAME.md section 16). For the viewer's set of possible tribes for an unmet opponent:
- **Every foreign climate seen** is some opponent's tribe. In 1v1 there's only one opponent, so seeing climate X narrows their set to just X.
- **Only your own climate seen** proves nothing, since mirror matches are possible. The opponent could be your tribe, or you simply haven't reached their land yet.
- **With more players,** a foreign climate means *some* unmet opponent is that tribe, not which one. It's a joint constraint across their sets: the climates seen must all be covered by the opponents' tribes, counting your own tribe if it's a mirror.

Climates also say roughly where the opponent is. Regions are roughly Voronoi-like around capitals, with deviation (GAME.md section 18), so a climate boundary is around the halfway line between capitals. That's useful for determinization and the belief model, but not exact enough to use as a hard rule.

## 9. Implemented

`possibleTribes(view, opponent, config)` applies three exact filters:

1. **Starting score:** a tribe stays only if its fixed starting score plus 5 per explored tile matches the opponent's first published score, for 9 to 25 explored tiles. The fixed part is the start tech's tier, the start unit's cost, the capital at its starting level, and its 9 border tiles, all read from the engine's tables.
2. **Climate** (section 8.1): when there's only one unmet opponent, a foreign climate seen that the tribes already known can't explain must be theirs. Your own climate rules nothing out, because of mirror matches.
3. **The score-change search** (sections 3 to 6), `scoreChangesFit`, for each tribe left, over the first `config.turns` changes. The default is 1, just their turn 0: that's where all the narrowing came from, below. Two-player games only: with more players, others' effects are mixed into each change.

**Measurements,** random bots on generated 11×11 maps, every map type, 96 games, 1,312 calls with an unmet opponent:
- **Never wrong:** 0 calls ruled out the real tribe.
- **Narrowing:** the search shrank 45 of the 1,312 sets, beyond what the starting score and climate had done. The mean set size went from 2.45 to 2.41.
- **Horizon:** 1 to 6 turns, and work limits of 50,000 and 200,000, all gave exactly the same sets. All of the narrowing comes from the opponent's turn 0, the one turn where every unit's start is known exactly.
- **Cost:** 5 ms per call on average, 12 ms at the 90th percentile, 270 ms at most.

The search is the most distinctive on turn 0. For example, a +25 there leaves only Oumaji (a Rider reaches 2 tiles out) and Xin-xi (Climbing) of the tribes sharing that starting score. Imperius and Bardur can't get there with a Warrior, and Kickoo's port would take until turn 1 to sail.

**Tests** (`tests/stockNavalon/tribeInference_test.cc`):
- the starting-score groups
- climate in maps and games
- a foreign climate settling the tribe, and a mirror match staying open
- the turn-0 example above
- coverage: the search's score model equals `Game::score` after every one of 70,000 random-game commands, for both players. Planting a gap (parks, temple levels, monuments, unit score) makes it fail.
- soundness, with the search at its default horizon: all 144 matchups (mirrors included), checked every turn from both sides for 20 turns of random play (5,760 checks), never ruled out the true tribe. By turn 20 the tribe was pinned down in 110 of the 144, mostly from climate.

**What the engine records for it:** `PlayerView::effectsInChanges` (the player's own effects within each published change), `attackedOnTurns` and `firstOccupied`. All three are the player's own knowledge.
