"""Self-play's games, by a worker (docs/stockNavalon.md section 6): the best
network against itself and against earlier bests (the opponent pool), and
the measures of those games. A run starts it in a process of its own
(run.py's SelfPlayThread, --self-play-process), so self-play's Python
doesn't share the interpreter lock or the cores with training, or as a
thread of the run, as before.

The two sides talk through two queues (multiprocessing's, or the standard
library's for a thread):
- to the worker: ("best",) reload best.pt, the best network having changed;
  ("fog", share) the share of games to start with fog; ("stop",).
- from it: ("progress", positions evaluated since the last report);
  ("games", kept, held, games, evaluated): finished games as text
  (polyengine.records_to_text), those for training and those held out;
  ("measures", {...}) after each batch of games; ("error", traceback).

Nothing on the GPU crosses between processes: the worker loads the best
network from the run's best.pt itself.
"""

from __future__ import annotations

import argparse
import collections
import os
import queue
import traceback

import numpy as np
import torch

import polyengine as pe

from .model import DTYPES, Network, load_weights, pick_device
from .selfplay import PoolPlay, SelfPlay
from .settings import search_config

# the latest games the logged share decided covers, about 15 to 20 minutes of
# self-play. Long enough to smooth over most of a restart's swing (all games
# start together, so those ending in a capture finish first and the ones
# reaching the turn limit come in a wave later), short enough to follow the
# network.
RECENT_GAMES = 512
# the latest self-play games commands per turn averages over
ACTIVITY_GAMES = 128
# the latest results kept against each pool network, for prioritized draws
POOL_RESULTS = 50


def self_play_settings(args: argparse.Namespace, fog_share: float) -> pe.SelfPlayConfig:
    """Self-play's settings from the run's flags (also the bootstrap's, with
    the score evaluator, and the opponent pool's)."""
    return pe.self_play_config(
        search=search_config(args, root_noise=True),
        max_turns=args.max_turns,
        stall_turns=args.stall_turns,
        fog_share=fog_share,
        network_beliefs=args.network_beliefs,
        peace_treaties=args.peace_treaties,
        full_share=args.full_share,
        quick_simulations=args.quick_simulations,
        threads=args.session_threads,
        handicap_share=args.handicap_share,
        handicap_stars=args.handicap_stars,
        handicap_income_share=args.handicap_income_share,
        handicap_income=args.handicap_income,
        handicap_catch_up_share=args.handicap_catch_up_share,
        catch_up_from_turn=args.catch_up_from_turn,
        catch_up_below=args.catch_up_below,
        trailing_search_factor=args.trailing_search_factor,
        explore_candidates=args.explore_candidates,
    )


def snapshot_steps(path: str) -> int:
    """The training steps a snapshot (step_N.pt) comes from."""
    return int(os.path.basename(path)[5:-3])


def pool_snapshots(run_dir: str, size: int, best_steps: int | None) -> list[str]:
    """The opponent pool: the latest `size` earlier best networks, from the
    snapshots kept at each promotion (pool/) and the anchored ones
    (checkpoints/), leaving out the current best."""
    found = {}
    for folder in ("checkpoints", "pool"):
        path = os.path.join(run_dir, folder)
        if not os.path.isdir(path):
            continue
        for name in os.listdir(path):
            if name.startswith("step_") and name.endswith(".pt"):
                found[snapshot_steps(name)] = os.path.join(path, name)
    steps = sorted(s for s in found if s != best_steps)[-size:] if size > 0 else []
    return [found[s] for s in steps]


class Worker:
    """Self-play, continuously, with the best network only: its own copy,
    reloaded from best.pt when the run says it changed, so a bad stretch of
    training never reaches the games."""

    def __init__(self, args: argparse.Namespace, seed: int, decided: list[bool],
                 gpu_lock=None):
        self.args, self.seed = args, seed
        self.device = torch.device(args.device) if args.device else pick_device()
        self.encoding = pe.Encoding()
        self.gpu_lock = gpu_lock
        self.best_path = os.path.join(args.dir, "best.pt")
        self.model = self._network()
        self.best_steps = self._load_best()
        self.fog_share = 1.0
        self.finished = 0
        # whether each of the latest games ended in a capture, starting from
        # the run's games so far
        self._decided = collections.deque(decided, maxlen=RECENT_GAMES)
        # commands per player turn in the latest games of this worker only:
        # how active the current network plays, the first thing to fall when
        # it turns passive
        self._activity = collections.deque(maxlen=ACTIVITY_GAMES)
        # whether each of the latest games ended in a stalemate
        self._stalled = collections.deque(maxlen=RECENT_GAMES)
        # handicap games, apart: who won each, the side with the head start
        # ("leader"), the other ("trailing"), or nobody
        self._handicap = collections.deque(maxlen=RECENT_GAMES)
        # opponent-pool games: how the current network did in each (1 won,
        # 0 lost, 0.5 neither), and the games played of each kind
        self._pool = collections.deque(maxlen=RECENT_GAMES)
        self._played = {"self": 0, "pool": 0}
        # pool networks, loaded once each, by snapshot path
        self._opponents: dict[str, Network] = {}
        # the current network's latest results against each pool network, by
        # path (1 won, 0 lost, 0.5 neither): what prioritized draws use
        self._against: dict[str, collections.deque] = {}
        self._pool_rng = np.random.default_rng(seed)
        self.games = 0

    def _network(self) -> Network:
        return Network(self.encoding, self.args.channels, self.args.blocks).to(
            self.device, DTYPES[self.args.inference_dtype]).eval()

    def _load_best(self) -> int:
        """The best network's weights, into the playing copy; its steps."""
        best = torch.load(self.best_path, map_location="cpu")
        load_weights(self.model, best["model"])
        return best["steps"]

    def _win_rate(self, path: str) -> float:
        """The current network's recent win rate against a pool network,
        smoothed toward 50% (one virtual win and one loss), so one it hasn't
        played counts as even."""
        results = self._against.get(path, ())
        return (sum(results) + 1) / (len(results) + 2)

    def _pool_weights(self, paths: list[str]) -> np.ndarray:
        """How likely each pool network is to be drawn: uniform, or with
        prioritized fictitious self-play (AlphaStar) by how often it beats the
        current network, (1 - win rate)^2, so practice goes where the current
        network is weakest."""
        if self.args.pool_weighting == "uniform":
            weights = np.ones(len(paths))
        else:
            weights = np.array([(1 - self._win_rate(p)) ** 2 for p in paths])
        return weights / weights.sum()

    def _opponent(self) -> tuple[str, Network] | None:
        """A pool network to play (`--pool-weighting`), and its path; None
        with an empty pool."""
        paths = pool_snapshots(self.args.dir, self.args.pool_size, self.best_steps)
        for path in list(self._opponents):
            if path not in paths:
                del self._opponents[path]
        if not paths:
            return None
        path = paths[self._pool_rng.choice(len(paths), p=self._pool_weights(paths))]
        if path not in self._opponents:
            net = self._network()
            load_weights(net, torch.load(path, map_location="cpu")["model"])
            self._opponents[path] = net
        return path, self._opponents[path]

    def measures(self, runner: SelfPlay) -> dict:
        """The games' measures for the status line, as the run shows them."""
        out = {"decided": float(np.mean(self._decided)) if self._decided else 0.0,
               "decided_full": len(self._decided) == self._decided.maxlen,
               "stalled": float(np.mean(self._stalled)) if self._stalled else None,
               "activity": float(np.mean(self._activity)) if self._activity else None,
               "handicap": None, "pool": None,
               "times": dict(runner.times), "cache": list(runner.cache),
               "games": self.games}
        if self._handicap:
            n = len(self._handicap)
            out["handicap"] = {"games": n,
                               "leader_won": round(self._handicap.count("leader") / n, 3),
                               "trailing_won": round(self._handicap.count("trailing") / n, 3)}
        if self._pool:
            pool = {"games": len(self._pool),
                    "current_won": round(float(np.mean(self._pool)), 3)}
            rates = {path: self._win_rate(path) for path in self._against}
            if rates:
                path = min(rates, key=rates.get)
                pool["hardest"] = {"steps": snapshot_steps(path),
                                   "current_won": round(rates[path], 3)}
            out["pool"] = pool
        return out

    def _play_pool(self, runner: PoolPlay, outbox) -> bool:
        """A few opponent-pool games, if they're due (`pool_share` of all
        games) and the pool has any. Returns whether it played."""
        share = self.args.pool_share
        total = self._played["self"] + self._played["pool"]
        if share <= 0 or self._played["pool"] >= share * total:
            return False
        drawn = self._opponent()
        if drawn is None:
            return False
        path, opponent = drawn
        games, evaluated = runner.play(self.model, opponent, 8)
        # for training, but not the held-out set or self-play's measures
        outbox.put(("games", pe.records_to_text(games), "", len(games), evaluated))
        results = self._against.setdefault(path, collections.deque(maxlen=POOL_RESULTS))
        for g in games:
            current = 1 - g.pool_opponent_seat
            result = 0.5 if g.winner < 0 else float(g.winner == current)
            self._pool.append(result)
            # games in flight when the opponent changed count for the new
            # one: few, and only at the switch
            results.append(result)
        self._played["pool"] += len(games)
        self.games += len(games)
        return True

    def _read(self, inbox) -> tuple[bool, bool]:
        """The run's messages so far: whether to stop, and whether the best
        network changed."""
        stop = changed = False
        while True:
            try:
                message = inbox.get_nowait()
            except queue.Empty:
                return stop, changed
            if message[0] == "stop":
                stop = True
            elif message[0] == "best":
                changed = True
            elif message[0] == "fog":
                self.fog_share = message[1]

    def run(self, inbox, outbox):
        """Games until told to stop."""
        config = self_play_settings(self.args, self.fog_share)
        runner = SelfPlay(self.encoding, self.device, config, parallel=self.args.parallel,
                          seed=self.seed, gpu_lock=self.gpu_lock)
        pool = PoolPlay(self.encoding, self.device, config, parallel=self.args.parallel,
                        seed=self.seed + 1,
                        gpu_lock=self.gpu_lock) if self.args.pool_share > 0 else None
        while True:
            stop, changed = self._read(inbox)
            if stop:
                return
            if changed:
                self.best_steps = self._load_best()
                # answers from the old network no longer hold
                runner.clear_caches()
                if pool is not None:
                    pool.clear_caches()
            runner.set_fog_share(self.fog_share)
            if pool is not None:
                pool.set_fog_share(self.fog_share)
                if self._play_pool(pool, outbox):
                    outbox.put(("measures", self.measures(runner)))
                    continue
            # a few games at a time, so a new best comes in often; the
            # positions evaluated are reported as they go, not only once a
            # game finishes
            counted = [0]

            def progress(p):
                outbox.put(("progress", int(p["evaluated"]) - counted[0]))
                counted[0] = int(p["evaluated"])

            games, stats = runner.play(self.model, 8, progress=progress, progress_seconds=2.0)
            # one game in `holdout_every` kept out of training; handicap
            # games train, but aren't held out
            kept, held = [], []
            ordinary = [g for g in games if g.handicap_leader < 0 and not g.catch_up]
            for g in games:
                self.finished += 1
                every = self.args.holdout_every
                hold = (every and self.finished % every == 0 and g.handicap_leader < 0
                        and not g.catch_up)
                (held if hold else kept).append(g)
            outbox.put(("games", pe.records_to_text(kept), pe.records_to_text(held),
                        len(games), int(stats["evaluated"]) - counted[0]))
            self._decided.extend(g.winner >= 0 for g in ordinary)
            self._stalled.extend(g.stalled for g in ordinary)
            # every seat plays each turn
            self._activity.extend(
                g.move_count / max(1, len(g.tribes) * g.turns) for g in ordinary)
            for g in games:
                leader = g.handicap_leader
                if leader >= 0:
                    self._handicap.append("nobody" if g.winner < 0 else
                                          "leader" if g.winner == leader else "trailing")
            self.games += len(games)
            self._played["self"] += len(games)
            outbox.put(("measures", self.measures(runner)))


def main(args: argparse.Namespace, seed: int, decided: list[bool], inbox, outbox,
         gpu_lock=None, threads: int | None = None):
    """A worker's whole life, in its own process (or a thread with the run's
    `gpu_lock`): games until told to stop; an error goes to the run as its
    traceback."""
    try:
        if threads is not None:
            # the network runs on the GPU: a few CPU threads are plenty, and
            # more would compete with the run's own
            torch.set_num_threads(threads)
        Worker(args, seed, decided, gpu_lock).run(inbox, outbox)
    except KeyboardInterrupt:
        # Ctrl-C reaches every process: the run stops it anyway
        pass
    except Exception:
        outbox.put(("error", traceback.format_exc()))
