// Frame data from assets/data/moves.txt: a table per fighter, with the same
// numbers the game's training mode measures (startup, active, recovery, frame
// advantage on hit and on block). Also a checker: unknown keys, missing active
// frames, boxes the wrong way round. Exits 1 on a problem.
//
//   node tools/framedata.mjs [file] [--fighter id] [--json] [--csv]
//
// The advantage formula is the simulation's: stun - (active - 1 + recovery),
// measured from a hit on the first active frame (tests/simtest.c checks the
// simulation against it, move by move).
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
const flag = (n) => args.includes(n);
const arg = (n) => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : null; };
const file = args.find((a) => !a.startsWith('--') && a !== arg('--fighter')) ?? join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'data', 'moves.txt');
const SLOTS = ['5P', '8P', '2P', '6P', '5K', '8K', '2K', '6K', 'JP', 'JK', 'THROW', 'SP1', 'SP2', 'SP3', 'SUPER'];
// the names sim.c gives a move the file does not name
const DEFAULT_NAME = { '5P': 'Body punch', '8P': 'High punch', '2P': 'Low jab', '6P': 'Lunge punch', '5K': 'Mid kick', '8K': 'High kick', '2K': 'Low sweep',
  '6K': 'Step kick', JP: 'Hop punch', JK: 'Hop kick', THROW: 'Throw', SP1: 'Special 1', SP2: 'Special 2', SP3: 'Special 3', SUPER: 'Super' };
const MOVE_KEYS = new Set(['su', 'ac', 'rc', 'dmg', 'lv', 'hs', 'bs', 'hstop', 'pushh', 'pushb', 'kd', 'launch', 'mg', 'hits', 'rehit', 'inv', 'mv', 'mvfrom', 'mvto', 'hit', 'ext', 'proj', 'flags', 'gdmg', 'name']);
const FIGHTER_KEYS = new Set(['name', 'hp', 'walk', 'back', 'jump', 'jumpfx', 'grav', 'dash', 'dashback', 'sp1', 'sp2', 'sp3', 'super']);
const FLAGS = new Set(['air', 'crouch', 'throw', 'special', 'super', 'proj']);
const errors = [];
// specials: a motion then P, K or X
const SPECIAL = /^(\[[1-9]\])?[1-9!]{1,10}[PKX]$/;
const err = (ln, m) => errors.push(`${file}:${ln}: ${m}`);

const tpl = {}, fighters = [];
let lineNo = 0;
for (const raw of readFileSync(file, 'utf8').split('\n')) {
  lineNo++;
  const line = raw.trim();
  if (!line || line.startsWith('#')) continue;
  const tok = line.split(/\s+/);
  const kv = (from, keys) => {
    const o = {};
    for (const t of tok.slice(from)) {
      const i = t.indexOf('=');
      if (i < 0) { err(lineNo, `'${t}' is not key=value`); continue; }
      const k = t.slice(0, i);
      if (!keys.has(k)) err(lineNo, `unknown key '${k}'`);
      o[k] = t.slice(i + 1);
    }
    return o;
  };
  if (tok[0] === 'fighter') for (const t of tok.slice(2)) { const [k, v] = t.split('='); if (['sp1', 'sp2', 'sp3', 'super'].includes(k) && !SPECIAL.test(v)) err(lineNo, `${k}=${v}: a motion then its button (P, K or X)`); }
  if (tok[0] === 'fighter') fighters.push({ id: tok[1], ...kv(2, FIGHTER_KEYS), moves: Object.fromEntries(SLOTS.map((s) => [s, { ...(tpl[s] ?? {}) }])) });
  else if (tok[0] === 'move') {
    if (!SLOTS.includes(tok[2])) { err(lineNo, `unknown move slot '${tok[2]}'`); continue; }
    const o = kv(3, MOVE_KEYS);
    if (tok[1] === '*') tpl[tok[2]] = { ...(tpl[tok[2]] ?? {}), ...o };
    else {
      const f = fighters.find((x) => x.id === tok[1]);
      if (!f) { err(lineNo, `fighter '${tok[1]}' is not declared yet`); continue; }
      Object.assign(f.moves[tok[2]], o);
    }
  } else err(lineNo, `unknown line '${tok[0]}'`);
}

const n = (m, k) => (m[k] === undefined ? 0 : Number(m[k]));
const rows = [];
for (const f of fighters) {
  if (arg('--fighter') && f.id !== arg('--fighter')) continue;
  for (const s of SLOTS) {
    const m = f.moves[s];
    const su = n(m, 'su'), ac = n(m, 'ac'), rc = n(m, 'rc');
    if (!su || !ac) err(0, `${f.id} ${s}: needs su and ac`);
    for (const k of ['hit', 'ext']) if (m[k]) {
      const b = m[k].split(',').map(Number);
      if (b.length !== 4 || b.some(Number.isNaN)) err(0, `${f.id} ${s}: bad ${k} box`);
      else if (b[2] < b[0] || b[3] < b[1]) err(0, `${f.id} ${s}: ${k} box is the wrong way round`);
    }
    for (const fl of (m.flags ?? '').split(',').filter(Boolean)) if (!FLAGS.has(fl)) err(0, `${f.id} ${s}: unknown flag ${fl}`);
    const flags = m.flags ?? '';
    const hs = n(m, 'hs'), bs = n(m, 'bs');
    const kd = n(m, 'kd') || n(m, 'launch');
    rows.push({ fighter: f.id, move: s, name: (m.name ?? DEFAULT_NAME[s]).replaceAll('_', ' '), startup: su, active: ac, recovery: rc, total: su + ac + rc, damage: n(m, 'dmg') * (n(m, 'hits') || 1),
      level: m.lv ?? 'm', onHit: kd ? 'KD' : hs - (ac - 1 + rc), onBlock: flags.includes('throw') ? '-' : bs - (ac - 1 + rc), hitstop: n(m, 'hstop'),
      notes: [flags.includes('proj') ? 'projectile' : '', flags.includes('throw') ? 'throw' : '', n(m, 'inv') ? `invuln ${n(m, 'inv')}f` : '', n(m, 'hits') > 1 ? `${m.hits} hits` : ''].filter(Boolean).join(', ') });
  }
}
if (flag('--json')) console.log(JSON.stringify(rows, null, 1));
else if (flag('--csv')) { console.log(Object.keys(rows[0]).join(',')); for (const r of rows) console.log(Object.values(r).join(',')); }
else {
  const cols = ['move', 'name', 'startup', 'active', 'recovery', 'total', 'damage', 'level', 'onHit', 'onBlock', 'hitstop', 'notes'];
  const head = ['Move', 'Name', 'Start', 'Act', 'Rec', 'Total', 'Dmg', 'Lvl', 'Hit', 'Block', 'Stop', 'Notes'];
  let cur = '';
  for (const r of rows) {
    if (r.fighter !== cur) {
      cur = r.fighter;
      const f = fighters.find((x) => x.id === cur);
      console.log(`\n${f.name ?? cur}  life ${f.hp ?? 1000}  walk ${f.walk ?? '-'}  specials ${f.sp1} ${f.sp2} ${f.sp3}  super ${f.super}`);
      console.log(head.map((h, i) => (i === 11 ? h : i === 1 ? ' ' + h.padEnd(17) : h.padStart(i === 0 ? 6 : 5))).join(' '));
    }
    const fmt = (v, i) => (typeof v === 'number' && (cols[i] === 'onHit' || cols[i] === 'onBlock') && v > 0 ? `+${v}` : String(v));
    console.log(cols.map((c, i) => (i === 11 ? fmt(r[c], i) : i === 1 ? ' ' + String(r[c]).padEnd(17) : fmt(r[c], i).padStart(i === 0 ? 6 : 5))).join(' '));
  }
}
if (errors.length) { console.error(errors.join('\n')); process.exit(1); }
