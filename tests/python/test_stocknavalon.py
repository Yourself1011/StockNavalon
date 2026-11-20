import argparse
import contextlib
import math
import os

import numpy as np

import pytest

torch = pytest.importorskip("torch")

import polyengine as pe
import stocknavalon as sn
from stocknavalon.train import losses, to_device


@pytest.fixture(scope="module")
def buffer():
    games = pe.play_games(pe.self_play_config(search=dict(simulations=20), max_turns=4), 4,
                          seed=2, threads=2)
    buf = pe.ReplayBuffer(100_000)
    buf.add(games)
    return buf


def test_network_shapes():
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2)
    planes = torch.zeros(3, enc.plane_count, 11, 11)
    globals_ = torch.zeros(3, enc.global_count)
    out = model(planes, globals_)
    assert out["policy"].shape == (3, enc.action_count)
    assert out["value"].shape == (3,)
    assert out["hidden_unit_here"].shape == (3, 1, 121)


def test_weights_saved_before_the_global_vector_grew_load_and_play_the_same():
    from stocknavalon.model import load_weights
    enc = pe.Encoding()
    torch.manual_seed(0)
    old = sn.Network(enc, channels=16, blocks=2).eval()
    new_inputs = pe.MAP_TYPE_COUNT
    with torch.no_grad():
        old.globals.weight[:, -new_inputs:] = 0
    # saved before the map type was added: fewer global inputs
    saved = old.state_dict()
    saved["globals.weight"] = saved["globals.weight"][:, :-new_inputs].clone()
    model = sn.Network(enc, channels=16, blocks=2).eval()
    assert load_weights(model, saved) == ["globals.weight"]
    planes = torch.randn(2, enc.plane_count, 11, 11)
    globals_ = torch.randn(2, enc.global_count)
    with torch.no_grad():
        a, b = old(planes, globals_), model(planes, globals_)
    assert torch.equal(a["policy"], b["policy"]) and torch.equal(a["value"], b["value"])


def test_weights_saved_before_the_shore_planes_load_and_play_the_same():
    from stocknavalon.model import load_weights
    enc = pe.Encoding()
    torch.manual_seed(1)
    old = sn.Network(enc, channels=16, blocks=2).eval()
    with torch.no_grad():
        old.stem.weight[:, -2:] = 0
    saved = old.state_dict()
    saved["stem.weight"] = saved["stem.weight"][:, :-2].clone()
    model = sn.Network(enc, channels=16, blocks=2).eval()
    assert load_weights(model, saved) == ["stem.weight"]
    planes = torch.randn(2, enc.plane_count, 11, 11)
    globals_ = torch.randn(2, enc.global_count)
    with torch.no_grad():
        a, b = old(planes, globals_), model(planes, globals_)
    assert torch.equal(a["policy"], b["policy"]) and torch.equal(a["value"], b["value"])


def test_weights_saved_before_aquacrop_load_and_play_the_same():
    # encoding version 16: one more input plane (last), and one more
    # resource in the unexplored head, whose old weights are dropped
    from stocknavalon.model import load_weights
    enc = pe.Encoding()
    torch.manual_seed(2)
    old = sn.Network(enc, channels=16, blocks=2).eval()
    with torch.no_grad():
        old.stem.weight[:, -1:] = 0
    saved = old.state_dict()
    saved["stem.weight"] = saved["stem.weight"][:, :-1].clone()
    for key in ("unexplored.weight", "unexplored.bias"):
        saved[key] = saved[key][:-1].clone()
    model = sn.Network(enc, channels=16, blocks=2).eval()
    assert sorted(load_weights(model, saved)) == ["stem.weight", "unexplored.bias",
                                                  "unexplored.weight"]
    planes = torch.randn(2, enc.plane_count, 11, 11)
    globals_ = torch.randn(2, enc.global_count)
    with torch.no_grad():
        a, b = old(planes, globals_), model(planes, globals_)
    assert torch.equal(a["policy"], b["policy"]) and torch.equal(a["value"], b["value"])


def test_losses_are_finite_and_fall_on_a_fixed_batch(buffer):
    torch.manual_seed(0)
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2)
    batch = to_device(buffer.encode(enc, buffer.sample(32, 1)), torch.device("cpu"))
    optimizer = torch.optim.Adam(model.parameters(), lr=3e-3)
    totals = []
    for _ in range(40):
        result = losses(model, model(batch["planes"], batch["globals"]), batch)
        for name, value in result.items():
            assert math.isfinite(value.item()), name
        optimizer.zero_grad()
        result["total"].backward()
        optimizer.step()
        totals.append(result["total"].item())
    assert totals[-1] < totals[0]


def test_trainer_and_network_self_play(buffer):
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2)
    trainer = sn.Trainer(model, enc, torch.device("cpu"))
    stats = trainer.step(buffer, 16)
    assert math.isfinite(stats["total"])
    records, info = sn.play(model, enc, torch.device("cpu"), 2,
                            pe.self_play_config(search=dict(simulations=10), max_turns=2),
                            parallel=2)
    # games finishing in the same batch all come back
    assert len(records) >= 2 and info["evaluated"] > 0
    # games the turn limit cuts off keep each seat's last network value, the
    # value target in place of a draw
    for r in records:
        if r.winner < 0:
            assert len(r.final_values) == 2
            assert all(-1 <= v <= 1 for v in r.final_values)
            assert r.value(0) == pytest.approx(r.final_values[0])


def test_gate_judges_on_decided_games_only():
    from stocknavalon.evaluate import gate_verdict
    # games cut off by the turn limit count for neither side
    assert gate_verdict({"captures": 12, "was_captured": 8}) == "pass"
    assert gate_verdict({"captures": 10, "was_captured": 10}) == "fail"
    assert gate_verdict({"captures": 9, "was_captured": 1}) == "inconclusive"
    assert gate_verdict({"captures": 0, "was_captured": 0}, min_decided=0) == "inconclusive"


def test_side_targets_one_territory_class_per_tile_under_symmetries(buffer):
    import numpy as np
    enc = pe.Encoding()
    n2 = enc.map_size ** 2
    for augment in (False, True):
        x = buffer.sample_examples(enc, 32, 5, augment=augment)
        assert x["side_targets"].shape == (32, enc.side_target_count)
        t = x["side_targets"][:, pe.SIDE_TERRITORY:].reshape(32, pe.TERRITORY_CLASSES, n2)
        m = x["side_masks"][:, pe.SIDE_TERRITORY:].reshape(32, pe.TERRITORY_CLASSES, n2)[:, 0]
        # every map tile has exactly one class; the masks cover the map
        assert np.all(t.sum(axis=1)[m == 1] == 1)
        assert m.sum() == 32 * n2
        assert np.all(np.abs(x["side_targets"][:, :2]) <= 1)
    # the side losses are there and finite
    model = sn.Network(enc, channels=16, blocks=2)
    batch = to_device(x, torch.device("cpu"))
    result = losses(model, model(batch["planes"], batch["globals"]), batch)
    for name in ("score_soon", "score_end", "territory"):
        assert math.isfinite(result[name].item())


def test_fog_free_self_play_trains_without_belief_targets():
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2)
    records, _ = sn.play(model, enc, torch.device("cpu"), 2,
                         pe.self_play_config(search=dict(simulations=6), max_turns=3,
                                             fog_share=0.0), parallel=2)
    assert all(not r.fog for r in records)
    buf = pe.ReplayBuffer(10_000)
    buf.add(records)
    x = buf.sample_examples(enc, 8, 1)
    # nothing hidden, so no belief head trains on these
    assert x["target_masks"].sum() == 0


def test_fog_schedule_starts_once_ramps_by_steps_and_only_pauses():
    from stocknavalon.fog import FogSchedule
    fog = FogSchedule(trigger=0.5, ramp_steps=1000, brake_failures=2)
    # no fog until the trigger, which needs a full window of games
    assert fog.update(100, 0.9, window_full=False) is None
    assert fog.update(200, 0.3, window_full=True) is None
    assert fog.share(200) == 0
    assert fog.update(300, 0.6, window_full=True) == "start"
    assert fog.share(800) == pytest.approx(0.5)
    # once started, play no longer matters: it never goes back
    fog.update(800, 0.0, window_full=True)
    assert fog.share(800) == pytest.approx(0.5)
    # two failed gates pause it; steps while paused don't count
    fog.gate("fail")
    fog.gate("inconclusive")
    fog.gate("fail")
    assert fog.braking(800)
    fog.update(1000, 0.0, window_full=True)
    assert fog.share(1000) == pytest.approx(0.5)
    # a pass resumes it from where it paused
    fog.gate("pass")
    fog.update(1100, 0.0, window_full=True)
    assert fog.share(1100) == pytest.approx(0.6)
    assert fog.share(10_000) == 1.0
    # and it survives a restart through its state
    again = FogSchedule(ramp_steps=1000)
    for key in ("start_step", "paused_steps", "failures", "last_step"):
        setattr(again, key, fog.state()[key])
    assert again.share(1100) == pytest.approx(0.6)


def test_network_beliefs_in_fogged_self_play_and_matches():
    from stocknavalon.evaluate import match
    from stocknavalon.model import belief_outputs
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2)
    # the raw outputs fill the layout the engine decodes
    out = model(torch.zeros(2, enc.plane_count, 11, 11), torch.zeros(2, enc.global_count))
    raw = belief_outputs(out, torch.tensor([1]), enc)
    assert raw.shape == (1, enc.belief_output_count)
    records, info = sn.play(model, enc, torch.device("cpu"), 2,
                            pe.self_play_config(search=dict(simulations=6), max_turns=3,
                                                network_beliefs=True), parallel=2)
    assert len(records) >= 2 and info["evaluated"] > 0
    result = match(model, sn.Network(enc, channels=16, blocks=2), enc,
                   torch.device("cpu"), games=2, parallel=2,
                   search=pe.search_config(simulations=4, samples=2),
                   max_turns=2, network_beliefs=True)
    assert result["games"] == 2


def test_a_match_splits_its_games_over_two_sessions():
    from stocknavalon.evaluate import match
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=1)
    keep: list = []
    result = match(model, sn.Network(enc, channels=16, blocks=1), enc, torch.device("cpu"),
                   games=8, parallel=8, search=pe.search_config(simulations=4, samples=1),
                   max_turns=2, sessions=2, keep=keep)
    assert result["games"] == 8
    # every game once: four maps, each played from both seats
    assert len({(r.game_seed, r.seat_zero_side) for r in keep}) == 8
    assert len({r.game_seed for r in keep}) == 4
    # both sessions' searches count
    assert all(d["simulations"] > 0 and d["mean_depth"] > 0 for d in result["depth"])


def test_learning_rate_warms_up_when_fresh_then_cosines_to_a_floor():
    from stocknavalon.train import learning_rate as lr
    peak, floor = 2e-4, 5e-5
    # a fresh optimizer starts near 0 and reaches the peak after the warm-up
    assert lr(0, 0, peak, floor, 100_000, 1000) < 1e-6
    assert lr(999, 999, peak, floor, 100_000, 1000) == pytest.approx(peak, rel=1e-3)
    # a loaded one doesn't warm up
    assert lr(0, None, peak, floor, 100_000, 1000) == pytest.approx(peak)
    # halfway down, then held at the floor
    assert lr(50_000, None, peak, floor, 100_000, 1000) == pytest.approx((peak + floor) / 2)
    assert lr(300_000, None, peak, floor, 100_000, 1000) == pytest.approx(floor)


def test_gate_stops_once_its_verdict_cannot_change():
    from stocknavalon.evaluate import gate_settled

    class Game:
        def __init__(self, winner, seat_zero_side=0):
            self.winner, self.seat_zero_side, self.final_scores = winner, seat_zero_side, [0, 0]

    settled = gate_settled(threshold=0.55, min_decided=20)
    wins = lambda n: [Game(0) for _ in range(n)]
    losses = lambda n: [Game(1) for _ in range(n)]
    # 2 won, 15 lost, 10 to go: even winning them all gives 12 of 27, under 55%
    assert settled(wins(2) + losses(15), 10)
    # 18 won, 2 lost, 5 to go: even losing them all leaves 18 of 25, over 55%
    assert settled(wins(18) + losses(2), 5)
    # 10 and 9 with 20 to go: still open
    assert not settled(wins(10) + losses(9), 20)
    # too few decided games possible: inconclusive whatever happens
    assert settled(wins(3) + losses(3), 5)


def test_held_out_set_keeps_its_positions_when_the_encoding_changes(tmp_path):
    import numpy as np
    from stocknavalon.run import load_heldout
    enc = pe.Encoding()
    records = pe.play_games(pe.self_play_config(search=dict(simulations=10), max_turns=4), 4,
                          seed=5, threads=2)
    held = pe.ReplayBuffer(100_000)
    held.add(records)
    drawn = held.sample_examples(enc, 8, 0, per_game=1, positions=True)
    path = str(tmp_path / "heldout.npz")
    # saved by an older encoding: drawn again from the same positions
    np.savez_compressed(path, **drawn, games=np.array(4), version=np.array(pe.ENCODING_VERSION - 1))
    again = load_heldout(path, records, enc, str(tmp_path))
    assert again is not None and "game_seeds" not in again
    for key in ("planes", "globals", "policy", "value", "targets"):
        assert np.array_equal(again[key], drawn[key]), key
    # and saved under the current version from then on
    with np.load(path) as saved:
        assert int(saved["version"]) == pe.ENCODING_VERSION
    assert load_heldout(path, records, enc, str(tmp_path)) is not None
    # its games gone: a new set
    np.savez_compressed(path, **drawn, games=np.array(4), version=np.array(pe.ENCODING_VERSION - 1))
    assert load_heldout(path, records[:1], enc, str(tmp_path)) is None


def test_ratings_put_networks_on_one_scale():
    from stocknavalon.rating import fit
    # b beats a 3 to 1, c beats b 3 to 1; a beats random 10 to 0
    ratings = fit([("a", "random", 10, 0), ("b", "a", 30, 10), ("c", "b", 30, 10)])
    assert ratings["random"] == 0
    assert 0 < ratings["a"] < ratings["b"] < ratings["c"]
    # 3 to 1 is about 190 Elo; the virtual draw pulls it in a little
    assert 150 < ratings["b"] - ratings["a"] < 200
    assert abs((ratings["c"] - ratings["b"]) - (ratings["b"] - ratings["a"])) < 15


def test_a_step_with_a_loss_that_is_not_finite_is_skipped(buffer, monkeypatch):
    import stocknavalon.train as train
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2)
    trainer = train.Trainer(model, enc, torch.device("cpu"))
    # the trained weights (the forward pass still updates BatchNorm's statistics)
    before = {k: v.clone() for k, v in model.named_parameters()}
    real = train.losses

    def broken(*args, **kwargs):
        result = real(*args, **kwargs)
        result["total"] = result["total"] * float("inf")
        return result

    monkeypatch.setattr(train, "losses", broken)
    assert trainer.step(buffer, 8) == {}
    assert trainer.skipped == 1
    assert all(torch.equal(before[k], v) for k, v in model.named_parameters())
    monkeypatch.setattr(train, "losses", real)
    assert trainer.step(buffer, 8)
    assert all(torch.isfinite(v).all() for v in model.state_dict().values() if v.is_floating_point())


def test_per_side_networks_give_the_same_batch_as_one_network():
    import copy
    from stocknavalon.selfplay import TIMES, _Clock, _launch
    enc = pe.Encoding()
    net = sn.Network(enc, channels=16, blocks=1).eval()
    twin = copy.deepcopy(net)
    session = pe.SelfPlaySession(
        pe.self_play_config(search=dict(simulations=8, samples=1), max_turns=8,
                            network_beliefs=True, paired=True), 4, 4, 5)
    clock = _Clock(dict.fromkeys(TIMES, 0.0))
    cpu = torch.device("cpu")
    one = _launch([net], session, enc, cpu, contextlib.nullcontext(), clock)
    each = _launch([net, twin], session, enc, cpu, contextlib.nullcontext(), clock)
    # paired games: both sides have positions waiting
    assert set(session.pending_sides()) == {0, 1}
    assert one is not None and one.size == each.size
    for a, b in ((one.moves, each.moves), (one.values, each.values),
                 (one.tie_breaks, each.tie_breaks), (one.beliefs, each.beliefs)):
        assert (a is None) == (b is None)
        if a is not None:
            assert torch.allclose(a.float(), b.float(), atol=1e-5)


def test_opponent_pool_games_train_only_the_current_network_at_either_seat(tmp_path):
    from stocknavalon.run import pool_snapshots
    from stocknavalon.selfplay import PoolPlay
    enc = pe.Encoding()
    current = sn.Network(enc, channels=16, blocks=1).eval()
    earlier = sn.Network(enc, channels=16, blocks=1).eval()
    runner = PoolPlay(enc, torch.device("cpu"),
                      pe.self_play_config(search=dict(simulations=8, samples=1), max_turns=8,
                                          quick_simulations=4, full_share=1.0),
                      parallel=8, seed=3)
    games, evaluated = runner.play(current, earlier, 6)
    assert len(games) >= 6 and evaluated > 0
    assert {g.pool_opponent_seat for g in games} == {0, 1}
    # the earlier network's moves have no policy targets: only the current
    # network's train (the C++ tests check which moves those are)
    buf = pe.ReplayBuffer(100_000)
    buf.add(games)
    assert 0 < buf.positions

    # the pool: the latest earlier bests from both folders, not the current
    for folder, steps in (("checkpoints", (10, 30)), ("pool", (20, 30, 40))):
        (tmp_path / folder).mkdir()
        for s in steps:
            (tmp_path / folder / f"step_{s}.pt").touch()
    names = [p.split(os.sep)[-2:] for p in pool_snapshots(str(tmp_path), 2, 40)]
    assert [n[1] for n in names] == ["step_20.pt", "step_30.pt"]
    assert len(pool_snapshots(str(tmp_path), 8, None)) == 4
    assert pool_snapshots(str(tmp_path), 0, None) == []


def test_value_per_turn_trains_and_held_out_reports_the_bias(buffer):
    from stocknavalon.train import heldout_losses
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2)
    trainer = sn.Trainer(model, enc, torch.device("cpu"), value_per_turn=True)
    stats = trainer.step(buffer, 16)
    assert math.isfinite(stats["total"]) and math.isfinite(stats["value"])
    examples = buffer.sample_examples(enc, 32, 1)
    assert (examples["turn_positions"] >= 1).all()
    held = heldout_losses(model, torch.device("cpu"), examples)
    assert -2 <= held["value_bias"] <= 2


def test_value_lambda_reanalyses_and_changes_the_targets(buffer):
    from stocknavalon.train import reanalyse
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2).eval()
    games = pe.play_games(pe.self_play_config(search=dict(simulations=8), max_turns=5), 3,
                          seed=5, threads=1)
    buf = pe.ReplayBuffer(100_000)
    buf.add(games)
    plain = buf.sample_examples(enc, 32, 3)
    # not reanalysed yet: the plain result
    early = buf.sample_examples(enc, 32, 3, value_lambda=0.5)
    assert np.array_equal(plain["value"], early["value"])
    assert len(buf.game_seeds(without_estimates=True)) == 3
    count, positions = reanalyse(buf, model, enc, torch.device("cpu"))
    assert count == 3 and positions > 0
    assert len(buf.game_seeds(without_estimates=True)) == 0
    # new games only: none left
    assert reanalyse(buf, model, enc, torch.device("cpu"), new_only=True) == (0, 0)
    blended = buf.sample_examples(enc, 32, 3, value_lambda=0.5)
    assert not np.array_equal(plain["value"], blended["value"])
    assert (np.abs(blended["value"]) <= 1).all()
    trainer = sn.Trainer(model, enc, torch.device("cpu"), value_lambda=0.9)
    stats = trainer.step(buf, 16)
    assert math.isfinite(stats["total"]) and math.isfinite(stats["value"])


def test_a_runs_settings_bootstrap_self_play_and_gates(tmp_path):
    # the run's flags, at their defaults, become working settings: the
    # bootstrap plays with them (score evaluator), and gates search with them
    from stocknavalon.run import parser, self_play_settings
    from stocknavalon.settings import search_config
    args = parser().parse_args(["--dir", str(tmp_path)])
    settings = self_play_settings(args, fog_share=1.0)
    assert settings.search.root_noise and settings.search.gumbel
    assert settings.search.simulations == args.simulations
    bootstrap = settings.copy()
    bootstrap.network_beliefs = False
    bootstrap.search.simulations = 6
    bootstrap.max_turns = 3
    games = pe.play_games(bootstrap, 2, seed=1, threads=1)
    assert len(games) == 2
    gate = search_config(args, simulations=args.eval_simulations)
    # the run's search: turn search 0, the most likely replies first
    assert gate.simulations == args.eval_simulations and gate.turn_search
    assert gate.command_visits == 0 and gate.replies == pe.Replies.LIKELY_FIRST
    turns = search_config(parser().parse_args(["--dir", str(tmp_path), "--turn-search", "0",
                                               "--evaluations", "100"]))
    assert turns.turn_search and turns.command_visits == 0 and turns.horizon == 3
    assert turns.count_evaluations and turns.simulations == 100
    assert turns.replies == pe.Replies.LIKELY_FIRST and turns.backup == pe.Backup.MEAN


def test_per_side_search_flags_take_one_value_or_one_each():
    from stocknavalon.play import main as _  # noqa: F401  (play's flags import cleanly)
    from stocknavalon.settings import add_search_arguments, side_count, side_searches
    p = argparse.ArgumentParser()
    add_search_arguments(p, simulations=64, samples=1)
    args = p.parse_args(["--turn-search", "8", "--replies", "likely", "discrepancy",
                         "--backup", "pbar-opponent"])
    assert side_count(args) == 2
    a, b = side_searches(args, side_count(args))
    assert a.turn_search and b.turn_search and a.command_visits == b.command_visits == 8
    assert (a.replies, b.replies) == (pe.Replies.LIKELY_FIRST, pe.Replies.DISCREPANCY)
    assert a.backup == b.backup == pe.Backup.PBAR_OPPONENT
    with pytest.raises(SystemExit):
        side_searches(p.parse_args(["--backup", "mean", "pbar", "pbar"]), 2)


def test_a_gate_splits_its_games_among_several_bests():
    from stocknavalon.run import gate_split
    assert gate_split(128, 1, 0.5) == [128]
    assert gate_split(128, 3, 0.5) == [64, 32, 32]
    assert gate_split(128, 4, 0.5) == [64, 22, 22, 20]
    for opponents in (2, 3, 5):
        parts = gate_split(128, opponents, 0.5)
        assert sum(parts) == 128 and all(p % 2 == 0 for p in parts)
    # too few games for everyone: the best still plays
    assert gate_split(4, 4, 0.5)[0] >= 2 and sum(gate_split(4, 4, 0.5)) == 4


def test_ratings_read_each_part_of_a_gate_against_several_bests(tmp_path):
    import json
    from stocknavalon.rating import results_from_log
    path = tmp_path / "log.txt"
    entries = [
        {"event": "gate", "steps": 10, "best": 8, "captures": 30, "was_captured": 20},
        {"event": "gate match", "steps": 12, "against": 10, "captures": 20, "was_captured": 10},
        {"event": "gate match", "steps": 12, "against": 8, "captures": 9, "was_captured": 5},
        {"event": "gate", "steps": 12, "best": 10, "captures": 29, "was_captured": 15,
         "opponents": {"10": [20, 10], "8": [9, 5]}},
    ]
    path.write_text("".join(json.dumps(e) + "\n" for e in entries))
    assert sorted(results_from_log(str(path))) == [
        ("10", "8", 30, 20), ("12", "10", 20, 10), ("12", "8", 9, 5)]


def test_pool_draws_favour_the_opponents_the_current_network_struggles_with(tmp_path):
    import collections
    from types import SimpleNamespace
    from stocknavalon.run import SelfPlayThread
    thread = SelfPlayThread.__new__(SelfPlayThread)
    thread.args = SimpleNamespace(pool_weighting="pfsp")
    thread._against = {"easy": collections.deque([1.0] * 20), "hard": collections.deque([0.0] * 20)}
    weights = thread._pool_weights(["easy", "hard", "new"])
    assert weights[1] > weights[2] > weights[0]
    assert abs(weights.sum() - 1) < 1e-9
    thread.args.pool_weighting = "uniform"
    assert list(thread._pool_weights(["easy", "hard", "new"])) == [1 / 3] * 3


def test_a_gate_against_several_bests_plays_each_and_judges_the_total(tmp_path):
    import json
    import threading
    from stocknavalon.run import EvaluationThread, parser
    enc = pe.Encoding()
    args = parser().parse_args([
        "--dir", str(tmp_path), "--channels", "16", "--blocks", "1",
        "--gate-opponents", "3", "--gate-games", "8", "--gate-max-turns", "4",
        "--eval-simulations", "4", "--samples", "1", "--gate-min-decided", "1",
        "--gate-threshold", "0", "--session-threads", "1", "--inference-dtype", "float32",
        "--no-network-beliefs"])
    torch.manual_seed(0)
    nets = {s: sn.Network(enc, 16, 1).state_dict() for s in (6, 8, 10, 12)}
    (tmp_path / "pool").mkdir()
    for s in (6, 8):
        torch.save({"model": nets[s], "steps": s}, tmp_path / "pool" / f"step_{s}.pt")
    promoted, verdicts, trained = [], [], []
    gate = EvaluationThread(
        args, enc, torch.device("cpu"), threading.Lock(), {"model": nets[10], "steps": 10},
        str(tmp_path / "best.pt"), on_new_best=lambda w, s: promoted.append(s),
        fog_share=lambda: 1.0, on_gate=verdicts.append, on_games=trained.extend)
    gate._run_gate(nets[12], 12)
    entries = [json.loads(l) for l in (tmp_path / "log.txt").read_text().splitlines()]
    parts = [e for e in entries if e["event"] == "gate match"]
    # the best, then the bests before it, newest first, the games split 4-2-2
    assert [p["against"] for p in parts] == [10, 8, 6]
    assert [p["games"] for p in parts] == [4, 2, 2]
    final = [e for e in entries if e["event"] == "gate"][-1]
    assert final["games"] == 8 and set(final["opponents"]) == {"10", "8", "6"}
    assert final["captures"] == sum(p["captures"] for p in parts)
    # every gate game trains, the candidate's moves only
    assert len(trained) == 8
    for r in trained:
        candidate = 0 if r.seat_zero_side == 0 else 1
        assert r.pool_opponent_seat == 1 - candidate
        searched, game = set(r.searched), r.start()
        for i, c in enumerate(r.commands):
            if i in searched:
                assert game.current_player == candidate
            game.apply(c)
    # threshold 0: it passes, and becomes the best
    assert verdicts == [final["verdict"]]
    if final["verdict"] == "pass":
        assert promoted == [12] and gate.best["steps"] == 12


def test_offline_training_compares_sizes_on_the_same_games(tmp_path, monkeypatch):
    import json
    import sys
    from stocknavalon import offline
    cfg = pe.self_play_config(search=dict(simulations=6), max_turns=5)
    pe.save_records(pe.play_games(cfg, 8, seed=1, threads=2), str(tmp_path / "records.txt"), False)
    pe.save_records(pe.play_games(cfg, 4, seed=9, threads=2), str(tmp_path / "holdout.txt"), False)
    monkeypatch.setattr(sys, "argv", [
        "offline", "--dir", str(tmp_path), "--sizes", "16x1", "24x2", "--steps", "2",
        "--eval-every", "1", "--batch", "8", "--device", "cpu", "--heldout-positions", "16",
        "--value-lambda", "0.9"])
    offline.main()
    entries = [json.loads(l) for l in (tmp_path / "offline" / "log.jsonl").read_text().splitlines()]
    assert [(e["steps"], e["size"]) for e in entries] == [
        (1, "16x1"), (1, "24x2"), (2, "16x1"), (2, "24x2")]
    assert all(math.isfinite(e["total"]) for e in entries)
    saved = torch.load(tmp_path / "offline" / "24x2.pt")
    assert saved["config"] == {"channels": 24, "blocks": 2} and saved["steps"] == 2


def test_a_run_logs_its_settings_at_the_start(tmp_path):
    from stocknavalon.run import parser, pretty, start_entry
    args = parser().parse_args(["--dir", str(tmp_path), "--gate-threshold", "0.55",
                                "--turn-search", "off"])
    entry = start_entry(args)
    assert entry["settings"]["gate_threshold"] == 0.55 and entry["settings"]["batch"] == 256
    assert entry["changed"] == {"gate_threshold": 0.55, "turn_search": ["off"]}
    assert entry["rules_version"] == pe.RULES_VERSION
    line = pretty({"time": "00:00:00", **entry})
    assert "--gate-threshold 0.55" in line and "--batch" not in line
