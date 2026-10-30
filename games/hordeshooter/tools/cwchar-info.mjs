// Prints what a .cwchar holds: clips and their frame counts, and per group the vertices, triangles and the
// bounds over every frame (metres, Y up). Usage: node tools/cwchar-info.mjs <file.cwchar> [...] [--json]
// The format is described in tools/blender/hordechars.py.
import { readFileSync } from 'node:fs';

export function readCwchar(path) {
  const b = readFileSync(path);
  if (b.toString('latin1', 0, 4) !== 'CWCH') throw new Error(`${path}: not a .cwchar`);
  const u32 = (o) => b.readUInt32LE(o);
  const nclip = u32(8), ngroup = u32(12);
  let p = 16;
  const clips = [];
  for (let k = 0; k < nclip; k++, p += 4) clips.push(u32(p));
  const total = clips.reduce((a, c) => a + c, 0);
  const groups = [];
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  const frameLo = []; // the lowest y of each frame (rest, walk, attack...)
  for (let f = 0; f < total; f++) frameLo.push(Infinity);
  for (let g = 0; g < ngroup; g++) {
    const nv = u32(p), nt = u32(p + 4);
    p += 8;
    const idxb = (nt * 6 + 3) & ~3;
    p += 20 * nv + idxb;
    for (let f = 0; f < total; f++) {
      for (let v = 0; v < nv; v++) {
        for (let a = 0; a < 3; a++) {
          const x = b.readFloatLE(p + 12 * v + 4 * a);
          if (x < lo[a]) lo[a] = x;
          if (x > hi[a]) hi[a] = x;
          if (a === 1 && x < frameLo[f]) frameLo[f] = x;
        }
      }
      p += 24 * nv;
    }
    groups.push({ verts: nv, tris: nt });
  }
  return { clips, groups, tris: groups.reduce((a, g) => a + g.tris, 0), lo, hi, frameLo, bytes: b.length };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const json = process.argv.includes('--json');
  for (const f of process.argv.slice(2).filter((a) => a !== '--json')) {
    const r = readCwchar(f);
    if (json) { console.log(JSON.stringify({ file: f, ...r })); continue; }
    const fmt = (v) => v.map((x) => x.toFixed(2)).join(', ');
    console.log(`${f}: clips [${r.clips}], ${r.groups.map((g) => `${g.verts}v/${g.tris}t`).join(' + ')} = ${r.tris} tris, bounds (${fmt(r.lo)}) .. (${fmt(r.hi)}), ${r.bytes} bytes`);
  }
}
