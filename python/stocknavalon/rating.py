"""Ratings on one scale (Elo) from a run's gate and anchor results, so
progress reads as one number rather than a chain of head-to-head matches.

    python -m stocknavalon.rating runs/ninth

Each network is named by its training steps; the random bot is fixed at 0.
Only games decided by a capture count, as in gates: a game cut off counts
for neither side. A Bradley-Terry fit, with one virtual draw per pairing so a
perfect record (every network against the random bot) gives a finite rating.

Matches are played at different fog shares (gates follow the schedule,
anchors always have fog), so a rating mixes conditions; it's for the trend.
"""

from __future__ import annotations

import argparse
import json
import math
import os
from collections import defaultdict
from typing import Iterable

ANCHOR = "random"
# virtual games per pairing, half won by each side
PRIOR_GAMES = 1.0


def results_from_log(path: str) -> list[tuple[str, str, int, int]]:
    """(player, opponent, player's captures, opponent's captures) from every
    gate and anchor match in a run's log."""
    out = []
    with open(path) as f:
        for line in f:
            if not line.startswith("{"):
                continue
            e = json.loads(line)
            if "captures" not in e or "was_captured" not in e:
                continue
            if e.get("event") == "gate" and "best" in e:
                opponent = str(e["best"])
            elif e.get("event") == "evaluation" and "against" in e:
                opponent = e["against"].removeprefix("step_")
            else:
                continue
            out.append((str(e["steps"]), opponent, int(e["captures"]), int(e["was_captured"])))
    return out


def fit(results: Iterable[tuple[str, str, int, int]], iterations: int = 2000,
        anchor: str = ANCHOR) -> dict[str, float]:
    """Elo ratings from (a, b, a's wins, b's wins), with `anchor` at 0 (or
    the lowest player at 0 if the anchor never played)."""
    wins: dict[str, float] = defaultdict(float)
    games: dict[tuple[str, str], float] = defaultdict(float)
    for a, b, won, lost in results:
        if a == b:
            continue
        wins[a] += won
        wins[b] += lost
        games[a, b] += won + lost
    pairs = list(games)
    for a, b in pairs:
        wins[a] += PRIOR_GAMES / 2
        wins[b] += PRIOR_GAMES / 2
        games[a, b] += PRIOR_GAMES
    players = sorted(wins)
    if not players:
        return {}
    # Zermelo's iteration for Bradley-Terry strengths
    strength = dict.fromkeys(players, 1.0)
    opponents: dict[str, list[tuple[str, float]]] = defaultdict(list)
    for (a, b), n in games.items():
        opponents[a].append((b, n))
        opponents[b].append((a, n))
    for _ in range(iterations):
        new = {}
        for p in players:
            denominator = sum(n / (strength[p] + strength[q]) for q, n in opponents[p])
            new[p] = wins[p] / denominator
        # keep the scale fixed (geometric mean 1)
        mean = math.exp(sum(math.log(s) for s in new.values()) / len(new))
        strength = {p: s / mean for p, s in new.items()}
    elo = {p: 400 * math.log10(s) for p, s in strength.items()}
    zero = elo[anchor] if anchor in elo else min(elo.values())
    return {p: round(r - zero) for p, r in elo.items()}


def best_networks(path: str) -> list[int]:
    """The networks that were the best (self-play's), in order: those that
    passed a gate, and the ones gates were played against."""
    best = set()
    with open(path) as f:
        for line in f:
            if not line.startswith("{"):
                continue
            e = json.loads(line)
            if e.get("event") == "gate":
                if "best" in e:
                    best.add(int(e["best"]))
                if e.get("verdict") == "pass":
                    best.add(int(e["steps"]))
    return sorted(best)


def run_ratings(run_dir: str) -> dict[str, float]:
    path = os.path.join(run_dir, "log.txt")
    return fit(results_from_log(path)) if os.path.exists(path) else {}


def main():
    parser = argparse.ArgumentParser(description="Elo ratings from a run's gates and anchors.")
    parser.add_argument("run", help="a run directory")
    parser.add_argument("--all", action="store_true",
                        help="every network, not only the best ones")
    args = parser.parse_args()
    path = os.path.join(args.run, "log.txt")
    results = results_from_log(path)
    ratings = fit(results)
    decided: dict[str, int] = defaultdict(int)
    for a, b, won, lost in results:
        decided[a] += won + lost
        decided[b] += won + lost
    best = set(map(str, best_networks(path)))
    names = [p for p in ratings if not p.isdigit()]
    names += sorted((p for p in ratings if p.isdigit() and (args.all or p in best)), key=int)
    for name in names:
        label = f"step {int(name):,}" if name.isdigit() else name
        print(f"{label:>14}  {ratings[name]:6.0f}  ({decided[name]} decided games)")


if __name__ == "__main__":
    main()
