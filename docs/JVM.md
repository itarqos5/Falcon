# The mod VM

Falcon is C++, so it cannot load a Java mod. This is the other way to meet one: a small **Java virtual machine** (`src/jvm/`, `include/falcon/jvm/`) that interprets the bytecode of a mod's
own classes, answers every class of the game, the mod loader and the libraries with a **recording stand-in** (a "shim"), and reads from the shims what the mod did and wanted. The client then
carries that out with its own code. Nothing of the mod is loaded into the client and no Mixin is applied.

```
mod .jar --> ClassFile (parse) --> Vm: interpreter + natives + shims --> Capture / ModReport --> the client
                                         |  (java/lang, java/util: native C++)     registrations, events, keys,
                                         |  (net/minecraft, net/fabricmc ...: shims)   chat, mixins (read, not applied)
```

## What runs

* **`class_file.cpp`**: the class file format (constant pool, fields, methods and their code, exception tables, `BootstrapMethods`, `Record`, `ConstantValue`) and the annotations of Mixin
  classes (`@Mixin` targets, the injector annotations on methods and the game methods they name). It bounds-checks everything it reads.
* **`interp.cpp`**: every opcode except `jsr`/`ret`; frames on one slab, `try`/`catch` through the exception tables (a Java exception is a C++ `JavaThrow`, so the loop has no checks on its fast path),
  an instruction budget and a depth limit that end a mod that never stops. About **400 to 500 million bytecodes a second** (`falcon_jvm_tests` prints it).
* **`vm.cpp`**: class loading from the mod's jar (and the jars it carries inside it), constant-pool resolution caches, inline caches for virtual calls, `invokedynamic` (lambdas, string
  concatenation, record `toString`/`equals`/`hashCode`), exceptions, and the shim machinery.
* **`natives.cpp`**: the part of the Java library a start-up reaches for, written natively: `Object`, `String`, `StringBuilder`, the boxes, `Math`, `System`, `Enum`, the exceptions, lists, sets,
  maps, `Optional`, `Objects`, `Arrays`, `Collections`. Anything else on a `java/` class is a shim and is listed as *unsupported*.
* **`shims.cpp`**: the stand-ins that know what a call means: `Registry.register` (what a mod adds: items, blocks ...), `Identifier`, `Component`, the event `register` calls (the handler is kept and
  run again), key mappings, the loggers and `System.out` (into the report's log), `FabricLoader.isModLoaded`, `displayClientMessage` (into the client's chat).
* **`mod_runtime.cpp`**: reads `fabric.mod.json` / `quilt.mod.json`, runs the `main` and `client` entry points, reads the mixin configs, and keeps the VM as a `ModInstance`. `ModHost` starts every
  Fabric and Quilt mod of a folder (optimization mods are not started) and ticks them together.

## Shims

A class the VM does not have (everything of `net/minecraft/`, `net/fabricmc/`, `org/slf4j/`, `com/google/common/` and the like, **even when the mod carries a copy**) becomes a stand-in. It takes
the constructor's arguments and every call made on it, as text, and answers with what a caller will not trip on: zero, an empty collection, an empty string, or for a reference another shim (the
object itself when the call is a builder's, so `new Item.Properties().stacksTo(16).durability(250)` keeps its chain). A static field of a stand-in class is one object, the same each time, that
remembers where it came from (`BuiltInRegistries.ITEM`, `ClientTickEvents.END_CLIENT_TICK`). So `Registry.register(BuiltInRegistries.ITEM, id, new RubyItem(properties))` is read as
*item `examplemod:ruby`, a `RubyItem` with `stacksTo(16), durability(250)`*, and the client can make that item.

## What the client does with it

* **The Mods screen** runs a selected mod's start-up and shows *What it does*: what it adds, the events and keys it registers, the mixins it carries and what they patch (not applied), what it
  logged, and where it stopped.
* **In a world**, `GameSession::tickMods` starts the mods when the world is ready and runs the handlers they registered for the client tick every game tick; what a handler says to the player
  (`displayClientMessage`) goes into the chat as `[Mod name] text`. `FALCON_NO_MODS=1` plays without them.
* **Not carried out yet:** the items, blocks and keys a mod registers are *captured* (`ModReport::registrations`, `keys`) but not yet added to the game's registries; every further event (screens,
  commands, the world's ticks) needs its own meaning in `shims.cpp`.

## Limits

* **Forge and NeoForge** mods are not run (their start-up is annotations and an event bus); Fabric and Quilt entry points are (`main`, `client`; method-reference entry points and Kotlin adapters are not).
* **No reflection, files, network, threads or `java.nio`**: those calls are shims. That is also what keeps a mod inside the VM: the only way out of it is a native that was written.
* **The bytecode is trusted to be well formed** (a real JVM verifies it): a hand-made jar with broken bytecode can crash the client. The Mods screen only runs mods the player put in their own mods folder.
* A string is bytes: a non-ASCII character is several `char`s. No garbage collection (a mod's start-up is short; the VM is dropped with the mod).
* Games' own behaviour is not run: a mod whose point is its Mixins (most optimization mods, Mod Menu's buttons) has its mixins read and listed, never applied.

## Testing

`falcon_jvm_tests` runs a mod the JDK compiled (`tests/jvm_mod/`, built by `build.ps1` into `tests/data/jvm/vmtest.jar`; the real game and loader classes are only stubbed to compile against). It
checks 23 computed results against what Java gives, an item with its properties, an event handler, the mixin, a chat line on the third tick, the speed, and that a loop and a recursion that never
end are stopped. `falcon_jvm_run <mod.jar> --verbose` runs any Fabric or Quilt mod and prints what it did (Mod Menu runs in about 6 ms).
