"""The training loop (docs/stockNavalon.md section 7).

    python -m stocknavalon.run --dir runs/first

1. Bootstrap: self-play with the score evaluator standing in for a network,
   until the replay buffer is full.
2. Then two loops at once, in two threads:
   - self-play keeps `--parallel` games going in each of its two sessions,
     adding each finished game to the buffer, with its own copy of the
     network, refreshed from training every `--sync-steps`;
   - training draws minibatches from the buffer, held to `--reuse` samples
     per position self-play has added, so it neither outruns the data nor
     falls behind it.

Everything lives in the run directory: the games (records.txt), the network
and optimizer (checkpoint.pt), and a log (log.txt). Ctrl-C stops it cleanly;
running again resumes.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import threading
import time

import numpy as np
import torch

import polyengine as pe

from .model import Network, pick_device
from .selfplay import SelfPlay
from .evaluate import match
from .train import Trainer, heldout_losses, score_weight_for


_log_lock = threading.Lock()
# held while a game goes into a record file and a buffer together, and while
# a record file is rewritten, so the two always agree
_records_lock = threading.Lock()


def compact(buffer: pe.ReplayBuffer, path: str):
    """Rewrites a record file to hold exactly its buffer's games: games past
    the buffer are never trained on again, and the checkpoints keep what
    they taught. Written aside and swapped in, so a crash can't leave half a
    file."""
    with _records_lock:
        buffer.save(path + ".tmp")
        os.replace(path + ".tmp", path)


def log(run_dir: str, entry: dict):
    entry = {"time": time.strftime("%H:%M:%S"), **entry}
    with _log_lock:
        print(json.dumps(entry))
        with open(os.path.join(run_dir, "log.txt"), "a") as f:
            f.write(json.dumps(entry) + "\n")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--dir", required=True)
    p.add_argument("--device", default=None)
    p.add_argument("--channels", type=int, default=64)
    p.add_argument("--blocks", type=int, default=6)
    p.add_argument(
        "--buffer", type=int, default=250_000, help="positions kept for training"
    )
    p.add_argument(
        "--threads",
        type=int,
        default=8,
        help="threads for the bootstrap's score-evaluator games",
    )
    p.add_argument("--reuse", type=float, default=9.0,
                   help="training samples per position self-play adds")
    p.add_argument("--warmup-steps", type=int, default=500,
                   help="training steps allowed at the start, on the buffer as it "
                        "is, before new games come in")
    p.add_argument("--sync-steps", type=int, default=200,
                   help="training steps between handing self-play new weights")
    p.add_argument("--checkpoint-minutes", type=float, default=10.0)
    p.add_argument("--log-seconds", type=float, default=60.0)
    p.add_argument("--batch", type=int, default=256)
    p.add_argument("--score-weight", type=float, default=0.1,
                   help="the value target's weight on the score lead, fading to "
                        "0 as captures decide most games")
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--parallel", type=int, default=64,
                   help="self-play games at once, in each of two sessions")
    p.add_argument("--simulations", type=int, default=100)
    p.add_argument("--samples", type=int, default=2)
    p.add_argument("--full-share", type=float, default=0.25,
                   help="share of decisions with the full search, recorded for "
                        "the policy; the rest get a quick search")
    p.add_argument("--quick-simulations", type=int, default=25)
    p.add_argument("--horizon", type=int, default=1)
    p.add_argument("--max-turns", type=int, default=50)
    p.add_argument("--eval-minutes", type=float, default=60.0,
                   help="minutes between evaluation matches (0 for none)")
    p.add_argument("--eval-games", type=int, default=64,
                   help="games per evaluation match, in seat-swapped pairs")
    p.add_argument("--eval-simulations", type=int, default=50)
    p.add_argument("--holdout-every", type=int, default=20,
                   help="one finished self-play game in this many is kept out "
                        "of training, for the held-out loss")
    p.add_argument("--minutes", type=float, default=float("inf"),
                   help="stop after this long")
    args = p.parse_args()

    os.makedirs(args.dir, exist_ok=True)
    records_path = os.path.join(args.dir, "records.txt")
    checkpoint_path = os.path.join(args.dir, "checkpoint.pt")
    device = torch.device(args.device) if args.device else pick_device()
    encoding = pe.Encoding()

    model = Network(encoding, args.channels, args.blocks).to(device)
    trainer = Trainer(model, encoding, device, lr=args.lr)
    iteration = 0
    if os.path.exists(checkpoint_path):
        state = torch.load(checkpoint_path, map_location=device)
        model.load_state_dict(state["model"])
        trainer.optimizer.load_state_dict(state["optimizer"])
        trainer.steps = state["steps"]
        iteration = state["iteration"]

    buffer = pe.ReplayBuffer(args.buffer)
    if os.path.exists(records_path):
        buffer.add(pe.load_records(records_path))
    holdout_path = os.path.join(args.dir, "holdout.txt")
    holdout = pe.ReplayBuffer(50_000)
    if os.path.exists(holdout_path):
        holdout.add(pe.load_records(holdout_path))
    log(
        args.dir,
        {
            "event": "start",
            "device": str(device),
            "iteration": iteration,
            "games": buffer.game_count,
            "positions": buffer.positions,
        },
    )

    session = dict(
        simulations=args.simulations,
        samples=args.samples,
        horizon=args.horizon,
        max_turns=args.max_turns,
        full_share=args.full_share,
        quick_simulations=args.quick_simulations,
    )

    # 1. bootstrap until the buffer is full, in chunks so progress is saved.
    # Full means within a game of the limit: past it, each new game only
    # pushes an old one out.
    seed = 1_000_000 + buffer.game_count

    def per_game():
        return buffer.positions / buffer.game_count if buffer.game_count else 500

    while iteration == 0 and args.buffer - buffer.positions > per_game():
        start = time.perf_counter()
        missing = args.buffer - buffer.positions
        chunk = max(1, min(100, int(missing / per_game())))
        games = pe.play_games(chunk, seed=seed, threads=args.threads, **session)
        seed += chunk
        pe.save_records(games, records_path)
        buffer.add(games)
        log(
            args.dir,
            {
                "event": "bootstrap",
                "games": buffer.game_count,
                "positions": buffer.positions,
                "decided": round(buffer.decided_share, 3),
                "seconds": round(time.perf_counter() - start, 1),
            },
        )
    # 2. self-play and training at once. The record files hold only what the
    # buffers do, trimmed now and at every checkpoint.
    compact(buffer, records_path)
    compact(holdout, holdout_path)
    gpu_lock = threading.Lock()
    trainer.gpu_lock = gpu_lock
    player = SelfPlayThread(args, encoding, device, model, buffer, records_path,
                            gpu_lock, seed=iteration * 7919 + 13, session=session,
                            holdout=holdout, holdout_path=holdout_path)
    player.start()
    evaluator = EvaluationThread(args, encoding, device, gpu_lock)
    evaluator.start()
    last_eval = time.perf_counter() if args.eval_minutes > 0 else float("inf")
    log(args.dir, {"event": "self-play start", "parallel": 2 * args.parallel})

    def checkpoint():
        compact(buffer, records_path)
        compact(holdout, holdout_path)
        torch.save({"model": model.state_dict(),
                    "optimizer": trainer.optimizer.state_dict(),
                    "steps": trainer.steps, "iteration": iteration,
                    "config": vars(args)}, checkpoint_path)

    added_at_start = buffer.added
    samples = 0
    began = last_log = last_checkpoint = time.perf_counter()
    window = {"steps": 0, "seconds_training": 0.0, "losses": {}, "waiting": 0.0}
    weight = score_weight_for(buffer.decided_share, args.score_weight)
    try:
        while time.perf_counter() - began < args.minutes * 60:
            if player.error is not None:
                raise player.error
            if evaluator.error is not None:
                raise evaluator.error
            now = time.perf_counter()
            # the throttle: no more samples than `reuse` per position added,
            # after a warm start on the buffer as it is
            allowed = (args.warmup_steps * args.batch
                       + args.reuse * (buffer.added - added_at_start))
            if samples + args.batch > allowed:
                time.sleep(0.2)
                window["waiting"] += time.perf_counter() - now
            else:
                if trainer.steps % 50 == 0:
                    weight = score_weight_for(buffer.decided_share, args.score_weight)
                for name, value in trainer.step(buffer, args.batch, weight).items():
                    window["losses"][name] = window["losses"].get(name, 0.0) + value
                samples += args.batch
                window["steps"] += 1
                window["seconds_training"] += time.perf_counter() - now
                if trainer.steps % args.sync_steps == 0:
                    player.publish(model)

            now = time.perf_counter()
            if now - last_log >= args.log_seconds:
                entry = status(window, player, buffer, trainer, weight, samples,
                               added_at_start, now - last_log,
                               args.warmup_steps * args.batch)
                entry["heldout"] = heldout_losses(model, encoding, device, holdout,
                                                  256, weight, gpu_lock)
                log(args.dir, entry)
                window = {"steps": 0, "seconds_training": 0.0, "losses": {},
                          "waiting": 0.0}
                last_log = now
            if now - last_eval >= args.eval_minutes * 60:
                evaluator.submit(model, trainer.steps)
                last_eval = now
            if now - last_checkpoint >= args.checkpoint_minutes * 60:
                iteration += 1
                checkpoint()
                last_checkpoint = now
    except KeyboardInterrupt:
        log(args.dir, {"event": "stopping"})
    finally:
        player.stop.set()
        evaluator.stop.set()
        player.join()
        evaluator.join()
        trainer.drain()
        iteration += 1
        checkpoint()
        log(args.dir, {"event": "stopped", "steps": trainer.steps,
                       "games": player.totals["games"]})


def status(window, player, buffer, trainer, weight, samples, added_at_start,
           seconds, warmup) -> dict:
    """One log line about the last `seconds`."""
    steps = window["steps"]
    played = player.take_window()
    added = buffer.added - added_at_start
    return {
        "event": "status",
        "steps": trainer.steps,
        "steps_per_minute": round(60 * steps / seconds, 1),
        "ms_per_step": round(1000 * window["seconds_training"] / steps) if steps else None,
        "training_waited": round(window["waiting"] / seconds, 2),
        "losses": {k: round(v / steps, 4) for k, v in window["losses"].items()} if steps else None,
        "games_per_minute": round(60 * played["games"] / seconds, 1),
        "positions_per_second": round(played["evaluated"] / seconds),
        "decided_recent": round(player.decided_recent(), 3),
        "score_weight": round(weight, 3),
        # samples per new position, past the warm start
        "reuse": round(max(0, samples - warmup) / added, 2) if added else None,
        "buffer_games": buffer.game_count,
    }


class SelfPlayThread(threading.Thread):
    """Self-play, continuously, with its own copy of the network: a model
    being trained can't also be read mid-update, and the two need different
    BatchNorm modes."""

    def __init__(self, args, encoding, device, model, buffer, records_path,
                 gpu_lock, seed, session, holdout, holdout_path):
        super().__init__(daemon=True)
        self.holdout, self.holdout_path = holdout, holdout_path
        self.finished = 0
        self.args, self.encoding, self.device = args, encoding, device
        self.buffer, self.records_path, self.gpu_lock = buffer, records_path, gpu_lock
        self.seed, self.session = seed, session
        self.model = Network(encoding, args.channels, args.blocks).to(device).eval()
        self.model.load_state_dict(model.state_dict())
        self.stop = threading.Event()
        self.error = None
        self._lock = threading.Lock()
        self._weights = None
        self._window = {"games": 0, "evaluated": 0}
        self._decided = collections.deque(maxlen=256)
        self.totals = {"games": 0}

    def publish(self, model: Network):
        """New weights for self-play, picked up between chunks of games."""
        with self.gpu_lock:
            weights = {k: v.detach().to("cpu", copy=True)
                       for k, v in model.state_dict().items()}
        with self._lock:
            self._weights = weights

    def take_window(self) -> dict:
        with self._lock:
            window, self._window = self._window, {"games": 0, "evaluated": 0}
        return window

    def decided_recent(self) -> float:
        with self._lock:
            return float(np.mean(self._decided)) if self._decided else 0.0

    def run(self):
        try:
            runner = SelfPlay(self.encoding, self.device, parallel=self.args.parallel,
                              seed=self.seed, gpu_lock=self.gpu_lock, **self.session)
            while not self.stop.is_set():
                with self._lock:
                    weights, self._weights = self._weights, None
                if weights is not None:
                    with self.gpu_lock:
                        self.model.load_state_dict(weights)
                # a few games at a time, so new weights come in often; the
                # positions evaluated are counted as they go, not only once a
                # game finishes
                counted = [0]

                def progress(p):
                    with self._lock:
                        self._window["evaluated"] += p["evaluated"] - counted[0]
                    counted[0] = p["evaluated"]

                games, stats = runner.play(self.model, 8, progress=progress,
                                           progress_seconds=2.0)
                # one game in `holdout_every` kept out of training
                kept, held = [], []
                for g in games:
                    self.finished += 1
                    every = self.args.holdout_every
                    (held if every and self.finished % every == 0 else kept).append(g)
                with _records_lock:
                    pe.save_records(kept, self.records_path)
                    self.buffer.add(kept)
                    if held:
                        pe.save_records(held, self.holdout_path)
                        self.holdout.add(held)
                with self._lock:
                    self._window["games"] += len(games)
                    self._window["evaluated"] += stats["evaluated"] - counted[0]
                    self._decided.extend(g.winner >= 0 for g in games)
                    self.totals["games"] += len(games)
        except Exception as e:  # handed to the training thread to raise
            self.error = e


class EvaluationThread(threading.Thread):
    """Evaluation matches, every so often: the latest network against the
    random bot, the score-evaluator search, and the network from the last
    evaluation. Each evaluated network is kept in checkpoints/, so later
    matches have earlier ones to play."""

    def __init__(self, args, encoding, device, gpu_lock):
        super().__init__(daemon=True)
        self.args, self.encoding, self.device, self.gpu_lock = args, encoding, device, gpu_lock
        self.stop = threading.Event()
        self.error = None
        self._lock = threading.Lock()
        self._request = None
        self.folder = os.path.join(args.dir, "checkpoints")
        os.makedirs(self.folder, exist_ok=True)

    def network(self, weights) -> Network:
        net = Network(self.encoding, self.args.channels, self.args.blocks).to(self.device)
        with self.gpu_lock:
            net.load_state_dict(weights)
        return net.eval()

    def submit(self, model: Network, steps: int):
        """The network to evaluate next; a request still waiting is replaced."""
        with self.gpu_lock:
            weights = {k: v.detach().to("cpu", copy=True)
                       for k, v in model.state_dict().items()}
        with self._lock:
            self._request = (weights, steps)

    def previous(self):
        snapshots = sorted((int(name[5:-3]), name) for name in os.listdir(self.folder)
                           if name.startswith("step_") and name.endswith(".pt"))
        return snapshots[-1] if snapshots else None

    def run(self):
        try:
            while not self.stop.is_set():
                with self._lock:
                    request, self._request = self._request, None
                if request is None:
                    time.sleep(1.0)
                    continue
                weights, steps = request
                earlier = self.previous()
                torch.save({"model": weights, "steps": steps},
                           os.path.join(self.folder, f"step_{steps}.pt"))
                model = self.network(weights)
                opponents = [("random", "random"), ("score", "score")]
                if earlier is not None:
                    state = torch.load(os.path.join(self.folder, earlier[1]),
                                       map_location="cpu")
                    opponents.append((f"step_{earlier[0]}", self.network(state["model"])))
                for name, opponent in opponents:
                    if self.stop.is_set():
                        return
                    result = match(model, opponent, self.encoding, self.device,
                                   games=self.args.eval_games,
                                   simulations=self.args.eval_simulations,
                                   samples=self.args.samples,
                                   max_turns=self.args.max_turns,
                                   seed=steps, gpu_lock=self.gpu_lock)
                    log(self.args.dir, {"event": "evaluation", "steps": steps,
                                        "against": name, **result})
        except Exception as e:  # handed to the training thread to raise
            self.error = e


if __name__ == "__main__":
    main()
