"""The stockNavalon Polytopia engine, from Python.

The engine is C++; this is a thin wrapper. Calls cost about as much as a game
step, so anything that loops over many games or positions should be a single
call into C++ (like play_random_games) rather than a Python loop.

    import stocknavalon as sn

    tribes = [sn.Tribe.IMPERIUS, sn.Tribe.BARDUR]
    game = sn.Game(sn.generate_map(tribes, seed=1), tribes, seed=1)
    while not game.game_over:
        commands = game.legal_commands()
        game.apply(commands[0])
"""

from ._engine import *  # noqa: F401,F403
from ._engine import __doc__  # noqa: F401
