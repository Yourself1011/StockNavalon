"""How fast the network runs on this machine, and self-play with it.

    python -m stocknavalon.measure [--device mps|cpu] [--dtypes float32 float16 bfloat16]
    python -m stocknavalon.measure --dtypes float16 --checkpoint runs/tenth --parallel 256 512

Each precision is timed in turn: network speed by batch size, then self-play
with the run's own settings (run.py's defaults: two sessions, the playout cap,
turn search) at each --parallel, for --seconds each. A precision the device
can't run is reported and skipped. With --checkpoint the network is a trained
one, whose games look like a run's (an untrained network's are much shorter).
"""

from __future__ import annotations

import argparse
import time

import torch

import polyengine as pe

from .model import DTYPES, Network, pick_device
from .run import parser as run_parser
from .run import self_play_settings
from .selfplay import SelfPlay


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default=None)
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    parser.add_argument("--dtypes", nargs="+", default=["float32", "float16", "bfloat16"],
                        choices=sorted(DTYPES))
    parser.add_argument("--threads", type=int, default=4,
                        help="each self-play session's threads (run.py's --session-threads)")
    parser.add_argument("--parallel", type=int, nargs="+", default=[128, 256, 512],
                        help="games at once in each self-play session (run.py's --parallel)")
    parser.add_argument("--seconds", type=float, default=30.0,
                        help="how long self-play is timed at each --parallel")
    parser.add_argument("--checkpoint", default=None,
                        help="a run directory or .pt: time self-play with a trained network")
    args = parser.parse_args()
    device = torch.device(args.device) if args.device else pick_device()
    encoding = pe.Encoding()
    for name in args.dtypes:
        try:
            measure(encoding, device, args, name)
        except (RuntimeError, TypeError) as e:
            print(f"{name}: can't run on {device} ({str(e).splitlines()[0]})")


def measure(encoding, device, args, name):
    dtype = DTYPES[name]
    model = Network(encoding, args.channels, args.blocks).to(device, dtype).eval()
    params = sum(p.numel() for p in model.parameters())
    print(f"\n{name}: device {device}, {params / 1e6:.2f}M parameters")

    n = encoding.map_size
    for batch in (16, 64, 256, 1024):
        planes = torch.randn(batch, encoding.plane_count, n, n, device=device, dtype=dtype)
        globals_ = torch.randn(batch, encoding.global_count, device=device, dtype=dtype)
        with torch.no_grad():
            # warm up, then wait for the GPU before timing
            for _ in range(3):
                model(planes, globals_)
            model(planes, globals_)["policy"].cpu()
            start = time.perf_counter()
            reps = 10
            for _ in range(reps):
                out = model(planes, globals_)
                # include bringing the results back, as self-play does
                out["policy"].cpu()
        ms = 1000 * (time.perf_counter() - start) / reps
        print(f"batch {batch:5d}: {ms:7.1f} ms, {batch / ms * 1000:9.0f} positions/s")

    if args.checkpoint:
        from .play import load
        model = load(args.checkpoint, device, args.channels, args.blocks).to(dtype).eval()
    run_args = run_parser().parse_args(["--dir", "measure", "--session-threads", str(args.threads)])
    config = self_play_settings(run_args, fog_share=1.0)
    for parallel in args.parallel:
        runner = SelfPlay(encoding, device, config, parallel=parallel, seed=1)
        runner.play(model, 1)  # warm up: games in flight
        evaluated = batches = 0
        start = time.perf_counter()
        while time.perf_counter() - start < args.seconds:
            _, stats = runner.play(model, 1)
            evaluated += stats["evaluated"]
            batches += stats["batches"]
        seconds = time.perf_counter() - start
        split = ", ".join(f"{k} {v / max(1e-9, sum(runner.times.values())):.0%}"
                          for k, v in runner.times.items())
        print(f"self-play, {parallel} games a session: {evaluated / seconds:.0f} positions "
              f"evaluated/s, average batch {evaluated / max(1, batches):.0f} ({split})")

if __name__ == "__main__":
    main()
