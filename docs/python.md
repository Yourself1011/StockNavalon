# Using polyengine from Python

`polyengine` is the engine as a Python package. It's a thin nanobind wrapper over the C++ library, so it plays exactly the same game. The rules are in [`src/engine/GAME.md`](../src/engine/GAME.md), and the engine's own choices where the rules are silent are in [`src/engine/ENGINE.md`](../src/engine/ENGINE.md). [`docs/cpp.md`](cpp.md) covers the same engine from C++.

Use Python for experiments, evaluation, training and notebooks. Use C++ for anything that runs per move at scale (see [Speed](#speed)).

## Setup

With [uv](https://docs.astral.sh/uv/), in a project venv:

```bash
uv venv .venv
uv pip install --python .venv/bin/python nanobind scikit-build-core pytest
uv pip install --python .venv/bin/python --no-build-isolation -e .
```

The install is editable for the Python files, but the C++ part is compiled. After changing any C++ code, rerun the last `uv pip install` line.

To run the tests, use the CMake build. It compiles the module itself and tests that build, so the tests always see the current C++ code, whatever pip installed:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release -R python
```

`.venv/bin/python -m pytest tests/python` also works, but it tests the pip-installed module.

Type stubs (`python/polyengine/_engine.pyi`) give editors signatures and docstrings. Regenerate them after changing the bindings. They're generated from the installed module, so reinstall first:

```bash
uv pip install --python .venv/bin/python --no-build-isolation -e .
.venv/bin/python -m nanobind.stubgen -m polyengine._engine -o python/polyengine/_engine.pyi
```

## A first game

```python
import polyengine as pe

tribes = [pe.Tribe.IMPERIUS, pe.Tribe.BARDUR]
game = pe.Game(pe.generate_map(tribes, seed=7), tribes, seed=7)

bots = [pe.RandomBot(1), pe.RandomBot(2)]
while not game.game_over and game.turn < 50:
    bots[game.current_player].play_turn(game)

print(game.turn, [game.score(p) for p in range(2)], game.winner)
```

## Things to know

- **Tiles are indices**, `y * size + x`. Use `game.index(x, y)`, `game.tile_x(t)`, `game.tile_y(t)`. `game.distance(a, b)` counts diagonals as one step.
- **HP is ×10**, so a full-health Warrior has `hp == 100`.
- **`Game` sees everything.** A bot should decide from `pe.make_view(game, player)`, which only has what that player knows, and use the `Game` only to list and apply commands.
- **Copies are independent**: `game.copy()`, `copy.copy(game)` and `copy.deepcopy(game)` all work. A `LegalMoves` copies the same way.
- **Read-only data comes back as copies.** `game.tiles`, `game.units`, `make_view(...)` and the like are snapshots. They don't change as the game goes on, and changing them doesn't change the game.
- **Randomness is seeded:** `Game(..., seed=)` for ruins and the Explorer, `generate_map(..., seed=)` for maps. The same seeds and commands give the same game.

## Commands

A `Command` has `type`, `from_tile`, `to_tile` and `arg`; unused fields are -1. Commands compare equal by value and can go in sets and dicts.

```python
pe.Command(pe.CommandType.RESEARCH, arg=pe.TechType.RIDING)
pe.Command(pe.CommandType.MOVE, from_tile=game.index(3, 3), to_tile=game.index(4, 4))
pe.Command(pe.CommandType.ENDTURN)
```

| Type | `from_tile` | `to_tile` | `arg` |
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

- **`game.legal_commands()`** lists every legal command for the current player except Resign, which is always legal.
- **`game.apply(c)`** raises `ValueError` if `c` isn't legal. **`game.apply_unchecked(c)`** skips the check, for commands taken from a legal list.
- **`game.describe(c)`** describes a command in words: `"move (3,3) -> (4,4)"`.
- **When a city levels up**, its two reward options are the only legal commands until one is chosen.

## Reference

### Enums

All enums convert to `int`, and can be passed wherever an `int` is expected (e.g. `arg=`).

| Enum | Values |
| --- | --- |
| `Tribe` | `XINXI`, `IMPERIUS`, `BARDUR`, `OUMAJI`, `KICKOO`, `HOODRICK`, `LUXIDOOR`, `VENGIR`, `ZEBASI`, `AIMO`, `QUETZALI`, `YADAKK` |
| `MapType` | `DRYLANDS`, `LAKES`, `PANGEA`, `CONTINENTS`, `ARCHIPELAGO`, `WATERWORLD` |
| `CommandType` | the command types above |
| `TerrainType` | `FIELD`, `FOREST`, `MOUNTAIN`, `WATER` (shallow), `OCEAN` |
| `ResourceType` | `NONE`, `FRUIT`, `CROP`, `GAME`, `METAL`, `FISH`, `STARFISH` |
| `ImprovementType` | map features (`CITY`, `VILLAGE`, `RUIN`, `LIGHTHOUSE`), buildings, monuments and actions (`HARVESTFRUIT`, `ROAD`, ...) |
| `UnitType` | land units, then boats (`RAFT`, `SCOUT`, `RAMMER`, `BOMBER`, `JUGGERNAUT`, `DINGHY`, `PIRATE`) |
| `TechType` | `BASIC` (everyone owns it) and the 25 techs |
| `CityReward` | `WORKSHOP`, `EXPLORER`, `CITYWALL`, `RESOURCES`, `POPULATIONGROWTH`, `BORDERGROWTH`, `PARK`, `SUPERUNIT` |
| `Task` | `PACIFIST`, `WEALTH`, `EXPLORER`, `KILLER`, `NETWORK`, `METROPOLIS`, `GENIUS` |
| `WinCondition` | `MIGHT`, `DOMINATION` |

### Maps

| | |
| --- | --- |
| `generate_map(tribes, type=LAKES, size=14, seed=0, candidates=10)` | a generated map for the tribes, in seat order. `candidates` maps are made and the fairest kept. |
| `load_map(path)`, `parse_map(text)` | a map from the text format (see `src/engine/map.h`) |
| `MapData` | `size`, `type`, `tiles`, `capitals` (each player's capital tile), `regions` (each tile's climate region, by seat) |
| `ruin_count_for_size(size)` | ruins a map of that size gets |

### `Game`

| | |
| --- | --- |
| `Game(map, tribes, seed=0, win_condition=MIGHT)` | a new game; `tribes` in the same seat order as the map |
| `copy()` | an independent copy |
| `size`, `map_type`, `turn`, `current_player`, `winner` (-1 until there is one), `game_over` | |
| `tiles`, `units` (alive only), `cities`, `players` | full state, fog ignored |
| `index(x, y)`, `tile_x(t)`, `tile_y(t)`, `distance(a, b)`, `tile_owner(t)` | geometry |
| `score(p)` (live), `published_score(p)` (what others see), `score_history`, `income(p)`, `tech_cost(p, tech)` | |
| `legal_commands()`, `is_legal(c)`, `apply(c)`, `apply_unchecked(c)`, `describe(c)` | commands |

The state classes are read-only:

- **`Tile`:** `terrain`, `resource`, `building`, `level`, `road`, `unit` (an index into the internal unit list, or -1), `territory`, `climate` (a tribe as an int, -1 if none), `explored_by(p)`.
- **`Unit`:** `type`, `passenger` (what a boat carries), `owner`, `tile`, `hp`, `max_hp`, `home`, `kills`, `veteran`, `alive`, `hidden`, `can_move`, `can_attack`, `can_act`.
- **`City`:** `tile`, `owner`, `level`, `population`, `capital_of`, `connected_for`, `pending_rewards`, `parks`, `workshop`, `wall`.
- **`Player`:** `tribe`, `stars`, `capital`, `alive`, `kills`, `lighthouses_found`, `techs`, `tasks_done`, `monuments_built` (lists), `has_met(p)`.

### `make_view(game, player, everything=False)` → `PlayerView`

What the player knows. `everything=True` ignores fog.

- **Fields:** `player`, `size`, `turn`, `current_player`, `winner`, `stars`, `income`, `techs`.
- **Score effects:** `effects_since_published[p]` and `effects_in_last_change[p]`: what this player's own actions visibly did to player `p`'s score (units seen dying, cities captured), since `p` last published, and within the change `p` last published. These are the player's own; other tribes' stars and techs are hidden.
- **Per player:** `tribes` (your own and those you've met; `None` until then), `alive`, `met`, and `scores`. Every player's score is public, met or not, but you see others' scores as of the end of their last turn; your own is live.
- **`score_history`:** every score published so far, oldest first, as `ScoreRecord`s (`player`, `turn`, `score`): the starting scores, then one each time a player ends a turn. Changes between a player's records are evidence of what they did, like a +300 for a tier-3 tech.
- **Own tasks:** `tasks_done`, `monuments_built`, `kills`, `peaceful_turns`, `lighthouses_found`.
- **Own history:** `effects_in_changes` (per `score_history` record, what this player visibly did to that change), `attacked_on_turns` (the turns their units or cities were attacked), `converted_away` (their units taken by conversion), `tiles[t].own_road` (roads they built), `first_occupied` (per tile, the first turn one of their units or cities stood there, -1 for never). Tribe inference reads these.
- **`relations[p]`:** `peace`, `they_asked`, `we_asked`, `our_embassy`, `their_embassy`.
- **`tiles`** (one per map tile): `explored`, then for explored tiles only `terrain`, `resource` (`NONE` until the player can see that kind), `building`, `level` (Windmill, Sawmill or Forge level, or a temple's level, which everyone can see), `turns_to_level_up` (temples: turns until the next level), `climate` (the tribe whose climate region it is), `road`, `owner`, and `unit`/`city`, which are indices into the view's own `units`/`cities`, or -1.
- **`units`:** `type`, `passenger`, `owner`, `tile`, `hp`, `max_hp`, `kills`, `veteran`, `hidden`. For the player's own units only, also `can_move`, `can_attack`, `can_act`, `hidden_enemy_near`.
- **`cities`:** `tile`, `owner`, `level`, `population`, `capital_of`, `wall`, `parks`.

### Encoding for the network

`Encoding(map_size=11, players=2)` turns states and commands into NumPy arrays, following [`docs/encoding.md`](encoding.md). `ENCODING_VERSION` is the version of that layout; record it with saved data and models.

| | |
| --- | --- |
| `plane_count`, `global_count`, `action_count` | C, G and A (88, 84 and 26,410 for 11×11, 1v1) |
| `encode_state(game, player)` | `(planes, globals)`: float32 `[C, N, N]` and `[G]`, from what the player knows |
| `encode_states(games, players)` | a batch: `[B, C, N, N]` and `[B, G]`, encoded in C++ without the GIL |
| `legal_mask(game)`, `legal_masks(games)` | uint8 `[A]` or `[B, A]`: 1 for each legal command's slot |
| `encode_targets(games, players)` | the belief heads' training targets from the true games, and where each is trained: float32 and uint8 `[B, T]` ([`encoding.md`](encoding.md) section 8) |
| `target_count`, `target_heads` | T, and each head's `(name, opponent, offset, size)` within it |
| `encode_command(game, c)` | the command's slot, or -1 if it has none (Resign, Stay) |
| `decode_command(game, index)` | the legal command in that slot; raises `ValueError` if none is |
| `explained_score(view, p)` | the part of player `p`'s published score the viewer can account for |
| `possible_tribes(view, p, turns=1, max_states=5000, max_work=50000)` | the tribes unmet player `p` could be, from their starting score, the climates seen and a search over their first `turns` score changes ([`tribeInference.md`](tribeInference.md)); always includes the real one, and the limits only bound the time |
| `score_changes_fit(view, p, tribe, ...)` | the search alone: could a player of `tribe` have made `p`'s score changes so far (two-player games) |
| `climates_seen(view)` | the tribes whose climate the player has seen |
| `starting_explored(tribe, score)` | the tiles a player of `tribe` with that starting score started with explored, -1 if none fits: where their capital is |
| `tribes_with_starting_score(score)` | the tribes that could start a game with that score |

```python
enc = pe.Encoding()   # 11x11, 1v1: the map must fit
tribes = [pe.Tribe.IMPERIUS, pe.Tribe.BARDUR]
game = pe.Game(pe.generate_map(tribes, size=11, seed=1), tribes, seed=1)
planes, globals_ = enc.encode_state(game, game.current_player)
mask = enc.legal_mask(game)
# ... the network picks an index where mask == 1 ...
game.apply(enc.decode_command(game, index))
```

### Search and bots

| | |
| --- | --- |
| `LegalMoves(game)` | the legal commands, kept up to date one command at a time |
| `legal.commands` | the current list; matches `game.legal_commands()` as a set |
| `legal.apply(game, c)` | applies `c` to `game` and updates the list |
| `legal.update(before, after)` | updates it for any other change between two games |
| `legal.copy()` | an independent copy, for a search node |
| `RandomBot(seed=0)` | `choose(game)` → one command; `play_turn(game, max_commands=200)` → the commands it played |
| `play_random_games(games, tribes, type=LAKES, size=14, seed=0, max_turns=100)` | plays random games on generated maps, entirely in C++; returns `(winner, turns)` for each, with winner -1 if the turn limit came first |

## Speed

Every call from Python into the engine costs about as much as a game step:

| | Per step |
| --- | --- |
| C++, with `LegalMoves` | about 6 µs |
| Python calling `legal_commands()`, `choose()`, `apply_unchecked()` | about 14 µs |

That's fine for scripts, evaluation and notebooks. For anything that runs per move at scale, like self-play or search, the loop belongs in C++ and should be reached through one call, the way `play_random_games` works. Calls that loop in C++ release the GIL, so other Python threads keep running.
