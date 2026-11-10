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


def test_losses_are_finite_and_fall_on_a_fixed_batch(buffer):
    torch.manual_seed(0)
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2)
    batch = to_device(buffer.encode(enc, buffer.sample(32, 1)), torch.device("cpu"))
    optimizer = torch.optim.Adam(model.parameters(), lr=3e-3)
    first = None
    for _ in range(40):
        result = losses(model, model(batch["planes"], batch["globals"]), batch)
        for name, value in result.items():
            assert math.isfinite(value.item()), name
        optimizer.zero_grad()
        result["total"].backward()
        optimizer.step()
        first = first if first is not None else float(result["total"])
    assert float(result["total"]) < first


def test_trainer_and_network_self_play(buffer):
    enc = pe.Encoding()
    model = sn.Network(enc, channels=16, blocks=2)
    trainer = sn.Trainer(model, enc, torch.device("cpu"))
    stats = trainer.step(buffer, 16, score_weight=1.0)
    assert math.isfinite(stats["total"])
    records, info = sn.play(model, enc, torch.device("cpu"), games=2, parallel=2,
                            simulations=10, max_turns=2)
    # games finishing in the same batch all come back
    assert len(records) >= 2 and info["evaluated"] > 0
    # a small weight on the score lead, gone once captures decide most games
    assert sn.score_weight_for(0.0) == pytest.approx(0.1)
    assert sn.score_weight_for(1.0) == 0.0
