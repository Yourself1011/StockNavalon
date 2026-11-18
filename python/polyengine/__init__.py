"""polyengine, a Polytopia engine, from Python.

The engine is C++; this is a thin wrapper. Calls cost about as much as a game
step, so anything that loops over many games or positions should be a single
call into C++ (like play_random_games) rather than a Python loop.

    import polyengine as pe

    tribes = [pe.Tribe.IMPERIUS, pe.Tribe.BARDUR]
    game = pe.Game(pe.generate_map(tribes, seed=1), tribes, seed=1)
    # there's no turn limit in the rules, so a game can go on forever
    while not game.game_over and game.turn < 100:
        commands = game.legal_commands()
        game.apply(commands[0])
"""

from ._engine import *  # noqa: F401,F403
from ._engine import __doc__  # noqa: F401


def configure(settings, **fields):
    """Sets these fields on a settings object (SearchConfig, SelfPlayConfig),
    refusing names it doesn't have; returns it."""
    for name, value in fields.items():
        if not hasattr(settings, name):
            raise TypeError(f"{type(settings).__name__} has no setting {name!r}")
        setattr(settings, name, value)
    return settings


def search_config(**fields) -> SearchConfig:  # noqa: F405
    """A SearchConfig with these fields set, the rest at their defaults."""
    return configure(SearchConfig(), **fields)  # noqa: F405


def self_play_config(**fields) -> SelfPlayConfig:  # noqa: F405
    """A SelfPlayConfig with these fields set, the rest at their defaults;
    `search` may be a SearchConfig or a dict of its fields, and `side_search`
    a list of them (None: the side uses `search`)."""
    settings = SelfPlayConfig()  # noqa: F405
    search = fields.pop("search", None)
    if isinstance(search, dict):
        search = configure(settings.search, **search)
    if search is not None:
        settings.search = search
    side = fields.pop("side_search", None)
    if side is not None:
        settings.side_search = [search_config(**s) if isinstance(s, dict) else s for s in side]
    return configure(settings, **fields)
