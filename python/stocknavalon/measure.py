"""How fast the network runs on this machine, and self-play with it.

    python -m stocknavalon.measure [--device mps|cpu] [--dtypes float32 float16 bfloat16]

Each precision is timed in turn: network speed by batch size, then self-play.
A precision the device can't run is reported and skipped.
"""

from __future__ import annotations

import argparse
import time

import numpy as np
import torch

import polyengine as pe

from .model import DTYPES, Network, pick_device
from .selfplay import play


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default=None)
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    parser.add_argument("--dtypes", nargs="+", default=["float32", "float16", "bfloat16"],
                        choices=sorted(DTYPES))
    parser.add_argument("--threads", type=int, default=4,
                        help="each self-play session's threads (run.py's --session-threads)")
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

    for parallel in (16, 64, 128):
        records, stats = play(model, encoding, device, games=parallel,
                              parallel=parallel, simulations=50, max_turns=3,
                              threads=args.threads)
        searched = sum(len(r.searched) for r in records)
        print(f"self-play, {parallel} games at once, {args.threads} threads, "
              f"50 simulations x 2 samples, "
              f"3 turns: {stats['evaluated'] / stats['seconds']:.0f} positions "
              f"evaluated/s, {searched / stats['seconds']:.1f} decisions/s, "
              f"average batch {stats['evaluated'] / max(1, stats['batches']):.0f}")


if __name__ == "__main__":
    main()
