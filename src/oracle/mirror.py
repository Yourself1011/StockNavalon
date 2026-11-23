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
    return "".join(ch for ch in name.lower() if ch.isalnum())


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

    def __init__(self, oracle, build, preset, size, tribes, keep_handicaps=False):
        self.process = subprocess.Popen([oracle, build], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, text=True, bufsize=1)
        self.ask_raw()  # the version line
        reply = self.ask(f"new {preset} {size} might crazy {' '.join(tribes)}")
        if "error" in reply:
            raise RuntimeError(reply["error"])
        # with keep_handicaps the AI seats keep Crazy's (for ai_move)
        self.ask("start 0 keep" if keep_handicaps else "start 0")

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

    def ai_move(self):
        """The game's AI's next command for the current player (after a
        legal()): {"command": ..., "index": its place in that list or -1}.
        apply("ai") plays it."""
        return self.ask("ai 0")

    def close(self):
        self.process.stdin.close()
        self.process.wait()


# the game's directions, in its order (a unit's facing)
DIRECTIONS = ["SW", "W", "NW", "N", "NE", "E", "SE", "S"]


# ---- commands ----

def _tile(coordinates, n):
    return coordinates["y"] * n + coordinates["x"]


LAND = (pe.TerrainType.FIELD, pe.TerrainType.FOREST, pe.TerrainType.MOUNTAIN)
BOATS = (pe.UnitType.RAFT, pe.UnitType.SCOUT, pe.UnitType.RAMMER, pe.UnitType.BOMBER,
         pe.UnitType.JUGGERNAUT, pe.UnitType.DINGHY, pe.UnitType.PIRATE)


def to_command(real, game, seat_of):
    """A real command (as the oracle dumps it) as a polyengine Command for
    the same position in our game."""
    n = game.size
    t = real["$type"].removesuffix("Command")
    C, T = pe.Command, pe.CommandType
    at = _tile(real["Coordinates"], n) if "Coordinates" in real else -1
    if t == "Move":
        start, end = _tile(real["From"], n), _tile(real["To"], n)
        # a boat moving onto land is our Disembark
        boat = any(u.tile == start and u.type in BOATS for u in game.units) and \
            game.tiles[end].terrain in LAND
        return C(T.DISEMBARK if boat else T.MOVE, start, end)
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
        # the game's Disembark is the one on a bridge: where the unit is
        return C(T.DISEMBARK, at, at)
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
    if t == "Stay":
        # the game's bots only: the unit is done for the turn
        return C(T.STAY, at)
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
        building, road = None, False
        if imp:
            kind = imp["type"]
            if _normal(kind) == "city":
                # a city's owner is its tile's; a village is a city of no one
                owner = tile["owner"]
                # its xp is our population: the progress through its level
                # (its population is a running total)
                building = ("city", seat_of[owner], imp["level"], imp["xp"]) \
                    if owner else ("village",)
            elif _normal(kind) == "bridge":
                # ours: a road on the water
                building, road = None, True
            else:
                building = (int(lookup(IMPROVEMENTS, kind)),)
        resource = int(lookup(RESOURCES, tile["resource"]["type"])) if tile["resource"] else 0
        tiles[t] = {
            "terrain": int(lookup(TERRAINS, tile["terrain"])),
            "resource": resource,
            "building": building,
            "road": tile["hasRoad"] or road,
        }
        unit = tile["unit"]
        if unit:
            units[t] = {"type": int(lookup(UNITS, unit["type"])),
                        "owner": seat_of[unit["owner"]], "hp": unit["health"],
                        # a unit made on the map's centre faces no way
                        # ("NONE"); ours then faces the first direction
                        "facing": DIRECTIONS.index(unit["direction"])
                        if unit["direction"] in DIRECTIONS else 0}
    players = {}
    for player in state["PlayerStates"]:
        if player["Id"] in seat_of:
            players[seat_of[player["Id"]]] = {
                "stars": player["currency"], "score": player["score"],
                "techs": sorted(int(lookup(TECHS, t)) for t in player["availableTech"]),
                "met": sorted(seat_of[p] for p in player["knownPlayers"] if p in seat_of),
                "pacifist": pacifist_real(player),
                "cooldown": sorted(seat_of[q] for q, r in player["relations"]
                                   if q in seat_of and r["State"] == "CoolDown"),
                "peace": sorted(seat_of[q] for q, r in player["relations"]
                                if q in seat_of and r["State"] == "Peace"),
                # the players with an embassy in this one's capital
                "embassies": sorted(seat_of[q] for q, r in player["relations"]
                                    if q in seat_of and r["EmbassyLevel"] >= 1),
                "explored": sorted(_tile(t["coordinates"], n) for t in m["tiles"]
                                   if player["Id"] in t["explorers"])}
    return {"tiles": tiles, "units": units, "players": players}


def pacifist_real(player):
    """The real count toward Pacifist while it runs; "done" once it's there
    (the game marks it completed a little later)."""
    for task in player["tasks"]:
        if task["$type"] == "PacifistTask":
            return "done" if task["IsCompleted"] or task["Turns"] > 4 else task["Turns"]
    return None


def pacifist_turns(game, p):
    """Our count toward Pacifist, as the real task shows it: while it's
    running (Meditation, not done yet)."""
    if int(pe.Task.PACIFIST) in game.players[p].tasks_done:
        return "done"
    if int(pe.TechType.MEDITATION) not in game.players[p].techs:
        return None
    return pe.make_view(game, p).peaceful_turns


def project_ours(game):
    tiles, units = {}, {}
    city_at = {c.tile: c for c in game.cities}
    city_index = {c.tile: i for i, c in enumerate(game.cities)}
    for t, tile in enumerate(game.tiles):
        building = None
        if tile.building == pe.ImprovementType.CITY:
            c = city_at[t]
            building = ("city", c.owner, c.level, c.population if not c.pending_rewards else None)
        elif tile.building == pe.ImprovementType.VILLAGE:
            building = ("village",)
        elif tile.building != pe.ImprovementType.NONE:
            building = (int(tile.building),)
        tiles[t] = {"terrain": int(tile.terrain), "resource": int(tile.resource),
                    "building": building, "road": tile.road}
    for u in game.units:
        units[u.tile] = {"type": int(u.type), "owner": u.owner, "hp": u.hp, "facing": u.facing}
    pending = {c.owner for c in game.cities if c.pending_rewards}
    players = {p: {"stars": player.stars,
                   # the real game scores a level once its reward is picked
                   "score": game.score(p) if p not in pending else None,
                   "techs": sorted(player.techs),
                   "met": [q for q in range(len(game.players)) if q != p and player.has_met(q)],
                   "pacifist": pacifist_turns(game, p),
                   "cooldown": [q for q in range(len(game.players)) if player.cooling_down_with(q)],
                   "peace": [q for q in range(len(game.players)) if q != p and game.at_peace(p, q)],
                   "embassies": [q for q in range(len(game.players))
                                 if q != p and game.players[q].has_embassy_with(p)],
                   "explored": [t for t, tile in enumerate(game.tiles) if tile.explored_by(p)]}
               for p, player in enumerate(game.players)}
    return {"tiles": tiles, "units": units, "players": players}


def differences(real, ours, current, limit=8):
    out = []
    for t in real["tiles"]:
        building = ours["tiles"][t]["building"]
        theirs = real["tiles"][t]["building"]
        if building and building[0] == "city" and theirs and theirs[0] == "city":
            if building[2] is None:
                real["tiles"][t]["building"] = theirs[:2] + (None, None)
            elif building[3] is None:
                real["tiles"][t]["building"] = theirs[:3] + (None,)
        if real["tiles"][t] != ours["tiles"].get(t):
            out.append(f"tile {t}: real {real['tiles'][t]} ours {ours['tiles'].get(t)}")
    for t in set(real["units"]) | set(ours["units"]):
        if real["units"].get(t) != ours["units"].get(t):
            out.append(f"unit on {t}: real {real['units'].get(t)} ours {ours['units'].get(t)}")
    for p in real["players"]:
        a, b = dict(real["players"][p]), dict(ours["players"][p])
        if b["score"] is None:
            a["score"] = None
        seen, mine = a.pop("explored"), b.pop("explored")
        if mine is None:
            seen = mine = []
        seen, mine = set(seen), set(mine)
        if a != b:
            out.append(f"player {p}: real {a} ours {b}")
        if seen != mine:
            out.append(f"player {p} explored: only real {sorted(seen - mine)}, "
                       f"only ours {sorted(mine - seen)}")
    return out[:limit]


# ---- random outcomes ----

# commands whose outcome can be random, and so is read from the real game
RANDOM = ("ExamineRuinsCommand", "CityRewardCommand", "AttackCommand")


def force_outcome(game, command, before, after, n, seat_of):
    """Fixes the random part of the command's outcome in our game to what the
    real game drew: a ruin's reward, the tiles an Explorer revealed, a
    converted unit's home."""
    who = command["PlayerId"]
    player = {p["Id"]: p for p in before["PlayerStates"]}[who]
    then = {p["Id"]: p for p in after["PlayerStates"]}[who]

    def explored(state):
        return {_tile(t["coordinates"], n) for t in state["Map"]["tiles"] if who in t["explorers"]}

    newly = sorted(explored(after) - explored(before))
    kind = command["$type"]
    if kind == "CityRewardCommand" and _normal(command["Reward"]) == "explorer":
        game.force_explorer(newly)
    elif kind == "ExamineRuinsCommand":
        techs = set(then["availableTech"]) - set(player["availableTech"])
        units = lambda state: sum(1 for t in state["Map"]["tiles"]
                                  if t["unit"] and t["unit"]["owner"] == who)
        start = player["startTile"]
        capital = lambda state: state["Map"]["tiles"][start["y"] * n + start["x"]]["improvement"]
        grew = (capital(after)["level"], capital(after)["xp"]) != \
            (capital(before)["level"], capital(before)["xp"])
        # an explorer can also meet a tribe, which pays stars; the other
        # rewards explore nothing (the examining unit doesn't move)
        if techs:
            game.force_ruin(pe.RuinReward.TECH, int(lookup(TECHS, techs.pop())))
        elif units(after) > units(before):
            game.force_ruin(pe.RuinReward.UNIT)
        elif newly:
            # (one that finds a lighthouse grows the capital too)
            game.force_ruin(pe.RuinReward.EXPLORER)
            game.force_explorer(newly)
        elif grew or then["currency"] <= player["currency"]:
            game.force_ruin(pe.RuinReward.POPULATION)
        else:
            game.force_ruin(pe.RuinReward.STARS)
    elif kind == "AttackCommand":
        target = after["Map"]["tiles"][command["Target"]["y"] * n + command["Target"]["x"]]
        unit = target["unit"]
        origin = before["Map"]["tiles"][command["Origin"]["y"] * n + command["Origin"]["x"]]["unit"]
        if origin and "Cloak" in (origin["type"], (origin.get("passengerUnit") or {}).get("type")) \
                or origin and origin["type"] == "Cloak_Boat":
            # an infiltration: its Daggers' tiles, in a random order, and
            # facings, from the real game
            old = {t["unit"]["id"] for t in before["Map"]["tiles"] if t["unit"]}
            game.force_daggers([(_tile(t["coordinates"], n),
                                 DIRECTIONS.index(t["unit"]["direction"])
                                 if t["unit"]["direction"] in DIRECTIONS else 0)
                                for t in after["Map"]["tiles"]
                                if t["unit"] and t["unit"]["owner"] == who and t["unit"]["id"] not in old])
        elif unit and unit["owner"] == who and unit["home"]:
            game.force_converted_home(_tile(unit["home"], n))


# ---- one game on both ----

def _ours(game):
    """Our side, as saved with a difference: the projection compared, each
    city (with its connection), unit homes and our legal list."""
    projected = project_ours(game)
    return {"tiles": {str(t): v for t, v in projected["tiles"].items()},
            "units": {str(t): v for t, v in projected["units"].items()},
            "players": {str(p): v for p, v in projected["players"].items()},
            "cities": [{"tile": c.tile, "owner": c.owner, "level": c.level,
                        "population": c.population, "pending": c.pending_rewards,
                        "connected_for": c.connected_for}
                       for c in game.cities],
            # each unit's tile and home city's tile
            "homes": {str(u.tile): (game.cities[u.home].tile if u.home >= 0 else -1)
                      for u in game.units if u.alive},
            "legal": [game.describe(c) for c in game.legal_commands()]}


def save(args, seed, real, legal, previous, game=None, previous_game=None):
    """The real state and legal list at a difference, the state before the
    last command, and our side then and now, for a closer look (the real
    maps aren't seeded, so a game can't be replayed)."""
    if args.save:
        os.makedirs(args.save, exist_ok=True)
        with open(os.path.join(args.save, f"{seed}.json"), "w") as f:
            json.dump({"state": real.state(), "legal": legal, "previous": previous,
                       "ours": _ours(game) if game is not None else None,
                       "ours_previous": _ours(previous_game) if previous_game is not None else None},
                      f, default=str)


def random_choice(rng, legal):
    """A random command, ending the turn 15% of the time."""
    ends = [i for i, c in enumerate(legal) if c["$type"] == "EndTurnCommand"]
    others = [i for i, c in enumerate(legal) if c["$type"] != "EndTurnCommand"]
    return rng.choice(others) if others and (rng.random() < 0.85 or not ends) else ends[0]


class Difference(Exception):
    """The two games differ: `verdict` says where, `detail` what."""

    def __init__(self, verdict, detail):
        super().__init__(verdict)
        self.verdict, self.detail = verdict, detail


class Lockstep:
    """A real game and ours kept in step, command by command: the real game
    plays each command first, and its random outcomes are fixed on ours; the
    two are compared before each. The real game's map starts ours; seats in
    `ai_seats` are played by the game's own AI, with Crazy's handicap moved
    onto them (the game makes its first seat the person). Used by play() and
    by the terminal game (stocknavalon.play --crazy)."""

    def __init__(self, args, seed, tribes, ai_seats=()):
        self.args, self.seed, self.ai_seats = args, seed, tuple(ai_seats)
        preset = mapstats.PRESETS.get(args.type, args.type)
        self.real = RealGame(args.oracle, args.build, preset, args.size, tribes,
                             keep_handicaps=bool(ai_seats))
        state = self.real.state()
        self.seat_of = {}
        for player in state["PlayerStates"]:
            if player["Id"] != NATURE:
                self.seat_of[player["Id"]] = len(self.seat_of)
        if ai_seats:
            ids = {s: i for i, s in self.seat_of.items()}
            bot = max(p["handicap"] for p in state["PlayerStates"] if p["Id"] in self.seat_of)
            for s, i in ids.items():
                self.real.ask(f"set 0 player {i} handicap {bot if s in ai_seats else 1}")
            state = self.real.state()
        income = [next(p["handicap"] for p in state["PlayerStates"] if p["Id"] == i)
                  for i in sorted(self.seat_of, key=self.seat_of.get)]
        map_type = pe.MapType[args.type.upper()]
        start = mapstats.real_map(state["Map"], tribes)
        self.game = pe.Game(mapstats.map_data(start, map_type),
                            [pe.Tribe[t.upper()] for t in tribes], seed=seed,
                            capital_income=income)
        self.n = self.game.size
        self.last = "the start"
        self.history = []
        self.previous = None
        self.pending_explorer = False

    def close(self):
        self.real.close()

    def legal(self):
        """The real legal list, after ending nature's turns (it has one of
        its own after every player's; ours has none)."""
        legal = self.real.legal()
        while legal and legal[0]["PlayerId"] == NATURE:
            rejected = self.real.apply(next(i for i, c in enumerate(legal)
                                            if c["$type"] == "EndTurnCommand"))
            if rejected:
                raise Difference("the real game rejected nature's end turn", [str(rejected)])
            legal = self.real.legal()
        return legal

    def over(self, game):
        """The game is over on both (the real one lets the winner finish the
        turn, and doesn't wipe the loser out)."""
        return game.game_over and self.real.state()["CurrentState"] in ("FinalTurn", "Ended")

    def check(self, game, legal):
        """Compares the states, then the legal lists; raises Difference."""
        args, real, n, last = self.args, self.real, self.n, self.last
        state = real.state()
        ours = project_ours(game)
        if legal and (legal[0]["$type"] == "PeaceRequestResponseCommand" or
                      (legal[0]["$type"] == "CityRewardCommand" and last.startswith("end turn"))):
            # the real game starts the turn (temple levels) once the offer
            # is answered, or a reward waiting then is picked
            ours["players"][game.current_player]["score"] = None
        if legal and legal[0]["$type"] == "CityRewardCommand":
            # the real game holds back the rest of the command's actions (more
            # of a reveal, say) until the reward is picked
            for side in ours["players"].values():
                side["explored"] = None
                side["score"] = None
            for tile in ours["tiles"].values():
                if tile["building"] and tile["building"][0] == "city":
                    tile["building"] = tile["building"][:2] + (None, None)
        diff = differences(project_real(state, self.seat_of), ours, game.current_player)
        if diff:
            if any("stars" in line for line in diff):
                # what our income is made of, next to the real cities
                diff.append("our income: " + ", ".join(
                    f"city {c.tile} of {c.owner}: {game.city_income(i)}"
                    for i, c in enumerate(game.cities) if c.owner >= 0) +
                    f"; embassies {[game.embassy_income(p) for p in range(len(game.players))]}")
                diff.append("real cities: " + ", ".join(
                    f"{_tile(t['coordinates'], n)}: L{t['improvement']['level']} "
                    f"prod {t['improvement']['production']} xp {t['improvement']['xp']} "
                    f"{t['improvement']['rewards']}"
                    for t in state["Map"]["tiles"]
                    if t["improvement"] and t["improvement"]["type"] == "City" and t["owner"]))
            save(args, self.seed, real, legal, self.previous, game, self.previous_game)
            raise Difference(f"state differs after {last}", self.history[-args.history:] + diff)
        translated = [to_command(c, game, self.seat_of) for c in legal]
        theirs = {command_key(c) for c in translated}
        mine = {command_key(c) for c in game.legal_commands()
                if c.type not in (pe.CommandType.RESIGN, pe.CommandType.STAY)}
        if theirs != mine:
            save(args, self.seed, real, legal, self.previous, game, self.previous_game)
            only_real = [game.describe(c) for c in translated if command_key(c) not in mine]
            only_ours = [game.describe(c) for c in game.legal_commands()
                         if command_key(c) in mine - theirs]
            raise Difference(f"legal lists differ after {last}", self.history[-args.history:] +
                             [f"only real: {only_real[:6]}", f"only ours: {only_ours[:6]}"])

    def ai_move(self, game, legal):
        """The game's AI's next command, as (the real legal index, or "ai"
        for a command the client never offers, such as Stay; the command;
        ours)."""
        move = self.real.ai_move()
        k = move["index"]
        chosen = move["command"]
        if k < 0 and chosen["$type"] != "StayCommand":
            save(self.args, self.seed, self.real, legal, self.previous, game, self.previous_game)
            raise Difference(f"the AI played a command not in the legal list after {self.last}",
                             self.history[-self.args.history:] + [str(chosen)])
        return (k if k >= 0 else "ai"), chosen, to_command(chosen, game, self.seat_of)

    def find(self, game, legal, command):
        """Our command's place in the real legal list; raises Difference if
        it isn't there."""
        for i, c in enumerate(legal):
            if command_key(to_command(c, game, self.seat_of)) == command_key(command):
                return i
        save(self.args, self.seed, self.real, legal, self.previous, game, self.previous_game)
        ours_legal = any(command_key(c) == command_key(command) for c in game.legal_commands())
        raise Difference(f"the move isn't in the real legal list after {self.last}",
                         self.history[-self.args.history:] +
                         [f"{game.describe(command)}; in our legal list: {ours_legal}"])

    def play(self, game, k, chosen, ours, apply_ours=True):
        """Plays the command on the real game, fixes its random outcome on
        ours, and (with apply_ours) plays it on ours."""
        state = self.real.state()
        self.previous_game = game.copy()
        rejected = self.real.apply(k)
        if rejected:
            raise Difference(f"the real game rejected {chosen}", [str(rejected)])
        self.previous = state
        if chosen["$type"] in RANDOM:
            force_outcome(game, chosen, state, self.real.state(), self.n, self.seat_of)
        after = self.real.state() if chosen["$type"] == "CityRewardCommand" else None
        self.last = f"{game.describe(ours)} ({chosen['$type']})"
        self.history.append(f"turn {game.turn} player {game.current_player}: {self.last}; "
                            f"stars {[p.stars for p in game.players]}, "
                            f"scores {[game.score(p) for p in range(len(game.players))]}, "
                            f"income {[game.income(p) for p in range(len(game.players))]}")
        if apply_ours:
            self.apply_ours(game, ours)
        if after is not None:
            # an Explorer (a reward's or a ruin's) that levelled a city up (a
            # lighthouse) walks on once that reward is picked
            self.after_reward = (chosen["PlayerId"], after)
            if apply_ours:
                self.finish_reward(game)
        self.pending_explorer = chosen["$type"] == "CityRewardCommand" and \
            _normal(chosen["Reward"]) == "explorer" or \
            chosen["$type"] == "ExamineRuinsCommand" or \
            (self.pending_explorer and chosen["$type"] == "CityRewardCommand")

    def apply_ours(self, game, ours):
        if ours.type == pe.CommandType.STAY:
            game.apply_unchecked(ours)
        else:
            game.apply(ours)

    pending_explorer = False
    after_reward = None
    previous_game = None

    def finish_reward(self, game):
        """After ours applied a reward: the rest of an Explorer the reward
        held back."""
        if self.after_reward is None:
            return
        who, after = self.after_reward
        self.after_reward = None
        p = self.seat_of[who]
        rest = [_tile(t["coordinates"], self.n) for t in after["Map"]["tiles"]
                if who in t["explorers"] and not game.tiles[_tile(t["coordinates"], self.n)].explored_by(p)]
        if rest and self.pending_explorer:
            game.force_reveal(p, rest)


def play(args, seed, setup=None, choose=random_choice, ai_seats=(), pick=None, tribes=None,
         max_turns=None, result=None):
    """Plays one game on both engines; `setup(real, game, seat_of)` can change
    the start on both first (scenarios.py), and `choose(rng, legal)` picks each
    command's index in the real legal list. Against the game's own AI: the
    seats in `ai_seats` are played by it (AI.GetMove, at Crazy, with Crazy's
    handicap), and the others by `pick(game)`, which returns one of our
    Commands (the bot's move on our game). Stops at the first difference as
    always, or at `max_turns`; `result`, a dict, gets the winning seat (None
    for none yet), the turns and the scores."""
    rng = random.Random(seed)
    tribes = tribes or rng.sample(mapstats.TRIBES, 2)
    step_ = Lockstep(args, seed, tribes, ai_seats)
    game = step_.game
    try:
        if result is not None:
            result.update(tribes=tribes, winner=None, turns=0, scores=None)
        if setup is not None:
            setup(step_.real, game, step_.seat_of)
            step_.last = "the scenario's setup"
        for step in range(args.steps):
            try:
                legal = step_.legal()
                if step_.over(game):
                    if result is not None:
                        result.update(turns=game.turn, winner=game.winner,
                                      scores=[game.score(p) for p in range(len(game.players))])
                    return step, "no differences (game over)", []
                step_.check(game, legal)
                if result is not None:
                    result.update(turns=game.turn,
                                  scores=[game.score(p) for p in range(len(game.players))],
                                  winner=game.winner if game.game_over else None)
                if max_turns is not None and game.turn > max_turns:
                    return step, "no differences (turn limit)", []
                seat = step_.seat_of.get(legal[0]["PlayerId"]) if legal else None
                if seat in ai_seats:
                    k, chosen, ours = step_.ai_move(game, legal)
                else:
                    if pick is not None and seat is not None:
                        k = step_.find(game, legal, pick(game))
                    else:
                        k = choose(rng, legal)
                    chosen, ours = legal[k], to_command(legal[k], game, step_.seat_of)
                step_.play(game, k, chosen, ours)
            except Difference as d:
                return step, d.verdict, d.detail
        return args.steps, "no differences", []
    finally:
        step_.close()


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
