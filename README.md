# StockNavalon

A C++ engine for Polytopia, for training bots: the rules, a playable CLI, a Python package, and building blocks for search.

## Setup

Needs CMake 3.25+, Ninja and a C++20 compiler. For the Python package, also [uv](https://docs.astral.sh/uv/):

```bash
uv venv .venv
uv pip install --python .venv/bin/python nanobind scikit-build-core pytest
```

The venv is optional for C++ work. When it's there, the CMake build also builds and tests the Python module.

## Build

Two presets, each building into `build/<preset>`:

```bash
cmake --preset release        # optimized
cmake --build --preset release

cmake --preset dev            # debug info, sanitizers, checked containers
cmake --build --preset dev
```

Each builds:
- `final_program`: the CLI
- `run_tests`: the C++ tests
- `random_game` and `one_ply_search`: the examples
- the Python module (into `build/<preset>/python`), when the venv has nanobind

The configure step (`cmake --preset ...`) only needs rerunning when the build setup changes. New source files are picked up on the next build.

## Test

```bash
ctest --preset release        # C++ and Python tests
ctest --preset dev            # C++ tests under the sanitizers (slower)
```

The Python tests run against the module just built, not the pip-installed one. The dev build compiles the Python module but doesn't test it, since a sanitized module can't load into a normal Python. Add `-R cpp` or `-R python` to run one suite.

## Run

```bash
./build/release/final_program [map file | map type] [tribe...] [--seed N] [--size N]
./build/release/final_program src/game/map.txt Imperius Bardur
./build/release/final_program lakes Imperius Bardur --seed 7
```

- **The map:** a map type (`drylands`, `lakes`, `pangea`, `continents`, `archipelago` or `waterworld`) generates a map of that type, 14x14 unless `--size` says otherwise. Otherwise it loads a map file (format in `src/game/map.h`), by default `src/game/map.txt`.
- **Players:** every player defaults to Imperius.
- **`--bot N`** makes seat N a random bot; repeat it for more bots. It plays its whole turn as soon as yours ends. With every seat a bot, the game plays itself, stopping after 300 turns, and shows the final map.
- **The map display:** drawn with emojis. Add `--ascii` for a letter view, if your terminal shows emojis badly.
- **`--reveal`:** the map only shows what the current player has explored; this shows everything, including the bot's commands, which would otherwise give away what's in the fog.

```bash
./build/release/final_program lakes Imperius Bardur --bot 1 --seed 7
```

### CLI commands

| Cmd | Args  | Description                                    |
| --- | ----- | ---------------------------------------------- |
| `l` |       | List the current player's legal commands       |
| `c` | `n`   | Apply legal command number `n` from `l`        |
| `e` |       | End turn                                       |
| `i` | `x y` | Inspect a tile and its unit                    |
| `s` |       | Show the current player's stars, techs, cities, tasks and diplomacy |
| `p` |       | Print the map                                  |
| `k` |       | Show the key for the emoji map                 |
| `q` |       | Quit                                           |

## Python

The engine is also a Python package, `stocknavalon`, built with nanobind and scikit-build-core. Install it into the venv (editable):

```bash
uv pip install --python .venv/bin/python --no-build-isolation -e .
```

Rerun that after changing C++ code. Then:

```python
import stocknavalon as sn

tribes = [sn.Tribe.IMPERIUS, sn.Tribe.BARDUR]
game = sn.Game(sn.generate_map(tribes, seed=1), tribes, seed=1)
game.apply(sn.Command(sn.CommandType.RESEARCH, arg=sn.TechType.RIDING))
view = sn.make_view(game, 0)          # what player 0 knows
results = sn.play_random_games(100, tribes)   # loops in C++
```

A call into the engine costs about as much as a game step (a full step from Python is about 14 µs, against 6 µs in C++), so loops over many games or positions belong in C++ as single calls. The full guide is [`docs/python.md`](docs/python.md).

## Editor setup

`.clangd` points clangd at the dev build's `compile_commands.json`, which CMake writes and keeps up to date. Run `cmake --preset dev` once after cloning; with the venv set up, it covers the Python bindings too.

## Docs

- [`docs/cpp.md`](docs/cpp.md): using the engine as a C++ library (linking, commands, fog, `LegalMoves` for search, threads), with compiled examples in `examples/`
- [`docs/python.md`](docs/python.md): the `stocknavalon` Python package (setup, commands, full API reference, speed)
- [`src/game/GAME.md`](src/game/GAME.md): the rules spec
- [`src/game/ENGINE.md`](src/game/ENGINE.md): how the engine implements it, and the assumptions made
