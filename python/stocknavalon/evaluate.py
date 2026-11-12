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
import time
from typing import Any

import numpy as np
import torch

import polyengine as pe

from .model import Network, as_input, belief_outputs


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
          simulations: int = 50, samples: int = 2, max_turns: int = 50,
          stall_turns: int = 0, fog_share: float = 1.0,
          network_beliefs: bool = False, peace_treaties: bool = True,
          threads: int = 1, settled=None,
          gpu_lock=None, progress=None, progress_seconds: float = 30.0,
          finish_share: float = 0.9, extra_moves: int = 50,
          max_minutes: float = 10.0,
          keep: list[pe.GameRecord] | None = None) -> dict[str, Any]:
    """`model` (side 0) against `opponent` (side 1): another Network, or
    "score" or "random". Returns side 0's results. `progress`, if given, is
    called every `progress_seconds` with the results so far."""
    gpu_lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
    networks: list[Network | None] = [
        model, opponent if isinstance(opponent, Network) else None]
    sides: list[str] = ["network",
                        "network" if isinstance(opponent, Network) else str(opponent)]
    for net in networks:
        if net is not None:
            net.eval()
    games += games % 2  # whole pairs
    session = pe.SelfPlaySession(
        parallel, games, seed=seed, simulations=simulations, samples=samples,
        max_turns=max_turns, temperature_turns=1, root_noise=False,
        full_share=1.0, sides=sides, paired=True, stall_turns=stall_turns,
        fog_share=fog_share, network_beliefs=network_beliefs,
        peace_treaties=peace_treaties, threads=threads)

    start = reported = time.perf_counter()
    records: list[pe.GameRecord] = []
    cut_at: int | None = None
    while not session.done():
        now = time.perf_counter()
        if progress is not None and now - reported >= progress_seconds:
            reported = now
            progress({**score_games(records), "games": len(records),
                      "of": games, "seconds": round(now - start)})
        planes, globals_, _ = session.pending_inputs(encoding, legal=False)
        b = len(planes)
        if b == 0:
            continue
        rows, slots = session.pending_slots(encoding)
        side_of = session.pending_sides()
        values = np.zeros(b, np.float32)
        move_logits = np.zeros(len(slots), np.float32)
        # the positions that want their network's beliefs, in order
        belief_rows = session.pending_belief_rows()
        beliefs = (np.zeros((len(belief_rows), encoding.belief_output_count), np.float32)
                   if len(belief_rows) else None)
        for side in (0, 1):
            idx = np.nonzero(side_of == side)[0]
            if len(idx) == 0:
                continue
            # this side's positions, and its moves among all the moves
            local = np.full(b, -1, np.int64)
            local[idx] = np.arange(len(idx))
            moves = np.nonzero(side_of[rows] == side)[0]
            network = networks[side]
            assert network is not None  # only network sides' positions are pending
            with gpu_lock:
                out = network(as_input(planes[idx], network, device),
                              as_input(globals_[idx], network, device))
                picked = out["policy"][torch.from_numpy(local[rows[moves]]).to(device),
                                       torch.from_numpy(slots[moves]).to(device)]
                move_logits[moves] = picked.float().cpu().numpy()
                values[idx] = out["value"].float().cpu().numpy()
                mine = np.nonzero(side_of[belief_rows] == side)[0]
                if len(mine):
                    beliefs[mine] = belief_outputs(
                        out, torch.from_numpy(local[belief_rows[mine]]).to(device),
                        encoding).float().cpu().numpy()
        session.submit_move_logits(encoding, move_logits, values, beliefs)
        records.extend(session.take_finished())
        # stop once the result can't change (`settled`: the games so far,
        # and how many are still to finish)
        if settled is not None and settled(records, games - len(records)):
            session.finish_now()
            records.extend(session.take_finished())
            break
        # the stragglers: a few more moves once most games are done, and
        # ended at once past the time limit
        if cut_at is None and len(records) >= finish_share * games:
            cut_at = len(records)
            session.finish_after(extra_moves)
        if time.perf_counter() - start >= max_minutes * 60:
            cut_at = len(records) if cut_at is None else cut_at
            session.finish_now()
            records.extend(session.take_finished())
            break

    records.extend(session.take_finished())
    result = score_games(records)
    # games that hadn't finished when the cut came (not stalemates)
    result["cut_short"] = 0 if cut_at is None else sum(
        1 for r in records[cut_at:]
        if r.winner < 0 and not r.stalled and r.turns < max_turns)
    result["seconds"] = round(time.perf_counter() - start)
    if keep is not None:
        keep.extend(records)
    return result


def gate_settled(threshold: float = 0.55, min_decided: int = 20):
    """For a gate's match: True once its verdict can't change, whatever the
    games still going do (each could still be won, lost or undecided)."""
    def settled(records: list[pe.GameRecord], remaining: int) -> bool:
        result = score_games(records)
        wins, losses = result["captures"], result["was_captured"]
        if wins + losses + remaining < min_decided:
            return True  # inconclusive whatever happens
        best = (wins + remaining) / (wins + losses + remaining)
        worst = wins / (wins + losses + remaining) if wins + losses + remaining else 0.0
        # out of reach, or already certain
        return best < threshold or (wins + losses >= min_decided and worst >= threshold)
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
