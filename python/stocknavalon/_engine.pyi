"""The stockNavalon Polytopia engine"""

from collections.abc import Sequence
import enum


class TerrainType(enum.IntEnum):
    FIELD = 0

    FOREST = 1

    MOUNTAIN = 2

    WATER = 3

    OCEAN = 4

class ResourceType(enum.IntEnum):
    NONE = 0

    FRUIT = 1

    CROP = 2

    GAME = 3

    METAL = 4

    FISH = 5

    STARFISH = 6

class ImprovementType(enum.IntEnum):
    NONE = 0

    CITY = 1

    VILLAGE = 2

    RUIN = 3

    LIGHTHOUSE = 4

    FARM = 5

    MINE = 6

    LUMBERHUT = 7

    WINDMILL = 8

    SAWMILL = 9

    FORGE = 10

    MARKET = 11

    PORT = 12

    TEMPLE = 13

    FORESTTEMPLE = 14

    MOUNTAINTEMPLE = 15

    WATERTEMPLE = 16

    ALTAROFPEACE = 17

    EMPERORSTOMB = 18

    EYEOFGOD = 19

    GATEOFPOWER = 20

    GRANDBAZAAR = 21

    PARKOFFORTUNE = 22

    TOWEROFWISDOM = 23

    HARVESTFRUIT = 24

    HUNTING = 25

    FISHING = 26

    CLEARFOREST = 27

    BURNFOREST = 28

    GROWFOREST = 29

    ROAD = 30

    BRIDGE = 31

class UnitType(enum.IntEnum):
    WARRIOR = 0

    RIDER = 1

    ARCHER = 2

    DEFENDER = 3

    SWORDSMAN = 4

    MINDBENDER = 5

    CATAPULT = 6

    KNIGHT = 7

    CLOAK = 8

    DAGGER = 9

    GIANT = 10

    RAFT = 11

    SCOUT = 12

    RAMMER = 13

    BOMBER = 14

    JUGGERNAUT = 15

    DINGHY = 16

    PIRATE = 17

class TechType(enum.IntEnum):
    BASIC = 0

    RIDING = 1

    ROADS = 2

    TRADE = 3

    FREESPIRIT = 4

    CHIVALRY = 5

    ORGANIZATION = 6

    FARMING = 7

    CONSTRUCTION = 8

    STRATEGY = 9

    DIPLOMACY = 10

    CLIMBING = 11

    MEDITATION = 12

    PHILOSOPHY = 13

    MINING = 14

    SMITHERY = 15

    FISHING = 16

    RAMMING = 17

    AQUATISM = 18

    SAILING = 19

    NAVIGATION = 20

    HUNTING = 21

    FORESTRY = 22

    MATHEMATICS = 23

    ARCHERY = 24

    SPIRITUALISM = 25

class Tribe(enum.IntEnum):
    XINXI = 0

    IMPERIUS = 1

    BARDUR = 2

    OUMAJI = 3

    KICKOO = 4

    HOODRICK = 5

    LUXIDOOR = 6

    VENGIR = 7

    ZEBASI = 8

    AIMO = 9

    QUETZALI = 10

    YADAKK = 11

class CommandType(enum.IntEnum):
    MOVE = 0

    ATTACK = 1

    RECOVER = 2

    HEALOTHERS = 3

    PROMOTE = 4

    DISBAND = 5

    STAY = 6

    DISEMBARK = 7

    UPGRADE = 8

    BUILD = 9

    DESTROY = 10

    HARVEST = 11

    CAPTURE = 12

    EXAMINERUINS = 13

    TRAIN = 14

    RESEARCH = 15

    CITYREWARD = 16

    PEACETREATY = 17

    PEACEREQUESTRESPONSE = 18

    BREAKPEACE = 19

    ESTABLISHEMBASSY = 20

    DESTROYEMBASSY = 21

    ENDTURN = 22

    RESIGN = 23

class MapType(enum.IntEnum):
    DRYLANDS = 0

    LAKES = 1

    PANGEA = 2

    CONTINENTS = 3

    ARCHIPELAGO = 4

    WATERWORLD = 5

class CityReward(enum.IntEnum):
    WORKSHOP = 0

    EXPLORER = 1

    CITYWALL = 2

    RESOURCES = 3

    POPULATIONGROWTH = 4

    BORDERGROWTH = 5

    PARK = 6

    SUPERUNIT = 7

class Task(enum.IntEnum):
    PACIFIST = 0

    WEALTH = 1

    EXPLORER = 2

    KILLER = 3

    NETWORK = 4

    METROPOLIS = 5

    GENIUS = 6

class WinCondition(enum.IntEnum):
    MIGHT = 0

    DOMINATION = 1

class Command:
    """
    One command, as the real game has them. from_tile and to_tile are tile indices (y * size + x), -1 when unused; arg is a unit type, improvement type, tech, city reward or player, depending on the type. See docs/python.md for each type's fields.
    """

    def __init__(self, type: CommandType, from_tile: int = -1, to_tile: int = -1, arg: int = -1) -> None: ...

    @property
    def type(self) -> CommandType: ...

    @type.setter
    def type(self, arg: CommandType, /) -> None: ...

    @property
    def from_tile(self) -> int: ...

    @from_tile.setter
    def from_tile(self, arg: int, /) -> None: ...

    @property
    def to_tile(self) -> int: ...

    @to_tile.setter
    def to_tile(self, arg: int, /) -> None: ...

    @property
    def arg(self) -> int: ...

    @arg.setter
    def arg(self, arg: int, /) -> None: ...

    def __eq__(self, arg: Command, /) -> bool: ...

    def __hash__(self) -> int: ...

    def __repr__(self) -> str: ...

class Tile:
    """A map tile, as the engine sees it (no fog)."""

    @property
    def terrain(self) -> TerrainType: ...

    @property
    def resource(self) -> ResourceType: ...

    @property
    def building(self) -> ImprovementType: ...

    @property
    def level(self) -> int: ...

    @property
    def road(self) -> bool: ...

    @property
    def unit(self) -> int: ...

    @property
    def territory(self) -> int: ...

    def explored_by(self, player: int) -> bool: ...

class Unit:
    """A unit, as the engine sees it. HP values are x10."""

    @property
    def type(self) -> UnitType: ...

    @property
    def passenger(self) -> UnitType: ...

    @property
    def owner(self) -> int: ...

    @property
    def tile(self) -> int: ...

    @property
    def hp(self) -> int:
        """HP x10"""

    @property
    def max_hp(self) -> int:
        """max HP x10"""

    @property
    def home(self) -> int: ...

    @property
    def kills(self) -> int: ...

    @property
    def veteran(self) -> bool: ...

    @property
    def alive(self) -> bool: ...

    @property
    def hidden(self) -> bool: ...

    @property
    def can_move(self) -> bool: ...

    @property
    def can_attack(self) -> bool: ...

    @property
    def can_act(self) -> bool: ...

class City:
    """A city, as the engine sees it."""

    @property
    def tile(self) -> int: ...

    @property
    def owner(self) -> int: ...

    @property
    def level(self) -> int: ...

    @property
    def population(self) -> int: ...

    @property
    def capital_of(self) -> int: ...

    @property
    def connected_for(self) -> int: ...

    @property
    def pending_rewards(self) -> int: ...

    @property
    def parks(self) -> int: ...

    @property
    def workshop(self) -> bool: ...

    @property
    def wall(self) -> bool: ...

class Player:
    """A player's full state, including what's hidden from opponents."""

    @property
    def tribe(self) -> Tribe: ...

    @property
    def stars(self) -> int: ...

    @property
    def capital(self) -> int: ...

    @property
    def alive(self) -> bool: ...

    @property
    def kills(self) -> int: ...

    @property
    def lighthouses_found(self) -> int: ...

    @property
    def techs(self) -> list[int]: ...

    @property
    def tasks_done(self) -> list[int]: ...

    @property
    def monuments_built(self) -> list[int]: ...

    def has_met(self, player: int) -> bool: ...

class MapData:
    """
    A map before the game starts: tiles and each player's capital tile, in seat order.
    """

    @property
    def size(self) -> int: ...

    @property
    def type(self) -> MapType: ...

    @property
    def tiles(self) -> list[Tile]: ...

    @property
    def capitals(self) -> list[int]: ...

def load_map(path: str) -> MapData:
    """Loads a map file (see map.h)."""

def parse_map(text: str) -> MapData:
    """Parses a map from text in the map file format."""

def ruin_count_for_size(size: int) -> int: ...

def generate_map(tribes: Sequence[Tribe], type: MapType = MapType.LAKES, size: int = 14, seed: int = 0, candidates: int = 10) -> MapData:
    """Generates a map for the tribes, in seat order (GAME.md section 18)."""

class Game:
    """
    The whole game state. Sees everything, including what's hidden by fog: bots should decide from make_view(game, player) and use the Game only to list and apply commands. Copies are independent.
    """

    def __init__(self, map: MapData, tribes: Sequence[Tribe], seed: int = 0, win_condition: WinCondition = WinCondition.MIGHT) -> None: ...

    def copy(self) -> Game:
        """An independent copy, for branching a search."""

    def __copy__(self) -> Game: ...

    def __deepcopy__(self, memo: object) -> Game: ...

    @property
    def size(self) -> int: ...

    @property
    def map_type(self) -> MapType: ...

    @property
    def current_player(self) -> int: ...

    @property
    def turn(self) -> int: ...

    @property
    def winner(self) -> int: ...

    @property
    def game_over(self) -> bool: ...

    @property
    def tiles(self) -> list[Tile]: ...

    @property
    def units(self) -> list[Unit]:
        """The units that are alive."""

    @property
    def cities(self) -> list[City]: ...

    @property
    def players(self) -> list[Player]: ...

    def index(self, x: int, y: int) -> int: ...

    def tile_x(self, tile: int) -> int: ...

    def tile_y(self, tile: int) -> int: ...

    def distance(self, a: int, b: int) -> int: ...

    def tile_owner(self, tile: int) -> int: ...

    def score(self, player: int) -> int: ...

    def income(self, player: int) -> int: ...

    def tech_cost(self, player: int, tech: TechType) -> int: ...

    def legal_commands(self) -> list[Command]:
        """Every legal command for the current player, except Resign."""

    def is_legal(self, command: Command) -> bool: ...

    def apply(self, command: Command) -> None:
        """Applies the command; raises ValueError if it isn't legal."""

    def apply_unchecked(self, command: Command) -> None:
        """Applies a command taken from legal_commands(), without checking."""

    def describe(self, command: Command) -> str:
        """The command in words, e.g. 'move (3,4) -> (4,5)'."""

class PlayerView:
    """
    What one player knows: explored tiles with their live contents, the units they can see, their own stars, techs, tasks and diplomacy. Unexplored tiles have explored=False and nothing else filled in.
    """

    class TileView:
        @property
        def explored(self) -> bool: ...

        @property
        def terrain(self) -> TerrainType: ...

        @property
        def resource(self) -> ResourceType: ...

        @property
        def building(self) -> ImprovementType: ...

        @property
        def level(self) -> int: ...

        @property
        def road(self) -> bool: ...

        @property
        def owner(self) -> int: ...

        @property
        def unit(self) -> int: ...

        @property
        def city(self) -> int: ...

    class UnitView:
        @property
        def type(self) -> UnitType: ...

        @property
        def passenger(self) -> UnitType: ...

        @property
        def owner(self) -> int: ...

        @property
        def tile(self) -> int: ...

        @property
        def hp(self) -> int: ...

        @property
        def max_hp(self) -> int: ...

        @property
        def kills(self) -> int: ...

        @property
        def veteran(self) -> bool: ...

        @property
        def hidden(self) -> bool: ...

        @property
        def can_move(self) -> bool: ...

        @property
        def can_attack(self) -> bool: ...

        @property
        def can_act(self) -> bool: ...

        @property
        def hidden_enemy_near(self) -> bool: ...

    class CityView:
        @property
        def tile(self) -> int: ...

        @property
        def owner(self) -> int: ...

        @property
        def level(self) -> int: ...

        @property
        def population(self) -> int: ...

        @property
        def capital_of(self) -> int: ...

        @property
        def wall(self) -> bool: ...

    class Relation:
        @property
        def peace(self) -> bool: ...

        @property
        def they_asked(self) -> bool: ...

        @property
        def we_asked(self) -> bool: ...

        @property
        def our_embassy(self) -> bool: ...

        @property
        def their_embassy(self) -> bool: ...

    @property
    def player(self) -> int: ...

    @property
    def size(self) -> int: ...

    @property
    def turn(self) -> int: ...

    @property
    def current_player(self) -> int: ...

    @property
    def winner(self) -> int: ...

    @property
    def stars(self) -> int: ...

    @property
    def income(self) -> int: ...

    @property
    def techs(self) -> list[int]: ...

    @property
    def tribes(self) -> list[Tribe]: ...

    @property
    def alive(self) -> list[bool]: ...

    @property
    def met(self) -> list[bool]: ...

    @property
    def scores(self) -> list[int]: ...

    @property
    def tasks_done(self) -> list[int]: ...

    @property
    def monuments_built(self) -> list[int]: ...

    @property
    def kills(self) -> int: ...

    @property
    def peaceful_turns(self) -> int: ...

    @property
    def lighthouses_found(self) -> int: ...

    @property
    def relations(self) -> list[PlayerView.Relation]: ...

    @property
    def tiles(self) -> list[PlayerView.TileView]: ...

    @property
    def units(self) -> list[PlayerView.UnitView]: ...

    @property
    def cities(self) -> list[PlayerView.CityView]: ...

def make_view(game: Game, player: int, everything: bool = False) -> PlayerView:
    """What the player knows about the game. everything=True ignores fog."""

class LegalMoves:
    """
    The current player's legal commands, updated one command at a time instead of regenerated. For search: keep one next to each Game, and apply commands through it. Always matches game.legal_commands() as a set, though not in the same order.
    """

    def __init__(self, game: Game) -> None: ...

    def copy(self) -> LegalMoves: ...

    def __copy__(self) -> LegalMoves: ...

    @property
    def commands(self) -> list[Command]: ...

    def apply(self, game: Game, command: Command) -> None:
        """Applies the command to the game and updates the list."""

    def update(self, before: Game, after: Game) -> None:
        """Updates the list for a game that went from before to after."""

class RandomBot:
    """
    Plays random legal commands: a random command type, then a random command of that type.
    """

    def __init__(self, seed: int = 0) -> None: ...

    def choose(self, game: Game) -> Command: ...

    def play_turn(self, game: Game, max_commands: int = 200) -> list[Command]: ...

def play_random_games(games: int, tribes: Sequence[Tribe], type: MapType = MapType.LAKES, size: int = 14, seed: int = 0, max_turns: int = 100) -> list[tuple[int, int]]:
    """
    Plays random games on generated maps. Returns (winner, turns) for each; winner is -1 if the turn limit came first.
    """
