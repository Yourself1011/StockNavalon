"""Self-play with the network: many games at once in C++, every waiting
position across them evaluated in one batch (docs/stockNavalon.md section
5).

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

from .model import Network, as_input, belief_outputs, tie_breaks


class _Pending:
    """A batch launched on the GPU, not yet collected."""

    def __init__(self, moves: torch.Tensor, values: torch.Tensor, size: int,
                 beliefs: torch.Tensor | None, tie_breaks: torch.Tensor):
        self.moves, self.values, self.size = moves, values, size
        # raw belief outputs for the positions that want them
        self.beliefs = beliefs
        # the predicted score lead and turns left [B, 2], for the search
        self.tie_breaks = tie_breaks


# Where self-play's time goes, in seconds (SelfPlay.times): the CPU (advancing
# games, encoding, handing results back), waiting for the GPU lock (training
# or a gate using the GPU), and the GPU (queueing a batch and waiting for its
# results). Mostly the CPU means more cores would help; mostly the GPU or the
# lock, a faster GPU.
TIMES = ("cpu", "lock", "gpu")


class _Clock:
    def __init__(self, times: dict[str, float]):
        self.times, self.last = times, time.perf_counter()

    def to(self, kind: str):
        """The time since the last call goes to `kind`."""
        now = time.perf_counter()
        self.times[kind] += now - self.last
        self.last = now


@torch.no_grad()
def _launch(networks: list, session: pe.SelfPlaySession, encoding: pe.Encoding,
            device: torch.device, gpu_lock, clock: _Clock) -> _Pending | None:
    """Prepares the session's waiting positions on the CPU and starts the
    network on them, without waiting for it: one network for every position,
    or one per side (a match, or an opponent-pool game), each side's outputs
    put together on the GPU. Only each position's move logits are kept
    (logits[rows, slots]), so a few KB come back from the GPU instead of
    every slot of every position."""
    planes, globals_, _ = session.pending_inputs(encoding, legal=False)
    b = len(planes)
    if b == 0:
        clock.to("cpu")
        return None
    rows, slots = session.pending_slots(encoding)
    belief_rows = session.pending_belief_rows()
    if all(n is networks[0] for n in networks):
        clock.to("cpu")
        model = networks[0]
        with gpu_lock:
            clock.to("lock")
            out = model(as_input(planes, model, device), as_input(globals_, model, device))
            moves = out["policy"][torch.from_numpy(rows).to(device),
                                  torch.from_numpy(slots).to(device)]
            beliefs = (belief_outputs(out, torch.from_numpy(belief_rows).to(device), encoding)
                       if len(belief_rows) else None)
            clock.to("gpu")
        return _Pending(moves, out["value"], b, beliefs, tie_breaks(out))

    side_of = session.pending_sides()
    clock.to("cpu")
    on = lambda a: torch.from_numpy(a).to(device)
    with gpu_lock:
        clock.to("lock")
        moves = torch.zeros(len(slots), device=device)
        values = torch.zeros(b, device=device)
        ties = torch.zeros((b, 2), device=device)
        beliefs = (torch.zeros((len(belief_rows), encoding.belief_output_count), device=device)
                   if len(belief_rows) else None)
        for side, network in enumerate(networks):
            idx = np.nonzero(side_of == side)[0]
            if len(idx) == 0:
                continue
            assert network is not None  # only network sides' positions are pending
            # this side's positions, and its moves among all the moves
            local = np.full(b, -1, np.int64)
            local[idx] = np.arange(len(idx))
            out = network(as_input(planes[idx], network, device),
                          as_input(globals_[idx], network, device))
            mine = np.nonzero(side_of[rows] == side)[0]
            moves[on(mine)] = out["policy"][on(local[rows[mine]]), on(slots[mine])].float()
            values[on(idx)] = out["value"].float()
            ties[on(idx)] = tie_breaks(out).float()
            mine = np.nonzero(side_of[belief_rows] == side)[0]
            if len(mine):
                beliefs[on(mine)] = belief_outputs(out, on(local[belief_rows[mine]]),
                                                   encoding).float()
        clock.to("gpu")
    return _Pending(moves, values, b, beliefs, ties)


def _collect(session: pe.SelfPlaySession, encoding: pe.Encoding,
             pending: _Pending, gpu_lock, clock: _Clock):
    """Waits for the batch's results and gives them to the session."""
    clock.to("cpu")
    with gpu_lock:
        clock.to("lock")
        moves = pending.moves.float().cpu().numpy()
        values = pending.values.float().cpu().numpy()
        beliefs = (None if pending.beliefs is None
                   else np.ascontiguousarray(pending.beliefs.float().cpu().numpy()))
        ties = np.ascontiguousarray(pending.tie_breaks.float().cpu().numpy())
        clock.to("gpu")
    session.submit_move_logits(encoding, moves, values, beliefs, ties)
    clock.to("cpu")


def alternate(sessions: list[pe.SelfPlaySession], networks: list, encoding: pe.Encoding,
              device: torch.device, gpu_lock, launching, clock: _Clock | None = None):
    """Sessions taking turns, so the CPU and the GPU work at the same time:
    while the GPU evaluates one session's batch, the CPU prepares the next
    one's and feeds the previous results in. Yields each session as its
    batch's results go in, with the batch's size. New batches are launched
    while `launching()`; once it's false, what's in flight is collected and
    it stops, leaving the sessions with nothing pending. `clock`, if given,
    gets the time split (TIMES)."""
    clock = clock if clock is not None else _Clock(dict.fromkeys(TIMES, 0.0))
    pending: list[_Pending | None] = [None] * len(sessions)
    current = 0
    while launching() or any(p is not None for p in pending):
        if pending[current] is None and launching():
            # the CPU work outside the lock, queueing the GPU's inside
            pending[current] = _launch(networks, sessions[current], encoding, device,
                                       gpu_lock, clock)
        other = (current + 1) % len(sessions)
        waiting = pending[other]
        if waiting is not None:
            _collect(sessions[other], encoding, waiting, gpu_lock, clock)
            pending[other] = None
            yield sessions[other], waiting.size
            clock.to("cpu")
        current = other


class SelfPlay:
    """Continuous self-play: games keep going between calls, so a call ends
    as soon as enough games have finished, without waiting for the slowest
    (game lengths are heavy-tailed). A finished game's slot starts a new one
    at once, so batches stay full. Games carry on across calls with whatever
    network is passed next."""

    def __init__(self, encoding: pe.Encoding, device: torch.device,
                 config: pe.SelfPlayConfig, parallel: int = 64, seed: int = 0, gpu_lock=None):
        self.encoding, self.device = encoding, device
        # shared with training when both run at once, around GPU work
        self.gpu_lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
        # effectively endless: games are taken as they finish
        endless = 1 << 30
        self.sessions = [pe.SelfPlaySession(config, parallel, endless, seed * 2 + i)
                         for i in range(2)]
        # running totals over every call (TIMES)
        self.times = dict.fromkeys(TIMES, 0.0)
        # the evaluation cache's lookups and hits, running totals
        self.cache = [0, 0]

    def clear_caches(self):
        """Forget cached evaluations: for a new network."""
        for session in self.sessions:
            session.clear_cache()

    def set_fog_share(self, share: float):
        """The share of games started from now on that have fog."""
        for session in self.sessions:
            session.set_fog_share(share)

    def play(self, model: Network, games: int, progress=None,
             progress_seconds: float = 30.0) -> tuple[list[pe.GameRecord], dict[str, float]]:
        """Plays until at least `games` more have finished (games finishing in
        the same batch all come back), with this network. Returns those
        records, and stats: positions evaluated, batches, seconds."""
        return self._play([model], games, progress, progress_seconds)

    def _play(self, networks: list, games: int, progress=None,
              progress_seconds: float = 30.0) -> tuple[list[pe.GameRecord], dict[str, float]]:
        """play, with one network for every side or one per side."""
        for network in networks:
            network.eval()
        encoding, device, sessions = self.encoding, self.device, self.sessions
        records = []
        evaluated = batches = 0
        start = reported = time.perf_counter()
        clock = _Clock(self.times)
        # once enough games are in, only what's in flight is collected
        for session, size in alternate(sessions, networks, encoding, device, self.gpu_lock,
                                       lambda: len(records) < games, clock):
            evaluated += size
            batches += 1
            lookups, hits = session.cache_stats()
            self.cache[0] += lookups
            self.cache[1] += hits
            records.extend(session.take_finished())
            now = time.perf_counter()
            if progress is not None and now - reported >= progress_seconds:
                reported = now
                progress({"finished": len(records), "games": games,
                          "evaluated": evaluated, "batches": batches,
                          "seconds": now - start})
        return records, {"evaluated": evaluated, "batches": batches,
                         "seconds": time.perf_counter() - start}


class PoolPlay(SelfPlay):
    """Opponent-pool games, continuously (as SelfPlay, two sessions taking
    turns): the current network against an earlier one, at random seats,
    with self-play's settings but no handicaps; only the current network's
    moves get training targets. The opponent can change between calls; games
    in flight carry on against the new one."""

    def __init__(self, encoding: pe.Encoding, device: torch.device,
                 config: pe.SelfPlayConfig, parallel: int = 64, seed: int = 0, gpu_lock=None):
        config = config.copy()
        config.handicap_share = 0.0
        config.sides = ["network", "network"]
        config.random_seats = True
        config.train_sides = [True, False]
        super().__init__(encoding, device, config, parallel, seed, gpu_lock)
        self._networks = None

    def play(self, model: Network, opponent: Network,
             games: int) -> tuple[list[pe.GameRecord], int]:
        """Plays until at least `games` more have finished. Returns those
        records, and the positions evaluated."""
        if self._networks != (id(model), id(opponent)):
            # cached answers came from other networks (or older weights,
            # cleared by the caller's clear_caches)
            self.clear_caches()
            self._networks = (id(model), id(opponent))
        records, stats = self._play([model, opponent], games)
        return records, int(stats["evaluated"])

def play(model: Network, encoding: pe.Encoding, device: torch.device,
         games: int, config: pe.SelfPlayConfig, parallel: int = 64, seed: int = 0,
         progress=None,
         progress_seconds: float = 30.0) -> tuple[list[pe.GameRecord], dict[str, float]]:
    """`games` self-play games from scratch with these settings (see SelfPlay
    for continuous self-play). Returns the records, and stats."""
    runner = SelfPlay(encoding, device, config, parallel=parallel, seed=seed)
    return runner.play(model, games, progress, progress_seconds)
