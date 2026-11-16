"""One network, two search settings, played against each other at the same
number of simulations, in seat-swapped pairs with fog and the network's
beliefs, as the run plays. Judged on decided games, as gates are. Changes no
training.

    python -m stocknavalon.searches runs/ninth                       # Gumbel against PUCT
    python -m stocknavalon.searches runs/ninth --candidates 16 8     # Gumbel's candidates
    python -m stocknavalon.searches runs/ninth --exploration 1.5 0.75
    python -m stocknavalon.searches runs/ninth --candidates 16 8 --exploration 1.5 0.75
    python -m stocknavalon.searches runs/ninth --side-simulations 64 512 --games 64

Side A is the first value given, side B the second. Each match also reports
how deep each side's searches went: mean depth in commands below the root,
and the share of simulations that reached the opponent's turn.
"""

from __future__ import annotations

import argparse
import math

import torch

import polyengine as pe

from .evaluate import match
from .model import DTYPES, pick_device
from .play import load


def main():
    parser = argparse.ArgumentParser(description="Two search settings, one network.")
    parser.add_argument("checkpoint", help="a run directory (its best network) or a .pt file")
    parser.add_argument("--simulations", type=int, nargs="+", default=[64])
    parser.add_argument("--games", type=int, default=64)
    parser.add_argument("--candidates", type=int, nargs=2, metavar=("A", "B"),
                        help="Gumbel's root candidates for each side")
    parser.add_argument("--exploration", type=float, nargs=2, metavar=("A", "B"),
                        help="the exploration constant below the root for each side")
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
    parser.add_argument("--device", default=None)
    parser.add_argument("--dtype", default="float16", choices=sorted(DTYPES))
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    args = parser.parse_args()

    device = torch.device(args.device) if args.device else pick_device()
    model = load(args.checkpoint, device, args.channels, args.blocks)
    if device.type != "cpu":
        model = model.to(DTYPES[args.dtype])
    encoding = pe.Encoding()
    if args.side_simulations:
        names = [f"{n} simulations" for n in args.side_simulations]
        setting = dict(gumbel=(True, True), side_simulations=tuple(args.side_simulations))
        # one match; --simulations doesn't apply
        args.simulations = [max(args.side_simulations)]
    elif args.candidates or args.exploration:
        # either or both, side by side
        setting = dict(gumbel=(True, True))
        parts = [[], []]
        if args.candidates:
            setting["side_gumbel_candidates"] = tuple(args.candidates)
            for k in (0, 1):
                parts[k].append(f"{args.candidates[k]} candidates")
        if args.exploration:
            setting["side_exploration"] = tuple(args.exploration)
            for k in (0, 1):
                parts[k].append(f"exploration {args.exploration[k]}")
        names = [", ".join(p) for p in parts]
    else:
        names = ["Gumbel", "PUCT"]
        setting = dict(gumbel=(True, False))
    for simulations in args.simulations:
        result = match(model, model, encoding, device, games=args.games,
                       parallel=args.games, simulations=simulations, samples=1,
                       max_turns=args.max_turns, stall_turns=args.stall_turns,
                       fog_share=args.fog_share, network_beliefs=args.fog_share > 0,
                       peace_treaties=False, threads=args.threads,
                       gumbel_candidates=args.gumbel_candidates,
                       score_utility=args.score_utility, win_sooner=args.win_sooner,
                       max_minutes=1e9, seed=simulations, **setting)
        won, lost = result["captures"], result["was_captured"]
        decided = won + lost
        share = won / decided if decided else math.nan
        # a rough 95% interval on the share of decided games won
        margin = 1.96 * math.sqrt(share * (1 - share) / decided) if decided else math.nan
        label = "each its own simulations" if args.side_simulations else f"{simulations} simulations"
        print(f"{label}: {names[0]} won {won}, {names[1]} won {lost} of "
              f"{decided} decided ({100 * share:.0f}% ± {100 * margin:.0f}), "
              f"{result['games']} games, {result['seconds'] / 60:.1f} min", flush=True)
        for name, d in zip(names, result["depth"]):
            print(f"  {name}: mean depth {d['mean_depth']:.2f} commands, "
                  f"{100 * d['reached_opponent']:.0f}% of simulations reached the "
                  f"opponent's turn", flush=True)


if __name__ == "__main__":
    main()
