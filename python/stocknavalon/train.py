"""Training stockNavalon's network on recorded games (docs/stockNavalon.md
section 6).

Every position trains every head at once: the policy towards the search's
visits, the value towards how the game went, and the beliefs towards the true
hidden state, each belief head only where its mask says (docs/encoding.md
section 8).
"""

from __future__ import annotations

import contextlib
import math
import copy
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

import numpy as np
import torch
import torch.nn.functional as F

import polyengine as pe

from .model import Network, STAR_COMPONENTS, as_input

NEG = -1e4  # a masked logit: far below any real one, without infinities


@dataclass
class LossWeights:
    policy: float = 1.0
    value: float = 1.0
    # all belief heads together, shared equally
    beliefs: float = 0.25
    # extra weight on the rare tiles that do hold a hidden unit
    hidden_unit_positive: float = 10.0
    # side heads (docs/encoding.md section 8.5): the two score leads
    # together, the stars and income ahead together, and the final territory
    score: float = 0.25
    economy: float = 0.25
    # the short-term values, all horizons together
    short_term: float = 0.25
    # turns until the game ends, for games that ended in a capture
    turns_left: float = 0.1
    territory: float = 0.5


def to_device(batch: dict[str, np.ndarray],
              device: torch.device) -> dict[str, torch.Tensor]:
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


def losses(model: Network, out: dict[str, torch.Tensor],
           batch: dict[str, torch.Tensor],
           weights: LossWeights = LossWeights()) -> dict[str, torch.Tensor]:
    """Each head's loss, and their weighted total."""
    result = {}

    # policy: towards the visits, over legal slots, where the search chose
    policy = batch["policy"]
    has = policy.sum(dim=1) > 0
    log_p = F.log_softmax(out["policy"].masked_fill(~batch["legal"], NEG), dim=1)
    cross = -(policy * log_p).sum(dim=1)
    result["policy"] = cross[has].mean() if has.any() else log_p.new_zeros(())
    # The cross-entropy can't fall below the targets' own entropy, which is
    # high for the spread-out visits of a short search. What's above it, the
    # KL divergence, is what shows learning (0 for a perfect match). Only
    # reported, not trained on: its gradient is the cross-entropy's.
    with torch.no_grad():
        entropy = -(policy * torch.log(policy.clamp(min=1e-12))).sum(dim=1)
        result["policy_kl"] = (cross - entropy)[has].mean() \
            if has.any() else log_p.new_zeros(())

    # value: weighted squared error, averaged over the batch rather than the
    # weights, so a smaller weight (a cut-off game's, or a sample left out)
    # really is less pull; with every weight 1 the two are the same
    w = batch["value_weight"]
    result["value"] = (w * (out["value"] - batch["value"]) ** 2).mean()

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

    # the unexplored tiles: a softmax per group per tile, over the tiles the
    # player hasn't explored
    offset, size = model.map_heads["unexplored"]
    b = targets.shape[0]
    t = targets[:, offset:offset + size].reshape(b, pe.UNEXPLORED_LAYERS, -1)
    m = masks[:, offset:offset + size].reshape(b, pe.UNEXPLORED_LAYERS, -1)
    groups = ((pe.UNEXPLORED_TERRAIN, pe.UNEXPLORED_RESOURCE),
              (pe.UNEXPLORED_RESOURCE, pe.UNEXPLORED_FEATURE),
              (pe.UNEXPLORED_FEATURE, pe.UNEXPLORED_LAYERS))
    for first, end in groups:
        tiles = m[:, first].float()
        log_p = F.log_softmax(out["unexplored"][:, first:end], dim=1)
        ce = -(t[:, first:end] * log_p).sum(dim=1)
        belief.setdefault("unexplored", []).append(
            (ce * tiles).sum() / tiles.sum().clamp(min=1))

    for name, parts in belief.items():
        result[name] = torch.stack(parts).mean()
    belief_total = torch.stack([result[name] for name in belief]).mean()
    result["total"] = (weights.policy * result["policy"]
                       + weights.value * result["value"]
                       + weights.beliefs * belief_total)

    # side heads, where the record has their targets (older records and
    # held-out sets made before them don't)
    if "side_targets" in batch:
        side, side_masks = batch["side_targets"], batch["side_masks"].float()
        for name, k in (("score_soon", pe.SIDE_SCORE_SOON),
                        ("score_end", pe.SIDE_SCORE_END),
                        ("stars_ahead", pe.SIDE_STARS_AHEAD),
                        ("income_ahead", pe.SIDE_INCOME_AHEAD),
                        ("turns_left", pe.SIDE_TURNS_LEFT)):
            m = side_masks[:, k]
            result[name] = ((out[name] - side[:, k]) ** 2 * m).sum() / m.sum().clamp(min=1)
        k, h = pe.SIDE_SHORT_TERM_VALUE, len(pe.SHORT_TERM_HORIZONS)
        m = side_masks[:, k:k + h]
        result["short_term_value"] = (((out["short_term_value"] - side[:, k:k + h]) ** 2 * m).sum()
                                      / m.sum().clamp(min=1))
        b, classes = side.shape[0], pe.TERRITORY_CLASSES
        t = side[:, pe.SIDE_TERRITORY:].reshape(b, classes, -1)
        tiles = side_masks[:, pe.SIDE_TERRITORY:].reshape(b, classes, -1)[:, 0]
        ce = -(t * F.log_softmax(out["territory"], dim=1)).sum(dim=1)
        result["territory"] = (ce * tiles).sum() / tiles.sum().clamp(min=1)
        result["total"] = (result["total"]
                           + weights.score * (result["score_soon"] + result["score_end"]) / 2
                           + weights.economy * (result["stars_ahead"] + result["income_ahead"]) / 2
                           + weights.short_term * result["short_term_value"]
                           + weights.turns_left * result["turns_left"]
                           + weights.territory * result["territory"])
    return result


def learning_rate(steps: int, fresh_steps: int | None, peak: float, floor: float,
                  decay_steps: int, warmup_steps: int) -> float:
    """The learning rate `steps` into a run (docs/stockNavalon.md section 6):
    cosine from `peak` down to `floor` over `decay_steps`, then held there,
    since training has no end and its data keeps changing. While the
    optimizer is fresh (`fresh_steps` steps since it started, None if its
    state was loaded), a linear warm-up from 0 over `warmup_steps` too: a new
    Adam moves every weight by about the learning rate at once, which jolts a
    trained network."""
    progress = min(1.0, steps / decay_steps) if decay_steps > 0 else 1.0
    rate = floor + (peak - floor) * 0.5 * (1 + math.cos(math.pi * progress))
    if fresh_steps is not None and warmup_steps > 0:
        rate *= min(1.0, (fresh_steps + 1) / warmup_steps)
    return rate


class Trainer:
    """Draws minibatches from a replay buffer and trains on them. The next
    batch is encoded in the background while the GPU works on this one (the
    encoding runs in C++ without holding Python's lock)."""

    def __init__(self, model: Network, encoding: pe.Encoding, device: torch.device,
                 lr: float = 1e-3, weight_decay: float = 1e-4,
                 weights: LossWeights = LossWeights(), per_game: int = 1,
                 gpu_lock=None, augment: bool = True, ema_decay: float = 0.999,
                 cut_off_weight: float = 1.0, value_share: float = 1.0,
                 search_value_weight: float = 0.0, value_per_turn: bool = False,
                 value_lambda: float = 1.0):
        self.model, self.encoding, self.device = model, encoding, device
        # An exponential moving average of the weights (decay about 1,000
        # steps), what self-play and gating use: with a fixed learning rate
        # the weights keep bouncing around a good solution, and the average
        # sits near its middle. BatchNorm statistics are copied, not averaged.
        self.ema = copy.deepcopy(model).eval()
        for p in self.ema.parameters():
            p.requires_grad_(False)
        self.ema_decay = ema_decay
        self.weights = weights
        self.per_game = per_game
        self.augment = augment
        # Against memorizing results (docs/stockNavalon.md section 6): a
        # cut-off game's positions weigh this much in the value loss, and
        # only this share of samples trains the value at all. Every game's
        # positions share one result, so each result is otherwise repeated
        # hundreds of times.
        self.cut_off_weight, self.value_share = cut_off_weight, value_share
        # the share of the value target from each position's own search
        # value: softer, position-specific targets than one result per game
        self.search_value_weight = search_value_weight
        # the value target a lambda-return of the player's later search
        # values ending in the result (ValueTarget::lambda); 1: the result
        self.value_lambda = value_lambda
        # Each player turn weighs the same in the value loss, however many
        # moves it took: positions are sampled per move, and the side that's
        # winning makes more moves, so under fog, where positions look
        # alike, per-move sampling makes the player to move look better off
        # than they are (notes/runs.md, "Why the bot passes")
        self.value_per_turn = value_per_turn
        self.optimizer = torch.optim.AdamW(model.parameters(), lr=lr,
                                           weight_decay=weight_decay)
        self.steps = 0
        # steps skipped for a loss that wasn't finite
        self.skipped = 0
        # steps since the optimizer started fresh; None once its state is
        # loaded from a checkpoint (no warm-up then)
        self.fresh_steps: int | None = 0
        # shared with self-play when both run at once, around GPU work
        self.gpu_lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
        self._pool = ThreadPoolExecutor(max_workers=1)
        self._next = None

    def _encode(self, buffer, batch_size, seed, city_weight):
        # sampled and encoded at once: safe while self-play adds games
        # with augment, each example through a random rotation or reflection
        # of the map: eight times the positions, and a map no longer
        # identifies its game
        return buffer.sample_examples(self.encoding, batch_size, seed,
                                      per_game=self.per_game,
                                      augment=self.augment,
                                      city_weight=city_weight,
                                      cut_off_weight=self.cut_off_weight,
                                      search_weight=self.search_value_weight,
                                      value_lambda=self.value_lambda)

    def set_learning_rate(self, rate: float):
        for group in self.optimizer.param_groups:
            group["lr"] = rate

    def step(self, buffer: pe.ReplayBuffer, batch_size: int,
             city_weight: float = 0.0) -> dict[str, float]:
        """One training step; its losses, or {} if it was skipped for a loss
        that wasn't finite (counted in `skipped`)."""
        # the city weight changes slowly: a batch made a step ago will do
        city_weight = round(city_weight, 3)
        key = (id(buffer), batch_size, city_weight)
        if self._next is not None and self._next[0] == key:
            examples = self._next[1].result()
        else:
            examples = self._encode(buffer, batch_size, self.steps, city_weight)
        # start on the next batch while this one trains
        self._next = (key, self._pool.submit(self._encode, buffer, batch_size,
                                             self.steps + 1, city_weight))
        with self.gpu_lock:
            batch = to_device(examples, self.device)
            if self.value_share < 1:
                keep = torch.rand(batch["value_weight"].shape,
                                  device=self.device) < self.value_share
                batch["value_weight"] = batch["value_weight"] * keep
            if self.value_per_turn:
                # 1 / the turn's positions, scaled to average 1 over the
                # batch so the value loss keeps its size
                per_turn = 1 / batch["turn_positions"].clamp(min=1)
                batch["value_weight"] = batch["value_weight"] * per_turn / per_turn.mean()
            self.model.train()
            out = self.model(batch["planes"], batch["globals"])
            result = losses(self.model, out, batch, self.weights)
            values = {name: value.item() for name, value in result.items()}
            # A loss that isn't finite (a bad target) would turn every
            # weight into NaN in one step: skip the batch instead
            finite = math.isfinite(values["total"])
            if finite:
                self.optimizer.zero_grad(set_to_none=True)
                result["total"].backward()
                torch.nn.utils.clip_grad_norm_(self.model.parameters(), 5.0)
                self.optimizer.step()
                self._update_ema()
            else:
                self.skipped += 1
                values = {}
        self.steps += 1
        if self.fresh_steps is not None:
            self.fresh_steps += 1
        return values

    @torch.no_grad()
    def _update_ema(self):
        d = self.ema_decay
        for averaged, live in zip(self.ema.parameters(), self.model.parameters()):
            averaged.mul_(d).add_(live.detach(), alpha=1 - d)
        for averaged, live in zip(self.ema.buffers(), self.model.buffers()):
            averaged.copy_(live)

    def drain(self):
        """Waits for the background batch, before the buffer changes."""
        if self._next is not None:
            self._next[1].result()
            self._next = None



@torch.no_grad()
@torch.no_grad()
def reanalyse(buffer: pe.ReplayBuffer, model: Network, encoding: pe.Encoding,
              device: torch.device, gpu_lock=None, new_only: bool = False,
              games_per_chunk: int = 32, batch: int = 1024) -> tuple[int, int]:
    """The network's value of the start of each player turn in the buffer's
    games, kept with them for the lambda-return value target (value_lambda):
    the games not reanalysed yet with `new_only`, else all of them. Safe while
    self-play adds games. Returns the games and positions evaluated."""
    lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
    seeds = buffer.game_seeds(without_estimates=new_only)
    positions = 0
    was_training = model.training
    model.eval()
    for k in range(0, len(seeds), games_per_chunk):
        planes, globals_, game_seeds, turns, players = buffer.turn_start_inputs(
            encoding, seeds[k:k + games_per_chunk])
        if len(planes) == 0:
            continue
        values = np.empty(len(planes), np.float32)
        for i in range(0, len(planes), batch):
            with lock:
                out = model(as_input(planes[i:i + batch], model, device),
                            as_input(globals_[i:i + batch], model, device))
                values[i:i + batch] = out["value"].float().cpu().numpy()
        buffer.set_value_estimates(game_seeds.tolist(), turns.tolist(), players.tolist(),
                                   values.tolist())
        positions += len(planes)
    model.train(was_training)
    return len(seeds), positions


def heldout_losses(model: Network, device: torch.device,
                   examples: dict[str, np.ndarray] | None,
                   gpu_lock=None) -> dict[str, float] | None:
    """The losses on a fixed set of examples from games kept out of training,
    so the numbers are comparable over time: a sign of overfitting if they
    rise while the training losses fall."""
    if examples is None:
        return None
    lock = gpu_lock if gpu_lock is not None else contextlib.nullcontext()
    with lock:
        was_training = model.training
        model.eval()
        batch = to_device(examples, device)
        out = model(batch["planes"], batch["globals"])
        result = losses(model, out, batch)
        values = {name: round(value.item(), 4) for name, value in result.items()}
        # the value's mean error, each player turn weighed once: how much
        # better off the network thinks the player to move is than they are
        # (it was +0.13 with per-move value weights)
        if "turn_positions" in batch:
            w = (batch["value_weight"] > 0) / batch["turn_positions"].clamp(min=1)
            if w.sum() > 0:
                bias = (w * (out["value"].float() - batch["value"])).sum() / w.sum()
                values["value_bias"] = round(bias.item(), 4)
        model.train(was_training)
    return values
