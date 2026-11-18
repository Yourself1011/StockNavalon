"""How well the bot understands its economy, from a run's recent held-out
games: the checks that showed the value was the bottleneck (notes/runs.md,
"Villages and ruins left alone"), in one command, for any checkpoint.

    python -m stocknavalon.diagnose runs/ninth
    python -m stocknavalon.diagnose runs/ninth --networks runs/ninth/checkpoints/step_212000.pt

From the games as they were played (by whichever network self-play used):
how often a village or ruin in reach at a turn's start is left alone, how
often a unit standing on one takes it, how often a port or bridge is built
when one could be, and the share of units left idle, by game phase. Then for
each network (by default the training network's average and the best): the
change in its value right after capturing a village, examining a ruin,
building a port or a bridge (same player still to move), and its prior for
those moves against the search's policy target where they were possible.
Changes no training.
"""

from __future__ import annotations

import argparse
import collections
import json
import os

import numpy as np
import torch

import polyengine as pe

from .model import Network
from .versus import load_side

FREE = (pe.ImprovementType.VILLAGE, pe.ImprovementType.RUIN)
UNIT_COMMANDS = {pe.CommandType.MOVE, pe.CommandType.ATTACK, pe.CommandType.RECOVER,
                 pe.CommandType.HEALOTHERS, pe.CommandType.CAPTURE,
                 pe.CommandType.EXAMINERUINS, pe.CommandType.DISEMBARK,
                 pe.CommandType.PROMOTE, pe.CommandType.UPGRADE, pe.CommandType.DISBAND}
BUILDS = {int(pe.ImprovementType.PORT): "port", int(pe.ImprovementType.BRIDGE): "bridge"}


def phase_of(turn: int) -> str:
    return "turns 0-10" if turn < 10 else "turns 10-25" if turn < 25 else "turns 25+"


def kind_of(game: pe.Game, c: pe.Command) -> str | None:
    """What a command does to the economy, for these checks: capturing a
    village, examining a ruin, building a port or bridge, stepping onto an
    unclaimed village or ruin; None otherwise."""
    if c.type == pe.CommandType.CAPTURE:
        return "capture village" if game.tiles[c.from_tile].building == pe.ImprovementType.VILLAGE else None
    if c.type == pe.CommandType.EXAMINERUINS:
        return "examine ruin"
    if c.type == pe.CommandType.BUILD:
        return BUILDS.get(int(c.arg))
    if c.type == pe.CommandType.MOVE and game.tiles[c.to_tile].building in FREE:
        return "step onto village or ruin"
    return None


def examine(records: list[pe.GameRecord], encoding: pe.Encoding):
    """One replay of the games: the behaviour counts, the positions right
    before and after each economy command, and the searched decisions where
    one was possible (with its slots)."""
    turns = collections.Counter()   # turn-level counts
    idle = collections.defaultdict(lambda: [0, 0])  # (phase, health) -> units, idle
    after = collections.defaultdict(list)  # kind -> (before, after, player)
    decisions, slots = [], []  # (record, move), [(kind, slot)]
    for ri, r in enumerate(records):
        searched = set(r.searched)
        game = r.start()
        cmds = r.commands
        i = 0
        while i < len(cmds):
            p, turn = game.current_player, game.turn
            legal = game.legal_commands()
            kinds = {kind_of(game, c) for c in legal} - {None}
            units = {u.tile: u.hp < u.max_hp for u in game.units if u.alive and u.owner == p}
            took, acted = set(), set()
            while i < len(cmds) and game.current_player == p:
                c = cmds[i]
                if i in searched:
                    options = [(kind_of(game, x), encoding.encode_command(game, x))
                               for x in game.legal_commands() if kind_of(game, x)]
                    if options:
                        decisions.append((ri, i))
                        slots.append(options)
                k = kind_of(game, c)
                if k:
                    took.add(k)
                    if k != "step onto village or ruin":
                        g = game.copy()
                        g.apply(c)
                        if g.current_player == p:
                            after[k].append((game.copy(), g, p))
                if c.type in UNIT_COMMANDS and c.from_tile in units:
                    acted.add(c.from_tile)
                game.apply(c)
                i += 1
            for k in kinds:
                turns[(k, "possible")] += 1
                turns[(k, "taken")] += k in took
            reach = "step onto village or ruin" in kinds
            turns[("free in reach", "possible")] += reach
            turns[("free in reach", "taken")] += reach and bool(
                took & {"step onto village or ruin", "capture village", "examine ruin"})
            for tile, damaged in units.items():
                key = (phase_of(turn), "damaged" if damaged else "full health")
                idle[key][0] += 1
                idle[key][1] += tile not in acted
    return turns, idle, after, decisions, slots


@torch.no_grad()
def values(net: Network, encoding: pe.Encoding, games: list[pe.Game], players: list[int],
           batch: int = 512) -> np.ndarray:
    out = []
    for k in range(0, len(games), batch):
        planes, globals_ = encoding.encode_states(games[k:k + batch], players[k:k + batch])
        out.append(net(torch.from_numpy(planes), torch.from_numpy(globals_))["value"].float().numpy())
    return np.concatenate(out) if out else np.zeros(0, np.float32)


@torch.no_grad()
def priors_and_targets(net: Network, encoding: pe.Encoding, records, decisions, slots,
                       batch: int = 512) -> dict[str, tuple[float, float, int]]:
    """Per kind: the mean prior and the mean search target of each possible
    option, and how many options."""
    sums = collections.defaultdict(lambda: [0.0, 0.0, 0])
    for k in range(0, len(decisions), batch):
        ex = encoding.encode_examples(records, decisions[k:k + batch])
        logits = net(torch.from_numpy(ex["planes"]), torch.from_numpy(ex["globals"]))["policy"].float().numpy()
        logits[~ex["legal"].astype(bool)] = -1e9
        prior = np.exp(logits - logits.max(1, keepdims=True))
        prior /= prior.sum(1, keepdims=True)
        for j, options in enumerate(slots[k:k + batch]):
            for kind, slot in options:
                if slot >= 0:
                    s = sums[kind]
                    s[0] += prior[j, slot]
                    s[1] += ex["policy"][j, slot]
                    s[2] += 1
    return {kind: (p / n, t / n, n) for kind, (p, t, n) in sums.items()}


def main():
    parser = argparse.ArgumentParser(description="The bot's economy, checked.")
    parser.add_argument("run", help="a run directory: its held-out games are used")
    parser.add_argument("--networks", nargs="+",
                        help="networks to check (run directories, checkpoints, snapshots); "
                             "by default the run's checkpoint average and best network")
    parser.add_argument("--games", type=int, default=200,
                        help="the latest this many ordinary held-out games")
    parser.add_argument("--json", action="store_true", help="also print the results as JSON")
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    args = parser.parse_args()
    torch.set_num_threads(args.threads)

    encoding = pe.Encoding()
    records = [r for r in pe.load_records(os.path.join(args.run, "holdout.txt"))
               if r.pool_opponent_seat < 0 and r.handicap_leader < 0 and not r.catch_up]
    records = records[-args.games:]
    maps = collections.Counter(str(r.map_type).split(".")[-1].lower() for r in records)
    print(f"{len(records)} held-out games: " + ", ".join(f"{k} {v}" for k, v in sorted(maps.items())))
    turns, idle, after, decisions, slots = examine(records, encoding)
    report: dict = {"games": len(records)}

    print("\nas played (player turns where it was possible, and taken that turn):")
    rows = [("free in reach", "a village or ruin in reach"),
            ("capture village", "a unit on a village could capture it"),
            ("examine ruin", "a unit on a ruin could examine it"),
            ("port", "a port could be built"), ("bridge", "a bridge could be built")]
    report["taken"] = {}
    for key, label in rows:
        n, taken = turns[(key, "possible")], turns[(key, "taken")]
        share = taken / n if n else float("nan")
        report["taken"][key] = {"turns": n, "taken": round(share, 3)}
        print(f"  {label:38s} {n:6d} turns, taken in {100 * share:3.0f}%")
    print("  units left idle all turn:")
    report["idle"] = {}
    for phase in ("turns 0-10", "turns 10-25", "turns 25+"):
        parts = []
        for health in ("full health", "damaged"):
            n, k = idle[(phase, health)]
            parts.append(f"{health} {100 * k / max(1, n):.0f}% of {n}")
            report["idle"][f"{phase}, {health}"] = round(k / max(1, n), 3)
        print(f"    {phase:11s} " + ", ".join(parts))

    paths = args.networks or [os.path.join(args.run, "checkpoint.pt"), args.run]
    report["networks"] = {}
    for path in paths:
        net, name = load_side(path, torch.device("cpu"), False, args.channels, args.blocks)
        net = net.float().eval()
        print(f"\n{name}:")
        entry: dict = {"value change": {}, "prior and target": {}}
        print("  value change right after (same player to move):")
        for kind in ("capture village", "examine ruin", "port", "bridge"):
            rows_ = after.get(kind, [])
            if not rows_:
                print(f"    {kind:16s} none in these games")
                continue
            before = values(net, encoding, [a for a, _, _ in rows_], [p for _, _, p in rows_])
            later = values(net, encoding, [b for _, b, _ in rows_], [p for _, _, p in rows_])
            d = later - before
            entry["value change"][kind] = {"mean": round(float(d.mean()), 4),
                                           "median": round(float(np.median(d)), 4), "n": len(d)}
            print(f"    {kind:16s} {d.mean():+.3f} (median {np.median(d):+.3f}, {len(d)})")
        print("  where possible, per option: the network's prior against the search's target")
        for kind, (prior, target, n) in sorted(priors_and_targets(
                net, encoding, records, decisions, slots).items()):
            entry["prior and target"][kind] = {"prior": round(prior, 4), "target": round(target, 4),
                                               "options": n}
            print(f"    {kind:26s} prior {100 * prior:5.2f}%, target {100 * target:5.2f}% ({n} options)")
        report["networks"][name] = entry
    if args.json:
        print(json.dumps(report))


if __name__ == "__main__":
    main()
