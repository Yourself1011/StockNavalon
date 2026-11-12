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

    def score(self, player: int) -> int:
        """The live score, which only its own player sees."""

    def published_score(self, player: int) -> int:
        """The score other players see: from the end of the player's last turn."""

    @property
    def score_history(self) -> list[ScoreRecord]:
        """Every published score so far, oldest first."""

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

def play_games(count: int, seed: int = 0, threads: int = 1, simulations: int = 100, samples: int = 2, horizon: int = 1, max_turns: int = 50, temperature_turns: int = 3, map_size: int = 11, full_share: float = 0.25, quick_simulations: int = 25, stall_turns: int = 0, fog_share: float = 1.0, peace_treaties: bool = True) -> list[GameRecord]:
    """
    Self-play games with the search on both seats, scored by the score lead until there's a network. A full_share of decisions get the full search and are recorded for the policy; the rest a quick one.
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

    def __init__(self, parallel: int, games: int, seed: int = 0, simulations: int = 100, samples: int = 2, horizon: int = 1, max_turns: int = 50, temperature_turns: int = 3, map_size: int = 11, root_noise: bool = True, threads: int = 1, full_share: float = 0.25, quick_simulations: int = 25, sides: Sequence[str] = ['network', 'network'], paired: bool = False, stall_turns: int = 0, fog_share: float = 1.0, network_beliefs: bool = False, peace_treaties: bool = True, first_play_urgency: bool = True, tree_reuse: bool = True, gumbel: Sequence[bool] = [False, False], gumbel_candidates: int = 16) -> None: ...

    def done(self) -> bool: ...

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

    def submit_move_logits(self, encoding: Encoding, move_logits: Annotated[NDArray[numpy.float32], dict(order='C', device='cpu', writable=False)], values: Annotated[NDArray[numpy.float32], dict(order='C', device='cpu', writable=False)], beliefs: Annotated[NDArray[numpy.float32], dict(order='C', device='cpu', writable=False)] | None = None) -> None:
        """
        The logits gathered at pending_slots, the values [B], and for the pending_belief_rows their raw belief outputs [R, belief_output_count] (without them, those take priorBeliefs).
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

    def encode(self, encoding: Encoding, positions: Sequence[tuple[int, int]], threads: int = 8, city_weight: float = 0.0, cut_off_weight: float = 1.0, search_weight: float = 0.0) -> dict[str, numpy.typing.NDArray[Any]]:
        """
        Training examples at those positions, like Encoding.encode_examples. The positions only hold until the next add(): from another thread than the one adding, use sample_examples.
        """

    def sample_examples(self, encoding: Encoding, count: int, seed: int, per_game: int = 4, threads: int = 8, augment: bool = False, city_weight: float = 0.0, cut_off_weight: float = 1.0, search_weight: float = 0.0, positions: bool = False) -> dict[str, numpy.typing.NDArray[Any]]:
        """
        Random training examples, sampled and encoded at once, so safe while another thread adds games. With augment, each is seen through a random one of the map's 8 symmetries (rotations and reflections). city_weight blends the value target with the final city lead (early in a run); cut_off_weight is the value weight of positions from games cut off by the turn limit or a stalemate. With positions, the dict also has game_seeds and moves [B]: which positions were drawn, to encode again later (Encoding.encode_examples).
        """

    def save(self, path: str) -> None:
        """Writes exactly the buffer's games to a record file, replacing it."""

def save_records(records: Sequence[GameRecord], path: str, append: bool = True) -> None:
    """Appends records to a text file, or with append=False replaces it."""

def load_records(path: str) -> list[GameRecord]: ...

def thin_policy_targets(records: Sequence[GameRecord], share: float, seed: int = 0) -> list[GameRecord]:
    """
    Copies of the records keeping the search's visits (the policy target, and what makes a move a training position) on a random share of their moves: a match's games at self-play's density.
    """

def run_cli(args: Sequence[str], encoding: Encoding, evaluate: Callable, simulations: int = 200, samples: int = 4, horizon: int = 1, network_beliefs: bool = True, gumbel: bool = False) -> int:
    """
    The engine's terminal game (README "Run"), with the seats given by --bot in `args` played by the search, its positions evaluated by evaluate(planes, globals, rows, slots, belief_rows) -> (move logits, values, raw belief outputs of belief_rows or None), as with SelfPlaySession.submit_move_logits. With network_beliefs, each bot turn samples the hidden state from the network's beliefs (asked once per turn), else from the fixed priors. Returns the exit code.
    """

def play_random_games(games: int, tribes: Sequence[Tribe], type: MapType = MapType.LAKES, size: int = 14, seed: int = 0, max_turns: int = 100) -> list[tuple[int, int]]:
    """
    Plays random games on generated maps. Returns (winner, turns) for each; winner is -1 if the turn limit came first.
    """

ENCODING_VERSION: int = 6

MAP_TYPE_COUNT: int = 6

OPENING_CHANGES: int = 6

SLOTS_PER_TILE: int = 218

STAR_COMPONENTS: int = 3

UNEXPLORED_TERRAIN: int = 0

UNEXPLORED_RESOURCE: int = 5

UNEXPLORED_FEATURE: int = 12

UNEXPLORED_LAYERS: int = 15

SIDE_SCORE_SOON: int = 0

SIDE_SCORE_END: int = 1

SIDE_TERRITORY: int = 2

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
