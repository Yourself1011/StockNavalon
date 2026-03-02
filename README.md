# StockNavalon

## Build

```bash
make dev    # dev build with sanitizers
make        # release build
```

## Run

```bash
./build/final_program
```

## CLI Commands


| Cmd | Args                  | Description                                            |
| --- | --------------------- | ------------------------------------------------------ |
| `l` | `x y`                 | List valid moves and attacks for a unit at (x, y)      |
| `m` | `fromX fromY toX toY` | Move a unit from one tile to another                   |
| `a` | `fromX fromY toX toY` | Attack a unit at the target tile                       |
| `i` | `x y`                 | Inspect a unit's stats (health, attack, defense, etc.) |
| `e` |                       | End turn                                               |


