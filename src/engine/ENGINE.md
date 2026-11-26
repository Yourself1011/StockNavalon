# Engine notes

How the engine implements GAME.md: what's done, and the calls made where the spec is silent. Each assumption is worth checking in the real game. Rule numbers aren't repeated here: they're in `rules.h`, `data/` and `MapGenSettings`.

## Layout

- `game.h`: the `Game` class, a plain value (copy it to branch a search). Everything refers to everything else by index; HP, attack and defence are stored ×10.
- `game.cc`: setup, queries, turn order, `legalCommands()` / `isLegal()` / `apply()`
- `movement.cc`, `combat.cc`, `cities.cc` (population, rewards, connections, capture, pushes, win), `build.cc`
- `moveSearch.h`: the movement rules as one search (`searchMoves`), plus terrain entry, road use and the most steps a move can take. The engine's legal moves use it with what the mover's owner knows; the bot's encoding uses it for where enemy units could go, and tribe inference for how far. Anything that works out movement calls it rather than having its own copy.
- `fog.cc`: exploration, sight, lighthouses, resource visibility, the Explorer
- `score.cc`: score, meeting tribes (`Game::meet`, the one path for every meeting) and Capital Vision
- `position.cc`: `Game::canonicalize` (units and cities renumbered by tile) and `Game::positionHash`, the same for the same position whatever order reached it (the search's transpositions)
- `ruins.cc`: ruins and their rewards
- `tasks.cc`: tasks, which unlock monuments
- `diplomacy.cc`: peace treaties and embassies
- `view.h`: `makeView(game, player)`, everything one player knows. Bots should read this, not the `Game`.
- `legalMoves.h`: `LegalMoves`, the current player's legal commands kept up to date one command at a time, for search. It compares the states before and after a command and regenerates only the unit and tile commands near the changes. It always matches `legalCommands()` as a set; the tests check that after every command on every map type.
- `mapgen.h`: `generateMap(settings)`, every map type
- `bots/randomBot.h`: a bot that plays random legal commands, the CLI's opponent and a test driver
- `cli.h`: the command-line game, with any bot in the `--bot` seats; `main.cc` runs it with the random bot, and `src/stockNavalon/main.cc` with stockNavalon
- stockNavalon's own code is in `src/stockNavalon/`, outside the engine. The engine never includes it.
- `python/bindings.cc` (outside `src/`): the Python module `polyengine`, built with nanobind and scikit-build-core. C++ code uses the engine directly; only Python goes through bindings.
- `Game::rng`: a seeded random state (`GameSettings::seed`), stored in the game so copies stay deterministic. `Game::forced` fixes the next random outcomes instead (ruin rewards, the Explorer's tiles, a converted unit's home, infiltration Daggers), to follow a game played elsewhere (`src/oracle/mirror.py`).
- `data/`: unit, tech, improvement and tribe tables
- `rules.h`: every other rule number of the game. Engine code reads rule numbers only from here and `data/`, and so does everything built on it, so a change to the game is one edit.
- `map.h`: the map file format

## Rule versions

Each fix to how the engine plays the game gets the next `RuleVersions` number (`game.h`), with a comment saying what changed and where it was confirmed, and the engine follows each rule from its version on (`settings.follows(RuleVersions::X)`). Records keep their `GameSettings::rulesVersion`, so old games replay as they were played; new games play `RuleVersions::LATEST`. A rule fix is a new version and its checks, nothing else. Where an assumption below was later settled by a fix, the fix's comment is the reference.

## Done

Everything in GAME.md for the 12 regular tribes:

- **Core game:** turns, stars and income, techs, every improvement and harvest, Destroy, all units and boats, movement, combat, healing, veterans, unit cap, villages and capture, level-up rewards, borders, connections, siege, Giant pushes, Might and Domination wins, elimination, Resign.
- **Fog of war:** per-player exploration, commands only on explored tiles, Hide, no retaliation against unseen attackers, hidden resources, lighthouses, the Explorer, and the per-player view.
- **Score**, published to other players at the end of each turn with the full public history (`Game::scoreHistory`), the star bonus for meeting a tribe, and ruins.
- **Tasks and monuments:** all 7 tasks, each unlocking its monument.
- **Diplomacy:** peace treaties (propose, answer, break), embassies, Capital Vision.
- **Map generation:** all 6 map types, following each type's steps in GAME.md section 18.

The Stay command and Destroy Embassy are never in `legalCommands()` (the game's players never get them); `applyUnchecked` still plays Stay, for following games where the game's own bots used it.

## Assumptions

### Turns and units

- **No income on turn 0.** The starting stars are the first turn's stars.
- **Moving uses up abilities.** After a move a unit can't Recover, Heal Others or Capture; only Dash lets it attack. Escape gives one more move after an attack.
- **Promote and Upgrade don't use the turn.** Upgrade is confirmed (game); Promote is assumed.
- **Recover is offered only when hurt.**
- **Level-up rewards and peace offers** must be answered before any other command. While one is waiting, `legalCommands()` returns only its options.
- **Resign** is legal but left out of `legalCommands()`, so random play never picks it.

### Training settings

Not part of the real game, all public in every view and in tribe inference: `GameSettings::headStart` (extra starting stars), `capitalIncome` (the game's per-player capital income handicap, GAME.md section 3) and `incomeBonuses` (more capital income from a given turn on, for catch-up games, through `Game::capitalIncome`).

### Movement and combat

- **Roads and cities in the borders of a player the mover is hostile to** give no road movement bonus (`roadFor`).
- **Boats** can always use shallow water; ocean needs Sailing. Boats can pass under and stop on bridges.
- **Cloak and Dinghy** can only target enemy cities (Infiltrate), never units or villages.

### Cities and building

- **Bridge** placement needs land on opposite sides of the water tile, north and south or east and west; the map's edge isn't land. Ports and bridges need water with nothing on it.
- **The shore** (`PlayerView::TileView::knownLand`): an unexplored tile directly beside explored shallow water (not ocean) shows as land or water, nothing more.
- **Nothing can be done to a tile with a visible enemy unit on it:** no building, harvesting or destroying, inside your borders too, until the unit is killed or moves off. A hidden enemy unit doesn't block, since legal moves mustn't give it away.

### Fog

- **A trained Cloak is visible** until it first moves.
- **Hidden units are still physical:** splash and stomp hit them without revealing them. Since Hide units can't stop on enemy cities, they never besiege one.
- **Resource visibility:** each resource shows once the tech it needs is researchable or owned.
- **Lighthouses** are left out of the starting 5x5, but one inside the starting borders is found at once (without the population, since the capital isn't set up yet).
- **The Explorer** follows GAME.md section 13. Its backtracking penalty only breaks ties. It enters the same terrain as units.

### Score and ruins

- **What a player knows of their own past,** for reading others' scores: `Game::firstOccupied` (the first turn each tile held one of their units or cities: others meet them by seeing those) and `Player::attackedOnTurns`, both in the view.
- **Examining a ruin** works like capturing: the unit must have started its turn on the ruin and not moved or attacked yet (the game confirms the first part). It ends the unit's turn.
- **Ruin rewards:** the free tech is a random researchable one; the population reward needs the player to hold their capital.

### Tasks and monuments

- **Each player can build each monument once.** GAME.md's "one of each per game" is read as per player.
- **Tasks are checked after every command**, and a task stays done once done.
- **Wealth** counts stars at any moment, even mid-turn.
- **Killer** counts enemy units killed by attacks, retaliation, splash and stomp.
- **Network** counts cities connected to the capital at the same time.
- **Monuments** go on field or shallow water inside the player's borders.

### Diplomacy

- **Peace and embassies need the two tribes to have met.**
- **Proposing peace needs Strategy; answering doesn't.** A player can't propose to a tribe that has already asked them.
- **Allies** can't capture each other's cities, embark at each other's ports, and use each other's roads for movement and connections.
- **Breaking peace** can be done any time on your turn. It freezes all the breaker's units (no move, attack or ability left this turn) and disbands, with no refund, those inside the other tribe's borders; the cooldowns are in `RuleVersions::GAME_COOLDOWN`.
- **Embassies:** all in a capital go when it's captured, and all of an eliminated tribe's ties are removed. The builder's view of the host capital's 3x3 is revealed once, since explored tiles stay live.
- **Capital Vision** reveals just the capital tile of each tribe met.

### Map generation

Every setting and per-type default is in `MapGenSettings` and `typeDefaults` (`mapgen.cc`).

- **Capitals in quadrants** (Drylands, Lakes, Archipelago, Water World): domains of width ÷ k, the grid centred (the odd leftover tile first); each coordinate kept `capitalDomainMargin` in from the domain's sides, weighted by (half − distance from offset half)², the margin shrinking where a domain is too small. **Suburbs** (Lakes, Archipelago): the early-village room ÷ players, clamped to `minSuburbs`–`maxSuburbs`, round-robin between capitals; pre-terrain villages are the density times what room is left, truncated.
- **Ocean** is water with no land among its 4 straight neighbours. **Lighthouses** go on the corner's terrain, land made field. **Villages** stay off the edge ring, with no other edge rule.
- **Lakes, Archipelago, Water World:** water from noise as GAME.md section 18 describes (`shapeFromNoise`); which tiles are lifted is per type (`TypeDefaults`). On Lakes, if a capital can't reach 2 villages over land, the water is redone.
- **Continents and Pangea** (`generateIslands`): as GAME.md section 18 describes. If the capitals can't be placed, the candidate map is dropped and `generateMap` tries another seed (the real game falls back to its noise path).
- **Resources** (`resourceRates`): every resource for the terrain tries at its chance times the tribe's modifier and one of the successes is picked, AquaCrop included on water. Two from a settlement, the same draw `outerShare` of the time.
- **AquaCrop** (`ResourceTypes::AQUACROP`) is visible to everyone, since the tech it needs isn't in the engine, and nothing uses it; building on its tile is allowed as on any resource tile (GAME.md section 4, VERIFY).
- **Starfish** go by random tries over the water tiles; one landing on a water corner is lost to the lighthouse. They never go next to a village or another starfish.
- **Fairness** (`startInequality`, GAME.md section 18): up to `candidates` maps, the first at most `equalityLimit` ending the search, the lowest kept. The territory search keeps its open tiles in a plain list, scanned for the lowest cost (the earliest among equals), as the real game does, since the order decides ties.
- **Starting resources** top the capital's neighbours up to the tribe's list, on random neighbours off the map edge, never one holding a resource the list still needs, changing terrain to fit. Water next to another landmass never becomes land.
- **Climate regions** grow from the capitals in turns, each claiming one random unclaimed tile next to it per turn (8 neighbours), until every tile has one. Map files can mark them with `^N`; without markers, each tile gets its nearest capital's climate.
- **Ruins** (`placeRuins`): one pass in random order with the rules in `mapgen.h` (`RUIN_*`); they can stand on a resource.
