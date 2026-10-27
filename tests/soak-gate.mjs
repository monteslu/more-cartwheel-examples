// hordeshooter soak gate: 30 game minutes with the bots at 1, 2, 3 and 4 players, no window and no GPU
// (build/native/project/games/hordeshooter/hordeshooter_soak). Each run must exit 0 and end with "soak ok";
// resident memory must stay within 5% of the first minute; the control injects a NaN position and the
// checks must catch it (exit 1). Run: node tests/soak-gate.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/project/games/hordeshooter/hordeshooter_soak');
const run = (players, env = {}) => spawnSync(exe, ['30', String(players)], { encoding: 'utf8', timeout: 300000, env: { ...process.env, ...env } });
let bad = 0;
for (const p of [1, 2, 3, 4]) {
  const r = run(p);
  const ok = r.status === 0 && /soak ok: 30 minutes/.test(r.stdout);
  const mem = [...r.stdout.matchAll(/rss (\d+) kB \(first minute (\d+)\)/g)].map((m) => +m[1] / +m[2]);
  const growth = mem.length ? Math.max(...mem) : 99;
  if (!ok || growth > 1.05) { console.log(`FAIL soak ${p} players: status ${r.status}, memory x${growth.toFixed(3)}\n${r.stderr.slice(-400)}`); bad++; }
  else console.log(`ok   soak ${p} players (memory x${growth.toFixed(3)} at worst)`);
}
// levels rise: gems are collected and level the players up, from the first wave and from a late start
// (a run begun at wave n starts at about the level a played run has there). Control: no xp, must fail.
const levels = (args, env = {}) => {
  const r = spawnSync(exe, args, { encoding: 'utf8', timeout: 300000, env: { ...process.env, ...env } });
  const m = /levels (\d+) to (\d+)/.exec(r.stdout);
  return r.status === 0 && m ? +m[1] : -1;
};
for (const [args, want, what] of [[['3', '2', '1'], 4, 'wave 1, 3 minutes, 2 players'], [['2', '3', '14'], 15, 'wave 14 start, 2 minutes, 3 players']]) {
  const lo = levels(args);
  if (lo < want) { console.log(`FAIL levels: ${what}: lowest player level ${lo}, want >= ${want}`); bad++; }
  else console.log(`ok   levels rise: ${what}: lowest player level ${lo} (>= ${want})`);
}
const nx = levels(['3', '2', '1'], { HORDE_SOAK_NOXP: '1' });
if (nx >= 4) { console.log('CONTROL PASSED (a failure): levels rose with no xp'); bad++; } else console.log(`ok   control: no xp leaves the lowest level at ${nx}`);
const c = run(2, { HORDE_SOAK_INJECT: '1' });
if (c.status === 0) { console.log('CONTROL PASSED (a failure): the injected NaN was not caught'); bad++; } else console.log('ok   control caught the injected NaN');
process.exit(bad ? 1 : 0);
