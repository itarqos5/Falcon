# Falcon against vanilla Minecraft 26.3: start-up and memory

Measured on 15 November 2026 on one PC: AMD Ryzen 5 5600G, AMD Radeon RX 580 2048SP, 16 GB RAM, Windows 10, Java 25. The scripts are in `tools/vs_vanilla`.
**No frame rate comparison with vanilla was run.** What follows is the start-up time and the memory of the two programs in the same world, nothing else.

## Results

| | Time from starting the process to "in the world" | Working set 30 s later | Private memory 30 s later |
| --- | --- | --- | --- |
| Vanilla 26.3, render distance 8 | 15.2 s, 15.6 s | 1484 MB | 2296 MB |
| Falcon, first start (caches are built), render distance 8 | 9.2 s, 9.2 s, 9.1 s | 245 MB | 370 MB |
| Falcon, later starts, render distance 8 | 5.3 s | 245 MB | 373 MB |
| Falcon, later starts, render distance 16 | 5.3 s | 392 MB | 521 MB |

Vanilla's memory is from one run, its start-up time from two (the second came from a probe run of the same kind); vanilla at render distance 16 was not measured. Falcon's first-start
figures are three runs and its memory at render distance 8 three runs (244.6, 245.7 and 244.8 MB). The raw numbers of the runs are in `docs/benchmarks/vs-vanilla.json`.

## Method

* **The same world.** Both games open a world with the seed 8676910650601010077 that has no chunks yet, so both generate the terrain. Vanilla 26.3 keeps a world's generation settings in
  `data/minecraft/world_gen_settings.dat` and ignores the seed in a `level.dat` made by Falcon, so `make_world.ps1` takes a settings file that vanilla itself made and changes only its
  seed to Falcon's.
* **The same options** (`tools/vs_vanilla/common.ps1`, `Write-BenchOptions`): render distance 8 (or 16) and the same simulation distance, vsync off, frame cap off, clouds off, minimal
  particles, no entity shadows, window 854x480, no pausing when the window loses focus.
* **Vanilla** is started the way the official launcher starts it, from horizon-launcher's copy of the game on this PC: `javaw -Xmx2G` (the official launcher's default heap limit) with
  the default garbage collector, `--quickPlaySingleplayer`. A made-up access token is used, and the addresses of Mojang's profile services are pointed at a closed port so that they fail
  at once; with the made-up token they answered slowly (401, 429), which would have been measured as start-up time.
* **Falcon** is `Falcon.exe` of the 1.2.0 build, with its tables and the game's own `data/` laid out as the launcher lays them out. The player stands where vanilla's player spawned
  (`--cam 9.5,63,1.5,0,0`), so the player does not move and no ticks are run for them.
* **"In the world".** Vanilla: the moment its log says `... logged in with entity id`, which is a moment before the first picture of the world (it favours vanilla slightly). Falcon: the
  moment it says `spawn area loaded` (its loading screen waits for the chunks around the spawn). Both are timed by a stopwatch from the start of the process, polling the log file.
* **Memory** is read with `Process.WorkingSet64` and `PrivateMemorySize64` 30 seconds after that moment.
* **Windows.** Vanilla ran in a visible window. Falcon ran in a hidden, unfocused one (`--background`), so that nothing was shown on the screen. The start-up time and the memory do
  not depend much on that; a frame rate would.

## What this does not show

* Vanilla generates structures and every feature of the world, runs the server tick, mobs and particles. Falcon's singleplayer does not have those yet, so it does less work. A part of
  the difference is work Falcon does not do.
* Few runs on one PC. They show the size of the difference, not exact figures.
* The "first start" of vanilla was not measured separately: its game folder was new each time, but the PC had run it before and its files were in the file cache.

## Repeating it

```
tools\vs_vanilla\make_world.ps1                         # the two worlds with one seed (needs a world_gen_settings.dat that vanilla made, see the script)
tools\vs_vanilla\run_vanilla.ps1 -RenderDistance 8       # a VISIBLE vanilla window for about a minute
tools\vs_vanilla\run_falcon.ps1 -RenderDistance 8 -Hidden          # first start: caches are built
tools\vs_vanilla\run_falcon.ps1 -RenderDistance 8 -Hidden -Warm    # later starts (after a run of the same -Name)
```

`run_vanilla.ps1` brings the game window to the front for a moment to give it a key press (the game reads keys only while it has the focus) and only ever looks at the game's own
screenshots; nothing of the desktop is captured.
