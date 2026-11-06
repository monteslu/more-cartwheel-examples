#!/usr/bin/env node
// Prints what every .cwchar in a directory (or the files named) holds: clips, frames, groups, triangles per
// group, and per frame the min and max of x, y, z over every group (game axes: y up, the model faces +Z).
//   node tools/cwchar-bounds.mjs <dir | file.cwchar ...> [--json]
// The format is described in tools/blender/hordechars.py. No dependencies.
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, basename } from 'node:path';

/** @param {string} path */
export function readCwchar(path) {
  const b = readFileSync(path);
  if (b.length < 16 || b.toString('latin1', 0, 4) !== 'CWCH') throw new Error(`${path}: not a .cwchar file`);
  const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const u32 = (o) => dv.getUint32(o, true);
  const version = u32(4), nclip = u32(8), ngroup = u32(12);
  let p = 16;
  const clips = [];
  for (let k = 0; k < nclip; k++, p += 4) clips.push(u32(p));
  const total = clips.reduce((a, b) => a + b, 0);
  const frames = Array.from({ length: total }, () => ({ min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] }));
  const groups = [];
  for (let g = 0; g < ngroup; g++) {
    const nv = u32(p), nt = u32(p + 4);
    p += 8;
    const colOff = p + 8 * nv;
    let emissive = 0;
    for (let v = 0; v < nv; v++) {
      const o = colOff + 12 * v;
      if (dv.getFloat32(o, true) > 1.5 || dv.getFloat32(o + 4, true) > 1.5 || dv.getFloat32(o + 8, true) > 1.5) emissive++;
    }
    p += 20 * nv + ((nt * 6 + 3) & ~3);
    const gb = [];
    for (let f = 0; f < total; f++) {
      const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
      for (let v = 0; v < nv; v++)
        for (let a = 0; a < 3; a++) {
          const x = dv.getFloat32(p + 12 * v + 4 * a, true);
          if (x < mn[a]) mn[a] = x;
          if (x > mx[a]) mx[a] = x;
        }
      p += 24 * nv;
      for (let a = 0; a < 3; a++) {
        frames[f].min[a] = Math.min(frames[f].min[a], mn[a]);
        frames[f].max[a] = Math.max(frames[f].max[a], mx[a]);
      }
      gb.push({ min: mn, max: mx });
    }
    groups.push({ verts: nv, tris: nt, emissiveVerts: emissive, frames: gb });
  }
  if (p !== b.length) throw new Error(`${path}: ${b.length - p} bytes left over (expected ${p})`);
  const out = [];
  let f = 0;
  clips.forEach((n, c) => {
    for (let i = 0; i < n; i++, f++) out.push({ clip: c, frame: i, min: frames[f].min, max: frames[f].max });
  });
  return { file: basename(path), version, clips, groups: groups.map(({ frames, ...g }) => g), frames: out, groupFrames: groups.map((g) => g.frames) };
}

const args = process.argv.slice(2);
const json = args.includes('--json');
const paths = args.filter((a) => a !== '--json');
if (!paths.length) {
  console.error('usage: cwchar-bounds.mjs <dir | file.cwchar ...> [--json]');
  process.exit(2);
}
const files = paths.flatMap((a) => (statSync(a).isDirectory() ? readdirSync(a).filter((n) => n.endsWith('.cwchar')).sort().map((n) => join(a, n)) : [a]));
const all = files.map(readCwchar);
const r3 = (v) => v.map((x) => +x.toFixed(3));
if (json) {
  console.log(JSON.stringify(all.map(({ groupFrames, ...m }) => ({ ...m, frames: m.frames.map((fr) => ({ ...fr, min: r3(fr.min), max: r3(fr.max) })) }))));
} else {
  for (const m of all) {
    console.log(`${m.file}: clips [${m.clips.join(', ')}], groups ${m.groups.length}, tris ${m.groups.map((g) => g.tris).join(' + ')}, verts ${m.groups.map((g) => g.verts).join(' + ')}`);
    for (const fr of m.frames) {
      const fmt = (v) => v.map((x) => x.toFixed(3).padStart(7)).join(' ');
      console.log(`  c${fr.clip} f${fr.frame}  min ${fmt(fr.min)}  max ${fmt(fr.max)}`);
    }
  }
}
