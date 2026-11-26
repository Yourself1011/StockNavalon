# The oracle: the real game's rules

The oracle runs the real game's own rules, so our engine can be checked against them. It loads the game's compiled rules library from the Steam install, starts its il2cpp runtime without Unity, and calls the game's own code: the same functions the app uses to build a game and generate its map. Its uses (rules checking, map statistics, matches against the built-in bot) and their findings are in [`notes/integration.md`](../notes/integration.md).

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
| `fairness <id>` | `{"inequality": x}`: the map generator's own fairness score for the game (`MapFairnessCalculations.GetInequality`), the best player's territory score over the worst's |
| `turfs <id>` | the territories that score comes from (`CreateTurfs`): per player, every tile it claims with its cost from the capital |
| `start <id> [keep]` | `{}`: every seat made a person (no AI plays by itself, the human handicap; with `keep` the AI seats keep their difficulty's handicap) and the match started with a `StartMatchCommand`, as the app does |
| `ai <id>` | `{"command": ..., "index": k}`: the command the game's AI would play next for the current player, as the game plays a bot's turn (a pending reward's or peace offer's answer, `CommandTriggerUtils.TryGetTriggerCommand`, else `AI.GetMove` at the game's difficulty), not yet played; `index` is its place in the last `legal` list, or −1 (a `StayCommand`, which the client never offers) |
| `legal <id>` | the current player's legal commands as a list of command objects (`MoveCommand`, `BuildCommand`, ...), numbered by position |
| `act <id> <ActionClass> <args>` | `{}`: one of the game's own actions (`BuildAction`, `TrainAction`, `IncreasePopulationAction`, ...) made by its constructor with that many arguments (enum values by name, coordinates as `x,y`, numbers, 1 or 0) and run to the end; for setting up scenarios |
| `set <id> <what> ... <field> <value>` | `{}`: an integer, enum or bool field set directly: `game`, `player <playerId>`, `tile <x> <y>`, `unit <x> <y>`, `improvement <x> <y>` |
| `apply <id> <k>` | `{}`, or `{"rejected": error}`: the k-th command of the last `legal` list (or with `ai`, the AI's last command), executed by the game's `ActionManager`, which is then run (`ForceUpdate`) until the actions the command queued are all done |
| `free <id>` | `{}`, and the game is gone |

```bash
printf 'new lakes 14 might crazy imperius bardur\nmap 0\n' | ./build/release/oracle game/current
```

- **Names** are the game's own enum names, in any case: maps `dryland lakes continents archipelago waterworld pangea`, modes `might domination perfection`, difficulties `easy normal hard crazy`, tribes `aimo aquarion bardur elyrion hoodrick imperius kickoo luxidoor oumaji quetzali vengir xinxi yadakk zebasi`. The oracle looks them up in the game's enums; nothing is restated.
- **The JSON** is any game object by reflection (`dump.cc`): `{"$type": class, field: value, ...}`, with enums by name, lists, sets and arrays as arrays, and dictionaries as `[key, value]` pairs. An object met a second time is `{"$ref": class}`, except a list or array of values, which is written in full each time (a boat and its passenger share their effects); the rules data, delegates and the AI's memory (`AIState`, `OpinionManager`) are left out.
- **Speed:** about 10 ms per game, map included.
- **Maps aren't reproducible from a seed:** the game generates several candidates with seeds from a cryptographic random generator and keeps the fairest (`MapGenerator.Generate`, `MapFairnessCalculations.GetInequality`). A map to keep is kept as its JSON.

## Comparing maps

`src/oracle/mapstats.py` compares the real generator with ours: maps from both for the same random tribe pairs, statistics of each map (terrain shares, resource and village counts, water bodies and landmasses, what's around each capital, climate regions), and per statistic both means, their gap in standard errors and a Kolmogorov–Smirnov test of the whole distribution.

```bash
cmake --build --preset release
.venv/bin/python src/oracle/mapstats.py --type lakes --size 14 --maps 1000
```

`--by-tribe` instead compares rates inside each tribe's climate region, since every tribe has its own terrain and resource modifiers: terrain shares of the land, each resource's rate per terrain next to a settlement and two away, and the starting resources next to the capital, with a z-score each (3,000 maps take about 40 seconds).

Ours is the build tree's module (`build/release/python`, or `POLYENGINE_BUILD_PACKAGE`), not the installed one, so it measures the engine as built. `--size N --players P` for other sizes and player counts. The real generator itself fails on some large maps with several players (an exception, or the oracle dies); those games are left out and the count printed, the oracle running in batches of 25 so one crash loses only its batch. `--save game/maps.jsonl` keeps the real maps (in `game/`, since they come from the game). About 15 seconds for 1,000 maps of each. Results so far are in [`notes/integration.md`](../notes/integration.md) section 5.

## Playing a game

`start`, then `legal` and `apply` in turn. The legal list is built the way the client offers commands (`legalCommands` in `game.cc`), each kept only if its own `IsValid` accepts it:

- per tile the player has explored (the client acts on no others; the commands' own checks would allow, say, a road in the fog), `CommandUtils`: unit actions (capture, recover, heal, promote, upgrade, disband, examine ruins, ...), buildable improvements, improvement abilities, trainable units;
- per unit of the player, `UnitDataExtensions`: a `MoveCommand` per movement option and an `AttackCommand` per attack option;
- a `ResearchCommand` per tech of the player's tribe;
- `CommandUtils.GetDiplomacyCommandsForOpponent` per other player;
- a pending trigger's answers: a `CityRewardCommand` per reward when a city levelled up, a `PeaceRequestResponseCommand` (accept or decline) for a peace offer; while one is pending, nothing else is valid;
- an `EndTurnCommand`.

Nature (player 255) has a turn of its own after every player's, where its only command is ending it; income and other turn-start effects come after it.

### Mirroring a game in polyengine

```sh
.venv/bin/python src/oracle/mirror.py --type lakes --size 11 --games 10 --history 5 --save /tmp/diffs
```

`mirror.py` plays the same game on both engines: polyengine starts from the real map, each step compares the real legal list (translated) with ours, plays a random one of them on both (ending nature's turns on the real side only), and compares the states: tiles, units (with their facing, whether they're hidden and their home city), and each player's stars, score, techs, explored tiles, the tribes it has met, its Pacifist count, its peace and cooldowns and the embassies in its capital. Each game stops at its first difference, printed with the commands before it (`--history`); `--save` keeps the real state and legal list there, and our side before and after (the real map isn't seeded, so a game can't be replayed). A game ends once ours is over and the real one is in `FinalTurn` or `Ended`, or waits only for the reward of a city the last capture levelled up (`Lockstep.over`). Names are translated where the two differ (`UNITS`, `IMPROVEMENTS`, `TECHS`).

Random outcomes are read off the real game after it applies the command and forced on ours: a ruin's reward (`Game.force_ruin`; told by a new tech, a new unit's id or a pending `TrainAction`, newly explored tiles, the capital's growth, else stars), an Explorer's tiles (`force_explorer`), a converted unit's home (`force_converted_home`) and an infiltration's Daggers (`force_daggers`). Differences only of timing are left out: the real game scores a city's new level, and counts the population past it, once its reward is picked; while a reward is pending it holds back the rest of the command, so explored tiles, scores and city populations wait until it's picked, ours gets the rest of an Explorer's walk afterwards (`force_reveal`), and the actions held back on the real `ActionStack` are allowed for (a unit on a pending `TrainAction`'s or `EmbarkAction`'s tile is left out on both sides, a pending `IncreaseCurrencyAction`'s stars counted as paid); it starts a turn (income, temple levels) once a peace offer waiting at the turn's start is answered; it marks Pacifist done a little after its count reaches 5. Findings are in [`notes/integration.md`](../notes/integration.md) section 4.

Random play runs about 300–500 commands a second (a `legal` and an `apply` each). Garbage collection is paused during each request, since objects a request builds are held in the oracle's own memory, which the collector doesn't scan; between requests everything kept is held by GC handles.

### Playing against the game's AI

```sh
cd python && ../.venv/bin/python -m stocknavalon.crazy ../runs/ninth --games 20 --size 11
```

`stocknavalon.crazy` plays a network against the game's own Crazy bot: `mirror.play` with `ai_seats` (the seats `ai` plays, with Crazy's handicap moved onto them, so the network can take either seat) and `pick` (the network's move, `pe.search` on our mirrored game, matched to the real legal list; the search's moves are its samples', so if its choice is illegal in the real game, the most visited legal one). Each step is compared as in a mirrored game, so these games check the rules too; a game stops at its first difference, which is reported and counts for neither side. A game still going at `--max-turns` (50) goes to the side ahead on score, undecided. `--type any` (the default) plays the map types in turn by pair of games, both seats on each, and reports the results by type. `--parallel` games (12) play at once, each with its own oracle, their positions evaluated together (`crazy.Batcher`). Every request to an oracle has a timeout (`mirror.ORACLE_TIMEOUT`, 2 minutes), since the game's code has none: past it the oracle is killed (`OracleTimeout`) and the game counts as failed, neither a result nor a rules difference; an exception in our own code is kept with its traceback, and the other games go on. The AI plays whole turns in about a second; a game of 30–40 turns takes under a minute at 50 simulations. Training runs can play these as an anchor (`--crazy-games`, docs/stockNavalon.md), and `stocknavalon.play --crazy SEAT` puts Crazy in a seat of the terminal game, against a person or the network (`crazy.CrazySeat`; the first rules difference is shown and the game goes on unchecked). Both go through `mirror.Lockstep`, the real game and ours kept in step (what `mirror.play` runs on too): the terminal game takes a starting game, a hook before every command (the real game plays it first, and its random outcome is fixed on ours) and seats played from outside (`run_cli`'s `start`, `before_command`, `outside_seats`, `outside_turn`).

### Scenarios

```sh
.venv/bin/python src/oracle/scenarios.py all --games 15 --steps 300
```

`scenarios.py` runs mirrored games set up to exercise one rule: `starvation` (cities on negative population), `embassies` (embassies each way, peace, attacks), `pushes` (a super unit on an occupied capital with ports beside it), `fights` (units of both sides side by side), `stomps` (a Giant beside its own port, among units of both sides), `boats` (ports, sea techs, stars), `ports` (ports joining several cities, level-ups in between), `connections` (roads, bridges, ports, captures), `kills` (enemies on 1 HP beside a melee boat and land units: moving in, landing, Persist), `portkills` (enemies on 1 HP on a port of each side, a Warrior beside each), `cloaks` (a Cloak beside its own port: hiding, embarking, landing), `infiltrations` (Cloaks beside the enemy capital, empty or defended) and `upgrades` (a Warrior beside its own port, an enemy in reach: embarking, upgrading at once and attacking). Run them one scenario at a time with their own `--save` folder, since saved states are named by seed. After a first round, each changes the start the same way on both engines, the real one through its own actions and fields (`act`, `set`, which also make it the edited player's turn, since the game only runs the current player's actions), ours through `Game`'s set-up calls (`set_stars`, `give_tech`, `spawn_unit`, `place_building`, `add_population`, `add_embassy`, `make_peace`, ...); then it brings the edits' side effects into line (`Both.settle`: stars, meetings, exploration, score) and plays with a chooser that favours the rule's commands, comparing as `mirror.py` does.

## How it works

- **The runtime** (`il2cpp.h`): `dlopen` the library, resolve the exported `il2cpp_*` API, `il2cpp_set_data_dir` to the pinned directory, `il2cpp_init`. All calls go through that API (classes, fields and methods by name, `runtime_invoke`), never fixed offsets, so a new build needs no code changes unless the game's own names change. Objects the oracle keeps between requests are held by GC handles.
- **The rules data** (`game.cc`, `start`): the app's data provider reads Unity assets, which don't exist here. Instead the oracle parses the extracted files with the game's own `GameLogicData.Parse` and `AvatarData.Parse` and puts them in `PolytopiaDataManager`'s caches, so the game finds them there and never asks for a provider.
- **Building a game** (`createLocalGame`): what the app's New Game menu does for single player (`ClientBase.CreateSession`, read from its method body): `GameSettings` with `ApplyGameTypeDefaults` (single player, the mode), the map type, size, opponent count and difficulty; the own player as a `PlayerState` with id 1 and its tribe; the AI players from `GameStateUtils.CreateAIOpponent`, with the difficulty's handicap; then `GameStateUtils.CreateLocalGame`, which adds nature, generates the map and places the starting units.
- **Finding things in the game's code:** the class layout in `game/current/cpp2il/DiffableCs/<assembly>/` (the rules are in `GameLogicAssembly`, the app in `PolytopiaAssembly`, the enums in `PolytopiaBackendBase`), and what a method calls in `game/current/isil/IsilDump/<assembly>/<class>.txt` (`grep -E "Method:|Call [A-Za-z]"`).
