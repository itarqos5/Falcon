// Prints, as JSON {java, args}, the command that starts the vanilla Minecraft 26.3 client from horizon-launcher's minecraft folder on this PC (its libraries and client jar):
//   node vanilla_command.mjs <game dir> [world name]
// Used by run_vanilla.ps1 to measure vanilla next to Falcon. Needs a Java 25 (the one under Program Files\Java, or JAVA_HOME).
import fs from 'node:fs';
import path from 'node:path';
const root = path.join(process.env.APPDATA, 'horizon-launcher', 'minecraft');
const version = JSON.parse(fs.readFileSync(path.join(root, 'versions', '26.3', '26.3.json'), 'utf8'));
const gameDir = process.argv[2];
const world = process.argv[3] || 'New World';
const ruleOk = (rules) => {
  if (!rules) return true;
  let ok = false;
  for (const r of rules) {
    const matches = !r.os || (r.os.name === 'windows' && (!r.os.arch || r.os.arch === 'x86_64' || r.os.arch === 'x64'));
    if (matches) ok = r.action === 'allow';
  }
  return ok;
};
const cp = [];
for (const lib of version.libraries) {
  if (!ruleOk(lib.rules)) continue;
  const a = lib.downloads && lib.downloads.artifact;
  if (!a) continue;
  const p = path.join(root, 'libraries', a.path);
  if (fs.existsSync(p)) cp.push(p);
  else console.error('missing library', a.path);
}
cp.push(path.join(root, 'versions', '26.3', '26.3.jar'));
// the official launcher's default heap (-Xmx2G), the default garbage collector
// (The services of Mojang that a vanilla client asks for a profile are pointed at a closed port: with the made-up token of this test they answer 401/429 slowly, which would be measured as
// start-up time; refused at once, the game carries on as it does offline.)
const closed = 'http://127.0.0.1:9';
const args = ['-Xmx2G', '-Dminecraft.api.env=custom', '-Dminecraft.api.auth.host=' + closed, '-Dminecraft.api.account.host=' + closed, '-Dminecraft.api.session.host=' + closed,
  '-Dminecraft.api.services.host=' + closed, '-cp', cp.join(';'), version.mainClass,
  '--username', 'Bench', '--version', '26.3', '--gameDir', gameDir, '--assetsDir', path.join(root, 'assets'), '--assetIndex', String(version.assets),
  '--uuid', '00000000000000000000000000000001', '--accessToken', '0', '--clientId', '0', '--xuid', '0', '--versionType', 'release',
  '--width', '854', '--height', '480', '--quickPlaySingleplayer', world];
const java = process.env.JAVA_HOME ? path.join(process.env.JAVA_HOME, 'bin', 'javaw.exe') : 'C:/Program Files/Java/jdk-25/bin/javaw.exe';
console.log(JSON.stringify({ java, args }));
