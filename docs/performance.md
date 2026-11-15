# World rendering performance

What changed in the world renderer, how it was measured, and what did and did not help. Measured on a Ryzen 5 5600G with an AMD RX 580 2048SP, at the game's
default 854x480 window; the same GPU time at 1920x1061 showed that the world pass here is bound by geometry, not by pixels.

## Result

The same fixed flight (`--bench 30`: 24 blocks a second over the terrain, 25 blocks above the ground), the old renderer against the new one, two runs each (they
agree to within 1%). "GPU" is the world pass as measured by an OpenGL timer query; "CPU" is the game's work per frame (average).

| Render distance | | GPU world pass | CPU per frame | Quads drawn | Sections drawn | Draw calls |
| --- | --- | --- | --- | --- | --- | --- |
| 16 | old | 3.40 ms | 1.60 ms | 485,000 | 1,539 | ~4,400 |
| 16 | new | **2.65 ms** (-22%) | **1.22 ms** (-24%) | 388,500 (-20%) | 872 | **3** |
| 32 | old | 9.60 ms | 3.68 ms | 1,683,000 | 5,823 | ~17,000 |
| 32 | new | **6.40 ms** (-33%) | **2.36 ms** (-36%) | 1,225,000 (-27%) | 2,790 | **3** |

If nothing else limited it, that is about 104 fps -> 156 fps at render distance 32 (1000 / the slower of CPU and GPU).

The picture is unchanged: with occlusion culling and multi-draw on, the frames captured at 12 camera poses (open air, near the ground, inside rock) are
**pixel-identical** to the old renderer's (0 differing pixels). The light map change below differs by at most 1/255 in a channel.

## What was done

* **Occlusion culling.** The mesher records, for every section, which of its six faces can see each other through it (a flood fill through the cells that do not
  block sight: `SectionMesh::visibility`). The renderer keeps those masks in a dense grid and each frame walks from the camera's own section outwards through the
  faces that connect (Sodium-style: it never turns back on a direction, and only walks sections in view). A cave under the ground or a valley behind a hill is never
  drawn. Sections that are not meshed yet count as open, so nothing is hidden by something unknown. With the camera inside solid rock the walk is skipped and
  everything in view is drawn, as the game does. The walk also replaced the per-frame scan and sort of every section, which is why CPU time fell too.
* **Multi-draw.** On an OpenGL 4.3+ context (the window asks for 4.5 and falls back to 3.3) a whole pass is one `glMultiDrawElementsIndirect`: one command per run of
  quads, and the section's place comes from an instanced attribute chosen by each command's base instance. Up to ~17,000 calls a frame became 3. The one-call-per-run
  path is still there for 3.3 contexts.
* **Light map as a 16x16 picture.** The shader's per-pixel light arithmetic (divisions, a 4th power) became one filtered texture read; the picture is made again when
  the Brightness option changes. It is also what the game itself does.
* **Face direction from the mesh.** The carried-light lookup (holding a torch) took the face normal from screen derivatives; the mesher's own direction is now stored
  in a spare vertex byte and used instead.

## What did not help (and was not kept)

* **Merging draw commands, a smaller or differently fetched vertex.** GPU time follows the number of quads drawn (about 5 ns each), not the number of commands
  (about 0.035 us each) or the vertex format: drawing more quads with fewer commands was 45% slower, and fetching the 20-byte vertex as two plain integers instead of
  four typed attributes changed nothing. So a 16-byte vertex would save memory, not time, and it cannot be reached without losing precision.
* **SIMD for terrain generation** was not attempted: terrain is bit-exact with the game and compiled with `-ffp-contract=off` for that reason, it only runs on worker
  threads (never in the frame), and it would mean restructuring the density-function graph. Worth a profile first.

## Reproducing it

`tools\benchmark.ps1` runs the benchmark with a hidden window (never a visible one) and `-Compare` compares frames of two builds pixel by pixel. Each part can be
switched off with an environment variable (listed at the top of that script), which is how the numbers above were compared in a single build.

## Uncapped (no vsync) feels choppy

With vsync off the game can run at hundreds of frames a second and still look uneven. What was changed, from reading how the frame loop behaves rather than from a
number measured on a window you can see (the measuring here uses hidden windows, which the display does not pace like a real one, so this one needs your eyes):

* **A short queue to the graphics card** (`gfx/frame_queue.hpp`). Without vsync the swap does not wait, so the CPU can queue many frames ahead; the driver's queue then
  fills and drains in bursts, and the frames come out unevenly spaced. Now, with vsync off and no frame cap, a fence after each frame and a wait for the one two back keeps
  it at two. It costs nothing when the card is the faster one. (`FALCON_FRAME_AHEAD=<n>` forces a limit, 0 turns it off, for comparing.)
* **New meshes are copied into the mesh arena by the GPU** from a staging buffer instead of being written into it from the CPU. Writing into a buffer that queued draws still
  read from can make the driver wait for all of them; with a deep queue that was one stall per uploaded section while chunks stream in. (`FALCON_DIRECT_UPLOAD=1` is the old way.)
* **The GPU timer queries only run while F3 or the benchmark shows the number**; they cost the driver something on every frame.
* `FALCON_NO_PACING=1` skips the 16 ms sleep of a hidden window, to measure it running flat out.

## Making chunks (measured 2026-11-15, 12 logical processors, `--bench 20` at render distance 16)

Where a chunk's worker time goes (`--bench` prints it as "worker time"; `FALCON_PROFILE_WORLDGEN=1` splits the terrain further), before any change:
terrain 18.7 ms, light 4.6, mesh 5.4 (about ten sections of 0.54), decorate 3.0. Terrain itself: biomes 2.6, density 4.0, aquifer fill 2.4, surface rules 4.5, caves and
ores 4.2, repacking into sections 0.25.

What was changed, each checked with `falcon_bench_chunks` (a hash of every block, biome and light value of 144 chunks, the same before and after, and the same on 1 and 6
threads) and the whole-chunk comparison against the real game in `falcon_worldgen_chunk_tests`:

* **The carvers' walks are made once per starting chunk** and shared (`Underground::SourcePlan`). Every chunk replays the carvers of the 17 x 17 chunks round it, and each
  walk depends on its starting chunk alone; only "does this step reach me" depends on the chunk being made. Caves took about 1.9 ms of the 3.5 ms for caves and ores (worked out from the totals, not timed on their own), and 0.25 ms afterwards (timed).
* **Ore veins that cannot reach the chunk are dropped after their first two random draws**, and the blob's sine is a table (`std::sin(float)` goes through the long double
  sine on this runtime, 2.8% of everything).
* **One thread, a chunk: 15.2 ms to 13.0-13.4 ms.** On 6 threads 3.37 ms to 3.0 ms a chunk (297 to 333 chunks a second).
* **All but two logical processors make chunks while the world loads**, at below-normal priority, and half of them afterwards (`PriorityPool::setActive`). The spawn area
  loads in 0.63 s instead of 0.85 s (OpenGL; Vulkan 0.91 s to 0.61 s).

What a profile of the terrain says is left (single thread, share of the time): Perlin noise sampling 28%, density function evaluation 22%, aquifer 12%, the biome
R-tree 8.5%, surface rules 9%, ore blobs 8%. All of those are the bit-exact port of the game's own arithmetic, so each needs care (and the ground truth test) rather than a
quick change; a cell test and a division per cell in the ore blobs, which looked like the cost, was not (rewriting it changed nothing, and was not kept).

Finding such things needs a profiler, and this PC has none: `FALCON_SAMPLE=<file>` (src/platform/sampling_profiler.cpp) samples the instruction pointer of the frame
thread and the workers during `--bench`, and `node tools/resolve_samples.mjs <file> <exe>` names the functions (it needs an exe that was not stripped: configure a second
build directory with `-DFALCON_STRIP=OFF`, and `-DFALCON_DIST_DIR=<elsewhere>` so that it does not replace dist/). `falcon_bench_chunks --sample gen|light <file>` does the same
for the generator alone, on one thread or many, with no window.

## Vulkan, compared with OpenGL

The frame thread does about 2 ms of work a frame on both; the Vulkan layer costs about 0.3 ms a frame more than the OpenGL driver. Pipelines are not the cost (six were made in
a world run, 0.14 to 0.7 ms each: the driver keeps its own cache). What was: the upload arena gave back every chunk bigger than a normal one after each frame, so when meshes
streamed in each big upload was a `vkAllocateMemory` and a map a few frames later, again and again; p99 CPU per frame was 16 to 39 ms against OpenGL's 4 to 6. Chunks are
now kept until they have been unused for about ten seconds: p99 5.2 ms. A thread for submitting and presenting was not added: with 2 ms of CPU against 4 ms of GPU per frame there
is little to overlap, and presenting cannot be measured with a window that is never shown.
