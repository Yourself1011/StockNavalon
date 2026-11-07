# StockNavalon

## Build

```bash
make        # release build -> build/release/final_program
make dev    # dev build with sanitizers -> build/dev/final_program
```

## Run

```bash
./build/release/final_program [map file | lakes] [tribe...] [--seed N] [--size N]
./build/release/final_program src/game/map.txt Imperius Bardur
./build/release/final_program lakes Imperius Bardur --seed 7
```

`lakes` generates a Lakes map (14x14 unless `--size` says otherwise) instead of loading a file.

`--bot N` makes seat N a random bot (repeat it for more bots). It plays its whole turn as soon as yours ends. Its commands are only listed with `--reveal`, since they'd give away what's in the fog. With every seat a bot, the game plays itself (stopping after 300 turns) and shows the final map.

```bash
./build/release/final_program lakes Imperius Bardur --bot 1 --seed 7
```

The map defaults to `src/game/map.txt`, and every player defaults to Imperius. The map is drawn with emojis; add `--ascii` for the old letter view if your terminal shows them badly. The map only shows what the current player has explored; add `--reveal` to see everything. The map file format is described in `src/game/map.h`.

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
