"""Evaluation matches (docs/stockNavalon.md section 6): a network against
another network, the score-evaluator search, or the random bot. Gating plays
them too, judged on decided games alone (`gate_verdict`).

Games come in pairs, the same map and tribes twice with the sides swapping
seats, so neither the map nor moving first favours a side. Both sides search
the same way, without root noise; the first turn picks moves in proportion to
visits for variety, then the most visited. A game cut off by the turn limit
goes to the side ahead on score.

Game lengths are heavy-tailed, so a match doesn't wait for its last few: once
most games are done (`finish_share`), each game still going gets
`extra_moves` more commands to finish, then is ended and scored by the lead,
like games at the turn limit. After `max_minutes` the rest end at once.
"""

from __future__ import annotations

import contextlib
import sys
import time
from typing import Any

import numpy as np
import torch

import polyengine as pe

from .model import Network
from .selfplay import alternate


def score_games(records: list[pe.GameRecord]) -> dict[str, Any]:
    """Side 0's results: wins (a draw on equal scores counts half), the
    percentage of games a capture decided, and of those the ones side 0 won
    (captured) and lost (was captured). Games without a capture go to the
    score lead, which says little against the random bot: spending every star
    every turn and wandering about scores well."""
    points = decided = captured = 0.0
    for r in records:
        seat = 0 if r.seat_zero_side == 0 else 1  # side 0's seat
        if r.winner >= 0:
            decided += 1
            captured += r.winner == seat
            points += 1.0 if r.winner == seat else 0.0
        else:
            lead = r.final_scores[seat] - r.final_scores[1 - seat]
            points += 1.0 if lead > 0 else 0.5 if lead == 0 else 0.0
    n = len(records)
    return {"games": n, "win_rate": round(points / n, 3) if n else None,
            "decided_percent": round(100 * decided / n, 1) if n else None,
            "captured_percent": round(100 * captured / n, 1) if n else None,
            "was_captured_percent": round(100 * (decided - captured) / n, 1) if n else None,
            # the counts, for gating, which judges on decided games alone
            "captures": int(captured), "was_captured": int(decided - captured)}


@torch.no_grad()
def match(model: Network, opponent, encoding: pe.Encoding, device: torch.device,
          games: int = 64, parallel: int = 32, seed: int = 0,
          search: pe.SearchConfig | None = None,
          side_search: list[pe.SearchConfig | None] | None = None,
          max_turns: int = 50, stall_turns: int = 0, fog_share: float = 1.0,
          network_beliefs: bool = False, peace_treaties: bool = True,
          threads: int = 1, train_sides: tuple[bool, bool] = (True, True), settled=None,
          gpu_lock=None, progress=None, progress_seconds: float = 30.0,
          finish_share: float = 0.9, extra_moves: int = 50,
          max_minutes: float = 10.0, sessions: int = 1,
          plans: tuple[str, str] = ("none", "none"), plan_turns: int = 20,
          map_types: list | None = None,
          keep: list[pe.GameRecord] | None = None) -> dict[str, Any]:
    """`model` (side 0) against `opponent` (side 1): another Network, or
    "score" or "random". Both search with `search` (by default 50 simulations
    in 2 determinized games), or each with its own `side_search` (None: `search`),
    so the same network can play one search against another; matches search
    without root noise. With `train_sides`, only those sides' moves get policy
    targets (a gate's games train the candidate only; the records mark the
    other side's seat, as opponent-pool games do). Returns side 0's results.
    `progress`, if given, is
    called every `progress_seconds` with the results so far. `plans` forces
    each side's opening (plans.h: none, bridge or naval) before `plan_turns`;
    `map_types` (polyengine.MapType) limits the maps. With `sessions`
    2, the games are split between two sessions taking turns, so the CPU and
    the GPU overlap as in self-play (the same games for a seed); one by
    default: with a network per side that's four small batches a round
    instead of two, and gates during a run (the GPU shared) slowed down
    enough to run out of time."""
    gpu_lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
    networks: list[Network | None] = [
        model, opponent if isinstance(opponent, Network) else None]
    sides: list[str] = ["network",
                        "network" if isinstance(opponent, Network) else str(opponent)]
    for net in networks:
        if net is not None:
            net.eval()
    games += games % 2  # whole pairs

    def quiet(s: pe.SearchConfig) -> pe.SearchConfig:
        s = s.copy()
        s.root_noise = False
        return s

    config = pe.self_play_config(
        search=quiet(search if search is not None else pe.search_config(simulations=50, samples=2)),
        side_search=[None if s is None else quiet(s) for s in side_search or []],
        max_turns=max_turns, temperature_turns=1, full_share=1.0, sides=sides, paired=True,
        stall_turns=stall_turns, fog_share=fog_share, network_beliefs=network_beliefs,
        peace_treaties=peace_treaties, threads=threads, train_sides=list(train_sides),
        side_plans=list(plans), plan_turns=plan_turns, map_types=list(map_types or []))
    # with two sessions, the games split so they're the same as one
    # session's
    first = games // 4 * 2 if sessions == 2 else games  # whole pairs in each
    shares = [(n, start) for n, start in ((first, 0), (games - first, first)) if n > 0]
    sessions = [pe.SelfPlaySession(config, max(1, min(parallel, n)), n, seed, first=start)
                for n, start in shares]

    def take_finished():
        for session in sessions:
            records.extend(session.take_finished())

    def finish_now():
        for session in sessions:
            session.finish_now()
        take_finished()

    start = reported = time.perf_counter()
    records: list[pe.GameRecord] = []
    cut_at: int | None = None
    for session, _ in alternate(sessions, networks, encoding, device, gpu_lock,
                                lambda: not all(s.done() for s in sessions)):
        now = time.perf_counter()
        if progress is not None and now - reported >= progress_seconds:
            reported = now
            progress({**score_games(records), "games": len(records),
                      "of": games, "seconds": round(now - start)})
        records.extend(session.take_finished())
        # stop once the result can't change (`settled`: the games so far,
        # and how many are still to finish)
        if settled is not None and settled(records, games - len(records)):
            finish_now()
            break
        # the stragglers: a few more moves once most games are done, and
        # ended at once past the time limit
        if cut_at is None and len(records) >= finish_share * games:
            cut_at = len(records)
            for s in sessions:
                s.finish_after(extra_moves)
        if time.perf_counter() - start >= max_minutes * 60:
            cut_at = len(records) if cut_at is None else cut_at
            finish_now()
            break

    take_finished()
    result = score_games(records)
    # games that hadn't finished when the cut came (not stalemates)
    result["cut_short"] = 0 if cut_at is None else sum(
        1 for r in records[cut_at:]
        if r.winner < 0 and not r.stalled and r.turns < max_turns)
    result["seconds"] = round(time.perf_counter() - start)
    # how deep each side's searches went, over both sessions
    result["depth"] = combined_depth([s.depth_stats() for s in sessions])
    if keep is not None:
        keep.extend(records)
    return result


def combined_depth(stats: list[list[dict[str, float]]]) -> list[dict[str, float]]:
    """Sessions' depth_stats as one: per side, the counts summed and the
    means weighted by simulations."""
    out = []
    for sides in zip(*stats):
        simulations = sum(d["simulations"] for d in sides)
        side = {"simulations": simulations,
                "evaluations": sum(d["evaluations"] for d in sides)}
        for key in ("mean_depth", "reached_opponent", "mean_turns"):
            side[key] = (sum(d[key] * d["simulations"] for d in sides) / simulations
                         if simulations else 0.0)
        out.append(side)
    return out


def verdict_settled(wins: int, losses: int, remaining: int, threshold: float = 0.55,
                    min_decided: int = 20) -> bool:
    """Whether a gate's verdict can't change, from its decided games so far
    and the games still going (each could still be won, lost or undecided)."""
    if wins + losses + remaining < min_decided:
        return True  # inconclusive whatever happens
    best = (wins + remaining) / (wins + losses + remaining)
    worst = wins / (wins + losses + remaining) if wins + losses + remaining else 0.0
    # out of reach, or already certain
    return best < threshold or (wins + losses >= min_decided and worst >= threshold)


def progress_line(p: dict[str, Any]) -> str:
    """A match's progress (match's `progress` callback) as a line: games
    finished, time so far, a rough time left from the pace so far, and side
    0's decided games. Rough: the quick games finish first, though the
    slowest are cut short (`finish_share`, `extra_moves`)."""
    done, of, seconds = p["games"], p["of"], p["seconds"]
    left = f", about {seconds / done * (of - done) / 60:.0f} min left" if done else ""
    return (f"  {done} of {of} games done, {seconds / 60:.0f} min{left}; side A "
            f"{p['captures']}–{p['was_captured']} of the decided games")


def show_progress(p: dict[str, Any]) -> None:
    """progress_line, written over itself in a terminal (clear_progress
    before printing anything else); a line each time otherwise."""
    if sys.stdout.isatty():
        # back to the line's start, the line, then clear what's left of it
        print(f"\r{progress_line(p)}\033[K", end="", flush=True)
    else:
        print(progress_line(p), flush=True)


def clear_progress() -> None:
    """Clears show_progress's line, in a terminal."""
    if sys.stdout.isatty():
        print("\r\033[K", end="", flush=True)


def gate_settled(threshold: float = 0.55, min_decided: int = 20):
    """For a gate's match: True once its verdict can't change (verdict_settled)."""
    def settled(records: list[pe.GameRecord], remaining: int) -> bool:
        result = score_games(records)
        return verdict_settled(result["captures"], result["was_captured"], remaining,
                               threshold, min_decided)
    return settled


def gate_verdict(result: dict[str, Any], threshold: float = 0.55,
                 min_decided: int = 20) -> str:
    """Whether a candidate (side 0) beat the best network: judged on the
    games a capture decided, since games cut off by the turn limit say
    nothing about winning. "pass", "fail", or "inconclusive" with fewer than
    `min_decided` decided games."""
    wins, losses = result["captures"], result["was_captured"]
    if wins + losses < max(1, min_decided):
        return "inconclusive"
    return "pass" if wins / (wins + losses) >= threshold else "fail"
