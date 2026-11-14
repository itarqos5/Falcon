# Falcon Launcher

A small native Windows launcher for Falcon, written in C++20 with a React interface.

- **No browser is bundled.** The interface is a React + TypeScript page, built into one HTML file, embedded in the exe and shown by the **WebView2 runtime that Windows already has** (it ships with Windows 11 and with Microsoft Edge on Windows 10). If it is missing, the first-run setup offers to install it.
- **Falcon is bundled.** `FalconLauncher.exe` carries `Falcon.exe` inside it and puts it in `%LOCALAPPDATA%\Falcon\client\` during setup. Nothing else to download for the client.
- **Its own title bar.** The window has no system frame: a custom title bar drags the window, with minimise and close buttons, and Windows still draws the shadow and rounded corners.
- **Sign-in is Microsoft's own page**, opened in a window of the launcher with the Minecraft launcher's public client id (see below). No code to type, no Azure registration. Your password never goes through Falcon.
- **Checks that you own Minecraft: Java Edition** before it does anything else, and shows your skin's head and your player name.
- **Pressing Play** downloads the game's assets from Mojang the first time (the client jar's `assets/` and `data/`, plus the sounds and languages, all hash-verified), then starts Falcon with your account's name and a 1280x720 window. (The launcher's own window has no system title bar, it draws its own; the game keeps an ordinary one.)

The interface follows the layout of launchers such as Modrinth App: an icon sidebar (Home, Settings, and your account at the bottom), a Falcon page with an Overview and a Logs tab, and a Settings page.

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

## Where things live

`%LOCALAPPDATA%\Falcon\` (override with `FALCON_LAUNCHER_HOME`)

| Path | What |
| --- | --- |
| `client\<id>\Falcon.exe` | the bundled client, written during setup |
| `game\<version>\assets`, `data` | the game's files from Mojang |
| `account.bin` | the sign-in token, encrypted with Windows DPAPI for your user |
| `launcher.json` | settings |
| `webview\` | the web view's profile |
| `logs\launcher.log` | what the launcher did, with timings (shown in the Logs tab) |

## Command line

| Flag | Does |
| --- | --- |
| `--mock [state]` | Pretend services, no account needed. States: `setup`, `signin`, `waiting`, `home`, `missing`, `notowned`, `noprofile`, `installing`, `running`, `error` |
| `--screenshot <png> [--wait ms]` | Draw one state in an invisible window, save a PNG, quit |
| `--page settings\|logs` | Open the Settings page, or the Logs tab, first (for screenshots) |
| `--selftest-signin` | The invisible sign-in window check described above |
| `--devtools` | Enable the browser's developer tools (F12) |

Developing the page alone: `cd launcher/ui && npm install && npm run dev`. In a normal browser a fake host plays the launcher's part; put a state after the URL (`#home`, `#signin`, `#waiting`, `#installing`, `#error`, …), or `#settings` / `#logs` for those pages.

## What it costs

Measured on a Ryzen 5 5600G:

- **Start-up:** the window and its splash are up in about 20 ms; the page is interactive about 0.3 s after launch.
- **Memory:** about **65 MB of private memory in total**: 2.7 MB for the launcher's own process, the rest in the six processes WebView2 needs (browser, renderer, utility). It is not as small as a fully native window would be. That is the price of using web code without shipping a browser. When minimised, the page is suspended and asked to give memory back.

## Known gaps

- **The game's gameplay data.** Falcon's menus and its look come from the game's own `assets/`, which the launcher downloads. Its singleplayer world also reads block, item, shape and recipe tables (`registries.json`, `gameplay-facts.json`, `model-states.json`, …) that are exported from a Minecraft install by horizon-launcher. Mojang's downloads do not contain them, so the launcher **looks for the newest complete export on the PC** (`%APPDATA%\horizon-launcher\versions\*\.local\horizon-client\<version>`, or the launcher's own `game\<version>` folder) and starts the game with `--data` pointing at it. On a PC that has one, worlds open. On a PC without one, the launcher says so on the Falcon page and opening a world shows a "Could not open the world" message until Falcon derives that data itself.
- The sign-in against the real Microsoft, Xbox and Mojang services is exercised here only against fakes: the first real sign-in on a machine is the real test. If Xbox rejects the way the Microsoft token is presented, the launcher tries the two usual ticket formats before it gives up.
- Falcon reads `--username` and `--uuid` for the chat name. There is no online play, so the access token is not passed to it.
