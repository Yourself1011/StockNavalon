import copy

import pytest
import stocknavalon as sn

TRIBES = [sn.Tribe.IMPERIUS, sn.Tribe.BARDUR]


def new_game(seed=1):
    return sn.Game(sn.generate_map(TRIBES, seed=seed), TRIBES, seed=seed)


def test_a_new_game():
    game = new_game()
    assert game.size == 14
    assert game.turn == 0
    assert game.current_player == 0
    assert not game.game_over
    assert [p.tribe for p in game.players] == TRIBES
    assert game.players[0].stars == 5
    assert game.score(0) == 515
    assert len(game.cities) == 2
    assert len(game.units) == 2


def test_legal_commands_and_apply():
    game = new_game()
    commands = game.legal_commands()
    assert sn.Command(sn.CommandType.ENDTURN) in commands
    assert all(game.is_legal(c) for c in commands)

    research = sn.Command(sn.CommandType.RESEARCH, arg=sn.TechType.RIDING)
    assert research in commands
    game.apply(research)
    assert sn.TechType.RIDING in game.players[0].techs
    assert game.players[0].stars == 0
    assert game.describe(research) == "research riding"

    with pytest.raises(ValueError):
        game.apply(research)


def test_copies_are_independent():
    game = new_game()
    branch = game.copy()
    branch.apply(sn.Command(sn.CommandType.ENDTURN))
    assert branch.current_player == 1
    assert game.current_player == 0
    assert copy.deepcopy(game).current_player == 0


def test_the_view_hides_what_the_player_doesnt_know():
    game = new_game()
    view = sn.make_view(game, 0)
    assert view.player == 0
    assert view.stars == 5
    explored = sum(t.explored for t in view.tiles)
    assert explored == 25
    # the enemy capital is still in fog
    assert all(c.owner == 0 for c in view.cities)
    assert view.scores[1] == -1
    everything = sn.make_view(game, 0, everything=True)
    assert len(everything.cities) == 2


def test_legal_moves_track_the_game():
    game = new_game()
    legal = sn.LegalMoves(game)
    bot = sn.RandomBot(3)
    for _ in range(300):
        if game.game_over:
            break
        command = bot.choose(game)
        legal.apply(game, command)
        assert set(legal.commands) == set(game.legal_commands())


def test_maps():
    for map_type in sn.MapType.__members__.values():
        game_map = sn.generate_map(TRIBES, type=map_type, size=11, seed=2,
                                   candidates=1)
        assert game_map.size == 11
        assert len(game_map.capitals) == 2
    parsed = sn.parse_map("3 1\nF0 F F\nF F F\nF F F1\n")
    assert parsed.capitals == [0, 8]


def test_play_random_games():
    results = sn.play_random_games(4, TRIBES, max_turns=20, seed=5)
    assert len(results) == 4
    for winner, turns in results:
        assert winner in (-1, 0, 1)
        assert turns <= 20


def test_under_ctest_the_build_tree_module_is_tested():
    package = __import__("os").environ.get("STOCKNAVALON_BUILD_PACKAGE")
    if package:
        assert sn.__file__.startswith(package)
