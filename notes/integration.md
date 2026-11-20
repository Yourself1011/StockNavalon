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
- **Steam updates it on its own.** GAME.md's data file is from 2.18.0 build 16751, so it's changed once already. The oracle works from a **pinned copy** (the dylib, the metadata and the extracted `GameLogicData`, in a gitignored directory or the scratchpad, never committed) with its version recorded. After an update, diff the new data file against the pinned one before moving to it: a balance change is a GAME.md change, and gets a `RuleVersions` number like any rules fix.

## 3. The oracle

A small C++ program of our own, separate from the engine, that loads the pinned library and serves it over stdin/stdout (one JSON message a line). It runs in its own process, so a crash in the game's code (or the runtime's global state) never reaches ours, and the engine never links against Midjiwan's code. The Python side drives it.

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
| 0 | Pin the build (dylib, metadata, data file); dump the class layout; diff the 2.18.1 data file against GAME.md's 2.18.0 numbers | We know which classes, fields and methods hold the state, commands, map generator and AI |
| 1 | The oracle: runtime up, data loaded, `new` and `map` | A real Lakes 14×14 map as JSON, from a seed |
| 2 | Map comparison (section 5) | The report for Lakes 14×14; the gaps listed in GAME.md section 18 and the generator fixed |
| 3 | `legal`, `apply`, `state`; the translation table and coordinate transform; forced draws; the mirror | One game played to its end on both engines with every command compared |
| 4 | Games at scale with the bot and the coverage policy; the report; fixtures into tests | Thousands of championship-format games with no mismatches, every rule in the coverage table exercised; then the other settings |
| 5 | Matches against Crazy (section 7) | A match result in [`runs.md`](runs.md) |

Phases 2 and 3 are independent once 1 is done.

## 9. Risks

- **Game construction is the hard part.** bengreff had to rebuild the client's session setup step by step to match the app. If ours differs (settings, handicaps, starting stars), every game is checked against the wrong reference. Section 3's check is the guard.
- **Updates move field offsets and method names.** The oracle pins one version and refuses data from another rather than misreading it.
- **The il2cpp runtime keeps global state.** bengreff saw failures in long-lived processes that a fresh one didn't reproduce. Restart the oracle every so many games, and replay a failing game in a fresh process before calling it a mismatch.
- **Coverage is the real limit of "complete":** a rule nothing ever triggers passes silently. The coverage table makes that visible; rules that stay at zero get a hand-built position or a scripted sequence of commands.
- **The bot's games are a narrow sample.** It plays one way; the coverage policy widens it, and the other settings (section 4) widen it further.
