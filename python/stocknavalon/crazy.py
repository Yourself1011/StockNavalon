"""Games against the real game's own Crazy bot (docs/oracle.md, "Playing
against the game's AI"): the oracle plays the game, its AI (AI.GetMove, at
Crazy, with Crazy's handicap) takes one seat, and the network takes the
other through a mirrored polyengine game, searching on what its own player
can see. The mirror checks the rules as it goes, as src/oracle/mirror.py
does: each step it compares both legal lists and the states, and a game
stops at its first difference, which is reported and counted for neither
side.

    python -m stocknavalon.crazy runs/ninth --games 20 --size 11

Seats alternate, tribes are drawn at random, and with --type any (the
default) the map types take turns by pair of games (both seats on each),
the results broken down by type. A game still going at
--max-turns goes to the side ahead on score. Needs the oracle built
(cmake --build --preset release) and the game set up (docs/oracle.md).
"""

from __future__ import annotations

import argparse
import contextlib
import math
import os
import random
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass, field

import numpy as np
import torch

import polyengine as pe

from .model import Network, belief_outputs, pick_device, tie_breaks
from .settings import add_search_arguments, side_searches

def _repository() -> str:
    """The repository's root: the first directory up from here (or from the
    working directory, for a copy of the package in the build tree) with
    src/oracle/mirror.py."""
    for start in (os.path.dirname(os.path.abspath(__file__)), os.getcwd()):
        d = start
        while True:
            if os.path.exists(os.path.join(d, "src", "oracle", "mirror.py")):
                return d
            parent = os.path.dirname(d)
            if parent == d:
                break
            d = parent
    return os.getcwd()


ROOT = _repository()
sys.path.insert(0, os.path.join(ROOT, "src", "oracle"))
try:
    import mirror  # noqa: E402
except ImportError:  # no oracle code here: matches against Crazy can't run
    mirror = None


def evaluator(model: Network, device: torch.device, encoding: pe.Encoding, gpu_lock=None):
    """The network as pe.search's evaluate callback (under `gpu_lock`, if
    given, as a run's matches share the GPU with training)."""
    lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()

    @torch.no_grad()
    def evaluate(planes, globals_, rows, slots, belief_rows):
        with lock:
            out = model(torch.from_numpy(planes).to(device),
                        torch.from_numpy(globals_).to(device))
        logits = out["policy"][torch.from_numpy(rows).to(device),
                               torch.from_numpy(slots).to(device)]
        beliefs = (belief_outputs(out, torch.from_numpy(belief_rows).to(device), encoding)
                   .float().cpu().numpy() if len(belief_rows) else None)
        return (logits.float().cpu().numpy(), out["value"].float().cpu().numpy(), beliefs,
                np.ascontiguousarray(tie_breaks(out).float().cpu().numpy()))
    return evaluate


# the map types by name, in the engine's order
MAP_TYPES = [name.lower() for name in pe.MapType.__members__]


@dataclass
class Settings:
    """Where the oracle and the game are, and the games' format: `type` a
    map type, or "any" for each in turn (map_type_of)."""
    oracle: str = os.path.join(ROOT, "build", "release", "oracle")
    build: str = os.path.join(ROOT, "game", "current")
    type: str = "any"
    size: int = 11
    max_turns: int = 50
    history: int = 6
    save: str | None = None  # the real state at each rules difference


@dataclass
class Outcome:
    """One game: the network's seat, who won (None: a rules difference
    stopped it first), whether by a capture (decided) rather than on score
    at the turn limit, and the difference if any."""
    seed: int
    seat: int
    tribes: list[str]
    map_type: str
    won: bool | None
    decided: bool
    turns: int
    verdict: str
    detail: list[str] = field(default_factory=list)


def map_type_of(settings: Settings, game: int) -> str:
    """Game `game`'s map type: the settings', or with "any" each type in turn
    by pair of games, so both seats play on each."""
    return MAP_TYPES[(game // 2) % len(MAP_TYPES)] if settings.type == "any" else settings.type


def play_game(model: Network, device: torch.device, search: pe.SearchConfig, seed: int,
              seat: int, settings: Settings, network_beliefs: bool = True,
              gpu_lock=None, map_type: str | None = None) -> Outcome:
    """One game against Crazy, the network in `seat`, on `map_type` (by
    default the settings')."""
    map_type = map_type or settings.type
    encoding = pe.Encoding()
    evaluate = evaluator(model, device, encoding, gpu_lock)
    rng = random.Random(seed)
    tribes = rng.sample(mirror.mapstats.TRIBES, 2)
    moves = [0]

    def pick(game: pe.Game) -> pe.Command:
        found = pe.search(game, encoding, evaluate, search, network_beliefs=network_beliefs,
                          seed=seed * 100003 + moves[0])
        moves[0] += 1
        commands, visits, chosen = found[0], found[1], found[4]
        # the search's moves are its samples' (what the player can see), so
        # one can be illegal in the real game, a hidden unit in the way: the
        # most visited legal one instead, as self-play falls back
        legal = {mirror.command_key(c) for c in game.legal_commands()}
        if mirror.command_key(commands[chosen]) in legal:
            return commands[chosen]
        order = sorted(range(len(commands)), key=lambda i: -visits[i])
        return next((commands[i] for i in order if mirror.command_key(commands[i]) in legal),
                    game.legal_commands()[0])

    args = argparse.Namespace(oracle=settings.oracle, build=settings.build, type=map_type,
                              size=settings.size, history=settings.history, save=settings.save,
                              steps=100000)
    result: dict = {}
    steps, verdict, detail = mirror.play(args, seed, ai_seats=(1 - seat,), pick=pick,
                                         tribes=tribes, max_turns=settings.max_turns,
                                         result=result)
    won, decided = None, False
    if verdict.startswith("no differences"):
        if result.get("winner") is not None:
            won, decided = result["winner"] == seat, True
        elif result.get("scores"):
            # the turn limit: the side ahead on score
            scores = result["scores"]
            won = scores[seat] > scores[1 - seat]
    return Outcome(seed, seat, tribes, map_type, won, decided, result.get("turns", 0), verdict,
                   detail)


def match(model: Network, device: torch.device, search: pe.SearchConfig, games: int,
          settings: Settings, seed: int = 0, parallel: int = 4, network_beliefs: bool = True,
          gpu_lock=None) -> dict:
    """`games` games against Crazy (seats alternating), `parallel` at a
    time, each with its own oracle. Returns the results as evaluate.match's
    (games, win_rate, captures, was_captured, ...; a game the turn limit
    ends goes to the side ahead on score, but isn't decided), with the games
    a rules difference stopped counted apart, those differences, and per map
    type (by_map) the games played, won and decided."""
    model.eval()
    start = time.time()
    with ThreadPoolExecutor(max_workers=max(1, parallel)) as pool:
        outcomes = list(pool.map(
            lambda g: play_game(model, device, search, seed + g, g % 2, settings,
                                network_beliefs, gpu_lock, map_type_of(settings, g)),
            range(games)))
    played = [o for o in outcomes if o.won is not None]
    decided = [o for o in played if o.decided]
    captures = sum(o.won for o in decided)
    n = len(played)
    return {"games": n,
            "win_rate": round(sum(o.won for o in played) / n, 3) if n else None,
            "decided_percent": round(100 * len(decided) / n, 1) if n else None,
            "captured_percent": round(100 * captures / n, 1) if n else None,
            "was_captured_percent": round(100 * (len(decided) - captures) / n, 1) if n else None,
            "captures": captures, "was_captured": len(decided) - captures,
            "seconds": round(time.time() - start, 1),
            "stopped": len(outcomes) - len(played),
            "by_map": by_map(played),
            "differences": [asdict(o) for o in outcomes if o.won is None]}


def by_map(played: list[Outcome]) -> dict[str, dict[str, int]]:
    """Per map type, in the engine's order: games played to the end, won,
    and decided by a capture."""
    out = {}
    for t in MAP_TYPES:
        games = [o for o in played if o.map_type == t]
        if games:
            out[t] = {"games": len(games), "won": sum(o.won for o in games),
                      "decided": sum(o.decided for o in games)}
    return out


class CrazySeat:
    """The real game's Crazy bot as a seat of the terminal game
    (stocknavalon.play --crazy): a real game in lockstep with the terminal's
    (mirror.Lockstep), started from the real game's map. Every command a
    person or a bot plays goes to the real game first (before_command), the
    Crazy seat's turns come from its AI (turn), and the two games are
    compared on the way: the first rules difference is shown, and the game
    goes on unchecked from there."""

    def __init__(self, seat: int, tribes: list[str], seed: int, settings: Settings):
        args = argparse.Namespace(oracle=settings.oracle, build=settings.build,
                                  type=settings.type, size=settings.size,
                                  history=settings.history, save=settings.save)
        self.seat = seat
        self.lockstep = mirror.Lockstep(args, seed, tribes, ai_seats=(seat,))
        self.checking = True

    def start(self) -> pe.Game:
        return self.lockstep.game

    def _legal(self, game: pe.Game) -> list:
        step = self.lockstep
        step.finish_reward(game)
        legal = step.legal()
        if self.checking:
            try:
                step.check(game, legal)
            except mirror.Difference as d:
                self.checking = False
                print(f"rules check: {d.verdict} (not checked from here on)", flush=True)
                for line in d.detail:
                    print("    " + line, flush=True)
        return legal

    def before_command(self, game: pe.Game, command: pe.Command):
        legal = self._legal(game)
        try:
            k = self.lockstep.find(game, legal, command)
        except mirror.Difference as d:
            raise RuntimeError(f"the real game doesn't allow {game.describe(command)} here")
        self.lockstep.play(game, k, legal[k], command, apply_ours=False)

    def turn(self, game: pe.Game) -> list[pe.Command]:
        played = []
        while not game.game_over and game.current_player == self.seat:
            legal = self._legal(game)
            if self.lockstep.over(game):
                break
            k, chosen, ours = self.lockstep.ai_move(game, legal)
            self.lockstep.play(game, k, chosen, ours)
            played.append(ours)
        return played

    def close(self):
        self.lockstep.close()


def wilson(wins: int, games: int, z: float = 1.96) -> tuple[float, float]:
    """The Wilson score interval of a win rate."""
    if games == 0:
        return 0.0, 1.0
    p = wins / games
    centre = (p + z * z / (2 * games)) / (1 + z * z / games)
    half = z * math.sqrt(p * (1 - p) / games + z * z / (4 * games * games)) / (1 + z * z / games)
    return centre - half, centre + half


def report(g: int, o: Outcome, start: float, wins: int, losses: int,
           stopped: int) -> tuple[int, int, int]:
    """Prints one game's result; returns the counts with it."""
    if o.won is None:
        stopped += 1
    elif o.won:
        wins += 1
    else:
        losses += 1
    outcome = "stopped" if o.won is None else "won" if o.won else "lost"
    print(f"game {g}, {o.map_type}: network seat {o.seat} ({o.tribes[o.seat]} against "
          f"{o.tribes[1 - o.seat]}): {outcome} at turn {o.turns}, {o.verdict} "
          f"({time.time() - start:.0f} s in)", flush=True)
    if o.won is None:
        for line in o.detail:
            print("    " + line, flush=True)
    return wins, losses, stopped


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("checkpoint", help="a run directory (its best network) or a .pt file")
    parser.add_argument("--games", type=int, default=10)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--type", default="any", choices=["any"] + MAP_TYPES,
                        help="the map type, or any: each in turn by pair of games")
    parser.add_argument("--size", type=int, default=11)
    parser.add_argument("--max-turns", type=int, default=50)
    parser.add_argument("--parallel", type=int, default=1,
                        help="games at a time, an oracle each")
    parser.add_argument("--save", help="save the real state at each rules difference here")
    parser.add_argument("--device", default=None)
    parser.add_argument("--network-beliefs", action=argparse.BooleanOptionalAction,
                        default=True)
    parser.add_argument("--channels", type=int, default=64)
    parser.add_argument("--blocks", type=int, default=6)
    add_search_arguments(parser, simulations=200, samples=4, turn_search=["0"],
                         replies=["likely"])
    args = parser.parse_args()

    from .play import load
    device = torch.device(args.device) if args.device else pick_device()
    model = load(args.checkpoint, device, args.channels, args.blocks)
    search = side_searches(args, 1)[0]
    settings = Settings(type=args.type, size=args.size, max_turns=args.max_turns, save=args.save)
    wins = losses = stopped = 0
    start = time.time()

    def one(g: int) -> tuple[int, Outcome]:
        return g, play_game(model, device, search, args.seed + g, g % 2, settings,
                            network_beliefs=args.network_beliefs,
                            map_type=map_type_of(settings, g))

    with ThreadPoolExecutor(max_workers=max(1, args.parallel)) as pool:
        done = [pool.submit(one, g) for g in range(args.games)]
        finished = (f.result() for f in as_completed(done))
        outcomes = []
        for g, o in finished:
            wins, losses, stopped = report(g, o, start, wins, losses, stopped)
            outcomes.append(o)
    low, high = wilson(wins, wins + losses)
    print(f"against Crazy: {wins} won, {losses} lost ({100 * low:.0f}–{100 * high:.0f}% "
          f"win rate, 95%), {stopped} stopped by a rules difference")
    for t, d in by_map([o for o in outcomes if o.won is not None]).items():
        print(f"  {t:12s} won {d['won']} of {d['games']} ({d['decided']} decided)")


if __name__ == "__main__":
    main()
