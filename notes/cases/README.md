# Cases

Positions that reproduce a problem, as one-game record files (`polyengine.load_records`).

- `determinize_no_world.txt` (2026-11-24, runs/eleventh, rules 87, drylands, fog): after its first
  10 commands (turn 1, player 0 to move), `pe.determinize(game, 0, seed)` finds no world for 4 of
  seeds 0-19 (each seed is 20 draws): the draws are turned down with "no fog tile for the unit the
  player's moves show" (the movers check, determinize.cc), though the one enemy unit the player can't see, a Warrior on tile 41, is on a fog
  tile next to an explored one. In self-play a search with no world raises ("search: no moves for
  game 0"), which stopped the run; about 0.3% of positions fail like this.

      import polyengine as pe
      r = pe.load_records("notes/cases/determinize_no_world.txt")[0]
      g = r.start()
      for c in r.commands[:10]: g.apply(c)
      [s for s in range(20) if pe.determinize(g, g.current_player, s) is None]   # 4 seeds
