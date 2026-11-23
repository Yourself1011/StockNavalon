"""polyengine, a Polytopia engine"""

from collections.abc import Callable, Sequence
import enum
from typing import Annotated, Any

import numpy
import numpy.typing
from numpy.typing import NDArray


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

    AQUACROP = 7

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

class RuinReward(enum.IntEnum):
    TECH = 0

    STARS = 1

    POPULATION = 2

    EXPLORER = 3

    UNIT = 4

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

    def __eq__(self, other: object) -> bool: ...

    def __hash__(self) -> int: ...

    def __repr__(self) -> str: ...

class ScoreRecord:
    """
    A score as other players saw it: published when its player ended a turn (or the starting score).
    """

    @property
    def player(self) -> int: ...

    @property
    def turn(self) -> int:
        """the turn that had just ended"""

    @property
    def score(self) -> int: ...

    def __eq__(self, other: object) -> bool: ...

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
    def road_builder(self) -> int:
        """The player who built the road, -1 for one the map started with."""

    @property
    def unit(self) -> int: ...

    @property
    def territory(self) -> int: ...

    @property
    def climate(self) -> int:
        """The tribe whose climate region the tile is in (as an int), -1 if none"""

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

    @property
    def facing(self) -> int:
        """
        The way it faces, as the game numbers directions: SW, W, NW, N, NE, E, SE, S (pushes go that way).
        """

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

    def has_embassy_with(self, player: int) -> bool:
        """Whether the player has an embassy in that player's capital."""

    def cooling_down_with(self, other: int) -> bool:
        """
        Whether peace with them was broken and the player can't attack them yet.
        """

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

    @property
    def regions(self) -> list[int]:
        """
        The seat whose climate region each tile is in; empty if the map doesn't say
        """

def load_map(path: str) -> MapData:
    """Loads a map file (see map.h)."""

def parse_map(text: str) -> MapData:
    """Parses a map from text in the map file format."""

def ruin_count_for_size(size: int) -> int: ...

TERRAIN_CHARS: str = 'FTMWO'

RESOURCE_CHARS: str = ' fcgmhsa'

def start_inequality(map: MapData) -> float:
    """
    How unequal the players' starts are, as the real game scores it (GAME.md section 18): the best territory's score over the worst's.
    """

def generate_map(tribes: Sequence[Tribe], type: MapType = MapType.LAKES, size: int = 14, seed: int = 0, candidates: int = 50) -> MapData:
    """
    Generates a map for the tribes, in seat order (GAME.md section 18): up to `candidates` of them, keeping the fairest.
    """

class Game:
    """
    The whole game state. Sees everything, including what's hidden by fog: bots should decide from make_view(game, player) and use the Game only to list and apply commands. Copies are independent.
    """

    def __init__(self, map: MapData, tribes: Sequence[Tribe], seed: int = 0, win_condition: WinCondition = WinCondition.MIGHT, capital_income: Sequence[int] = []) -> None:
        """
        capital_income: each seat's capital income bonus (the game's handicap: 1 for people, 4 for Crazy bots); empty for 1 each.
        """

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

    def score(self, player: int) -> int:
        """The live score, which only its own player sees."""

    def published_score(self, player: int) -> int:
        """The score other players see: from the end of the player's last turn."""

    @property
    def score_history(self) -> list[ScoreRecord]:
        """Every published score so far, oldest first."""

    def income(self, player: int) -> int: ...

    def city_income(self, city: int) -> int:
        """What the city (an index into cities) earns a turn."""

    def at_peace(self, a: int, b: int) -> bool: ...

    def embassy_income(self, player: int) -> int: ...

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

    def force_ruin(self, reward: RuinReward, tech: int = -1) -> None:
        """
        The next ruin gives this reward (and, for a tech, this tech) instead of a random one.
        """

    def force_explorer(self, tiles: Sequence[int]) -> None:
        """The next Explorer reveals exactly these tiles instead of walking."""

    def force_reveal(self, player: int, tiles: Sequence[int]) -> None:
        """
        Explores these tiles for the player, as the rest of an outcome fixed earlier (an Explorer the game finished later), and checks tasks again.
        """

    def set_stars(self, player: int, stars: int) -> None: ...

    def give_tech(self, player: int, tech: TechType) -> None: ...

    def spawn_unit(self, player: int, type: UnitType, tile: int) -> int:
        """
        A unit of no city, as if trained there: it can't act until a later turn.
        """

    def set_unit(self, unit: int, passenger: UnitType, hp: int) -> None:
        """Sets what a boat carries and the unit's HP (x10)."""

    def found_city(self, tile: int, owner: int) -> int:
        """Makes the village there the player's city, as a capture does."""

    def set_unit_ready(self, unit: int) -> None: ...

    def set_met(self, player: int, other: int, met: bool) -> None: ...

    def place_building(self, player: int, tile: int, type: ImprovementType) -> None:
        """Builds as the player would, without paying."""

    def add_population(self, city_tile: int, amount: int) -> None: ...

    def set_unit_hp(self, tile: int, hp: int) -> None: ...

    def set_tile(self, tile: int, terrain: TerrainType, resource: ResourceType) -> None: ...

    def add_embassy(self, builder: int, host: int) -> None: ...

    def set_city_level(self, city_tile: int, level: int) -> None: ...

    def make_peace(self, a: int, b: int) -> None: ...

    def force_converted_home(self, tile: int) -> None:
        """
        The next converted unit belongs to the city on this tile instead of a random one.
        """

    def force_daggers(self, daggers: Sequence[tuple[int, int]]) -> None:
        """
        The next infiltration's Daggers, as (tile, facing) each, instead of their random order.
        """

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
        def level(self) -> int:
            """Windmill, Sawmill or Forge level, or a temple's level"""

        @property
        def turns_to_level_up(self) -> int:
            """Temples: turns until the next level, 0 at the top level"""

        @property
        def climate(self) -> Tribe | None:
            """The tribe whose climate the tile is in"""

        @property
        def road(self) -> bool: ...

        @property
        def own_road(self) -> bool:
            """A road this player built themselves."""

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

        @property
        def parks(self) -> int: ...

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
    def tribes(self) -> list[Tribe | None]:
        """
        Each player's tribe: your own and those you've met; None until then. Starting scores (the first records in score_history) narrow an unmet tribe down.
        """

    @property
    def alive(self) -> list[bool]: ...

    @property
    def met(self) -> list[bool]: ...

    @property
    def scores(self) -> list[int]:
        """
        Every player's score, met or not, as this player sees it: their own live, everyone else's from the end of their last turn.
        """

    @property
    def score_history(self) -> list[ScoreRecord]:
        """Every published score so far (public), oldest first."""

    @property
    def effects_since_published(self) -> list[int]:
        """
        Per player: what this player's own actions visibly did to their score since they last published (negative for losses).
        """

    @property
    def effects_in_last_change(self) -> list[int]:
        """Per player: the same, within the change they last published."""

    @property
    def effects_in_changes(self) -> list[int]:
        """
        Per score_history record: what this player's own actions visibly did to the publisher's score within that change.
        """

    @property
    def converted_away(self) -> list[tuple[UnitType, UnitType]]:
        """This player's units taken by conversion: (type, passenger)."""

    @property
    def attacked_on_turns(self) -> list[int]:
        """The turns this player's units or cities were attacked."""

    @property
    def first_occupied(self) -> list[int]:
        """
        Per tile: the first turn one of this player's units or cities stood on it, -1 for never.
        """

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

class Replies(enum.Enum):
    """
    Turn search: how the opponent's replies are drawn (search.h, SearchConfig::replies).
    """

    SAMPLED = 0

    LIKELY_FIRST = 1

    DISCREPANCY = 2

class Backup(enum.Enum):
    """
    How a node's value comes from its children's (search.h, SearchConfig::backup).
    """

    MEAN = 0

    PBAR = 1

    PBAR_OPPONENT = 2

class SearchConfig:
    """
    A search's settings (search.h, SearchConfig), defaults as there. polyengine.search_config(**fields) makes one.
    """

    def __init__(self) -> None: ...

    def copy(self) -> SearchConfig: ...

    def __copy__(self) -> SearchConfig: ...

    @property
    def simulations(self) -> int:
        """per determinized game (network evaluations with count_evaluations)"""

    @simulations.setter
    def simulations(self, arg: int, /) -> None: ...

    @property
    def samples(self) -> int:
        """determinized games per decision"""

    @samples.setter
    def samples(self, arg: int, /) -> None: ...

    @property
    def exploration(self) -> float: ...

    @exploration.setter
    def exploration(self, arg: float, /) -> None: ...

    @property
    def fpu_reduction(self) -> float: ...

    @fpu_reduction.setter
    def fpu_reduction(self, arg: float, /) -> None: ...

    @property
    def first_play_urgency(self) -> bool: ...

    @first_play_urgency.setter
    def first_play_urgency(self, arg: bool, /) -> None: ...

    @property
    def horizon(self) -> int:
        """turn changes a line may cross (UNLIMITED_HORIZON: no cutoff)"""

    @horizon.setter
    def horizon(self, arg: int, /) -> None: ...

    @property
    def root_noise(self) -> bool: ...

    @root_noise.setter
    def root_noise(self, arg: bool, /) -> None: ...

    @property
    def noise_alpha(self) -> float: ...

    @noise_alpha.setter
    def noise_alpha(self, arg: float, /) -> None: ...

    @property
    def noise_weight(self) -> float: ...

    @noise_weight.setter
    def noise_weight(self, arg: float, /) -> None: ...

    @property
    def determinize_tries(self) -> int: ...

    @determinize_tries.setter
    def determinize_tries(self, arg: int, /) -> None: ...

    @property
    def full_information(self) -> bool: ...

    @full_information.setter
    def full_information(self, arg: bool, /) -> None: ...

    @property
    def gumbel(self) -> bool: ...

    @gumbel.setter
    def gumbel(self, arg: bool, /) -> None: ...

    @property
    def gumbel_candidates(self) -> int: ...

    @gumbel_candidates.setter
    def gumbel_candidates(self, arg: int, /) -> None: ...

    @property
    def gumbel_visit_scale(self) -> float: ...

    @gumbel_visit_scale.setter
    def gumbel_visit_scale(self, arg: float, /) -> None: ...

    @property
    def gumbel_value_scale(self) -> float: ...

    @gumbel_value_scale.setter
    def gumbel_value_scale(self, arg: float, /) -> None: ...

    @property
    def explore_candidates(self) -> int: ...

    @explore_candidates.setter
    def explore_candidates(self, arg: int, /) -> None: ...

    @property
    def score_utility(self) -> float: ...

    @score_utility.setter
    def score_utility(self, arg: float, /) -> None: ...

    @property
    def win_sooner(self) -> float: ...

    @win_sooner.setter
    def win_sooner(self, arg: float, /) -> None: ...

    @property
    def turn_search(self) -> bool: ...

    @turn_search.setter
    def turn_search(self, arg: bool, /) -> None: ...

    @property
    def command_visits(self) -> int: ...

    @command_visits.setter
    def command_visits(self, arg: int, /) -> None: ...

    @property
    def turn_widening(self) -> float: ...

    @turn_widening.setter
    def turn_widening(self, arg: float, /) -> None: ...

    @property
    def turn_widening_power(self) -> float: ...

    @turn_widening_power.setter
    def turn_widening_power(self, arg: float, /) -> None: ...

    @property
    def turn_temperature(self) -> float: ...

    @turn_temperature.setter
    def turn_temperature(self, arg: float, /) -> None: ...

    @property
    def max_turn_commands(self) -> int: ...

    @max_turn_commands.setter
    def max_turn_commands(self, arg: int, /) -> None: ...

    @property
    def count_evaluations(self) -> bool: ...

    @count_evaluations.setter
    def count_evaluations(self, arg: bool, /) -> None: ...

    @property
    def replies(self) -> Replies:
        """turn search: how the opponent's replies are drawn (Replies)"""

    @replies.setter
    def replies(self, arg: Replies, /) -> None: ...

    @property
    def backup(self) -> Backup:
        """how a node's value comes from its children's (Backup)"""

    @backup.setter
    def backup(self, arg: Backup, /) -> None: ...

def regularized_policy(prior: Sequence[float], q: Sequence[float], lambda_: float) -> list[float]:
    """The policy PUCT heads toward (search.h, regularizedPolicy)."""

class SelfPlayConfig:
    """
    Self-play's or a match's settings (selfPlay.h, SelfPlayConfig), defaults as there. polyengine.self_play_config(**fields) makes one.
    """

    def __init__(self) -> None: ...

    def copy(self) -> SelfPlayConfig: ...

    def __copy__(self) -> SelfPlayConfig: ...

    @property
    def search(self) -> SearchConfig: ...

    @search.setter
    def search(self, arg: SearchConfig, /) -> None: ...

    @property
    def side_search(self) -> list[SearchConfig | None]:
        """per side, a whole search instead of `search` (None: `search`)"""

    @side_search.setter
    def side_search(self, arg: Sequence[SearchConfig | None], /) -> None: ...

    @property
    def map_size(self) -> int: ...

    @map_size.setter
    def map_size(self, arg: int, /) -> None: ...

    @property
    def max_turns(self) -> int: ...

    @max_turns.setter
    def max_turns(self, arg: int, /) -> None: ...

    @property
    def stall_turns(self) -> int: ...

    @stall_turns.setter
    def stall_turns(self, arg: int, /) -> None: ...

    @property
    def fog_share(self) -> float: ...

    @fog_share.setter
    def fog_share(self, arg: float, /) -> None: ...

    @property
    def peace_treaties(self) -> bool: ...

    @peace_treaties.setter
    def peace_treaties(self, arg: bool, /) -> None: ...

    @property
    def tree_reuse(self) -> bool: ...

    @tree_reuse.setter
    def tree_reuse(self, arg: bool, /) -> None: ...

    @property
    def network_beliefs(self) -> bool: ...

    @network_beliefs.setter
    def network_beliefs(self, arg: bool, /) -> None: ...

    @property
    def temperature_turns(self) -> int: ...

    @temperature_turns.setter
    def temperature_turns(self, arg: int, /) -> None: ...

    @property
    def full_share(self) -> float: ...

    @full_share.setter
    def full_share(self, arg: float, /) -> None: ...

    @property
    def quick_simulations(self) -> int: ...

    @quick_simulations.setter
    def quick_simulations(self, arg: int, /) -> None: ...

    @property
    def threads(self) -> int: ...

    @threads.setter
    def threads(self, arg: int, /) -> None: ...

    @property
    def sides(self) -> list[str]:
        """who plays each side: network, score or random"""

    @sides.setter
    def sides(self, arg: Sequence[str], /) -> None: ...

    @property
    def side_plans(self) -> list[str]:
        """each side's forced plan (plans.h): none, bridge, naval or convert"""

    @side_plans.setter
    def side_plans(self, arg: Sequence[str], /) -> None: ...

    @property
    def plan_turns(self) -> int:
        """turns a forced plan lasts, from its start"""

    @plan_turns.setter
    def plan_turns(self, arg: int, /) -> None: ...

    @property
    def plan_when_ahead(self) -> float:
        """
        NaN: plans from the start; else from a side's first decision with its own search value at least this
        """

    @plan_when_ahead.setter
    def plan_when_ahead(self, arg: float, /) -> None: ...

    @property
    def map_types(self) -> list[MapType]:
        """the map types games are drawn from, evenly; empty for every type"""

    @map_types.setter
    def map_types(self, arg: Sequence[MapType], /) -> None: ...

    @property
    def paired(self) -> bool: ...

    @paired.setter
    def paired(self, arg: bool, /) -> None: ...

    @property
    def random_seats(self) -> bool: ...

    @random_seats.setter
    def random_seats(self, arg: bool, /) -> None: ...

    @property
    def train_sides(self) -> list[bool]: ...

    @train_sides.setter
    def train_sides(self, arg: Sequence[bool], /) -> None: ...

    @property
    def handicap_share(self) -> float: ...

    @handicap_share.setter
    def handicap_share(self, arg: float, /) -> None: ...

    @property
    def handicap_income_share(self) -> float: ...

    @handicap_income_share.setter
    def handicap_income_share(self, arg: float, /) -> None: ...

    @property
    def handicap_income(self) -> list[int]: ...

    @handicap_income.setter
    def handicap_income(self, arg: Sequence[int], /) -> None: ...

    @property
    def handicap_stars(self) -> list[int]: ...

    @handicap_stars.setter
    def handicap_stars(self, arg: Sequence[int], /) -> None: ...

    @property
    def handicap_catch_up_share(self) -> float: ...

    @handicap_catch_up_share.setter
    def handicap_catch_up_share(self, arg: float, /) -> None: ...

    @property
    def catch_up_from_turn(self) -> int: ...

    @catch_up_from_turn.setter
    def catch_up_from_turn(self, arg: int, /) -> None: ...

    @property
    def catch_up_below(self) -> float: ...

    @catch_up_below.setter
    def catch_up_below(self, arg: float, /) -> None: ...

    @property
    def trailing_search_factor(self) -> int: ...

    @trailing_search_factor.setter
    def trailing_search_factor(self, arg: int, /) -> None: ...

    @property
    def explore_candidates(self) -> int: ...

    @explore_candidates.setter
    def explore_candidates(self, arg: int, /) -> None: ...

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

class GameRecord:
    """
    A recorded game: its setup and commands, replayed to rebuild any position (docs/stockNavalon.md section 5).
    """

    @property
    def map_size(self) -> int: ...

    @property
    def map_type(self) -> MapType: ...

    @property
    def tribes(self) -> list[Tribe]: ...

    @property
    def game_seed(self) -> int:
        """The game's random seed: identifies it among the others."""

    @property
    def rules_version(self) -> int:
        """
        The rules the game was played by (GameSettings::rulesVersion; RULES_VERSION is the latest): older records replay by theirs.
        """

    @rules_version.setter
    def rules_version(self, arg: int, /) -> None: ...

    @property
    def head_start(self) -> list[int]:
        """
        Each player's head start in stars, in a handicap game; empty for an ordinary one.
        """

    @property
    def capital_income(self) -> list[int]:
        """
        Each player's capital income bonus, in a handicap game; empty for the usual.
        """

    @property
    def income_bonuses(self) -> list[tuple[int, int, int]]:
        """A catch-up game's boosts: (player, from turn, extra income)."""

    @property
    def pool_opponent_seat(self) -> int:
        """
        In an opponent-pool game, the seat the earlier network played; -1 otherwise.
        """

    @property
    def catch_up(self) -> bool:
        """
        A catch-up game, whether or not its boost was given: its value targets count only from the boost on.
        """

    @property
    def handicap_leader(self) -> int:
        """In a handicap game, the player given the edge; -1 otherwise."""

    def replays_legally(self) -> bool:
        """
        Whether every command is legal when replayed, and no player's stars go below zero.
        """

    @property
    def winner(self) -> int:
        """-1 if the turn limit came first"""

    @property
    def turns(self) -> int: ...

    @property
    def final_scores(self) -> list[int]: ...

    @property
    def seat_zero_side(self) -> int:
        """In a match, the side that played seat 0."""

    @property
    def move_count(self) -> int: ...

    @property
    def searched(self) -> list[int]:
        """The moves the search chose, which have a policy target"""

    def start(self) -> Game:
        """The game before any command."""

    @property
    def commands(self) -> list[Command]:
        """Every command played, in order, from start()."""

    def result(self, player: int) -> float:
        """+1 or -1 for a win or loss, 0 for a game cut off by the turn limit."""

    @property
    def final_territory(self) -> list[int]:
        """
        Each tile's owner when the game ended (-1 for nobody); empty in older records.
        """

    @property
    def turn_scores(self) -> list[int]:
        """
        Each player's latest published score at each turn, flattened turns x players; empty in older records.
        """

    @property
    def peace_treaties(self) -> bool:
        """Whether peace treaties were allowed in the game."""

    @property
    def final_cities(self) -> list[int]:
        """Each player's cities when the game ended; empty in older records."""

    def city_value(self, player: int) -> float:
        """The final city lead, (mine - the best opponent's) / their sum."""

    @property
    def fog(self) -> bool:
        """
        Played with fog; without it, its positions are encoded with full information.
        """

    @property
    def stalled(self) -> bool:
        """
        Ended by the stalemate cutoff (stall_turns) rather than the turn limit; not saved with the record.
        """

    @property
    def root_values(self) -> list[float]:
        """Each move's search value for its player (NaN without one)."""

    @property
    def final_values(self) -> list[float]:
        """
        For a game cut off by the turn limit, each seat's last search value with the network (NaN if none); empty otherwise.
        """

    def value(self, player: int) -> float:
        """
        The value target: the result, or for a game cut off by the turn limit the player's final value (0 without one).
        """

    def score_value(self, player: int, scale: float = 500.0) -> float:
        """The final score lead, squashed into [-1, 1]."""

def play_games(config: SelfPlayConfig, count: int, seed: int = 0, threads: int = 1) -> list[GameRecord]:
    """
    Self-play games with these settings, every side scored by the score lead (no network): the bootstrap before there's one.
    """

class SelfPlaySession:
    """
    Many self-play games at once, their waiting positions evaluated together by the caller's network:

        while not session.done():
            planes, globals_, legal = session.pending_inputs(enc)
            logits, values = model(planes, globals_)
            session.submit_network(enc, logits, values)
        records = session.take_finished()
    """

    def __init__(self, config: SelfPlayConfig, parallel: int, games: int, seed: int = 0, first: int = 0) -> None:
        """
        `games` games with these settings, `parallel` at a time, numbered from `first`: with the same seed, sessions of games [0, n) and [n, m) play the same games as one of [0, m) (n even for paired games).
        """

    def done(self) -> bool: ...

    def clear_cache(self) -> None:
        """Forget the cached evaluations: after the network changes."""

    def cache_stats(self) -> tuple[int, int]:
        """(lookups, hits) of the evaluation cache since the last call."""

    def depth_stats(self) -> list:
        """
        Per side: simulations so far, their mean depth in moves below the root (a sampled turn is one), the share that reached the opponent's turn, their mean turn changes (turn search reaches further), and the network evaluations asked for.
        """

    @property
    def finished_games(self) -> int: ...

    def pending_inputs(self, encoding: Encoding, legal: bool = True) -> tuple[NDArray[numpy.float32], NDArray[numpy.float32], NDArray[numpy.uint8]]:
        """
        Advances every game to its next waiting positions and encodes them: (planes [B, C, N, N], globals [B, G], legal [B, A]). With legal=False the masks are skipped (empty): pending_slots gives what the search needs.
        """

    def submit_network(self, encoding: Encoding, logits: Annotated[NDArray[numpy.float32], dict(order='C', device='cpu', writable=False)], values: Annotated[NDArray[numpy.float32], dict(order='C', device='cpu', writable=False)]) -> None:
        """
        The network's policy logits [B, A] and values [B] for the pending positions.
        """

    def pending_sides(self) -> NDArray[numpy.int64]:
        """
        For each pending position, the side (0 or 1) whose network evaluates it: in a match, only network sides' positions are pending. Call after pending_inputs.
        """

    def pending_slots(self, encoding: Encoding) -> tuple[NDArray[numpy.int64], NDArray[numpy.int64]]:
        """
        For the pending positions' moves, flattened: (rows, slots) to gather from the policy logits, logits[rows, slots], so only those leave the GPU. Call after pending_inputs.
        """

    def pending_belief_rows(self) -> NDArray[numpy.int64]:
        """
        The pending positions that want the network's beliefs (network_beliefs): give their raw belief outputs to submit_move_logits. Call after pending_inputs.
        """

    def submit_move_logits(self, encoding: Encoding, move_logits: Annotated[NDArray[numpy.float32], dict(order='C', device='cpu', writable=False)], values: Annotated[NDArray[numpy.float32], dict(order='C', device='cpu', writable=False)], beliefs: Annotated[NDArray[numpy.float32], dict(order='C', device='cpu', writable=False)] | None = None, tie_breaks: Annotated[NDArray[numpy.float32], dict(order='C', device='cpu', writable=False)] | None = None) -> None:
        """
        The logits gathered at pending_slots, the values [B], and for the pending_belief_rows their raw belief outputs [R, belief_output_count] (without them, those take priorBeliefs). tie_breaks [B, 2]: each position's predicted final score lead and turns left, for the search's tie-breaks (score_utility, win_sooner).
        """

    def submit_scores(self) -> None:
        """Evaluates the pending positions with the score evaluator instead."""

    def finish_now(self) -> None:
        """
        Ends every game still being played, scored by the lead now, and starts no more. Call after submitting, before the next pending.
        """

    def set_fog_share(self, share: float) -> None:
        """The share of games started from now on that have fog."""

    def finish_after(self, moves: int) -> None:
        """
        Gives every game still being played this many more commands, then ends it scored by the lead; starts no more games.
        """

    def take_finished(self) -> list[GameRecord]:
        """The games finished since the last call."""

class ReplayBuffer:
    """
    The recent games training draws from, held in C++: random positions across all of them, the oldest games dropped past max_positions.
    """

    def __init__(self, max_positions: int) -> None: ...

    def add(self, games: Sequence[GameRecord]) -> None: ...

    @property
    def game_count(self) -> int: ...

    @property
    def positions(self) -> int:
        """Positions with a policy target (moves the search chose)."""

    @property
    def added(self) -> int:
        """Positions ever added, dropped ones included."""

    @property
    def decided_share(self) -> float:
        """The share of games decided before the turn limit."""

    def sample(self, count: int, seed: int, per_game: int = 1) -> list[tuple[int, int]]:
        """
        Random (game, move) positions with a policy target, in groups of per_game from the same game (replayed once for the group).
        """

    def encode(self, encoding: Encoding, positions: Sequence[tuple[int, int]], threads: int = 8, city_weight: float = 0.0, cut_off_weight: float = 1.0, search_weight: float = 0.0, value_lambda: float = 1.0) -> dict[str, numpy.typing.NDArray[Any]]:
        """
        Training examples at those positions, like Encoding.encode_examples. The positions only hold until the next add(): from another thread than the one adding, use sample_examples.
        """

    def sample_examples(self, encoding: Encoding, count: int, seed: int, per_game: int = 4, threads: int = 8, augment: bool = False, city_weight: float = 0.0, cut_off_weight: float = 1.0, search_weight: float = 0.0, positions: bool = False, value_lambda: float = 1.0) -> dict[str, numpy.typing.NDArray[Any]]:
        """
        Random training examples, sampled and encoded at once, so safe while another thread adds games. With augment, each is seen through a random one of the map's 8 symmetries (rotations and reflections). city_weight blends the value target with the final city lead (early in a run); cut_off_weight is the value weight of positions from games cut off by the turn limit or a stalemate. With positions, the dict also has game_seeds and moves [B]: which positions were drawn, to encode again later (Encoding.encode_examples).
        """

    def game_seeds(self, without_estimates: bool = False) -> list[int]:
        """
        The seeds of the games held: all, or with without_estimates only those not reanalysed yet (set_value_estimates).
        """

    def turn_start_inputs(self, encoding: Encoding, seeds: Sequence[int], threads: int = 8) -> tuple[NDArray[numpy.float32], NDArray[numpy.float32], NDArray[numpy.uint64], NDArray[numpy.int32], NDArray[numpy.int32]]:
        """
        Reanalysis: the start of each player turn of those games still held, encoded for the player to move: (planes [B, C, N, N], globals [B, G], game seeds, turns, players [B]). Safe while another thread adds games.
        """

    def set_value_estimates(self, seeds: Sequence[int], turns: Sequence[int], players: Sequence[int], values: Sequence[float]) -> None:
        """
        Reanalysis: the network's values of those turn starts (turn_start_inputs), for the lambda-return value target (value_lambda). Games no longer held are skipped.
        """

    def save(self, path: str) -> None:
        """Writes exactly the buffer's games to a record file, replacing it."""

def save_records(records: Sequence[GameRecord], path: str, append: bool = True) -> None:
    """Appends records to a text file, or with append=False replaces it."""

def load_records(path: str) -> list[GameRecord]: ...

def records_to_text(records: Sequence[GameRecord]) -> str:
    """The records as save_records writes them, as a string."""

def records_from_text(text: str) -> list[GameRecord]:
    """Records from records_to_text's string."""

def thin_policy_targets(records: Sequence[GameRecord], share: float, seed: int = 0) -> list[GameRecord]:
    """
    Copies of the records keeping the search's visits (the policy target, and what makes a move a training position) on a random share of their moves: a match's games at self-play's density.
    """

def run_cli(args: Sequence[str], encoding: Encoding, evaluate: Callable, search: SearchConfig, network_beliefs: bool = True, explain: bool = False, seat_search: Sequence[SearchConfig | None] = [], seat_plans: Sequence[str] = [], plan_turns: int = 20, plan_when_ahead: float = float('nan'), start: object | None = None, before_command: object | None = None, outside_seats: Sequence[int] = [], outside_turn: object | None = None) -> int:
    """
    The engine's terminal game (README "Run"), with the seats given by --bot in `args` played by the search, its positions evaluated by evaluate(planes, globals, rows, slots, belief_rows) -> (move logits, values, raw belief outputs of belief_rows or None), as with SelfPlaySession.submit_move_logits. With network_beliefs, each bot turn samples the hidden state from the network's beliefs (asked once per turn), else from the fixed priors. seat_search gives seats their own search (None, or seats past it: `search`); seat_plans their forced plans (plans.h: none, bridge, naval or convert) for plan_turns turns, from the start or (plan_when_ahead) once the seat's evaluation reaches it. With explain, each bot turn also lists its decisions: the command played and the search's leading moves, with priors, visits and values. For a game played alongside another (stocknavalon.play --crazy): start() returns the starting Game; before_command(game, command) is called before every command a person or a bot plays; outside_seats are played by outside_turn(game), which plays the turn on the game itself and returns its commands. Returns the exit code.
    """

def search(game: Game, encoding: Encoding, evaluate: Callable, search: SearchConfig, network_beliefs: bool = True, seed: int = 0) -> tuple[list[Command], list[int], list[float], list[float], int, list[list[tuple[int, float]]]]:
    """
    One search for the player to move, as the CLI's bot does, `evaluate` as for run_cli, with these settings (SearchConfig). Returns (moves, visits, values, priors, chosen index, leaves): leaves per move are (count, mean value for the searcher) of where its simulations ended: in the searcher's own turn, an opponent's, the searcher's later turns, the game's end.
    """

def profile_start() -> None:
    """Starts timing self-play's phases (profile.h), from zero."""

def profile_stop() -> dict:
    """
    Stops timing; per phase, the thread time spent (summed over threads) and how many times it ran.
    """

def play_random_games(games: int, tribes: Sequence[Tribe], type: MapType = MapType.LAKES, size: int = 14, seed: int = 0, max_turns: int = 100) -> list[tuple[int, int]]:
    """
    Plays random games on generated maps. Returns (winner, turns) for each; winner is -1 if the turn limit came first.
    """

ENCODING_VERSION: int = 16

RULES_VERSION: int = 70

MAP_TYPE_COUNT: int = 6

OPENING_CHANGES: int = 6

SLOTS_PER_TILE: int = 218

STAR_COMPONENTS: int = 3

UNEXPLORED_TERRAIN: int = 0

UNEXPLORED_RESOURCE: int = 5

UNEXPLORED_FEATURE: int = 13

UNEXPLORED_LAYERS: int = 16

SIDE_SCORE_SOON: int = 0

SIDE_SCORE_END: int = 1

SIDE_STARS_AHEAD: int = 2

SIDE_INCOME_AHEAD: int = 3

SIDE_SHORT_TERM_VALUE: int = 4

SHORT_TERM_HORIZONS: list = [3.0, 10.0, 30.0]

UNLIMITED_HORIZON: int = 1000

SIDE_TURNS_LEFT: int = 7

TURNS_LEFT_SCALE: float = 50.0

TURNS_LEFT_CAP: float = 100.0

SIDE_TERRITORY: int = 8

TERRITORY_CLASSES: int = 3

HIDDEN_UNIT_COUNT_CAP: int = 20

class Encoding:
    """
    States and commands as arrays for the network (docs/encoding.md). Arrays are NumPy: planes float32 [C, N, N], globals float32 [G], masks uint8 [A]. The batched forms run in C++ without the GIL.
    """

    def __init__(self, map_size: int = 11, players: int = 2) -> None: ...

    @property
    def map_size(self) -> int: ...

    @property
    def players(self) -> int: ...

    @property
    def plane_count(self) -> int:
        """C"""

    @property
    def global_count(self) -> int:
        """G"""

    @property
    def action_count(self) -> int:
        """A"""

    @property
    def belief_output_heads(self) -> list[tuple[str, int, int, int]]:
        """
        Where each belief head's raw outputs go in the array submit_move_logits takes: (name, opponent, offset, size), like target_heads but the stars as the mixture's 3 x STAR_COMPONENTS raw parameters (docs/encoding.md section 8.4).
        """

    @property
    def belief_output_count(self) -> int: ...

    @property
    def side_target_count(self) -> int:
        """
        The side targets' length: the score lead soon and at the end, then [3][N][N] final territory (docs/encoding.md section 8.5)
        """

    @property
    def target_count(self) -> int:
        """The belief targets' length (docs/encoding.md section 8)"""

    @property
    def target_heads(self) -> list[tuple[str, int, int, int]]:
        """
        Each belief head's part of the targets: (name, opponent, offset, size), opponents counted from 1.
        """

    def encode_state(self, game: Game, player: int, full_information: bool = False) -> tuple[NDArray[numpy.float32], NDArray[numpy.float32]]:
        """
        The player's input: (planes [C, N, N], globals [G]). With full_information, from the full view (every tile and opponent shown, `explored` still the player's own).
        """

    def encode_states(self, games: Sequence[Game], players: Sequence[int]) -> tuple[NDArray[numpy.float32], NDArray[numpy.float32]]:
        """A batch: (planes [B, C, N, N], globals [B, G])."""

    def legal_mask(self, game: Game) -> NDArray[numpy.uint8]:
        """1 for each legal command's slot: [A]."""

    def legal_masks(self, games: Sequence[Game]) -> NDArray[numpy.uint8]:
        """A batch of masks: [B, A]."""

    def encode_targets(self, games: Sequence[Game], players: Sequence[int]) -> tuple[NDArray[numpy.float32], NDArray[numpy.uint8]]:
        """
        The belief targets from the true games, and where each is trained: (targets [B, T], masks [B, T]). The value target is the game's result, added once it's over.
        """

    def encode_examples(self, records: Sequence[GameRecord], positions: Sequence[tuple[int, int]]) -> dict[str, numpy.typing.NDArray[Any]]:
        """
        Training examples at (record, move) positions, each the position just before that move, for the player to move: a dict of planes [B, C, N, N], globals [B, G], legal [B, A], policy [B, A] (the search's visits, normalized; all 0 for a move it didn't choose), value and value_weight [B] (GameRecord.value: the result, or for a game cut off by the turn limit the player's final value; weights all 1), targets and target_masks [B, T], side_targets and side_masks [B, S]. Copies the records: for training, use ReplayBuffer.encode.
        """

    def encode_command(self, game: Game, command: Command) -> int:
        """The command's slot, or -1 if it has none (Resign, Stay)."""

    def decode_command(self, game: Game, index: int) -> Command:
        """The legal command in that slot; raises ValueError if none is."""

def determinize(game: Game, player: int, seed: int, encoding: Encoding | None = None, beliefs: Annotated[NDArray[numpy.float32], dict(shape=(None,), order='C', writable=False)] | None = None) -> Game | None:
    """
    A game agreeing with everything `player` knows of `game`, the rest sampled as the search does: from the network's raw belief outputs for the player's position (with `encoding`), else the fixed priors. None if no sample fit.
    """

def possible_tribes(view: PlayerView, opponent: int, turns: int = 1, max_states: int = 5000, max_work: int = 50000) -> list[Tribe]:
    """
    The tribes the opponent could be, from their starting score, the climates seen and their first `turns` score changes (docs/tribeInference.md). Always includes the real one; the limits only bound the time.
    """

def score_changes_fit(view: PlayerView, opponent: int, tribe: Tribe, turns: int = 1, max_states: int = 5000, max_work: int = 50000) -> bool:
    """
    Whether a player of `tribe` could have made the opponent's published score changes so far. Two-player games only; otherwise True.
    """

def climates_seen(view: PlayerView) -> list[Tribe]:
    """The tribes whose climate the player has seen."""

def tribes_with_starting_score(score: int) -> list[Tribe]:
    """The tribes that could start the game with this score."""

def starting_explored(tribe: Tribe, score: int) -> int:
    """
    How many tiles a player of this tribe with this starting score started with explored (9 to 25), or -1 if none fits. 25 means their capital is at least 2 tiles from every edge.
    """

def explained_score(view: PlayerView, player: int) -> int:
    """
    The part of a player's published score the viewer can account for from what they see.
    """
