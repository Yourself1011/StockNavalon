"""Play against a trained network in the terminal: the engine's CLI (README
"Run"), with the network's search in the seats given with --bot. It searches
on determinized games, so it only knows what its own player could.

    python -m stocknavalon.play runs/second lakes Imperius Bardur --size 11 --bot 1

To watch it play itself, make every seat a bot and add --watch (Enter for
each turn) or --delay SECONDS:

    python -m stocknavalon.play runs/ninth lakes Imperius Bardur --size 11 --bot 0 --bot 1 --watch

The first argument is a run directory (its best network) or a .pt file,
such as a snapshot in checkpoints/. Everything after it goes to the CLI, apart
from these options.
"""

from __future__ import annotations

import argparse
import os
import sys

import torch

import polyengine as pe

from .model import Network, load_weights, pick_device


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
    parser.add_argument("--horizon", type=int, default=1)
    parser.add_argument("--device", default=None)
    parser.add_argument("--peace-treaties", action=argparse.BooleanOptionalAction,
                        default=False,
                        help="allow peace treaties (off, as in training)")
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    args, rest = parser.parse_known_args()
    if not args.peace_treaties:
        rest.append("--no-treaties")

    device = torch.device(args.device) if args.device else pick_device()
    model = load(args.checkpoint, device, args.channels, args.blocks)
    encoding = pe.Encoding()

    @torch.no_grad()
    def evaluate(planes, globals_, rows, slots):
        out = model(torch.from_numpy(planes).to(device),
                    torch.from_numpy(globals_).to(device))
        logits = out["policy"][torch.from_numpy(rows).to(device),
                               torch.from_numpy(slots).to(device)]
        return logits.float().cpu().numpy(), out["value"].float().cpu().numpy()

    sys.exit(pe.run_cli(rest, encoding, evaluate, simulations=args.simulations,
                        samples=args.samples, horizon=args.horizon))


if __name__ == "__main__":
    main()
