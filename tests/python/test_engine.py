import copy

import pytest
import polyengine as pe

TRIBES = [pe.Tribe.IMPERIUS, pe.Tribe.BARDUR]


def new_game(seed=1):
    return pe.Game(pe.generate_map(TRIBES, seed=seed), TRIBES, seed=seed)


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
    assert pe.Command(pe.CommandType.ENDTURN) in commands
    assert all(game.is_legal(c) for c in commands)

    research = pe.Command(pe.CommandType.RESEARCH, arg=pe.TechType.RIDING)
    assert research in commands
    game.apply(research)
    assert pe.TechType.RIDING in game.players[0].techs
    assert game.players[0].stars == 0
    assert game.describe(research) == "research riding"

    with pytest.raises(ValueError):
        game.apply(research)


def test_copies_are_independent():
    game = new_game()
    branch = game.copy()
    branch.apply(pe.Command(pe.CommandType.ENDTURN))
    assert branch.current_player == 1
    assert game.current_player == 0
    assert copy.deepcopy(game).current_player == 0


def test_the_view_hides_what_the_player_doesnt_know():
    game = new_game()
    view = pe.make_view(game, 0)
    assert view.player == 0
    assert view.stars == 5
    explored = sum(t.explored for t in view.tiles)
    assert explored == 25
    # the enemy capital is still in fog
    assert all(c.owner == 0 for c in view.cities)
    # scores are public, even for a tribe not yet met; its tribe isn't
    assert view.scores == [game.score(0), game.score(1)]
    assert not view.met[1]
    assert view.tribes == [pe.Tribe.IMPERIUS, None]


def test_others_see_a_score_when_its_player_ends_their_turn():
    game = new_game()
    start = game.score(0)
    game.apply(pe.Command(pe.CommandType.RESEARCH, arg=pe.TechType.RIDING))
    assert pe.make_view(game, 0).scores[0] == start + 100
    assert pe.make_view(game, 1).scores[0] == start
    game.apply(pe.Command(pe.CommandType.ENDTURN))
    assert pe.make_view(game, 1).scores[0] == start + 100
    assert game.published_score(0) == start + 100
    last = pe.make_view(game, 1).score_history[-1]
    assert (last.player, last.turn, last.score) == (0, 0, start + 100)
    everything = pe.make_view(game, 0, everything=True)
    assert len(everything.cities) == 2


def test_legal_moves_track_the_game():
    game = new_game()
    legal = pe.LegalMoves(game)
    bot = pe.RandomBot(3)
    for _ in range(300):
        if game.game_over:
            break
        command = bot.choose(game)
        legal.apply(game, command)
        assert set(legal.commands) == set(game.legal_commands())


def test_maps():
    for map_type in pe.MapType.__members__.values():
        game_map = pe.generate_map(TRIBES, type=map_type, size=11, seed=2,
                                   candidates=1)
        assert game_map.size == 11
        assert len(game_map.capitals) == 2
    parsed = pe.parse_map("3 1\nF0 F F\nF F F\nF F F1\n")
    assert parsed.capitals == [0, 8]


def test_play_random_games():
    results = pe.play_random_games(4, TRIBES, max_turns=20, seed=5)
    assert len(results) == 4
    for winner, turns in results:
        assert winner in (-1, 0, 1)
        assert turns <= 20


def test_under_ctest_the_build_tree_module_is_tested():
    package = __import__("os").environ.get("POLYENGINE_BUILD_PACKAGE")
    if package:
        assert pe.__file__.startswith(package)


def test_encoding_shapes_and_round_trip():
    import numpy as np

    enc = pe.Encoding(map_size=11, players=2)
    assert (enc.plane_count, enc.global_count, enc.action_count) == (100, 133, 26410)
    tribes = [pe.Tribe.IMPERIUS, pe.Tribe.BARDUR]
    game = pe.Game(pe.generate_map(tribes, size=11, seed=3), tribes, seed=3)

    planes, globals_ = enc.encode_state(game, 0)
    assert planes.shape == (100, 11, 11) and planes.dtype == np.float32
    assert globals_.shape == (133,) and globals_.dtype == np.float32
    assert planes[0].sum() == 121  # every tile is on the map

    mask = enc.legal_mask(game)
    commands = game.legal_commands()
    assert mask.dtype == np.uint8 and mask.sum() == len(commands)
    for command in commands:
        index = enc.encode_command(game, command)
        assert mask[index] == 1
        assert enc.decode_command(game, index) == command

    games = [game, game.copy()]
    batch_planes, batch_globals = enc.encode_states(games, [0, 1])
    assert batch_planes.shape == (2, 100, 11, 11)
    assert batch_globals.shape == (2, 133)
    assert np.array_equal(batch_planes[0], planes)
    assert enc.legal_masks(games).shape == (2, 26410)


def test_climate_and_possible_tribes():
    tribes = [pe.Tribe.VENGIR, pe.Tribe.AIMO]
    game = pe.Game(pe.generate_map(tribes, size=11, seed=4), tribes, seed=4)
    view = pe.make_view(game, 0)
    capital = game.cities[game.players[0].capital].tile
    assert view.tiles[capital].climate == pe.Tribe.VENGIR
    assert pe.tribes_with_starting_score(730) == [pe.Tribe.VENGIR, pe.Tribe.AIMO]
    possible = pe.possible_tribes(view, 1)
    assert pe.Tribe.AIMO in possible
    assert set(possible) <= {pe.Tribe.VENGIR, pe.Tribe.AIMO}
    assert pe.Tribe.VENGIR in pe.climates_seen(view)
    # the score search's limits are options, and the evidence it reads is
    # in the view
    assert pe.Tribe.AIMO in pe.possible_tribes(view, 1, turns=0)
    assert pe.score_changes_fit(view, 1, pe.Tribe.AIMO, turns=2, max_work=1000)
    assert len(view.effects_in_changes) == len(view.score_history)
    assert view.first_occupied[capital] == 0
    assert view.attacked_on_turns == []
    assert pe.starting_explored(pe.Tribe.AIMO, view.scores[1]) in range(9, 26)
    assert pe.OPENING_CHANGES == 6
