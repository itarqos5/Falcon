// Splits a bundle file ("=== path/File.java ===" lines between the sources) into modapi/src (the game, the loader's API and Falcon's own glue) or modapi/jdk (the parts of the JDK the VM has no
// native class for, compiled with --patch-module java.base so that they keep the JDK's names): node modapi/split.mjs <bundle.txt> [src|jdk|auto]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const [, , bundle, kind = 'src'] = process.argv;
const text = fs.readFileSync(bundle, 'utf8').replace(/\r\n/g, '\n');
const parts = text.split(/^=== (.+?) ===$/m);
let count = 0;
for (let i = 1; i < parts.length; i += 2) {
  const rel = parts[i].trim();
  const target = kind === 'auto' ? (rel.startsWith('java/') || rel.startsWith('javax/') ? 'jdk' : 'src') : kind;
  const file = path.join(here, target, rel);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, parts[i + 1].replace(/^\n/, ''));
  count++;
}
console.log(`${count} file(s) written to modapi/${kind === 'auto' ? 'src and jdk' : kind}`);
