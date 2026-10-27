// A minimal glTF 2.0 binary writer: skinned meshes with vertex colours,
// skeleton animation, plain meshes. Written for this game's generators.
// No dependencies; every byte is made by our own code.
import { writeFileSync, readFileSync, existsSync } from 'node:fs';

export class Glb {
  constructor() {
    this.nodes = []; this.meshes = []; this.skins = []; this.anims = []; this.materials = [];
    this.accessors = []; this.views = []; this.chunks = []; this.size = 0; this.sceneNodes = [];
    this.images = []; this.textures = []; this.samplers = [{ magFilter: 9729, minFilter: 9987, wrapS: 10497, wrapT: 10497 }, { magFilter: 9729, minFilter: 9987, wrapS: 33071, wrapT: 33071 }]; this.texCache = new Map();
  }
  /** append a typed array as a buffer view + accessor; returns the accessor index */
  acc(arr, type, ctype, target, minmax) {
    const bytes = new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength);
    const pad = (4 - (this.size % 4)) % 4;
    if (pad) { this.chunks.push(new Uint8Array(pad)); this.size += pad; }
    const view = { buffer: 0, byteOffset: this.size, byteLength: bytes.length };
    if (target) view.target = target;
    this.views.push(view);
    this.chunks.push(bytes); this.size += bytes.length;
    const n = { VEC3: 3, VEC4: 4, VEC2: 2, SCALAR: 1, MAT4: 16 }[type];
    const a = { bufferView: this.views.length - 1, componentType: ctype, count: arr.length / n, type };
    if (minmax) {
      const lo = new Array(n).fill(Infinity), hi = new Array(n).fill(-Infinity);
      for (let i = 0; i < arr.length; i++) { lo[i % n] = Math.min(lo[i % n], arr[i]); hi[i % n] = Math.max(hi[i % n], arr[i]); }
      a.min = lo; a.max = hi;
    }
    this.accessors.push(a);
    return this.accessors.length - 1;
  }
  /** embed a PNG file's bytes; returns a texture index (cached by path); clamp picks the clamp-to-edge sampler */
  tex(path, clamp = false) {
    const key = path + (clamp ? '#c' : '');
    if (this.texCache.has(key)) return this.texCache.get(key);
    const bytes = new Uint8Array(readFileSync(path));
    const pad = (4 - (this.size % 4)) % 4;
    if (pad) { this.chunks.push(new Uint8Array(pad)); this.size += pad; }
    this.views.push({ buffer: 0, byteOffset: this.size, byteLength: bytes.length });
    this.chunks.push(bytes); this.size += bytes.length;
    this.images.push({ bufferView: this.views.length - 1, mimeType: 'image/png', name: path.split('/').pop() });
    this.textures.push({ source: this.images.length - 1, sampler: clamp ? 1 : 0 });
    this.texCache.set(key, this.textures.length - 1);
    return this.textures.length - 1;
  }
  /** a PBR material from a texture set written by make-textures (dir/name_c.png, _n, _orm, _e) */
  pbr(name, dir, set, o = {}) {
    const m = { name, pbrMetallicRoughness: { baseColorFactor: [...(o.tint || [1, 1, 1]), o.alpha ?? 1], roughnessFactor: o.rough ?? 1, metallicFactor: o.metal ?? (set ? 1 : 0) } };
    if (set) {
      const c = !o.noColor, p = (s) => `${dir}/${set}_${s}.png`;
      if (c) m.pbrMetallicRoughness.baseColorTexture = { index: this.tex(p('c'), o.clamp) };
      m.pbrMetallicRoughness.metallicRoughnessTexture = { index: this.tex(p('orm'), o.clamp) };
      m.occlusionTexture = { index: this.tex(p('orm'), o.clamp), strength: o.ao ?? 1 };
      m.normalTexture = { index: this.tex(p('n'), o.clamp), scale: o.normal ?? 1 };
      if (o.emissive) { m.emissiveFactor = o.emissive; if (existsSync(p('e'))) m.emissiveTexture = { index: this.tex(p('e'), o.clamp) }; }
    } else if (o.emissive) m.emissiveFactor = o.emissive;
    if (o.alphaMode) m.alphaMode = o.alphaMode;
    if (o.doubleSided) m.doubleSided = true;
    this.materials.push(m);
    return this.materials.length - 1;
  }
  material(name, rough = 0.8, metal = 0, emissive = null) {
    const m = { name, pbrMetallicRoughness: { baseColorFactor: [1, 1, 1, 1], roughnessFactor: rough, metallicFactor: metal } };
    if (emissive) m.emissiveFactor = emissive;
    this.materials.push(m);
    return this.materials.length - 1;
  }
  /** geometry: {pos, nrm, col (rgba float), idx, joints?, weights?}; returns the mesh index */
  /** one primitive's attributes; returns the primitive record */
  prim(g, material) {
    const attrs = { POSITION: this.acc(new Float32Array(g.pos), 'VEC3', 5126, 34962, true), NORMAL: this.acc(new Float32Array(g.nrm), 'VEC3', 5126, 34962) };
    if (g.uv && g.uv.length) attrs.TEXCOORD_0 = this.acc(new Float32Array(g.uv), 'VEC2', 5126, 34962);
    if (g.col) attrs.COLOR_0 = this.acc(new Float32Array(g.col.filter((_, i) => i % 4 !== 3)), 'VEC3', 5126, 34962);
    if (g.joints) {
      attrs.JOINTS_0 = this.acc(new Uint8Array(g.joints), 'VEC4', 5121, 34962);
      attrs.WEIGHTS_0 = this.acc(new Float32Array(g.weights), 'VEC4', 5126, 34962);
    }
    const big = g.pos.length / 3 > 65535;
    const idx = this.acc(big ? new Uint32Array(g.idx) : new Uint16Array(g.idx), 'SCALAR', big ? 5125 : 5123, 34963);
    return { attributes: attrs, indices: idx, material };
  }
  /** mesh(name, g, material) for one primitive, or mesh(name, [[g, material], ...]) */
  mesh(name, g, material) {
    const prims = Array.isArray(g) ? g.map(([gg, mm]) => this.prim(gg, mm)) : [this.prim(g, material)];
    this.meshes.push({ name, primitives: prims });
    return this.meshes.length - 1;
  }
  node(n) { this.nodes.push(n); return this.nodes.length - 1; }
  /** channels: [{node, path:'rotation'|'translation', times:[], values:[]}] */
  anim(name, channels) {
    const samplers = [], chs = [];
    for (const c of channels) {
      const inp = this.acc(new Float32Array(c.times), 'SCALAR', 5126, 0, true);
      const out = this.acc(new Float32Array(c.values), c.path === 'rotation' ? 'VEC4' : 'VEC3', 5126, 0);
      samplers.push({ input: inp, output: out, interpolation: 'LINEAR' });
      chs.push({ sampler: samplers.length - 1, target: { node: c.node, path: c.path } });
    }
    this.anims.push({ name, samplers, channels: chs });
  }
  write(path) {
    const total = this.size + ((4 - (this.size % 4)) % 4);
    const json = {
      asset: { version: '2.0', generator: 'brawler tools' },
      scene: 0, scenes: [{ nodes: this.sceneNodes }], nodes: this.nodes, meshes: this.meshes,
      materials: this.materials, accessors: this.accessors, bufferViews: this.views,
      buffers: [{ byteLength: total }],
    };
    if (this.images.length) { json.images = this.images; json.textures = this.textures; json.samplers = this.samplers; }
    if (this.skins.length) json.skins = this.skins;
    if (this.anims.length) json.animations = this.anims;
    let js = Buffer.from(JSON.stringify(json));
    js = Buffer.concat([js, Buffer.alloc((4 - (js.length % 4)) % 4, 0x20)]);
    const bin = Buffer.alloc(total);
    let o = 0;
    for (const c of this.chunks) { bin.set(c, o); o += c.length; }
    const head = Buffer.alloc(12);
    head.write('glTF', 0); head.writeUInt32LE(2, 4); head.writeUInt32LE(12 + 8 + js.length + 8 + bin.length, 8);
    const jh = Buffer.alloc(8); jh.writeUInt32LE(js.length, 0); jh.write('JSON', 4);
    const bh = Buffer.alloc(8); bh.writeUInt32LE(bin.length, 0); bh.write('BIN\0', 4);
    writeFileSync(path, Buffer.concat([head, jh, js, bh, bin]));
  }
}

/** geometry builder: flat-shaded triangles with a colour, optionally skinned to one bone */
export class Geo {
  constructor() { this.pos = []; this.nrm = []; this.col = []; this.idx = []; this.joints = []; this.weights = []; }
  tri(a, b, c, color, bone) {
    const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    let n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
    const l = Math.hypot(...n) || 1; n = n.map((x) => x / l);
    if (this.ctr) { // convex shapes: wind every face outward
      const m = [(a[0] + b[0] + c[0]) / 3 - this.ctr[0], (a[1] + b[1] + c[1]) / 3 - this.ctr[1], (a[2] + b[2] + c[2]) / 3 - this.ctr[2]];
      if (n[0] * m[0] + n[1] * m[1] + n[2] * m[2] < 0) { n = n.map((x) => -x); [b, c] = [c, b]; }
    }
    const base = this.pos.length / 3;
    for (const p of [a, b, c]) {
      this.pos.push(...p); this.nrm.push(...n); this.col.push(color[0], color[1], color[2], 1);
      this.joints.push(bone, 0, 0, 0); this.weights.push(1, 0, 0, 0);
    }
    this.idx.push(base, base + 1, base + 2);
  }
  quad(a, b, c, d, color, bone) { this.tri(a, b, c, color, bone); this.tri(a, c, d, color, bone); }
  /** a tapered box from centre c with half sizes (hx0,hz0) at the bottom and (hx1,hz1) at the top, height h */
  frustum(c, hx0, hz0, hx1, hz1, h, color, bone) {
    this.ctr = c;
    const y0 = c[1] - h / 2, y1 = c[1] + h / 2;
    const B = [[c[0] - hx0, y0, c[2] - hz0], [c[0] + hx0, y0, c[2] - hz0], [c[0] + hx0, y0, c[2] + hz0], [c[0] - hx0, y0, c[2] + hz0]];
    const T = [[c[0] - hx1, y1, c[2] - hz1], [c[0] + hx1, y1, c[2] - hz1], [c[0] + hx1, y1, c[2] + hz1], [c[0] - hx1, y1, c[2] + hz1]];
    this.quad(B[3], B[2], B[1], B[0], color, bone);
    this.quad(T[0], T[1], T[2], T[3], color, bone);
    this.quad(B[0], B[1], T[1], T[0], color, bone); // back (-z)
    this.quad(B[2], B[3], T[3], T[2], color, bone); // front (+z)
    this.quad(B[3], B[0], T[0], T[3], color, bone); // -x
    this.quad(B[1], B[2], T[2], T[1], color, bone); // +x
  }
  box(c, sx, sy, sz, color, bone) { this.frustum(c, sx / 2, sz / 2, sx / 2, sz / 2, sy, color, bone); }
  /** a segment from a to b as an n-sided tapered prism with end caps (a limb) */
  limb(a, b, r0, r1, color, bone, n = 8) {
    this.ctr = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2];
    const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], L = Math.hypot(...d);
    const w = d.map((x) => x / L);
    const up = Math.abs(w[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0];
    let u = [w[1] * up[2] - w[2] * up[1], w[2] * up[0] - w[0] * up[2], w[0] * up[1] - w[1] * up[0]];
    const ul = Math.hypot(...u); u = u.map((x) => x / ul);
    const v = [w[1] * u[2] - w[2] * u[1], w[2] * u[0] - w[0] * u[2], w[0] * u[1] - w[1] * u[0]];
    const ring = (p, r) => Array.from({ length: n }, (_, i) => {
      const t = (i / n) * Math.PI * 2, cs = Math.cos(t) * r, sn = Math.sin(t) * r;
      return [p[0] + u[0] * cs + v[0] * sn, p[1] + u[1] * cs + v[1] * sn, p[2] + u[2] * cs + v[2] * sn];
    });
    const A = ring(a, r0), B = ring(b, r1);
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      this.quad(A[i], A[j], B[j], B[i], color, bone);
      this.tri(a, A[j], A[i], color, bone);
      this.tri(b, B[i], B[j], color, bone);
    }
  }
  /** a low-poly ellipsoid */
  blob(c, rx, ry, rz, color, bone, seg = 8, rings = 5) {
    this.ctr = c;
    const P = (i, j) => {
      const th = (j / rings) * Math.PI, ph = (i / seg) * Math.PI * 2;
      return [c[0] + rx * Math.sin(th) * Math.cos(ph), c[1] + ry * Math.cos(th), c[2] + rz * Math.sin(th) * Math.sin(ph)];
    };
    for (let j = 0; j < rings; j++) {
      for (let i = 0; i < seg; i++) {
        const a = P(i, j), b = P(i + 1, j), cc = P(i + 1, j + 1), d = P(i, j + 1);
        if (j === 0) this.tri(a, d, cc, color, bone);
        else if (j === rings - 1) this.tri(a, d, b, color, bone);
        else this.quad(a, d, cc, b, color, bone);
      }
    }
  }
  get geometry() { return { pos: this.pos, nrm: this.nrm, col: this.col, idx: this.idx, joints: this.joints, weights: this.weights }; }
}
