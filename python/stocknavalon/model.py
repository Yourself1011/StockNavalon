"""stockNavalon's network: one trunk, with policy, value and belief heads.

The layout of every input and output is docs/encoding.md; the design is
docs/stockNavalon.md section 2. Sizes come from the encoding, so the same code
builds a network for any map size or player count.
"""

from __future__ import annotations

import torch
from torch import nn
import torch.nn.functional as F

import polyengine as pe

# the belief heads read from pooled features, and those read per tile
POOLED_HEADS = ("tribe", "techs", "hidden_unit_count", "exploration")
TILE_HEADS = ("hidden_unit_here", "hidden_unit_type")
# a mixture of negative binomials for the opponent's stars (encoding.md 8.3):
# a weight, mean and spread for each component
STAR_COMPONENTS = 3


def pick_device() -> torch.device:
    """MPS on Apple Silicon when there's a GPU to use, otherwise the CPU."""
    if torch.backends.mps.is_available():
        return torch.device("mps")
    if torch.cuda.is_available():
        return torch.device("cuda")
    return torch.device("cpu")


def pool(x: torch.Tensor) -> torch.Tensor:
    """Mean and max over the board: [B, C, N, N] -> [B, 2C]."""
    return torch.cat([x.mean(dim=(2, 3)), x.amax(dim=(2, 3))], dim=1)


class Block(nn.Module):
    """A residual block. With global pooling (KataGo), the board's mean and
    max after the first convolution come back as a per-channel bias, so
    information crosses the board in one block."""

    def __init__(self, channels: int, global_pooling: bool):
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn2 = nn.BatchNorm2d(channels)
        self.pooled = nn.Linear(2 * channels, channels) if global_pooling else None

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        y = F.relu(self.bn1(self.conv1(x)))
        if self.pooled is not None:
            y = y + self.pooled(pool(y))[:, :, None, None]
        y = self.bn2(self.conv2(y))
        return F.relu(x + y)


class Network(nn.Module):
    def __init__(self, encoding: pe.Encoding, channels: int = 64, blocks: int = 6,
                 pooling_blocks: tuple[int, ...] = (2, 4)):
        super().__init__()
        self.n = encoding.map_size
        self.tile_slots = pe.SLOTS_PER_TILE
        self.global_slots = encoding.action_count - self.tile_slots * self.n * self.n
        self.heads = {name: (offset, size) for name, opponent, offset, size
                      in encoding.target_heads if opponent == 1}
        self.opponents = encoding.players - 1

        self.stem = nn.Conv2d(encoding.plane_count, channels, 3, padding=1, bias=False)
        self.stem_bn = nn.BatchNorm2d(channels)
        # the global vector, as a bias on every tile
        self.globals = nn.Linear(encoding.global_count, channels)
        self.trunk = nn.Sequential(*[Block(channels, i in pooling_blocks)
                                     for i in range(blocks)])

        self.policy_tiles = nn.Conv2d(channels, self.tile_slots, 1)
        self.policy_global = nn.Linear(2 * channels, self.global_slots)
        self.value = nn.Sequential(nn.Linear(2 * channels, 64), nn.ReLU(),
                                   nn.Linear(64, 1))

        # belief heads, per opponent
        k = self.opponents
        self.belief_pooled = nn.ModuleDict({
            name: nn.Linear(2 * channels, k * self.heads[name][1])
            for name in POOLED_HEADS})
        self.stars = nn.Linear(2 * channels, k * 3 * STAR_COMPONENTS)
        self.unit_here = nn.Conv2d(channels, k, 1)
        # the type head's targets are [types][N][N]
        self.unit_types = self.heads["hidden_unit_type"][1] // (self.n * self.n)
        self.unit_type = nn.Conv2d(channels, k * self.unit_types, 1)

    def forward(self, planes: torch.Tensor, globals_: torch.Tensor) -> dict:
        x = self.stem_bn(self.stem(planes)) + self.globals(globals_)[:, :, None, None]
        x = self.trunk(F.relu(x))
        pooled = pool(x)
        b = x.shape[0]

        out = {
            # per-tile slots first (slot, y, x), then the global ones
            "policy": torch.cat([self.policy_tiles(x).reshape(b, -1),
                                 self.policy_global(pooled)], dim=1),
            "value": torch.tanh(self.value(pooled)).squeeze(1),
        }
        k = self.opponents
        for name, layer in self.belief_pooled.items():
            out[name] = layer(pooled).reshape(b, k, -1)
        out["stars"] = self.stars(pooled).reshape(b, k, 3, STAR_COMPONENTS)
        out["hidden_unit_here"] = self.unit_here(x).reshape(b, k, -1)
        out["hidden_unit_type"] = self.unit_type(x).reshape(
            b, k, self.unit_types, self.n * self.n)
        return out
