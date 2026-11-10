"""Evaluation matches (docs/stockNavalon.md section 7.2): a network against
another network, the score-evaluator search, or the random bot.

Games come in pairs, the same map and tribes twice with the sides swapping
seats, so neither the map nor moving first favours a side. Both sides search
the same way, without root noise; the first turn picks moves in proportion to
visits for variety, then the most visited. A game cut off by the turn limit
goes to the side ahead on score.
"""

from __future__ import annotations

import contextlib
import time

import numpy as np
import torch

import polyengine as pe

from .model import Network


def score_games(records) -> dict:
    """Side 0's results: wins (a draw on equal scores counts half), and how
    many games a capture decided."""
    points = decided = 0.0
    for r in records:
        seat = 0 if r.seat_zero_side == 0 else 1  # side 0's seat
        if r.winner >= 0:
            decided += 1
            points += 1.0 if r.winner == seat else 0.0
        else:
            lead = r.final_scores[seat] - r.final_scores[1 - seat]
            points += 1.0 if lead > 0 else 0.5 if lead == 0 else 0.0
    n = len(records)
    return {"games": n, "win_rate": round(points / n, 3) if n else None,
            "decided": round(decided / n, 3) if n else None}


@torch.no_grad()
def match(model: Network, opponent, encoding: pe.Encoding, device: torch.device,
          games: int = 64, parallel: int = 32, seed: int = 0,
          simulations: int = 50, samples: int = 2, max_turns: int = 50,
          gpu_lock=None) -> dict:
    """`model` (side 0) against `opponent` (side 1): another Network, or
    "score" or "random". Returns side 0's results."""
    gpu_lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
    networks = [model, opponent if isinstance(opponent, Network) else None]
    sides = ["network", "network" if networks[1] is not None else opponent]
    for net in networks:
        if net is not None:
            net.eval()
    games += games % 2  # whole pairs
    session = pe.SelfPlaySession(
        parallel, games, seed=seed, simulations=simulations, samples=samples,
        max_turns=max_turns, temperature_turns=1, root_noise=False,
        full_share=1.0, sides=sides, paired=True)

    start = time.perf_counter()
    while not session.done():
        planes, globals_, _ = session.pending_inputs(encoding, legal=False)
        b = len(planes)
        if b == 0:
            continue
        rows, slots = session.pending_slots(encoding)
        side_of = session.pending_sides()
        values = np.zeros(b, np.float32)
        move_logits = np.zeros(len(slots), np.float32)
        for side in (0, 1):
            idx = np.nonzero(side_of == side)[0]
            if len(idx) == 0:
                continue
            # this side's positions, and its moves among all the moves
            local = np.full(b, -1, np.int64)
            local[idx] = np.arange(len(idx))
            moves = np.nonzero(side_of[rows] == side)[0]
            with gpu_lock:
                out = networks[side](torch.from_numpy(planes[idx]).to(device),
                                     torch.from_numpy(globals_[idx]).to(device))
                picked = out["policy"][torch.from_numpy(local[rows[moves]]).to(device),
                                       torch.from_numpy(slots[moves]).to(device)]
                move_logits[moves] = picked.float().cpu().numpy()
                values[idx] = out["value"].float().cpu().numpy()
        session.submit_move_logits(encoding, move_logits, values)

    result = score_games(session.take_finished())
    result["seconds"] = round(time.perf_counter() - start)
    return result
