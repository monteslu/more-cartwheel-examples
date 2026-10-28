// Pull the solid tree meshes Tux Racer uses out of sky-mesa's generated
// header (../sky-mesa/src/tree-meshes.h, scripts/build-tree-models.py there:
// four species x two branching variants x two LODs x {bark, leaves}) into
// assets/model/trees.bin. Tux Racer's conifer (tree1) is sky-mesa's spruce
// (species 2, both variants, both LODs, bark + leaves); its barren tree
// (tree2) is the leafless birch (species 1, bark only). Output order (12):
//   spruce: lod0 v0 bark, lod0 v0 leaves, lod0 v1 bark, lod0 v1 leaves,
//           lod1 v0 bark, lod1 v0 leaves, lod1 v1 bark, lod1 v1 leaves
//   birch:  lod0 v0 bark, lod0 v1 bark, lod1 v0 bark, lod1 v1 bark
// File: "TRE1", u32 count, then per mesh: u32 vertices, u32 indices,
// float32 p[v*3], n[v*3], uv[v*2], uint32 ix[i]. Little-endian.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
const root = resolve(dirname(new URL(import.meta.url).pathname), '..');
const src = readFileSync(resolve(root, '../sky-mesa/src/tree-meshes.h'), 'utf8');
function arr(name, type) {
  const re = new RegExp(`static const ${type} ${name}\\[\\]=\\{([^}]*)\\};`);
  const m = src.match(re);
  if (!m) throw new Error('missing ' + name);
  return m[1].split(',').map((t) => Number(t.replace('f', '')));
}
const want = [];
for (const lod of [0, 1]) for (const v of [0, 1]) want.push([lod, 1, v, 0]);
const chunks = [Buffer.from('TRE1'), Buffer.alloc(4)];
chunks[1].writeUInt32LE(want.length);
let total = 0;
for (const [lod, s, v, part] of want) {
  const base = `tree_${lod}_${s}_${v}_${part}`;
  const p = arr(base + '_p', 'float'), n = arr(base + '_n', 'float'), uv = arr(base + '_uv', 'float'), ix = arr(base + '_ix', 'uint32_t');
  const verts = p.length / 3;
  const h = Buffer.alloc(8); h.writeUInt32LE(verts, 0); h.writeUInt32LE(ix.length, 4);
  chunks.push(h, Buffer.from(Float32Array.from(p).buffer), Buffer.from(Float32Array.from(n).buffer), Buffer.from(Float32Array.from(uv).buffer), Buffer.from(Uint32Array.from(ix).buffer));
  total += ix.length / 3;
  console.log(base, verts, 'vertices', ix.length / 3, 'triangles');
}
mkdirSync(resolve(root, 'assets/model'), { recursive: true });
const out = resolve(root, 'assets/model/trees.bin');
writeFileSync(out, Buffer.concat(chunks));
console.log('wrote', out, Buffer.concat(chunks).length, 'bytes,', total, 'triangles');
