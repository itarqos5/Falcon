# The client's log and crash reports

The client writes its own log, in Falcon's folder: `%LOCALAPPDATA%\Falcon\logs\` on Windows, `~/.local/share/Falcon/logs/` elsewhere (`$XDG_DATA_HOME` when it is set). The launcher keeps its
own log (`launcher.log`) in the same folder; nothing of the launcher is in the client's files and the client does not write the launcher's.

| file | what it is |
| --- | --- |
| `latest.log` | this run, a line to a line, flushed as it is written (so it is all there after a crash) |
| `<date>_<time>.log` | the runs before it, named by the time the run was started (`2026-11-17_22-40-57.log`), the last 15; when the run ends, `latest.log` takes this name |
| `<date>_<time>-crash.log` | the log of a run that ended in a crash or a fatal error |
| `<date>_<time>-unclean.log` | the log of a run that stopped without a word (it was killed, or the PC lost power): found as `latest.log` by the next run, which renames it |
| `crash-<date>_<time>.log` | a crash or a fatal error: what happened and where, the stack, what the game was doing, the last 80 lines of the log; the last 15 |

The launcher's Logs tab shows the client's log: `latest.log` while the game runs, and the newest dated log when it does not.

A line reads `2026-11-17 22:40:09.945 INFO  [render] 40 fps (25.2 ms); chunks 1474, sections 645 of 7569 drawn, ...`: the time, the level (DEBUG, INFO, WARN, ERROR), the category and the
message. The console gets the same lines as it always did (`[falcon] ...`): the launcher and the test scripts read those.

## What is in it

* the header: the version and when it was built, the PC (system, processor, memory), what the program was started with (the account's name and id are left out), and, when the run
  before did not end cleanly, which log that was and which crash report goes with it;
* `[gpu]` the card, the driver and the shading language; `[options]` render distance, LODs, vsync, frame rate limit; `[render]` the window, the graphics API, how the world is drawn;
* `[world]`, `[save]`, `[net]`, `[resources]`, `[audio]`: opening and saving worlds, travelling between dimensions, connecting to servers, the files and packs read, the sounds;
* every ten seconds while a world is open: `[render]` frames a second, chunks, sections and quads drawn, draw calls, video and block memory, the process's memory; `[chunks]` the jobs
  waiting and what a chunk costs in each stage (terrain, decoration, light, mesh); `[lod]` the tiles drawn and loaded and the video memory they take;
* `[perf]` a frame that took more than a quarter of a second, with what the game was doing; `[zoom]` the spyglass's cone of chunks coming up and going down; `[ui]` the screen that is
  open (DEBUG: `FALCON_LOG_LEVEL=debug` writes the debug lines too);
* the end: `end of log (exit code 0, ran 35.7 s)`. A log without that line is a run that did not end cleanly.

## Crashes

An unhandled exception (an access violation, a stack overflow, a division by zero ...), `std::terminate` (an exception nobody caught, also on a worker thread), `abort()` and a fatal error
the program catches itself (no assets, no OpenGL, a shader that does not compile ...) write a crash report before the process ends, say so in the log (`ERROR [crash]`) and, outside
a scripted run, show a window with the file's name. The report says **what** (`access violation (0xC0000005), writing address 0x0 (a null pointer was used)`, or the exception's type
and message), **where** (`Falcon.exe+0x27BEDF`, the module and the offset), the stack, and **what the game was doing**: the screen, the window, the options, the world (name, seed,
dimension, game mode, position, render distance, chunks loaded, jobs waiting, frame rate).

The release build is stripped, so the stack has offsets and not names. To read one, use the build the report came from: `addr2line -f -C -e Falcon.exe 0x27BEDF` (an unstripped build
shows the names the same way).

## For tests and scripts

* `FALCON_LOG_DIR=<folder>` writes the files there. A scripted run (`--capture`, `--background`) does not write into the person's folder unless this is set, and `--help` does not
  touch the logs at all.
* `FALCON_LOG_LEVEL=debug` writes the debug lines.
* `FALCON_TEST_CRASH=null|abort|throw|thread` makes the client fail four seconds after it starts (a null pointer, `abort()`, an exception in `run`, an exception on a worker thread), to
  see a report.
* The code: `include/falcon/util/log.hpp` (`log::info("render", "...")` and the like, `log::line` for the old console lines, `log::addContext` for what a crash report says the game was
  doing); `tests/test_log.cpp` covers the file, the renaming at the start and at the end, the marking of an unclean run, the levels and categories, and the report.
