#!/usr/bin/env node
// crawler's pack manifest: writes assets/roots.txt from what is in assets/ (the scenes, every model, texture, grade
// and effect sheet, every floor with its bake and bake hash). Run it after adding assets or floors; gen-levels.mjs
// runs it too. The game and crawler_levelview pack the same list.
import { existsSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const A = join(dirname(fileURLToPath(import.meta.url)), '../assets');
const list = (d, re) => (existsSync(join(A, d)) ? readdirSync(join(A, d)).filter((f) => re.test(f)).sort().map((f) => `${d}/${f}`) : []);
const lines = [
  '# crawler: what its carts pack. Written by tools/gen-roots.mjs (edit that, not this file).',
  'project.cwproj',
  ...list('scenes', /\.cwscene$/),
  ...list('models', /\.cwchar$/),
  ...list('textures', /\.png$/),
  ...list('grade', /\.cube$/),
  ...list('fx', /\.png$/),
  'levels/count.txt',
  ...list('levels', /^\d{3}\.cwmaze$/),
  ...list('lightmaps', /^\d{3}\.(cwlm|hash)$/),
];
writeFileSync(join(A, 'roots.txt'), lines.join('\n') + '\n');
console.log(`gen-roots: ${lines.length - 1} entries`);
