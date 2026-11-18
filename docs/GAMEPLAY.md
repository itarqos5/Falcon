# The world in singleplayer: the day, the mobs, the dimensions

What a singleplayer world does beyond its terrain, how it is built, and what was and was not checked. Falcon's own simplification of the game's rules in every case, with the game's numbers
(health, speed, damage, ranges, cooldowns, drops, the sky's colours) where they are known.

## The day and the night

- `world/day_cycle.*`: the time of day (0 sunrise, 6000 noon, 12000 sunset, 18000 midnight), saved with the world. `/time set|add|query|pause|resume|rate` as the 26.3 command tree has them
  (`day`, `noon`, `night`, `midnight` and `minecraft:` markers; `advance_time` game rule; F3 shows the time and the moon phase).
- The sky is **data**: the game keeps it as keyframed tracks in `data/minecraft/timeline/day.json` (sun and star angle with a cubic Bezier ease, sky, fog, sky-light colours and factors,
  the star brightness, the sunrise and sunset colour) and the base colours in `dimension_type/overworld.json`. `daycycle::Timeline` reads them at run time and evaluates them the way the game
  does. Without the files a closed-form approximation is used (it agrees to within a tick or two).
- `sky.frag` (and its Vulkan twin) draws the glow, the stars, the sun and the moon with its eight phases; the light map takes the game's `SkyFactor`, `SkyLightColor` and `AmbientColor`
  (so the night is blue and dim and not black), and so does the CPU copy of the light map that items, mobs and the hand use (`setEntitySkyLight`).
- Checked: logic tests (`falcon_logic_tests`) for the maths and the timeline reader, hidden screenshots at dawn, dusk, night and with the moon. Not checked: against the real game side by side.

## Mobs

- `world/mob.*`, `mob_path.cpp`, `mob_spawn.cpp`: pigs, cows, sheep, chickens; zombies, husks, skeletons, creepers, spiders; endermen; zombified piglins, blazes, ghasts; the Ender Dragon and the
  end crystals. They are drawn from the game's own models by `MobRenderer`.
- Body: the game's `LivingEntity` physics (gravity 0.08, drag, ground friction, the speed attribute turned into acceleration, step height 0.6, jumping, swimming up, fall damage, lava, drowning
  not), knockback, hurt flash, the fall on the side when dead, burning in daylight for zombies and skeletons, endermen hurt by water.
- Brain (not the game's goal selectors): passive mobs stroll (one try in 120 ticks, ten blocks), look at the player, and run when hit; hostile ones notice the player within their follow range,
  walk a path found with A* over the blocks (`findMobPath`: steps up one block, drops three, avoids lava, fire and deep water, goes round walls), strike when close (damage scaled by the
  difficulty as the game does), a creeper swells for 30 ticks within three blocks and blows up (power 3), skeletons keep their distance and shoot arrows (speed 1.6, spread by difficulty), spiders
  leap and climb, endermen turn on a player who looks at their head and teleport, zombified piglins come in a crowd once one is hit, blazes hover and shoot three small fireballs, ghasts a
  large one.
- The player hits mobs with the attack cooldown, critical hits and sprint knockback (`GameSession::attackMob`); mobs hurt the player through `GameSession::hurt` (armor counts), knock them back
  and tilt the view. Killing gives the loot table of the species and the experience points (no orbs yet: the points go straight to the bar).
- Coming and going: animals appear with a chunk (one in ten chunks has a herd of four on its grass, once, remembered in the save), monsters appear in the dark (24 to 100 blocks from the player,
  light by the game's rule, the sky light less what the night takes away, a cap of 70, packs of the game's size), and fade away beyond 128 blocks or, slowly, beyond 32. `/summon` makes any of them;
  `/kill` removes them. They are saved with the world.
- Not done: the game's real spawn tables by biome and the caves' special cases, breeding, taming, trading, most other mobs (slimes, witches, villagers, piglins, hoglins, the wither, the warden ...),
  mobs picking up blocks, doors, boats and minecarts for mobs, pathfinding to open doors or swim, XP orbs, mob armor and held items, fire on a burning mob, the real dragon phases (landing, perching,
  breath clouds).
- Test aids: `FALCON_MOB_LOG=1` prints every mob once a second; `FALCON_TEST_MOBS=ender_dragon,ghast` draws a row of models in front of the player.

## The Nether and the End

- A dimension is an `OverworldGenerator` built from `noise_settings/nether.json` or `end.json` (the generator now takes any min_y and height inside the world, a lava or air default fluid, and
  the Nether's and the End's biome sources: five points on two noises, and `TheEndBiomeSource`). The End needed three new density functions, `distance_to_point`, `slice` and
  `end_outer_islands` (the outer islands' simplex noise is the game's, from a legacy random source with 17292 values skipped).
  The settings of both say `legacy_random_source: true`, and `RandomState` follows what 26.3 does then (read from its bytecode): the positional factory is `LegacyRandomSource(seed).forkPositional()`,
  named noises and the surface rules' random factories come from it by the name's hash, the old blended noise draws from `LegacyRandomSource(seed)`, and the Nether's temperature and
  vegetation noises are made the old way (`LegacyFbmInitializer`: one Perlin per octave from the same source, 262 values skipped for an unused octave) from `LegacyRandomSource(seed)` and
  `(seed + 1)`. So the terrain, the surface and the biomes of both are meant to be the game's own for a seed. **That is not checked against the real game**: the Overworld's is (the chunk
  tests), the Nether's and End's has no reference data yet, and the cave carver of the Nether (`nether_cave`) and the placed features are not ported, so a real Nether chunk has tunnels and
  ores that Falcon's does not.
- `WorldRenderer::enterDimension` empties the loaded chunks and starts making another dimension's; the light engine has a no-sky mode for the Nether. Each dimension keeps its own edits, items,
  containers and mobs, in `falcon_save.dat` (Overworld) and `falcon_save_nether.dat` / `falcon_save_end.dat`; the player's dimension and position are saved.
- Nether decoration (`world/nether_decoration.*`, Falcon's own, not the game's placed features): glowstone, quartz and gold ore, magma at the lava sea, fire, mushrooms, roots, fungi and vines,
  huge fungi, basalt columns. Not done: fortresses, bastions, nether fossils, the nether carver, lava falls and the rest of the features.
- Portals: fire in a frame of obsidian (4x5 up to 21x21) lights a Nether portal (`PortalShape`), a broken frame puts it out; four seconds inside (none in creative) travel with coordinates times
  or over eight, the portal that is near is used or one is made (a frame, a platform). An eye of ender in an end portal frame opens the End portal when the ring of twelve is filled; there are no
  strongholds yet, so frames come from the creative inventory or `/give`. The purple screen overlay and the portal sounds are there.
- The End: the obsidian platform at 100, 48, 0, the ten pillars (two caged), the dragon and a crystal on each pillar the first time the player arrives, the boss bar, the crystals healing the
  dragon, the exit portal with the egg once it is dead. A crystal that is hit blows up (power 6).
- Test aids: `FALCON_START_DIMENSION=1` (Nether) or `=2` (End) starts there; `falcon_dim_probe --dim nether|end [--chunk X Z] [--slice Z] [--map N]` prints what a generator makes without the game
  (`cmake --build build --target falcon_dim_probe`).
- Checked: hidden screenshots of the Nether (soul sand valley, wastes with fungi), the portal (made by commands, entered by `/tp`), the End (pillars, boss bar, models), the logic tests, and
  all test suites. Not checked: the dragon's fight from start to end by hand, saving and loading in the Nether and the End, the End portal by eyes, returning from the End.

## Buckets, lava and /tp

- Buckets (`game_session_buckets.cpp`, `BucketItem.use`): an empty bucket scoops up a source of water or lava (or the water of a waterlogged block) it is aimed at, within five blocks; a full
  one puts a source down where it is aimed (inside a plant or a replaceable block, else beside the face that was hit; into a waterloggable block it waterlogs it); in the Nether water boils away
  and only the empty bucket is left; an empty bucket on a cow gives milk. In creative nothing is used up.
- Lava now flows (the same `FluidSimulator` as water, with lava's numbers: every 30 ticks, two blocks of spread, two levels lost a block; in the Nether every 10, four blocks and one level),
  makes no sources of its own, and meets water as the game has it: lava beside or under water becomes obsidian if it is a source and cobblestone if it flows, lava running down onto water makes
  stone, with the fizz. Water in the Nether turns to steam. Not done: cauldrons, powder snow and fish buckets, dispensers, lava burning items and mobs that fall in.
- `/tp` to coordinates puts the loading screen up when the destination's chunks are not made yet (it used to let the player fall through the unloaded air while they came); near by it is
  instant as before. `tp <location>` has no rotation, as in the game: `tp @s x y z yaw pitch`.
- The Nether's fog follows the biome with a delay: the colour is the average of the biomes within 16 blocks of the camera and moves towards it over a couple of seconds, so crossing from one
  biome to the next does not jump.

## Growth and rot (random ticks)

`world/random_ticks.cpp`: grass spreads to dirt and dies under a roof, leaves rot away when no log is within six blocks, wheat, carrots, potatoes, beetroots and nether wart ripen (the game's
speed rule for farmland and neighbours), sugar cane and cactus grow, farmland drinks and dries, saplings become a plain tree. Not done: stems and fruit, vines, bamboo, kelp, mushrooms, moss.

## Advancements, Statistics and World Options (the Game Menu)

All three are in the Game Menu (Escape) and work.

* **Advancements** (`game/progress_screens.cpp`, `game/advancements.cpp`): the tree is read from the game's own `data/minecraft/advancement/{story,nether,end,adventure,husbandry}`. The layout is a simple
  tidy tree, not the game's own (`TreeNodePosition`), so the positions differ. Only the criteria Falcon can check are met: an inventory change (items and item tags), changing dimension, and the tick
  trigger; kills, trades, structures, potions and the rest stay locked. Progress is saved in the world (`falcon_save.dat`), checked every second, and the chat says "<player> has made the advancement [Title]".
* **Statistics**: counters for time, distances (walking, sprinting, crouching, swimming, falling, flying), jumps, deaths, blocks mined, items picked up, mobs killed, damage dealt and taken; tabs General,
  Items and Mobs; saved in the world. Not counted: crafted, used, broken and dropped items, mobs killed by arrows or TNT, climbing.
* **World Options**: difficulty, game mode (needs cheats), the cheats switch, and Edit Game Rules (every rule Falcon knows, as switches and sliders, applied at once like `/gamerule`).
* **The spyglass**: the zoom (field of view x 0.1) makes the chunks it looks at (see RENDERING.md, "Zoom").
