# Rendering and the Graphics API setting

Video Settings has a **Graphics API** button: *Prefer OpenGL* or *Prefer Vulkan* (the game's own labels; there is no "Default"). The
choice is kept in `falcon_options.txt` as `graphicsBackend:opengl` / `graphicsBackend:vulkan` (an old `graphicsApi:<number>` is still
read: 0 and 1 mean OpenGL, 2 Vulkan). It takes effect the next time the game starts.

## What works today

Only OpenGL can draw the game. `effectiveGraphicsApi()` (`falcon/gfx/graphics_api.hpp`) is the one place that decides what really runs: it
returns the chosen API when `graphicsApiRenders()` says it can render, otherwise OpenGL, with a one-line reason that the start-up log
prints (`[falcon] graphics API: opengl - the vulkan renderer is not built yet: drawing with opengl`) and the Vulkan button's tooltip
repeats. A new API (Vulkan first, Metal after) is one enum value plus its ids and names in that header; the setting screen, the options
file and the start-up choice need no other change.

## What an abstraction layer has to cover (measured on the code, not guessed)

The renderer's whole dependence on OpenGL, counted in `src/` and `include/`:

| | |
|---|---|
| calls into OpenGL | 425, in 10 files: `world_renderer.cpp` 172, `panorama_renderer.cpp` 42, `texture.cpp` 35, `gui_renderer.cpp` 34, `shader.cpp` 29, `hand_renderer.cpp` 25, `texture_array.cpp` 20, `render_target.cpp` 20 |
| distinct functions | about 60 (no geometry shaders, no compute, no instancing, no tessellation, no multisampling) |
| shader programs | 6, all `#version 330 core`, embedded as strings (world, sky, selection lines, GUI, panorama + its blur, held item) |
| buffers | vertex, index (one shared quad index list), one big **arena** vertex buffer (`BufferSubData`, `CopyBufferSubData` to grow and compact it), dynamic and stream buffers |
| textures | 2D (GUI atlases, fonts), **2D array with mip levels** (block textures; mips are built on the CPU), **3D** (the carried-light volume, `R8`), **cube map** (panorama), render-target colour textures |
| render targets | two off-screen colour targets (the panorama blur), the window's back buffer, and a read-back of it (screenshots) |
| draws | `DrawArrays`, `DrawElements`, `DrawElementsBaseVertex` (one per visible section) |
| state | blend with separate colour/alpha factors, depth test / mask / func, cull face / front face, scissor, viewport, clear |
| queries | GPU timer (`TIME_ELAPSED`), three in flight |

That is small enough to put behind one interface; it is not small enough to do without one.

## The plan

1. **One interface (`RenderDevice`)** with exactly the objects above, nothing more: `Buffer` (vertex / index / uniform, static / dynamic,
   sub-update and device-side copy), `Texture` (2D, 2D array with a mip count, 3D, cube; sub-region and per-mip upload), `Sampler`
   (filter, wrap), `RenderTarget`, `Pipeline` (a compiled program plus its fixed state: blend, depth, cull, vertex layout; Vulkan needs
   these baked, so OpenGL gets the same shape), and a `CommandList` for `bind`, `draw`, `drawIndexedBaseVertex`, `setScissor`,
   `setViewport`, `clear`, `beginTimer / endTimer`, `readback`. No GL enum or object may appear above it.
2. **Port the OpenGL code to it first, with no change in behaviour.** Order: `Texture` / `TextureArray2D` / `RenderTarget` /
   `Shader` (they are already thin wrappers over GL), then `GuiRenderer` (small, covered by many screenshots), `PanoramaRenderer`,
   `HandRenderer`, last `WorldRenderer`. Every step is checked by the existing tests and by pixel-comparing captures before and after.
3. **Shaders written once**, in GLSL 450 with explicit locations, compiled to SPIR-V by `glslang` at build time. Vulkan takes the SPIR-V;
   the OpenGL backend takes GLSL 330 produced from it with SPIRV-Cross (or keeps the six hand-written 330 sources next to their 450
   twins until the translator is trusted); Metal takes MSL from the same SPIR-V. Uniforms become one uniform block per program.
4. **Windowing.** GLFW creates an OpenGL context (`GLFW_OPENGL_API`) today; for Vulkan it is created with `GLFW_NO_API` and a
   swapchain over the window surface. The API therefore has to be known before the window exists, which is why the setting only
   takes effect at start-up. The start-up code that builds the window moves behind a `GraphicsBackend` (create window, create
   device, present, read back the frame for `--capture`).
5. **The Vulkan backend.** Instance + device + swapchain with a depth image; one render pass per frame with the panorama blur in its
   own off-screen pass; per-pipeline descriptor sets (texture array, 3D volume, GUI atlases); the arena as a device-local buffer with a
   staging ring; mips uploaded from the CPU chains that already exist; timestamp queries for the GPU timer. Needs the Vulkan headers
   (`Vulkan-Headers` via FetchContent) and the loader (`vulkan-1.dll`, loaded at run time so the game still starts without it).
6. **Metal** (macOS) follows the same interface once Vulkan has shaken it out; it needs its own window code (`CAMetalLayer`).

## What I would not do

* Put `#ifdef VULKAN` branches inside the renderers. That is how two backends turn into one tangle; the interface is the point.
* Start with Vulkan. It makes the interface (and its mistakes) permanent before a second implementation has tested it; porting OpenGL
  onto it first is the cheapest way to find out what it is missing.
