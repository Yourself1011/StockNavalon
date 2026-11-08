# Tribe inference: ruling tribes out from the opening

The spec for working out which tribes an unmet opponent could be, from public information. It's part of stockNavalon (design in [`stockNavalon.md`](stockNavalon.md), sections 4 and 5). Status: **partly implemented** in `src/stockNavalon/tribeInference.h`: the starting-score filter and the climate filter (sections 8.1 and 10). The search over later score changes (sections 3 to 6) and its coverage test are next.

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

**Turn horizon:** only the first `turns` changes are searched (default 5). Later changes are too ambiguous to rule much out, and the search grows each turn. **To measure:** 3, 4, 5 and 6, by how small the set gets and how long it takes (section 7).

## 3. The opponent's state, as searched

For each candidate tribe, the search tracks the opponent's state turn by turn:

| State | Starts as | Notes |
| --- | --- | --- |
| stars | the tribe's starting stars | income is added at the start of every turn after turn 0 |
| techs | Basic plus the tribe's start tech | |
| cities | 1: the capital | with each city's level and population. Luxidoor's capital starts at level 3. |
| capital extras | none | workshop (+1 income), wall, park; from level-up rewards |
| units | the tribe's start unit | by type; for the unit cap and how much they can explore |
| explored tiles | 25 (the 5×5 start), or less near a corner | only used as an upper bound: total explored ≤ map tiles |
| pending rewards | none | a level-up's reward is chosen within the same turn |

**Income** at the start of each turn after turn 0 is the sum, per city, of:
- its level
- +1 for the capital
- +1 for a workshop, and +1 per park

That's GAME.md section 3. Markets and embassies aren't reachable in 5 turns, and starting resources only add population, not income.

## 4. Everything that can change their score in a turn

A published change, after taking out the viewer's own effects, must equal the sum of the events below over that turn. Each row is one event.

**Linked to the engine, not copied from it.** A missing or wrong event is the one way this could wrongly rule out a tribe, so the list isn't trusted on its own:
- **Every number comes from the engine:** costs, tiers, unit costs, population per building, level-up rewards, and the score formula itself (`unitData`, `techData`, `improvementData`, `tribeData`, `rewardsForLevel`, `Game::score`). The inference never restates them.
- **A coverage test** (section 7) plays thousands of engine games. After every command, it checks that each player's score change is explained by one or more of the events below, using the full game state. If the engine ever gains a new way to change score, or an existing one changes amount, that test fails before the inference can go wrong. That holds even for events too rare or too late to show up in the soundness test.

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

Exploration is the flexible part: +5 per tile, anything from none up to a bound. A loose bound lets almost any change through, so it's worth getting tight. It's still always an upper bound, never a guess.

**How units reveal tiles:** around every tile along their path (GAME.md section 16). Units can't move into unexplored tiles, so every tile on a path is already explored, and only the ring of tiles within sight of the path is new. A unit with sight `r` sees a `(2r + 1)²` square from each tile.

A move of `L` tiles (one king's step each) reveals tiles within `r` of the path. All of those lie within `L + r` of the start, and the start's own square was already explored. So per move:

`new tiles ≤ (2(L + r) + 1)² − (2r + 1)²`

| Move | Sight | At most new tiles |
| --- | --- | --- |
| 1 tile (Warrior) | 1 | 16 |
| 2 tiles (Rider) | 1 | 40 |
| 3 tiles (Knight) | 1 | 72 |
| any, ending on a mountain or with the Scout skill | 2 | the same formula with r = 2 |

A path of `L` tiles really only exposes a band along it, so this square bound is loose. A tighter one counts only tiles within `r` of some tile the path could cover. That's worth using if the loose bound turns out to let too much through (section 7 measures it). Roads double `L`.

**Position changes per unit per turn:**
- one move
- a second move for Escape units (Rider), after attacking
- a kill-move-in after a melee kill
- embarking or disembarking also count as a move

So per turn, exploration is at most the sum of that bound over every move each unit makes.

**Bigger reveals:**
- **An Explorer** (level-2 reward or ruin): up to 12 steps, each revealing at most 25. Bounded by the rest of the map.
- **Capturing a village:** its 3×3, up to 9.
- **Border Growth:** up to 16.

**Overall:** total explored never exceeds the map's tiles (121 on Tiny), so the cumulative +5s are capped too.

## 6. The search

Per candidate tribe, turn by turn:

1. **Start of turn** (turns after 0): add income.
2. **Pick the turn's actions:** a combination of the events in section 4 that the stars can pay for (refunds count), respecting techs, unit caps and limits.
3. **Match the score:** the published change, minus the viewer's own effects, must equal the actions' fixed score plus some exploration amount, which must be a multiple of 5 within the bound from section 5.
4. **Carry the resulting state** to the next turn.

The tribe stays possible if any path matches all `turns` changes.

**Keeping it small:**
- Dead ends are cut as soon as a turn's score can't be matched.
- Identical states are merged: the same stars, techs, cities and units reached by different routes.
- Order within a turn doesn't matter, apart from population triggering a level-up and its reward. Actions are chosen as a set, and level-ups are resolved after.
- Stars are tiny early on: 2 to 7 at the start, a few more per turn. So only a handful of purchase combinations fit each turn.

**Second player:** the opponent's first change comes after the viewer's first turn. Their turn 0 still has no income, and the turn numbering follows their turns, not the viewer's.

## 7. Tests and measurements

1. **Never wrong** (a hard test): across thousands of self-play games on every map type and every tribe matchup, the true tribe is always in the possible set, at every turn. Random bots are good at this, since they do odd things a real player wouldn't. A failure means a missing event or a bound that's too tight.
2. **Coverage,** linking the event list to the engine (section 4): across the same games, after every command, each player's score change must be explained by events in the list, worked out from the full game state. This catches events the soundness test might never reach.
3. **Usefulness,** per horizon (3, 4, 5, 6):
   - the average size of the set, and how often it's down to one tribe, turn by turn
   - for the random bot and for the stronger bots once they exist, since real players' openings are more distinctive
4. **Cost:** time per call, per horizon. It runs once per turn per unmet opponent, so even a few milliseconds is fine.

Then pick the horizon from those numbers.

## 8. Where the result goes

- **Encoding:** a per-opponent "could be this tribe" flag for each of the 12 tribes, replacing the opening-change features considered earlier. It's an encoding change, so the version goes up.
- **Determinization:** the tribe is sampled only from the possible set.
- **Belief model:** only has to say which of the possible tribes is likely. There's no hand-set prior: it learns which tribes opponents tend to pick from its training games. Those picks should come from stockNavalon's own tribe choice, made as part of playing well, so the prior follows what's actually strong to play.
- **Climate,** as a filter on the same set (section 8.1).
- **Later:** seen units or buildings that a tribe couldn't have yet.

### 8.1 Climate

Each tile shows its tribe's climate, unambiguously (GAME.md section 16). For the viewer's set of possible tribes for an unmet opponent:
- **Every foreign climate seen** is some opponent's tribe. In 1v1 there's only one opponent, so seeing climate X narrows their set to just X.
- **Only your own climate seen** proves nothing, since mirror matches are possible. The opponent could be your tribe, or you simply haven't reached their land yet.
- **With more players,** a foreign climate means *some* unmet opponent is that tribe, not which one. It's a joint constraint across their sets: the climates seen must all be covered by the opponents' tribes, counting your own tribe if it's a mirror.

Climates also say roughly where the opponent is. Regions are roughly Voronoi-like around capitals, with deviation (GAME.md section 18), so a climate boundary is around the halfway line between capitals. That's useful for determinization and the belief model, but not exact enough to use as a hard rule.

## 9. Open questions

1. **The horizon:** measure 3, 4, 5 and 6 turns (section 7).
2. **How loose the exploration bound can be** before it stops ruling tribes out (section 5).
3. **Rules the engine itself doesn't have yet:** the coverage test links the event list to the engine, so the inference is exactly as right as the engine's score rules. Mid-game score rules still need checking against the real game (`stockNavalon.md` section 5.1).

## 10. Implemented so far

`possibleTribes(view, opponent)` already applies two exact filters, with no search:

1. **Starting score:** a tribe stays only if its fixed starting score plus 5 per explored tile matches the opponent's first published score, for 9 to 25 explored tiles. The fixed part is the start tech's tier, the start unit's cost, the capital at its starting level, and its 9 border tiles, all read from the engine's tables. The range of explored tiles covers capitals near an edge or a corner lighthouse. That's why, say, Oumaji (520 normally) stays possible at 515: a corner-capital Oumaji starts 5 lower. It relies on capitals never being on the map edge, which GAME.md guarantees for villages, and so for capitals.
2. **Climate** (section 8.1): when there's only one unmet opponent, a foreign climate seen that the tribes already known can't explain must be theirs. Your own climate rules nothing out, because of mirror matches.

**Tests** (`tests/stockNavalon/tribeInference_test.cc`):
- the starting-score groups
- climate in maps and games
- a foreign climate settling the tribe, and a mirror match staying open
- soundness: every one of the 144 matchups (mirrors included), checked every turn for 20 turns of random play, never ruled out the true tribe. By turn 20 the tribe was pinned down exactly in 110 of the 144, mostly from climate. That count reflects random play, which scouts aimlessly; a purposeful bot would find climates sooner.

