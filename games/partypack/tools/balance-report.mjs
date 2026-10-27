// Reads out/balance/*.log (tools/balance.sh) and prints the balance numbers the fun rubric asks for:
//   node games/partypack/tools/balance-report.mjs out/balance
import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

const dir = process.argv[2] || 'out/balance';
const games = [], tourns = [];
for (const f of readdirSync(dir).filter((n) => n.endsWith('.log'))) {
  for (const line of readFileSync(join(dir, f), 'utf8').split('\n')) {
    const kv = (l) => Object.fromEntries(l.split(' ').slice(1).map((p) => p.split('=')));
    if (line.startsWith('GAME ')) games.push({ cfg: f.replace('.log', ''), ...kv(line) });
    else if (line.startsWith('TOURN ')) tourns.push({ cfg: f.replace('.log', ''), ...kv(line) });
  }
}
const nums = (s) => s.split(',').map(Number);
const pct = (a, b) => (b ? (100 * a / b).toFixed(1) + '%' : 'n/a');
console.log(`games ${games.length}, tournaments ${tourns.length}`);

// PP3: length of play per game, in seconds (60 ticks a second)
const byGame = {};
for (const g of games) (byGame[g.id] ||= []).push(g);
console.log('\nPP3 play length (s): min / median / max, and PP5 tight finish (top two within 15% of the leader)');
let tightAll = 0, nAll = 0;
for (const [id, gs] of Object.entries(byGame)) {
  const t = gs.map((g) => +g.ticks / 60).sort((a, b) => a - b);
  let tight = 0;
  for (const g of gs) {
    const sc = nums(g.scores).sort((a, b) => b - a);
    if (sc[0] <= 0 || (sc[0] - sc[1]) / sc[0] <= 0.15) tight++;
  }
  tightAll += tight; nAll += gs.length;
  console.log(`  ${id.padEnd(8)} ${t[0].toFixed(1)} / ${t[t.length >> 1].toFixed(1)} / ${t[t.length - 1].toFixed(1)}   tight ${pct(tight, gs.length)}  (n=${gs.length})`);
}
console.log(`  all: tight ${pct(tightAll, nAll)}`);

// PP10 mirror seats: all-mid, win rate per seat
const mid = tourns.filter((t) => t.cfg === 'mid' || t.cfg === 'mid-long');
const seatWins = [0, 0, 0, 0];
for (const t of mid) seatWins[+t.winner]++;
console.log(`\nPP10 mirror seats (all mid, ${mid.length} tournaments): ` + seatWins.map((w) => pct(w, mid.length)).join(' '));

// PP9 skill vs luck: seat 0 hard against three mid
const hv = tourns.filter((t) => t.cfg === 'hard-vs-mid');
console.log(`PP9 hard bot vs three mid: wins ${pct(hv.filter((t) => +t.winner === 0).length, hv.length)} (n=${hv.length}; the fair share is 25%)`);
const mx = tourns.filter((t) => t.cfg === 'mixed');
if (mx.length) console.log(`   mixed 2,1,0,1: wins per seat ` + [0, 1, 2, 3].map((s) => pct(mx.filter((t) => +t.winner === s).length, mx.length)).join(' '));

// per game skill ladder: average place per skill from the mixed run
console.log('\nskill ladder (mixed run, average place 0 best, by skill level) per game:');
const lad = {};
for (const g of games.filter((g) => g.cfg === 'mixed')) {
  const pl = nums(g.places);
  for (let s = 0; s < 4; s++) { const sk = +g.skills[s]; ((lad[g.id] ||= {})[sk] ||= []).push(pl[s]); }
}
for (const [id, m] of Object.entries(lad))
  console.log(`  ${id.padEnd(8)} ` + Object.entries(m).map(([sk, a]) => `skill ${sk}: ${(a.reduce((x, y) => x + y, 0) / a.length).toFixed(2)}`).join('   '));

// PP7 comeback: last after two rounds wins the match
let lastWins = 0, lastN = 0;
for (const t of tourns.filter((t) => t.cfg === 'mid' || t.cfg === 'mid-long' || t.cfg === 'mixed')) {
  const a = nums(t.after2), min = Math.min(...a);
  const lasts = a.map((v, i) => (v === min ? i : -1)).filter((i) => i >= 0);
  if (lasts.length === 1) { lastN++; if (+t.winner === lasts[0]) lastWins++; }
}
console.log(`\nPP7 last after round 2 wins the match: ${pct(lastWins, lastN)} (n=${lastN}; aim 15% to 30%)`);
const ties = tourns.filter((t) => t.tie !== 'no').length;
console.log(`tie for first on points: ${pct(ties, tourns.length)} of tournaments (decided by round wins, then seat order, shown on screen)`);
