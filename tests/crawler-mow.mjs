// crawler's mowing rules (DESIGN.md, the crowd): a corridor is a line to shoot through. One hero of each class holds
// fire into a charging line of 10 of the floor's own light kinds at their strengths (crawler_soak --mow: an open room
// of the tileset, nobody hurt) on floors 18, 58 and 118, and must cut it down within 3.2 s.
// Control: CRAWLER_OLD_MOW=1 (the monsters' health, shots and spawners as before) must leave a line standing past
// that on every one of those floors.
// And the furthest-tileset offer is fair: two bots starting fresh on floor 101 with what the offer gives
// (CRAWLER_HEAD_START=1: an elixir a player for each tileset passed by, two potions; the tileset's first floor a
// breather) clear it without a fall, losing at most half the health a fresh pair loses under the old rules (the
// control, CRAWLER_OLD_MOW=1 with no head start, which must not meet that bound).
//   node tests/crawler-mow.mjs
import { spawnSync } from 'node:child_process';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const exe = join(ROOT, 'build/native/games/crawler/crawler_soak');
let bad = 0;
for (const floor of [18, 58, 118]) {
  const r = spawnSync(exe, ['--mow', String(floor)], { encoding: 'utf8' });
  const c = spawnSync(exe, ['--mow', String(floor)], { encoding: 'utf8', env: { ...process.env, CRAWLER_OLD_MOW: '1' } });
  const times = (out) => [...(out || '').matchAll(/mow floor \d+ (\w+): ([\d.]+) s/g)].map((m) => `${m[1]} ${m[2]}`).join(', ');
  if (r.status === 0 && times(r.stdout).split(',').length === 4) console.log(`ok   floor ${floor}: ${times(r.stdout)}`);
  else { console.log(`FAIL floor ${floor} (status ${r.status}): ${times(r.stdout)}`); bad++; }
  if (c.status === 1) console.log(`ok   control fails as it must, floor ${floor} as before: ${times(c.stdout)}`);
  else { console.log(`CONTROL PASSED (a failure), floor ${floor} as before: ${times(c.stdout)} (status ${c.status})`); bad++; }
}
const clear = (env) => {
  const r = spawnSync(exe, ['--clear', join(ROOT, 'games/crawler/assets/levels/101.cwmaze'), '2'], { encoding: 'utf8', env: { ...process.env, ...env } });
  const m = /cleared in ([\d.]+) s .*health lost (\d+), falls (\d+)/.exec(r.stdout || '');
  return { ok: r.status === 0, line: (r.stdout || '').trim().split('\n').slice(-2).join(' | '), secs: m ? +m[1] : NaN, lost: m ? +m[2] : Infinity, falls: m ? +m[3] : 99, firstTry: !/on try/.test(r.stdout || '') };
};
const head = clear({ CRAWLER_HEAD_START: '1' }), old = clear({ CRAWLER_OLD_MOW: '1' });
const fair = (r, bound) => r.ok && r.firstTry && r.falls === 0 && r.lost <= bound;
const bound = Number.isFinite(old.lost) ? old.lost / 2 : 0;
if (fair(head, bound)) console.log(`ok   floor 101 from the offer: ${head.line} (bound ${bound} health)`);
else { console.log(`FAIL floor 101 from the offer: ${head.line} (bound ${bound} health)`); bad++; }
if (!fair(old, bound)) console.log(`ok   control fails as it must, a fresh pair under the old rules: ${old.line}`);
else { console.log(`CONTROL PASSED (a failure): ${old.line}`); bad++; }
process.exit(bad ? 1 : 0);
