"""Two networks played against each other in full: every game to its end or
the turn limit, no early stop, in seat-swapped pairs with the run's search,
fog and beliefs. By default the gate's own pairing: the training network's
average (checkpoint.pt) against the best network (best.pt). Changes no
training.

    python -m stocknavalon.versus runs/ninth
    python -m stocknavalon.versus runs/ninth/checkpoints/step_212000.pt runs/ninth/best.pt

A side is a run directory (its best network), or a .pt file: a checkpoint's
average of the weights (what gates play; --live for the weights themselves),
or a snapshot. Besides the full result, it replays what a gate would have
said from the same games: its shorter turn limit, and its early stop, taking
the games in the order they finished. And how the decided games split by
length, since a gate's early stop only sees the quick ones.
"""

from __future__ import annotations

import argparse
import math
import os

import torch

import polyengine as pe

from .evaluate import clear_progress, gate_verdict, match, show_progress, verdict_settled
from .model import DTYPES, Network, load_weights, pick_device
from .play import load
from .settings import add_search_arguments, side_count, side_searches


def load_side(path: str, device: torch.device, live: bool, channels: int,
              blocks: int) -> tuple[Network, str]:
    """A run directory's best network, a checkpoint's average (or with
    `live`, its weights), or a snapshot; and a name for it."""
    if os.path.isdir(path):
        return load(path, device, channels, blocks), f"{path} (best)"
    state = torch.load(path, map_location=device)
    key = "ema" if "ema" in state and not live else "model"
    config = state.get("config", {})
    model = Network(pe.Encoding(), config.get("channels", channels),
                    config.get("blocks", blocks)).to(device)
    load_weights(model, state[key])
    steps = state.get("steps", "?")
    kind = {"ema": "average", "model": "weights" if "ema" in state else "snapshot"}[key]
    return model.eval(), f"{path} ({kind}, {steps} steps)"


def decided_share(won: int, lost: int) -> str:
    decided = won + lost
    if not decided:
        return "no decided games"
    share = won / decided
    # a rough 95% interval on the share of decided games won
    margin = 1.96 * math.sqrt(share * (1 - share) / decided)
    return f"{won}–{lost} of {decided} decided ({100 * share:.0f}% ± {100 * margin:.0f})"


def side_won(r: pe.GameRecord) -> int | None:
    """1 if side A won the game by capture, 0 if it lost, None if undecided."""
    if r.winner < 0:
        return None
    seat = 0 if r.seat_zero_side == 0 else 1  # side A's seat
    return int(r.winner == seat)


def gate_replay(records: list[pe.GameRecord], games: int, max_turns: int,
                threshold: float, min_decided: int) -> tuple[str, int, int, int]:
    """What a gate would have said from these games, taken in the order they
    finished: games past its turn limit count as undecided, and it stops as
    soon as its verdict can't change. Returns the verdict, the games it would
    have waited for, and side A's wins and losses by then."""
    won = lost = seen = 0
    for r in records:
        seen += 1
        result = side_won(r) if r.turns <= max_turns else None
        won += result == 1
        lost += result == 0
        if verdict_settled(won, lost, games - seen, threshold, min_decided):
            break
    verdict = gate_verdict({"captures": won, "was_captured": lost}, threshold, min_decided)
    return verdict, seen, won, lost


def main():
    parser = argparse.ArgumentParser(description="Two networks, a full match.")
    parser.add_argument("a", help="side A: a run directory, or a .pt file; with only "
                                  "a run directory, its checkpoint's average")
    parser.add_argument("b", nargs="?", help="side B; by default the run's best network")
    parser.add_argument("--live", action="store_true",
                        help="a checkpoint's weights rather than their average")
    parser.add_argument("--games", type=int, default=128, help="in seat-swapped pairs")
    # as gates (--eval-simulations)
    add_search_arguments(parser, simulations=50, samples=1)
    parser.add_argument("--max-turns", type=int, default=150)
    parser.add_argument("--stall-turns", type=int, default=25)
    parser.add_argument("--gate-max-turns", type=int, default=75,
                        help="for the gate replay: a gate's turn limit")
    parser.add_argument("--gate-threshold", type=float, default=0.55)
    parser.add_argument("--gate-min-decided", type=int, default=20)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--device", default=None)
    parser.add_argument("--dtype", default="float16", choices=sorted(DTYPES))
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    args = parser.parse_args()

    a, b = args.a, args.b
    if b is None:
        # a run directory alone: the gate's pairing
        b = a
        a = os.path.join(a, "checkpoint.pt")
    device = torch.device(args.device) if args.device else pick_device()
    print(f"device: {device}", flush=True)
    sides = [load_side(path, device, args.live, args.channels, args.blocks) for path in (a, b)]
    models = [m.to(DTYPES[args.dtype]) if device.type != "cpu" else m for m, _ in sides]
    names = [name for _, name in sides]
    print(f"A: {names[0]}\nB: {names[1]}", flush=True)

    # the per-side flags (--turn-search, --replies, --backup): one value for
    # both, or one per side (A, B)
    sides_search = side_searches(args, side_count(args))
    records: list[pe.GameRecord] = []
    result = match(models[0], models[1], pe.Encoding(), device, games=args.games,
                   parallel=args.games, search=sides_search[0],
                   side_search=sides_search if len(sides_search) > 1 else None,
                   max_turns=args.max_turns, stall_turns=args.stall_turns, fog_share=1.0,
                   network_beliefs=True, peace_treaties=False, threads=args.threads,
                   seed=args.seed, progress=show_progress, progress_seconds=10,
                   finish_share=1.0, max_minutes=1e9, keep=records)

    clear_progress()
    won, lost = result["captures"], result["was_captured"]
    print(f"\nfull match, {result['games']} games to turn {args.max_turns}, "
          f"{result['seconds'] / 60:.0f} min:")
    print(f"  A {decided_share(won, lost)}; {100 * result['win_rate']:.0f}% counting "
          f"undecided games by score")
    verdict = gate_verdict(result, args.gate_threshold, args.gate_min_decided)
    print(f"  as a gate's verdict on all of them: {verdict}")

    replayed, waited, gate_won, gate_lost = gate_replay(
        records, args.games, args.gate_max_turns, args.gate_threshold, args.gate_min_decided)
    print(f"a gate from the same games (turn limit {args.gate_max_turns}, early stop): "
          f"{replayed} after {waited} games, A {decided_share(gate_won, gate_lost)}")

    decided = sorted((r.turns, side_won(r)) for r in records if side_won(r) is not None)
    if decided:
        print("decided games by length:")
        third = math.ceil(len(decided) / 3)
        for k in range(0, len(decided), third):
            part = decided[k:k + third]
            w = sum(x for _, x in part)
            print(f"  turns {part[0][0]}–{part[-1][0]}: A {decided_share(w, len(part) - w)}")


if __name__ == "__main__":
    main()
