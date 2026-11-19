"""The search's settings as command-line flags, defined once for every tool
(run, play, versus): each registers them with its own defaults and builds a
polyengine.SearchConfig from the parsed arguments. A new search setting is a
field in SearchConfig (search.h), its binding, and a flag here."""

from __future__ import annotations

import argparse

import polyengine as pe

# the per-side choices' names on the command line
REPLIES = {"sampled": pe.Replies.SAMPLED, "likely": pe.Replies.LIKELY_FIRST,
           "discrepancy": pe.Replies.DISCREPANCY}
BACKUPS = {"mean": pe.Backup.MEAN, "pbar": pe.Backup.PBAR,
           "pbar-opponent": pe.Backup.PBAR_OPPONENT}


def add_search_arguments(parser: argparse.ArgumentParser, *, simulations: int,
                         samples: int, gumbel: bool = True, score_utility: float = 0.05,
                         win_sooner: float = 0.05, turn_search: list[str] | None = None,
                         replies: list[str] | None = None) -> None:
    """The search flags, with these defaults (the rest are SearchConfig's)."""
    defaults = pe.SearchConfig()
    g = parser.add_argument_group("search")
    g.add_argument("--simulations", type=int, default=simulations,
                   help="per determinized game")
    g.add_argument("--samples", type=int, default=samples,
                   help="determinized games per decision")
    g.add_argument("--horizon", type=int, default=None,
                   help="turn changes a search line may cross; by default no cutoff, "
                        "or 3 with --turn-search")
    g.add_argument("--exploration", type=float, default=defaults.exploration,
                   help="the exploration constant below the root: lower follows the "
                        "policy's favourites more closely")
    g.add_argument("--gumbel", action=argparse.BooleanOptionalAction, default=gumbel,
                   help="Gumbel search at the root (else PUCT)")
    g.add_argument("--gumbel-candidates", type=int, default=defaults.gumbel_candidates,
                   help="root moves Gumbel search considers")
    g.add_argument("--score-utility", type=float, default=score_utility,
                   help="tie-break toward a bigger predicted score lead")
    g.add_argument("--win-sooner", type=float, default=win_sooner,
                   help="tie-break toward a quicker win (or a slower loss) by the "
                        "predicted turns left")
    g.add_argument("--turn-search", nargs="+", default=turn_search, metavar="off|N",
                   help="turn search (notes/design.md section 9): past End Turn, whole "
                        "turns sampled from the policy; a node below the root searches "
                        "command by command once N simulations went through it (0: until "
                        "End Turn), off for today's search. One value for every side, or "
                        "one per side (or seat) where the tool has several")
    g.add_argument("--replies", nargs="+", default=replies, choices=list(REPLIES),
                   help="turn search: how the opponent's replies are drawn: sampled from "
                        "the policy, the likely one first then sampled, or the likely one "
                        "then discrepancy turns (search.h, SearchConfig::replies). One per "
                        "side, as --turn-search")
    g.add_argument("--backup", nargs="+", default=None, choices=list(BACKUPS),
                   help="how a node's value comes from its children: the mean of its "
                        "simulations, or weighted by the regularized policy everywhere or "
                        "where the opponent is to move (search.h, SearchConfig::backup). "
                        "One per side, as --turn-search")
    g.add_argument("--evaluations", type=int, default=None,
                   help="the budget per determinized game in network evaluations "
                        "instead of --simulations (a sampled turn costs one per "
                        "command), for searches at equal cost")


def turn_search_values(values: list[str] | None) -> list[int | None]:
    """--turn-search's values: each a commandVisits, or None for off."""
    return [None if v == "off" else int(v) for v in values or ["off"]]


def per_side(values: list | None, sides: int, flag: str) -> list:
    """A per-side flag's values, one for each of `sides` sides: one value for
    every side, or one per side (None for each when not given)."""
    if values is None:
        return [None] * sides
    if len(values) not in (1, sides):
        raise SystemExit(f"{flag}: one value, or one per side ({sides})")
    return list(values) * sides if len(values) == 1 else list(values)


def side_count(args: argparse.Namespace) -> int:
    """How many sides the per-side flags (--turn-search, --replies, --backup)
    give values for: 1 when each has one value or none."""
    return max(len(v) for v in (args.turn_search, args.replies, args.backup, [None]) if v)


def side_searches(args: argparse.Namespace, sides: int, **overrides) -> list[pe.SearchConfig]:
    """The search of each of `sides` sides (or seats): the flags
    (add_search_arguments) with the per-side ones (--turn-search, --replies,
    --backup) one value for every side or one per side, and any fields in
    `overrides` on top. Without --horizon, a turn search looks 3 turn changes
    ahead and today's search has no cutoff."""
    visits = per_side(turn_search_values(args.turn_search), sides, "--turn-search")
    replies = per_side(args.replies, sides, "--replies")
    backups = per_side(args.backup, sides, "--backup")
    out = []
    for side in range(sides):
        v = visits[side]
        horizon = (args.horizon if args.horizon is not None
                   else 3 if v is not None else pe.UNLIMITED_HORIZON)
        fields = dict(simulations=args.evaluations or args.simulations, samples=args.samples,
                      horizon=horizon, exploration=args.exploration, gumbel=args.gumbel,
                      gumbel_candidates=args.gumbel_candidates,
                      score_utility=args.score_utility, win_sooner=args.win_sooner,
                      turn_search=v is not None, command_visits=v or 0,
                      count_evaluations=args.evaluations is not None)
        if replies[side] is not None:
            fields["replies"] = REPLIES[replies[side]]
        if backups[side] is not None:
            fields["backup"] = BACKUPS[backups[side]]
        fields.update(overrides)
        out.append(pe.search_config(**fields))
    return out


def search_config(args: argparse.Namespace, **overrides) -> pe.SearchConfig:
    """The SearchConfig the flags (add_search_arguments) describe, for a tool
    with one search, with any fields in `overrides` set on top."""
    return side_searches(args, 1, **overrides)[0]


def modified(search: pe.SearchConfig, **fields) -> pe.SearchConfig:
    """A copy of `search` with these fields changed."""
    return pe.configure(search.copy(), **fields)
