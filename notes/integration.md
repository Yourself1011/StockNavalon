# Integration with the real game

A plan, not built. **The first goal is checking our engine against the real game:** are the rules sound and complete, and is the map generator believable. Matches against the game's built-in Crazy bot come after. Online play, human replays and a live advisor are out of scope (the user's decisions, 2026-11-19). The rules spec is [`src/engine/GAME.md`](../src/engine/GAME.md); how the engine implements it, [`src/engine/ENGINE.md`](../src/engine/ENGINE.md).

**The approach:** load the game's own rules library from the Steam install and drive it headlessly as a reference (an *oracle*). Play games on both engines side by side, with our bot choosing the moves, and compare after every command. Compare real generated maps with ours by their statistics.

## 1. What bengreff/polytopia did

[bengreff/polytopia](https://github.com/bengreff/polytopia) runs **the game's own compiled rules** instead of a replica. The parts that matter here:

- **Hosting the game's code (`polyhost/`).** The rules live in `GameLogicAssembly`, shipped as il2cpp native code (`GameAssembly.dylib` on the Mac's Steam build). polyhost `dlopen`s it, starts the il2cpp runtime without Unity, and calls the game's own functions through the exported `il2cpp_*` API:
  - `ActionManager.ExecuteCommand` applies a command;
  - `CommandUtils` and `UnitDataExtensions.GetMovementOptions`/`GetAttackOptions` list legal moves, each checked with the command's `IsValid`;
  - `MapGenerator` makes maps;
  - `AI.GetMove` is the built-in bot;
  - `GameState.Serialize`/`Deserialize` save and load;
  - reflection over the il2cpp classes dumps any state as JSON (`dump.hpp`).
- **Building games the way the app does.** They rebuilt `ClientBase.CreateSession`'s pass-and-play branch step by step, and checked with Frida traces of the real app that the result is byte-identical (8,244 bytes). Single-player maps were byte-identical for 5 recorded games, and the bot's moves for 212 of 212.
- **Lessons from their checks:**
  - `IsValid` alone is laxer than the client: it accepts captures, recovers and abilities that the game's own `Can*` checks reject. So the generators are the authority on what's legal, not an `IsValid` sweep.
  - The AI's `StayCommand` is never offered to a person.
  - `EndMatch` is sent automatically once the game reaches its final turn.
  - One process-wide random generator is shared by `GetRandomPickableTribe` and `utilityRng`, so each game needs its own seed.
  - State diffs need a short allowlist of fields that only look different (`birthClimateSkinType`, `VillageNameSeed`).
- **The built-in AI** has +3★ a turn on Crazy (handicap 4 against a person's 1), sees everything, and makes deliberate mistakes on lower difficulties (`StupidFactor`). Their network wins 94–98% against Crazy, greedy, no search.

**Their code has no license file**, so it's all rights reserved: read it for ideas, write our own.

## 2. The install

- `~/Library/Application Support/Steam/steamapps/common/The Battle of Polytopia/Polytopia.app`, **version 2.18.1 build 16768** (checked 2026-11-19).
- `Contents/Frameworks/GameAssembly.dylib`: a universal binary (arm64 and x86_64) exporting 241 `il2cpp_*` functions, so it loads natively on this Mac with no emulator.
- `Contents/Resources/Data/il2cpp_data/Metadata/global-metadata.dat`: the class and method metadata the runtime needs, and that Cpp2IL or Il2CppDumper turn into a class layout.
- **Signed ad hoc only**, without the hardened runtime, so loading the library into our own process is allowed.
- **Steam updates it on its own.** GAME.md's data file is from 2.18.0 build 16751, so it's changed once already. The oracle works from a **pinned copy** in the gitignored `game/` (`src/oracle/setup.sh`, [`docs/oracle.md`](../docs/oracle.md)). After an update, diff the new data file against the pinned one before moving to it: a balance change is a GAME.md change, and gets a `RuleVersions` number like any rules fix.

## 3. The oracle

Built (`src/oracle/`, [`docs/oracle.md`](../docs/oracle.md)) up to `new`, `state` and `map`: a small C++ program of our own, separate from the engine, that loads the pinned library and serves it over stdin/stdout (one request a line, one JSON answer a line). It runs in its own process, so a crash in the game's code (or the runtime's global state) never reaches ours, and the engine never links against Midjiwan's code. The Python side drives it.

| Request | Answer |
| --- | --- |
| `new` (map type, size, tribes, seed, settings) | a game built as the app builds a pass-and-play Might game; returns its id |
| `map` (game) | the generated map: terrain, resources, villages, ruins, capitals, climate per tile |
| `legal` (game, player) | the legal commands from the game's own generators (not `IsValid` sweeps) |
| `apply` (game, command) | applies it with `ActionManager.ExecuteCommand`; returns the new state |
| `state` (game) | the state projection (section 4) as JSON, by reflection |
| `ai` (game) | the built-in AI's command, for later (section 7) |

- Built from the class layout dumped with Cpp2IL or Il2CppDumper, plus Ghidra for any method whose behaviour needs reading (game creation especially).
- **Is the oracle the real game?** Our game construction could differ from the app's in some setting. Two cheap checks: settings read from a pass-and-play game started in the app (its save, or a Frida snapshot like bengreff's) match ours; and a short game played in the app with the same seed gives the same map. bengreff showed this can be made exact; we need it only once per version.

## 4. Rules: games on both engines

**The mirror:** a polyengine `Game` kept in step with the oracle's game, command by command.

```
oracle (real rules)                          polyengine (ours)
  new ── map ──► translate to MapData ──────► Game
  legal ◄──────────────────────────────────── compare with LegalMoves
  apply ◄── translate ◄── chosen Command ◄─── bot or coverage policy picks
  state ─────────────────────────────────────► compare with our projection
```

- **Start:** the oracle generates the map, which becomes a `MapData` (`src/engine/map.h`): terrain, resources, villages, ruins, lighthouses, capitals in seat order, climate regions. Both games then start from the same position.
- **Commands:** our command set is one-to-one with the real `CommandType`s (GAME.md section 2, "Actions"), so translation is one table: type, tile, argument. Tiles map through one coordinate transform, worked out once (origin, y up or down, axis order).
- **Who picks the moves:** the user's suggestion, our bot, played on the mirror through `makeView` as in training, so the games look like the ones the engine actually has to get right. Its move goes to both engines. Bot games alone miss rules it rarely or never uses (Destroy Embassy, Disband, most diplomacy, rare unit skills, late techs), so a share of commands come from a **coverage policy**: a random legal command, weighted towards the command types, units and skills least exercised so far.
- **What's compared, after every command:**
  - **legal lists**, as sets after translation. A command the real game allows and we don't is a missing or wrong rule; one we allow and it doesn't is a rule we've made up. This is the completeness check, and it catches errors before they're played.
  - **the state projection**: the parts our engine models. Units (type, owner, tile, health, veteran, moved and attacked this turn), cities (owner, level, population, buildings, rewards, walls), tiles (terrain, resource, improvement, owner, explored per player), and each player's stars, income, techs, score, tasks and diplomacy. Everything else (skins, names, AI memory) is left out.
- **Randomness:** combat is deterministic, but a few outcomes aren't, and the mirror must take them from the oracle. In our engine they're the `Game::randomInt` calls: the ruin reward and its tech (`ruins.cc`), a converted unit's home city (`combat.cc`), the explorer's path (`fog.cc`). The engine gets a queue of forced draws that `randomInt` takes from before its own generator; the bridge fills it by matching the real outcome with the engine's options, which the engine exposes rather than the bridge restating. In training the queue is always empty. If the real game is random somewhere ours isn't (or the other way round), it shows up as a mismatch.
- **On a mismatch:** stop that game, and save both states before and after, the command and the legal lists as a fixture. Each one is a translation bug or a rules bug. A rules fix follows ENGINE.md's usual path: GAME.md updated and marked **(game)**, a new `RuleVersions` number, and a test made from the fixture. GAME.md's **VERIFY** items should all resolve this way.
- **Settings:** the real 1v1 Might format first (Lakes, 14×14), then every map type and size, more players, peace treaties on, and Domination, since the engine keeps all of these as settings.
- **The report**, per batch of games: commands compared, mismatches by kind, and a coverage table (each command type, unit, skill, improvement, tech and city reward: how many times exercised). A rule never exercised hasn't been checked, however many games pass.

## 5. Maps: is the generator believable

This needs only the oracle's `new` and `map`, so it can come before the rules work.
- **Samples:** a few thousand real maps for each map type and size (14×14 Lakes first), with random tribes, and as many of ours with the same settings.
- **Statistics compared**, each as a distribution over maps, real against ours:
  - terrain shares (field, forest, mountain, shallow water, ocean) overall and by climate;
  - water's shape: the number and sizes of lakes and landmasses, coastline length, whether the capitals are connected by land;
  - villages, ruins and lighthouses: counts, and distances to the nearest capital;
  - resources by ring around each capital (fruit, game, fish, crops, metal, starfish within 1, 2, 3 tiles), and how even the two capitals are (GAME.md section 18's open question about how fairness is scored);
  - the distance between capitals;
  - per tribe, since tribes bias their own climate's terrain and resources.
- **The test:** two-sample tests per statistic (Kolmogorov–Smirnov, or a χ² on counts), reported with the gap in plain units ("ours has 1.3 fewer forests per map"). A stronger check of "believable": a small classifier trained to tell real maps from ours. Near 50% accuracy means indistinguishable; the features it relies on say what to fix.
- **By eye:** real and generated maps side by side in the CLI's emoji view, a dozen of each.
- **The goal is the distribution, not the exact maps.** Reproducing the real generator tile for tile would mean copying its code, and the bot gains nothing from it. Maps that are believable in every statistic above are enough.

### First comparison (2026-11-19)

`src/oracle/mapstats.py` (docs/oracle.md): 1,000 Lakes 14×14 maps from each generator, 500 of each other type, random tribe pairs.

**The real generator's structure** (from its method names and what each calls, `MapGenerator.GenerateInternal`), next to ours:

| Step | Real | Ours |
| --- | --- | --- |
| Which path | Continents and Pangea: `TryGenerateMapFromIslands` (noise, smoothed; continents grown from it; cities per continent; capitals put on continents), falling back to the other path if that fails. Everything else: `GenerateMapFromNoise` | A separate shape per type; Pangea grown from the centre, Continents from far-apart seeds |
| Noise path | capitals in domains, `ReassignCapitalsBasedOnPlacementSettingsNoise`, suburbs, pre-terrain cities, then **per-tile random noise** (the capitals' neighbours forced to land) smoothed `smoothIterations` times and cut into land and water by `wetness` (`SetTerrainFromNoise`), post-terrain cities, climates | the same order, but water **grown as lakes** from seeds (Lakes), fingers (Archipelago) or shapes (Water World) |
| Settings per type | `MapGeneratorSettings` (the oracle's `mapsettings`): wetness Dryland 0, Lakes 0.3, Continents 0.5, Archipelago 0.65, Water World 0.85, Pangea 0.5; smoothing 0–2; pre-terrain city density 0.3 (Water World 0.1, Dryland 0); 1–2 suburbs (none on Dryland, Water World) | `MapGenSettings` per type, from the wiki |
| Then | climates (as `WorldContinent`s, one per player, with the tribe's modifiers), terrain per climate region, `MakeOcean`, lighthouses, resources, starting resources, a flood near some capitals (a tribe ability), ruins, starfish | climates, terrain, lighthouses, resources, starting resources, ruins, starfish |
| Fairness | up to `equalityIterations` = 50 candidates, `MapFairnessCalculations.GetInequality` (territories, `CreateTurfs`, scored by `CountTurfScore`), `equalityLimit` 1.01 | 10 candidates, the gap in resources within 2 and villages within 4 |

So the order and the per-type steps agree (the community's description came from the same code); the shapes of water and land are made differently, and so is fairness.

**What differs, on every map type:**
- **Too few villages:** Lakes 8.8 against 10.2, Drylands 8.2 against 11.0 (no water there, so it's the placement rules: post-terrain spacing or the edge bands), Archipelago 7.1 against 9.7, Continents 6.7 against 9.0. Fewer villages also means fewer land resources (metal, fruit, game).
- **Capitals too close:** 6.1–7.5 tiles apart against 7.3–8.6 (Chebyshev), except Pangea (equal).
- **Climate regions too uneven:** the larger region holds 59% of the land against 51–54% (Drylands, Lakes, Archipelago); real regions split land more evenly.
- **AquaCrop**, Aquarion's water crop, is on every real map with water (7 on Lakes, 17 on Archipelago), whatever the tribes; we have no such resource. Fish are correspondingly fewer (Lakes 13.8 against our 23.9). For regular tribes it's a water tile that can't hold fish. What it blocks (ports? fishing?) is for phase 3.
- **Shallow water and ocean:** total water is right on Lakes (27% each), but real Lakes have 6.8% ocean against our 0.4%: real lakes are fewer and larger (largest 39 tiles against 26), deep enough for ocean in the middle. Our lakes crowd the capitals: water within 2 tiles of a capital 8.6 against 4.5, coastal capitals 90% against 62%.
- **Too much water** on Continents (48% against 39%) and Water World (91% against 85%), with too many landmasses: Continents 5.6 against 3.1, Pangea 5.5 against exactly 2 (always one continent and one islet, it seems), Water World 7.8 against 3.2.
- **Ruins on water:** Lakes 0.19 against our 0.01; Archipelago 0.31 against 1.0.
- **Fair:** resources within 1 and 2 tiles of a capital are about right (Lakes 5.7 and 12.1 against 6.1 and 12.4), but the real gap between the two capitals is larger (3.3 against 1.4 within 2): the real fairness score isn't ours.
- Right already: ruins and lighthouse counts, the land edge ring on Lakes, Drylands' terrain shares within 1–2 points.

**Measured exactly** (1,000 real Lakes maps, 300 of each other type):
- **Ocean** is water with no land among its 4 straight neighbours: all 52,531 water tiles fit. Ours makes ocean only 2 or more tiles from land.
- **Lighthouses keep their corner's terrain**: on Pangea always ocean, Water World 97% water, Archipelago and Continents about half water. Ours always puts field under them.
- **No village bands at the edge**: on Drylands (every village post-terrain) 48% of real villages are on the 2nd tile from the edge and 11% on the 4th, which GAME.md (from the wiki) rules out. Only the edge ring itself is never used.
- **Suburbs are 3 tiles from their capital**: half of real Lakes villages have their nearest capital exactly 3 away, 5% at 4 (ours 40% and 26%).
- **Capitals are 2–3 tiles from the edge** (94%; ours spread over 2–5), and on Lakes 38% have no water next to them (ours 10%).

**The plan, in order** (each step checked with `mapstats` on all six types, then GAME.md section 18 and ENGINE.md updated):
1. **Exact rules:** ocean by straight neighbours; lighthouses keep the corner's terrain; villages only kept off the edge ring; suburbs 3 tiles out, 1–2 per capital; capitals 2–3 from the edge.
2. **Water from noise** on the noise path (Lakes, Archipelago, Water World): per-tile random values, smoothed, cut at the type's water share, with the capitals' neighbours forced to land, in place of grown lakes, fingers and shapes. The real settings (wetness, smoothing passes) as the start, tuned to ocean share, lake sizes, water near capitals and coastal capitals.
3. **Even climate regions:** the smallest region claims next (real larger region 51–54% of the land, ours 59%).
4. **Continents and Pangea** on the same noise: real Pangea always 2 landmasses, Continents about 3 (ours 5.5 each), Continents 39% water (ours 48%).

**Step 1 done** (2026-11-20): all five, plus Continents and Pangea capitals never under 2 from the edge (a candidate without such a village is dropped). Results (500 maps each, real against ours):
- villages: Lakes 10.19 / 10.25, Drylands 10.92 / 10.90 (were 8.8 and 8.2); capital distance Lakes 7.26 / 7.26, Drylands 7.47 / 7.28 (were 6.3, 6.4); capitals' distance to the edge 2.6 / 2.6 on every noise-path type.
- **Landmasses were mostly the lighthouses:** field under every corner made up to four 1-tile islands on water maps. Now Pangea 2.00 / 1.99 (was 5.5), Continents 3.06 / 3.00 (was 5.6), Water World 3.3 / 3.9 (was 7.8). Archipelago is now clearly short of islands (8.9 / 3.0).
- Lakes ocean 7.0% / 2.4% (was 0.4%): the rest is lake shape, step 2.
- Continents capitals are now too close (8.7 / 6.6) and too far in (2.15 / 2.99 from the edge: real ones are almost always exactly 2 from it): step 4.
- Tests: the map test checks the new ocean rule; the tribe-inference test that needs a particular Lakes start moved to seed 1; the bridge-rush test only expects the bridge tech in games whose capitals start apart (real Continents capitals share a landmass 30–40% of the time, ours 0–12%); the tree-reuse test's threshold is 0.95 (it was at 0.898 against 0.9 before, and the new maps alone move it between 0.86 and 0.93).

**Step 2 done** (2026-11-20): Lakes, Archipelago and Water World water from noise (GAME.md section 18). What fixed it, from the real generator's code (behaviour only): `GenerateNoise` lifts a tile to `wetness + (1 − wetness) × random` rather than pinning it, high meaning land; `SmoothNoise` averages each 3×3 with the outside of the map at `surroundingSpaceValue`. Which tiles are lifted was fitted, since the variants that follow the code literally (forced tiles kept unsmoothed after each pass) came out far too wet next to capitals under our ranking cut: real Lakes lift a capital's 8 neighbours and the early villages' 4, Archipelago only a capital's 4, Water World as Lakes. Results (1,000 maps each, real / ours):
- **Lakes:** ocean 7.1% / 7.3% (was 2.4%), largest lake 39.7 / 40.0 (was 26), coastal capitals 57% / 63% (was 90%), landmasses 1.26 / 1.30; water next to a capital (none, 1, 2, 3+) 40/15/24/21% / 40/13/24/23%, and next to a village the same to within 1 point. Left: water within 2 tiles of capitals 4.3 / 5.7, and fewer 1–2-tile ponds (1.26 / 0.94).
- **Archipelago:** landmasses 9.1 / 9.2 (was 3.0), villages 9.6 / 9.7 (was 6.7), coastal capitals 99% / 99%. Left: shallow 48.6% / 50.7% and ocean 14.2% / 12.0%, water within 2 of capitals 12.3 / 13.9.
- **Water World:** water 85% / 85%, landmasses 3.3 / 3.9 (7.8 before step 1), villages 2.6 / 3.0. Left: coastal capitals 83% / 60%; neither lift shape fits (8 neighbours too dry, 4 too wet).
- The tree-reuse test now plays 16 games: over 16 games reuse saves 10–15% of evaluations on every seed tried; 4 games swung between 2% and 14% with the maps.

**Step 3 done** (2026-11-20): climate regions take turns claiming one random unclaimed tile beside them (8 neighbours), so they come out equal in size with irregular borders. The larger of two regions' share of the land, real / ours: Drylands 50.7% / 51.0% (was 56.7%), Lakes 53.2% / 54.1% (57.2%), Continents 56.1% / 56.8%, Archipelago 53.4% / 53.7%, Water World 59.2% / 61.0%, Pangea 54.3% / 53.9%. The share of tiles nearer the other capital than their own matches too (about 7%, was 2%).
- Noticed on the way: on every type our land has about 1 point too few mountains (12.4% / 11.4% of land) and 1 point too little forest (31% / 30%). A terrain-rates tweak, after step 4, **compared tribe by tribe** (each tribe has its own terrain and resource modifiers, so the pooled numbers can hide gaps that cancel out): `mapstats` needs a per-tribe breakdown first (terrain and resources within each climate region, by its tribe).

**Step 4 done** (2026-11-20). Continents: landmasses grown from noise with the outside of the map as land (one pass), one per player or one fewer or more, each taking its best noise plus a compactness pull (0.05 per neighbour already its own), never touching another. Pangea keeps its shape, with exactly half the map water. Both: villages fill the ring 2 in first and avoid a coordinate 3 from the edge; capitals are villages on that ring, as far apart as possible. Results (500 maps, real / ours):
- **Continents:** landmasses of 3+ tiles 1 / 2 / 3 on 19 / 53 / 28% of maps against 20 / 54 / 26%; capital distance 8.6 / 8.8 (was 6.6); coastal capitals 69% / 66% (95%); water 38.6% / 38.4% (49%); coastal land 44% / 46% (78% without the compactness pull); villages 9.1 / 9.3; capitals sharing a landmass 28% / 39%; on 11×11, same landmass or bridgeable 90% / 97%. Left: ocean 19.3% / 17.6%, and few inland ponds (1–2-tile water bodies 1.1 / 0.25).
- **Pangea:** water 49.5% / 49.4% (spread 45–54% before), capital distance 8.8 / 9.0 (8.2), villages 8.4 / 8.8. Left: shallow 21.3% / 19.6% and ocean 28.2% / 29.9%, coastal capitals 92% / 97%.
- Tests: the map test checks capitals exactly 2 from the edge and the coordinate-3 rule instead of "capitals on different landmasses" (28% of real maps share one); the bridge rush's join count is 5 of 8 (it joins in 25 of 32 games on the new maps, so 8 games give 5 to 7).

**Terrain and resources by tribe done** (2026-11-20). `mapstats --by-tribe` measures, inside each tribe's region, terrain shares of the land and resource rates by terrain, next to a settlement and two away, plus starting resources next to the capital (3,000 Drylands and Lakes maps). Three rules came out of it, each fitting all 12 tribes (GAME.md section 18):
- **Terrain:** weights field 48, forest 38 × modifier, mountain 14 × modifier, normalised. The wiki's order (mountain first, forest taking only from fields) was off by up to 11 points (Hoodrick forest 62% against 51%).
- **Resources:** each resource tries on its own (fruit and crop 0.5, game 0.5, metal 0.8, fish 0.52, AquaCrop 0.3) times the modifier, and one of the successes is picked; two away, the same draw 32% of the time. Quetzali fields were 75% fruit against 97.5%; fish two from a settlement 49% against 13%.
- **Starting resources** top up the capital's neighbours to the tribe's list instead of adding to them (Hoodrick 4.4 game against 2.5), on random neighbours.
- Left: AquaCrop itself (its attempt leaves the tile empty for now); Oumaji's field share 1.2 points high; Vengir's region on Lakes 1.9 points drier; starting-resource counts within 0.2 a capital. Pooled, every resource count is now within noise on every map type except metal on Archipelago and Pangea (0.5–0.75 low). Also seen: **starfish** too few on Lakes (2.0 / 2.85) and Continents (3.0 / 3.9), our 1 per 25 water tiles being off, and the **fairness** gap (real capitals differ by 3.0–3.3 resources within 2, ours by 0.9–1.3).
**Starfish and AquaCrop done** (2026-11-20), for the door to special tribes:
- **AquaCrop** is a resource (`ResourceTypes::AQUACROP`, last), placed like the others (tries at 0.3 on water and ocean, times the tribe's modifier, so an Aquarion modifier will work when it's added) and kept on the tile. Visible to everyone and unused, since its tech and the Aqua Farm aren't in the engine. Counts match real maps (Lakes 7.2 / 7.4, Archipelago 16.5 / 15.8). Encoding version 16: its own input plane, last, and an 8th resource in the `unexplored` head; `load_weights` drops a saved head whose shape changed, so older networks still load (tested). Open for phase 3: whether regular tribes see it, and whether it blocks a Port or Bridge.
- **Starfish:** the count is the type's wetness × tiles ÷ 25, rounded up (read from `GenerateInternal`: 3 on 14×14 Lakes, 7 on Water World), placed by random tries over the water tiles. Starfish can be next to corners (13% of Pangea's are; GAME.md had them banned), and one on a water corner is lost to the lighthouse. Real / ours: Archipelago 5.72 / 5.68, Water World 6.83 / 6.77, Continents 3.83 / 3.92, Lakes 2.85 / 3.00, Pangea 3.90 / 3.77 (were 2.0 to 7.0, off by up to 0.9).
- Tests: plane counts 105 → 106; end-relative plane offsets; a test of loading a pre-AquaCrop network; the target test skips a dead opponent (masks are off then, and a new random game killed one); the tribe-inference test now searches for a map with the start it needs instead of a fixed seed.
**Fairness, read** (2026-11-20; GAME.md section 18 has the rule). The oracle's new `fairness` and `turfs` requests call the game's `GetInequality` and `CreateTurfs`, which settled the details by comparison: a Python emulation (scratchpad) matches the game's score exactly on 99% of Drylands maps and typically to 0.0003 on water maps.
- Real maps are very fair by this measure: median 1.006–1.009, at most about 1.10 (Lakes, Drylands, Continents). Ours by the same measure: median 1.29–1.50, up to 6.5–8.0. Our own score (resources within 2, villages within 4) is a different thing: real capitals differ by about 3 resources within 2, ours by about 1.
- **Done** (2026-11-20): `startInequality` (mapgen.h) is the real score, and the generator keeps the fairest of up to 50 candidates, stopping at 1.01. The last detail came from the tie-break: a tile two capitals reach at the same cost goes to the first seat (`GameState.IndexOfPlayerId`); with it the engine's score equals the game's on all 1,200 maps checked (200 of each type). Ours now: median 1.007–1.012, worst 1.05–1.09 (real 1.006–1.009, about 1.10), and the resource gap between capitals within 2 tiles matches (Lakes 3.29 / real 3.11, was 1.2). A map takes 2–3 ms (was about 0.5).
**Other map sizes** (2026-11-20): `mapstats --size N --players P` at 11, 16, 18, 20 and 30, 2 to 8 players. Fixed on the way, all at 11×11 (the current training size) or at large sizes, where 14×14 hadn't shown them:
- **Capital domains are centred** with the leftover tiles at the edges, the odd one first: 11×11 capitals are always on 3 or 8, ours were on 2, 7 or 8 (5.6 apart against always 5). The weighting inside a domain is (half − distance from offset half)², which fits every size from 11 to 30 and 2 to 12 players (largest gap between the real and our coordinate histograms 0.10, was 1.0 at 11×11 and 16×16 with 9 players).
- **Suburb and pre-terrain counts** come from the generator's own formula, t = int(0.33 × width), room = t² − capitals: suburbs room ÷ players clamped to the type's 1–2 (so 2), pre-terrain int(density × what's left). Lakes and Archipelago village counts now match at every size; 11×11 Water World has no pre-terrain village (real 0.11 villages a map, ours were 1.06).
- **Tiny islands:** 1 on 11×11 Continents and Pangea (the wiki's table had 0; real 0.8–0.95), and none as a step on Water World (real single-tile villages there come from ordinary placement; ours had up to 11.8 a 30×30 map).
- **Capitals on Continents and Pangea** prefer the ring 2 in, but fall back to villages further in: 30×30 Pangea's land rarely reaches the ring (26% of real capitals on it). Our generator threw on 30×30 Pangea before.
- Tests: map test bounds (Continents water from 30%, capitals at least 2 in); the naval plan's port count is 5 of 8 (24–25 of 32 games, so 8 give 5 to 7).
- **After those fixes, the full grid** (6 types × sizes 11, 16, 18, 20, 30 × 2–8 players, 150–300 maps each; flagged when KS p < 0.001):
  - **Drylands:** nothing flagged at any size or player count.
  - **Lakes:** close everywhere; 11×11 ocean 5.7% / real 3.3% and occasional islets real maps don't have; 30×30 fewer islets (landmasses 1.8 / 2.4).
  - **Archipelago:** water 1–3 points high, ocean about 2 low, the edge ring a little more land; at 30×30 fewer islands (31 / 37); with 4–8 players less fair than real (1.20–1.57 / 1.12–1.37).
  - **Water World:** at 16–30, about 1–2 villages short and correspondingly less AquaCrop (20×20: 14 / 17).
  - **Pangea:** capitals always on the ring 2 in, while real ones move inland as maps grow and players increase (average 2.9–3.8 from the edge at 18–30); slightly too many villages up to 20×20, too few at 30×30.
  - **Continents** drifts most with size: the edge ring too much land (73–83% / 58–67%), too little ocean (30×30: 20.5% / 30.8% with 2 players, 12.5% / 28% with 8), capitals always on the ring (real 2.5–3.5 from the edge with 4–8 players), too many villages at 30×30 (52 / 42 with 8 players).
  - The real generator itself fails on some large multi-player maps (an exception "Crash occured IN FINAL STEP to generate a map" on 18×18 Pangea with 4 players, or a segfault); `mapstats` now runs the oracle in batches and leaves those games out.
  - Next, if large maps matter: Continents' shape and both continent types' capital placement are the fitted parts of the generator (from statistics, not read from the code), and they're what drifts. Reading `TryGenerateMapFromIslands` and `AddContinents` would replace the fitting.
- The oracle's general dumper segfaults on a `Dictionary<int, List<TurfTile>>`; `turfs` goes round it through the dictionary's own methods. Worth fixing before phase 3 needs dictionaries.

## 6. Engine changes this needs

All general, none specific to the real game:
- **Forced random draws** (section 4).
- **`MapData` covering every feature** of a real start: check lighthouses, starting units, and anything else a real start has that `MapData` can't say.
- **A state projection** exposed through the Python bindings (one call returning plain arrays), so the bridge compares without restating the engine's state layout.
- **Legal lists through the bindings,** already there (`LegalMoves`).

## 7. Later: matches against Crazy

Once the rules check passes. The oracle's `ai` request plays the built-in AI's seats headlessly (no client, no Frida), and our bot plays its seat through the mirror. This gives a benchmark outside our own engine: the win rate against Crazy, by tribe pair and map type, seats swapped, with Wilson intervals, comparable with bengreff's 94–98%. A few games in the real app with the same seeds confirm the headless matches play the same way. Crazy sees everything and gets +3★ a turn; our bot stays fog-correct.

## 8. Phases

| Phase | What | Done when |
| --- | --- | --- |
| 0 | Pin the build (dylib, metadata, data file); dump the class layout; diff the 2.18.1 data file against GAME.md's 2.18.0 numbers | We know which classes, fields and methods hold the state, commands, map generator and AI. **Done** except the data diff |
| 1 | The oracle: runtime up, data loaded, `new` and `map` | A real Lakes 14×14 map as JSON. **Done** (not from a seed: see below) |
| 2 | Map comparison (section 5) | The report for Lakes 14×14; the gaps listed in GAME.md section 18 and the generator fixed |
| 3 | `legal`, `apply`, `state`; the translation table and coordinate transform; forced draws; the mirror | One game played to its end on both engines with every command compared |
| 4 | Games at scale with the bot and the coverage policy; the report; fixtures into tests | Thousands of championship-format games with no mismatches, every rule in the coverage table exercised; then the other settings |
| 5 | Matches against Crazy (section 7) | A match result in [`runs.md`](runs.md) |

Phases 2 and 3 are independent once 1 is done.

**Found in phases 0 and 1** (2026-11-19):
- The pinned build is 2.18.1 build 16768: game version 124, rules data still version 28 (as GAME.md's 2.18.0 extraction). That one was in the scratchpad and is gone, so the data diff means checking GAME.md's **(data file)** numbers against the new file rather than diffing two files.
- **Real maps can't be made from a seed.** `MapGenerator.Generate` makes several candidates, each from a seed from a cryptographic random generator, and keeps the one `MapFairnessCalculations.GetInequality` scores fairest; the loop reads `MapGeneratorSettings.equalityIterations` and `equalityLimit` (how exactly is still to read). Maps are compared as distributions anyway (section 5), and one worth keeping is kept as its JSON. Phase 3 doesn't need seeds either: the mirror imports the map. This is also the answer to GAME.md section 18's open question on how fairness is chosen: `MapFairnessCalculations` is the code to read.
- The real generator works in **continents** (`WorldContinent`, `AddContinents`, `AddCitiesToContintents`) and places pre-terrain cities, suburbs and post-terrain cities (`MapGeneratorSettings`' densities and suburb counts), then lighthouses, ruins, resources and starfish. Worth reading next to GAME.md section 18 in phase 2.
- `CreateLocalGame` gives the own player handicap 1 and Crazy handicap 4 (5 stars each to start), as bengreff reported.

## 9. Risks

- **Game construction is the hard part.** bengreff had to rebuild the client's session setup step by step to match the app. If ours differs (settings, handicaps, starting stars), every game is checked against the wrong reference. Section 3's check is the guard.
- **Updates move field offsets and method names.** The oracle pins one version and refuses data from another rather than misreading it.
- **The il2cpp runtime keeps global state.** bengreff saw failures in long-lived processes that a fresh one didn't reproduce. Restart the oracle every so many games, and replay a failing game in a fresh process before calling it a mismatch.
- **Coverage is the real limit of "complete":** a rule nothing ever triggers passes silently. The coverage table makes that visible; rules that stay at zero get a hand-built position or a scripted sequence of commands.
- **The bot's games are a narrow sample.** It plays one way; the coverage policy widens it, and the other settings (section 4) widen it further.
