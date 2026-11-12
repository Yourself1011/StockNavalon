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
# a weight, mean and spread for each component, as the engine decodes it
STAR_COMPONENTS = pe.STAR_COMPONENTS


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
        # heads about the map rather than an opponent
        self.map_heads = {name: (offset, size) for name, opponent, offset, size
                          in encoding.target_heads if opponent == 0}
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

        # side heads, never read by the search: the score lead soon and at
        # the end, and each tile's final owner (docs/encoding.md section 8.5)
        self.score_lead = nn.Linear(2 * channels, 2)
        self.territory = nn.Conv2d(channels, pe.TERRITORY_CLASSES, 1)
        # the unexplored tiles: terrain, resource and feature, per tile
        self.unexplored = nn.Conv2d(channels, pe.UNEXPLORED_LAYERS, 1)

    def forward(self, planes: torch.Tensor,
                globals_: torch.Tensor) -> dict[str, torch.Tensor]:
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
        lead = torch.tanh(self.score_lead(pooled))
        out["score_soon"], out["score_end"] = lead[:, 0], lead[:, 1]
        out["territory"] = self.territory(x).reshape(b, pe.TERRITORY_CLASSES, -1)
        out["unexplored"] = self.unexplored(x).reshape(b, pe.UNEXPLORED_LAYERS, -1)
        return out


# heads added after networks were already trained: weights saved before them
# load without them, and these start fresh
NEWER_HEADS = ("score_lead.", "territory.", "unexplored.")
# layers reading the global vector, which grows at its end (the map type,
# encoding version 6): weights saved before get zeros for the new inputs, so
# the network computes exactly what it did
WIDER_INPUTS = ("globals.weight",)


def load_weights(model: nn.Module, weights: dict) -> list[str]:
    """Loads saved weights, allowing only the heads in NEWER_HEADS to be
    missing and the layers in WIDER_INPUTS to have fewer inputs. Returns the
    keys not loaded as they are: missing, or widened."""
    weights = dict(weights)
    current = model.state_dict()
    widened = []
    for key in WIDER_INPUTS:
        saved, now = weights.get(key), current[key]
        if saved is not None and saved.shape[0] == now.shape[0] and saved.shape[1] < now.shape[1]:
            weights[key] = torch.cat([saved, saved.new_zeros(
                (saved.shape[0], now.shape[1] - saved.shape[1]))], dim=1)
            widened.append(key)
    missing, unexpected = model.load_state_dict(weights, strict=False)
    wrong = [k for k in missing if not k.startswith(NEWER_HEADS)]
    if wrong or unexpected:
        raise RuntimeError(f"Weights don't fit the network: missing {wrong}, "
                           f"unexpected {list(unexpected)}")
    return list(missing) + widened


def belief_outputs(out: dict[str, torch.Tensor], rows: torch.Tensor,
                   encoding: pe.Encoding) -> torch.Tensor:
    """The raw belief outputs of the batch's `rows`, as the flat array the
    engine decodes into beliefs to sample from ([R, belief_output_count],
    laid out by encoding.belief_output_heads). Stays on the network's device."""
    first = out["tribe"]
    result = first.new_zeros((len(rows), encoding.belief_output_count))
    for name, opponent, offset, size in encoding.belief_output_heads:
        # opponent 0: a head about the map, not per opponent
        part = out[name][rows] if opponent == 0 else out[name][rows, opponent - 1]
        result[:, offset:offset + size] = part.reshape(len(rows), -1)
    return result


# Inference precisions (training stays in float32)
DTYPES = {"float32": torch.float32, "float16": torch.float16, "bfloat16": torch.bfloat16}


def as_input(array, model: nn.Module, device: torch.device) -> torch.Tensor:
    """An input array on the model's device, in the model's precision."""
    return torch.from_numpy(array).to(device=device, dtype=next(model.parameters()).dtype)

