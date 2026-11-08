# Using polyengine from C++

polyengine, the game engine, is a C++20 library. Bots, search and self-play in C++ use it directly. There's no binding layer, and nothing between your code and the engine. The rules it implements are in [`src/engine/GAME.md`](../src/engine/GAME.md); the choices made where the rules are silent are in [`src/engine/ENGINE.md`](../src/engine/ENGINE.md).

Two complete, compiled examples go with this guide:

- [`examples/random_game.cc`](../examples/random_game.cc): a full game between two random bots.
- [`examples/one_ply_search.cc`](../examples/one_ply_search.cc): a small search bot using `LegalMoves` and copies.

## Building and linking

CMake builds the engine as the static library target `engine`, with `src/engine` as its include directory. stockNavalon's code (the encoding and tribe inference) is the separate target `stocknavalon`, with `src/stockNavalon` as its include directory; it links the engine, so linking `stocknavalon` gives you both. Build with the presets (see the README):

```bash
cmake --preset release
cmake --build --preset release
./build/release/random_game
```

The `dev` preset builds the same targets with sanitizers, for catching memory errors and undefined behaviour while developing a bot.

To add your own program, add it to `CMakeLists.txt` the way the examples are:

```cmake
add_executable(my_bot my_bot.cc)
target_link_libraries(my_bot PRIVATE engine)
```

From a separate CMake project, `add_subdirectory(path/to/bot)` and link `engine` the same way. Headers are included by their path inside `src/engine`, e.g. `#include "game.h"`, `#include "bots/randomBot.h"`.

## The basics

```cpp
#include "game.h"
#include "mapgen.h"

std::vector<TribeTypes::Tribe> tribes = {TribeTypes::IMPERIUS, TribeTypes::BARDUR};
MapData map = generateMap({.type = MapTypes::LAKES, .tribes = tribes, .seed = 7});
Game game(map, tribes, {.seed = 7});

// there's no turn limit in the rules, so a game can go on forever
while (!game.gameOver() && game.turn < 100) {
    std::vector<Command> legal = game.legalCommands();
    game.applyUnchecked(legal[0]);   // or game.apply(), which checks first
}
```

### Things to know

- **`Game` is a plain value.** Everything inside refers to everything else by index (tiles, units, cities, players), so `Game copy = game;` is a complete, independent game. Copying a Small map takes about 0.1 µs. Branch a search by copying.
- **Tiles are indices**, `y * size + x`. Use `game.index(x, y)`, `game.tileX(t)`, `game.tileY(t)`. Distance is Chebyshev: diagonals count as one step (`game.distance(a, b)`).
- **HP, attack and defence are ×10**, so every value is an exact integer. A full-health Warrior has `hp == 100`.
- **Units are indices into `game.units`**, and dead units stay there with `alive == false`, so always check `alive`. A dead unit's slot is reused, so don't hold on to an index across commands. Find units by tile instead: `game.unitAt(tile)`, or `game.tiles[t].unit`, which is -1 when empty.
- **Randomness lives in the game** (`GameSettings::seed`, used for ruins and the Explorer), so a copy plays out the same way as the original. Map generation takes its own seed.
- **Only change the game through commands.** Public functions like `spawnUnit`, `reveal` and `addPopulation` exist for tests and for setting up positions, and they skip the rules.

### Commands

A `Command` is `{type, from, to, arg}`. Unused fields are -1.

| Type | `from` | `to` | `arg` |
| --- | --- | --- | --- |
| `MOVE`, `ATTACK`, `DISEMBARK` | unit's tile | target tile | |
| `RECOVER`, `HEALOTHERS`, `PROMOTE`, `DISBAND`, `CAPTURE`, `EXAMINERUINS`, `HARVEST` | unit's tile | | |
| `UPGRADE` | boat's tile | | `UnitType` (Scout, Rammer, Bomber) |
| `BUILD` | tile | | `ImprovementType` |
| `DESTROY` | tile | | |
| `TRAIN` | city's tile | | `UnitType` |
| `RESEARCH` | | | `TechType` |
| `CITYREWARD` | city's tile | | `CityReward` |
| `PEACETREATY`, `BREAKPEACE`, `ESTABLISHEMBASSY`, `DESTROYEMBASSY` | | | the other player |
| `PEACEREQUESTRESPONSE` | | | proposer × 2, plus 1 to accept |
| `ENDTURN`, `RESIGN` | | | |

`toString(game, command)` describes a command in words, e.g. `move (3,4) -> (4,5)`.

- **`legalCommands()`** lists every legal command for the current player except Resign. Resign is always legal, but it's left out so random play never picks it.
- **`apply(command)`** throws `std::invalid_argument` if the command isn't legal. **`applyUnchecked(command)`** skips that check, for commands you took from a legal list.
- **When a city levels up**, the only legal commands are its two reward options until one is chosen. End Turn isn't legal in between.

## Fog: what a bot should look at

`Game` knows everything, including hidden units, unexplored tiles and the opponent's stars and techs. A bot should decide from what its player knows:

```cpp
#include "view.h"

PlayerView view = makeView(game, game.currentPlayer);
```

A `PlayerView` has:
- explored tiles with their live contents (unexplored ones have `explored == false` and nothing else filled in)
- the units the player can see
- its cities, stars, income, techs, tasks and diplomacy (other tribes' stars and techs are hidden)
- every player's score, since scores are public: your own live, the others' as of the end of their last turn, plus `scoreHistory`, every score published so far

`makeView(game, player, true)` ignores fog, for debugging.

`legalCommands()` itself never leaks hidden information. It only offers what the player could know is possible: for example, a tile with a hidden enemy on it looks free to move onto.

Search is the exception. Applying commands to copies of the full `Game` lets a search see the future with perfect information, so a fair search needs hidden state sampled from the view. See ENGINE.md and the bot design docs when they exist.

## Search: `LegalMoves`

Search asks for the legal commands after each single command. `LegalMoves` (`legalMoves.h`) keeps that list up to date without regenerating it from scratch. It compares the state before and after each command, and recomputes only the commands near what changed.

```cpp
#include "legalMoves.h"

LegalMoves legal(game);              // once, at the root
for (const Command &c : legal.commands()) {
    Game child = game;               // branch both
    LegalMoves childLegal = legal;
    childLegal.apply(child, c);      // applies c to child and updates the list
    // ... evaluate child, using childLegal.commands() ...
}
```

- `commands()` always matches `game.legalCommands()` as a set, though not in the same order. The tests check this after every command on every map type.
- `apply(game, command)` applies the command to `game` and updates the list. `update(before, after)` updates it for any other change, like several commands at once.
- It's two flat command lists, so it's cheap to copy into each search node.
- When it helps: a step of random play costs about 5.7 µs with `LegalMoves`, and about 6.9 µs with `legalCommands()` + `apply()`. End Turn and research rebuild the whole list, since they change everything.

## Encoding for the network

`encoding.h` (in `stocknavalon`) turns states and commands into arrays, following [`docs/encoding.md`](encoding.md):

```cpp
#include "encoding.h"

EncodingConfig config{.mapSize = 11, .players = 2};
std::vector<float> planes(planeCount(config) * 11 * 11), globals(globalCount(config));
encodeState(game, game.currentPlayer, config, planes.data(), globals.data());

std::vector<std::uint8_t> mask(actionCount(config));
legalMask(legal.commands(), game, config, mask.data());   // from a LegalMoves
Command command = decodeCommand(game, index, config);
```

`encodeState` reads only what the player knows, which the tests check. `ENCODING_VERSION` changes whenever the layout's meaning does.

## Bots

`RandomBot` (`bots/randomBot.h`) is the reference for the shape of a bot:

```cpp
RandomBot bot(seed);
Command c = bot.choose(game);        // one command
std::vector<Command> turn = bot.playTurn(game);   // a whole turn, until End Turn
```

Simple bots for testing the engine go in `src/engine/bots/`; any `.cc` file under `src/engine` becomes part of the engine library on the next build. stockNavalon itself lives in `src/stockNavalon/`, the `stocknavalon` library.

## Maps

```cpp
#include "mapgen.h"

MapData map = generateMap({.size = 14, .type = MapTypes::CONTINENTS,
                           .tribes = tribes, .seed = 1});
MapData fromFile = loadMap("src/engine/map.txt");   // format in map.h
```

`MapGenSettings` (in `mapgen.h`) holds every tunable map generation default. `map.capitals[i]` is player `i`'s capital tile.

## Threads

- Separate `Game` objects can be used from separate threads freely. The engine has no shared mutable state: its data tables are constant, and the move search's scratch buffers are per thread.
- A single `Game`, `LegalMoves` or `RandomBot` isn't safe to use from two threads at once.

## API at a glance

The headers are the reference; these are the parts a bot uses.

| Header | What's there |
| --- | --- |
| `game.h` | `Game`: state, geometry (`index`, `distance`, `forEachNeighbour`), queries (`tileOwner`, `unitAt`, `cityAt`, `income`, `score`, `techCost`, `previewAttack`, `moveTargets`, `attackTargets`, `atPeace`, `explored`, `canSee`), commands (`legalCommands`, `isLegal`, `apply`, `applyUnchecked`) |
| `command.h` | `Command`, `CommandTypes`, `toString` |
| `view.h` | `PlayerView`, `makeView` |
| `encoding.h` | `EncodingConfig`, `encodeState`, `encodeCommand`, `decodeCommand`, `legalMask` |
| `legalMoves.h` | `LegalMoves` |
| `mapgen.h`, `map.h` | `generateMap`, `MapGenSettings`, `loadMap`, `parseMap`, `MapData` |
| `tile.h`, `unit.h`, `city.h`, `player.h` | the state structs |
| `data/*.h` | unit, tech, improvement and tribe tables (`unitInfo(type)`, `techData[tech]`, `improvementInfo(type)`, `tribeData[tribe]`) |
| `bots/randomBot.h` | `RandomBot` |
