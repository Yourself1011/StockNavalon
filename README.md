# StockNavalon

## Build

```bash
make        # release build -> build/release/final_program
make dev    # dev build with sanitizers -> build/dev/final_program
```

## Run

```bash
./build/release/final_program [map file | map type] [tribe...] [--seed N] [--size N]
./build/release/final_program src/game/map.txt Imperius Bardur
./build/release/final_program lakes Imperius Bardur --seed 7
```

A map type (`drylands`, `lakes`, `pangea`, `continents`, `archipelago` or `waterworld`) generates a map of that type (14x14 unless `--size` says otherwise) instead of loading a file.

`--bot N` makes seat N a random bot (repeat it for more bots). It plays its whole turn as soon as yours ends. Its commands are only listed with `--reveal`, since they'd give away what's in the fog. With every seat a bot, the game plays itself (stopping after 300 turns) and shows the final map.

```bash
./build/release/final_program lakes Imperius Bardur --bot 1 --seed 7
```

The map defaults to `src/game/map.txt`, and every player defaults to Imperius. The map is drawn with emojis; add `--ascii` for the old letter view if your terminal shows them badly. The map only shows what the current player has explored; add `--reveal` to see everything. The map file format is described in `src/game/map.h`.

## Python

The engine is also a Python package, `stocknavalon`, built with nanobind and scikit-build-core. Set it up in a project venv (needs [uv](https://docs.astral.sh/uv/)):

```bash
uv venv .venv
uv pip install --python .venv/bin/python nanobind scikit-build-core pytest
uv pip install --python .venv/bin/python --no-build-isolation -e .
.venv/bin/python -m pytest tests/python
```

Rerun the last install after changing C++ code. Then:

```python
import stocknavalon as sn

tribes = [sn.Tribe.IMPERIUS, sn.Tribe.BARDUR]
game = sn.Game(sn.generate_map(tribes, seed=1), tribes, seed=1)
game.apply(sn.Command(sn.CommandType.RESEARCH, arg=sn.TechType.RIDING))
view = sn.make_view(game, 0)          # what player 0 knows
results = sn.play_random_games(100, tribes)   # loops in C++
```

A call into the engine costs about as much as a game step (a full step from Python is about 14 µs, against 6 µs in C++), so loops over many games or positions belong in C++ as single calls.

The engine also builds with plain CMake, which makes the CLI and the tests like the Makefile does:

```bash
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
```

## Editor setup

clangd (and other tools) read `compile_commands.json`, which isn't checked in. Generate it after cloning, and again whenever you add a source file. With the Python venv set up, it covers the bindings too:

```bash
make compile_commands.json
```

## Test

```bash
make test        # release build
make test MODE=dev   # with sanitizers
```

## CLI Commands

| Cmd | Args  | Description                                   |
| --- | ----- | --------------------------------------------- |
| `l` |       | List the current player's legal commands      |
| `c` | `n`   | Apply legal command number `n` from `l`       |
| `e` |       | End turn                                      |
| `i` | `x y` | Inspect a tile and its unit                   |
| `s` |       | Show the current player's stars, techs, cities |
| `p` |       | Print the map                                 |
| `k` |       | Show the key for the emoji map                |
| `q` |       | Quit                                          |

## Docs

- `src/game/GAME.md`: the rules spec
- `src/game/ENGINE.md`: what's implemented, what isn't yet, and the assumptions made
