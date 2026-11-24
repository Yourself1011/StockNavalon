"""Networks trained offline on a run's games, side by side, by its training
settings: whether a bigger network learns the same data better (notes/todo.md,
"network size"). No self-play: every network trains on the same buffer (the
run's records.txt) and is judged on the same held-out positions, drawn from
the run's latest held-out games, so neither the data nor an old held-out set
favours one.

    python -m stocknavalon.offline --dir runs/ninth --sizes 64x6 128x10 --steps 10000

Every flag of stocknavalon.run applies (--batch, --lr, --value-share,
--value-lambda, ...), as the run would train; with --init, every network
starts from those weights (its size must match), so training settings can be
compared from a trained network:

    python -m stocknavalon.offline --dir runs/tenth --init runs/tenth/best.pt --sizes 64x6 \
        --value-lambda 1.0 --lr 5e-5 --city-weight 0 --out runs/tenth/offline_lambda1
 Every --eval-every steps, each
network's average is scored on the held-out positions, next to the run's own
network for reference; the averages are saved to --out at the end (each with
its size, so stocknavalon.play and versus load them).
"""

from __future__ import annotations

import json
import os
import time

import torch

import polyengine as pe

from .model import load_weights, pick_device
from .run import (HELDOUT_POSITIONS, make_trainer, parser, run_city_weight,
                  run_learning_rate)
from .train import heldout_losses, reanalyse
from .versus import load_side

SHOWN = ("total", "policy", "policy_kl", "value", "value_bias", "short_term_value")


def size_of(text: str) -> tuple[int, int]:
    """'128x10' as (channels, blocks)."""
    channels, blocks = text.lower().split("x")
    return int(channels), int(blocks)


def main():
    p = parser()
    p.description = "Networks trained offline on a run's games, compared."
    p.add_argument("--sizes", nargs="+", default=["64x6", "128x10"],
                   help="networks to train, as CHANNELSxBLOCKS")
    p.add_argument("--steps", type=int, default=10_000)
    p.add_argument("--eval-every", type=int, default=500)
    p.add_argument("--heldout-games", type=int, default=400,
                   help="the latest this many held-out games, for the held-out positions")
    p.add_argument("--heldout-positions", type=int, default=4 * HELDOUT_POSITIONS)
    p.add_argument("--out", default=None, help="where to save (default: <dir>/offline)")
    args = p.parse_args()
    out = args.out or os.path.join(args.dir, "offline")
    os.makedirs(out, exist_ok=True)
    device = torch.device(args.device) if args.device else pick_device()
    encoding = pe.Encoding()
    print(f"device: {device}", flush=True)

    # the data: every network gets its own buffer of the same games (each
    # keeps its own reanalysed values for the lambda-return)
    games = pe.load_records(os.path.join(args.dir, "records.txt"))
    held = [r for r in pe.load_records(os.path.join(args.dir, "holdout.txt"))
            if r.pool_opponent_seat < 0 and r.handicap_leader < 0 and not r.catch_up]
    holdout = pe.ReplayBuffer(10_000_000)
    holdout.add(held[-args.heldout_games:])
    examples = holdout.sample_examples(encoding, args.heldout_positions, 0, per_game=1)
    print(f"{len(games)} training games; {len(examples['value'])} held-out positions from "
          f"the latest {min(len(held), args.heldout_games)} held-out games", flush=True)

    # the run's own network, for reference
    reference = os.path.join(args.dir, "checkpoint.pt")
    if os.path.exists(reference):
        net, name = load_side(reference, device, False, args.channels, args.blocks)
        ref = heldout_losses(net, device, examples)
        print(f"reference, {name}: " + ", ".join(f"{k} {ref[k]:.3f}" for k in SHOWN if k in ref),
              flush=True)
        del net

    runs = []
    for text in args.sizes:
        channels, blocks = size_of(text)
        trainer = make_trainer(args, encoding, device, channels, blocks)
        if args.init:
            # the run's way: the weights, and their average starting from them
            load_weights(trainer.model, torch.load(args.init, map_location=device)["model"])
            trainer.ema.load_state_dict(trainer.model.state_dict())
        buffer = pe.ReplayBuffer(args.buffer)
        buffer.add(games)
        parameters = sum(p.numel() for p in trainer.model.parameters())
        runs.append({"size": text, "channels": channels, "blocks": blocks,
                     "trainer": trainer, "buffer": buffer, "seconds": 0.0})
        print(f"{text}: {parameters / 1e6:.2f}M parameters", flush=True)

    log_path = os.path.join(out, "log.jsonl")
    for step in range(1, args.steps + 1):
        for run in runs:
            trainer, buffer = run["trainer"], run["buffer"]
            start = time.perf_counter()
            # the lambda-return's values, as the run reanalyses them
            if args.value_lambda < 1 and (step - 1) % args.reanalyse_steps == 0:
                reanalyse(buffer, trainer.ema, encoding, device)
            trainer.set_learning_rate(run_learning_rate(args, trainer.steps, trainer.fresh_steps))
            trainer.step(buffer, args.batch, run_city_weight(args, trainer.steps))
            run["seconds"] += time.perf_counter() - start
        if step % args.eval_every == 0 or step == args.steps:
            for run in runs:
                losses = heldout_losses(run["trainer"].ema, device, examples)
                entry = {"steps": step, "size": run["size"],
                         "ms_per_step": round(1000 * run["seconds"] / step, 1), **losses}
                with open(log_path, "a") as f:
                    f.write(json.dumps(entry) + "\n")
                print(f"step {step:6d}  {run['size']:>7s} ({entry['ms_per_step']:.0f} ms/step): "
                      + ", ".join(f"{k} {losses[k]:.3f}" for k in SHOWN if k in losses),
                      flush=True)

    for run in runs:
        path = os.path.join(out, f"{run['size']}.pt")
        torch.save({"model": run["trainer"].ema.state_dict(), "steps": run["trainer"].steps,
                    "config": {"channels": run["channels"], "blocks": run["blocks"]}}, path)
        print(f"saved {path}", flush=True)


if __name__ == "__main__":
    main()
