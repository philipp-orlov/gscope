#!/usr/bin/env node
// Regenerates what the dashboard takes from librpc: the TypeScript for ../backend/include/gscope/wire/monitor.bufs
// (rpcc --ts) into src/app/core/wire, and a copy of librpc's TypeScript runtime (with the libbufs reader and builder
// it re-exports) into src/app/core/librpc. The same scheme as libsql's tools/sql/ui/scripts/vendor-librpc.mjs.
//   node scripts/vendor-librpc.mjs path/to/rpcc [--check]
// With --check nothing is written: it exits 1 if the committed files differ (the ctest gscope_frontend_wire_up_to_date).
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const frontend = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const gscope = path.join(frontend, '..');
const rpcc = process.argv[2];
const check = process.argv.includes('--check');
if (!rpcc) {
  console.error('usage: node scripts/vendor-librpc.mjs path/to/rpcc [--check]');
  process.exit(2);
}
const schema = path.join(gscope, 'backend/include/gscope/wire/monitor.bufs');
const runtime = path.join(gscope, '../librpc/ts/src');
const bufsRuntime = path.join(gscope, '../libbufs/ts/src');
const out = check ? fs.mkdtempSync(path.join(os.tmpdir(), 'gscope-wire-')) : path.join(frontend, 'src/app/core');
fs.mkdirSync(path.join(out, 'wire'), { recursive: true });
fs.mkdirSync(path.join(out, 'librpc'), { recursive: true });
const r = spawnSync(rpcc, ['--no-cpp', '--ts', '--ts-runtime', '../librpc/index', '-o', path.join(out, 'wire'), schema], { stdio: 'inherit' });
if (r.status !== 0) process.exit(1);
for (const f of ['bufs', 'client', 'rpc.bufs', 'index']) {
  const from = f === 'bufs' ? bufsRuntime : runtime;   // librpc/ts/src/bufs.ts re-exports libbufs/ts/src/bufs.ts
  const text = `// Vendored from ${f === 'bufs' ? 'libbufs/ts' : 'librpc/ts'}/src by scripts/vendor-librpc.mjs. Do not edit here.\n` + fs.readFileSync(path.join(from, f + '.ts'), 'utf8');
  fs.writeFileSync(path.join(out, 'librpc', f + '.ts'), text);
}
if (check) {
  let stale = false;
  for (const dir of ['wire', 'librpc']) {
    for (const f of fs.readdirSync(path.join(out, dir))) {
      const committed = path.join(frontend, 'src/app/core', dir, f);
      if (!fs.existsSync(committed) || fs.readFileSync(committed, 'utf8') !== fs.readFileSync(path.join(out, dir, f), 'utf8')) {
        console.error(`stale: src/app/core/${dir}/${f}`);
        stale = true;
      }
    }
  }
  fs.rmSync(out, { recursive: true, force: true });
  if (stale) {
    console.error('run: node scripts/vendor-librpc.mjs path/to/rpcc');
    process.exit(1);
  }
}
