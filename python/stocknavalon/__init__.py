"""stockNavalon: the bot's network, training and self-play, on polyengine.

How it works: docs/stockNavalon.md; plans: notes/. Needs PyTorch (`uv pip install -e ".[train]"`).
"""

from .model import Network, pick_device
from .train import Trainer, LossWeights, losses
from .selfplay import play

__all__ = ["Network", "pick_device", "Trainer", "LossWeights", "losses",
           "play"]
