# Rendering and the Graphics API setting

Video Settings has a **Graphics API** button: *Prefer OpenGL* or *Prefer Vulkan* (the game's own labels; there is no "Default"). The
choice is kept in `falcon_options.txt` as `graphicsBackend:opengl` / `graphicsBackend:vulkan` (an old `graphicsApi:<number>` is still
read: 0 and 1 mean OpenGL, 2 Vulkan). It takes effect the next time the game starts. `--graphics-api opengl|vulkan` chooses for one run.

The window is made for the API that was chosen (`Window`, src/platform/window.cpp): Vulkan needs a window without an OpenGL context. When Vulkan
cannot be used (no loader, no device of version 1.3, anything failing while it starts) the window is made for OpenGL instead, and the start-up log says why:

```
[falcon] graphics API: opengl - vulkan cannot be used (no Vulkan loader on this PC (vulkan-1.dll / libvulkan.so.1)): drawing with opengl
[falcon] graphics API: vulkan
[falcon] Vulkan 1.3.260 on AMD Radeon RX 580 2048SP
```

## How the Vulkan renderer works (src/vulkan/)

**The renderers are not changed.** They draw through the functions of `falcon/gfx/gl.hpp`, which are pointers (`gl::DrawArrays`, `gl::BufferData`, ...). With OpenGL the pointers
point into the driver. With Vulkan they point into `src/vulkan/gl_*.cpp`: an implementation, on Vulkan, of the part of OpenGL 3.3 (and the one 4.3 call, multi-draw-indirect) that
the game uses. The measured size of that part: about 85 functions, no geometry shaders, no compute, no instancing but one attribute divisor, no multisampling, 6 programs (9 with the
blur and the cube). That is small enough to be written out completely and exactly, which is why the plan below (an interface, and every renderer ported to it) was not taken first:
it would have meant rewriting working, tuned OpenGL code, with no way to tell the two apart afterwards; this way every frame the OpenGL path draws is also a test of the Vulkan one,
and the OpenGL path is untouched.

| file | what it holds |
|---|---|
| `vk_api.*` | the Vulkan entry points, loaded by hand through GLFW (nothing is linked against a Vulkan library; a PC without one still starts the game) |
| `vk_core.*` | instance, device selection, memory, the two frames in flight (command pool, fence, host-visible arena, descriptor pools, timer queries, deferred destruction) |
| `vk_swapchain.*` | the backbuffer (the window's picture and its depth), the swapchain, the copy onto it at the end of a frame |
| `vk_backend.cpp` | `start`, `stop`, `present`: what `Window` calls |
| `gl_buffers.cpp` | buffers (`STREAM_DRAW` in the frame's host memory, the rest on the device, written through staging) and vertex arrays |
| `gl_textures.cpp` | textures (2D, 2D array with mip levels, 3D, cube; RGBA8, R8, RGBA16F), samplers, framebuffers, mip generation |
| `gl_programs.cpp` | programs, uniforms (read out of the SPIR-V: a name becomes an offset in one uniform block), the pipeline cache |
| `gl_draw.cpp` | passes (dynamic rendering), clears, the draws, the fixed-function state |
| `gl_misc.cpp` | read-back, timer queries, fences, `GetString`, putting it all into `gl::` |
| `spirv_reflect.*`, `shader_registry.*`, `spirv_data.*` | reading names out of SPIR-V; which Vulkan shader is the twin of a GLSL one; the compiled SPIR-V |

Conventions that make the two draw the same picture:

- **Targets are stored the way OpenGL stores them**: the first row is the bottom. Every target (the window's picture, the blur's textures) is drawn with an ordinary viewport, so a texture
  that was drawn into is sampled exactly as in OpenGL, scissor rectangles need no flip, and `glReadPixels` is a copy. Only the copy onto the swapchain turns the picture over. The
  price: counter-clockwise (OpenGL's front face) is clockwise to Vulkan, which the pipelines are made with.
- **Depth**: OpenGL's clip depth runs -w..w, Vulkan's 0..w. Every vertex shader ends with `gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5`, which gives the depth OpenGL gives.
- **Frames**: the first call of a frame begins its command buffer; swapping the buffers submits it. A pass is opened by the first draw to a target and closed when the target changes, a copy
  must be made, or the frame ends (copies cannot happen inside a pass). Layouts are tracked per image. Two frames are in flight; objects that are replaced or deleted are destroyed when
  the frame that may still use them is done. `glFenceSync` is the number of a submission.
- **Window with no swapchain**: captures and `--background` never show a frame, so they only draw into the backbuffer and read it back.

### Shaders

The game's shaders are GLSL 330 text given to `gl::ShaderSource`. Each has a **twin** for Vulkan in `src/vulkan/shaders/` (GLSL 450: explicit locations, one uniform block with the
names the 330 shader gives its uniforms, samplers at set 1), compiled to SPIR-V by `tools/compile_vk_shaders.mjs` (`glslangValidator`, from the Vulkan SDK or the package
`glslang-tools`) into `src/vulkan/spirv_data.cpp`, which is committed: building the game needs no shader compiler. A GLSL shader names its twin with a comment line, `// vk: gui.frag`;
the `#define LIGHT_LUT` and `#define FLAT_NORMAL` lines of the world shader choose the variants of its twin (specialization constants).
**When a shader changes, its twin has to change too**; the CI draws the same frames with both APIs and shows how far apart they are.

When a shader changes: `cmake -S . -B <scratch dir> -G Ninja -DFALCON_COMPILE_SHADERS=ON` and `cmake --build <scratch dir> --target falcon_vk_shaders`. That fetches glslang 16.6.0 (its
libraries only, built from source: no SPIRV-Tools, no Python; `tools/compile_spirv.cpp` is the small command-line front end) and rewrites `src/vulkan/spirv_data.cpp`. Use a scratch
build directory outside the repository (the build takes a few minutes the first time). Without glslang, `glslangValidator` from the Vulkan SDK works too: `node tools/compile_vk_shaders.mjs <path to it>`.

### Checking it

`falcon_vulkan_tests` reads the SPIR-V back (no graphics card) and checks that each program has the uniforms, samplers and vertex inputs of its GLSL 330 original.

Pictures: the same frame with `--graphics-api opengl` and `--graphics-api vulkan` (hidden window, `--capture`), then `falcon_image_diff a.png b.png <max-rms> <diff.png>`. Measured on an
RX 580 (Vulkan 1.3.260), 1280x720, 2026-11-15: title screen, a world (sea, cliff, held arm, hotbar), the pause menu, the inventory, third person (F5) and the Options screen over the
blurred panorama all agree with OpenGL to an RMS of 0.04..0.5 (of 255), except where something moves between two runs: the animated water and the panorama's rotation differ by
as much between two OpenGL runs as between OpenGL and Vulkan. `--bench` runs on Vulkan too; GPU time of the world pass and CPU time per frame are the same as OpenGL's within the
spread between runs (3.6..4.7 ms GPU at render distance 12), the process's working set is far smaller (about 165 MB against 1.1 GB peak; the cause was not investigated, probably the OpenGL driver's system-memory copies of the buffers).
Not measured: other GPUs and drivers, anything with the validation layer (it is not installed on that PC), resizing, fullscreen, and a visible window.

`FALCON_VK_VALIDATION=1` turns the validation layer on where it is installed (messages go to stderr, tagged `[vulkan] validation`); `FALCON_VK_DEVICE=<part of a name>` chooses among
several devices. The `Vulkan` workflow (`.github/workflows/vulkan.yml`) is meant to draw the title screen and a world on Mesa's software Vulkan (lavapipe) with the layer on; it has not
run yet.

### What it needs, what is not done

Vulkan 1.3 (dynamic rendering, synchronization2), the vertex formats the meshes use (16-bit integers read as numbers), and a queue that draws and shows frames. Not done: pipelines are
made when first needed (a short stall the first time a state is seen; there is no pipeline cache on disk), the driver's memory is allocated in blocks of one object each (a few hundred
objects), there is no transfer queue, and it is only measured on one GPU (see above).

## Levels of detail (LODs): src/render/lod_renderer.*, src/worldgen/lod_surface.*

Beyond the chunks that are loaded, the world is drawn as columns, the way Distant Horizons draws it: every cell is a block-shaped column with a flat top and a flat colour, with a wall where
the next cell is lower. Video Settings > LODs: a checkbox (on or off), **LOD Distance** (a slider, 64 to 8192 chunks, every doubling the same width) and **LOD View Strength** (Low,
Medium or Full Detail). The three are saved as lodsEnabled, lodChunkRadius and lodDetail, and the presets set them (Fast: off; Fancy: 128 chunks, Medium; Fabulous!: 256, Full).

* **Where the heights come from: no chunk is generated for a LOD.** *Chunk* mode (the cells next to the loaded chunks, 1 or 2 blocks wide) reads the generator's own final density of whole
  chunks and takes the top solid block of each column (OverworldGenerator::surfaceHeights): 16 microseconds a column, and the chunk's own ground (mean error 0.26 blocks against the real
  chunks, 100% within 4). Further out the *preliminary surface* (the game's aquifer surface level, one evaluation of a function of x and z: 3 microseconds a column) is corrected by the
  real height on a sparse grid, 100 samples a tile (*Residual*: 20 microseconds a column, 3.3 blocks off where the bare preliminary surface is 10). Low Detail uses the preliminary
  surface alone. The colour is the biome's (grass tint from its climate, sand, snow where it is cold at that height, rock above the trees, the water's own colour and depth), with a few
  percent of lightness varying from cell to cell, and where the decorator would grow trees (Decorator::treeFacts) a share of the columns is the colour of leaves and as tall as a tree.
* **Which cells where.** Tiles are 32 x 32 cells. A quadtree splits a tile while its cell would be more than k times its distance wide: Low Detail k = 1/40 (cells from 8 blocks), Medium
  k = 1/100 (from 2), Full k = 1/200 (from 1: the cells next to the chunks are blocks). So the cells double in width each time the distance doubles, and a radius of 8192 chunks is
  nine levels of detail and a few hundred tiles (23 MB at Low).
* **Making them.** The world's worker pool makes the tiles after the chunks (a coarse stand-in first so that the horizon fills in at once, then the tile itself, nearest first), a finished
  tile is meshed there (a run of cells with the same top and colour is one quad: a calm sea is a few quads) and put on the GPU by the main thread within a time budget. Tiles are kept
  while they might be wanted again, within a budget of vertices.
* **Drawing.** A pass of its own between the sky and the chunks, with its own near and far plane (so the depth buffer stays precise to 130 000 blocks); then the depth is cleared and the chunks
  are drawn over it. The haze (fog) is stretched to the far end of the LODs while they are on. 12 bytes a vertex, all the tiles in a few big buffers (a buffer a tile would be thousands of
  allocations), one draw a tile, tiles outside the view skipped. Shaders: the lod program (src/vulkan/shaders/lod.vert and lod.frag are its Vulkan twins). Only worlds this client generates
  itself have LODs (not a server's).

Measured (hidden window, a 20 second flight at render distance 12, LODs 256 chunks, 12 logical processors): the GPU time of the frame does not change; the frame thread takes 0.25 ms more at Low
Detail, 0.4 more at Medium, 0.8 at Full (897 tiles, 72 MB at Medium). OpenGL and Vulkan draw the same picture (RMS 0.23 of 255). The tests: falcon_lod_tests (the mesher),
falcon_worldgen_chunk_tests (the heights against real chunks); falcon_bench_chunks --lod-bench prints the cost and the error of each way of getting a height.

## What an interface would have to cover (measured on the code, not guessed)

The renderer's whole dependence on OpenGL, counted in `src/` and `include/`:

| | |
|---|---|
| calls into OpenGL | 425, in 10 files: `world_renderer.cpp` 172, `panorama_renderer.cpp` 42, `texture.cpp` 35, `gui_renderer.cpp` 34, `shader.cpp` 29, `hand_renderer.cpp` 25, `texture_array.cpp` 20, `render_target.cpp` 20 |
| distinct functions | about 60 (no geometry shaders, no compute, no instancing, no tessellation, no multisampling) |
| shader programs | 6, all `#version 330 core`, embedded as strings (world, sky, selection lines, GUI, panorama + its blur, held item) |
| buffers | vertex, index (one shared quad index list), one big **arena** vertex buffer (`BufferSubData`, `CopyBufferSubData` to grow and compact it), dynamic and stream buffers |
| textures | 2D (GUI atlases, fonts), **2D array with mip levels** (block textures; mips are built on the CPU), **3D** (the carried-light volume, `R8`), **cube map** (panorama), render-target colour textures |
| render targets | two off-screen colour targets (the panorama blur), the window's back buffer, and a read-back of it (screenshots) |
| draws | `DrawArrays`, `DrawElements`, `DrawElementsBaseVertex` (one per visible section), `MultiDrawElementsIndirect` |
| state | blend with separate colour/alpha factors, depth test / mask / func, cull face / front face, scissor, viewport, clear |
| queries | GPU timer (`TIME_ELAPSED`), three in flight |

## What would still be worth doing

* **A real `RenderDevice` interface** (buffers, textures, samplers, render targets, pipelines, a command list; no GL enum above it), with the renderers ported to it one at a time. The
  Vulkan layer above makes this easier to judge than it was: its list of what the game really needs is the list of what the interface needs. It is the way to a Metal backend (macOS),
  and the way to a Vulkan path that is faster than "OpenGL's calls turned into Vulkan's": pipelines made ahead, one descriptor set per material, no tracking of OpenGL's state.
* **Metal** (macOS) with its own window code (`CAMetalLayer`).
