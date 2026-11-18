# The mod platform (modapi)

The mod VM (`docs/JVM.md`) interprets a mod's own classes. What a mod *calls* (the game, Fabric's API, libraries, the JDK) is answered by **the platform**: Falcon's own Java classes, written
from the behaviour of those APIs (not copied from anything), compiled with the JDK into `modapi/falcon-modapi.jar` and loaded by the VM as real classes under the names the game uses
(`net.minecraft.core.BlockPos`, `net.fabricmc.fabric.api.event.Event`, `com.google.gson.Gson`, `java.nio.file.Files` ...). A class the platform does not have is still a recording stand-in
(`docs/JVM.md`), so a mod that needs more than there is runs on and says what was missing instead of stopping.

```
mod .jar --> VM --> mod's classes (interpreted)
                      |--> platform jar (Java: net.minecraft.*, net.fabricmc.*, com.mojang.*, org.joml.*, com.google.gson.*, java.* the VM has no native class for)
                      |        |--> net.falcon.Host / Fs / Regex / Reflect: natives answered by the client (src/jvm/host_natives.cpp)
                      |--> VM natives (java.lang, java.util collections ... in C++) + extension classes (net.falcon.jdk.*Ext, see below)
                      '--> stand-ins for the rest
client --> HostState (where the player and the camera are, the window, the world's name, what block is where, keys ...)  --> mods
mods   --> Capture (what they register, say, draw on the screen, submit to the level)                                    --> client
```

## Layout (`modapi/`)

* `src/`: the game's, the loader's and the libraries' classes (compiled normally); `src/net/falcon/` is Falcon's own glue (`Host`, `Events`, `FalconLoader`, `Registrations`, `Fs`, `Regex`, `Reflect`).
* `jdk/`: JDK classes under their own names (`java.io.*`, `java.nio.file.*`, `java.util.function.*`, streams, `CompletableFuture`, `UUID`, `Pattern` ...) compiled with `--patch-module java.base`, for what the VM has no native class for.
* `build.ps1` makes `falcon-modapi.jar` (committed, like the test mods' jars); `gen_registries.mjs` writes `Items` and `Blocks` from the names of the game's registries; `split.mjs` splits a bundle of
  sources into the folders.
* Where the jar is found: `FALCON_MODAPI_JAR`, else the one of the source tree (`FALCON_MODAPI_DEV_JAR`, set by CMake). Without it the mods run against stand-ins only.

## The Fabric surface

`Events` (host to mods) fires the real Fabric events, whose `Event`/`EventFactory` are Java here: client tick, lifecycle, play connection (join, disconnect), chat messages, the HUD
(`HudRenderCallback`, `HudElementRegistry`), the level (`LevelRenderEvents`: `COLLECT_SUBMITS`, `AFTER_ENTITIES` ...), and client commands (`ClientCommandRegistrationCallback` with a Brigadier of
its own: `CommandDispatcher`, `LiteralArgumentBuilder`, argument types; every way of reading a line is tried). `FabricLoader` answers the game and config folders (`<game>/config`), `isModLoaded`, the mod list.
Registries: `Registry.register` of an item or block tells the client (`Host.registerItem` / `registerBlock`), which adds them to its own tables (`docs/JVM.md`). Keys: `KeyMapping`.

## Drawing

* **The screen** (`GuiGraphicsExtractor`, and `GuiGraphics` for older mods): `fill`, text, `blit`, items, scissors and the 2D pose become `DrawCmd`s; the client draws them with its own graphics.
* **The level** (`SubmitNodeCollector.submitCustomGeometry(pose, renderType, renderer)`): the mod's `RenderType` carries a `RenderPipeline` (renderpearl: topology, depth test, blend, cull, vertex format). The
  renderer gets a `VertexConsumer` (`BufferBuilder`), the vertices go to the client as nine floats each (`WorldSubmit`), and `WorldRenderer::drawModGeometry` draws them with the client's own line and
  triangle drawing: wide lines as camera-facing strips, quads as two triangles, with the depth test, blending and write mask the pipeline names. That drawing runs on OpenGL and on the Vulkan
  backend alike (both implement the same `gl::` calls), which is the translation layer: **a mod never touches the graphics API, only the pipeline description.**
* Raw LWJGL calls (`org.lwjgl.opengl.GL11` ...) are not carried out yet.

## What the VM does for the platform

* `Vm::addPlatformJar`: classes load from it before a mod's jar; a stand-in made earlier for a name the jar has is made over in place.
* **Host natives**: a `native` method of a platform class is answered by C++ (`defineHostNative`).
* **Extension classes**: when a native class (`ArrayList`, `HashMap`, `String`, `Math` ...) has no such method, the VM looks for a static method of that name with the receiver first in
  `net.falcon.jdk.<Class>Ext` (`CollectionExt.stream`, `MapExt.computeIfAbsent`, `StringExt.matches`, `MathExt.hypot` ...), walking the receiver's classes and interfaces.
* **Mixin accessors and invokers** of a mod (`@Accessor("field")`, `@Invoker("method")`) are honoured on the platform's classes: they read and write the field or call the method, and the target counts
  as implementing the mixin's interface (so casts hold). Injectors are not applied.
* **Reflection** for the reflective Gson (`Reflect`): fields by name, objects made by their no-argument constructor, enum constants, generic signatures (`TypeToken`).
* **Files** (`Fs`): the game folder may be read and written, the mods folder read; any other path is as good as missing.
* A `HashMap` has a hash index when it is big; a handler that runs every frame is collected (`Vm::collectGarbage`).

## Working on it

* `falcon_jvm_run --api-gaps <mod.jar>`: every method and field of the game's, loader's and libraries' classes the mod names that the platform does not have yet (the to-do list); `--jdk-gaps`: the same for the JDK.
* `falcon_jvm_run --sim <mod.jar> <game folder> [wwp_add 1 2 3 | click:Key Name ...]`: starts the mod, joins a world, runs commands and key presses, ticks and a frame of the level and of the HUD, and prints what the
  mod submitted, drew and said, and what it lacked.
* `FALCON_JVM_TRACE=com/example/`: prints every call of a class under that name (what runs, in what order); an error report carries the methods that were running.

## State of wWaypoints (the mod this was driven by)

Runs on the platform: start-up with its real configuration files (reflective Gson, atomic writes, backups), its commands, key mappings, events, the world and HUD render paths; with a waypoint
present it submits the fill and outline of its block highlights, which the client draws. Not done: its screens (a widget library: `EditBox`, `AbstractWidget`, `Screen` ... the largest part of
`--api-gaps`), the sharing and party features (network, `java.net.http`, `java.awt` file dialogs), the Mixin injectors it carries (toggle sneak, key binds list, nick hiding ...).
