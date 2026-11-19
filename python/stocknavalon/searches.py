"""One network, two search settings, played against each other at the same
number of simulations, in seat-swapped pairs with fog and the network's
beliefs, as the run plays. Judged on decided games, as gates are. Changes no
training.

    python -m stocknavalon.searches runs/ninth                       # Gumbel against PUCT
    python -m stocknavalon.searches runs/ninth --candidates 16 8     # Gumbel's candidates
    python -m stocknavalon.searches runs/ninth --exploration 1.5 0.75
    python -m stocknavalon.searches runs/ninth --candidates 16 8 --exploration 1.5 0.75
    python -m stocknavalon.searches runs/ninth --side-simulations 64 512 --games 64
    python -m stocknavalon.searches runs/ninth --turn-search off 8 --evaluations 1024
    python -m stocknavalon.searches runs/ninth --turn-search 8 --replies sampled discrepancy \
        --backup pbar --evaluations 200

Turn search (notes/design.md section 9): "off" is today's search, a number
is turn search with that many commandVisits (0: command by command until End
Turn, large: the policy completes nearly every turn), to --turn-horizon turns.
--evaluations sets both sides' budget in network evaluations a decision, so
the comparison is at equal cost. --replies (how the opponent's replies are
drawn) and --backup (how values are backed up) as in the other tools'
search flags (stocknavalon/settings.py). The per-side flags take one value
for both sides or one each, and combine: the sides differ in each that has
two values.

Side A is the first value given, side B the second. Each match also reports
how deep each side's searches went: mean depth in commands below the root,
and the share of simulations that reached the opponent's turn.
"""

from __future__ import annotations

import argparse
import math

import torch

import polyengine as pe

from .evaluate import clear_progress, match, show_progress
from .model import DTYPES, pick_device
from .play import load
from .settings import BACKUPS, REPLIES, modified, per_side, turn_search_values


def side_of_seat(r: pe.GameRecord, seat: int) -> int:
    """Which side (0: A, 1: B) played a seat in a match's game."""
    return seat if r.seat_zero_side == 0 else 1 - seat


def searched_decisions(records: list[pe.GameRecord]) -> list[int]:
    """Per side, the decisions searched (every move, in a match)."""
    counts = [0, 0]
    for r in records:
        game = r.start()
        for c in r.commands:
            counts[side_of_seat(r, game.current_player)] += 1
            game.apply(c)
    return counts


def villages_left(records: list[pe.GameRecord]) -> list[float]:
    """Per side, the share of its turns that started with a unclaimed village
    or ruin a unit could step onto, where no unit stepped onto, captured or
    examined one that turn."""
    free = (pe.ImprovementType.VILLAGE, pe.ImprovementType.RUIN)
    take = (pe.CommandType.CAPTURE, pe.CommandType.EXAMINERUINS)
    turns, left = [0, 0], [0, 0]
    for r in records:
        game = r.start()
        cmds = r.commands
        i = 0
        while i < len(cmds):
            p = game.current_player
            tiles = game.tiles
            available = any(c.type == pe.CommandType.MOVE and tiles[c.to_tile].building in free
                            for c in game.legal_commands())
            taken = False
            while i < len(cmds) and game.current_player == p:
                c = cmds[i]
                tiles = game.tiles
                taken |= ((c.type == pe.CommandType.MOVE and tiles[c.to_tile].building in free)
                          or c.type in take)
                game.apply(c)
                i += 1
            if available:
                side = side_of_seat(r, p)
                turns[side] += 1
                left[side] += not taken
    return [left[k] / turns[k] if turns[k] else float("nan") for k in (0, 1)]


def main():
    parser = argparse.ArgumentParser(description="Two search settings, one network.")
    parser.add_argument("checkpoint", help="a run directory (its best network) or a .pt file")
    parser.add_argument("--simulations", type=int, nargs="+", default=[64])
    parser.add_argument("--games", type=int, default=64)
    parser.add_argument("--candidates", type=int, nargs=2, metavar=("A", "B"),
                        help="Gumbel's root candidates for each side")
    parser.add_argument("--exploration", type=float, nargs=2, metavar=("A", "B"),
                        help="the exploration constant below the root for each side")
    parser.add_argument("--turn-search", nargs="+", metavar="off|N",
                        help="turn search: off, or its commandVisits; one for both "
                             "sides or one each")
    parser.add_argument("--replies", nargs="+", choices=list(REPLIES),
                        help="turn search: how the opponent's replies are drawn; one for "
                             "both sides or one each")
    parser.add_argument("--backup", nargs="+", choices=list(BACKUPS),
                        help="how values are backed up; one for both sides or one each")
    parser.add_argument("--turn-horizon", type=int, default=3,
                        help="turn changes a turn search looks across")
    parser.add_argument("--samples", type=int, default=1,
                        help="determinized games per decision, both sides (play uses 4)")
    parser.add_argument("--evaluations", type=int,
                        help="both sides' budget, in network evaluations a decision")
    parser.add_argument("--side-simulations", type=int, nargs=2, metavar=("A", "B"),
                        help="simulations for each side, both with Gumbel search: "
                             "whether more search pays")
    parser.add_argument("--gumbel-candidates", type=int, default=16)
    parser.add_argument("--score-utility", type=float, default=0.05)
    parser.add_argument("--win-sooner", type=float, default=0.05)
    parser.add_argument("--fog-share", type=float, default=1.0)
    parser.add_argument("--max-turns", type=int, default=150)
    parser.add_argument("--stall-turns", type=int, default=25)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--seed", type=int,
                        help="the games' seed (maps, tribes); by default the budget")
    parser.add_argument("--device", default=None)
    parser.add_argument("--dtype", default="float16", choices=sorted(DTYPES))
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    args = parser.parse_args()

    device = torch.device(args.device) if args.device else pick_device()
    print(f"device: {device}", flush=True)
    model = load(args.checkpoint, device, args.channels, args.blocks)
    if device.type != "cpu":
        model = model.to(DTYPES[args.dtype])
    encoding = pe.Encoding()
    # what each side changes from the shared search
    changes: list[dict] = [{}, {}]
    if args.side_simulations:
        names = [f"{n} simulations" for n in args.side_simulations]
        for k in (0, 1):
            changes[k] = dict(simulations=args.side_simulations[k])
        # one match; --simulations doesn't apply
        args.simulations = [max(args.side_simulations)]
    else:
        # each side's changes, and how it's described
        parts: list[list[str]] = [[], []]
        if args.replies and not args.turn_search:
            raise SystemExit("--replies: only with --turn-search")
        if args.turn_search:
            for k, v in enumerate(per_side(turn_search_values(args.turn_search), 2,
                                           "--turn-search")):
                parts[k].append("today's search" if v is None else f"turn search ({v})")
                if v is not None:
                    changes[k].update(turn_search=True, command_visits=v,
                                      horizon=args.turn_horizon)
        for k, r in enumerate(per_side(args.replies, 2, "--replies")):
            if r is not None:
                changes[k]["replies"] = REPLIES[r]
                parts[k].append(f"{r} replies")
        for k, b in enumerate(per_side(args.backup, 2, "--backup")):
            if b is not None:
                changes[k]["backup"] = BACKUPS[b]
                parts[k].append(f"{b} backup")
        for k in (0, 1):
            if args.candidates:
                changes[k]["gumbel_candidates"] = args.candidates[k]
                parts[k].append(f"{args.candidates[k]} candidates")
            if args.exploration:
                changes[k]["exploration"] = args.exploration[k]
                parts[k].append(f"exploration {args.exploration[k]}")
        if parts[0] or parts[1]:
            names = [", ".join(p) for p in parts]
            if names[0] == names[1]:
                names = [f"A: {names[0]}", f"B: {names[1]}"]
        else:
            names = ["Gumbel", "PUCT"]
            changes[1] = dict(gumbel=False)
    if args.evaluations:
        args.simulations = [args.evaluations]
    for simulations in args.simulations:
        shared = pe.search_config(simulations=simulations, samples=args.samples, gumbel=True,
                                  gumbel_candidates=args.gumbel_candidates,
                                  score_utility=args.score_utility,
                                  win_sooner=args.win_sooner,
                                  count_evaluations=bool(args.evaluations))
        records: list[pe.GameRecord] = []
        result = match(model, model, encoding, device, games=args.games,
                       parallel=args.games, search=shared,
                       side_search=[modified(shared, **c) for c in changes],
                       max_turns=args.max_turns, stall_turns=args.stall_turns,
                       fog_share=args.fog_share, network_beliefs=args.fog_share > 0,
                       peace_treaties=False, threads=args.threads,
                       max_minutes=1e9, seed=simulations if args.seed is None else args.seed,
                       keep=records, progress=show_progress, progress_seconds=10)
        clear_progress()
        won, lost = result["captures"], result["was_captured"]
        decided = won + lost
        share = won / decided if decided else math.nan
        # a rough 95% interval on the share of decided games won
        margin = 1.96 * math.sqrt(share * (1 - share) / decided) if decided else math.nan
        label = ("each its own simulations" if args.side_simulations
                 else f"{simulations} evaluations a decision" if args.evaluations
                 else f"{simulations} simulations")
        print(f"{label}: {names[0]} won {won}, {names[1]} won {lost} of "
              f"{decided} decided ({100 * share:.0f}% ± {100 * margin:.0f}), "
              f"{result['games']} games, {result['seconds'] / 60:.1f} min", flush=True)
        decisions = searched_decisions(records)
        untaken = villages_left(records)
        for side, (name, d) in enumerate(zip(names, result["depth"])):
            print(f"  {name}: mean depth {d['mean_depth']:.2f} moves, "
                  f"{100 * d['reached_opponent']:.0f}% of simulations reached the "
                  f"opponent's turn, {d['mean_turns']:.2f} turns ahead on average; "
                  f"{d['evaluations'] / max(1, decisions[side]):.0f} evaluations a decision; "
                  f"a village or ruin in reach left in {100 * untaken[side]:.0f}% of turns",
                  flush=True)


if __name__ == "__main__":
    main()
