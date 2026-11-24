"""Mirrored games set up to exercise one rule (docs/oracle.md, "Scenarios").

    .venv/bin/python src/oracle/scenarios.py starvation --games 10

Each scenario changes the start of a mirrored game the same way on both
engines (the real one through its own actions, oracle `act` and `set`; ours
through Game's set-up calls), then plays it with a chooser that favours the
commands the rule is about, comparing after every command as mirror.py does.
"""

import argparse
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mirror  # noqa: E402

pe = mirror.pe


class Both:
    """The same edit on both games: `seat` is our seat, the real id seat + 1."""

    def __init__(self, real, game, seat_of):
        self.real, self.game = real, game
        self.id = {seat: real_id for real_id, seat in seat_of.items()}
        self.n = game.size

    def ask(self, line):
        reply = self.real.ask(line)
        if "error" in reply:
            raise RuntimeError(f"{line}: {reply['error']}")

    def act(self, seat, action):
        """A real action as `seat`: the game only runs actions of the player
        whose turn it is, so it's made that player's turn for it. A level-up
        waiting would hold it back, so its reward is picked first."""
        self.rewards()
        current = self.real.state()["CurrentPlayerIndex"]
        self.ask(f"set 0 game <CurrentPlayerIndex>k__BackingField {seat}")
        self.ask(f"act 0 {action}")
        self.ask(f"set 0 game <CurrentPlayerIndex>k__BackingField {current}")

    def tile(self, x, y):
        return y * self.n + x

    def stars(self, seat, stars):
        self.ask(f"set 0 player {self.id[seat]} currency {stars}")
        self.game.set_stars(seat, stars)

    def tech(self, seat, name):
        self.act(seat, f"ResearchAction {self.id[seat]} {name} 0")
        self.game.give_tech(seat, mirror.lookup(mirror.TECHS, name))

    def unit(self, seat, name, x, y, ready=False):
        self.act(seat, f"TrainAction {self.id[seat]} {name} {x},{y} 0")
        # spent for the player whose turn it is, ready for the other (the
        # game readies a player's units as its turn ends, ours as the next
        # begins)
        spent = int(seat == self.game.current_player and not ready)
        self.ask(f"set 0 unit {x} {y} moved {spent}")
        self.ask(f"set 0 unit {x} {y} attacked {spent}")
        unit = self.game.spawn_unit(seat, mirror.lookup(mirror.UNITS, name), self.tile(x, y))
        if ready:
            self.game.set_unit_ready(unit)
        return unit

    def build(self, seat, name, x, y):
        self.act(seat, f"BuildAction {self.id[seat]} {name} {x},{y} 0")
        self.game.place_building(seat, self.tile(x, y), mirror.lookup(mirror.IMPROVEMENTS, name))

    def population(self, seat, x, y, amount):
        for _ in range(abs(amount)):
            if amount > 0:
                self.act(seat, f"IncreasePopulationAction {self.id[seat]} {x},{y} {x},{y} 0")
            else:
                self.act(seat, f"DecreasePopulationAction {self.id[seat]} {x},{y} 0")
        self.game.add_population(self.tile(x, y), amount)

    def rewards(self, prefer=("Workshop", "CityWall", "BorderGrowth", "SuperUnit")):
        """Picks every pending city reward on both (a preferred one, never the
        Explorer, which is random)."""
        while (legal := self.real.legal()) and legal[0]["$type"] == "CityRewardCommand":
            options = [mirror._normal(c["Reward"]) for c in legal]
            k = next((options.index(mirror._normal(p)) for p in prefer
                      if mirror._normal(p) in options),
                     next(i for i, o in enumerate(options) if o != "explorer"))
            self.real.apply(k)
            self.game.apply(mirror.to_command(legal[k], self.game, {i: s for s, i in self.id.items()}))

    def level_up(self, x, y, level):
        """The current player's city at (x, y) up to `level`, its rewards
        picked on both."""
        seat = self.game.current_player
        self.rewards()
        while (city := next(c for c in self.game.cities if c.tile == self.tile(x, y))).level < level:
            self.population(seat, x, y, city.level + 1 - city.population)
            self.rewards()

    def city_level(self, x, y, level):
        self.ask(f"set 0 improvement {x} {y} level {level}")
        self.game.set_city_level(self.tile(x, y), level)

    def embassy(self, builder, host):
        self.act(builder, f"EstablishEmbassyAction {self.id[builder]} {self.id[host]}")
        self.game.add_embassy(builder, host)

    def settle(self):
        """Brings the side effects of the edits into line, so the scenario
        starts the same on both: the real stars, meetings and exploration
        for ours, our score for the real one (its score is a running total,
        which edits like a city's level skip)."""
        state = self.real.state()
        for player in state["PlayerStates"]:
            if player["Id"] not in self.id.values():
                continue
            seat = next(s for s, i in self.id.items() if i == player["Id"])
            self.game.set_stars(seat, player["currency"])
            for other, real_id in self.id.items():
                if other != seat:
                    self.game.set_met(seat, other, real_id in player["knownPlayers"])
            seen = [mirror._tile(t["coordinates"], self.n) for t in state["Map"]["tiles"]
                    if player["Id"] in t["explorers"]]
            self.game.force_reveal(seat, seen)
            # (not while a level-up waits for its reward: ours has scored the
            # level, the game will when the reward is picked)
            if not any(c.pending_rewards for c in self.game.cities if c.owner == seat):
                self.ask(f"set 0 player {player['Id']} score {self.game.score(seat)}")

    def capture_village(self, seat, x, y):
        self.act(seat, f"CaptureCityAction {self.id[seat]} {x},{y} 0")
        self.game.found_city(self.tile(x, y), seat)

    def peace(self, a, b):
        self.act(b, f"PeaceRequestResponseAction {self.id[b]} {self.id[a]} 1")
        self.game.make_peace(a, b)


def capital(game, seat):
    t = game.cities[game.players[seat].capital].tile
    return game.tile_x(t), game.tile_y(t)


def neighbours(game, x, y, terrains):
    out = []
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            nx, ny = x + dx, y + dy
            if (dx or dy) and 0 <= nx < game.size and 0 <= ny < game.size and \
                    game.tiles[ny * game.size + nx].terrain in terrains and \
                    game.tiles[ny * game.size + nx].building == pe.ImprovementType.NONE:
                out.append((nx, ny))
    return out


def favouring(predicate, share=0.7):
    """A chooser: a command the predicate likes `share` of the time when there
    is one, else mirror's random choice."""
    def choose(rng, legal):
        liked = [i for i, c in enumerate(legal) if predicate(c)]
        if liked and rng.random() < share:
            return rng.choice(liked)
        return mirror.random_choice(rng, legal)
    return choose


def kind(*types):
    return lambda c: c["$type"] in types


# ---- the scenarios: (setup, chooser) ----

def starvation(rng):
    """Cities on negative population: income and score."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        for seat in (0, 1):
            x, y = capital(game, seat)
            both.population(seat, x, y, -rng.randint(1, 6))
    return setup, favouring(kind("EndTurnCommand"), 0.5)


def embassies(rng):
    """Embassies each way, at peace or not, and attacks: income and war."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        both.embassy(0, 1)
        if rng.random() < 0.5:
            both.embassy(1, 0)
        if rng.random() < 0.5:
            both.peace(0, 1)
    return setup, favouring(kind("EndTurnCommand", "AttackCommand", "BreakPeaceCommand"), 0.6)


def pushes(rng):
    """A super unit on an occupied capital, ports beside it."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        seat = game.current_player
        both.tech(seat, "Fishing")
        x, y = capital(game, seat)
        for wx, wy in neighbours(game, x, y, (pe.TerrainType.WATER,)):
            if rng.random() < 0.6 and game.tiles[both.tile(wx, wy)].resource == pe.ResourceType.NONE:
                both.build(seat, "Port", wx, wy)
        both.level_up(x, y, 4)
        both.settle()
        # the next level brings the super unit (chosen below)
        city = next(c for c in game.cities if c.tile == both.tile(x, y))
        both.population(seat, x, y, city.level + 1 - city.population)
    def choose(rng_, legal):
        rewards = [i for i, c in enumerate(legal) if c["$type"] == "CityRewardCommand"]
        supers = [i for i in rewards if mirror._normal(legal[i]["Reward"]) == "superunit"]
        if supers:
            return supers[0]
        grow = [i for i, c in enumerate(legal) if c["$type"] == "BuildCommand"]
        if grow and rng_.random() < 0.5:
            return rng_.choice(grow)
        return mirror.random_choice(rng_, legal)
    return setup, choose


def fights(rng):
    """Units of both sides next to each other: attacks, facing, pushes."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        x0, y0 = capital(game, 0)
        x1, y1 = capital(game, 1)
        mx, my = (x0 + x1) // 2, (y0 + y1) // 2
        land = (pe.TerrainType.FIELD, pe.TerrainType.FOREST)
        spots = neighbours(game, mx, my, land)
        rng.shuffle(spots)
        for i, (x, y) in enumerate(spots[:6]):
            both.unit(i % 2, rng.choice(["Warrior", "Rider", "Archer", "Defender"]), x, y)
    return setup, favouring(kind("AttackCommand"), 0.8)


def stomps(rng):
    """A Giant beside a port of its own, to embark as a Juggernaut, among
    units of both sides."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        seat = game.current_player
        both.tech(seat, "Fishing")
        both.tech(seat, "Sailing")
        land = (pe.TerrainType.FIELD, pe.TerrainType.FOREST)
        x0, y0 = capital(game, seat)
        for wx, wy in neighbours(game, x0, y0, (pe.TerrainType.WATER,)) + \
                [(x, y) for y in range(game.size) for x in range(game.size)]:
            t = game.tiles[wy * game.size + wx]
            if t.terrain != pe.TerrainType.WATER or game.tile_owner(wy * game.size + wx) != seat or \
                    t.building != pe.ImprovementType.NONE:
                continue
            shore = [p for p in neighbours(game, wx, wy, land) if game.tiles[p[1] * game.size + p[0]].unit < 0]
            if not shore:
                continue
            both.build(seat, "Port", wx, wy)
            # a level-up from the port holds back every later action
            both.rewards()
            gx, gy = shore[0]
            both.unit(seat, "Giant", gx, gy, ready=True)
            around = [p for p in neighbours(game, wx, wy, land) if game.tiles[p[1] * game.size + p[0]].unit < 0]
            for i, (nx, ny) in enumerate(around[:4]):
                both.unit(1 - seat if i % 2 == 0 else seat, "Warrior", nx, ny)
            return
    def choose(rng_, legal):
        moves = [i for i, c in enumerate(legal) if c["$type"] in ("MoveCommand", "AttackCommand")]
        if moves and rng_.random() < 0.9:
            return rng_.choice(moves)
        return mirror.random_choice(rng_, legal)
    return setup, choose


def boats(rng):
    """Ports, sea techs and stars: embarking, upgrades, starfish, disembarking."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        for seat in (0, 1):
            for tech in ("Fishing", "Sailing", "Ramming", "Navigation"):
                both.tech(seat, tech)
            both.stars(seat, 60)
            x, y = capital(game, seat)
            for wx, wy in neighbours(game, x, y, (pe.TerrainType.WATER,))[:2]:
                both.build(seat, "Port", wx, wy)
    return setup, favouring(kind("MoveCommand", "UpgradeCommand", "BuildCommand", "DisembarkCommand"), 0.8)


def ports(rng):
    """A second city by the water, ports and roads to build: connections over
    water."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        seat = game.current_player
        for tech in ("Fishing", "Sailing", "Riding", "Roads"):
            both.tech(seat, tech)
        both.stars(seat, 100)
        x0, y0 = capital(game, seat)
        villages = sorted((game.distance(game.index(x, y), game.index(x0, y0)), x, y)
                          for y in range(game.size) for x in range(game.size)
                          if game.tiles[y * game.size + x].building == pe.ImprovementType.VILLAGE)
        for _, x, y in villages[:2]:
            both.capture_village(seat, x, y)
    def portish(c):
        return c["$type"] == "BuildCommand" and mirror._normal(c["Type"]) in ("port", "road", "bridge")
    return setup, favouring(portish, 0.8)


def connections(rng):
    """Roads, bridges and ports near the capitals, and captures."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        for seat in (0, 1):
            for tech in ("Riding", "Roads", "Fishing", "Sailing"):
                both.tech(seat, tech)
            both.stars(seat, 80)
    def roadish(c):
        return (c["$type"] == "BuildCommand" and mirror._normal(c["Type"]) in ("road", "bridge", "port")) or \
            c["$type"] in ("CaptureCommand", "MoveCommand")
    return setup, favouring(roadish, 0.8)


def kills(rng):
    """Enemies on 1 HP next to a boat by the shore and land units on other
    terrain: what moves into the dead unit's tile, Persist."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        seat = game.current_player
        for tech in ("Fishing", "Sailing", "Ramming", "Riding", "Archery"):
            both.tech(seat, tech)
        both.stars(seat, 30)
        land = (pe.TerrainType.FIELD, pe.TerrainType.FOREST, pe.TerrainType.MOUNTAIN)
        weak = []
        x0, y0 = capital(game, seat)
        for wx, wy in neighbours(game, x0, y0, (pe.TerrainType.WATER,)):
            if game.tile_owner(both.tile(wx, wy)) != seat:
                continue
            shore = [p for p in neighbours(game, wx, wy, land) if game.tiles[both.tile(*p)].unit < 0]
            if not shore:
                continue
            both.build(seat, "Port", wx, wy)
            both.rewards()
            if len(shore) < 2:
                continue
            # a Warrior moved onto the port becomes a Raft carrying it
            sx, sy = shore[1]
            both.unit(seat, "Warrior", sx, sy, ready=True)
            ids = {i: s for s, i in both.id.items()}
            def play(pick):
                both.rewards()
                legal = real.legal()
                k = next((i for i, c in enumerate(legal) if pick(c)), None)
                if k is None:
                    st = real.state()
                    raise RuntimeError(f"not offered: {st['Map']['tiles'][both.tile(wx, wy)]['unit']}, "
                                       f"{[p['currency'] for p in st['PlayerStates']]}")
                real.apply(k)
                game.apply(mirror.to_command(legal[k], game, ids))
            play(lambda c: c["$type"] == "MoveCommand" and c["To"]["x"] == wx and c["To"]["y"] == wy)
            # (the move can find a lighthouse)
            both.rewards()
            both.ask(f"set 0 unit {wx} {wy} moved 0")
            both.ask(f"set 0 unit {wx} {wy} attacked 0")
            game.set_unit_ready(game.tiles[both.tile(wx, wy)].unit)
            boat = rng.choice(["Rammership", "Scoutship"])
            play(lambda c: c["$type"] == "UpgradeCommand" and c["Type"] == boat)
            weak.append(shore[0])
            break
        # land attackers beside other targets
        for x, y in neighbours(game, x0, y0, (pe.TerrainType.FIELD, pe.TerrainType.FOREST)):
            if game.tiles[both.tile(x, y)].unit >= 0 or (x, y) in weak or len(weak) >= 4:
                continue
            targets = [p for p in neighbours(game, x, y, land) if game.tiles[both.tile(*p)].unit < 0
                       and p not in weak and abs(p[0] - x0) + abs(p[1] - y0) > 0]
            if not targets:
                continue
            both.unit(seat, rng.choice(["Warrior", "Rider", "Archer"]), x, y, ready=True)
            weak.append(rng.choice(targets))
        both.rewards()
        for x, y in weak:
            both.unit(1 - seat, rng.choice(["Warrior", "Defender"]), x, y)
            both.ask(f"set 0 unit {x} {y} health 10")
            game.set_unit_hp(both.tile(x, y), 10)
    return setup, favouring(kind("AttackCommand"), 0.9)


def portkills(rng):
    """Enemies on 1 HP on a port, ours and theirs, a Warrior of ours beside
    each: does the killer move onto the port?"""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        seat = game.current_player
        for s in (seat, 1 - seat):
            both.tech(s, "Fishing")
        land = (pe.TerrainType.FIELD, pe.TerrainType.FOREST)
        for owner in (seat, 1 - seat):
            x0, y0 = capital(game, owner)
            for wx, wy in neighbours(game, x0, y0, (pe.TerrainType.WATER,)):
                if game.tile_owner(both.tile(wx, wy)) != owner:
                    continue
                shore = [p for p in neighbours(game, wx, wy, land) if game.tiles[both.tile(*p)].unit < 0]
                if not shore:
                    continue
                both.build(owner, "Port", wx, wy)
                both.unit(seat, "Warrior", *shore[0], ready=True)
                both.unit(1 - seat, "Warrior", wx, wy)
                both.ask(f"set 0 unit {wx} {wy} health 10")
                game.set_unit_hp(both.tile(wx, wy), 10)
                break
    return setup, favouring(kind("AttackCommand"), 0.95)


def cloaks(rng):
    """Cloaks beside their own port: hiding, embarking and landing."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        seat = game.current_player
        for tech in ("Fishing", "Sailing", "Diplomacy"):
            both.tech(seat, tech)
        both.stars(seat, 40)
        x0, y0 = capital(game, seat)
        land = (pe.TerrainType.FIELD, pe.TerrainType.FOREST)
        for wx, wy in neighbours(game, x0, y0, (pe.TerrainType.WATER,)):
            shore = [p for p in neighbours(game, wx, wy, land) if game.tiles[both.tile(*p)].unit < 0]
            if game.tile_owner(both.tile(wx, wy)) == seat and shore:
                both.build(seat, "Port", wx, wy)
                both.unit(seat, "Cloak", *shore[0], ready=True)
                break
    return setup, favouring(kind("MoveCommand", "DisembarkCommand"), 0.9)


def infiltrations(rng):
    """Cloaks beside the enemy capital, empty or defended: the Daggers, their
    homes, and captures after."""
    def setup(real, game, seat_of):
        both = Both(real, game, seat_of)
        seat = game.current_player
        for tech in ("Diplomacy", "Fishing"):
            both.tech(seat, tech)
        both.stars(seat, 30)
        x0, y0 = capital(game, 1 - seat)
        both.city_level(x0, y0, 3)
        land = (pe.TerrainType.FIELD, pe.TerrainType.FOREST)
        free = [p for p in neighbours(game, x0, y0, land) if game.tiles[both.tile(*p)].unit < 0]
        for x, y in free[:2]:
            both.unit(seat, "Cloak", x, y, ready=True)
        if rng.random() < 0.5 and (unit := game.tiles[both.tile(x0, y0)].unit) >= 0:
            # an empty city half the time
            both.ask(f"set 0 unit {x0} {y0} health 1")
            game.set_unit_hp(both.tile(x0, y0), 1)
    return setup, favouring(kind("AttackCommand", "CaptureCommand", "MoveCommand"), 0.9)


SCENARIOS = {f.__name__: f for f in (starvation, embassies, pushes, fights, stomps, boats, ports,
                                     connections, kills, portkills, cloaks, infiltrations)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("scenario", choices=list(SCENARIOS) + ["all"])
    parser.add_argument("--type", default="lakes")
    parser.add_argument("--size", type=int, default=11)
    parser.add_argument("--games", type=int, default=10)
    parser.add_argument("--steps", type=int, default=400)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--oracle", default="build/release/oracle")
    parser.add_argument("--build", default="game/current")
    parser.add_argument("--history", type=int, default=4)
    parser.add_argument("--save", help="save the real state at each difference here")
    args = parser.parse_args()
    names = list(SCENARIOS) if args.scenario == "all" else [args.scenario]
    for name in names:
        for g in range(args.games):
            seed = args.seed + g
            setup, choose = SCENARIOS[name](random.Random(seed))

            def settled(real, game, seat_of, setup=setup):
                # a round first, so units made for the scenario aren't made
                # in turn 0
                for _ in range(2):
                    while True:
                        legal = real.legal()
                        end = next(i for i, c in enumerate(legal) if c["$type"] == "EndTurnCommand")
                        nature = legal[0]["PlayerId"] == mirror.NATURE
                        real.apply(end)
                        if not nature:
                            break
                    game.apply(pe.Command(pe.CommandType.ENDTURN))
                while (legal := real.legal()) and legal[0]["PlayerId"] == mirror.NATURE:
                    real.apply(next(i for i, c in enumerate(legal) if c["$type"] == "EndTurnCommand"))
                # a level-up waiting would hold back every action the setup
                # makes
                Both(real, game, seat_of).rewards()
                setup(real, game, seat_of)
                Both(real, game, seat_of).settle()
            try:
                steps, verdict, detail = mirror.play(args, seed, settled, choose)
            except RuntimeError as e:
                steps, verdict, detail = 0, f"setup failed: {e}", []
            print(f"{name} {g}: {verdict} at step {steps}")
            for line in detail:
                print("    " + line)


if __name__ == "__main__":
    main()
