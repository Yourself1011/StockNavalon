"""One network, two searches: Gumbel search at the root (side 0) against
PUCT (side 1), at the same number of simulations, in seat-swapped pairs with
fog and the network's beliefs, as the run plays. Judged on decided games, as
gates are. Changes no training.

    python -m stocknavalon.searches runs/ninth --simulations 16 64 --games 64
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
    parser = argparse.ArgumentParser(description="Gumbel search against PUCT, one network.")
    parser.add_argument("checkpoint", help="a run directory (its best network) or a .pt file")
    parser.add_argument("--simulations", type=int, nargs="+", default=[16, 64])
    parser.add_argument("--games", type=int, default=64)
    parser.add_argument("--gumbel-candidates", type=int, default=16)
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
    for simulations in args.simulations:
        result = match(model, model, encoding, device, games=args.games,
                       parallel=args.games, simulations=simulations, samples=1,
                       max_turns=args.max_turns, stall_turns=args.stall_turns,
                       fog_share=args.fog_share, network_beliefs=args.fog_share > 0,
                       peace_treaties=False, threads=args.threads,
                       gumbel=(True, False), gumbel_candidates=args.gumbel_candidates,
                       max_minutes=1e9, seed=simulations)
        won, lost = result["captures"], result["was_captured"]
        decided = won + lost
        share = won / decided if decided else math.nan
        # a rough 95% interval on the share of decided games won
        margin = 1.96 * math.sqrt(share * (1 - share) / decided) if decided else math.nan
        print(f"{simulations} simulations: Gumbel won {won}, PUCT won {lost} of {decided} "
              f"decided ({100 * share:.0f}% ± {100 * margin:.0f}), "
              f"{result['games']} games, {result['seconds'] / 60:.1f} min", flush=True)


if __name__ == "__main__":
    main()
