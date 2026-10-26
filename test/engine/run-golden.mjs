// Run the engine's golden vectors in WebAssembly and compare with golden.txt.
//
//   node test/engine/run-golden.mjs            compare, exit 1 on any difference
//   node test/engine/run-golden.mjs --write    replace golden.txt with this run
//
// Build first with scripts/build-engine-wasm.sh.
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const goldenPath = join(here, 'golden.txt');
const { default: load } = await import(join(here, '../../build/wasm/nl_engine.mjs'));
const m = await load();

const size = 8192;
const buf = m._malloc(size);
const len = m._nl_selftest(buf, size);
const got = m.UTF8ToString(buf, len);
m._free(buf);

if (process.argv.includes('--write')) {
  writeFileSync(goldenPath, got);
  console.log(got.trimEnd());
  console.log(`wrote ${goldenPath}`);
  process.exit(0);
}
if (!existsSync(goldenPath)) {
  console.log(got.trimEnd());
  console.error('no golden.txt yet - run with --write once the output is checked');
  process.exit(1);
}
const want = readFileSync(goldenPath, 'utf8');
const w = new Map(want.trim().split('\n').map(l => l.split(' ')));
let bad = 0;
for (const line of got.trim().split('\n')) {
  const [name, crc] = line.split(' ');
  const ok = w.get(name) === crc;
  if (!ok) bad++;
  console.log(`${ok ? 'OK  ' : 'FAIL'} ${name} ${crc}${ok ? '' : ` (want ${w.get(name)})`}`);
  w.delete(name);
}
for (const name of w.keys()) { bad++; console.log(`FAIL ${name} missing`); }
process.exit(bad ? 1 : 0);
