"""The training loop (docs/stockNavalon.md section 6).

    python -m stocknavalon.run --dir runs/NAME [--init WEIGHTS.pt]

1. Bootstrap: self-play with the score evaluator standing in for a network,
   until the replay buffer is full; or, starting from given weights
   (`--init`), the network's own games from the start.
2. Then three threads:
   - self-play keeps `--parallel` games going in each of its two sessions,
     adding each finished game to the buffer, always with the **best
     network** (best.pt);
   - training draws minibatches from the buffer, held to `--reuse` samples
     per position self-play has added, and keeps an average of its weights
     (EMA);
   - evaluation: every `--gate-steps`, the average plays the best network,
     and replaces it only if it wins (gating, judged on decided games); every
     `--eval-steps`, the best network plays the random bot and the score
     search.

Everything lives in the run directory: the games (records.txt), the network,
its average and optimizer (checkpoint.pt), the best network (best.pt), and a
log (log.txt). Ctrl-C stops it cleanly; running again resumes.
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import json
import os
import sys
import threading
import time
from typing import Any

import numpy as np
import torch

import polyengine as pe

from .model import DTYPES, NEWER_HEADS, Network, load_weights, pick_device
from .selfplay import SelfPlay
from .evaluate import gate_settled, gate_verdict, match
from .fog import FogSchedule
from .train import Trainer, heldout_losses
from .train import learning_rate as learning_rate_at


@dataclasses.dataclass
class Window:
    """Training since the last status line."""
    steps: int = 0
    seconds_training: float = 0.0
    waiting: float = 0.0
    losses: dict[str, float] = dataclasses.field(default_factory=dict)


# the size of the fixed held-out set, and the held-out games it's drawn
# from, one position at a time: from too few, it's a handful of games and
# says little (the first one, drawn as soon as there were 512 positions,
# came from a few early games and swung between 0.7 and 1.3)
HELDOUT_POSITIONS = 512
HELDOUT_MIN_GAMES = 100
# the latest games the logged share decided covers, about 15 to 20 minutes of
# self-play. Long enough to smooth over most of a restart's swing (all games
# start together, so those ending in a capture finish first and the ones
# reaching the turn limit come in a wave later), short enough to follow the
# network.
RECENT_GAMES = 512
# the latest self-play games commands per turn averages over
ACTIVITY_GAMES = 128

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


def log(run_dir: str, entry: dict[str, Any]):
    """An entry for log.txt, as JSON, and for the terminal, readably."""
    entry = {"time": time.strftime("%H:%M:%S"), **entry}
    with _log_lock:
        print(pretty(entry), flush=True)
        with open(os.path.join(run_dir, "log.txt"), "a") as f:
            f.write(json.dumps(entry) + "\n")


_COLOUR = sys.stdout.isatty()


def _style(text: str, code: str) -> str:
    return f"\033[{code}m{text}\033[0m" if _COLOUR else text


def _num(value, digits: int = 0, suffix: str = "") -> str:
    if value is None:
        return "-"
    return f"{value:,.{digits}f}{suffix}"


def pretty(e: dict[str, Any]) -> str:
    """One log entry as a line for the terminal."""
    when = _style(e["time"], "2")
    kind = e["event"]
    if kind == "status":
        losses = e.get("losses") or {}
        held = e.get("heldout") or {}
        parts = [
            (f"train waiting for {_num(e['filling'][1])} positions "
             f"({_num(e['filling'][0])} so far)" if e.get("filling") else
             f"train {_num(e['steps'])} steps ({_num(e['steps_per_minute'])}/min, "
             f"{_num(e['ms_per_step'])} ms, idle {_num(100 * e['training_waited'])}%)"),
            f"play {_num(e['games_per_minute'], 1)} games/min, "
            f"{_num(e['positions_per_second'])} pos/s",
            f"decided {_num(e.get('decided_recent_percent'))}% "
            f"(buffer {_num(e.get('decided_buffer_percent'))}%), "
            f"stalemate {_num(e.get('stalled_recent_percent'))}%",
            f"{_num(e.get('commands_per_turn'), 1)} commands/turn "
            f"(best {_num(e.get('best'))}, fog "
            f"{_num(None if e.get('fog_share') is None else 100 * e['fog_share'])}%)",
            f"reuse {_num(e['reuse'], 1)}, city weight {_num(e.get('city_weight'), 2)}, "
            f"lr {e['learning_rate']:.1e}" if e.get('learning_rate') is not None else
            f"reuse {_num(e['reuse'], 1)}, city weight {_num(e.get('city_weight'), 2)}",
            f"loss {_num(losses.get('total'), 3)} "
            f"(policy KL {_num(losses.get('policy_kl'), 3)}, "
            f"value {_num(losses.get('value'), 3)})",
            f"held-out {_num(held.get('total'), 3)} "
            f"(KL {_num(held.get('policy_kl'), 3)}, value {_num(held.get('value'), 3)})",
        ]
        return f"{when}  {_style('status', '1')}  " + "  |  ".join(parts)
    if kind == "evaluation":
        rate = e.get("win_rate")
        colour = "32" if rate is not None and rate > 0.55 else \
                 "31" if rate is not None and rate < 0.45 else "33"
        win = _style(f"{_num(None if rate is None else 100 * rate)}% won", colour)
        return (f"{when}  {_style('eval', '1;36')}    step {_num(e['steps'])} vs "
                f"{e['against']}: {win} of {e['games']} games  "
                f"(captured {_num(e.get('captured_percent'))}%, "
                f"was captured {_num(e.get('was_captured_percent'))}%, "
                f"{e.get('cut_short', 0)} cut short, "
                f"{_num(e['seconds'] / 60, 1)} min)")
    if kind == "gate":
        colour = {"pass": "32", "fail": "31"}.get(e["verdict"], "33")
        return (f"{when}  {_style('gate', '1;35')}    step {_num(e['steps'])} vs best "
                f"{_num(e['best'])}: {_style(e['verdict'], colour)}, won "
                f"{e['captures']} of {e['captures'] + e['was_captured']} decided "
                f"({e['games']} games, {_num(e['seconds'] / 60, 1)} min)")
    if kind == "evaluation start":
        return (f"{when}  {_style('eval', '1;36')}    step {_num(e['steps'])}: "
                f"{e['games']} games each against {', '.join(e['against'])}")
    if kind == "gate progress":
        decided = e["captures"] + e["was_captured"]
        return (f"{when}  {_style('gate', '35')}    vs {e['against']}: "
                f"{e['games']}/{e['of']} games, won {e['captures']} of {decided} "
                f"decided so far  ({_num(e['seconds'] / 60, 1)} min)")
    if kind == "evaluation progress":
        rate = e.get("win_rate")
        return (f"{when}  {_style('eval', '36')}    vs {e['against']}: "
                f"{e['games']}/{e['of']} games, "
                f"{_num(None if rate is None else 100 * rate)}% won so far  "
                f"({_num(e['seconds'] / 60, 1)} min)")
    if kind == "bootstrap":
        return (f"{when}  {_style('bootstrap', '1')}  {_num(e['games'])} games, "
                f"{_num(e['positions'])} positions, decided "
                f"{_num(e.get('decided_percent'))}%  ({_num(e['seconds'], 1)} s)")
    rest = ", ".join(f"{k} {v}" for k, v in e.items() if k not in ("time", "event"))
    return f"{when}  {_style(kind, '1')}  {rest}"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--dir", required=True)
    p.add_argument("--device", default=None)
    p.add_argument("--channels", type=int, default=64)
    p.add_argument("--blocks", type=int, default=6)
    p.add_argument(
        "--buffer", type=int, default=500_000,
        help="positions kept for training: about 1,300 games of 150 turns; "
             "fewer games are easier to learn by heart"
    )
    p.add_argument(
        "--threads",
        type=int,
        default=8,
        help="threads for the bootstrap's score-evaluator games",
    )
    p.add_argument("--session-threads", type=int, default=4,
                   help="threads for each self-play session's and match's own "
                        "work (advancing games, encoding positions)")
    p.add_argument("--reuse", type=float, default=4.0,
                   help="training samples per position self-play adds")
    p.add_argument("--augment", action=argparse.BooleanOptionalAction, default=True,
                   help="train on each position through a random one of the "
                        "map's 8 rotations and reflections")
    p.add_argument("--per-game", type=int, default=1,
                   help="positions a minibatch takes from each game it samples; "
                        "few, so the value head can't learn games' results by heart")
    p.add_argument("--warmup-steps", type=int, default=500,
                   help="training steps allowed at a new run's start, on the "
                        "bootstrap buffer, before new games come in")
    p.add_argument("--init", default=None,
                   help="a new run's starting weights (a checkpoint or snapshot "
                        ".pt): no score-evaluator bootstrap, the network plays "
                        "its own games from the start")
    p.add_argument("--min-positions", type=int, default=50_000,
                   help="positions in the buffer before training starts, so the "
                        "first batches aren't drawn from a handful of games")
    p.add_argument("--ema-decay", type=float, default=0.999,
                   help="the weights' moving average, which gating plays")
    p.add_argument("--gate-steps", type=int, default=2000,
                   help="training steps between gates: the average against the "
                        "best network, replacing it if it wins")
    p.add_argument("--gate-games", type=int, default=128,
                   help="all played at once")
    p.add_argument("--gate-games-trained", type=float, default=0.25,
                   help="the share of a gate's games added to training, each "
                        "thinned to self-play's --full-share of training "
                        "positions: every move had a full search, with no "
                        "noise, and unthinned they swamped self-play")
    p.add_argument("--gate-max-minutes", type=float, default=30.0,
                   help="a gate's time limit, like --eval-max-minutes; games "
                        "still going then count for neither side")
    p.add_argument("--gate-threshold", type=float, default=0.55,
                   help="share of the decided games the average must win")
    p.add_argument("--gate-min-decided", type=int, default=20,
                   help="decided games a gate needs, else it's inconclusive")
    p.add_argument("--checkpoint-minutes", type=float, default=10.0)
    p.add_argument("--log-seconds", type=float, default=60.0)
    p.add_argument("--batch", type=int, default=256)
    p.add_argument("--lr", type=float, default=2e-4,
                   help="the peak learning rate: warmed up to over the first "
                        "--lr-warmup-steps while the optimizer is fresh, then "
                        "cosine down to --lr-floor over --lr-decay-steps of the "
                        "run, then held")
    p.add_argument("--lr-floor", type=float, default=5e-5)
    p.add_argument("--lr-decay-steps", type=int, default=100_000)
    p.add_argument("--lr-warmup-steps", type=int, default=1000)
    p.add_argument("--city-weight", type=float, default=0.3,
                   help="the value target's weight on the final city lead at "
                        "the start of a run, while true results are scarce; it "
                        "fades to 0 over --city-fade-steps, by steps alone")
    p.add_argument("--city-fade-steps", type=int, default=30_000)
    p.add_argument("--peace-treaties", action=argparse.BooleanOptionalAction,
                   default=False,
                   help="allow peace treaties in training games (off: a bot that "
                        "can't use them yet makes and breaks them every few "
                        "turns, freezing its own army)")
    p.add_argument("--gate-max-turns", type=int, default=75,
                   help="gate games stop here (self-play's limit is --max-turns); "
                        "captures come well before")
    p.add_argument("--cut-off-weight", type=float, default=0.3,
                   help="the value loss weight of positions from games cut off "
                        "by the turn limit or a stalemate (decided games: 1)")
    p.add_argument("--search-value-weight", type=float, default=0.25,
                   help="the share of the value target from each position's own "
                        "search value; the rest is the result (against an "
                        "overconfident value: every position of a decided game "
                        "otherwise has the same ±1 target)")
    p.add_argument("--value-share", type=float, default=0.1,
                   help="the share of samples the value trains on, so each "
                        "game's result is repeated less")
    p.add_argument("--parallel", type=int, default=64,
                   help="self-play games at once, in each of two sessions")
    p.add_argument("--simulations", type=int, default=64)
    p.add_argument("--samples", type=int, default=1)
    p.add_argument("--full-share", type=float, default=0.5,
                   help="share of decisions with the full search, recorded for "
                        "the policy; the rest get a quick search")
    p.add_argument("--quick-simulations", type=int, default=16)
    p.add_argument("--horizon", type=int, default=1)
    p.add_argument("--max-turns", type=int, default=150,
                   help="games stop here at the latest (truncated, not drawn)")
    p.add_argument("--fog-share", type=float, default=None,
                   help="a fixed share of self-play and gate games played with "
                        "fog, the rest with full information; by default the "
                        "fog schedule decides. Anchor matches always have fog")
    p.add_argument("--inference-dtype", choices=sorted(DTYPES), default="float16",
                   help="the precision self-play and matches run the network in "
                        "(training stays float32); measure.py compares them")
    p.add_argument("--network-beliefs", action=argparse.BooleanOptionalAction,
                   default=True,
                   help="with fog, sample the hidden state from the network's "
                        "belief heads (once per turn) instead of fixed priors")
    p.add_argument("--fog-trigger", type=float, default=0.5,
                   help="the fog schedule: the share of the latest games "
                        "decided by a capture that starts bringing fog in")
    p.add_argument("--fog-ramp-steps", type=int, default=25_000,
                   help="the fog schedule: training steps from no fog to all fog")
    p.add_argument("--fog-brake", type=int, default=3,
                   help="the fog schedule: failed gates in a row that pause the ramp")
    p.add_argument("--stall-turns", type=int, default=25,
                   help="once anyone has attacked or captured, a game stops after "
                        "this many turns without either (0 for never)")
    p.add_argument("--eval-steps", type=int, default=5000,
                   help="training steps between the best network's matches "
                        "against the random bot and the score search (0 for "
                        "none); counted from the last, so restarts keep the "
                        "schedule")
    p.add_argument("--eval-games", type=int, default=64,
                   help="games per evaluation match, in seat-swapped pairs")
    p.add_argument("--eval-simulations", type=int, default=50)
    p.add_argument("--eval-max-minutes", type=float, default=10.0,
                   help="once 90% of a match's games are done the rest get 50 "
                        "more moves; past this many minutes they end at once "
                        "(scored by the lead)")
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
    trainer = Trainer(model, encoding, device, lr=args.lr, per_game=args.per_game,
                      augment=args.augment, ema_decay=args.ema_decay,
                      cut_off_weight=args.cut_off_weight, value_share=args.value_share,
                      search_value_weight=args.search_value_weight)
    iteration = 0
    initialized = False
    if os.path.exists(checkpoint_path):
        state = torch.load(checkpoint_path, map_location=device)
        fresh = load_weights(model, state["model"])
        if not fresh:
            trainer.optimizer.load_state_dict(state["optimizer"])
            trainer.fresh_steps = None  # its state goes on: no warm-up
        else:
            # new heads or wider inputs: the optimizer's state no longer
            # fits; it starts over
            log(args.dir, {"event": "network grown", "fresh": sorted({k.split('.')[0] for k in fresh})})
        trainer.steps = state["steps"]
        iteration = state["iteration"]
        initialized = state.get("config", {}).get("init") is not None
        load_weights(trainer.ema, state.get("ema", state["model"]))
        if any(k.startswith(NEWER_HEADS) for k in fresh):
            # the average's new heads would start from other random weights
            trainer.ema.load_state_dict(model.state_dict())
    elif args.init:
        state = torch.load(args.init, map_location=device)
        load_weights(model, state["model"])
        trainer.ema.load_state_dict(model.state_dict())
        trainer.steps = state.get("steps", 0)
        initialized = True

    # The city term fades with this run's training steps alone: nothing it
    # affects can bring it back, so it can't loop
    run_start = (state.get("run_start_steps", trainer.steps)
                 if os.path.exists(checkpoint_path) else trainer.steps)

    def learning_rate() -> float:
        return learning_rate_at(trainer.steps - run_start, trainer.fresh_steps,
                                args.lr, args.lr_floor, args.lr_decay_steps,
                                args.lr_warmup_steps)

    def city_weight() -> float:
        if args.city_fade_steps <= 0:
            return 0.0
        return args.city_weight * max(0.0, 1 - (trainer.steps - run_start) / args.city_fade_steps)

    # how much fog: fixed, or the schedule, which the checkpoint remembers
    fog = FogSchedule(trigger=args.fog_trigger, ramp_steps=args.fog_ramp_steps,
                      brake_failures=args.fog_brake)
    if os.path.exists(checkpoint_path) and "fog_schedule" in state:
        for key in ("start_step", "paused_steps", "failures", "last_step"):
            setattr(fog, key, state["fog_schedule"][key])

    fog_braking = False

    def fog_share() -> float:
        return args.fog_share if args.fog_share is not None else fog.share(trainer.steps)

    # the best network, the only one self-play plays: the starting weights
    # until a gate replaces them
    best_path = os.path.join(args.dir, "best.pt")
    if os.path.exists(best_path):
        best = torch.load(best_path, map_location="cpu")
    else:
        best = {"model": {k: v.detach().to("cpu", copy=True)
                          for k, v in trainer.ema.state_dict().items()},
                "steps": trainer.steps}
        torch.save(best, best_path)

    buffer = pe.ReplayBuffer(args.buffer)
    # the latest games, for the share decided until self-play has its own
    recent: list[pe.GameRecord] = []
    if os.path.exists(records_path):
        recent = pe.load_records(records_path)
        buffer.add(recent)
    holdout_path = os.path.join(args.dir, "holdout.txt")
    holdout = pe.ReplayBuffer(100_000)
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
        stall_turns=args.stall_turns,
        fog_share=fog_share(),
        network_beliefs=args.network_beliefs,
        peace_treaties=args.peace_treaties,
        full_share=args.full_share,
        quick_simulations=args.quick_simulations,
        threads=args.session_threads,
    )

    # 1. bootstrap until the buffer is full, in chunks so progress is saved.
    # Full means within a game of the limit: past it, each new game only
    # pushes an old one out.
    seed = 1_000_000 + buffer.game_count

    def per_game():
        return buffer.positions / buffer.game_count if buffer.game_count else 500

    while (iteration == 0 and not initialized
           and args.buffer - buffer.positions > per_game()):
        start = time.perf_counter()
        missing = args.buffer - buffer.positions
        chunk = max(1, min(100, int(missing / per_game())))
        # the score evaluator has no belief heads to ask
        games = pe.play_games(chunk, seed=seed, threads=args.threads,
                              **{k: v for k, v in session.items()
                                 if k != "network_beliefs"})
        seed += chunk
        pe.save_records(games, records_path)
        buffer.add(games)
        recent = (recent + games)[-RECENT_GAMES:]
        log(
            args.dir,
            {
                "event": "bootstrap",
                "games": buffer.game_count,
                "positions": buffer.positions,
                "decided_percent": round(100 * buffer.decided_share, 1),
                "seconds": round(time.perf_counter() - start, 1),
            },
        )
    # 2. self-play and training at once. The record files hold only what the
    # buffers do, trimmed now and at every checkpoint.
    compact(buffer, records_path)
    compact(holdout, holdout_path)
    gpu_lock = threading.Lock()
    trainer.gpu_lock = gpu_lock
    player = SelfPlayThread(args, encoding, device, best["model"], buffer,
                            records_path, gpu_lock, seed=iteration * 7919 + 13,
                            session=session, holdout=holdout,
                            holdout_path=holdout_path, recent=recent)
    del recent
    player.best_steps = best["steps"]
    player.start()
    player.fog_share = fog_share()
    evaluator = EvaluationThread(args, encoding, device, gpu_lock, best, best_path,
                                 on_new_best=player.publish, fog_share=fog_share,
                                 on_gate=fog.gate, on_games=player.add_games)
    evaluator.start()
    # Anchor matches by training progress: every eval-steps since the last,
    # so a restart keeps the schedule; a run without any plays them at once,
    # for a baseline. Gates fall on fixed steps, every multiple of
    # gate-steps, so restarts neither shift nor delay them.
    previous = evaluator.previous()
    last_eval_steps = previous[0] if previous is not None else None
    last_gate_block = trainer.steps // args.gate_steps if args.gate_steps > 0 else 0
    log(args.dir, {"event": "self-play start", "parallel": 2 * args.parallel,
                   "best": best["steps"]})

    def checkpoint():
        compact(buffer, records_path)
        compact(holdout, holdout_path)
        torch.save({"model": model.state_dict(), "ema": trainer.ema.state_dict(),
                    "optimizer": trainer.optimizer.state_dict(),
                    "fog_schedule": fog.state(), "run_start_steps": run_start,
                    "steps": trainer.steps, "iteration": iteration,
                    "config": vars(args)}, checkpoint_path)

    added_at_start = buffer.added
    samples = 0
    # the warm start is for a new run only: a resumed one has trained on
    # this buffer already, and another 500 steps at every restart would
    # only help it learn the games by heart
    warmup = (args.warmup_steps * args.batch
              if trainer.steps == 0 and not initialized else 0)
    began = last_log = last_checkpoint = time.perf_counter()
    window = Window()
    heldout_set_path = os.path.join(args.dir, "heldout.npz")
    heldout_set = None
    if os.path.exists(heldout_set_path):
        with np.load(heldout_set_path) as saved:
            heldout_set = dict(saved)
        # drawn with an older target layout, or from too few games: draw it
        # again (a new baseline)
        reason = ("the target layout changed"
                  if heldout_set["targets"].shape[1] != encoding.target_count
                  else "the input layout changed"
                  if heldout_set["globals"].shape[1] != encoding.global_count
                  else "drawn from too few games" if "games" not in heldout_set
                  else None)
        if reason:
            heldout_set = None
            log(args.dir, {"event": "held-out set redrawn", "reason": reason})
        else:
            heldout_set.pop("games")
    try:
        while time.perf_counter() - began < args.minutes * 60:
            if player.error is not None:
                raise player.error
            if evaluator.error is not None:
                raise evaluator.error
            now = time.perf_counter()
            # the throttle: no more samples than `reuse` per position added,
            # after a new run's warm start on the bootstrap buffer
            allowed = warmup + args.reuse * (buffer.added - added_at_start)
            if samples + args.batch > allowed or buffer.positions < args.min_positions:
                time.sleep(0.2)
                window.waiting += time.perf_counter() - now
            else:
                trainer.set_learning_rate(learning_rate())
                for name, value in trainer.step(buffer, args.batch, city_weight()).items():
                    window.losses[name] = window.losses.get(name, 0.0) + value
                samples += args.batch
                window.steps += 1
                window.seconds_training += time.perf_counter() - now

            # the fog schedule moves with training; self-play picks up the
            # share for the games it starts next
            if args.fog_share is None:
                if fog.update(trainer.steps, player.decided_recent(),
                              player.decided_window_full()) == "start":
                    log(args.dir, {"event": "fog ramp start", "steps": trainer.steps,
                                   "decided_percent": round(100 * player.decided_recent(), 1),
                                   "ramp_steps": fog.ramp_steps})
                # gate results arrive from the evaluation thread at any time,
                # so the brake's state is compared with the last one seen
                if fog.braking(trainer.steps) != fog_braking:
                    fog_braking = not fog_braking
                    log(args.dir, {"event": "fog ramp paused" if fog_braking
                                   else "fog ramp resumed", "steps": trainer.steps,
                                   "fog_share": round(fog.share(trainer.steps), 3)})
            player.fog_share = fog_share()

            now = time.perf_counter()
            # no status lines while evaluation reports its progress; the next
            # one covers the whole stretch
            if now - last_log >= args.log_seconds and not evaluator.busy.is_set():
                entry = status(window, player, buffer, trainer, samples,
                               added_at_start, now - last_log, warmup, city_weight())
                # why training is idle, before it has enough to train on
                if buffer.positions < args.min_positions:
                    entry["filling"] = [buffer.positions, args.min_positions]
                # a fixed held-out set, chosen once there are enough games and
                # saved, so the numbers stay comparable across restarts
                if heldout_set is None and holdout.game_count >= HELDOUT_MIN_GAMES:
                    # wins only and every weight 1: it measures the real
                    # objective, whatever the training targets are blended
                    # with; one position per game sampled, across all of them
                    heldout_set = holdout.sample_examples(
                        encoding, HELDOUT_POSITIONS, 0, per_game=1)
                    np.savez_compressed(heldout_set_path, **heldout_set,
                                        games=np.array(holdout.game_count))
                    log(args.dir, {"event": "held-out set drawn",
                                   "games": holdout.game_count})
                entry["heldout"] = heldout_losses(model, device, heldout_set, gpu_lock)
                log(args.dir, entry)
                window = Window()
                last_log = now
            if args.gate_steps > 0 and trainer.steps // args.gate_steps > last_gate_block:
                evaluator.submit_gate(trainer.ema, trainer.steps)
                last_gate_block = trainer.steps // args.gate_steps
            if args.eval_steps > 0 and (
                    last_eval_steps is None
                    or trainer.steps - last_eval_steps >= args.eval_steps):
                evaluator.submit_anchors(trainer.steps)
                last_eval_steps = trainer.steps
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


def status(window: Window, player: "SelfPlayThread", buffer: pe.ReplayBuffer,
           trainer: Trainer, samples: int, added_at_start: int,
           seconds: float, warmup: int, city_weight: float) -> dict[str, Any]:
    """One log line about the last `seconds`."""
    steps = window.steps
    played = player.take_window()
    added = buffer.added - added_at_start
    return {
        "event": "status",
        "steps": trainer.steps,
        "steps_per_minute": round(60 * steps / seconds, 1),
        "ms_per_step": round(1000 * window.seconds_training / steps) if steps else None,
        "training_waited": round(window.waiting / seconds, 2),
        "losses": {k: round(v / steps, 4) for k, v in window.losses.items()} if steps else None,
        "games_per_minute": round(60 * played["games"] / seconds, 1),
        "positions_per_second": round(played["evaluated"] / seconds),
        # games ending in a capture rather than the turn limit: the latest
        # games, and the whole buffer
        "decided_recent_percent": round(100 * player.decided_recent(), 1),
        "decided_buffer_percent": round(100 * buffer.decided_share, 1),
        # ended by the stalemate cutoff, of this session's latest games
        "stalled_recent_percent": (None if player.stalled_recent() is None
                                   else round(100 * player.stalled_recent(), 1)),
        "commands_per_turn": (None if player.commands_per_turn() is None
                              else round(player.commands_per_turn(), 2)),
        # samples per new position, past the warm start
        "reuse": round(max(0, samples - warmup) / added, 2) if added else None,
        "buffer_games": buffer.game_count,
        # the network self-play plays, and the share of its games with fog
        "best": player.best_steps,
        "city_weight": round(city_weight, 3),
        "learning_rate": trainer.optimizer.param_groups[0]["lr"],
        "fog_share": round(player.fog_share, 3),
    }


class SelfPlayThread(threading.Thread):
    """Self-play, continuously, with the best network only: its own copy,
    replaced when a gate passes (`publish`), so a bad stretch of training
    never reaches the games."""

    def __init__(self, args, encoding, device, weights, buffer, records_path,
                 gpu_lock, seed, session, holdout, holdout_path, recent):
        super().__init__(daemon=True)
        self.holdout, self.holdout_path = holdout, holdout_path
        self.finished = 0
        self.args, self.encoding, self.device = args, encoding, device
        self.buffer, self.records_path, self.gpu_lock = buffer, records_path, gpu_lock
        self.seed, self.session = seed, session
        self.model = Network(encoding, args.channels, args.blocks).to(
            device, DTYPES[args.inference_dtype]).eval()
        load_weights(self.model, weights)
        # the training step the network playing comes from
        self.best_steps: int | None = None
        # the share of games to start with fog, set by the training loop
        self.fog_share = 1.0
        self.stop = threading.Event()
        self.error = None
        self._lock = threading.Lock()
        self._weights = None
        self._window = {"games": 0, "evaluated": 0}
        # whether each of the latest games ended in a capture, starting from
        # the run's games so far
        self._decided = collections.deque((g.winner >= 0 for g in recent),
                                          maxlen=RECENT_GAMES)
        # commands per player turn in the latest games of this session only:
        # how active the current network plays, the first thing to fall when
        # it turns passive
        self._activity = collections.deque(maxlen=ACTIVITY_GAMES)
        # whether each of the latest games ended in a stalemate
        self._stalled = collections.deque(maxlen=RECENT_GAMES)
        self.totals = {"games": 0}

    def publish(self, weights: dict, steps: int):
        """A new best network, picked up between chunks of games."""
        with self._lock:
            self._weights = weights
            self.best_steps = steps

    def take_window(self) -> dict[str, int]:
        with self._lock:
            window, self._window = self._window, {"games": 0, "evaluated": 0}
        return window

    def decided_recent(self) -> float:
        with self._lock:
            return float(np.mean(self._decided)) if self._decided else 0.0

    def add_games(self, games: list[pe.GameRecord]):
        """Games played elsewhere (a gate's) for training: into the buffer and
        the record file, but not the held-out set or self-play's measures."""
        with _records_lock:
            pe.save_records(games, self.records_path)
            self.buffer.add(games)

    def decided_window_full(self) -> bool:
        with self._lock:
            return len(self._decided) == self._decided.maxlen

    def stalled_recent(self) -> float | None:
        with self._lock:
            return float(np.mean(self._stalled)) if self._stalled else None

    def commands_per_turn(self) -> float | None:
        with self._lock:
            return float(np.mean(self._activity)) if self._activity else None

    def run(self):
        try:
            runner = SelfPlay(self.encoding, self.device, parallel=self.args.parallel,
                              seed=self.seed, gpu_lock=self.gpu_lock, **self.session)
            while not self.stop.is_set():
                with self._lock:
                    weights, self._weights = self._weights, None
                if weights is not None:
                    with self.gpu_lock:
                        load_weights(self.model, weights)
                runner.set_fog_share(self.fog_share)
                # a few games at a time, so new weights come in often; the
                # positions evaluated are counted as they go, not only once a
                # game finishes
                counted = [0]

                def progress(p):
                    with self._lock:
                        self._window["evaluated"] += int(p["evaluated"]) - counted[0]
                    counted[0] = int(p["evaluated"])

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
                    self._window["evaluated"] += int(stats["evaluated"]) - counted[0]
                    self._decided.extend(g.winner >= 0 for g in games)
                    self._stalled.extend(g.stalled for g in games)
                    # every seat plays each turn
                    self._activity.extend(
                        g.move_count / max(1, len(g.tribes) * g.turns) for g in games)
                    self.totals["games"] += len(games)
        except Exception as e:  # handed to the training thread to raise
            self.error = e


class EvaluationThread(threading.Thread):
    """Matches, one at a time:
    - **gates:** the weights' average against the best network; if it wins
      (on decided games, `gate_verdict`), it becomes the best, saved to
      best.pt and handed to self-play;
    - **anchors:** the best network against the random bot, the score search
      and the best network last anchored. Each anchored network is kept in
      checkpoints/, so later matches have earlier ones to play."""

    def __init__(self, args, encoding, device, gpu_lock, best, best_path, on_new_best,
                 fog_share, on_gate, on_games):
        super().__init__(daemon=True)
        self.args, self.encoding, self.device, self.gpu_lock = args, encoding, device, gpu_lock
        self.best, self.best_path, self.on_new_best = best, best_path, on_new_best
        # gates play with the current fog share, and report to the schedule
        self.fog_share, self.on_gate = fog_share, on_gate
        # some of a gate's games go to training (its compute isn't all lost),
        # thinned to self-play's density
        self.on_games = on_games
        self.stop = threading.Event()
        # set while anchor matches are being played
        self.busy = threading.Event()
        self.error = None
        self._lock = threading.Lock()
        self._gate = None
        self._anchors = None
        self.folder = os.path.join(args.dir, "checkpoints")
        os.makedirs(self.folder, exist_ok=True)

    def network(self, weights) -> Network:
        net = Network(self.encoding, self.args.channels, self.args.blocks).to(
            self.device, DTYPES[self.args.inference_dtype])
        with self.gpu_lock:
            load_weights(net, weights)
        return net.eval()

    def submit_gate(self, model: Network, steps: int):
        """A candidate to gate; one still waiting is replaced."""
        with self.gpu_lock:
            weights = {k: v.detach().to("cpu", copy=True)
                       for k, v in model.state_dict().items()}
        with self._lock:
            self._gate = (weights, steps)

    def submit_anchors(self, steps: int):
        """Anchor matches for the best network, as of training step `steps`."""
        with self._lock:
            self._anchors = steps

    def previous(self):
        snapshots = sorted((int(name[5:-3]), name) for name in os.listdir(self.folder)
                           if name.startswith("step_") and name.endswith(".pt"))
        return snapshots[-1] if snapshots else None

    def _match(self, model, opponent, name, steps, games, fog_share=1.0, gate=False,
               keep=None):
        def report(p):
            log(self.args.dir, {"event": "gate progress" if gate else "evaluation progress",
                                "steps": steps, "against": name, **p})

        # A gate plays all its games at once, to a shorter turn limit (its
        # captures come well before), with its own time limit, and stops as
        # soon as its verdict can't change: it has to keep up with training.
        # Matches use the training rules (peace treaties or not).
        return match(model, opponent, self.encoding, self.device, games=games,
                     parallel=games if gate else 32,
                     simulations=self.args.eval_simulations,
                     samples=self.args.samples,
                     max_turns=self.args.gate_max_turns if gate else self.args.max_turns,
                     stall_turns=self.args.stall_turns, fog_share=fog_share,
                     # the belief heads only train once there's fog in
                     # training: until then they'd be untrained nonsense, so
                     # fogged matches use the fixed priors
                     network_beliefs=self.args.network_beliefs and self.fog_share() > 0,
                     peace_treaties=self.args.peace_treaties,
                     threads=self.args.session_threads,
                     settled=(gate_settled(self.args.gate_threshold,
                                           self.args.gate_min_decided) if gate else None),
                     seed=steps, gpu_lock=self.gpu_lock, progress=report,
                     progress_seconds=120 if gate else 30,
                     max_minutes=(self.args.gate_max_minutes if gate
                                  else self.args.eval_max_minutes),
                     keep=keep)

    def _run_gate(self, weights, steps):
        best_steps = self.best["steps"]
        log(self.args.dir, {"event": "gate start", "steps": steps, "best": best_steps,
                            "games": self.args.gate_games})
        games: list[pe.GameRecord] = []
        result = self._match(self.network(weights), self.network(self.best["model"]),
                             f"best {best_steps}", steps, self.args.gate_games,
                             fog_share=self.fog_share(), gate=True, keep=games)
        # a share of them, at self-play's density of training positions
        rng = np.random.default_rng(steps)
        kept = [games[i] for i in sorted(rng.choice(
            len(games), size=round(self.args.gate_games_trained * len(games)),
            replace=False))] if games else []
        kept = pe.thin_policy_targets(kept, self.args.full_share, steps)
        self.on_games(kept)
        result["games_trained_on"] = len(kept)
        verdict = gate_verdict(result, self.args.gate_threshold,
                               self.args.gate_min_decided)
        self.on_gate(verdict)
        log(self.args.dir, {"event": "gate", "steps": steps, "best": best_steps,
                            "verdict": verdict, **result})
        if verdict == "pass":
            self.best = {"model": weights, "steps": steps}
            torch.save(self.best, self.best_path + ".tmp")
            os.replace(self.best_path + ".tmp", self.best_path)
            self.on_new_best(weights, steps)

    def _run_anchors(self):
        steps = self.best["steps"]
        earlier = self.previous()
        if earlier is not None and earlier[0] == steps:
            return  # this best network has had its anchors
        torch.save(self.best, os.path.join(self.folder, f"step_{steps}.pt"))
        model = self.network(self.best["model"])
        opponents: list[tuple[str, str | Network]] = [
            ("random", "random"), ("score", "score")]
        if earlier is not None:
            state = torch.load(os.path.join(self.folder, earlier[1]), map_location="cpu")
            opponents.append((f"step_{earlier[0]}", self.network(state["model"])))
        log(self.args.dir, {"event": "evaluation start", "steps": steps,
                            "against": [name for name, _ in opponents],
                            "games": self.args.eval_games})
        for name, opponent in opponents:
            if self.stop.is_set():
                return
            result = self._match(model, opponent, name, steps, self.args.eval_games)
            log(self.args.dir, {"event": "evaluation", "steps": steps,
                                "against": name, **result})

    def run(self):
        try:
            while not self.stop.is_set():
                # anchors first: they're skipped at once unless the best
                # network changed, and gates come often enough that putting
                # them first starved the anchors entirely
                with self._lock:
                    anchors, self._anchors = self._anchors, None
                    gate = None if anchors is not None else self._gate
                    if gate is not None:
                        self._gate = None
                if gate is None and anchors is None:
                    time.sleep(1.0)
                    continue
                # status lines pause during anchor matches, but not during a
                # gate
                if gate is not None:
                    self._run_gate(*gate)
                else:
                    self.busy.set()
                    self._run_anchors()
                    self.busy.clear()
        except Exception as e:  # handed to the training thread to raise
            self.error = e


if __name__ == "__main__":
    main()
