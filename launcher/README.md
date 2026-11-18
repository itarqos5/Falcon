# Falcon Launcher

A small native launcher for Falcon (Windows and Linux), written in C++20 with a React interface. This page describes the Windows one; what differs on Linux is in the section "Linux" below.

- **No browser is bundled.** The interface is a React + TypeScript page, built into one HTML file, embedded in the exe and shown by the **WebView2 runtime that Windows already has** (it ships with Windows 11 and with Microsoft Edge on Windows 10). If it is missing, the first-run setup offers to install it.
- **Falcon is bundled.** `FalconLauncher.exe` carries `Falcon.exe` inside it and puts it in `%LOCALAPPDATA%\Falcon\client\` during setup. Nothing else to download for the client.
- **The client only plays for an account that owns Minecraft: Java Edition.** The client inside the launcher is built with the ownership check (`falcon_client_licensed`): when the launcher starts it, it hands over the signed-in account's Minecraft access token (in the environment of that one process, and removed from the launcher again at once), and the client asks Mojang's servers itself whether that account owns the game and is the profile it was started for. Started by hand, or with a token that is not a real one, it refuses and says why. The check is Mojang's answer, so it cannot be passed with a made-up one. It is a licence check, not copy protection: the source is open, and anyone who builds Falcon themselves (`cmake --build --preset release`, the `Falcon.exe` in `dist/`) gets a client without it, for development and tests.
- **Its own title bar.** The window has no system frame: a custom title bar drags the window, with minimise and close buttons, and Windows still draws the shadow and rounded corners.
- **Sign-in is Microsoft's own page**, opened in a window of the launcher with the Minecraft launcher's public client id (see below). No code to type, no Azure registration. Your password never goes through Falcon.
- **Checks that you own Minecraft: Java Edition** before it does anything else, and shows your skin's head and your player name.
- **Pressing Play** downloads the game's assets from Mojang the first time (the client jar's `assets/` and `data/`, plus the sounds and languages, all hash-verified), and Falcon's data tables (one zip of about 2.4 MB from Falcon's own GitHub release, checked against a SHA-256 the launcher carries), then starts Falcon with your account's name and a 1280x720 window. (The launcher's own window has no system title bar, it draws its own; the game keeps an ordinary one.)

The interface follows the layout of launchers such as Modrinth App: an icon sidebar (Home, Settings, and your account at the bottom), a Falcon page with an Overview, a Mods and a Logs tab, and a Settings page.

## Build

Needs Node.js (to build the page) and the usual Falcon toolchain.

```
cmake --preset release
cmake --build --preset launcher -j 3
```

The result is `dist-launcher\FalconLauncher.exe` (about 7 MB, statically linked). The first configure downloads the WebView2 SDK headers and loader from NuGet (`Microsoft.Web.WebView2` 1.0.4314.35, checked against a SHA-256 in `launcher/CMakeLists.txt`). The launcher builds the game first, because it embeds it.

Tests (no window, no network, a temporary folder): `build\launcher\falcon_launcher_tests.exe`. They cover the hashing and encodings, the sign-in address and the redirect it ends at, the Microsoft/Xbox/Minecraft chain against a fake server (including every Xbox error), every way the sign-in window can end, the skin-to-avatar cutting, the game-file install against a fake Mojang (including tampered downloads), the secure token store, the message API, and a whole first-run → sign-in → launch flow.

`FalconLauncher.exe --selftest-signin` checks the real sign-in window without showing it and without any network: it opens it on the redirect address, checks that the window catches it and closes, then opens another and cancels it. Exit code 0 when both work.

## How sign-in works, and what that means

The player signs in on `login.live.com` in a private browsing window of the launcher (nothing of the login is kept in the launcher's profile). When Microsoft sends the page on to `https://login.live.com/oauth20_desktop.srf?code=…`, the launcher catches that navigation before anything loads, takes the one-time code from the address, and trades it for tokens. From there: Xbox Live → XSTS → Minecraft services → entitlements and profile.

The client id is the **Minecraft launcher's own public id, `00000000402b5328`**. That is why no registration is needed, and it has consequences you should know:

- Microsoft's consent page names the app **"Minecraft"** (it is the official launcher's identity), not Falcon.
- Falcon is not an application Mojang has approved; it simply uses the id the official launcher uses, as other third-party launchers do. Mojang or Microsoft could change what that id accepts at any time. If you ship Falcon to others, registering your own application and getting it approved (<https://aka.ms/AppRegInfo>) is the way that stays supported.
- To use another id, set `FALCON_MS_CLIENT_ID`, or `msClientId` in `%LOCALAPPDATA%\Falcon\launcher.json`, or build with `-DFALCON_LAUNCHER_MS_CLIENT_ID=…`. Only ids that work with Microsoft's older account service (`login.live.com`, like the launcher's) are supported; Azure application ids (the long GUIDs) are not.
- The token is kept in `account.bin`, encrypted with Windows DPAPI for your user. Signing out deletes it.

## Mods

The **Mods** tab of the Falcon page searches **Modrinth** (its public API, <https://docs.modrinth.com>) for mods that list the game version (26.3), shows each one's icon, title, author, description, downloads and loaders, and has an **Install** button that puts the mod's .jar into `%LOCALAPPDATA%\Falcon\mods` (`~/.local/share/Falcon/mods` on Linux). A filter picks a loader (Fabric, NeoForge, Forge, Quilt), and "Open mods folder" shows the folder.

- **Which file:** the newest *release* of the project for the game version (the newest version of any kind when there is no release), and in it the .jar marked primary. A newer install of the same mod replaces the older file.
- **Checked:** the download address must be Modrinth's own file server (`cdn.modrinth.com`, https, no port or login part); the file's name is cut down to plain characters and must end in `.jar`; the size and the SHA-1 Modrinth names must match, else nothing is kept. It is downloaded under a temporary name and moved into place when it is good.
- **Icons** are fetched by the launcher (only from Modrinth's file server, 400 KB at most, only real pictures) and handed to the page as `data:` addresses, because the page may not load anything from the network itself.
- **The network does not hold the window:** the search and the install run on their own worker thread; `searchMods` and `installMod` answer through the event channel when Modrinth has answered, and the page ignores the answer of a search that a newer one replaced.
- **Optimization mods are not installed** (Sodium, Lithium, Iris, FerriteCore ...): they replace parts of the game that Falcon has written itself (the renderer, the lighting, the chunk loading), so they cannot work with it, now or later. The tab says so in a notice at the top, and the block is not one fixed list (`src/mods/optimization_mods.cpp`, shared with the game): a project is blocked when it is a known one by its id or slug, when Modrinth files it under the **Optimization** category (so a new one is caught without a new release), or when the version needs one (Iris and Sodium Extra need Sodium; the needed projects are asked for and judged the same way). It is checked in three places, so that no message to the launcher can get one through: the search marks the hit "Not compatible" (the Install button is replaced, with the reason), `installMod` asks Modrinth about the project and what the version needs before anything is downloaded, and after the download the jar's own metadata (its mod id and what it depends on, read from fabric.mod.json, quilt.mod.json, mods.toml or mcmod.info) is judged once more, which catches one under another name; then the file is deleted. A check that cannot be made (no answer from Modrinth) is a failed install: nothing is installed that was not checked. Optimization mods already in the mods folder (put there by hand) are listed in a warning on the tab and marked in the game's Mods screen.
- **What Falcon does with a mod:** nothing yet. Falcon is written in C++, not Java, so it cannot run a .jar mod; the tab puts the file where a mod loader would look for it, and says so.

## Where things live

`%LOCALAPPDATA%\Falcon\` (override with `FALCON_LAUNCHER_HOME`)

| Path | What |
| --- | --- |
| `client\<id>\Falcon.exe` | the bundled client, written during setup |
| `game\<version>\assets`, `data` | the game's files from Mojang |
| `game\<version>\*.json`, `*.nbt`, `biome-parameters\` | Falcon's data tables, from the zip on Falcon's GitHub release (`tables.json` holds the hash they came from) |
| `account.bin` | the sign-in token, encrypted with Windows DPAPI for your user |
| `launcher.json` | settings |
| `webview\` | the web view's profile |
| `mods\` | the .jar mods installed from the Mods tab |
| `mods.json` | which Modrinth project each of them is (so the tab can say "Installed") |
| `logs\launcher.log` | what the launcher did, with timings |
| `logs\latest.log` | the game's log while it runs, then `<date>_<time of launch>.log` (shown in the Logs tab; see docs/LOGS.md) |

## Command line

| Flag | Does |
| --- | --- |
| `--mock [state]` | Pretend services, no account needed. States: `setup`, `signin`, `waiting`, `home`, `missing`, `notowned`, `noprofile`, `installing`, `running`, `error` |
| `--screenshot <png> [--wait ms]` | Draw one state in an invisible window, save a PNG, quit |
| `--page settings\|logs` | Open the Settings page, or the Logs tab, first (for screenshots) |
| `--selftest-signin` | The invisible sign-in window check described above |
| `--selftest-mods <dir>` | The Mods tab's whole way with the real network: a search for mods of 26.3, their icons, and the install of the first one that has a .jar into `<dir>\mods`. Exit code 0 when it worked |
| `--devtools` | Enable the browser's developer tools (F12) |

Developing the page alone: `cd launcher/ui && npm install && npm run dev`. In a normal browser a fake host plays the launcher's part; put a state after the URL (`#home`, `#signin`, `#waiting`, `#installing`, `#error`, …), or `#settings` / `#logs` for those pages.

## Linux

The same launcher, with the same page, runs on Linux (x86-64). What is different:

- **The web view is WebKitGTK** (GTK 3, the package `libwebkit2gtk-4.1-0`; `sudo apt install libwebkit2gtk-4.1-0` on Debian and Ubuntu, `webkit2gtk4.1` on Fedora, `webkit2gtk-4.1` on Arch)
  instead of WebView2. A small script in front of the page gives it the two names it talks to the launcher with, so the page itself is the same file. The page is answered from memory to
  `falcon://app/index.html`, and nothing but it may be shown in that window (links open in the browser). No browser profile is kept on disk.
- **One file.** `FalconLauncher` carries the page, the Falcon client (the one that checks ownership) and the icon inside it, like `FalconLauncher.exe`; setup puts the client at
  `~/.local/share/Falcon/client/<id>/Falcon`, and adds an application menu entry (`~/.local/share/applications/falcon-launcher.desktop`, with the icon next to it) so the desktop can show it with
  its icon. It needs only the C library and the packages above; the C++ runtime and OpenSSL are inside it.
- **The window has no system title bar**, as on Windows: the page draws its own, and a press on it starts the window manager's own move (it works on X11 and Wayland). It is a fixed-size window.
  The Microsoft sign-in is a window of the usual kind with a title, as there.
- **Where the sign-in is kept:** Linux has nothing a program can call to encrypt for the current user (a keyring may or may not be running), so `account.bin` is a file in a folder only that user can open
  (mode 0600 in 0700): the refresh token as it is, which signing out deletes and which Microsoft can revoke. The official Minecraft launcher keeps its tokens in a file in the same way.
- **Folders:** `$XDG_DATA_HOME/Falcon`, which is `~/.local/share/Falcon` (`FALCON_LAUNCHER_HOME` overrides it, as on Windows). The game's files are downloaded there and passed to the client with `--assets`.
- **Network:** HTTP/1.1 over OpenSSL (no libcurl), with the system's certificates (looked for in the places the distributions put them; `SSL_CERT_FILE` and `SSL_CERT_DIR` work). It does not read the
  desktop's proxy settings: `HTTPS_PROXY` is not used yet.
- **Build:** `cmake --preset release -DFALCON_STATIC_OPENSSL=ON -DFALCON_BUILD_LAUNCHER=ON`, then `cmake --build --preset launcher` (needs `libgtk-3-dev`, `libwebkit2gtk-4.1-dev` and Node.js as well as the
  game's own packages, see the main README). The result is `dist-launcher/FalconLauncher`. The tests run the same way (`cmake --build --preset check`), on the fake network.

If the window comes up blank or the launcher crashes at once with some graphics drivers (NVIDIA on Wayland in particular), WebKitGTK's own switches help: `WEBKIT_DISABLE_DMABUF_RENDERER=1` or
`WEBKIT_DISABLE_COMPOSITING_MODE=1` in the environment, before starting it.

## What it costs

Measured on a Ryzen 5 5600G, on Windows:

- **Start-up:** the window and its splash are up in about 20 ms; the page is interactive about 0.3 s after launch.
- **Memory:** about **65 MB of private memory in total**: 2.7 MB for the launcher's own process, the rest in the six processes WebView2 needs (browser, renderer, utility). It is not as small as a fully native window would be. That is the price of using web code without shipping a browser. When minimised, the page is suspended and asked to give memory back.

## Known gaps

- The sign-in against the real Microsoft, Xbox and Mojang services is exercised here only against fakes: the first real sign-in on a machine is the real test. If Xbox rejects the way the Microsoft token is presented, the launcher tries the two usual ticket formats before it gives up.
- Falcon reads `--username` and `--uuid` for the chat name. There is no online play, so the access token is not passed to it.
