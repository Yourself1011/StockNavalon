"""Play against a trained network in the terminal: the engine's CLI (README
"Run"), with the network's search in the seats given with --bot. It searches
on determinized games, so it only knows what its own player could. Without
--seed, each game gets a random one, printed so it can be played again.

    python -m stocknavalon.play runs/second lakes Imperius Bardur --size 11 --bot 1

To watch it play itself, make every seat a bot and add --watch (Enter for
each turn) or --delay SECONDS; --pov shows each bot's own view, through its
fog, after its turn:

    python -m stocknavalon.play runs/ninth lakes Imperius Bardur --size 11 --bot 0 --bot 1 --watch

The first argument is a run directory (its best network) or a .pt file,
such as a snapshot in checkpoints/. Everything after it goes to the CLI, apart
from these options.
"""

from __future__ import annotations

import argparse
import os
import random
import sys

import numpy as np
import torch

import polyengine as pe

from .model import Network, belief_outputs, load_weights, pick_device, tie_breaks


def load(path: str, device: torch.device, channels: int, blocks: int) -> Network:
    """The network in a run directory or a .pt file. A run directory's is its
    best network (best.pt, the one self-play plays and gates promote), sized
    by its checkpoint. Snapshots don't record their size, so `channels` and
    `blocks` are for those."""
    config = {}
    if os.path.isdir(path):
        checkpoint = os.path.join(path, "checkpoint.pt")
        config = torch.load(checkpoint, map_location="cpu").get("config", {})
        best = os.path.join(path, "best.pt")
        path = best if os.path.exists(best) else checkpoint
    state = torch.load(path, map_location=device)
    config = state.get("config", config)
    model = Network(pe.Encoding(), config.get("channels", channels),
                    config.get("blocks", blocks)).to(device)
    load_weights(model, state["model"])
    print(f"{path}: {state.get('steps', '?')} training steps", file=sys.stderr)
    return model.eval()


def main():
    parser = argparse.ArgumentParser(
        description="Play against a trained network; other arguments go to the CLI.")
    parser.add_argument("checkpoint", help="a run directory or a .pt file")
    parser.add_argument("--simulations", type=int, default=200,
                        help="per determinized game")
    parser.add_argument("--samples", type=int, default=4,
                        help="determinized games per command")
    parser.add_argument("--horizon", type=int, default=pe.UNLIMITED_HORIZON,
                        help="turn changes a search line may cross; by default no cutoff")
    parser.add_argument("--score-utility", type=float, default=0.05,
                        help="tie-break toward a bigger predicted score lead")
    parser.add_argument("--win-sooner", type=float, default=0.05,
                        help="tie-break toward a quicker win (or a slower loss)")
    parser.add_argument("--exploration", type=float, default=1.5,
                        help="the search's exploration constant below the root")
    parser.add_argument("--gumbel-candidates", type=int, default=16)
    parser.add_argument("--gumbel", action=argparse.BooleanOptionalAction, default=True,
                        help="Gumbel search at the root")
    parser.add_argument("--explain", action="store_true",
                        help="after each bot turn, list its decisions: the command "
                             "played and the search's leading moves, with their "
                             "priors, visits and values (gives away the bot's "
                             "thinking, so for watching bots)")
    parser.add_argument("--device", default=None)
    parser.add_argument("--network-beliefs", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="sample the hidden state from the network's belief "
                             "heads, as in training with fog (else fixed priors)")
    parser.add_argument("--peace-treaties", action=argparse.BooleanOptionalAction,
                        default=False,
                        help="allow peace treaties (off, as in training)")
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    args, rest = parser.parse_known_args()
    # a new game each time unless a seed is given; printed, to play it again
    if "--seed" not in rest:
        seed = random.randrange(2**32)
        rest += ["--seed", str(seed)]
        print(f"seed {seed}", file=sys.stderr)
    if not args.peace_treaties:
        rest.append("--no-treaties")

    device = torch.device(args.device) if args.device else pick_device()
    model = load(args.checkpoint, device, args.channels, args.blocks)
    encoding = pe.Encoding()

    @torch.no_grad()
    def evaluate(planes, globals_, rows, slots, belief_rows):
        out = model(torch.from_numpy(planes).to(device),
                    torch.from_numpy(globals_).to(device))
        logits = out["policy"][torch.from_numpy(rows).to(device),
                               torch.from_numpy(slots).to(device)]
        beliefs = (belief_outputs(out, torch.from_numpy(belief_rows).to(device), encoding)
                   .float().cpu().numpy() if len(belief_rows) else None)
        return (logits.float().cpu().numpy(), out["value"].float().cpu().numpy(), beliefs,
                np.ascontiguousarray(tie_breaks(out).float().cpu().numpy()))

    sys.exit(pe.run_cli(rest, encoding, evaluate, simulations=args.simulations,
                        samples=args.samples, horizon=args.horizon,
                        network_beliefs=args.network_beliefs, gumbel=args.gumbel,
                        score_utility=args.score_utility, win_sooner=args.win_sooner,
                        exploration=args.exploration,
                        gumbel_candidates=args.gumbel_candidates,
                        explain=args.explain))


if __name__ == "__main__":
    main()
