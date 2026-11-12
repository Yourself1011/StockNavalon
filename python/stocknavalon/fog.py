"""When training brings fog in (docs/stockNavalon.md section 6): none at
first, then a ramp by training steps, started once by how the fog-free bot
plays and never reversed.

It mustn't follow a live measure of play, or it can loop the way the old
score weight did (more fog, worse play, less fog, better play, more fog).
So performance decides only when the ramp starts, once, and the ramp then
moves with training steps alone. The one thing play can still do is pause
it: after several failed gates in a row the share holds where it is until a
gate passes. Pausing can't oscillate the way reversing could.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass


@dataclass
class FogSchedule:
    # the share of recent self-play games decided by a capture that starts
    # the ramp: the fog-free bot is winning games, not stalling
    trigger: float = 0.5
    # training steps from no fog to all fog
    ramp_steps: int = 25_000
    # failed gates in a row that pause the ramp
    brake_failures: int = 3

    # the state, kept in the checkpoint
    start_step: int | None = None
    paused_steps: int = 0
    failures: int = 0
    last_step: int | None = None

    def share(self, step: int) -> float:
        """The share of games played with fog at this training step."""
        if self.start_step is None:
            return 0.0
        progress = step - self.start_step - self.paused_steps
        return min(1.0, max(0.0, progress / self.ramp_steps))

    def braking(self, step: int) -> bool:
        return self.failures >= self.brake_failures and 0.0 < self.share(step) < 1.0

    def update(self, step: int, decided_share: float, window_full: bool) -> str | None:
        """Called as training goes. Returns "start" when the ramp starts."""
        event = None
        if self.start_step is None and window_full and decided_share >= self.trigger:
            self.start_step = step
            event = "start"
        # steps spent braking don't count towards the ramp
        if self.last_step is not None and self.braking(self.last_step):
            self.paused_steps += step - self.last_step
        self.last_step = step
        return event

    def gate(self, verdict: str):
        """A gate's result: failures in a row pause the ramp, a pass resumes
        it; an inconclusive gate changes nothing."""
        if verdict == "fail":
            self.failures += 1
        elif verdict == "pass":
            self.failures = 0

    def state(self) -> dict:
        return asdict(self)
