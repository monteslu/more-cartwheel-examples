// Writes tests/coopplatformer-save-v4.dat: a save block as the game wrote it at save version 4 (before the big coins),
// with settings and progress that are not the defaults, for tests/coopplatformer-save.mjs. The shell's block
// (cartwheel/shell.h, engine v0.38.3): magic, shell version 1, game version, game size, an FNV-1a checksum of
// everything from the settings on, the settings, then the game's save_t. The bytes after the version 4 save_t (where
// version 5 keeps its big coins) are filled with 0xff, so the check sees the migration start the big coins empty.
//   node tests/coopplatformer-save-v4-make.mjs
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const GAME_MAX = 6144, GAME_AT = 32, V4 = 4144; // save_t up to `medals`: 12 + seen[16][256] + 9 words
const b = Buffer.alloc(GAME_AT + GAME_MAX);
b.writeUInt32LE(0x4c485357, 0); // "WSHL"
b.writeUInt16LE(1, 4); // shell version
b.writeUInt16LE(4, 6); // game version
b.writeUInt32LE(V4, 8); // game size
b[17] = 3; // music volume (default 8)
b[18] = 5; // effects volume
b[24] = 2; // screen shake: Off
const g = GAME_AT;
b.writeUInt32LE(3, g); // abilities
b.writeInt32LE(123, g + 4); // coins
b.writeInt32LE(9000, g + 8); // best ticks
for (let i = 0; i < 40; i++) b[g + 12 + i] = 1; // seen
let o = g + 12 + 16 * 256;
for (const v of [0x1f, 7, 9, 400, 42, 5, 2, 0x41, 0]) { b.writeUInt32LE(v >>> 0, o); o += 4; } // reached, level, unlocked, totals, hats, hat_levels
if (o !== g + V4) throw new Error(`layout: ${o - g} != ${V4}`);
b.fill(0xff, g + V4, g + V4 + 32);
let h = 2166136261;
for (let i = 16; i < b.length; i++) h = Math.imul(h ^ b[i], 16777619) >>> 0;
b.writeUInt32LE(h, 12);
writeFileSync(join(dirname(fileURLToPath(import.meta.url)), 'coopplatformer-save-v4.dat'), b);
