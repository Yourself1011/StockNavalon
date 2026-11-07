# Game

This game is a C++ replica of Polytopia. It exists so we can train a bot faster than the real game allows. No graphics are needed, only the game logic.

This file is the rules spec the engine is built from. It describes behaviour only, never the real game's code.

- **Target version:** 2.18 (Nov 2026). The last rules change found is the 2025 Balance Pass (1 Sep 2025), on top of Path of the Ocean (2.8) and Patch of the Ocean (2.8.5).
- **VERIFY** marks values that are uncertain, that sources disagree on, or that come from memory. All of them are collected in the checklist at the end.
- **Where values come from**, strongest first:
  - **(game):** checked by us in the real game. Wins over everything else.
  - **(data file):** read directly from the game's own `GameLogicData` (version 28, game 2.18.0 build 16751). It's extracted from your install into the scratchpad and never committed to the repo. Exact for numbers, but it doesn't contain any logic.
  - **(bengreff):** reported in the design docs of [bengreff/polytopia](https://github.com/bengreff/polytopia), which runs the real game's compiled rules (build 2.17.3). They say each fact comes from the game's data file, its code, or a measurement.
  - Unmarked: search excerpts of polytopia.fandom.com (the wiki itself would not load) and community sources. Sources are listed per section.

## 1. Overview and scope

Polytopia is a turn-based strategy game played on a square grid. Players gain stars at the start of each turn and spend them on actions: researching techs, building improvements, and training units.

**In scope:**

- the 12 regular tribes (section 17)
- **first target: 1v1 Might** on a Small (14×14) Lakes map with fog of war. This is the competitive format.
- later: more players, other map types and sizes, and Domination. The engine should keep player count, map type, map size and win condition as settings, not hard-code them.
- fog of war

**Out of scope:** see section 20.

## 2. Turn structure

- Players take turns one after another, in seat order. A round is complete once every player has taken a turn.
- **Start of a player's turn:**
    1. The player receives their star income (section 3). A city with an enemy unit on it produces nothing.
    2. Every unit the player owns can act again: move, attack and use abilities.
    3. Temples gain levels on their own schedule (section 6).

    This order is correct (game).

- **During the turn:** any number of actions in any order, as long as the player can afford them.
- **End turn:** the player ends their turn explicitly. Every unit that could still use an ability heals automatically, as if it had used Recover (section 12) (game).
- **Capturing and examining ruins:** a unit can only capture a village or city, or examine a ruin, if it _started_ its turn on that tile. So each takes at least 2 turns.
- **Actions that end a unit's turn** (bengreff): embarking, disembarking, capturing, examining a ruin, Recover, Heal Others.
- **Units without their tech:** units can come from ruins, city rewards, the starting unit or conversion without the player owning their tech. They work normally (bengreff).
- **Meeting a tribe** means seeing one of its units or one of its cities; a tile inside its borders alone doesn't count (game). The first time you meet another tribe you get `1 + 2 × ceil(their score / 1000)` stars, at most 11 (wiki). This is why the engine has to track score (section 3).

Sources: <https://polytopia.fandom.com/wiki/Game_Modes>, <https://github.com/bengreff/polytopia/blob/main/docs/design/scope-and-rules.md> (rules doc only)

### Actions

The real game has one command per kind of action. The engine should use the same set, so a later bridge to the real game maps one-to-one. This is the regular-tribe list (bengreff, from the game's command types):

| Group            | Commands                                                                                                                              |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| Units            | Move, Attack (includes Mind Bender conversion and Infiltrate), Recover, Heal Others, Promote, Disband, Stay, Disembark, Upgrade (boat) |
| Tiles and cities | Build (every improvement, including road, bridge, port and the forest actions), Destroy, Harvest (starfish), Capture, Examine Ruins   |
| Economy          | Train, Research, City Reward (choose a level-up reward)                                                                               |
| Diplomacy        | Peace Treaty (propose), Peace Request Response, Break Peace, Establish Embassy, Destroy Embassy                                       |
| Turn and game    | End Turn, Resign                                                                                                                      |

- Each command is identified by its type, a source tile, a target tile and one argument (unit type, improvement type, tech, city reward or opponent).

## 3. Stars and economy

- **Starting stars:**
  - 5 for most tribes
  - 7 for Xin-xi, Hoodrick, Quetzali and Yădakk
  - 6 for Oumaji
  - 2 for Luxidoor
- **Income** is added at the start of each of the player's turns. It is the sum of:
  - each city's level in stars
  - +1 for the capital, so a level-1 capital makes 2 (bengreff, from the code). The bonus is a per-player "handicap": humans and Normal bots get 1, Easy 0, Hard 2, Crazy 4. We use 1 for every player.
  - +1 per Workshop and +1 per Park (section 7)
  - Market income (section 6)
  - embassy income (section 15)
- There is no other base income.
- A city with an enemy unit standing on it (under siege) produces no stars.
- **Negative population:** each point of negative population lowers that city's income by 1, never below 0. It doesn't change how many units the city supports (wiki).
- **One-time stars:**
  - Clear Forest: +1
  - Disband: half the unit's cost, rounded down
  - meeting a tribe: see section 2
  - the Resources city reward: +5
  - Starfish Harvest: +8
  - the ruin star reward: +10

### Score

Score only decides the star bonus for meeting a tribe (section 2), but that bonus needs it tracked exactly (wiki Score page).

- **Units:** 5 points per star of the unit's cost. A super unit (Giant) is worth 50.
  - Boat upgrades add nothing.
  - Converting a unit changes neither player's score.
- **Territory:** 20 per tile inside the player's borders.
- **Exploration:** 5 per explored tile.
- **Cities:** a level-1 city is worth 100.
  - Each population point gained is +5.
  - Reaching level n also gives a bonus of 50 − 5n points: +40 at level 2, +35 at level 3, and so on.
  - Together, each level above 1 is worth 50 in total. From level 10 on, the bonus is zero or negative.
  - +250 per Park.
- **Monuments:** 400 each.
- **Temples:** 100 at level 1, +100 per level above it, up to 500 at level 5.
- **Techs:** 100 per tier of each tech owned (the basic tech is tier 0).
- **Losing points:** score goes down when the player loses a city, a building or a unit (killed or disbanded).
- **Check: starting scores on turn 0.** These are a good engine test:

    | Score | Tribes                                     |
    | ----- | ------------------------------------------ |
    | 515   | Xin-xi, Imperius, Bardur, Kickoo, Luxidoor |
    | 520   | Oumaji                                     |
    | 615   | Zebasi, Yădakk                             |
    | 620   | Quetzali, Hoodrick                         |
    | 730   | Vengir, Ai-Mo                              |

    A capital in a map corner scores 5 less, because the corner lighthouse tile starts hidden.

Sources: <https://polytopia.io/2025-balance-pass/>, <https://polytopia.fandom.com/wiki/Star>, <https://polytopia.fandom.com/wiki/City>

## 4. Terrain and resources

| Terrain       | Resources      | Notes                                                     |
| ------------- | -------------- | --------------------------------------------------------- |
| Field         | Fruit, Crop    |                                                           |
| Forest        | Game           | Entering it ends movement (section 10)                    |
| Mountain      | Metal          | Needs Climbing to enter. Units on a mountain see 2 tiles. |
| Shallow water | Fish, Starfish | Water next to land                                        |
| Ocean         | Starfish       | Water not next to land. Ruins can appear here.            |

- Whales and Whale Hunting were removed in 2.8; Starfish replaced them.
- **Hidden resources:** every resource is on the map from the start. A resource is only shown to a player once the tech that uses it is researchable or owned (game). For example, crops appear once Farming can be bought, which happens after Organization.

Sources: <https://polytopia.fandom.com/wiki/Terrain>, <https://polytopia.fandom.com/wiki/Technology>

## 5. Tech tree and cost

**Cost:** `tier × (number of cities owned) + 4`, so with one city a tech costs 5, 6 or 7 stars by tier.

- **Literacy** (from Philosophy) makes every tech a third cheaper, rounded up (game).
- At one city, tiers 1/2/3 cost 5/6/7 (bengreff, from the game's own price function).

| Tier 1       | Unlocks                                                  | Tier 2 (parent: tier 1)                                                            | Tier 3 (parent: tier 2)                                                                                                    |
| ------------ | -------------------------------------------------------- | ---------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| Riding       | Rider                                                    | **Roads:** Road, Bridge · **Free Spirit:** Temple, Disband                         | **Trade** (←Roads): Market, Wealth task · **Chivalry** (←Free Spirit): Knight, Destroy                                     |
| Organization | Harvest Fruit; reveals crop                              | **Farming:** Farm · **Strategy:** Defender, peace treaties                         | **Construction** (←Farming): Windmill, Burn Forest · **Diplomacy** (←Strategy): Cloak, Embassy, Capital Vision             |
| Climbing     | Mountain movement; mountain defence bonus; reveals metal | **Mining:** Mine · **Meditation:** Mountain Temple, Pacifist task                  | **Smithery** (←Mining): Swordsman, Forge · **Philosophy** (←Meditation): Mind Bender, Literacy, Genius task                |
| Hunting      | Hunting                                                  | **Archery:** Archer; forest defence bonus · **Forestry:** Lumber Hut, Clear Forest | **Spiritualism** (←Archery): Grow Forest, Forest Temple · **Mathematics** (←Forestry): Catapult, Sawmill                   |
| Fishing      | Fishing, Port; shallow-water movement                    | **Sailing:** ocean movement, Scout · **Ramming:** Rammer                           | **Navigation** (←Sailing): Bomber, Starfish Harvest · **Aquatism** (←Ramming): Water Temple; water and ocean defence bonus |

- You can research a tech once you own its parent.
- **Backwards research:** owning a child tech also makes its parent researchable (game). For example, Vengir can research Mining from Smithery without owning Climbing.
- The parent links, tiers and unlocks in this table match the data file. Ocean movement comes from Sailing. Strategy is called `shields` in the data file.
- The data file's terrain defence bonuses are 15 (meaning ×1.5): forest from Archery, mountain from Climbing, water and ocean from Aquatism.
- Every player also owns a hidden tier-0 "basic" tech from the start. It unlocks Warrior, Giant, Dagger, the explorer, the Raft, cities and ruins, and movement on field and forest.
- **Movement unlocks** in the data file are numbers per terrain: field 1, forest 2 (basic); mountain 2 (Climbing); shallow water 2 (Fishing); ocean 1 (Sailing). **VERIFY** what the numbers mean. They don't match plain movement costs, since forest and mountain end movement.

Sources: <https://polytopia.fandom.com/wiki/Technology>, <https://polytopia.io/2025-balance-pass/>

## 6. Improvements and actions

You can only build on tiles inside your own borders, unless a row says otherwise.

| Name             | Cost | Population                 | Requirement / effect                                                                                                                                                |
| ---------------- | ---- | -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Harvest Fruit    | 2    | +1                         | Removes the fruit                                                                                                                                                   |
| Hunting          | 2    | +1                         | Removes the game                                                                                                                                                    |
| Fishing          | 2    | +1                         | Removes the fish                                                                                                                                                    |
| Starfish Harvest | 0    | –                          | +8 stars. Done by a naval unit standing on the starfish; also works outside your borders.                                                                           |
| Farm             | 5    | +2                         | Field with crop                                                                                                                                                     |
| Mine             | 5    | +2                         | Mountain with metal                                                                                                                                                 |
| Lumber Hut       | 3    | +1                         | Forest                                                                                                                                                              |
| Clear Forest     | 0    | –                          | Forest becomes field; +1 star                                                                                                                                       |
| Burn Forest      | 3    | –                          | Empty forest becomes field with crop                                                                                                                                |
| Grow Forest      | 5    | –                          | Field becomes forest                                                                                                                                                |
| Windmill         | 5    | +1 per adjacent Farm       | Field, next to at least one Farm. Max level 8. One per city.                                                                                                        |
| Sawmill          | 5    | +1 per adjacent Lumber Hut | Field, next to at least one Lumber Hut. Max level 8. One per city.                                                                                                  |
| Forge            | 5    | +2 per adjacent Mine       | Field or forest, next to at least one Mine. Max level 8. One per city.                                                                                              |
| Market           | 5    | 0                          | Field, next to at least one Windmill, Sawmill or Forge. Each turn, gives +1 star per level of each adjacent Windmill, Sawmill and Forge. Max level 8. One per city. |
| Port             | 7    | +1                         | Shallow water only. Lets units embark (section 10). Connects cities over water (section 7). You can't use a Port in enemy territory.                                |
| Road             | 3    | –                          | Field or forest, on a free or own tile. Not on mountains. Can be built outside your borders.                                                                        |
| Bridge           | 5    | –                          | One shallow-water tile; acts as a road                                                                                                                              |
| Temple           | 20   | +1                         | Field                                                                                                                                                               |
| Forest Temple    | 15   | +1                         | Forest                                                                                                                                                              |
| Mountain Temple  | 20   | +1                         | Mountain                                                                                                                                                            |
| Water Temple     | 20   | +1                         | Shallow water or ocean                                                                                                                                              |
| Monument         | 0    | +3                         | Unlocked by completing its task (section 14). One of each per game.                                                                                                 |
| Destroy          | 0    | –                          | Removes a building or ruin from your own tile; you lose what it gave                                                                                                |
| Disband          | –    | –                          | Removes your unit and refunds half its cost, rounded down                                                                                                           |

- **Adjacency buildings** (bengreff):
  - A Windmill, Forge or Sawmill needs at least one adjacent Farm, Mine or Lumber Hut respectively.
  - A Market needs an adjacent Windmill, Forge or Sawmill.
  - These buildings level up as matching neighbours are built later. Each new level adds population straight away, which can level the city up in the middle of a turn.
- **"One per city"** is the data file's `limited` flag on Windmill, Sawmill, Forge and Market. We read it as one per city.
- **Max level 8:** Windmill, Sawmill, Forge and Market are capped at level 8, so Market income is capped too.
- **Temples:** a temple starts at level 1, worth 100 points. It gains a level every 2 turns, up to level 5, for +100 points each time. Points only matter for score, so in Domination a temple is just +1 population.
- **Can be built outside your borders:** Road, Bridge and Starfish Harvest (the data file's `freelance` flag).
- **Harvests remove the resource:** Fruit, Hunting, Fishing, Starfish, Clear Forest and Burn Forest consume it (`consumed` flag).
- **Done by a unit:** Starfish Harvest needs a unit standing on the tile (`manual` flag).
- **Removed:** Customs House and Whale Hunting no longer exist.

Sources: <https://polytopia.fandom.com/wiki/Buildings>, <https://polytopia.fandom.com/wiki/Market>, <https://polytopia.fandom.com/wiki/Temples>, <https://polytopia.fandom.com/wiki/Port>, <https://steamcommunity.com/app/874390/discussions/0/3757726983059762031/>, <https://polytopia.io/2025-balance-pass/>

## 7. Cities

### Villages and capture

- A village is a neutral settlement.
- A unit that starts its turn on a village or an enemy city can capture it. Capturing ends that unit's turn.
- A captured village becomes a level-1 city. A captured enemy city keeps its level.
- The capturing unit is then supported by the captured city.
- A captured capital loses its capital connections and the population those gave (section "Connections").

### Population and levels

- Going from level n−1 to level n needs n population (bengreff, measured). Leftover population carries over.
- A city never loses a level, but its population can go negative.

### Level-up rewards

The owner chooses one reward each time the city levels up:

| New level | Option A                                             | Option B                              |
| --------- | ---------------------------------------------------- | ------------------------------------- |
| 2         | Workshop (+1 star/turn)                              | Explorer (section 13)                 |
| 3         | City Wall (×4 defence for Fortify units in the city) | Resources (+5 stars)                  |
| 4         | Population Growth (+3 population)                    | Border Growth (borders 3×3 → 5×5)     |
| 5 and up  | Park (+1 star/turn)                                  | Super Unit (a Giant on the city tile) |

### Borders

- A city's borders start as the 3×3 area around it.
- Border Growth expands them to 5×5, but never onto tiles another city already owns.

### Connections

- A city is connected if a path of roads, bridges and ports runs from it to the capital, or to a city that is already connected. No tech is needed for the connection itself.
- When a city becomes connected, it and the capital each get +1 population.
- **Roads used:** your own, plus roads in neutral or allied (peace treaty) territory. Never roads in enemy territory.
- **Fog:** no connection forms through road or water tiles you haven't explored.
- **Over water:**
  - A Port links to another Port, or to a city, at most 5 tiles away, i.e. with at most 4 water tiles between them (game). This matches the data file's range of 5.
  - The water route can only cross your own, neutral or allied territory with no fog.
  - Crossing ocean tiles needs Navigation.
  - So with a Port next to the capital, cities can be connected without roads.
- **Shared neutral roads:** a neutral road touching several players' cities can only be used for new connections by the player first in seat order. Connections that already exist aren't affected, and this doesn't apply over water.
- **Breaking connections:**
  - Capturing a connected city breaks every connection that used roads in its territory.
  - Ending a peace treaty breaks connections that used the former ally's roads.
  - A broken connection removes its population, which can leave the capital on negative population.

### Unit cap

- Each city supports (level + 1) units (bengreff, measured; wiki).
- You can't train a unit in a city that is already at its cap.
- Independent units don't count toward any city's cap.
- **Which city a unit belongs to** (wiki):
  - A unit that captures a village or city moves over to that city.
  - A unit from a ruin belongs to the exploring unit's city, but doesn't count toward its cap.
  - A converted unit counts toward the converting player's capital (or a random city if they have no capital), even past the cap. While over the cap, the city can't train units.
  - When a city is captured, the units it trained lose their home city. They don't count again if the city is retaken.

### Siege

- An enemy unit standing on a city stops that city's income and stops it training units.

### Giant placement

When a Giant spawns on a city tile that already has a unit on it, that unit is pushed off. A ruin's reward unit pushes the examining unit off its ruin the same way (game):

- A new unit is pushed toward the centre of the map.
- A friendly unit is pushed onward in the direction it was moving.
- An enemy unit is pushed backward.
- If the target tile is blocked, try the next tiles counter-clockwise, then clockwise, at increasing distance.
- If no tile is free, the pushed unit is destroyed.
- Pushing an enemy off a city this way lifts the siege (bengreff).

The push directions are confirmed (game).

Sources: <https://polytopia.fandom.com/wiki/City>, <https://polytopia.fandom.com/wiki/Population>, <https://polytopia.fandom.com/wiki/City_Connection>, <https://docs.google.com/document/d/1ZvDRejjXxgwr-NjIavDckoCeHrz4zUkagzpt9Xk0Nl8/mobilebasic>

## 8. Units

- Cost is in stars. Movement and range are in tiles.
- Defence values like 0.5 are exact; they are not rounded.
- "Trainable" means the unit can be trained in a city. Units that aren't trainable come from rewards, embarking, upgrading or infiltrating.

| Unit        | Cost | HP               | Attack | Defence | Movement | Range | Skills                                                     | Unlocked by                                   | Trainable |
| ----------- | ---- | ---------------- | ------ | ------- | -------- | ----- | ---------------------------------------------------------- | --------------------------------------------- | --------- |
| Warrior     | 2    | 10               | 2      | 2       | 1        | 1     | Dash, Fortify                                              | start                                         | yes       |
| Rider       | 3    | 10               | 2      | 1       | 2        | 1     | Dash, Escape, Fortify                                      | Riding                                        | yes       |
| Archer      | 3    | 10               | 2      | 1       | 1        | 2     | Dash, Fortify                                              | Archery                                       | yes       |
| Defender    | 3    | 15               | 1      | 3       | 1        | 1     | Fortify                                                    | Strategy                                      | yes       |
| Swordsman   | 5    | 15               | 3      | 3       | 1        | 1     | Dash (no Fortify, per the game data via bengreff)          | Smithery                                      | yes       |
| Mind Bender | 5    | 10               | 0      | 1       | 1        | 1     | Heal, Convert, Stiff                                       | Philosophy                                    | yes       |
| Catapult    | 8    | 10               | 4      | 0       | 1        | 3     | Stiff                                                      | Mathematics                                   | yes       |
| Knight      | 8    | 10               | 3.5    | 1       | 3        | 1     | Dash, Persist, Fortify                                     | Chivalry                                      | yes       |
| Cloak       | 8    | 5                | 2      | 0.5     | 2        | 1     | Hide, Infiltrate, Dash, Scout, Creep, Stiff, Static        | Diplomacy                                     | yes       |
| Dagger      | –    | 10               | 2      | 2       | 1        | 1     | Dash, Surprise, Independent, Static                        | from Infiltrate only                          | no        |
| Giant       | –    | 40               | 5      | 4       | 1        | 1     | Static                                                     | Super Unit reward                             | no        |
| Raft        | 0    | (passenger's)    | 0      | 1       | 2        | 2     | Water, Carry, Stiff, Static                                | land unit moves onto a Port                   | no        |
| Scout       | +5   | (passenger's)    | 2      | 1       | 3        | 2     | Water, Dash, Carry, Scout, Static                          | upgrade a Raft (Sailing)                      | no        |
| Rammer      | +5   | (passenger's)    | 3      | 3       | 3        | 1     | Water, Dash, Carry, Static                                 | upgrade a Raft (Ramming)                      | no        |
| Bomber      | +15  | (passenger's)    | 3      | 2       | 2        | 3     | Water, Carry, Splash, Stiff, Static                        | upgrade a Raft (Navigation)                   | no        |
| Juggernaut  | 0    | 40 (passenger's) | 4      | 4       | 2        | 1     | Water, Carry, Stiff, Stomp, Static                         | Giant moves onto a Port                       | no        |
| Dinghy      | –    | (passenger's)    | 2      | 0.5     | 2        | 1     | Water, Carry, Hide, Infiltrate, Dash, Scout, Stiff, Static | Cloak moves onto a Port                       | no        |
| Pirate      | –    | 15               | 2      | 2       | 2        | 1     | Water, Carry, Dash, Surprise, Independent, Static          | Dagger moves onto a Port, or spawned on water | no        |
| Explorer    | –    | –                | –      | –       | –        | –     | Moves itself; can't be controlled (section 13)             | Explorer reward, ruins                        | no        |

- **Where these numbers come from:** every row except the Explorer matches the data file, which stores HP, attack and defence ×10.
  - A boat takes the current HP and max HP of the unit that embarked, including more than 10 (e.g. a veteran or a Giant) (game). The data file's HP of 10 for boats is ignored. The Pirate has no passenger, so it uses its own 15.
  - Every boat also has a `swim` skill alongside Water. **VERIFY** what it does.
  - Data-file names: Raft is `transportship`, Scout `scoutship`, Rammer `rammership`, Bomber `bombership`, Dinghy `cloak_boat`. The Explorer is a unit called `scout` with 1 HP.
- **Promotion limit:** 3 kills for Warrior, Rider, Archer, Defender, Swordsman, Mind Bender, Catapult, Knight and Cloak. 0 (never) for Dagger, Giant and every boat.
  - The Cloak has a limit of 3 in the data file but is also Static, and the wiki says Static units never become veterans. So the Cloak can't be promoted.
- **Pirate:** Independent (game; wiki), even though the data file doesn't list that skill. Presumably it inherits it from the Dagger it was made from.
- **Unlisted costs:** Dagger costs 2 and Giant 10 in the data file, but neither can be trained. Score uses these costs: a Giant is worth 50, which matches 10 × 5 (section 3). Disband refunds use them too.
- **Naval units:**
  - A naval unit has the HP and max HP of the land unit it carries.
  - A Raft can only be upgraded inside your own borders. Upgrading does not use the Raft's turn (game).
  - A Raft that is already upgraded can be changed to another boat type by paying the difference (bengreff).
  - Disembarking loses the boat upgrade: the passenger comes back as itself (bengreff). It keeps its HP and veteran status (game).
- **Population:** every unit except Independent ones belongs to a city and counts toward that city's cap. A trained unit belongs to the city that trained it.

Sources: <https://polytopia.fandom.com/wiki/List_of_Units>, each unit's page, <https://polytopia.fandom.com/wiki/Port>, <https://devtrackers.gg/polytopia/p/18b222db-experience-the-path-of-the-ocean>

## 9. Unit skills

| Skill       | Effect                                                                                                                                                                                                                                                                                                     |
| ----------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Dash        | Can attack after moving in the same turn                                                                                                                                                                                                                                                                   |
| Escape      | Can move after attacking. It can't attack again that turn, even with Dash (wiki).                                                                                                                                                                                                                          |
| Persist     | Can attack again immediately after a kill, with no limit on repeats                                                                                                                                                                                                                                        |
| Fortify     | Defence ×1.5 in your own city, ×4 if that city has a City Wall. Only Fortify units get any city bonus.                                                                                                                                                                                                     |
| Heal        | "Heal Others" action: up to +4 HP to every adjacent friendly unit. Uses the unit's turn (game).                                                                                                                                                                                                            |
| Convert     | Its attack converts the target to the attacker's side instead of damaging it. The converted unit counts toward your capital's cap (or a random city if you have no capital), unless it's Independent.                                                                                                      |
| Splash      | The attack also hits enemies next to the target, for half damage (section 11)                                                                                                                                                                                                                              |
| Stiff       | Never retaliates                                                                                                                                                                                                                                                                                           |
| Surprise    | Its attacks get no retaliation                                                                                                                                                                                                                                                                             |
| Static      | Can never become a veteran                                                                                                                                                                                                                                                                                 |
| Independent | Uses no population and belongs to no city                                                                                                                                                                                                                                                                  |
| Hide        | The unit becomes invisible to enemies when it moves; there is no Hide action (game). The move that hides a visible unit ends its attack for the turn, even with Dash (game). It ignores zone of control and can move through enemy units and enemy cities, but can't end a move on an enemy city (game). It stays hidden on an unclaimed village; capturing the village reveals it until it next moves (game). An enemy unit moving onto its exact tile reveals it, and that enemy keeps its move (game). Enemy units next to it see a warning (an eye symbol) but not the unit itself (game). |
| Creep       | Ignores terrain movement penalties except mountains. Does **not** ignore zone of control (wiki).                                                                                                                                                                                                           |
| Scout       | Sees 2 tiles instead of 1                                                                                                                                                                                                                                                                                  |
| Infiltrate  | Only targets adjacent enemy cities, never unclaimed villages (game). The unit is used up and spawns as many Daggers as the city's level (max 5) inside that city's borders. Pirates spawn on water tiles. |
| Carry       | Carries one land unit. Moving onto land next to water lets it off, which ends the turn (section 10).                                                                                                                                                                                                       |
| Water       | Can only move on water                                                                                                                                                                                                                                                                                     |
| Stomp       | Every time it moves, including disembarking, every adjacent enemy takes splash damage (section 11) (game). Assumed to be half of what the unit's attack on that enemy would deal, not rounded.                                                                                                             |

Source: <https://polytopia.fandom.com/wiki/Unit_Skills>

## 10. Movement and embarking

- **Distance** everywhere in the game counts diagonal steps the same as straight ones (game). That covers movement, attack range, sight, borders and spacing rules. So "within 2 tiles" means a 5×5 square.
- **Movement points:** each step onto a neighbouring tile (including diagonals) costs 1.
- **Forest and mountain:** entering a forest or mountain ends movement. Leaving one costs nothing extra.
- **Mountains** need Climbing.
- **Roads:**
  - A step from a road or city tile to another road or city tile costs 0.5. Cities count as roads.
  - That step ignores the forest penalty, but not zone of control.
  - A bridge counts as a road.
- **Zone of control:**
  - A unit that steps onto a tile next to an enemy must stop there, even with movement left.
  - The unit's starting tile doesn't count.
  - Units with Hide or Creep ignore this. (Sneak also ignores it, but no current unit has Sneak.)
- **Occupied tiles:** a unit can never stop on an occupied tile, but it can pass through friendly units. Zone of control only comes from enemy units, never from friendly or allied ones (game).
- **Water:**
  - Land units can't enter water except by embarking.
  - Water units can't enter land.
  - Shallow water needs Fishing; ocean needs Sailing.
- **Embarking:**
  - A land unit that moves onto a friendly Port becomes a Raft carrying it.
  - A Giant becomes a Juggernaut, a Cloak a Dinghy, a Dagger a Pirate.
  - Embarking ends the unit's turn.
- **Disembarking:**
  - A naval unit that moves onto land next to water turns back into its passenger.
  - Disembarking ends its turn.
  - The passenger keeps its veteran status and current HP (game), but the boat upgrade is lost (section 8).

Sources: <https://polytopia.fandom.com/wiki/Movement>, <https://polytopia.fandom.com/wiki/Roads_(Building)>, <https://polytopia.fandom.com/wiki/Port>

## 11. Combat

There is no randomness in combat:

```
attackForce  = attack  * (hp / maxHp)
defenceForce = defence * (hp / maxHp) * defenceBonus
total        = attackForce + defenceForce
damage       = round(attackForce  / total * attack  * 4.5)        // to defender
retaliation  = round(defenceForce / total * defence * 4.5)        // to attacker
```

- Retaliation uses the defender's health from **before** the hit (bengreff, checked against the game's own combat function on 27 attacks).
- **Example:** a full-health Warrior attacking a full-health Warrior: 4.5 before rounding, so 5 damage dealt and 5 taken back (see Rounding below).
- **Defence bonus:**
  - 1 normally.
  - Units besieging an enemy city get no city bonus, and villages give none.
  - 1.5 on forest with Archery, on mountains with Climbing, on water and ocean with Aquatism.
  - 1.5 for a Fortify unit in its own city. 4 if that city has a City Wall; the game shows this as a double shield (game).
  - City and terrain bonuses never combine, because a city tile has no forest or mountain (game).
- **Retaliation** happens only if all of these hold:
  - the defender survived
  - the attacker is within the defender's range
  - the defender can see the attacker. An attacker hidden in fog doesn't get hit back (game).
  - the defender isn't Stiff
  - the attacker doesn't have Surprise
  - retaliation doesn't round to 0 (only matters if damage is rounded; see Rounding below)
- **Splash:** every enemy next to the target takes half of the main (already rounded) damage, **not** rounded. So an odd main damage leaves those units on a .5 HP value (wiki).
- **After a kill:**
  - A melee attacker (range 1) moves into the dead unit's tile, if it's allowed to enter that terrain.
  - A ranged attacker stays put, even when it attacked from 1 tile away.
- **Naval units:** killing a naval unit also kills the unit it carries.
- **Rounding:** damage and retaliation are rounded to the nearest whole HP, halves up (wiki; bengreff checked this against the game's own combat function on 27 attacks).
  - So a full-health Warrior attacking a full-health Warrior deals 5 and takes 5 back.
  - HP can still end up fractional, and even below 1, because splash damage isn't rounded (see Splash). That explains the in-game observation of units below 1 HP.
  - **Displaying a .5 HP value:** the map rounds it up and the unit info box rounds it down, so a unit can show 0 HP in its info box while still alive. Display only; the engine keeps the exact value.
  - The engine should store HP in tenths (as integers), so all of these values are exact.
- **Attacking ends movement:** a unit can't move after attacking unless it has Escape. A unit that moved can't attack unless it has Dash.

Sources: <https://polytopia.fandom.com/wiki/Combat>, <https://github.com/neveront/polytopia_calculator>, <https://polytopia.fandom.com/wiki/City_Wall>, <https://github.com/bengreff/polytopia/blob/main/docs/strategy/universal-book.md> (Part II §1)

## 12. Veterans and healing

- **Veterans:**
  - After 3 kills (by attacking or retaliating), a unit can be promoted.
  - Promotion is an explicit Promote action (bengreff). It gives +5 max HP and heals the unit fully.
  - Static units can never be promoted.
- **Healing (Recover):**
  - Recover is an ability: +4 HP inside your borders, +2 elsewhere, never above max. It ends the unit's turn.
  - A unit can only use it if it hasn't used its turn, i.e. hasn't moved or attacked (bengreff).
  - A unit that can still use an ability when the player ends their turn recovers automatically (game).
- **Mind Bender:** the Heal action gives up to +4 to every adjacent friendly unit.

Sources: <https://polytopia.fandom.com/wiki/Units>, <https://polytopia.fandom.com/wiki/City>

## 13. Ruins and explorers

- **Ruins:** a unit standing on a ruin can examine it, but only if it started its turn there, not on the turn it arrives (game). Examining is an action and ends the unit's turn (game).
- **Rewards** are picked uniformly at random from those whose conditions hold (game):
  - a free tech (only if some tech is left to research)
  - 10 stars
  - +3 population in the capital
  - an Explorer (only if some tile in the 5×5 around the ruin is still unexplored)
  - on land, a veteran Swordsman; on water, a veteran Rammer carrying a Warrior. It appears on the ruin and pushes the examining unit off by the Giant placement rules (section 7) (game).
- The full conditions are listed at <https://polytopia.fandom.com/wiki/Ruin>.

- **Explorer** (wiki, version 115):
  - It moves itself 12 times (15 before Feb 2026), one step each time, then disappears.
  - It can enter the same terrain the player's units can: mountains with Climbing, shallow water with Fishing, ocean with Sailing (game). The wiki's Climbing, Sailing and Navigation is out of date.
  - It can step onto tiles with other units on them, including enemies.
  - Before every step it scores tiles again. Lower is better:
    - A tile it could stand on scores by the fog tiles standing there would clear (its 3×3), counting at most 4: 143 for 4 or more, 153 for 3, 163 for 2, 173 for 1. If a lighthouse is among that fog, 110, 120, 130 and 140 instead. Fog cleared by sight from a mountain doesn't count.
    - A breadth-first search then spreads scores 3 steps outward, +100 a step, keeping the lowest score per tile. So the closest fog always wins, and the amount cleared breaks ties between equally close options. In all it looks 4 steps ahead.
    - Tiles it has already walked over get a small penalty, so it avoids backtracking when it can.
  - It steps to the neighbour with the lowest score. Neighbours with no score (no fog within reach) count as equal, apart from the backtracking penalty. Ties are broken at random.
  - If it has no neighbour it can enter, it disappears.

Sources: <https://polytopia.fandom.com/wiki/Ruin>, <https://polytopia.fandom.com/wiki/Explorer>

## 14. Monuments and tasks

Each task appears once its trigger happens. Completing it lets the player build that monument once.

| Task       | Appears after            | Goal                             | Monument        |
| ---------- | ------------------------ | -------------------------------- | --------------- |
| Pacifist   | researching Meditation   | Make no attacks for 5 turns      | Altar of Peace  |
| Wealth     | researching Trade        | Have 100 stars at once           | Emperor's Tomb  |
| Explorer   | finding 1 lighthouse     | Find all 4 corner lighthouses    | Eye of God      |
| Killer     | killing an enemy unit    | Kill 10 enemy units              | Gate of Power   |
| Network    | making 1 city connection | Connect 5 cities to the capital  | Grand Bazaar    |
| Metropolis | a city leveling up       | Have a city at level 5 or higher | Park of Fortune |
| Genius     | researching Philosophy   | Research every tech              | Tower of Wisdom |

- **Monument stats:** each gives +3 population and is worth 400 points.
- **Placement:** on field or shallow water (data file; wiki), presumably within your borders. Each monument is unique.
- **Lighthouses:** finding one gives +1 population to your capital (game; wiki).

Source: <https://polytopia.fandom.com/wiki/Monuments>

## 15. Diplomacy

- **Embassy** (Diplomacy tech):
  - Costs 5 stars (data file; wiki).
  - Can only be built in another tribe's capital. No unit is needed (game).
  - The capital must still belong to its original owner, not a tribe that captured it.
  - At most one embassy per tribe.
  - Gives +2 stars per turn to both the builder and the host, or +4 each while they have a peace treaty.
  - The builder sees the 3×3 area around the host's capital.
  - **Attacking destroys embassies:** attacking a tribe (one of its units or cities) destroys the attacker's embassy in the defender's capital and the defender's embassy in the attacker's capital.
  - Embassies can also be destroyed with an explicit action (bengreff).
  - The data file also has embassy levels, an upgrade cost and a tech-price multiplier, but none of these exist in the game (game; wiki). Ignore them.
- **Capital Vision** (Diplomacy): shows the capitals of tribes you have met.
- **Peace treaty** (Strategy):
  - One side proposes and the other accepts.
  - While it lasts: neither side can attack the other, each can use the other's roads, and embassy income doubles.
  - Either side can cancel it.
  - **Breaking it:** the breaker can't attack that turn, and their active units freeze. Their units inside the other tribe's borders are disbanded with no refund.
  - Built-in bots do propose peace, so an agent playing against them has to answer peace requests (bengreff).
  - **Kept.** Treaties are a real state between two tribes and are possible in 1v1. They're built after the core game, since early training doesn't need them.
- **"At war"** is part of how the built-in bots decide what to do, not a rule between players (game). The engine has no war state: embassies are limited only by the rules above.

Sources: <https://polytopia.io/diplomacy/>, <https://polytopia.fandom.com/wiki/Embassy>, <https://polytopia.fandom.com/wiki/Tribe_Relations>

## 16. Fog of war

- **Sight:**
  - Units see 1 tile around them (3×3).
  - Scout-skill units, and any unit on a mountain, see 2 tiles (5×5).
  - Every tile inside your cities' borders is always revealed (game).
- **Start:** each player starts with the 5×5 area around their capital explored.
- **Explored tiles stay explored.** A per-player flag goes from unexplored to explored and never back.
- **What explored tiles show:** their live contents, including enemy units, whether or not one of your units can currently see them.
  - Source: the game's code, per bengreff, who also lists the wiki's "stale last-seen view" as wrong.
- **Always hidden from opponents:**
  - units with Hide
  - unexplored tiles
  - the player's stars
  - the player's techs. They aren't shown, though an opponent can infer some from the units and buildings they see (game).
- Legal moves never act on tiles you haven't explored. For example, you can't build a road onto a fogged tile (bengreff).

Sources: <https://polytopia.fandom.com/wiki/Fog_of_War>, <https://github.com/bengreff/polytopia/blob/main/docs/design/scope-and-rules.md> (rules doc only)

## 17. Tribes

The "terrain/resource bias" numbers multiply the base chances in section 18, inside that tribe's climate region.

All values are from the data file. Modifiers left blank are 1.

| Tribe    | Start tech   | Start unit  | Stars | Terrain modifiers                   | Resource modifiers             | Starting resources (near capital)        |
| -------- | ------------ | ----------- | ----- | ----------------------------------- | ------------------------------ | ---------------------------------------- |
| Xin-xi   | Climbing     | Warrior     | 7     | mountain 1.5                        | metal 1.5                      | 2 metal                                  |
| Imperius | Organization | Warrior     | 5\*   | –                                   | fruit 2, game 0.5              | 2 fruit                                  |
| Bardur   | Hunting      | Warrior     | 5\*   | forest 0.8                          | crop 0                         | 2 game                                   |
| Oumaji   | Riding       | Rider       | 6     | forest 0.2, mountain 0.5, water 0.5 | game 0.2                       | 2 fruit                                  |
| Kickoo   | Fishing      | Warrior     | 5\*   | water 2, mountain 0.5               | fish 1.5                       | 2 fish                                   |
| Hoodrick | Archery      | Archer      | 7     | forest 1.5, mountain 0.5            | –                              | 2 game                                   |
| Luxidoor | none         | Warrior     | 2     | –                                   | –                              | 2 game, 2 fruit, 2 fish, 1 crop, 1 metal |
| Vengir   | Smithery     | Swordsman   | 5\*   | –                                   | metal 2; fruit, fish, game 0.1 | 1 metal                                  |
| Zebasi   | Farming      | Warrior     | 5\*   | forest 0.5, mountain 0.5            | fruit 0.5                      | 2 crop                                   |
| Ai-Mo    | Philosophy   | Mind Bender | 5     | mountain 1.5                        | crop 0.1                       | 2 game                                   |
| Quetzali | Strategy     | Defender    | 7     | –                                   | fruit 2, crop 0.1              | 2 fruit                                  |
| Yădakk   | Roads        | Warrior     | 7     | forest 0.5, mountain 0.5            | fruit 1.5                      | 2 fruit                                  |

- \* No `startingStars` in the data file, so the engine default applies. The 2025 Balance Pass says it's 5.
- Every tribe also starts with the hidden "basic" tech (section 5).
- **Luxidoor** has `bonus: 1` in the data file, the only tribe with a bonus. This matches its special start: a level-3 capital with no level-up rewards taken (2025 Balance Pass).
- **Changes from community sources:**
  - Bardur's forest modifier is 0.8, not 1.5, and it has no game bonus.
  - Oumaji also has water 0.5.
  - Zebasi has no crop ×2 modifier; it gets its crop from the 2 starting crops instead.
  - Quetzali has no metal modifier.
- **VERIFY** where starting resources are placed, e.g. on the tiles next to the capital (one source says Kickoo's fish go straight up, down, left and right).

Sources: <https://polytopia.io/2025-balance-pass/>, <https://github.com/bengreff/polytopia/blob/main/docs/strategy/universal-book.md>, <https://github.com/HenBOMB/Polyfish>, <https://github.com/QuasiStellar/Polytopia-Map-Generator>

## 18. Map generation

We only need maps that look like real ones (similar statistics), not exact seed-for-seed copies. The rules below come from the wiki's Map Generation page and a PolyChampions video. Both are based on an analysis of the game's map code by the PolyMod community. They're precise, but the result still gets tuned later against real maps.

**Several attempts per game:** the game generates several candidate maps and keeps the one with the fairest starting conditions (video). How fairness is scored isn't public. Community ports even out the resources near each capital. **VERIFY**; a first version can score each capital by the resources and villages it can reach early.

| Size                                      | Tiny  | Small | Normal | Large | Huge  | Massive |
| ----------------------------------------- | ----- | ----- | ------ | ----- | ----- | ------- |
| Tiles                                     | 11×11 | 14×14 | 16×16  | 18×18 | 20×20 | 30×30   |
| Ruins                                     | 4     | 5     | 7      | 9     | 11    | 23      |
| Tiny island villages (Continents, Pangea) | 0     | 1     | 2      | 3     | 4     | 9       |

- **Map size in Domination** depends on the number of opponents: 1 → Tiny, 2 → Small, 3 → Normal, 4 or more → Large. Perfection uses Normal.
- **Player limit:** Tiny maps hold at most 9 players, the other sizes at most 16.

| Map type    | Water   | Shape                                                                                                                        |
| ----------- | ------- | ---------------------------------------------------------------------------------------------------------------------------- |
| Drylands    | 0–10%   | All land, apart from 2 fish water tiles at Kickoo and Luxidoor capitals                                                      |
| Lakes       | 25–30%  | Scattered inland lakes, with land bridges always along the map edges. Every player gets a land route to at least 2 villages. |
| Pangea      | 40–60%  | One central landmass surrounded by ocean, plus occasional islands                                                            |
| Continents  | 40–70%  | Separate large landmasses, each 30–200 tiles and at least 1 tile apart; often has 1-tile shallow "rivers"                    |
| Archipelago | 60–80%  | Broken, irregular islands; almost every city is coastal                                                                      |
| Water World | 90–100% | Small islands of 1–2 villages; land is forced under every city                                                               |

**Village spacing rules:**

- **All villages:**
  - Never on water.
  - Never on the map edge.
  - At least 3 tiles apart, i.e. 2 tiles between them, so that their 3×3 borders never overlap. Border Growth can make borders overlap later.
- **Post-terrain villages** also can't be on the 2nd tile from the edge or on the **4th** tile from the edge. So counting from 1 at the edge, they can only be on the 3rd tile or the 5th and beyond.
  - This "4th tile" rule only applies to post-terrain villages, not capitals or suburbs.
  - It leaves visible bands with no villages, most of all on Drylands, where every village is post-terrain.
- **Post-terrain placement** picks a random tile among all tiles where a village is still allowed, places a village, then recomputes the allowed tiles. It repeats until no tile is allowed, so the map ends up with as many villages as fit.
- A village placed on forest turns that tile into field.

**Which steps each map type uses:**

| Step                  | Drylands | Lakes   | Archipelago          | Water World | Continents / Pangea |
| --------------------- | -------- | ------- | -------------------- | ----------- | ------------------- |
| Capitals in quadrants | yes      | yes     | yes                  | yes         | no                  |
| Suburbs               | no       | yes     | yes                  | no          | no                  |
| Pre-terrain villages  | no       | 0.3     | 0.3                  | 0.1         | no                  |
| Post-terrain villages | yes      | yes     | yes                  | yes         | yes                 |
| Tiny island villages  | no       | no      | no                   | yes         | yes                 |
| Ruins on water        | max 1/3  | max 1/3 | max 1/3 (**VERIFY**) | no limit    | max 1/3             |

The pre-terrain row gives the density coefficient.

**Shared steps:**

- **Capitals in quadrants:** the map is split into equal domains: 4 for 1–4 players, 9 for 5–9, 16 for 10–16. Each capital goes in an unoccupied domain, chosen with weighted randomness.
- **Suburbs:** two villages per capital, at least 2 tiles from it, as space allows (video). They also follow the spacing rules above. On larger maps each capital usually gets both. Because they're placed before terrain, they're usually a couple of tiles away.
- **Pre-terrain villages:** `(floor(width / 3)² − capitals − suburbs) × density`.
- **Lighthouses:** one in each of the 4 corners.

**Order per map type** (from the video's worked examples):

- **Drylands:**
    1. capitals
    2. mountains and forests (no water)
    3. post-terrain villages
    4. tribe climates filled in
    5. lighthouses
    6. resources
    7. ruins

    Kickoo and Luxidoor capitals still get 2 water tiles, each with a fish.

- **Lakes:**
    1. capitals
    2. suburbs
    3. pre-terrain villages
    4. water, placed around the capitals and villages. The map's outer ring is always land; about 25–30% of tiles end up water.
    5. post-terrain villages (often none fit)
    6. mountains and forests
    7. lighthouses
    8. resources
    9. ruins and starfish
- **Archipelago:**
    1. capitals
    2. suburbs
    3. pre-terrain villages
    4. land grown as peninsulas around the capitals and suburbs, about ¾ of the map water
    5. mountains and forests. These can cut a suburb off from land access.
    6. post-terrain villages
    7. lighthouses
    8. resources
    9. ruins and starfish
- **Water World:**
    1. every tile starts as ocean
    2. capitals, with their tile and some tiles around them forced to land
    3. pre-terrain villages (density 0.1)
    4. terrain
    5. post-terrain villages (usually none fit)
    6. lighthouses
    7. resources
    8. ruins and starfish
- **Pangea:**
    1. land grows from a seed in the centre of the map, roughly in a circle, until about half the tiles are water
    2. villages placed on the land one at a time under the post-terrain rules until none fit
    3. some villages become capitals, chosen to be as far apart as possible and preferring tiles next to water (so capitals are almost always coastal)
    4. tiny island villages
    5. terrain by tribe
    6. lighthouses
    7. resources
    8. ruins and starfish
- **Continents:**
    1. landmasses made from noise. Their number depends on player count, wetness and map size. Each is 30–200 tiles, at least 1 tile from the others, and about half the map is water; e.g. 2 players on 14×14 gives about 50 land tiles each. One-tile strips of shallow water ("rivers") are common.
    2. villages placed one at a time on each continent, at least one per continent where possible
    3. tiny island villages
    4. one village per player becomes a capital, on different landmasses where possible
    5. mountains and forests by tribe
    6. lighthouses
    7. resources
    8. ruins and starfish

**Terrain:** each tile takes the modifiers of the tribe whose climate region it's in (section 17).

- Base rates: field 48%, forest 38%, mountain 14%.
- Since 2.0.58 these rates are hit as exactly as possible within each tribe's region, rather than rolled per tile.
- **Tribe modifiers apply in order:**
    1. The mountain modifier scales mountains, and forest and field adjust proportionally.
    2. The forest modifier then scales forest, taking only from fields.
    3. Field is whatever is left.
- **Water modifiers:** per the wiki, Oumaji's water reduction has no effect on generation. Kickoo's ×2 water is meant to turn 40% of its tiles to water on Continents, but that is bugged and doesn't apply. **VERIFY** whether Kickoo's water modifier does anything on other map types.
- **Climate regions:** land is split roughly equally between the tribes in the game. **VERIFY** the method; earlier sources describe each capital growing its region outward one tile per round.

**Resources** only appear within 2 tiles of a city or village. "Inner" means next to a city or village, "outer" 2 tiles away. These are shares of all tiles of that terrain, before tribe modifiers:

| Terrain        | Resource | Inner | Outer |
| -------------- | -------- | ----- | ----- |
| Field (48%)    | Fruit    | 18%   | 6%    |
|                | Crop     | 18%   | 6%    |
|                | Empty    | 12%   | 36%   |
| Forest (38%)   | Game     | 19%   | 6%    |
|                | Empty    | 19%   | 32%   |
| Mountain (14%) | Metal    | 11%   | 3%    |
|                | Empty    | 3%    | 11%   |
| Shallow water  | Fish     | 50%   | 50%   |

- **Tribe resource modifiers** multiply these rates. Where the result exceeds the terrain's share, every such tile gets the resource (video).
  - Imperius fields next to a village: fruit 36% + crop 18% > 48%, so every one has a resource.
  - Xin-xi and Vengir mountains next to a village: metal 16.5% or 22% > 14%, so every one has metal.
  - **VERIFY** the exact method. The video also claims Quetzali fields are always full, which these numbers don't give (36% + 1.8%).
- Fish can also appear on ocean tiles inside a tribe's borders, and on some ocean tiles just outside them.
- Then each tribe's starting resources are added (section 17).

**Ruins** (count from the size table), placed last together with starfish:

- Only on mountain, forest, field or ocean (never shallow water).
- Never next to another ruin or a village.
- Ocean ruins must be at least 2 tiles from any village.
- The water limit per map type is in the table above.

**Starfish:** about 1 per 25 water tiles, on shallow water or ocean, if there's room. Never next to another starfish, a lighthouse or a city.

### Defaults until verified

The details below aren't public. The engine uses these defaults for now. They'll be settled by comparing the statistics of generated maps against real maps from the game, not by more research. Only if a gap can't be tuned away is the one game function behind it worth reading. Each default should be a setting, so it's easy to change.

| Unknown | Default |
| ------- | ------- |
| How the fairest map is picked | Generate 10 candidate maps. Score each capital by the resources within 2 tiles of it plus the villages within 4 tiles. Keep the map with the smallest gap between the best and worst capital. |
| Climate regions | Each capital grows its region outward one tile per round, all capitals at once, with ties broken at random, until every tile has a region. |
| Capping resource modifiers | Per tile, each resource's chance is (its rate ÷ the terrain's share) × the tribe modifier. If a terrain's chances add up to more than 1, scale them down to add up to 1, so no tile is empty. |
| Kickoo's water modifier | No effect, apart from the 2 fish water tiles at its capital on Drylands. |
| Who gets the meeting bonus | Only the player who sees the other tribe. The other tribe meets them when it sees one of theirs. |
| Archipelago water ruins | At most a third, like Lakes and Pangea. |
| Where starting resources go | Random tiles next to the capital, with the terrain changed to fit the resource if needed (e.g. fish needs water). |
| The `swim` skill and the movement-unlock numbers | Ignored. Movement follows section 10. |
| Where a capital goes in its domain | A random tile at least a tile in from the domain's sides and 2 from the map edge. |
| How lakes are shaped | Grown from random seeds away from other water, 4 to 12 tiles at a time along straight neighbours, until the target share is reached. Redone if a capital can't reach 2 villages over land. |
| How far suburbs are | 3 to 4 tiles from their capital, placed round-robin between capitals. |
| Fish on ocean | 50% on ocean next to a village, 25% two tiles away. |
| Ocean ruins "at least 2 tiles from any village" | At least 3 tiles away (2 tiles between). |

Sources: <https://polytopia.fandom.com/wiki/Map_Generation> (full page), <https://youtu.be/fJ562xzAIVs>, <https://github.com/HenBOMB/Polyfish>, <https://github.com/QuasiStellar/Polytopia-Map-Generator>, <https://github.com/usemsedge/PolytopiaBoardRenderer>

## 19. Win and elimination

- **Might** (first target): a player wins once the number of capitals they hold equals the number of players. In 1v1, that means capturing the enemy capital while holding your own (bengreff, from the code). There is no turn limit.
- **Domination** (later): the last tribe standing wins. There is no turn limit.
- **Elimination** (both modes): a player is out once they own no cities (bengreff), and their remaining units disappear (game).

Sources: <https://polytopia.fandom.com/wiki/Game_Modes>, <https://github.com/bengreff/polytopia/blob/main/docs/design/scope-and-rules.md>

## 20. Out of scope

- Special tribes: Aquarion, ∑∫ỹriȱŋ, Polaris, Cymanti. That rules out their units, techs, terrain (ice, flooded, algae) and skills, and also Landfill.
- Perfection and Glory modes. Score is still tracked, because the star bonus for meeting a tribe uses it.
- How the built-in AI decides what to do, and its difficulty settings. We use the Normal/human capital income.
- Graphics, sound, multiplayer networking.

## Checklist for checking in the real game

Each item, with the section it's from:

**Combat and movement**

- [x] Rounding: when a value is exactly .5, does it round up or to even? Is there a minimum damage? (§11)
  - Doesn't round. A unit can technically have <1 health
  - Resolved by the wiki Combat page: main damage rounds to whole HP (halves up), splash is halved without rounding, which is how HP gets below 1
- [x] Splash damage rounding (§11)
  - Half of the rounded main damage, not rounded again (wiki)
- [x] Do the city defence bonus and terrain bonus combine? (§11)
  - A unit can be double defended (for example, if the unit has fortify and the city has walls), but city tiles cannot have any terrain (forests or mountains) that would give a terrain bonus
- [x] Does an attacker hidden in fog get hit back? (§11)
  - No
- [x] Can units pass through friendly units? (§10)
  - Yes, zoc does not apply to friendly units (or allied units)
- [x] When a unit disembarks, does it keep its HP and veteran status? (§10)
  - Yes

**Turns, healing and skills**

- [x] Order of the start-of-turn steps (§2)
  - Order of steps is right
- [x] Is healing automatic or an explicit Recover action? (§12)
  - Healing is an ability, like any other ability. If a unit can still use an ability when the player ends their turn, healing happens automatically
- [x] Do the Heal action and upgrading a Raft use up the unit's turn? (§8, §9)
  - Healing uses up the turn, but upgrading does not
- [x] What reveals a hidden unit? (§9)
  - An opponent unit moving on to the exact tile that the hidden unit is on (however, opponents will see an eye symbol next to their unit if a hidden unit is next to it)
  - The moving unit keeps its move. There is no Hide action: units hide by moving
  - A Cloak stays hidden on an unclaimed village until it captures it, then is visible until it moves. It can pass through enemy cities but not end on one. Villages can't be infiltrated
  - The move that hides a visible Cloak ends its attack for the turn (no Dash)
- [x] Explorer terrain techs (§13)
  - The same as units: Climbing, Fishing, Sailing
- [x] Stomp damage amount (§9)
  - Same as splash damage

**Units**

- [x] Cloak and Dinghy attack value (§8)
  - Data file: both 2. Raft defence 1 and Swordsman without Fortify are confirmed too.
- [x] Boat HP: the passenger's, or the data file's 10? (§8)
  - The passenger's, including more than 10
- [ ] What the `swim` skill does (§8; low priority)
  - Cloak can't be promoted: it's Static (wiki)
- [x] Is a Pirate Independent? (§8)
  - Yes

**Economy and cities**

- [x] Capital income bonus; city unit cap (level + 1?) (§3, §7)
  - bengreff: capital +1 for humans (code), cap = level + 1 (measured)
- [x] Market cost and cap (6 or 8); one per city for Windmill, Sawmill, Forge and Market (§6)
  - Data file: cost 5, max level 8, all four marked `limited`
- [x] Port connection range: 4 or 5 water tiles? (§7)
  - At most 5 tiles away, i.e. 4 water tiles between
- [ ] What the movement-unlock numbers mean (§5; low priority)
- [x] Literacy rounding; "backwards research"; tech parent links; whether ocean comes from Sailing or Navigation (§5)
  - Literacy rounds up
  - Backwards research does work (owning the t3 tech allows you to buy the t2 tech)
  - bengreff: parent links match the data file; ocean comes from Sailing
- [x] Star bonus for meeting a tribe; income effect of negative population; embassy tech discount (§2, §3, §5)
  - Wiki Star and Population pages; there is no embassy discount
- [x] Score formula, used by the meeting bonus (§3)
  - Wiki Score page
- [x] Which city a found lighthouse gives +1 population to (§14)
  - The capital
- [x] Hidden resources before their tech (§4)
  - Hidden resources are only shown if the tech that unlocks them is buyable (for example, crops only appear when farming is buyable)

**Ruins and monuments**

- [x] Ruins: does exploring use an action, and are the rewards uniform? (§13)
  - Yes, exploring uses an action. Rewards are uniform, but some have conditions for activation. See the wiki for details <https://polytopia.fandom.com/wiki/Ruin>
- [x] Monument placement rules and task triggers (§14)
  - Wiki Monuments page

**Diplomacy and fog**

- [x] Whether building an embassy needs a unit; what counts as "at war" (§15)
  - No unit needed; "war" only matters to the built-in bots
  - No embassy levels (game; wiki)
- [x] How far cities see; whether enemy techs are visible (§16)
  - Everything inside city borders is revealed; enemy techs are hidden
  - Live contents of explored tiles: settled by bengreff (code)

**Tribes, map and endgame**

- [ ] Where starting resources are placed (§17; low priority)
  - Tribe modifiers and starting resources: settled by the data file
  - Default: random tiles next to the capital (section 18)
- [x] Map-generation terrain percentages (§18)
  - Wiki Map Generation page
- [ ] Map generation: climate-region method, how the fairest map is picked, how resource modifiers are capped, Kickoo's water modifier (§18)
  - Defaults are in §18 "Defaults until verified". These get settled by comparing against real maps.
- [ ] Map generation: the limit on water ruins for Archipelago (§18)
- [x] Giant push directions when a Giant spawns on an occupied city (§7)
  - Correct as written
  - Post-terrain edge rule settled by the PolyChampions video
- [x] What happens to an eliminated player's units (§19)
  - They disappear
