// Frame times from a profiler trace (cartwheel/profile.h, CW_PROFILE_TRACE or tools/cwtrace.mjs in the engine):
// prints the worst frames, how many went over the 60 Hz budget, and the percentiles.
//   node tools/frame-trace.mjs trace.json [--budget 16.7] [--field cpu_ms|wall_ms] [--top 10]
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : d; };
const file = args.find((a, i) => !a.startsWith('--') && !(i > 0 && args[i - 1].startsWith('--')));
if (!file) { console.error('usage: frame-trace.mjs trace.json [--budget 16.7] [--field cpu_ms|wall_ms] [--top 10]'); process.exit(2); }
const budget = +opt('--budget', '16.7'), field = opt('--field', 'cpu_ms'), top = +opt('--top', '10');
const t = JSON.parse(readFileSync(file, 'utf8'));
const ev = (Array.isArray(t) ? t : t.traceEvents).filter((e) => e.name === 'frame' && e.ph === 'X');
if (!ev.length) { console.error(`${file}: no frame events`); process.exit(2); }
const ms = ev.map((e) => ({ frame: e.args.frame, ms: e.args[field] }));
const sorted = ms.map((x) => x.ms).sort((a, b) => a - b);
const pct = (p) => sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * p))];
const over = ms.filter((x) => x.ms > budget);
console.log(`${file}: ${ms.length} frames, ${field} p50 ${pct(0.5).toFixed(2)} p95 ${pct(0.95).toFixed(2)} p99 ${pct(0.99).toFixed(2)} max ${sorted.at(-1).toFixed(2)} ms; ${over.length} over ${budget} ms`);
for (const x of [...ms].sort((a, b) => b.ms - a.ms).slice(0, top)) console.log(`  frame ${x.frame}: ${x.ms.toFixed(2)} ms`);
process.exitCode = over.length ? 1 : 0;
