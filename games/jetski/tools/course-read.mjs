// What the generated course headers say, for tools that check or photograph a course without running it.
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const HERE = dirname(fileURLToPath(import.meta.url));
export const COURSE_IDS = ['atoll', 'sound', 'canyon', 'harbour', 'storm'];
const rows = (src, id, table) => {
  const at = src.indexOf(`${id}_${table}[]`);
  if (at < 0) return [];
  const end = src.indexOf('};', at);
  return [...src.slice(src.indexOf('{', at + id.length + table.length + 4) + 1, end).matchAll(/\{([^{}]*)\}/g)].map((m) => m[1].split(',').map((v) => parseFloat(v)));
};
const num = (src, key) => parseFloat(src.match(new RegExp(`\\.${key} = (-?[\\d.]+)`))[1]);
/** @param {string} id course id @returns the course's data: track, buoys, ramps, placed models, scatter maps, heights and how to read them */
export function readCourse(id) {
  const src = readFileSync(join(HERE, `../src/courses/${id}.h`), 'utf8');
  const models = (src.match(new RegExp(`${id}_models\\[\\] = \\{([^}]*)\\}`))?.[1].match(/"[^"]+"/g) || []).map((s) => s.slice(1, -1).replace(/^res:\/\/models\/|\.glb$/g, ''));
  const quads = num(src, 'quads'), spacing = num(src, 'spacing'), minh = num(src, 'minh'), maxh = num(src, 'maxh');
  const maps = [...new Set([...src.matchAll(new RegExp(`"course/${id}/([a-z0-9_]+)\\.u8"`, 'g'))].map((m) => m[1]))];
  const waves = [...src.match(/\.waves = \{(.*?)\}, \.swell/s)[1].matchAll(/\{([^{}]*)/g)].map((m) => m[1].split(',').map(parseFloat));
  const swell = src.match(/\.swell = \{([^}]*)\}/)[1].split(',').map(parseFloat), tide = src.match(/\.tide = \{([^}]*)\}/)[1].split(',').map(parseFloat);
  const r16 = readFileSync(join(HERE, `../assets/course/${id}/heights.r16`));
  const N = quads + 1, H = new Uint16Array(r16.buffer, r16.byteOffset, N * N);
  /** the terrain's height at a world point: the same bilinear read as the heightmap the game builds its land from */
  const ground = (x, z) => {
    const u = Math.min(N - 1.001, Math.max(0, x / spacing + quads / 2)), v = Math.min(N - 1.001, Math.max(0, z / spacing + quads / 2));
    const i = Math.floor(u), j = Math.floor(v), a = u - i, b = v - j;
    const h = (ii, jj) => minh + (H[jj * N + ii] / 65535) * (maxh - minh);
    return (h(i, j) * (1 - a) + h(i + 1, j) * a) * (1 - b) + (h(i, j + 1) * (1 - a) + h(i + 1, j + 1) * a) * b;
  };
  const start = src.match(/\.start = \{([^}]*)\}/)[1].split(',').map(parseFloat);
  const hookText = src.match(/\.hook = \{(.*)\}/)?.[1] || '';
  const hook = hookText ? { kind: num(hookText, 'kind'), x: num(hookText, 'x'), z: num(hookText, 'z') } : null;
  const landmark = src.match(/\.landmark_at = \{([^}]*)\}/)[1].split(',').map(parseFloat);
  return { id, models, quads, spacing, minh, maxh, maps, waves, swell, tide, ground, start, hook, landmark, len: num(src, 'len'), track: rows(src, id, 'track'), buoys: rows(src, id, 'buoys'), ramps: rows(src, id, 'ramps'), placed: rows(src, id, 'placed'), rampTop: num(src, 'ramp_top') };
}
/** @param {string} name model name @returns its lowest point and horizontal extent, in model space, from the glb's accessors and node translations */
export function modelBounds(name) {
  const b = readFileSync(join(HERE, `../assets/models/${name}.glb`)), n = b.readUInt32LE(12), g = JSON.parse(b.toString('utf8', 20, 20 + n));
  const box = [Infinity, Infinity, Infinity, -Infinity, -Infinity, -Infinity];
  for (const node of g.nodes || []) {
    if (node.mesh === undefined) continue;
    const t = node.translation || [0, 0, 0];
    for (const p of g.meshes[node.mesh].primitives) {
      const a = g.accessors[p.attributes.POSITION];
      for (let k = 0; k < 3; k++) { box[k] = Math.min(box[k], a.min[k] + t[k]); box[k + 3] = Math.max(box[k + 3], a.max[k] + t[k]); }
    }
  }
  return { minx: box[0], miny: box[1], minz: box[2], maxx: box[3], maxy: box[4], maxz: box[5] };
}
/** @param {string} name model name @param {number} band how far over its lowest point counts as its foot @returns the model-space points of its foot */
export function modelFoot(name, band = 0.6) {
  const b = readFileSync(join(HERE, `../assets/models/${name}.glb`)), n = b.readUInt32LE(12), g = JSON.parse(b.toString('utf8', 20, 20 + n));
  const bin = 20 + n + 8, pts = [];
  for (const node of g.nodes || []) {
    if (node.mesh === undefined) continue;
    const t = node.translation || [0, 0, 0];
    for (const p of g.meshes[node.mesh].primitives) {
      const a = g.accessors[p.attributes.POSITION], v = g.bufferViews[a.bufferView], stride = v.byteStride || 12, o = bin + (v.byteOffset || 0) + (a.byteOffset || 0);
      for (let i = 0; i < a.count; i++) pts.push([b.readFloatLE(o + i * stride) + t[0], b.readFloatLE(o + i * stride + 4) + t[1], b.readFloatLE(o + i * stride + 8) + t[2]]);
    }
  }
  const low = Math.min(...pts.map((q) => q[1]));
  return pts.filter((q) => q[1] <= low + band);
}
