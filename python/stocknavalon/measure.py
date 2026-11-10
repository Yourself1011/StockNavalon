"""How fast the network runs on this machine, and self-play with it.

    python -m stocknavalon.measure [--device mps|cpu]
"""

from __future__ import annotations

import argparse
import time

import numpy as np
import torch

import polyengine as pe

from .model import Network, pick_device
from .selfplay import play


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default=None)
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    args = parser.parse_args()
    device = torch.device(args.device) if args.device else pick_device()
    encoding = pe.Encoding()
    model = Network(encoding, args.channels, args.blocks).to(device).eval()
    params = sum(p.numel() for p in model.parameters())
    print(f"device {device}, {params / 1e6:.2f}M parameters")

    n = encoding.map_size
    for batch in (16, 64, 256, 1024):
        planes = torch.randn(batch, encoding.plane_count, n, n, device=device)
        globals_ = torch.randn(batch, encoding.global_count, device=device)
        with torch.no_grad():
            for _ in range(3):
                out = model(planes, globals_)
            out["policy"].cpu()
            start = time.perf_counter()
            reps = 10
            for _ in range(reps):
                out = model(planes, globals_)
                # include bringing the results back, as self-play does
                out["policy"].cpu()
        ms = 1000 * (time.perf_counter() - start) / reps
        print(f"batch {batch:5d}: {ms:7.1f} ms, {batch / ms * 1000:9.0f} positions/s")

    for parallel in (16, 64):
        records, stats = play(model, encoding, device, games=parallel,
                              parallel=parallel, simulations=50, max_turns=3)
        searched = sum(len(r.searched) for r in records)
        print(f"self-play, {parallel} games at once, 50 simulations x 2 samples, "
              f"3 turns: {stats['evaluated'] / stats['seconds']:.0f} positions "
              f"evaluated/s, {searched / stats['seconds']:.1f} decisions/s, "
              f"average batch {stats['evaluated'] / max(1, stats['batches']):.0f}")


if __name__ == "__main__":
    main()
