#!/usr/bin/env node
// For each fighter gate that pins sim.hash: plays its pad script through tests/gatereplay.c and compares with the pin.
// No window, no GPU; what a matching hash says is that the rules still play that script the same, not that it renders.
// Usage: gatereplay.mjs <gatereplay binary> <moves.txt> <scratch dir> [--expect-mismatch]
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const [bin, moves, scratch, flag] = process.argv.slice(2);
const root = join(dirname(fileURLToPath(import.meta.url)), '../../../tests');
const gates = JSON.parse(readFileSync(join(root, 'fighter-gates.json'), 'utf8'));
// the pad's buttons in main.c's default bindings: west punch, south kick, east block, north punch + kick
const BTN = { west: 1, south: 2, east: 4, north: 3 };
const DIRS = ['left', 'right', 'up', 'down'];

function pad(p) {
  const b = p?.buttons ?? [];
  let [l, r, u, d] = DIRS.map((k) => b.includes(k));
  let m = 0;
  for (const k of b) {
    if (BTN[k] !== undefined) m |= BTN[k];
    else if (!DIRS.includes(k)) throw new Error(`button ${k} is not mapped here`);
  }
  // stick8 in main.c: the radial dead zone, then the 8-way gate
  const x = p?.axes?.lx ?? 0, y = -(p?.axes?.ly ?? 0), ax = Math.abs(x), ay = Math.abs(y);
  if (ax * ax + ay * ay >= 0.35 * 0.35) {
    const horiz = ax > 0 && ay <= ax * 0.466, vert = ay > 0 && ax <= ay * 0.466;
    if (!vert) { l ||= x < 0; r ||= x > 0; }
    if (!horiz) { u ||= y > 0; d ||= y < 0; }
  }
  return [l, r, u, d].map(Number).concat(m).join(' ');
}

let same = 0, moved = 0, skipped = 0;
for (const g of gates) {
  const pin = g.expect?.['sim.hash'];
  if (pin === undefined) continue;
  let events = '-';
  try {
    if (g.input) {
      const script = JSON.parse(readFileSync(join(root, g.input), 'utf8'));
      if (script.some((e) => e.keys?.length || e.pad2?.keys?.length)) throw new Error('a keyboard script, and main.c binds the keys');
      events = join(scratch, `${g.name}.events`);
      writeFileSync(events, script.map((e) => `${e.frame} ${pad(e)} ${pad(e.pad2)}`).join('\n') + '\n');
    }
  } catch (err) { console.log(`  ${g.name}: not replayed (${err.message})`); skipped++; continue; }
  const hash = Number(execFileSync(bin, [moves, events, String(g.frames), g.app === 'fighter_auto' ? '1' : '0', g.app === 'fighter_train' ? '1' : '0']).toString());
  if (hash === pin) same++; else moved++;
  console.log(`  ${g.name}: pin ${pin}, replay ${hash} ${hash === pin ? 'same' : 'MOVED'}`);
}
console.log(`gatereplay: ${same} same, ${moved} moved, ${skipped} not replayed`);
if (flag === '--expect-mismatch') process.exit(moved > 0 ? 0 : 1);
process.exit(moved > 0 ? 1 : 0);
