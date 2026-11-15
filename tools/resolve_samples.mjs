// Turns the samples of FALCON_SAMPLE (the profiler of falcon/platform/sampling_profiler.hpp, also falcon_bench_chunks --sample) into the list of functions the time went to.
//
//   node tools/resolve_samples.mjs <samples file> <exe> [lines per thread, default 20] [only this thread]
//
// A sample line is "<thread name> exe <offset>" (the program itself: matched to the function whose symbol is the nearest one below it, with the names from MinGW's nm, so
// the exe must not be stripped: configure with -DFALCON_STRIP=OFF) or "<thread name> dll <module>" (any other module: the graphics driver, the C runtime, ntdll where a thread
// waits ...). Each thread name (main, worker, ...) gets its own list; the percentages are of that name's samples.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';

const [, , samplesFile, exe, linesArg, onlyThread] = process.argv;
if (!samplesFile || !exe) {
    console.error('usage: node tools/resolve_samples.mjs <samples file> <exe> [lines] [thread]');
    process.exit(2);
}
const imageBase = 0x140000000n;  // (MinGW-w64's default for a 64-bit program; objdump -p shows it)

const nm = spawnSync('C:/mingw/bin/nm.exe', ['-n', '-C', '--defined-only', exe], { encoding: 'utf8', maxBuffer: 1 << 28 });
if (nm.status !== 0) {
    console.error(nm.stderr);
    process.exit(1);
}
const symbols = [];  // [address, name], ascending (nm -n)
for (const line of nm.stdout.split(/\r?\n/)) {
    const m = /^([0-9a-f]{16}) [TtWwiI] (.+)$/.exec(line);
    if (m) symbols.push([BigInt('0x' + m[1]), m[2]]);
}
if (symbols.length === 0) console.error('(no symbols in ' + exe + ': it is stripped; configure with -DFALCON_STRIP=OFF)');
const find = (address) => {
    let lo = 0, hi = symbols.length - 1, best = -1;
    while (lo <= hi) {
        const mid = (lo + hi) >> 1;
        if (symbols[mid][0] <= address) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best < 0 ? '??' : symbols[best][1];
};

const threads = new Map();  // name -> { total, byFunction }
for (const line of fs.readFileSync(samplesFile, 'utf8').split(/\r?\n/)) {
    const m = /^(\S+) (exe|dll) (.+)$/.exec(line);
    if (!m) continue;
    const [, thread, kind, what] = m;
    if (onlyThread && thread !== onlyThread) continue;
    let t = threads.get(thread);
    if (!t) threads.set(thread, (t = { total: 0, byFunction: new Map() }));
    const name = kind === 'exe' ? find(BigInt('0x' + what) + imageBase) : `[${what}]`;
    t.byFunction.set(name, (t.byFunction.get(name) ?? 0) + 1);
    ++t.total;
}
for (const [thread, t] of threads) {
    console.log(`\n== ${thread}: ${t.total} samples`);
    const rows = [...t.byFunction.entries()].sort((a, b) => b[1] - a[1]).slice(0, Number(linesArg ?? 20));
    for (const [name, n] of rows) console.log(`${((n * 100) / t.total).toFixed(1).padStart(5)}%  ${name.length > 150 ? name.slice(0, 147) + '...' : name}`);
}
