// Everything the page states as a number lives here, with where it comes from. The measurements are those of docs/vs-vanilla.md (docs/benchmarks/vs-vanilla.json) and
// docs/performance.md; change them together with those files.

export const VERSION = '1.2.1';
export const REPO = 'https://github.com/itarqos5/Falcon';
export const RELEASE = 'https://github.com/itarqos5/Falcon/releases/latest';
export const LAUNCHER_SIZE = '9.5 MB';

// Seconds from starting the program to being in a new world (render distance 8, same seed).
export const startup = {
  vanilla: { seconds: 15.4, runs: '15.2 s and 15.6 s' },
  falconFirst: { seconds: 9.1, runs: '9.2 s, 9.2 s and 9.1 s: its caches of blocks, shapes and recipes are built' },
  falconLater: { seconds: 5.3, runs: '5.3 s and 5.3 s' },
  scale: 16,
};

// Megabytes of working set, 30 seconds after the world is ready (render distance 8).
export const memory = {
  vanilla: { mb: 1484, privateMb: 2296 },
  falcon: { mb: 245, privateMb: 370 },
  falcon16: { mb: 392, privateMb: 521 },
  scale: 1500,
};

export const startupRatio = Math.round((startup.vanilla.seconds / startup.falconLater.seconds) * 10) / 10; // 2.9
export const memoryRatio = Math.round((memory.vanilla.mb / memory.falcon.mb) * 10) / 10; // 6.1

export const caveats = [
  {
    title: 'Vanilla does more.',
    text: 'It generates structures and every feature of the world, runs mobs (animals were in view), particles and the server tick. Falcon’s singleplayer has none of those yet, so part of the difference is work Falcon does not do.',
  },
  {
    title: 'No frame rate comparison.',
    text: 'Vanilla’s frames per second were not measured, so there is no claim about them. Falcon’s frame times are only compared with Falcon’s own earlier renderer.',
  },
  {
    title: 'How “in the world” was timed.',
    text: 'Vanilla: until its log says the player has logged in, a moment before the first picture, which favours vanilla slightly. Falcon: until it says the spawn area is loaded.',
  },
  {
    title: 'Different windows.',
    text: 'Vanilla ran in a visible 854×480 window, Falcon in a hidden window of that size (start-up and memory depend little on that; a frame rate would). Falcon’s player stood where vanilla’s spawned.',
  },
  {
    title: 'Few runs, one PC.',
    text: 'Two vanilla runs for the start, one for memory. Ryzen 5 5600G, Radeon RX 580 2048SP, 16 GB, Windows 10, Java 25, vanilla with the official launcher’s default ‑Xmx2G. These show the size of the difference, not exact figures.',
  },
  {
    title: 'Windows only.',
    text: 'Everything measured on this page was measured on Windows. The Linux build has not been benchmarked.',
  },
  {
    title: 'Same options.',
    text: 'Vsync off, frame cap off, clouds off, minimal particles, no entity shadows, same seed. The scripts and the full method are in the repository.',
  },
];

export const features = [
  { title: 'The game’s own menus', text: 'Title screen, world list, options, resource packs, the pause menu: drawn from the game’s own pictures, sizes and layout rules.' },
  { title: 'Singleplayer worlds', text: 'Terrain from a port of the game’s world generator (the same seed gives the same land), caves and ores, survival and creative, crafting, chests, furnaces, elytra and food.' },
  { title: 'Multiplayer', text: 'Joins servers of version 26.3 in online or offline mode. Other players are drawn as the game draws them: skins, capes, armor, elytra, swimming, sleeping, crossbows.' },
  { title: 'A small launcher', text: 'Signs you in with Microsoft, checks that you own the game, downloads the game’s files from Mojang, and starts Falcon. About 10 MB on Windows and 17 MB on Linux (the game client is inside it).' },
];

export const shots = [
  { src: '/world.jpg', alt: 'A snowy hill above a lake in a Falcon singleplayer world', caption: 'A singleplayer world at render distance 14' },
  { src: '/valley.jpg', alt: 'A valley in Falcon', caption: 'A valley from the world generator' },
  { src: '/title.jpg', alt: 'Falcon’s title screen', caption: 'The title screen' },
  { src: '/chasm.jpg', alt: 'A chasm in Falcon', caption: 'Caves and a chasm' },
];

export const missing = [
  'Mobs, villagers and trading, structures, day and night, weather and clouds in singleplayer',
  'The anvil, enchanting table, brewing stand and most other windows',
  'The effects of potions and food, and the crumbs when eating',
  'Redstone beyond levers, doors and buttons',
  'Steering mounts other than boats in multiplayer; multiplayer has so far been tried mostly against a test server in offline mode',
  'macOS: not yet (Windows and Linux only for now)',
];

export const faq = [
  { q: 'Is Falcon made by Mojang or Microsoft?', a: 'No. It is an independent project, not approved by or connected to Mojang or Microsoft. “Minecraft” is a trademark of Mojang Synergies AB.' },
  {
    q: 'Which systems does it run on?',
    a: 'Windows 10 and 11, and 64-bit Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36 and later, current Arch). The Linux launcher shows its window with WebKitGTK 4.1 (on Debian and Ubuntu: sudo apt install libwebkit2gtk-4.1-0). There is no macOS version.',
  },
  { q: 'Do I need to buy Minecraft?', a: 'Yes. The launcher only works for a Microsoft account that owns Minecraft: Java Edition, and the client checks it again before it opens.' },
  {
    q: 'What does the launcher download?',
    a: 'The game’s pictures, sounds and languages from Mojang’s own servers, for your account, and Falcon’s data tables (about 2.4 MB, a zip on this project’s GitHub release) which it checks against a hash it carries. The tables are derived from the game’s data: block and item numbers, shapes, facts. No textures, sounds or code.',
  },
  { q: 'Why does the sign-in page say “Minecraft”?', a: 'Falcon uses the Minecraft launcher’s public sign-in identity, as other third-party launchers do, so no registration is needed. Falcon is not an approved application of Mojang’s, and Mojang could change what that identity accepts.' },
  { q: 'Can I use it on a server?', a: 'It joins servers of version 26.3. Some servers kick unknown clients and anti-cheat plugins may object to it; use it at your own risk.' },
  { q: 'Where is the code?', a: 'On GitHub, under the Apache 2.0 licence.' },
];
