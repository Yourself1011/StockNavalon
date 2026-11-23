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
import dataclasses
import json
import multiprocessing
import os
import queue
import subprocess
import sys
import threading
import time
from typing import Any

import numpy as np
import torch

import polyengine as pe

from .model import DTYPES, NEWER_HEADS, Network, load_weights, pick_device
from . import worker
from .worker import RECENT_GAMES, pool_snapshots, self_play_settings, snapshot_steps
from .settings import add_search_arguments, search_config
from .evaluate import gate_settled, gate_verdict, match
from . import crazy
from .fog import FogSchedule
from .rating import run_ratings
from .train import Trainer, heldout_losses, reanalyse
from .train import learning_rate as learning_rate_at


@dataclasses.dataclass
class Window:
    """Training since the last status line."""
    steps: int = 0
    seconds_training: float = 0.0
    waiting: float = 0.0
    losses: dict[str, float] = dataclasses.field(default_factory=dict)
    # steps skipped for a loss that wasn't finite
    skipped: int = 0


# the size of the fixed held-out set, and the held-out games it's drawn
# from, one position at a time: from too few, it's a handful of games and
# says little (the first one, drawn as soon as there were 512 positions,
# came from a few early games and swung between 0.7 and 1.3)
HELDOUT_POSITIONS = 512
HELDOUT_MIN_GAMES = 100
# the self-play worker process's own CPU threads for torch: its network runs
# on the GPU, so a few are plenty, and more would compete with the run's
WORKER_TORCH_THREADS = 2
# how long a stopping run waits for the self-play worker to finish its batch
WORKER_STOP_SECONDS = 30
# with the lambda-return value target, games added since are reanalysed this
# often (training steps): the buffer turns over within the hour, and a game
# without values trains on the plain result
REANALYSE_NEW_STEPS = 100

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
        # some entries are for the log only (pretty gives them no line)
        line = pretty(entry)
        if line:
            print(line, flush=True)
        with open(os.path.join(run_dir, "log.txt"), "a") as f:
            f.write(json.dumps(entry) + "\n")


_COLOUR = sys.stdout.isatty()


# what heldout.npz keeps beside the examples
HELDOUT_EXTRAS = ("games", "version", "game_seeds", "moves")


def finite_weights(weights: dict) -> bool:
    """Whether every floating-point tensor in saved weights is finite."""
    return all(torch.isfinite(t).all() for t in weights.values() if t.is_floating_point())


def without_positions(saved: dict) -> dict:
    """The held-out examples alone, as heldout_losses takes them."""
    return {k: v for k, v in saved.items() if k not in HELDOUT_EXTRAS}


def load_heldout(path: str, records: list[pe.GameRecord], encoding: pe.Encoding,
                 run_dir: str) -> dict | None:
    """The saved held-out set, or None to draw one. When the encoding has
    changed, the same positions are encoded again from the held-out games
    (`records`), so the numbers stay comparable; a set saved before positions
    were kept, or whose games are gone, is drawn again (a new baseline)."""
    if not os.path.exists(path):
        return None
    with np.load(path) as saved:
        saved = dict(saved)
    if "games" not in saved:
        log(run_dir, {"event": "held-out set redrawn", "reason": "drawn from too few games"})
        return None
    # a set saved without its version can only be judged by its shapes
    current = (int(saved.get("version", pe.ENCODING_VERSION)) == pe.ENCODING_VERSION
               and saved["targets"].shape[1] == encoding.target_count
               and saved["globals"].shape[1] == encoding.global_count
               and saved["side_targets"].shape[1] == encoding.side_target_count
               and "turn_positions" in saved)
    if current:
        return without_positions(saved)
    if "game_seeds" in saved:
        index = {r.game_seed: i for i, r in enumerate(records)}
        seeds = [int(s) for s in saved["game_seeds"]]
        if all(s in index for s in seeds):
            positions = [(index[s], int(m)) for s, m in zip(seeds, saved["moves"])]
            examples = encoding.encode_examples(records, positions)
            np.savez_compressed(path, **examples, games=saved["games"],
                                version=np.array(pe.ENCODING_VERSION),
                                game_seeds=saved["game_seeds"], moves=saved["moves"])
            log(run_dir, {"event": "held-out set encoded again",
                          "version": pe.ENCODING_VERSION})
            return examples
    log(run_dir, {"event": "held-out set redrawn", "reason": "the encoding changed"})
    return None


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
             f"{_num(e['ms_per_step'])} ms, idle {_num(100 * e['training_waited'])}%)"
             + (_style(f", {e['skipped_steps']} skipped", "31") if e.get("skipped_steps") else "")),
            f"play {_num(e['games_per_minute'], 1)} games/min, "
            f"{_num(e['positions_per_second'])} pos/s" + (
                " (CPU {cpu:.0%}, GPU {gpu:.0%}, lock {lock:.0%})".format(**e["selfplay_time"])
                if e.get("selfplay_time") else "") + (
                f", cache {100 * e['cache_hits']:.0f}%" if e.get("cache_hits") is not None else ""),
            f"decided {_num(e.get('decided_recent_percent'))}% "
            f"(buffer {_num(e.get('decided_buffer_percent'))}%), "
            f"stalemate {_num(e.get('stalled_recent_percent'))}%",
            (f"handicap: trailing side won {_num(100 * e['handicap']['trailing_won'])}% "
             f"of {e['handicap']['games']}  |  " if e.get("handicap") else "") +
            (f"pool: current won {_num(100 * e['pool']['current_won'])}% "
             f"of {e['pool']['games']}" + (
                 f" (hardest {_num(e['pool']['hardest']['steps'])}: "
                 f"{_num(100 * e['pool']['hardest']['current_won'])}%)"
                 if e["pool"].get("hardest") else "") + "  |  " if e.get("pool") else "") +
            f"{_num(e.get('commands_per_turn'), 1)} commands/turn "
            f"(best {_num(e.get('best'))}" + (
                f", fog {_num(100 * e['fog_share'])}%"
                if e.get("fog_share") is not None and e["fog_share"] < 1 else "") + ")",
        ]
        # settings that change while they matter, left out once settled: the
        # city weight faded to 0, the learning rate at its floor, the reuse
        # at its target
        settings = []
        if e.get("reuse") is not None and not (
                e.get("reuse_target") and abs(e["reuse"] - e["reuse_target"]) <= 0.05 * e["reuse_target"]):
            settings.append(f"reuse {_num(e['reuse'], 1)}")
        if e.get("city_weight"):
            settings.append(f"city weight {_num(e['city_weight'], 2)}")
        lr = e.get("learning_rate")
        if lr is not None and not (e.get("lr_floor") and abs(lr - e["lr_floor"]) <= 1e-3 * e["lr_floor"]):
            settings.append(f"lr {lr:.1e}")
        if settings:
            parts.append(", ".join(settings))
        parts += [
            f"loss {_num(losses.get('total'), 3)} "
            f"(policy KL {_num(losses.get('policy_kl'), 3)}, "
            f"value {_num(losses.get('value'), 3)})",
            f"held-out {_num(held.get('total'), 3)} "
            f"(KL {_num(held.get('policy_kl'), 3)}, value {_num(held.get('value'), 3)}"
            + (f", bias {held['value_bias']:+.3f}" if held.get("value_bias") is not None else "")
            + ")",
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
        against = ("" if not e.get("opponents") else " (" + ", ".join(
            f"{_num(int(s))}: {w}-{l}" for s, (w, l) in e["opponents"].items()) + ")")
        return (f"{when}  {_style('gate', '1;35')}    step {_num(e['steps'])} vs best "
                f"{_num(e['best'])}: {_style(e['verdict'], colour)}, won "
                f"{e['captures']} of {e['captures'] + e['was_captured']} decided"
                f"{against} ({e['games']} games, {_num(e['seconds'] / 60, 1)} min)")
    if kind == "gate match":
        return ""  # its gate's line lists each part
    if kind == "rating":
        return (f"{when}  {_style('rating', '1;34')}  best {_num(e['best'])}: "
                f"{_num(e['elo'])} Elo (random 0, score search {_num(e.get('score_bot'))})")
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
    if kind == "run start":
        changed = " ".join(
            f"--{k.replace('_', '-')} " + (" ".join(map(str, v)) if isinstance(v, list) else str(v))
            for k, v in e["changed"].items() if k != "dir")
        return (f"{when}  {_style('run start', '1')}  {changed or 'all settings default'}"
                f"  (code {e.get('code') or '?'}, encoding {e['encoding_version']}, "
                f"rules {e['rules_version']})")
    if kind == "bootstrap":
        return (f"{when}  {_style('bootstrap', '1')}  {_num(e['games'])} games, "
                f"{_num(e['positions'])} positions, decided "
                f"{_num(e.get('decided_percent'))}%  ({_num(e['seconds'], 1)} s)")
    rest = ", ".join(f"{k} {v}" for k, v in e.items() if k not in ("time", "event"))
    return f"{when}  {_style(kind, '1')}  {rest}"


def parser() -> argparse.ArgumentParser:
    """The run's flags."""
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
    p.add_argument("--reuse", type=float, default=1.0,
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
    p.add_argument("--gate-steps", type=int, default=4000,
                   help="training steps between gates: the average against the "
                        "best network, replacing it if it wins")
    p.add_argument("--gate-games", type=int, default=128,
                   help="all played at once")
    p.add_argument("--gate-opponents", type=int, default=3,
                   help="networks a gate plays: the best, and the bests before it "
                        "(from the snapshots), the same games split among them, so "
                        "a candidate has to beat more than its predecessor; the "
                        "verdict is on the pooled result")
    p.add_argument("--gate-best-share", type=float, default=0.5,
                   help="with several gate opponents, the share of the games "
                        "against the current best; the rest go evenly to the others")
    p.add_argument("--gate-games-trained", type=float, default=1.0,
                   help="the share of a gate's games added to training: only the "
                        "candidate's moves (the opponents' are older networks'), "
                        "thinned to self-play's --full-share, since every move had "
                        "a full search without noise. A gate is a few percent of "
                        "self-play's games between gates")
    p.add_argument("--gate-max-minutes", type=float, default=30.0,
                   help="a gate's time limit, like --eval-max-minutes; games "
                        "still going then count for neither side")
    p.add_argument("--gate-threshold", type=float, default=0.5,
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
    p.add_argument("--lr-floor", type=float, default=2.5e-5)
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
    p.add_argument("--search-value-weight", type=float, default=0.0,
                   help="the share of the value target from each position's own "
                        "search value; the rest is the result. Off: the value "
                        "is the result alone, and the search's values train "
                        "the short-term value heads instead")
    p.add_argument("--value-lambda", type=float, default=0.95,
                   help="the value target as a lambda-return: each later turn of "
                        "the player's blends in its search value, ending in the "
                        "result, over roughly 1 / (1 - lambda) turns (0.9: ten), so "
                        "what a move leads to soon is credited at once. 1: the "
                        "result alone. The values are the training network's "
                        "own, reanalysed (--reanalyse-steps), not the search "
                        "values recorded by the network that played. Held-out "
                        "losses still use the result")
    p.add_argument("--reanalyse-steps", type=int, default=2000,
                   help="with --value-lambda, every this many training steps the "
                        "whole buffer's turn starts are valued again by the "
                        "training network's average (new games every "
                        f"{REANALYSE_NEW_STEPS})")
    p.add_argument("--value-per-turn", action=argparse.BooleanOptionalAction, default=False,
                   help="weigh each player turn the same in the value loss, not "
                        "each move, against the network rating the player to move "
                        "too well (the held-out bias); off: the bias fell to about "
                        "0 without it")
    p.add_argument("--value-share", type=float, default=0.2,
                   help="the share of samples the value trains on, so each "
                        "game's result is repeated less")
    p.add_argument("--parallel", type=int, default=256,
                   help="self-play games at once, in each of two sessions")
    p.add_argument("--self-play-process", action=argparse.BooleanOptionalAction, default=True,
                   help="self-play in a process of its own (worker.py), so its Python "
                        "doesn't share the interpreter lock or the cores with training; "
                        "else a thread of the run")
    # self-play's full searches; gates and anchors the same, at --eval-simulations
    add_search_arguments(p, simulations=64, samples=1, turn_search=["0"], replies=["likely"])
    p.add_argument("--handicap-share", type=float, default=0.2,
                   help="share of self-play games where one side, at random, "
                        "gets an edge, so the bot learns to fight from behind; "
                        "kept out of the held-out set and the self-play measures")
    p.add_argument("--handicap-income-share", type=float, default=0.75,
                   help="of handicap games, the share where the edge is extra "
                        "capital income each turn, as the game's own bots get "
                        "(Hard +1, Crazy +3); the rest get a head start in stars")
    p.add_argument("--handicap-income", type=int, nargs="+", default=[1, 2],
                   help="the extra capital income handicap games draw from")
    p.add_argument("--handicap-stars", type=int, nargs="+", default=[3, 6, 10],
                   help="the head starts handicap games draw from")
    p.add_argument("--handicap-catch-up-share", type=float, default=0.5,
                   help="of handicap games, the share that are catch-up games "
                        "instead: once a side's search value falls below "
                        "--catch-up-below in the mid or late game, it gets "
                        "--handicap-income more capital income from then on")
    p.add_argument("--catch-up-from-turn", type=int, default=10)
    p.add_argument("--catch-up-below", type=float, default=-0.5)
    p.add_argument("--trailing-search-factor", type=int, default=2,
                   help="in handicap games, a side whose value is below "
                        "--catch-up-below searches with this many times the "
                        "simulations")
    p.add_argument("--pool-share", type=float, default=0.2,
                   help="share of self-play games against an earlier best "
                        "network (the opponent pool) instead of the current one, "
                        "so the bot keeps beating strategies it has moved past; "
                        "only the current network's moves train")
    p.add_argument("--pool-size", type=int, default=8,
                   help="the opponent pool: this many of the latest earlier "
                        "best networks")
    p.add_argument("--pool-weighting", choices=["pfsp", "uniform"], default="pfsp",
                   help="how pool opponents are drawn: pfsp (AlphaStar's prioritized "
                        "fictitious self-play: by (1 - the current network's recent "
                        "win rate against each)^2, so the ones it struggles with "
                        "come up most) or uniformly")
    p.add_argument("--explore-candidates", type=int, default=2,
                   help="of the Gumbel candidates in self-play's full searches, "
                        "how many are random legal moves, so moves the policy "
                        "has all but ruled out still get tried")
    p.add_argument("--full-share", type=float, default=0.25,
                   help="share of decisions with the full search, recorded for "
                        "the policy; the rest get a quick search")
    p.add_argument("--quick-simulations", type=int, default=16)
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
    p.add_argument("--eval-steps", type=int, default=10000,
                   help="training steps between the best network's matches "
                        "against the random bot and the score search (0 for "
                        "none); counted from the last, so restarts keep the "
                        "schedule")
    p.add_argument("--eval-games", type=int, default=64,
                   help="games per evaluation match, in seat-swapped pairs")
    p.add_argument("--eval-simulations", type=int, default=50)
    p.add_argument("--eval-max-minutes", type=float, default=10.0,
                   help="once 90%% of a match's games are done the rest get 50 "
                        "more moves; past this many minutes they end at once "
                        "(scored by the lead)")
    p.add_argument("--crazy-games", type=int, default=0,
                   help="anchors also play this many games against the real game's "
                        "Crazy bot (stocknavalon.crazy; needs the oracle built and "
                        "the game set up, docs/oracle.md), checking the rules as they "
                        "go; 0 for none")
    p.add_argument("--crazy-parallel", type=int, default=4,
                   help="games against Crazy at a time, an oracle each")
    p.add_argument("--crazy-type", default="any", choices=["any"] + crazy.MAP_TYPES,
                   help="their map type, or any: each in turn by pair of games, "
                        "results by type")
    p.add_argument("--crazy-size", type=int, default=11, help="their map size")
    p.add_argument("--crazy-max-turns", type=int, default=60,
                   help="their turn limit (the side ahead on score wins, undecided)")
    p.add_argument("--holdout-every", type=int, default=20,
                   help="one finished self-play game in this many is kept out "
                        "of training, for the held-out loss")
    p.add_argument("--minutes", type=float, default=float("inf"),
                   help="stop after this long")
    return p


def make_trainer(args: argparse.Namespace, encoding: pe.Encoding, device: torch.device,
                 channels: int | None = None, blocks: int | None = None) -> Trainer:
    """A fresh network and its trainer, as the run's flags describe (a
    different size with `channels` and `blocks`)."""
    model = Network(encoding, channels or args.channels, blocks or args.blocks).to(device)
    return Trainer(model, encoding, device, lr=args.lr, per_game=args.per_game,
                   augment=args.augment, ema_decay=args.ema_decay,
                   cut_off_weight=args.cut_off_weight, value_share=args.value_share,
                   search_value_weight=args.search_value_weight,
                   value_per_turn=args.value_per_turn, value_lambda=args.value_lambda)


def run_learning_rate(args: argparse.Namespace, steps: int, fresh_steps: int | None) -> float:
    """The learning rate `steps` into training, by the run's flags."""
    return learning_rate_at(steps, fresh_steps, args.lr, args.lr_floor, args.lr_decay_steps,
                            args.lr_warmup_steps)


def run_city_weight(args: argparse.Namespace, steps: int) -> float:
    """The value target's weight on the final city lead `steps` into
    training: faded out over --city-fade-steps."""
    if args.city_fade_steps <= 0:
        return 0.0
    return args.city_weight * max(0.0, 1 - steps / args.city_fade_steps)


def code_version() -> str | None:
    """The git commit the code is at, with "+changes" if the working tree
    differs from it; None outside a git checkout."""
    here = os.path.dirname(os.path.abspath(__file__))
    try:
        commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=here,
                                capture_output=True, text=True, timeout=10).stdout.strip()
        changed = subprocess.run(["git", "status", "--porcelain"], cwd=here,
                                 capture_output=True, text=True, timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    return (commit + ("+changes" if changed else "")) if commit else None


def start_entry(args: argparse.Namespace) -> dict[str, Any]:
    """The run start's log entry: every setting, those differing from the
    defaults apart, and what the code was."""
    settings = vars(args)
    defaults = vars(parser().parse_args(["--dir", args.dir]))
    return {"event": "run start", "settings": settings,
            "changed": {k: v for k, v in settings.items() if v != defaults.get(k)},
            "code": code_version(), "encoding_version": pe.ENCODING_VERSION,
            "rules_version": pe.RULES_VERSION}


def main():
    args = parser().parse_args()

    os.makedirs(args.dir, exist_ok=True)
    log(args.dir, start_entry(args))
    records_path = os.path.join(args.dir, "records.txt")
    checkpoint_path = os.path.join(args.dir, "checkpoint.pt")
    device = torch.device(args.device) if args.device else pick_device()
    encoding = pe.Encoding()

    trainer = make_trainer(args, encoding, device)
    model = trainer.model
    iteration = 0
    initialized = False
    if os.path.exists(checkpoint_path):
        state = torch.load(checkpoint_path, map_location=device)
        broken = not (finite_weights(state["model"]) and finite_weights(state.get("ema", {})))
        if broken:
            # weights gone to NaN (a bad batch, before steps were guarded):
            # carry on from the best network, with a fresh optimizer
            best = torch.load(os.path.join(args.dir, "best.pt"), map_location=device)
            state = {**state, "model": best["model"], "ema": best["model"]}
            log(args.dir, {"event": "recovered from the best network",
                           "steps": state["steps"], "best": best["steps"]})
        fresh = load_weights(model, state["model"])
        if broken:
            fresh = fresh or ["(recovered)"]
        if not fresh:
            trainer.optimizer.load_state_dict(state["optimizer"])
            trainer.fresh_steps = None  # its state goes on: no warm-up
        elif not broken:
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
        return run_learning_rate(args, trainer.steps - run_start, trainer.fresh_steps)

    def city_weight() -> float:
        return run_city_weight(args, trainer.steps - run_start)

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
    # as loaded, before self-play adds to it: for encoding the held-out set
    # again when the layout changes
    holdout_records = pe.load_records(holdout_path) if os.path.exists(holdout_path) else []
    holdout.add(holdout_records)
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

    session = self_play_settings(args, fog_share())

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
        bootstrap = session.copy()
        bootstrap.network_beliefs = False
        games = pe.play_games(bootstrap, chunk, seed=seed, threads=args.threads)
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

    def revalue(new_only: bool):
        """Reanalysis for the lambda-return value target (train.reanalyse),
        with the training network's average: the whole buffer, or the games
        added since."""
        start = time.perf_counter()
        games, positions = reanalyse(buffer, trainer.ema, encoding, device, gpu_lock,
                                     new_only=new_only)
        if not new_only:
            log(args.dir, {"event": "reanalysed", "steps": trainer.steps, "games": games,
                           "positions": positions,
                           "seconds": round(time.perf_counter() - start, 1)})

    reanalysing = args.value_lambda < 1
    if reanalysing:
        revalue(new_only=False)
    last_full = last_new = trainer.steps
    player = SelfPlayThread(args, buffer, records_path, gpu_lock, seed=iteration * 7919 + 13,
                            holdout=holdout, holdout_path=holdout_path, recent=recent,
                            best_steps=best["steps"])
    del recent
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
    heldout_set = load_heldout(heldout_set_path, holdout_records, encoding, args.dir)
    del holdout_records
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
                values = trainer.step(buffer, args.batch, city_weight())
                window.skipped += not values
                for name, value in values.items():
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
                # what the settled values settle at, so the terminal line can
                # leave them out (the log keeps everything)
                entry["reuse_target"] = args.reuse
                entry["lr_floor"] = args.lr_floor
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
                        encoding, HELDOUT_POSITIONS, 0, per_game=1, positions=True)
                    np.savez_compressed(heldout_set_path, **heldout_set,
                                        games=np.array(holdout.game_count),
                                        version=np.array(pe.ENCODING_VERSION))
                    heldout_set = without_positions(heldout_set)
                    log(args.dir, {"event": "held-out set drawn",
                                   "games": holdout.game_count})
                entry["heldout"] = heldout_losses(model, device, heldout_set, gpu_lock)
                log(args.dir, entry)
                window = Window()
                last_log = now
            if reanalysing and trainer.steps - last_full >= args.reanalyse_steps:
                revalue(new_only=False)
                last_full = last_new = trainer.steps
            elif reanalysing and trainer.steps - last_new >= REANALYSE_NEW_STEPS:
                revalue(new_only=True)
                last_new = trainer.steps
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
    trained = steps - window.skipped
    played = player.take_window()
    added = buffer.added - added_at_start
    return {
        "event": "status",
        "steps": trainer.steps,
        "steps_per_minute": round(60 * steps / seconds, 1),
        "ms_per_step": round(1000 * window.seconds_training / steps) if steps else None,
        "training_waited": round(window.waiting / seconds, 2),
        "losses": ({k: round(v / trained, 4) for k, v in window.losses.items()}
                   if trained else None),
        # steps whose loss wasn't finite, so they were skipped (a bad target)
        "skipped_steps": window.skipped or None,
        "games_per_minute": round(60 * played["games"] / seconds, 1),
        "positions_per_second": round(played["evaluated"] / seconds),
        # where self-play's time went: the CPU, waiting for the GPU lock, the GPU
        "selfplay_time": player.take_times(),
        # the share of evaluations the cache answered instead of the network
        "cache_hits": player.take_cache_rate(),
        # games ending in a capture rather than the turn limit: the latest
        # games, and the whole buffer
        "decided_recent_percent": round(100 * player.decided_recent(), 1),
        "decided_buffer_percent": round(100 * buffer.decided_share, 1),
        # ended by the stalemate cutoff, of this session's latest games
        "stalled_recent_percent": (None if player.stalled_recent() is None
                                   else round(100 * player.stalled_recent(), 1)),
        # handicap games: how often the side that started behind came back
        "handicap": player.handicap_recent(),
        # opponent-pool games: how often the current network beat an earlier best
        "pool": player.pool_recent(),
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


def gate_split(games: int, opponents: int, best_share: float) -> list[int]:
    """A gate's games among its opponents, in seat-swapped pairs: about
    `best_share` against the current best (the first), the rest evenly over
    the earlier ones. The total stays `games`, whatever the opponents."""
    pairs = games // 2
    if opponents <= 1:
        return [2 * pairs]
    best = min(pairs, max(1, round(pairs * best_share)))
    rest, others = pairs - best, opponents - 1
    return [2 * best] + [2 * (rest // others + (k < rest % others)) for k in range(others)]


class SelfPlayThread(threading.Thread):
    """Self-play, continuously, with the best network only. A worker
    (worker.py) plays the games: in a process of its own (--self-play-process,
    the default), so its Python doesn't share the interpreter lock or the cores
    with training, or as a thread. This thread takes its games into the buffer
    and the record files (held-out ones apart), and keeps its measures for the
    status line. A worker that fails, or dies (a crash in C++), stops the run
    with the reason."""

    def __init__(self, args, buffer, records_path, gpu_lock, seed, holdout, holdout_path,
                 recent, best_steps):
        super().__init__(daemon=True)
        self.args, self.buffer, self.records_path = args, buffer, records_path
        self.holdout, self.holdout_path = holdout, holdout_path
        # the training step the network playing comes from
        self.best_steps = best_steps
        self.stop = threading.Event()
        self.error = None
        self._lock = threading.Lock()
        self._window = {"games": 0, "evaluated": 0}
        self._fog_share = None
        # the worker's latest measures (Worker.measures), and its time and
        # cache totals at the last status line
        self._measures: dict | None = None
        self._times_then = None
        self._cache_then = None
        self.totals = {"games": 0}
        # whether each of the run's latest ordinary games ended in a capture,
        # for the worker to start its measure from
        decided = [g.winner >= 0 for g in recent
                   if g.handicap_leader < 0 and not g.catch_up and g.pool_opponent_seat < 0]
        decided = decided[-worker.RECENT_GAMES:]
        if args.self_play_process:
            # spawn: a fresh interpreter, which loads its own torch and GPU
            # state (a forked one would inherit the parent's)
            context = multiprocessing.get_context("spawn")
            self._inbox, self._outbox = context.Queue(), context.Queue()
            self._worker = context.Process(
                target=worker.main, args=(args, seed, decided, self._inbox, self._outbox),
                kwargs={"threads": WORKER_TORCH_THREADS}, daemon=True)
        else:
            self._inbox, self._outbox = queue.Queue(), queue.Queue()
            self._worker = threading.Thread(
                target=worker.main, args=(args, seed, decided, self._inbox, self._outbox),
                kwargs={"gpu_lock": gpu_lock}, daemon=True)

    @property
    def fog_share(self) -> float:
        return 1.0 if self._fog_share is None else self._fog_share

    @fog_share.setter
    def fog_share(self, share: float):
        """The share of games to start with fog: the worker hears of changes."""
        if share != self._fog_share:
            self._fog_share = share
            self._inbox.put(("fog", share))

    def start(self):
        self._worker.start()
        super().start()

    def publish(self, weights: dict, steps: int):
        """A new best network (already saved to best.pt): the worker reloads it
        between batches of games."""
        with self._lock:
            self.best_steps = steps
        self._inbox.put(("best",))

    def add_games(self, games: list[pe.GameRecord]):
        """Games played elsewhere (a gate's) for training: into the buffer and
        the record file, but not the held-out set or self-play's measures."""
        with _records_lock:
            pe.save_records(games, self.records_path)
            self.buffer.add(games)

    def _take(self, kept: str, held: str, games: int, evaluated: int):
        """A batch of the worker's games: those to train on into the buffer
        and the record file, the held-out ones into theirs."""
        kept_games = pe.records_from_text(kept) if kept else []
        held_games = pe.records_from_text(held) if held else []
        with _records_lock:
            if kept_games:
                with open(self.records_path, "a") as f:
                    f.write(kept)
                self.buffer.add(kept_games)
            if held_games:
                with open(self.holdout_path, "a") as f:
                    f.write(held)
                self.holdout.add(held_games)
        with self._lock:
            self._window["games"] += games
            self._window["evaluated"] += evaluated
            self.totals["games"] += games

    def run(self):
        try:
            while not self.stop.is_set():
                try:
                    message = self._outbox.get(timeout=0.5)
                except queue.Empty:
                    if not self._worker.is_alive():
                        code = getattr(self._worker, "exitcode", None)
                        raise RuntimeError(f"the self-play worker stopped (exit code {code})")
                    continue
                kind = message[0]
                if kind == "progress":
                    with self._lock:
                        self._window["evaluated"] += message[1]
                elif kind == "games":
                    self._take(*message[1:])
                elif kind == "measures":
                    with self._lock:
                        self._measures = message[1]
                elif kind == "error":
                    raise RuntimeError(f"the self-play worker failed:\n{message[1]}")
        except Exception as e:  # handed to the training thread to raise
            self.error = e
        finally:
            # stop the worker; its last games still come in meanwhile (a
            # process can't exit while its queue holds messages nobody takes)
            self._inbox.put(("stop",))
            deadline = time.perf_counter() + WORKER_STOP_SECONDS
            while self._worker.is_alive() and time.perf_counter() < deadline:
                try:
                    message = self._outbox.get(timeout=0.2)
                except queue.Empty:
                    continue
                if message[0] == "games":
                    self._take(*message[1:])
            if self._worker.is_alive() and hasattr(self._worker, "terminate"):
                self._worker.terminate()

    def _measure(self, key):
        with self._lock:
            return None if self._measures is None else self._measures[key]

    def take_window(self) -> dict[str, int]:
        with self._lock:
            window, self._window = self._window, {"games": 0, "evaluated": 0}
        return window

    def take_cache_rate(self) -> float | None:
        """The share of evaluations the cache answered since the last call."""
        now = self._measure("cache")
        if now is None:
            return None
        then, self._cache_then = self._cache_then, now
        if then is None or now[0] == then[0]:
            return None
        return round((now[1] - then[1]) / (now[0] - then[0]), 3)

    def take_times(self) -> dict[str, float] | None:
        """The shares of self-play's time since the last call spent on the
        CPU, waiting for the GPU lock and on the GPU (selfplay.TIMES)."""
        now = self._measure("times")
        if now is None:
            return None
        then, self._times_then = self._times_then, now
        if then is None:
            return None
        spent = {k: now[k] - then[k] for k in now}
        total = sum(spent.values())
        return {k: round(v / total, 2) for k, v in spent.items()} if total > 0 else None

    def decided_recent(self) -> float:
        return self._measure("decided") or 0.0

    def decided_window_full(self) -> bool:
        return bool(self._measure("decided_full"))

    def handicap_recent(self) -> dict | None:
        """Of the latest handicap games, the share each side won."""
        return self._measure("handicap")

    def pool_recent(self) -> dict | None:
        """Of the latest opponent-pool games, the share the current network
        won; and the pool network it does worst against."""
        return self._measure("pool")

    def stalled_recent(self) -> float | None:
        return self._measure("stalled")

    def commands_per_turn(self) -> float | None:
        return self._measure("activity")


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
        snapshots = sorted((snapshot_steps(name), name) for name in os.listdir(self.folder)
                           if name.startswith("step_") and name.endswith(".pt"))
        return snapshots[-1] if snapshots else None

    def _match(self, model, opponent, name, steps, games, fog_share=1.0, gate=False,
               keep=None, share=1.0, seed=None):
        def report(p):
            log(self.args.dir, {"event": "gate progress" if gate else "evaluation progress",
                                "steps": steps, "against": name, **p})

        # A gate plays all its games at once, to a shorter turn limit (its
        # captures come well before), with its own time limit, and stops as
        # soon as its verdict can't change: it has to keep up with training.
        # Matches use the training rules (peace treaties or not).
        return match(model, opponent, self.encoding, self.device, games=games,
                     parallel=games if gate else 32,
                     search=search_config(self.args, simulations=self.args.eval_simulations),
                     max_turns=self.args.gate_max_turns if gate else self.args.max_turns,
                     stall_turns=self.args.stall_turns, fog_share=fog_share,
                     # the belief heads only train once there's fog in
                     # training: until then they'd be untrained nonsense, so
                     # fogged matches use the fixed priors
                     network_beliefs=self.args.network_beliefs and self.fog_share() > 0,
                     peace_treaties=self.args.peace_treaties,
                     threads=self.args.session_threads,
                     # a gate's games train the candidate's moves only
                     train_sides=(True, False) if gate else (True, True),
                     # a gate's part (several opponents) stops on its own
                     # share of the decided games and the time
                     settled=(gate_settled(self.args.gate_threshold,
                                           max(2, round(self.args.gate_min_decided * share)))
                              if gate else None),
                     seed=steps if seed is None else seed, gpu_lock=self.gpu_lock,
                     progress=report, progress_seconds=120 if gate else 30,
                     max_minutes=(self.args.gate_max_minutes * share if gate
                                  else self.args.eval_max_minutes),
                     keep=keep)

    def _gate_opponents(self) -> list[tuple[int, dict]]:
        """A gate's opponents, as (training steps, weights): the best, then
        the bests before it, newest first (--gate-opponents), from the
        snapshots kept at promotions and anchors."""
        best_steps = self.best["steps"]
        out = [(best_steps, self.best["model"])]
        if self.args.gate_opponents > 1:
            earlier = pool_snapshots(self.args.dir, self.args.gate_opponents - 1, best_steps)
            for path in reversed(earlier):
                out.append((snapshot_steps(path),
                            torch.load(path, map_location="cpu")["model"]))
        return out

    def _run_gate(self, weights, steps):
        best_steps = self.best["steps"]
        opponents = self._gate_opponents()
        split = gate_split(self.args.gate_games, len(opponents), self.args.gate_best_share)
        log(self.args.dir, {"event": "gate start", "steps": steps, "best": best_steps,
                            "games": self.args.gate_games,
                            **({"opponents": [s for s, _ in opponents]}
                               if len(opponents) > 1 else {})})
        games: list[pe.GameRecord] = []
        candidate = self.network(weights)
        if len(opponents) == 1:
            result = self._match(candidate, self.network(self.best["model"]),
                                 f"best {best_steps}", steps, self.args.gate_games,
                                 fog_share=self.fog_share(), gate=True, keep=games)
        else:
            # the same games, split among the opponents; judged on the total
            result = {"games": 0, "captures": 0, "was_captured": 0, "seconds": 0,
                      "opponents": {}}
            for k, ((against, opponent), count) in enumerate(zip(opponents, split)):
                if count == 0:
                    continue
                part = self._match(candidate, self.network(opponent),
                                   f"{'best' if k == 0 else 'step'} {against}", steps, count,
                                   fog_share=self.fog_share(), gate=True, keep=games,
                                   share=count / self.args.gate_games, seed=steps + k)
                log(self.args.dir, {"event": "gate match", "steps": steps,
                                    "against": against, **part})
                for key in ("games", "captures", "was_captured", "seconds"):
                    result[key] += part[key]
                result["opponents"][str(against)] = [part["captures"], part["was_captured"]]
        # a share of them (by default all), the candidate's moves only, at
        # self-play's density of training positions
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
            # kept for the opponent pool (apart from the anchors' snapshots,
            # which mark the networks already anchored)
            pool = os.path.join(self.args.dir, "pool")
            os.makedirs(pool, exist_ok=True)
            torch.save(self.best, os.path.join(pool, f"step_{steps}.pt"))
            self.on_new_best(weights, steps)
        self._log_rating()

    def _log_rating(self):
        """The best network's rating, refitted from every match so far."""
        ratings = run_ratings(self.args.dir)
        steps = str(self.best["steps"])
        if steps in ratings:
            log(self.args.dir, {"event": "rating", "best": self.best["steps"],
                                "elo": ratings[steps], "score_bot": ratings.get("score")})

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
        if self.args.crazy_games > 0 and not self.stop.is_set():
            self._run_crazy(model, steps)
        self._log_rating()

    def _run_crazy(self, model, steps: int):
        """The real game's Crazy bot, through the oracle: a rating against a
        fixed outside opponent, and a rules check (each game stops at its
        first difference from the real game, logged with the real state
        saved in crazy/)."""
        settings = crazy.Settings(type=self.args.crazy_type, size=self.args.crazy_size,
                                  max_turns=self.args.crazy_max_turns,
                                  save=os.path.join(self.args.dir, "crazy"))
        if crazy.mirror is None or not os.path.exists(settings.oracle):
            log(self.args.dir, {"event": "crazy skipped", "reason": f"no oracle at {settings.oracle}"})
            return
        try:
            result = crazy.match(
                model, self.device, search_config(self.args, simulations=self.args.eval_simulations),
                self.args.crazy_games, settings, seed=steps, parallel=self.args.crazy_parallel,
                network_beliefs=self.args.network_beliefs and self.fog_share() > 0,
                gpu_lock=self.gpu_lock)
        except Exception as e:  # the oracle's trouble shouldn't stop training
            log(self.args.dir, {"event": "crazy failed", "steps": steps, "error": repr(e)})
            return
        differences = result.pop("differences")
        log(self.args.dir, {"event": "evaluation", "steps": steps, "against": "crazy", **result})
        for d in differences:
            log(self.args.dir, {"event": "rules difference", "steps": steps, **d})
        if differences:
            # our rules differ from the real game's: stop rather than train on
            # them (the training loop raises this)
            raise RuntimeError(f"{len(differences)} rules difference(s) against Crazy at step "
                               f"{steps}, logged as \"rules difference\" (states in "
                               f"{os.path.join(self.args.dir, 'crazy')}): stopping")

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
