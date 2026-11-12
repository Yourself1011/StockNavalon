import math

import pytest

torch = pytest.importorskip("torch")

import polyengine as pe
import stocknavalon as sn
from stocknavalon.train import losses, to_device


@pytest.fixture(scope="module")
def buffer():
    games = pe.play_games(4, seed=2, threads=2, simulations=20, max_turns=4)
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
    records, info = sn.play(model, enc, torch.device("cpu"), games=2, parallel=2,
                            simulations=10, max_turns=2)
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
    records, _ = sn.play(model, enc, torch.device("cpu"), games=2, parallel=2,
                         simulations=6, max_turns=3, fog_share=0.0)
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
    records, info = sn.play(model, enc, torch.device("cpu"), games=2, parallel=2,
                            simulations=6, max_turns=3, network_beliefs=True)
    assert len(records) >= 2 and info["evaluated"] > 0
    result = match(model, sn.Network(enc, channels=16, blocks=2), enc,
                   torch.device("cpu"), games=2, parallel=2, simulations=4,
                   max_turns=2, network_beliefs=True)
    assert result["games"] == 2


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
    records = pe.play_games(4, seed=5, threads=2, simulations=10, max_turns=4)
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
