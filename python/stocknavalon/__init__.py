"""stockNavalon: the bot's network, training and self-play, on polyengine.

Design: docs/stockNavalon.md. Needs PyTorch (`uv pip install -e ".[train]"`).
"""

from .model import Network, pick_device
from .train import Trainer, LossWeights, losses, score_weight_for
from .selfplay import play

__all__ = ["Network", "pick_device", "Trainer", "LossWeights", "losses",
           "score_weight_for", "play"]
