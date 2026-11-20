# Engine notes

How the engine implements GAME.md: what's done, and the calls made where the spec is silent. Each assumption is worth checking in the real game.

## Layout

- `game.h`: the `Game` class, a plain value (copy it to branch a search). Everything refers to everything else by index; HP, attack and defence are stored ×10.
- `game.cc`: setup, queries, turn order, `legalCommands()` / `isLegal()` / `apply()`
- `movement.cc`, `combat.cc`, `cities.cc` (population, rewards, connections, capture, pushes, win), `build.cc`
- `moveSearch.h`: the movement rules as one search (`searchMoves`), plus terrain entry, road use and the most steps a move can take. The engine's legal moves use it with what the mover's owner knows; the bot's encoding uses it for where enemy units could go, and tribe inference for how far. Anything that works out movement calls it rather than having its own copy.
- `fog.cc`: exploration, sight, lighthouses, resource visibility, the Explorer
- `score.cc`: score, meeting tribes and Capital Vision
- `ruins.cc`: ruins and their rewards
- `tasks.cc`: tasks, which unlock monuments
- `diplomacy.cc`: peace treaties and embassies
- `view.h`: `makeView(game, player)`, everything one player knows. Bots should read this, not the `Game`.
- `legalMoves.h`: `LegalMoves`, the current player's legal commands kept up to date one command at a time, for search. A node keeps one next to its `Game`. It finds what changed by comparing the states before and after, and regenerates only the unit and tile commands near the changes. It always matches `legalCommands()` as a set; the tests check that after every command on every map type.
- `mapgen.h`: `generateMap(settings)`, every map type
- `bots/randomBot.h`: a bot that plays random legal commands, the CLI's opponent and a test driver
- `cli.h`: the command-line game, with any bot in the `--bot` seats; `main.cc` runs it with the random bot, and `src/stockNavalon/main.cc` with stockNavalon
- stockNavalon's own code (the encoding, tribe inference) is in `src/stockNavalon/`, outside the engine. The engine never includes it.
- `python/bindings.cc` (outside `src/`): the Python module `polyengine`, built with nanobind and scikit-build-core. C++ code uses the engine directly; only Python goes through bindings. `CMakeLists.txt` builds the engine (`engine`) and stockNavalon (`stocknavalon`) as static libraries for it.
- `Game::rng`: a seeded random state (`GameSettings::seed`), stored in the game so copies stay deterministic
- `data/`: unit, tech, improvement and tribe tables
- `rules.h`: every other rule of the game: score amounts, costs and income, rewards, radii, combat, healing, task thresholds. Engine code reads rule numbers only from here and `data/`, and so does everything built on it (stockNavalon), so a change to the game is one edit.
- `map.h`: the map file format

## Done

Everything in GAME.md for the 12 regular tribes:

- **Core game:** turns, stars and income, techs (cost, Literacy, backwards research), every improvement and harvest, Destroy, all units and boats, movement (roads, forest and mountain stops, zone of control, embark, disembark), combat (formula, retaliation, splash, stomp, Persist, Escape, conversion, infiltration), healing, veterans, unit cap, villages and capture, level-up rewards, borders, road and port connections (including the shared neutral road rule), siege, Giant pushes, Might and Domination wins, elimination, Resign.
- **Fog of war:** per-player exploration (the 5x5 start, unit sight of 1 or 2, borders always revealed), commands only on explored tiles, Hide (invisible after moving, no Dash on the move that hides it, moves through enemies and enemy cities but never stops on one, revealed by capturing or by an enemy moving onto it, the eye warning), no retaliation against unseen attackers, no connections through unexplored tiles, hidden resources, lighthouses, the Explorer, and the per-player view.
- **Score** (the starting scores of every tribe match GAME.md section 3), published to other players at the end of each turn with the full public history (`Game::scoreHistory`), the star bonus for meeting a tribe, and ruins.
- **Tasks and monuments:** all 7 tasks, each unlocking its monument.
- **Diplomacy:** peace treaties (propose, answer, break), embassies (build, income, vision, destroy), Capital Vision.
- **Map generation:** all 6 map types, following each type's steps in GAME.md section 18 and its defaults until verified.

Ignored on purpose: the Stay command. It's in the command list but never legal.

## Assumptions

### Turns and units

- **No income on turn 0.** The starting stars are the first turn's stars.
- **Moving uses up abilities.** After a move a unit can't Recover, Heal Others or Capture; only Dash lets it attack. Escape gives one more move after an attack.
- **Promote and Upgrade don't use the turn.** Upgrade is confirmed (game); Promote is assumed.
- **Starfish Harvest** doesn't use the unit's turn.
- **Recover is offered only when hurt.**
- **Level-up rewards** must be chosen before any other command. While one is waiting, `legalCommands()` returns only the two options.
- **Disband refund** is half of the passenger's cost plus the boat upgrade's cost.
- **Resign** is legal but left out of `legalCommands()`, so random play never picks it.

### Rule versions

- Each fix to how the engine plays the game gets the next `RuleVersions` number (`game.h`), and the engine follows each rule from its version on (`GameSettings::follows`). Records keep the version, so old games replay as they were played; new games play `RuleVersions::LATEST`. A rule fix is a new version and its check, nothing else.

### Movement and combat

- **Roads and cities in enemy territory** give no road movement bonus, the same as for connections.
- **Zone of control from enemies in fog**: enemies on tiles the mover hasn't explored exert zone of control too, as in the game, so a move can stop short next to a unit the player doesn't know is there. Hidden units (Hide) exert none. Games recorded before this replay under `RuleVersions::UNSEEN_ZONE_OF_CONTROL - 1`, when only units the mover saw did.
- **Boats** can always use shallow water; ocean needs Sailing. Boats can pass under and stop on bridges.
- **Cloak and Dinghy** can only target enemy cities (Infiltrate), never units or villages.
- **Infiltrated Daggers and Pirates** fill the city tile first, then free tiles further out in its borders. They can't act until the next turn, and they belong to no city.
- **Capital income per player** (`GameSettings::capitalIncome`): the game's own handicap (GAME.md section 3), public in every view (`PlayerView::capitalIncome`) and in tribe inference's income; 1 for everyone by default.
- **Income bonuses from a turn on** (`GameSettings::incomeBonuses`): more capital income for a player from a given turn, for training catch-up games; public, through `Game::capitalIncome`.
- **Head starts** (`GameSettings::headStart`): extra starting stars per player, for training handicap games; public, so every player's view has them (`PlayerView::headStart`) and tribe inference adds them to that player's starting stars. Not part of the real game.
- **Giant pushes**: "counter-clockwise" is as seen on screen, with y growing downward. The 8 neighbouring tiles are tried in order of their angle from the push direction; with none free the unit is destroyed. Games recorded before this replay under an older rules version (`RuleVersions::ADJACENT_PUSHES`), which searched rings further out.

### Cities and building

- **Losing the capital** leaves the player with no capital: no +1 income and no connections. No other city takes over.
- **Roads** can't go on villages, ruins or lighthouses. They can share a tile with a building, and buildings can go on road tiles. Ports and bridges need water with nothing on it.
- **Bridge** placement needs land on opposite sides of the water tile, north and south or east and west; the map's edge isn't land.
- **The shore** (`PlayerView::TileView::knownLand`): an unexplored tile directly beside explored shallow water (not ocean) shows as land or water, nothing more.
- **Nothing can be done to a tile with a visible enemy unit on it:** no building, harvesting or destroying, inside your borders too, until the unit is killed or moves off. A hidden enemy unit doesn't block, since legal moves mustn't give it away.
- **The shared neutral road rule:** a network of neutral road tiles "touches" a player when any of its tiles is next to a tile inside that player's borders. When it touches several players, only the first of them in seat order can use it for new connections; connections that already exist keep using it.

### Fog

- **Units reveal along their path,** around every tile they pass (GAME.md section 16). When several routes reach the destination, the path is the one that leaves the most movement.
- **A trained Cloak is visible** until it first moves.
- **Hidden units are still physical:** splash and stomp hit them without revealing them. Since Hide units can't stop on enemy cities, they never besiege one.
- **Resource visibility:** fruit shows with Organization, crop with Farming, game with Hunting, metal with Mining, fish with Fishing, starfish with Navigation, each once that tech is researchable or owned.
- **Lighthouses** are left out of the starting 5x5, but one inside the starting borders is found at once (without the population, since the capital isn't set up yet).
- **The Explorer** follows GAME.md section 13. Its backtracking penalty is 1 point, so it only breaks ties. It enters the same terrain as units.

### Score, meeting and ruins

- **Meeting a tribe** happens when a player first sees one of its units (not a hidden one) or one of its cities, or receives its peace offer or embassy (`Game::meet`, one path for all three). Only the player who meets gets the bonus; the other meets them when they see back. The bonus uses the met tribe's live score at that moment, not the published one, so meeting at a different time gives a different bonus: games recorded before diplomacy introduced the sender replay with `RuleVersions::DIPLOMACY_MEETS`.
- **What a player knows of their own past,** for reading others' scores: `Game::firstOccupied` (the first turn each tile held one of their units or cities: others meet them by seeing those) and `Player::attackedOnTurns`, both in the view.
- **Converted units** keep counting for the score of the player who first had them, and come off that score when they die.
- **Examining a ruin** works like capturing: the unit must have started its turn on the ruin and not moved or attacked yet (the game confirms the first part). It ends the unit's turn. A hidden unit stays hidden.
- **Ruin rewards:** the free tech is a random researchable one; the population reward needs the player to hold their capital.

### Tasks and monuments

- **Each player can build each monument once.** GAME.md's "one of each per game" is read as per player.
- **Tasks are checked after every command**, and a task stays done once done.
- **Pacifist** counts the player's own turns ended without any attack (including conversion and infiltration), starting over when Meditation is researched or when they attack.
- **Wealth** needs Trade and 100 stars at any moment, even mid-turn.
- **Killer** counts enemy units killed by attacks, retaliation, splash and stomp.
- **Network** needs 5 cities connected to the capital at the same time.
- **Monuments** go on field or shallow water inside the player's borders, and can't be destroyed.

### Diplomacy

- **Peace and embassies need the two tribes to have met.** Destroying an embassy in your own capital doesn't.
- **Proposing peace needs Strategy; answering doesn't.** A proposal waits until it's answered. A player can't propose to a tribe that has already asked them, and must answer instead.
- **Allies:**
  - their units can pass through each other, exert no zone of control on each other, and don't besiege each other's cities
  - they can't capture each other's cities
  - each can embark at the other's ports
  - each uses the other's roads, for movement and for connections
  - splash and stomp don't hit them
  - healing is still +4 only inside your own borders
- **Breaking peace** can be done any time on your turn. It freezes all the breaker's units (no move, attack or ability left this turn) and disbands, with no refund, those inside the other tribe's borders.
- **Embassies:**
  - only the host can destroy one, in their own capital
  - all embassies in a capital go when it's captured
  - all of an eliminated tribe's ties are removed
  - the builder's view of the host capital's 3x3 is revealed once, since explored tiles stay live
- **Capital Vision** reveals just the capital tile of each tribe met.

### Map generation

- **Per-type defaults**, all settings in `MapGenSettings`:

  | Type | Water | Pre-terrain villages |
  | --- | --- | --- |
  | Drylands | none, apart from starting fish | none |
  | Lakes | 25.5-28.1% | 0.3 |
  | Pangea | from its growth (about 50%) | none |
  | Continents | from its growth (36-47%) | none |
  | Archipelago | 61.2-64.3% | 0.3 |
  | Water World | 84.7-85.7% | 0.1 |

- **Capitals in quadrants** (Drylands, Lakes, Archipelago, Water World): domains of width ÷ k, the grid centred (the odd leftover tile first); each coordinate 2 in from the domain's sides (`capitalDomainMargin`), weighted by (half − distance from offset half)², the margin shrinking where a domain is too small; at least 2 from the map edge. **Suburbs** (Lakes, Archipelago): the early-village room (`earlyVillageRoom`, t² − capitals with t = int(0.33 × width)) ÷ players, clamped to `minSuburbs`–`maxSuburbs`, 3 tiles out, round-robin between capitals; pre-terrain villages are the density times what room is left, truncated.
- **Ocean** is water with no land among its 4 straight neighbours. **Lighthouses** go on the corner's terrain, land made field. **Villages** stay off the edge ring and 3 apart, with no other edge rule.
- **Lakes, Archipelago, Water World:** water from noise as GAME.md section 18 describes (`shapeFromNoise`): a uniform value per tile, the lifted ones `wetness + (1 - wetness) × value`, smoothed `smoothing` times over each 3x3 with `edgeNoise` outside the map, then the lowest values among the tiles that aren't settlements (or, on Lakes, the outer ring) become water up to a share drawn in the type's range. Which tiles are lifted is per type (`TypeDefaults`). On Lakes, if a capital can't reach 2 villages over land, the water is redone (up to 30 times).
- **Continents and Pangea** (`generateIslands`): as GAME.md section 18 describes, read from the real generator: seeds at domain positions (one in the middle on Pangea), continents grown one at a time to their targets, villages round-robin, capitals from villages by score then refined, the row-by-row fallback, tiny island villages. If the capitals can't be placed, the candidate map is dropped and `generateMap` tries another seed (the real game falls back to its noise path).
- **Resources** (`resourceRates`): every resource for the terrain tries at its chance times the tribe's modifier and one of the successes is picked, AquaCrop included on water. Two from a settlement, the same draw `outerShare` (0.32) of the time.
- **AquaCrop** (`ResourceTypes::AQUACROP`) is visible to everyone, since the tech it needs isn't in the engine, and nothing uses it; building on its tile is allowed as on any resource tile (GAME.md section 4, VERIFY).
- **Starfish:** `ceil(wetness × size² / 25)`, by random tries over the water tiles (one per water tile); one landing on a water corner is lost to the lighthouse.
- **Fairness** (`startInequality`, GAME.md section 18): up to `candidates` (50) maps, the first at most `equalityLimit` (1.01) ending the search, the lowest kept. The territory search keeps its open tiles in a plain list, scanned for the lowest cost (the earliest among equals), as the real game does, since the order decides ties. About 2–3 ms a 14×14 map.
- **Starting resources** top the capital's neighbours up to the tribe's list, on random neighbours off the map edge, never one holding a resource the list still needs, changing terrain to fit. Water next to another landmass never becomes land.
- **Climate regions** grow from the capitals in turns, each claiming one random unclaimed tile next to it per turn (8 neighbours), until every tile has one: equal sizes, irregular borders. Map files can mark them with `^N`; without markers, each tile gets its nearest capital's climate.
- **Ruins** (`placeRuins`): one pass in random order with the rules in `mapgen.h` (`RUIN_*`); they can stand on a resource. **Starfish** never go next to a village or another starfish.
