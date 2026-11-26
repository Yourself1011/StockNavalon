"""How each side of a saved match lost its games (versus --save): for every
game lost by capture, the board at the loser's last chance to stop it,
against what the loser could see then.

    python -m stocknavalon.versus A B --save match.txt
    python -m stocknavalon.losses match.txt

Per side, over the games it lost, at its last chance (the end of its turn
before the unit that took its capital stepped onto it: a unit captures only a
city it started its turn on): its units within 2 tiles of its capital (and
whether one stood on it); the enemy units within 2 tiles, and how many of
those it could see; whether the taker was in sight, and how far; then how
many of its turns it left the enemy on its capital. Few defenders with the
attackers in sight: it saw it coming and didn't defend; with them out of
sight, it was caught in the fog. Changes nothing.
"""

from __future__ import annotations

import argparse
import copy
import statistics

import polyengine as pe

NEAR = 2


def loss(r: pe.GameRecord) -> dict | None:
    """The loser's last chance, or None for a game not decided by capture: the
    end of its turn before the unit that took its capital stepped onto it
    (a unit captures only a city it started its turn on, so it got there a
    turn earlier)."""
    if r.winner < 0 or r.stalled:
        return None
    loser = 1 - r.winner
    game = r.start()
    capital = game.cities[game.players[loser].capital].tile
    arrivals = (pe.CommandType.MOVE, pe.CommandType.ATTACK, pe.CommandType.DISEMBARK)
    end = None      # the game at the end of the loser's latest turn
    warning = None  # that game when the taker stepped onto the capital, and from where
    held = 0  # the loser's turns that ended with an enemy on its capital
    captured = False
    for c in r.commands:
        if game.current_player != loser and c.type in arrivals and c.to_tile == capital:
            warning = (end, c.from_tile)
        if c.type == pe.CommandType.CAPTURE and c.from_tile == capital and game.current_player != loser:
            captured = True
            break
        game.apply(c)
        if c.type == pe.CommandType.ENDTURN and game.current_player == r.winner:
            end = copy.copy(game)
            sieged = any(u.tile == capital and u.owner != loser for u in game.units)
            held = held + 1 if sieged else 0
    if not captured or warning is None or warning[0] is None:
        return None
    before, came_from = warning
    view = pe.make_view(before, loser)
    mine = [u for u in before.units if u.owner == loser]
    near = lambda u: before.distance(u.tile, capital) <= NEAR
    attackers = [u for u in before.units if u.owner != loser and near(u)]
    return {
        "turn": r.turns,
        "units": len(mine),
        "defenders": sum(near(u) for u in mine),
        "on_capital": any(u.tile == capital for u in mine),
        "attackers": len(attackers),
        "seen": sum(view.tiles[u.tile].unit >= 0 for u in attackers),
        "taker_seen": view.tiles[came_from].unit >= 0,
        "taker_distance": before.distance(came_from, capital),
        "held_turns": held,
    }


def summary(name: str, rows: list[dict]):
    if not rows:
        print(f"{name}: no losses by capture")
        return
    n = len(rows)
    mean = lambda k: statistics.mean(r[k] for r in rows)
    share = lambda k: 100 * sum(bool(r[k]) for r in rows) / n
    seen = 100 * sum(r["seen"] for r in rows) / max(1, sum(r["attackers"] for r in rows))
    print(f"{name}: {n} games lost by capture, on turn "
          f"{statistics.median(r['turn'] for r in rows):.0f} (median)")
    print(f"  its last chance (the end of its turn before the taker stepped on): "
          f"{mean('units'):.1f} units, {mean('defenders'):.1f} within {NEAR} of the capital, "
          f"one on it in {share('on_capital'):.0f}% of games")
    print(f"  enemies within {NEAR} then: {mean('attackers'):.1f}, {seen:.0f}% of them in sight; "
          f"the taker in sight in {share('taker_seen'):.0f}% of games, "
          f"{mean('taker_distance'):.1f} tiles out")
    print(f"  turns it then left the enemy on its capital: {mean('held_turns'):.1f}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("records", help="a match saved with versus --save")
    args = parser.parse_args()
    records = pe.load_records(args.records)
    by_side = {0: [], 1: []}
    unread = 0
    for r in records:
        row = loss(r)
        if row is None:
            # decided by capture, yet no last chance to read (taken before
            # the loser's first turn ended)
            unread += r.winner >= 0 and not r.stalled
            continue
        # the loser's side: side A sits in seat 0 when seat_zero_side is 0
        loser_seat = 1 - r.winner
        side = loser_seat if r.seat_zero_side == 0 else 1 - loser_seat
        by_side[side].append(row)
    summary("side A", by_side[0])
    summary("side B", by_side[1])
    if unread:
        print(f"({unread} games decided by capture left out: no turn of the loser's to read)")


if __name__ == "__main__":
    main()
