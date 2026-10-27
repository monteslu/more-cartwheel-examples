// Soak and measurement runs of brawler_auto, off the GPU lock: software GL (llvmpipe, 2 threads), the engine's job
// pool at 2, nice 10, and many simulation ticks per rendered frame (BRAWLER_TURBO), so 30 minutes of game time takes
// a fraction of that on the wall clock. The game prints one `brawler-stats` line per stage cleared or lost and a
// `brawler-status` line every 60 s of game time (BRAWLER_LOG=1); this script reads them, samples the process's
// resident memory at each status line, and checks what a soak must show.
//
//   node games/brawler/tools/soak.mjs --exe <brawler_auto> --pak <brawler_auto.cwpak> [--players 1] [--minutes 30]
//        [--turbo 60] [--stage N] [--env K=V,...] [--out file.json] [--expect-fail]
//
// Checks (each one fails the run): the process exits 0; no NaN position; the token leak audit is 0 at every status
// line; no hit from off camera or from an enemy still walking in (unfair 0); no stall of 60 s of game time with the
// match running (no kill, no wave, no camera move, no damage to any enemy); resident memory grows less than 5 percent
// from minute 5 to the end; with --all-stages every stage was cleared with its boss taken through all three phases.
// --expect-fail inverts the exit code (controls: a run with an injected fault must fail).
import { spawn } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';

const argv = process.argv.slice(2);
const arg = (k, d) => (argv.includes(k) ? argv[argv.indexOf(k) + 1] : d);
const exe = arg('--exe'), pak = arg('--pak');
if (!exe || !pak) { console.error('usage: soak.mjs --exe <brawler_auto> --pak <cwpak> [--players N] [--minutes M]'); process.exit(2); }
const players = +arg('--players', 1), minutes = +arg('--minutes', 30), turbo = +arg('--turbo', 60);
const ticks = Math.round(minutes * 3600);
const extra = Object.fromEntries((arg('--env', '') || '').split(',').filter(Boolean).map((kv) => kv.split('=')));
const env = {
  ...process.env, LIBGL_ALWAYS_SOFTWARE: '1', LP_NUM_THREADS: '2', CW_WORKERS: '2', SDL_VIDEODRIVER: 'offscreen', SDL_AUDIODRIVER: 'dummy',
  CW_QUALITY: 'low', CW_FRAMES: String(Math.ceil(ticks / turbo)), CW_PAK: pak, BRAWLER_TURBO: String(turbo), BRAWLER_LOG: '1',
  BRAWLER_SOAK: '1', BRAWLER_PLAYERS: String(players), ...(arg('--stage') ? { BRAWLER_STAGE: arg('--stage') } : {}), ...extra,
};
const kv = (line) => Object.fromEntries([...line.matchAll(/(\w+)=(-?\d+)/g)].map((m) => [m[1], +m[2]]));
const stats = [], status = [];
let rss0 = null, rssEnd = null, buf = '';
const p = spawn('nice', ['-n', '10', exe], { env, stdio: ['ignore', 'pipe', 'pipe'] });
const rss = () => { try { return +/VmRSS:\s+(\d+)/.exec(readFileSync(`/proc/${p.pid}/status`, 'utf8'))[1]; } catch { return null; } };
const onData = (d) => {
  buf += d;
  let i;
  while ((i = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, i); buf = buf.slice(i + 1);
    if (line.includes('brawler-stats')) stats.push({ why: /brawler-stats (\w+)/.exec(line)[1], ...kv(line) });
    else if (line.includes('brawler-status')) {
      const s = kv(line); s.rss = rss(); status.push(s);
      if (s.run >= 5 * 3600 && rss0 == null) rss0 = s.rss;
      rssEnd = s.rss;
    } else if (/\[error\]|\[warn\]|brawler-frozen/.test(line)) console.log(line);
  }
};
p.stdout.on('data', onData); p.stderr.on('data', onData);
const code = await new Promise((r) => p.on('close', r));

const problems = [];
if (code !== 0) problems.push(`exit ${code}`);
const worst = (k) => Math.max(0, ...status.map((s) => s[k]), ...stats.map((s) => s[k] ?? 0));
if (worst('nan') > 0) problems.push(`NaN positions: ${worst('nan')}`);
if (status.some((s) => s.leak !== 0) || stats.some((s) => s.leak !== 0)) problems.push('token leak');
if (worst('unfair') > 0) problems.push(`unfair hits: ${worst('unfair')}`);
const stall = Math.max(0, ...status.filter((s) => s.phase === 0).map((s) => s.stall), ...stats.map((s) => s.stall_max));
if (stall >= 3600) problems.push(`stall of ${(stall / 60).toFixed(0)} s`);
const growth = rss0 && rssEnd ? (rssEnd - rss0) / rss0 : null;
if (growth != null && growth > 0.05) problems.push(`memory grew ${(growth * 100).toFixed(1)} percent from minute 5`);
if (status.length < Math.floor(ticks / 3600) - 1) problems.push(`only ${status.length} status lines for ${minutes} minutes`);
const cleared = [0, 1, 2, 3].map((s) => stats.find((x) => x.why === 'clear' && x.stage === s));
if (argv.includes('--all-stages')) for (const [s, c] of cleared.entries()) {
  if (!c) problems.push(`stage ${s + 1} never cleared`);
  else if (c.boss_phase < 2) problems.push(`stage ${s + 1} boss reached phase ${c.boss_phase + 1} only`);
}
const summary = {
  players, minutes, turbo, exit: code, statusLines: status.length, stages: stats, rssKb: { minute5: rss0, end: rssEnd, growth },
  stallMaxTicks: stall, stillMaxTicks: worst('still_max'), unfair: worst('unfair'), nan: worst('nan'),
  reached: [...new Set(status.map((s) => s.stage))], cleared: cleared.map((c) => !!c), problems,
};
if (arg('--out')) writeFileSync(arg('--out'), JSON.stringify(summary, null, 1));
console.log(JSON.stringify({ ...summary, stages: stats.length }, null, 1));
const failed = problems.length > 0;
console.log(failed ? `FAIL: ${problems.join('; ')}` : 'ok');
process.exit(argv.includes('--expect-fail') ? (failed ? 0 : 1) : failed ? 1 : 0);
