# stockNavalon

stockNavalon is a Polytopia bot: AlphaZero-style search with a neural network, playing through fog by sampling the hidden state ([`docs/stockNavalon.md`](docs/stockNavalon.md)). This repository also holds **polyengine**, the C++ game engine it's built on: the rules, a playable CLI, a Python package (`polyengine`), and building blocks for search.

- `src/engine/`: polyengine, the game. It doesn't depend on any bot.
- `src/stockNavalon/`: the bot's C++ code, built on the engine: encoding, tribe inference, determinization, the search, game records and self-play.
- `python/stocknavalon/`: the network and training loop (PyTorch).
- `notes/`: working notes for development: the to-do list, designs not built yet, and what training runs showed.
- `tests/engine/`, `tests/stockNavalon/`: their tests, in one doctest binary; `tests/python/`: the Python package's.

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
- `final_program`: the CLI, against the random bot
- `play_stocknavalon`: the same CLI, against stockNavalon
- `run_tests`: the C++ tests
- `random_game`, `one_ply_search` and `search_vs_random`: the examples
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
./build/release/final_program src/engine/map.txt Imperius Bardur
./build/release/final_program lakes Imperius Bardur --seed 7
```

- **The map:** a map type (`drylands`, `lakes`, `pangea`, `continents`, `archipelago` or `waterworld`) generates a map of that type, 14x14 unless `--size` says otherwise. Otherwise it loads a map file (format in `src/engine/map.h`), by default `src/engine/map.txt`.
- **Players:** every player defaults to Imperius.
- **`--bot N`** makes seat N a random bot; repeat it for more bots. It plays its whole turn as soon as yours ends. With every seat a bot, the game plays itself, stopping after 300 turns, and shows the final map.
- **`--watch`:** in a game of bots only, shows the board after every turn and waits for Enter (`q` to stop). **`--delay S`** does the same without waiting, S seconds a turn.
- **`--no-treaties`:** no peace treaties (training games are played without them).
- **The map display:** drawn with emojis. Add `--ascii` for a letter view, if your terminal shows emojis badly.
- **`--reveal`:** the map only shows what the current player has explored; this shows everything, including the bot's commands, which would otherwise give away what's in the fog.

```bash
./build/release/final_program lakes Imperius Bardur --bot 1 --seed 7
```

### Playing stockNavalon

`play_stocknavalon` is the same CLI, with stockNavalon in the `--bot` seats instead of the random bot:

```bash
./build/release/play_stocknavalon lakes Imperius Bardur --size 11 --bot 1
```

- **Fair play:** it searches on determinized games, so it only knows what its own player could.
- **Its options:** `--simulations N` per determinized game (default 200), `--samples N` determinized games per command (default 4), and `--horizon N`, the turn changes its search looks across (default 1, so it considers your reply).
- **How it plays:** `play_stocknavalon` judges positions by the score lead, so expect it to build up score rather than attack. A turn takes it about a third of a second.
- **Against a trained network:** `stocknavalon.play` runs the same CLI with the network's search, from a run directory (its best network, `best.pt`) or a `.pt` snapshot, with the same options plus `--device`:

  ```bash
  .venv/bin/python -m stocknavalon.play runs/second lakes Imperius Bardur --size 11 --bot 1
  ```

  It plays without peace treaties, as in training, unless given `--peace-treaties`, and guesses what's hidden from the network's beliefs (`--no-network-beliefs` for the fixed priors). Without `--seed` it picks a random one and prints it, so a game can be replayed. To watch it play itself, make both seats bots: `--bot 0 --bot 1 --watch`.

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

The engine is also a Python package, `polyengine`, built with nanobind and scikit-build-core. Install it into the venv (editable):

```bash
uv pip install --python .venv/bin/python --no-build-isolation -e .
```

Rerun that after changing C++ code. Then:

```python
import polyengine as pe

tribes = [pe.Tribe.IMPERIUS, pe.Tribe.BARDUR]
game = pe.Game(pe.generate_map(tribes, seed=1), tribes, seed=1)
game.apply(pe.Command(pe.CommandType.RESEARCH, arg=pe.TechType.RIDING))
view = pe.make_view(game, 0)          # what player 0 knows
results = pe.play_random_games(100, tribes)   # loops in C++
```

A call into the engine costs about as much as a game step (a full step from Python is about 14 µs, against 6 µs in C++), so loops over many games or positions belong in C++ as single calls. The full guide is [`docs/python.md`](docs/python.md).

## Training stockNavalon

The network and training loop are in `python/stocknavalon` (PyTorch). Install them into the venv, then measure the machine and start a run:

```bash
uv pip install --python .venv/bin/python --no-build-isolation -e ".[train]"
.venv/bin/python -m stocknavalon.measure          # network and self-play speed
.venv/bin/python -m stocknavalon.rating runs/NAME # Elo of each best network so far
.venv/bin/python -m stocknavalon.searches runs/NAME  # Gumbel search against PUCT, same network
.venv/bin/python -m stocknavalon.run --dir runs/NAME
```

A run bootstraps from score-evaluator games, then runs self-play and training at once, in two threads, with training held to `--reuse` samples per new position. It saves its games, checkpoint and log in the run directory; Ctrl-C stops it cleanly, and running it again resumes. Every 5,000 training steps (about an hour) it also plays evaluation matches (against the random bot, the score-evaluator search and its previous self) and logs the win rates; each status line includes the loss on held-out games. It uses Apple Silicon's GPU (MPS) when one is available. See `docs/stockNavalon.md` section 6.

## Editor setup

`.clangd` points clangd at the dev build's `compile_commands.json`, which CMake writes and keeps up to date. Run `cmake --preset dev` once after cloning; with the venv set up, it covers the Python bindings too.

## Docs

- [`docs/stockNavalon.md`](docs/stockNavalon.md): how stockNavalon, the bot, works (network, search, determinization, self-play, training)
- [`docs/tribeInference.md`](docs/tribeInference.md): the spec for working out an unmet opponent's possible tribes exactly from their opening score changes
- [`docs/encoding.md`](docs/encoding.md): the spec for encoding states and commands as arrays for the network
- [`docs/cpp.md`](docs/cpp.md): using the engine as a C++ library (linking, commands, fog, `LegalMoves` for search, threads), with compiled examples in `examples/`
- [`docs/python.md`](docs/python.md): the `polyengine` Python package (setup, commands, full API reference, speed)
- [`src/engine/GAME.md`](src/engine/GAME.md): the rules spec
- [`src/engine/ENGINE.md`](src/engine/ENGINE.md): how the engine implements it, and the assumptions made

Working notes, for development rather than reference: [`notes/todo.md`](notes/todo.md) (the checklist), [`notes/design.md`](notes/design.md) (designs not built yet, and why), [`notes/runs.md`](notes/runs.md) (what training runs showed).
