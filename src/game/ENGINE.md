# Engine notes

How the engine implements GAME.md: what's done, what isn't yet, and the calls made where the spec is silent. Each assumption is worth checking in the real game.

## Layout

- `game.h`: the `Game` class, a plain value (copy it to branch a search). Everything refers to everything else by index; HP, attack and defence are stored ×10.
- `game.cc`: setup, queries, turn order, `legalCommands()` / `isLegal()` / `apply()`
- `fog.cc`: exploration, sight, lighthouses, resource visibility, the Explorer
- `view.h`: `makeView(game, player)`, everything one player knows. Bots should read this, not the `Game`.
- `score.cc`: score and meeting tribes; `ruins.cc`: ruins
- `mapgen.h`: `generateMap(settings)`, Lakes map generation
- `movement.cc`, `combat.cc`, `cities.cc` (population, rewards, connections, capture, pushes, win), `build.cc`
- `Game::rng`: a seeded random state (`GameSettings::seed`), stored in the game so copies stay deterministic
- `data/`: unit, tech, improvement and tribe tables
- `map.h`: the map file format

## Done (milestone 1: core 1v1 Might)

Turns, stars and income, techs (cost, Literacy, backwards research), every improvement and harvest, Destroy, all regular units and boats, movement (roads, forest and mountain stops, zone of control, embark, disembark), combat (formula, retaliation, splash, stomp, Persist, Escape, conversion, infiltration), healing, veterans, unit cap, villages and capture, level-up rewards (except Explorer), borders, road and port connections, siege, Giant pushes, Might and Domination wins, elimination, Resign.

Fog of war: per-player exploration (the 5x5 start, unit sight of 1 or 2, borders always revealed), commands only on explored tiles, Hide (invisible after moving, no Dash on the move that hides it, moves through enemies and enemy cities but never stops on one, revealed by capturing or by an enemy moving onto it, the eye warning), no retaliation against unseen attackers, no connections through unexplored tiles, hidden resources, lighthouses, the Explorer reward, and the per-player view.

Score (GAME.md section 3; the starting scores of every tribe match), the star bonus for meeting a tribe, ruins and their rewards, and Lakes map generation (GAME.md section 18, every step, using the defaults until verified).

## Not done yet

- Score and the star bonus for meeting a tribe
- Monuments and tasks
- Diplomacy (peace, embassies)
- Map generation for other map types (Drylands, Pangea, Continents, Archipelago, Water World)
- The shared neutral road rule (seat-order priority for new connections)

## Assumptions

- **No income on turn 0.** The starting stars are the first turn's stars.
- **Moving uses up abilities.** After a move a unit can't Recover, Heal Others or Capture; only Dash lets it attack. Escape gives one more move after an attack.
- **Promote and Upgrade don't use the turn.** Upgrade is confirmed (game); Promote is assumed.
- **Starfish Harvest** doesn't use the unit's turn.
- **Recover is offered only when hurt.**
- **Roads and cities in enemy territory** give no road movement bonus, the same as for connections.
- **Boats** can always use shallow water; ocean needs Sailing. Boats can pass under bridges.
- **Roads** can't go on villages, ruins or lighthouses. They can share a tile with a building, and buildings can go on road tiles. Ports and bridges need water with nothing on it.
- **Bridge** placement only checks for shallow water. It doesn't check that there's land on both sides.
- **No building on a tile with an enemy unit.**
- **Cloak and Dinghy** can only target enemy cities (Infiltrate), never units or villages.
- **Infiltrated Daggers and Pirates** fill the city tile first, then free tiles further out in its borders. They can't act until the next turn, and they belong to no city.
- **Disband refund** is half of the passenger's cost plus the boat upgrade's cost.
- **Losing the capital** leaves the player with no capital: no +1 income and no connections. No other city takes over.
- **Giant pushes**: "counter-clockwise" is as seen on screen, with y growing downward. Tiles at each distance are tried in order of their angle from the push direction.
- **Level-up rewards** must be chosen before any other command. While one is waiting, `legalCommands()` returns only the two options.
- **Fog: units reveal only around where they end a move**, not along the path.
- **Moving onto a hidden enemy** reveals it. The mover stays where it was and keeps its move (game).
- **A trained Cloak is visible** until it first moves.
- **Hidden units are still physical:** splash and stomp hit them without revealing them. Since Hide units can't stop on enemy cities, they never besiege one.
- **Resource visibility:** fruit shows with Organization, crop with Farming, game with Hunting, metal with Mining, fish with Fishing, starfish with Navigation, each once that tech is researchable or owned.
- **Lighthouses** are left out of the starting 5x5, but one inside the starting borders is found at once (without the population, since the capital isn't set up yet).
- **The Explorer** follows GAME.md §13. Its backtracking penalty is 1 point, so it only breaks ties. It enters the same terrain as units.
- **Stay** is ignored: it's in the command list but never legal.
- **Meeting a tribe** happens when a player first sees one of its units (not a hidden one) or one of its cities (GAME.md section 2). Only the player who sees gets the bonus; the other meets them when they see back. The bonus uses the met tribe's score at that moment.
- **Converted units** keep counting for the score of the player who first had them, and come off that score when they die.
- **Examining a ruin** works like capturing: the unit must have started its turn on the ruin and not moved or attacked yet (the game confirms the first part). It ends the unit's turn. A hidden unit stays hidden.
- **Ruin rewards:** the free tech is a random researchable one; the population reward needs the player to hold their capital; the reward unit appears on the ruin and pushes the examining unit off by the Giant rules (GAME.md section 7), so the unit reward is always possible.
- **Map generation (Lakes):** capitals go at a random spot in their domain, a tile in from its sides and at least 2 from the map edge. Suburbs go 3 to 4 tiles from their capital, round-robin between capitals. Lakes grow from random seeds away from other water, 4 to 12 tiles at a time along straight neighbours, until the map is 25-30% water; if a capital can't reach 2 villages over land, the water is redone (up to 30 times). Starting resources go on tiles off the map edge, overwriting other resources only when the ring is full. Ruins avoid resources and corners. Starfish never go next to a village, a lighthouse or another starfish. All of these are settings in `MapGenSettings`.
- **Resign** is legal but left out of `legalCommands()`, so random play never picks it.
