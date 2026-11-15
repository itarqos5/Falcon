// Reads or sets the seed of a world, so that vanilla and Falcon can be given the same one.
//   node world_seed.mjs get <gz NBT file>          prints the first seed in it (vanilla 26.3 keeps it in data/minecraft/world_gen_settings.dat, Falcon in level.dat)
//   node world_seed.mjs set <gz NBT file> <seed>    rewrites the seed(s) in it
// (A TAG_Long named "seed" is looked for in the uncompressed NBT: it is where both games keep it.)
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

function findLong(buf, name) {
  const pattern = Buffer.concat([Buffer.from([0x04, 0x00, name.length]), Buffer.from(name, 'latin1')]);
  const at = buf.indexOf(pattern);
  return at < 0 ? -1 : at + pattern.length;
}

const [mode, target, value] = process.argv.slice(2);
if (mode === 'get') {
  const file = target;
  const raw = zlib.gunzipSync(fs.readFileSync(file));
  const at = findLong(raw, 'seed');
  if (at < 0) throw new Error('no seed in ' + file);
  console.log(raw.readBigInt64BE(at).toString());
} else if (mode === 'set') {
  const raw = zlib.gunzipSync(fs.readFileSync(target));
  let n = 0;
  for (const name of ['seed', 'RandomSeed']) {
    let from = 0;
    for (;;) {
      const pattern = Buffer.concat([Buffer.from([0x04, 0x00, name.length]), Buffer.from(name, 'latin1')]);
      const at = raw.indexOf(pattern, from);
      if (at < 0) break;
      raw.writeBigInt64BE(BigInt(value), at + pattern.length);
      from = at + pattern.length + 8;
      ++n;
    }
  }
  if (n === 0) throw new Error('no seed in ' + target);
  fs.writeFileSync(target, zlib.gzipSync(raw));
  console.log('set ' + n + ' seed field(s)');
} else {
  throw new Error('get <file> | set <file> <seed>');
}
