"""Plays the same game on the real game (the oracle) and on polyengine, and
compares them after every command (docs/oracle.md, notes/integration.md
section 4).

    .venv/bin/python src/oracle/mirror.py --type lakes --size 14 --games 5

The real game generates the map; polyengine starts from the same one. Each
step takes the real legal list, translates it, compares it with ours, plays
one of the commands on both, and compares the states. The first difference
in each game is reported, with the command before it.
"""

import argparse
import json
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mapstats  # noqa: E402  (also makes the build tree's polyengine importable)

pe = mapstats.pe


# ---- names: the game's enum names for ours, where they differ ----

def _normal(name):
    return "".join(ch for ch in name.lower() if ch.isalpha())


def _by_name(enum, exceptions):
    table = {_normal(e.name): e for e in enum}
    table.update({_normal(real): ours for real, ours in exceptions.items()})
    return table


UNITS = _by_name(pe.UnitType, {
    "Transportship": pe.UnitType.RAFT, "Scoutship": pe.UnitType.SCOUT,
    "Rammership": pe.UnitType.RAMMER, "Bombership": pe.UnitType.BOMBER,
    "Cloak_Boat": pe.UnitType.DINGHY})
# the game's own "Scout" (a 1-health unit) is not our scout ship
del UNITS[_normal("Scout")]
UNITS[_normal("Scoutship")] = pe.UnitType.SCOUT
IMPROVEMENTS = _by_name(pe.ImprovementType, {
    "Monument1": pe.ImprovementType.ALTAROFPEACE, "Monument2": pe.ImprovementType.TOWEROFWISDOM,
    "Monument3": pe.ImprovementType.GRANDBAZAAR, "Monument4": pe.ImprovementType.EMPERORSTOMB,
    "Monument5": pe.ImprovementType.GATEOFPOWER, "Monument6": pe.ImprovementType.PARKOFFORTUNE,
    "Monument7": pe.ImprovementType.EYEOFGOD})
TECHS = _by_name(pe.TechType, {"Shields": pe.TechType.STRATEGY})
REWARDS = _by_name(pe.CityReward, {})
TERRAINS = _by_name(pe.TerrainType, {})
RESOURCES = _by_name(pe.ResourceType, {})


def lookup(table, name):
    found = table.get(_normal(name))
    if found is None:
        raise KeyError(f"no polyengine name for {name!r}")
    return found


# ---- the real game ----

# the real game's player id for nature, which owns no seat
NATURE = 255

class RealGame:
    """One game in an oracle process."""

    def __init__(self, oracle, build, preset, size, tribes):
        self.process = subprocess.Popen([oracle, build], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, text=True, bufsize=1)
        self.ask_raw()  # the version line
        reply = self.ask(f"new {preset} {size} might crazy {' '.join(tribes)}")
        if "error" in reply:
            raise RuntimeError(reply["error"])
        self.ask("start 0")

    def ask_raw(self, line=None):
        if line is not None:
            self.process.stdin.write(line + "\n")
            self.process.stdin.flush()
        reply = self.process.stdout.readline()
        if not reply:
            raise RuntimeError(f"the oracle died on {line!r}")
        return json.loads(reply)

    def ask(self, line):
        return self.ask_raw(line)

    def state(self):
        return self.ask("state 0")

    def legal(self):
        return self.ask("legal 0")

    def apply(self, k):
        return self.ask(f"apply 0 {k}")

    def close(self):
        self.process.stdin.close()
        self.process.wait()


# ---- commands ----

def _tile(coordinates, n):
    return coordinates["y"] * n + coordinates["x"]


def to_command(real, n, seat_of):
    """A real command (as the oracle dumps it) as a polyengine Command."""
    t = real["$type"].removesuffix("Command")
    C, T = pe.Command, pe.CommandType
    at = _tile(real["Coordinates"], n) if "Coordinates" in real else -1
    if t == "Move":
        return C(T.MOVE, _tile(real["From"], n), _tile(real["To"], n))
    if t == "Attack":
        return C(T.ATTACK, _tile(real["Origin"], n), _tile(real["Target"], n))
    if t == "Build":
        if _normal(real["Type"]) == "starfishing":
            return C(T.HARVEST, at)
        return C(T.BUILD, at, arg=int(lookup(IMPROVEMENTS, real["Type"])))
    if t == "Train":
        return C(T.TRAIN, at, arg=int(lookup(UNITS, real["Type"])))
    if t == "Research":
        return C(T.RESEARCH, arg=int(lookup(TECHS, real["Type"])))
    if t == "CityReward":
        return C(T.CITYREWARD, at, arg=int(lookup(REWARDS, real["Reward"])))
    if t == "Upgrade":
        return C(T.UPGRADE, at, arg=int(lookup(UNITS, real["Type"])))
    if t == "Disembark":
        return C(T.DISEMBARK, _tile(real["From"], n) if "From" in real else at,
                 _tile(real["To"], n) if "To" in real else -1)
    if t in ("Recover", "HealOthers", "Promote", "Disband", "Capture", "ExamineRuins",
             "Destroy"):
        return C(getattr(T, t.upper()), at)
    if t in ("PeaceTreaty", "BreakPeace", "EstablishEmbassy", "DestroyEmbassy"):
        return C(getattr(T, t.upper()), arg=seat_of[real["OpponentId"]])
    if t == "PeaceRequestResponse":
        return C(T.PEACEREQUESTRESPONSE,
                 arg=seat_of[real["OpponentId"]] * 2 + (1 if real["Accepted"] else 0))
    if t == "EndTurn":
        return C(T.ENDTURN)
    raise KeyError(f"no translation for {real['$type']}")


def command_key(c):
    return (int(c.type), c.from_tile, c.to_tile, c.arg)


# ---- states, as the parts both engines model ----

def project_real(state, seat_of):
    m = state["Map"]
    n = m["width"]
    tiles, units = {}, {}
    for tile in m["tiles"]:
        t = _tile(tile["coordinates"], n)
        imp = tile["improvement"]
        building = None
        if imp:
            kind = imp["type"]
            if _normal(kind) == "city":
                # a city's owner is its tile's; a village is a city of no one
                owner = tile["owner"]
                building = ("city", seat_of[owner], imp["level"]) if owner else ("village",)
            else:
                building = (int(lookup(IMPROVEMENTS, kind)),)
        tiles[t] = {
            "terrain": int(lookup(TERRAINS, tile["terrain"])),
            "resource": int(lookup(RESOURCES, tile["resource"]["type"])) if tile["resource"] else 0,
            "building": building,
            "road": tile.get("hasRoad", False),
        }
        unit = tile["unit"]
        if unit:
            units[t] = {"type": int(lookup(UNITS, unit["type"])),
                        "owner": seat_of[unit["owner"]], "hp": unit["health"]}
    players = {}
    for player in state["PlayerStates"]:
        if player["Id"] in seat_of:
            players[seat_of[player["Id"]]] = {
                "stars": player["currency"], "score": player["score"],
                "techs": sorted(int(lookup(TECHS, t)) for t in player["availableTech"]),
                "met": sorted(seat_of[p] for p in player["knownPlayers"] if p in seat_of)}
    return {"tiles": tiles, "units": units, "players": players}


def project_ours(game):
    tiles, units = {}, {}
    city_at = {c.tile: c for c in game.cities}
    for t, tile in enumerate(game.tiles):
        building = None
        if tile.building == pe.ImprovementType.CITY:
            c = city_at[t]
            building = ("city", c.owner, c.level)
        elif tile.building == pe.ImprovementType.VILLAGE:
            building = ("village",)
        elif tile.building != pe.ImprovementType.NONE:
            building = (int(tile.building),)
        tiles[t] = {"terrain": int(tile.terrain), "resource": int(tile.resource),
                    "building": building, "road": tile.road}
    for u in game.units:
        units[u.tile] = {"type": int(u.type), "owner": u.owner, "hp": u.hp}
    players = {p: {"stars": player.stars, "score": game.score(p),
                   "techs": sorted(player.techs),
                   "met": [q for q in range(len(game.players)) if q != p and player.has_met(q)]}
               for p, player in enumerate(game.players)}
    return {"tiles": tiles, "units": units, "players": players}


def differences(real, ours, limit=8):
    out = []
    for t in real["tiles"]:
        if real["tiles"][t] != ours["tiles"].get(t):
            out.append(f"tile {t}: real {real['tiles'][t]} ours {ours['tiles'].get(t)}")
    for t in set(real["units"]) | set(ours["units"]):
        if real["units"].get(t) != ours["units"].get(t):
            out.append(f"unit on {t}: real {real['units'].get(t)} ours {ours['units'].get(t)}")
    for p in real["players"]:
        if real["players"][p] != ours["players"].get(p):
            out.append(f"player {p}: real {real['players'][p]} ours {ours['players'].get(p)}")
    return out[:limit]


# ---- one game on both ----

def save(args, seed, real, legal):
    """The real state and legal list at a difference, for a closer look (the
    real maps aren't seeded, so a game can't be replayed)."""
    if args.save:
        os.makedirs(args.save, exist_ok=True)
        with open(os.path.join(args.save, f"{seed}.json"), "w") as f:
            json.dump({"state": real.state(), "legal": legal}, f)

def play(args, seed):
    rng = random.Random(seed)
    tribes = rng.sample(mapstats.TRIBES, 2)
    preset = mapstats.PRESETS.get(args.type, args.type)
    real = RealGame(args.oracle, args.build, preset, args.size, tribes)
    try:
        state = real.state()
        seat_of = {}
        for player in state["PlayerStates"]:
            if player["Id"] != NATURE:
                seat_of[player["Id"]] = len(seat_of)
        map_type = pe.MapType[args.type.upper()]
        start = mapstats.real_map(state["Map"], tribes)
        game = pe.Game(mapstats.map_data(start, map_type),
                       [pe.Tribe[t.upper()] for t in tribes], seed=seed)
        n = game.size
        last = "the start"
        history = []
        for step in range(args.steps):
            legal = real.legal()
            while legal and legal[0]["PlayerId"] == NATURE:
                # nature has a turn of its own, after every player's; ours
                # has none, so only the real game ends it
                rejected = real.apply(next(i for i, c in enumerate(legal)
                                           if c["$type"] == "EndTurnCommand"))
                if rejected:
                    return step, "the real game rejected nature's end turn", [str(rejected)]
                legal = real.legal()
            diff = differences(project_real(real.state(), seat_of), project_ours(game))
            if diff:
                save(args, seed, real, legal)
                return step, f"state differs after {last}", history[-args.history:] + diff
            translated = [to_command(c, n, seat_of) for c in legal]
            theirs = {command_key(c) for c in translated}
            mine = {command_key(c) for c in game.legal_commands()
                    if c.type not in (pe.CommandType.RESIGN, pe.CommandType.STAY)}
            if theirs != mine:
                save(args, seed, real, legal)
                only_real = [game.describe(c) for c in translated if command_key(c) not in mine]
                only_ours = [game.describe(c) for c in game.legal_commands()
                             if command_key(c) in mine - theirs]
                return step, f"legal lists differ after {last}", history[-args.history:] + \
                    [f"only real: {only_real[:6]}", f"only ours: {only_ours[:6]}"]
            ends = [i for i, c in enumerate(legal) if c["$type"] == "EndTurnCommand"]
            others = [i for i, c in enumerate(legal) if c["$type"] != "EndTurnCommand"]
            k = rng.choice(others) if others and (rng.random() < 0.85 or not ends) else ends[0]
            rejected = real.apply(k)
            if rejected:
                return step, f"the real game rejected {legal[k]}", [str(rejected)]
            last = f"{game.describe(translated[k])} ({legal[k]['$type']})"
            history.append(f"turn {game.turn} player {game.current_player}: {last}")
            game.apply(translated[k])
        return args.steps, "no differences", []
    finally:
        real.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--type", default="lakes",
                        choices=[t.name.lower() for t in pe.MapType])
    parser.add_argument("--size", type=int, default=14)
    parser.add_argument("--games", type=int, default=5)
    parser.add_argument("--steps", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--save", help="save the real state at each difference here")
    parser.add_argument("--history", type=int, default=0,
                        help="show this many commands before a difference")
    parser.add_argument("--oracle", default="build/release/oracle")
    parser.add_argument("--build", default="game/current")
    args = parser.parse_args()
    for g in range(args.games):
        steps, verdict, detail = play(args, args.seed + g)
        print(f"game {g}: {verdict} at step {steps}")
        for line in detail:
            print("    " + line)


if __name__ == "__main__":
    main()
