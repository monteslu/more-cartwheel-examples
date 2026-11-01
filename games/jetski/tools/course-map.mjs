// A course from above: land and water, the line (white), buoys (orange-red: keep on your right, yellow: keep on your left), ramps (green, a tick at the lip).
//   node games/jetski/tools/course-map.mjs <course id> <out.png> [ramps json: a file of { id: rows } to draw in place of the course's own]
import { readFileSync, writeFileSync } from 'node:fs';
import { deflateSync, crc32 } from 'node:zlib';
import { readCourse } from './course-read.mjs';
const [id, out, other] = process.argv.slice(2), c = readCourse(id), ramps = other ? JSON.parse(readFileSync(other, 'utf8'))[id] : c.ramps;
const S = 1200, SIZE = c.quads * c.spacing, b = Buffer.alloc(S * S * 3);
const px = (x) => Math.round((x / SIZE + 0.5) * S), set = (x, y, r, g, a) => { if (x < 0 || y < 0 || x >= S || y >= S) return; const k = (y * S + x) * 3; b[k] = r; b[k + 1] = g; b[k + 2] = a; };
for (let j = 0; j < S; j++) for (let i = 0; i < S; i++) { const h = c.ground(((i + 0.5) / S - 0.5) * SIZE, ((j + 0.5) / S - 0.5) * SIZE); if (h > 0) set(i, j, 70 + Math.min(90, h), 62 + Math.min(70, h * 0.7), 50); else set(i, j, 18, 48 + Math.max(0, 40 + h * 2), 88); }
const line = (x0, z0, x1, z1, col, w = 1) => { const n = Math.ceil(Math.hypot(x1 - x0, z1 - z0) / SIZE * S) + 1; for (let k = 0; k <= n; k++) { const t = k / n, x = px(x0 + (x1 - x0) * t), y = px(z0 + (z1 - z0) * t); for (let a = -w; a <= w; a++) for (let d = -w; d <= w; d++) set(x + a, y + d, ...col); } };
c.track.forEach((t, i) => { const u = c.track[(i + 1) % c.track.length]; line(t[0], t[1], u[0], u[1], [235, 235, 235]); });
for (const r of ramps) {
  const sy = Math.sin(r[2]), cy = Math.cos(r[2]), hl = 20, hw = 9, P = [[-hw, -hl], [hw, -hl], [hw, hl], [-hw, hl]].map(([l, a]) => [r[0] + sy * a - cy * l, r[1] + cy * a + sy * l]);
  P.forEach((p, i) => { const q = P[(i + 1) % 4]; line(p[0], p[1], q[0], q[1], [60, 240, 90], 1); });
  line(r[0], r[1], r[0] + sy * 40, r[1] + cy * 40, [60, 240, 90], 1);
}
for (const q of c.buoys) { const col = q[2] > 0 ? [255, 70, 30] : [255, 210, 30], x = px(q[0]), y = px(q[1]); for (let a = -5; a <= 5; a++) for (let d = -5; d <= 5; d++) if (a * a + d * d <= 26) set(x + a, y + d, ...col); }
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const n = Buffer.concat([Buffer.from(t), d]), e = Buffer.alloc(4); e.writeUInt32BE(crc32(n)); return Buffer.concat([l, n, e]); };
const raw = Buffer.alloc((S * 3 + 1) * S);
for (let y = 0; y < S; y++) b.copy(raw, y * (S * 3 + 1) + 1, y * S * 3, (y + 1) * S * 3);
const ih = Buffer.alloc(13); ih.writeUInt32BE(S, 0); ih.writeUInt32BE(S, 4); ih[8] = 8; ih[9] = 2;
writeFileSync(out, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
