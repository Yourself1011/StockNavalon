# The oracle: the real game's rules

The oracle runs the real game's own rules, so our engine can be checked against them. It loads the game's compiled rules library from the Steam install, starts its il2cpp runtime without Unity, and calls the game's own code: the same functions the app uses to build a game and generate its map. The plan it's part of (rules checking, map statistics, later matches against the built-in bot) is in [`notes/integration.md`](../notes/integration.md).

- `src/oracle/`: the oracle's code, our own. It's separate from the engine: it never links the engine, and the engine knows nothing about it.
- `game/`: the pinned copy of the game, its extracted data and its decompiled class layout. **Gitignored, never committed**: it's Midjiwan's code and data.
- macOS on Apple Silicon only, where the Steam build's library runs natively.

## Setup

Needs the Steam install of The Battle of Polytopia and [uv](https://docs.astral.sh/uv/). Then:

```bash
src/oracle/setup.sh            # or: src/oracle/setup.sh /path/to/Polytopia.app
cmake --build --preset release --target oracle
./build/release/oracle game/current
```

`setup.sh` (about 20 seconds) pins a copy of the build, so a Steam update can't change the rules under a running check:

| Path | What |
| --- | --- |
| `game/<version>-<build>/GameAssembly-arm64.dylib` | the rules library, the arm64 half of the app's universal `GameAssembly.dylib` |
| `game/<version>-<build>/Metadata/global-metadata.dat` | the runtime's class and method metadata |
| `game/<version>-<build>/gamedata/` | the rules data, `GameLogicData1`–`28` and `AvatarData`, extracted from `data.unity3d` with UnityPy (`src/oracle/extract_data.py`) |
| `game/<version>-<build>/cpp2il/` | the class layout as C# stubs (fields, offsets, method signatures, no bodies), from [Cpp2IL](https://github.com/SamboyCoding/Cpp2IL) |
| `game/<version>-<build>/isil/` | method bodies in Cpp2IL's intermediate language, for reading what a method calls |
| `game/<version>-<build>/Info.plist` | the app's version |
| `game/current` | a link to the pinned build in use |
| `game/tools/` | Cpp2IL (ad hoc signed, since macOS kills unsigned arm64 binaries), and a venv with UnityPy |

Pinned now: **2.18.1, build 16768** (Unity 6000.3.17): game version 124, rules data version 28. After a Steam update, run `setup.sh` again: it pins the new build next to the old one and moves `game/current` to it.

## Use

One request a line on stdin, one JSON answer a line on stdout. The first line out is `{"version": <game version>}`; an error is `{"error": "..."}` and the oracle carries on.

| Request | Answer |
| --- | --- |
| `new <map> <size> <mode> <difficulty> <tribe> <tribe>...` | `{"game": id}`: a single-player game, the own player with the first tribe and the game's AI with the others |
| `state <id>` | the whole `GameState` |
| `map <id>` | its `MapData`: size and tiles |
| `mapsettings <id>` | the `MapGeneratorSettings` its map was made with (wetness, richness, city densities, ...) |
| `free <id>` | `{}`, and the game is gone |

```bash
printf 'new lakes 14 might crazy imperius bardur\nmap 0\n' | ./build/release/oracle game/current
```

- **Names** are the game's own enum names, in any case: maps `dryland lakes continents archipelago waterworld pangea`, modes `might domination perfection`, difficulties `easy normal hard crazy`, tribes `aimo aquarion bardur elyrion hoodrick imperius kickoo luxidoor oumaji quetzali vengir xinxi yadakk zebasi`. The oracle looks them up in the game's enums; nothing is restated.
- **The JSON** is any game object by reflection (`dump.cc`): `{"$type": class, field: value, ...}`, with enums by name, lists, sets and arrays as arrays, and dictionaries as `[key, value]` pairs. An object met a second time is `{"$ref": class}`; the rules data and delegates are left out.
- **Speed:** about 10 ms per game, map included.
- **Maps aren't reproducible from a seed:** the game generates several candidates with seeds from a cryptographic random generator and keeps the fairest (`MapGenerator.Generate`, `MapFairnessCalculations.GetInequality`). A map to keep is kept as its JSON.

## Comparing maps

`src/oracle/mapstats.py` compares the real generator with ours: maps from both for the same random tribe pairs, statistics of each map (terrain shares, resource and village counts, water bodies and landmasses, what's around each capital, climate regions), and per statistic both means, their gap in standard errors and a Kolmogorov–Smirnov test of the whole distribution.

```bash
cmake --build --preset release
.venv/bin/python src/oracle/mapstats.py --type lakes --size 14 --maps 1000
```

Ours is the build tree's module (`build/release/python`, or `POLYENGINE_BUILD_PACKAGE`), not the installed one, so it measures the engine as built. `--save game/maps.jsonl` keeps the real maps (in `game/`, since they come from the game). About 15 seconds for 1,000 maps of each. Results so far are in [`notes/integration.md`](../notes/integration.md) section 5.

## How it works

- **The runtime** (`il2cpp.h`): `dlopen` the library, resolve the exported `il2cpp_*` API, `il2cpp_set_data_dir` to the pinned directory, `il2cpp_init`. All calls go through that API (classes, fields and methods by name, `runtime_invoke`), never fixed offsets, so a new build needs no code changes unless the game's own names change. Objects the oracle keeps between requests are held by GC handles.
- **The rules data** (`game.cc`, `start`): the app's data provider reads Unity assets, which don't exist here. Instead the oracle parses the extracted files with the game's own `GameLogicData.Parse` and `AvatarData.Parse` and puts them in `PolytopiaDataManager`'s caches, so the game finds them there and never asks for a provider.
- **Building a game** (`createLocalGame`): what the app's New Game menu does for single player (`ClientBase.CreateSession`, read from its method body): `GameSettings` with `ApplyGameTypeDefaults` (single player, the mode), the map type, size, opponent count and difficulty; the own player as a `PlayerState` with id 1 and its tribe; the AI players from `GameStateUtils.CreateAIOpponent`, with the difficulty's handicap; then `GameStateUtils.CreateLocalGame`, which adds nature, generates the map and places the starting units.
- **Finding things in the game's code:** the class layout in `game/current/cpp2il/DiffableCs/<assembly>/` (the rules are in `GameLogicAssembly`, the app in `PolytopiaAssembly`, the enums in `PolytopiaBackendBase`), and what a method calls in `game/current/isil/IsilDump/<assembly>/<class>.txt` (`grep -E "Method:|Call [A-Za-z]"`).
