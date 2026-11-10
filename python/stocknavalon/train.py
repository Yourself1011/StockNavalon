"""Training stockNavalon's network on recorded games (docs/stockNavalon.md
section 7).

Every position trains every head at once: the policy towards the search's
visits, the value towards how the game went, and the beliefs towards the true
hidden state, each belief head only where its mask says (docs/encoding.md
section 8).
"""

from __future__ import annotations

import contextlib
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

import numpy as np
import torch
import torch.nn.functional as F

import polyengine as pe

from .model import Network, STAR_COMPONENTS

NEG = -1e4  # a masked logit: far below any real one, without infinities


@dataclass
class LossWeights:
    policy: float = 1.0
    value: float = 1.0
    # all belief heads together, shared equally
    beliefs: float = 0.25
    # extra weight on the rare tiles that do hold a hidden unit
    hidden_unit_positive: float = 10.0


def to_device(batch: dict, device: torch.device) -> dict:
    out = {}
    for key, array in batch.items():
        tensor = torch.from_numpy(np.ascontiguousarray(array))
        out[key] = (tensor.bool() if array.dtype == np.uint8 else tensor).to(device)
    return out


def masked_cross_entropy(logits, target, mask):
    """Cross-entropy over the masked-in classes, for rows with any."""
    rows = mask.any(dim=-1)
    if not rows.any():
        return logits.new_zeros(())
    log_p = F.log_softmax(logits.masked_fill(~mask, NEG), dim=-1)
    return -(target * log_p).sum(dim=-1)[rows].mean()


def star_nll(raw, count):
    """Negative log-likelihood of `count` under a mixture of negative
    binomials, from the head's raw output [..., 3, components]."""
    weight = F.log_softmax(raw[..., 0, :], dim=-1)
    mean = F.softplus(raw[..., 1, :]) + 1e-3
    spread = F.softplus(raw[..., 2, :]) + 1e-3
    k = count.unsqueeze(-1)
    log_pmf = (torch.lgamma(k + spread) - torch.lgamma(spread) - torch.lgamma(k + 1)
               + spread * torch.log(spread / (spread + mean))
               + k * torch.log(mean / (spread + mean)))
    return -torch.logsumexp(weight + log_pmf, dim=-1)


def losses(model: Network, out: dict, batch: dict,
           weights: LossWeights = LossWeights()) -> dict:
    """Each head's loss, and their weighted total."""
    result = {}

    # policy: towards the visits, over legal slots, where the search chose
    policy = batch["policy"]
    has = policy.sum(dim=1) > 0
    log_p = F.log_softmax(out["policy"].masked_fill(~batch["legal"], NEG), dim=1)
    result["policy"] = -(policy * log_p).sum(dim=1)[has].mean() \
        if has.any() else log_p.new_zeros(())

    # value: towards the blend of score and result, weighted
    w = batch["value_weight"]
    result["value"] = (w * (out["value"] - batch["value"]) ** 2).sum() \
        / w.sum().clamp(min=1e-6)

    # beliefs, per opponent
    targets, masks = batch["targets"], batch["target_masks"]
    belief = {}
    n2 = model.n * model.n
    for k in range(model.opponents):
        def part(name):
            offset, size = model.heads[name]
            offset += k * sum(size for _, size in model.heads.values())
            return targets[:, offset:offset + size], masks[:, offset:offset + size]

        t, m = part("tribe")
        belief.setdefault("tribe", []).append(
            masked_cross_entropy(out["tribe"][:, k], t, m))

        t, m = part("techs")
        bce = F.binary_cross_entropy_with_logits(out["techs"][:, k], t, reduction="none")
        belief.setdefault("techs", []).append((bce * m).sum() / m.sum().clamp(min=1))

        t, m = part("stars")
        nll = star_nll(out["stars"][:, k], t[:, 0])
        belief.setdefault("stars", []).append(
            (nll * m[:, 0]).sum() / m[:, 0].sum().clamp(min=1))

        t, m = part("hidden_unit_here")
        weight = 1 + (weights.hidden_unit_positive - 1) * t
        bce = F.binary_cross_entropy_with_logits(out["hidden_unit_here"][:, k], t,
                                                 reduction="none")
        belief.setdefault("hidden_unit_here", []).append(
            (bce * weight * m).sum() / (weight * m).sum().clamp(min=1))

        t, m = part("hidden_unit_type")
        types = model.unit_types
        t = t.reshape(-1, types, n2)
        m = m.reshape(-1, types, n2)
        tiles = m[:, 0, :]  # every type's mask is the same per tile
        log_p = F.log_softmax(out["hidden_unit_type"][:, k], dim=1)
        ce = -(t * log_p).sum(dim=1)
        belief.setdefault("hidden_unit_type", []).append(
            (ce * tiles).sum() / tiles.sum().clamp(min=1))

        for name in ("hidden_unit_count", "exploration"):
            t, m = part(name)
            belief.setdefault(name, []).append(
                masked_cross_entropy(out[name][:, k], t, m))

    for name, parts in belief.items():
        result[name] = torch.stack(parts).mean()
    belief_total = torch.stack([result[name] for name in belief]).mean()
    result["total"] = (weights.policy * result["policy"]
                       + weights.value * result["value"]
                       + weights.beliefs * belief_total)
    return result


class Trainer:
    """Draws minibatches from a replay buffer and trains on them. The next
    batch is encoded in the background while the GPU works on this one (the
    encoding runs in C++ without holding Python's lock)."""

    def __init__(self, model: Network, encoding: pe.Encoding, device: torch.device,
                 lr: float = 1e-3, weight_decay: float = 1e-4,
                 weights: LossWeights = LossWeights(), per_game: int = 4,
                 gpu_lock=None):
        self.model, self.encoding, self.device = model, encoding, device
        self.weights = weights
        self.per_game = per_game
        self.optimizer = torch.optim.AdamW(model.parameters(), lr=lr,
                                           weight_decay=weight_decay)
        self.steps = 0
        # shared with self-play when both run at once, around GPU work
        self.gpu_lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
        self._pool = ThreadPoolExecutor(max_workers=1)
        self._next = None

    def _encode(self, buffer, batch_size, score_weight, seed):
        # sampled and encoded at once: safe while self-play adds games
        return buffer.sample_examples(self.encoding, batch_size, seed,
                                      per_game=self.per_game,
                                      score_weight=score_weight)

    def step(self, buffer: pe.ReplayBuffer, batch_size: int,
             score_weight: float) -> dict:
        key = (id(buffer), batch_size, score_weight)
        if self._next is not None and self._next[0] == key:
            examples = self._next[1].result()
        else:
            examples = self._encode(buffer, batch_size, score_weight, self.steps)
        # start on the next batch while this one trains
        self._next = (key, self._pool.submit(self._encode, buffer, batch_size,
                                             score_weight, self.steps + 1))
        with self.gpu_lock:
            batch = to_device(examples, self.device)
            self.model.train()
            out = self.model(batch["planes"], batch["globals"])
            result = losses(self.model, out, batch, self.weights)
            self.optimizer.zero_grad(set_to_none=True)
            result["total"].backward()
            torch.nn.utils.clip_grad_norm_(self.model.parameters(), 5.0)
            self.optimizer.step()
            values = {name: value.item() for name, value in result.items()}
        self.steps += 1
        return values

    def drain(self):
        """Waits for the background batch, before the buffer changes."""
        if self._next is not None:
            self._next[1].result()
            self._next = None


def score_weight_for(decided_share: float, weight: float = 0.1,
                     start: float = 0.1, end: float = 0.6) -> float:
    """The value target's small weight on the score lead (docs/stockNavalon.md
    section 7): `weight` while few games end in a capture, fading to 0 as
    captures become the norm, linearly between `start` and `end` of games
    decided."""
    return float(weight * np.clip((end - decided_share) / (end - start), 0.0, 1.0))


@torch.no_grad()
def heldout_losses(model: Network, encoding: pe.Encoding, device: torch.device,
                   buffer: pe.ReplayBuffer, count: int, score_weight: float,
                   gpu_lock=None) -> dict | None:
    """The losses on games kept out of training (the same sample each time,
    so the numbers are comparable): a sign of overfitting if they rise while
    the training losses fall."""
    if buffer.positions == 0:
        return None
    lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
    examples = buffer.sample_examples(encoding, count, 0, score_weight=score_weight)
    with lock:
        was_training = model.training
        model.eval()
        batch = to_device(examples, device)
        result = losses(model, model(batch["planes"], batch["globals"]), batch)
        values = {name: round(value.item(), 4) for name, value in result.items()}
        model.train(was_training)
    return values

