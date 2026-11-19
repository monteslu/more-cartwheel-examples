// A baseline JPEG encoder (JFIF, 4:4:4, the standard tables of ITU T.81 Annex K), no dependencies: what the
// launcher art is saved as. encodeJpeg(w, h, rgba, quality 1..100) -> Buffer.
const ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63];
const Q_LUMA = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
  18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99];
const Q_CHROMA = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
  ...new Array(32).fill(99)];
const DC_LUMA = { bits: [0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0], vals: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] };
const DC_CHROMA = { bits: [0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0], vals: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] };
const AC_LUMA = { bits: [0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d], vals: [
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
  0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
  0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa] };
const AC_CHROMA = { bits: [0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77], vals: [
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
  0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
  0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa] };

/** symbol -> [code, length] from a table's code-length counts */
function codes({ bits, vals }) {
  const out = new Map();
  let code = 0, k = 0;
  for (let len = 1; len <= 16; len++) {
    for (let i = 0; i < bits[len - 1]; i++) out.set(vals[k++], [code++, len]);
    code <<= 1;
  }
  return out;
}

/** a quality-scaled quantisation table, natural order (the IJG scaling) */
function scaleTable(base, quality) {
  const q = Math.min(100, Math.max(1, quality | 0)), s = q < 50 ? 5000 / q : 200 - 2 * q;
  return base.map((v) => Math.min(255, Math.max(1, Math.floor((v * s + 50) / 100))));
}

// the 8-point DCT basis: COS[u * 8 + x] = C(u) / 2 * cos((2x + 1) u pi / 16)
const COS = new Float64Array(64);
for (let u = 0; u < 8; u++) for (let x = 0; x < 8; x++) COS[u * 8 + x] = (u ? 1 : Math.SQRT1_2) / 2 * Math.cos((2 * x + 1) * u * Math.PI / 16);

/**
 * @param {number} w
 * @param {number} h
 * @param {Uint8Array} rgba  w * h * 4 bytes, alpha ignored
 * @param {number} [quality]
 * @returns {Buffer}
 */
export function encodeJpeg(w, h, rgba, quality = 92) {
  const qt = [scaleTable(Q_LUMA, quality), scaleTable(Q_CHROMA, quality)];
  const huff = { dc: [codes(DC_LUMA), codes(DC_CHROMA)], ac: [codes(AC_LUMA), codes(AC_CHROMA)] };
  const head = [];
  const u16 = (v) => [(v >> 8) & 255, v & 255];
  const seg = (marker, body) => head.push(0xff, marker, ...u16(body.length + 2), ...body);
  head.push(0xff, 0xd8);
  seg(0xe0, [0x4a, 0x46, 0x49, 0x46, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0]);
  seg(0xdb, [0, ...ZIGZAG.map((i) => qt[0][i]), 1, ...ZIGZAG.map((i) => qt[1][i])]);
  seg(0xc0, [8, ...u16(h), ...u16(w), 3, 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1]);
  const dht = (cls, id, t) => [(cls << 4) | id, ...t.bits, ...t.vals];
  seg(0xc4, [...dht(0, 0, DC_LUMA), ...dht(1, 0, AC_LUMA), ...dht(0, 1, DC_CHROMA), ...dht(1, 1, AC_CHROMA)]);
  seg(0xda, [3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0]);

  // the entropy-coded scan, 0xff bytes stuffed
  let out = Buffer.alloc(1 << 20), n = 0, acc = 0, nacc = 0;
  const byte = (b) => {
    if (n + 2 > out.length) { const bigger = Buffer.alloc(out.length * 2); out.copy(bigger, 0, 0, n); out = bigger; }
    out[n++] = b;
    if (b === 0xff) out[n++] = 0;
  };
  const put = (code, len) => {
    acc = (acc << len) | code; nacc += len;
    while (nacc >= 8) { nacc -= 8; byte((acc >> nacc) & 255); }
    acc &= (1 << nacc) - 1;
  };
  const size = (v) => { let a = Math.abs(v), s = 0; while (a) { s++; a >>= 1; } return s; };
  const bitsOf = (v, s) => (v >= 0 ? v : v + (1 << s) - 1);

  const block = new Float64Array(64), tmp = new Float64Array(64), coef = new Int32Array(64);
  const pred = [0, 0, 0];
  const planes = [new Float64Array(w * h), new Float64Array(w * h), new Float64Array(w * h)];
  for (let i = 0; i < w * h; i++) {
    const r = rgba[4 * i], g = rgba[4 * i + 1], b = rgba[4 * i + 2];
    planes[0][i] = 0.299 * r + 0.587 * g + 0.114 * b - 128;
    planes[1][i] = -0.168736 * r - 0.331264 * g + 0.5 * b;
    planes[2][i] = 0.5 * r - 0.418688 * g - 0.081312 * b;
  }
  for (let by = 0; by < h; by += 8) for (let bx = 0; bx < w; bx += 8) {
    for (let c = 0; c < 3; c++) {
      const p = planes[c], t = c ? 1 : 0;
      for (let y = 0; y < 8; y++) {
        const sy = Math.min(h - 1, by + y) * w;
        for (let x = 0; x < 8; x++) block[y * 8 + x] = p[sy + Math.min(w - 1, bx + x)];
      }
      // rows then columns
      for (let y = 0; y < 8; y++) for (let u = 0; u < 8; u++) {
        let s = 0;
        for (let x = 0; x < 8; x++) s += COS[u * 8 + x] * block[y * 8 + x];
        tmp[y * 8 + u] = s;
      }
      for (let u = 0; u < 8; u++) for (let v = 0; v < 8; v++) {
        let s = 0;
        for (let y = 0; y < 8; y++) s += COS[v * 8 + y] * tmp[y * 8 + u];
        coef[v * 8 + u] = Math.round(s / qt[t][v * 8 + u]);
      }
      const dc = coef[0], diff = dc - pred[c];
      pred[c] = dc;
      const ds = size(diff), dcode = huff.dc[t].get(ds);
      put(dcode[0], dcode[1]);
      if (ds) put(bitsOf(diff, ds), ds);
      let run = 0;
      for (let k = 1; k < 64; k++) {
        const v = coef[ZIGZAG[k]];
        if (!v) { run++; continue; }
        while (run > 15) { const z = huff.ac[t].get(0xf0); put(z[0], z[1]); run -= 16; }
        const s = size(v), ac = huff.ac[t].get((run << 4) | s);
        put(ac[0], ac[1]); put(bitsOf(v, s), s);
        run = 0;
      }
      if (run) { const e = huff.ac[t].get(0); put(e[0], e[1]); }
    }
  }
  if (nacc) put((1 << (8 - nacc)) - 1, 8 - nacc);
  return Buffer.concat([Buffer.from(head), out.subarray(0, n), Buffer.from([0xff, 0xd9])]);
}
