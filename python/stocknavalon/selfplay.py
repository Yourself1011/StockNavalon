"""Self-play with the network: many games at once in C++, every waiting
position across them evaluated in one batch (docs/stockNavalon.md section
6.2).

The games are split between two sessions that take turns, so the CPU and the
GPU work at the same time: while the GPU evaluates one session's batch, the
CPU prepares the other's (advancing its games, encoding its positions) and
feeds it the previous results. PyTorch runs on the GPU asynchronously until a
result is copied back, which is what lets the two overlap.
"""

from __future__ import annotations

import contextlib
import time

import numpy as np
import torch

import polyengine as pe

from .model import Network


class _Pending:
    """A batch launched on the GPU, not yet collected."""

    def __init__(self, moves: torch.Tensor, values: torch.Tensor, size: int):
        self.moves, self.values, self.size = moves, values, size


@torch.no_grad()
def _launch(model: Network, session: pe.SelfPlaySession, encoding: pe.Encoding,
            device: torch.device, gpu_lock) -> _Pending | None:
    """Prepares the session's waiting positions on the CPU and starts the
    network on them, without waiting for it. Only each position's move
    logits are kept (logits[rows, slots]), so a few KB come back from the GPU
    instead of every slot of every position."""
    planes, globals_, _ = session.pending_inputs(encoding, legal=False)
    if len(planes) == 0:
        return None
    rows, slots = session.pending_slots(encoding)
    with gpu_lock:
        out = model(torch.from_numpy(planes).to(device),
                    torch.from_numpy(globals_).to(device))
        moves = out["policy"][torch.from_numpy(rows).to(device),
                              torch.from_numpy(slots).to(device)]
    return _Pending(moves, out["value"], len(planes))


def _collect(session: pe.SelfPlaySession, encoding: pe.Encoding,
             pending: _Pending, gpu_lock):
    """Waits for the batch's results and gives them to the session."""
    with gpu_lock:
        moves = pending.moves.float().cpu().numpy()
        values = pending.values.float().cpu().numpy()
    session.submit_move_logits(encoding, moves, values)


class SelfPlay:
    """Continuous self-play: games keep going between calls, so a call ends
    as soon as enough games have finished, without waiting for the slowest
    (game lengths are heavy-tailed). A finished game's slot starts a new one
    at once, so batches stay full. Games carry on across calls with whatever
    network is passed next."""

    def __init__(self, encoding: pe.Encoding, device: torch.device,
                 parallel: int = 64, seed: int = 0, gpu_lock=None, **session_args):
        self.encoding, self.device = encoding, device
        # shared with training when both run at once, around GPU work
        self.gpu_lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
        # effectively endless: games are taken as they finish
        endless = 1 << 30
        self.sessions = [pe.SelfPlaySession(parallel, endless, seed=seed * 2 + i,
                                            **session_args) for i in range(2)]

    def play(self, model: Network, games: int, progress=None,
             progress_seconds: float = 30.0) -> tuple:
        """Plays until at least `games` more have finished (games finishing in
        the same batch all come back), with this network. Returns those
        records, and stats: positions evaluated, batches, seconds."""
        model.eval()
        encoding, device, sessions = self.encoding, self.device, self.sessions
        pending: list[_Pending | None] = [None, None]
        records = []
        evaluated = batches = 0
        start = reported = time.perf_counter()
        current = 0
        while len(records) < games or any(p is not None for p in pending):
            now = time.perf_counter()
            if progress is not None and now - reported >= progress_seconds:
                reported = now
                progress({"finished": len(records), "games": games,
                          "evaluated": evaluated, "batches": batches,
                          "seconds": now - start})
            # the CPU prepares this session while the GPU runs the other's
            # batch; once enough games are in, only what's in flight is
            # collected, so the sessions are left with nothing pending
            if pending[current] is None and len(records) < games:
                # the CPU work outside the lock, queueing the GPU's inside
                pending[current] = _launch(model, sessions[current], encoding,
                                           device, self.gpu_lock)
                if pending[current] is not None:
                    evaluated += pending[current].size
                    batches += 1
            other = 1 - current
            if pending[other] is not None:
                _collect(sessions[other], encoding, pending[other], self.gpu_lock)
                pending[other] = None
                records.extend(sessions[other].take_finished())
            current = other
        return records, {"evaluated": evaluated, "batches": batches,
                         "seconds": time.perf_counter() - start}


def play(model: Network, encoding: pe.Encoding, device: torch.device,
         games: int, parallel: int = 64, seed: int = 0, progress=None,
         progress_seconds: float = 30.0, **session_args) -> tuple:
    """`games` self-play games from scratch (see SelfPlay for continuous
    self-play). Returns the records, and stats."""
    runner = SelfPlay(encoding, device, parallel=parallel, seed=seed, **session_args)
    return runner.play(model, games, progress, progress_seconds)
