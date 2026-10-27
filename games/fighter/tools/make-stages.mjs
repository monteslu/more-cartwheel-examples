// Writes assets/scenes/stage1..3.cwscene: three stages with their own light design.
// node tools/make-stages.mjs
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, '..', 'assets', 'scenes');

const f = (n) => (Math.round(n * 10000) / 10000).toString();
const v3 = (a) => `vec3(${a.map(f).join(', ')})`;
const col = (c, a = 1) => `color(${f(c[0])}, ${f(c[1])}, ${f(c[2])}, ${a})`;
const lin = (c) => c.map((x) => (x <= 0.04045 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4));

class Scene {
  constructor(title) { this.t = ['[scene format=1]', '', `[node name="Stage" type="Node3D"]`, `title = "${title}"`, '']; }
  node(name, type, props = {}, parent = '.') {
    this.t.push(`[node name="${name}" parent="${parent}" type="${type}"]`);
    for (const [k, v] of Object.entries(props)) this.t.push(`${k} = ${v}`);
    this.t.push('');
  }
  mesh(name, prim, size, pos, c, o = {}) {
    const p = { primitive: prim, size: v3(size), position: v3(pos), color: col(lin(c)), roughness: f(o.rough ?? 0.8), metalness: f(o.metal ?? 0),
                cast_shadow: o.shadow === false ? 'false' : 'true', receive_shadow: 'true' };
    if (o.rot) p.rotation = v3(o.rot);
    if (o.emissive) p.emissive = col(lin(o.emissive));
    if (o.sheen) p.sheen = f(o.sheen);
    if (o.clearcoat) p.clearcoat = f(o.clearcoat);
    this.node(name, 'MeshInstance3D', p);
  }
  omni(name, pos, c, energy, range, extra = {}) {
    this.node(name, 'OmniLight3D', { position: v3(pos), color: col(lin(c)), energy: f(energy), range: f(range), decay: '2', ...extra });
  }
  flashes() { for (let i = 0; i < 3; i++) this.omni(`Flash${i}`, [0, -50, 0], [1, 0.85, 0.6], 0, 9); }
  write(file) { writeFileSync(join(out, file), this.t.join('\n')); }
}

// ── stage 1: rooftop at dusk ───────────────────────────────────────────
{
  const s = new Scene('Rooftop Dusk');
  s.node('Sky', 'ProceduralSky', { sun: '"../Sun"', time_of_day: '18.4', latitude: '35', declination: '15', north: '160', sun_energy: '7', sky_energy: '1.2',
    mie: '2.4', sun_disk_intensity: '1.2', ground_color: col(lin([0.35, 0.22, 0.18])), env_update_angle: '2' });
  s.node('Sun', 'DirectionalLight3D', { shadow: 'true', shadow_size: '2048', shadow_extent: '14', shadow_far: '60', shadow_bias: '-0.0003', shadow_normal_bias: '0.03',
    shadow_cascades: '3', shadow_distance: '40' });
  s.node('Env', 'WorldEnvironment', { background: col(lin([0.1, 0.12, 0.25])), ambient_color: col(lin([1, 0.6, 0.4])), ambient_energy: '0.05',
    sky_color: col(lin([1, 0.62, 0.42])), ground_color: col(lin([0.35, 0.22, 0.18])), hemi_energy: '0.35' });
  s.node('Fog', 'HeightFog', { density: '0.006', height_falloff: '0.08', base_height: '0', use_sky_color: 'true', sun_scatter: '0.45', sun_g: '0.8', max_opacity: '0.65', start_distance: '12', sky_distance: '300' });
  s.node('Post', 'PostProcess', { tonemap: 'AGX', exposure: '1.0', bloom: 'true', bloom_intensity: '0.09', bloom_threshold: '1.0', bloom_knee: '0.6', ssao: 'true', ssao_radius: '0.7',
    ssao_intensity: '1.0', aa: 'SMAA', vignette: 'true', vignette_intensity: '0.3', vignette_smoothness: '0.6', grade: 'true', lut: '"grades/dusk.cube"', grade_intensity: '0.8' });
  s.node('Camera', 'Camera3D', { position: v3([0, 1.6, 9]), fov: '34', near: '0.5', far: '400', interpolate: 'false' });
  s.mesh('tex_tile_floor', 'BOX', [20, 0.5, 8], [0, -0.25, -0.5], [0.6, 0.6, 0.6], { rough: 1.0 });
  s.mesh('tex_concrete_trim', 'BOX', [20.4, 0.25, 0.4], [0, 0.12, 3.5], [0.5, 0.5, 0.5], { rough: 1.0 });
  s.mesh('tex_concrete_wallL', 'BOX', [0.8, 3.2, 9], [-10.4, 1.4, -0.5], [0.5, 0.5, 0.5]);
  s.mesh('tex_concrete_wallR', 'BOX', [0.8, 3.2, 9], [10.4, 1.4, -0.5], [0.5, 0.5, 0.5]);
  s.mesh('tex_concrete_back', 'BOX', [21.6, 1.4, 0.8], [0, 0.45, -4.6], [0.45, 0.45, 0.47]);
  // railing posts and rail
  for (let i = 0; i < 9; i++) s.mesh(`tex_metal_post${i}`, 'CYLINDER', [0.12, 1.2, 0.12], [-8 + i * 2, 1.65, -4.6], [0.35, 0.35, 0.38], { metal: 0.9, rough: 0.35 });
  s.mesh('tex_metal_rail', 'BOX', [18.2, 0.08, 0.08], [0, 2.2, -4.6], [0.4, 0.4, 0.43], { metal: 0.9, rough: 0.3 });
  // water tanks and vents on the far side
  s.mesh('tex_metal_tank1', 'CYLINDER', [3.2, 3.6, 3.2], [-13, 2.0, -9], [0.4, 0.33, 0.28], { metal: 0.6, rough: 0.55 });
  s.mesh('tex_metal_tank2', 'CYLINDER', [2.6, 3.0, 2.6], [12, 1.7, -8], [0.38, 0.32, 0.28], { metal: 0.6, rough: 0.6 });
  s.mesh('tex_concrete_vent', 'BOX', [3, 2, 2], [9, 1, -7.5], [0.4, 0.4, 0.42]);
  // the skyline: dark towers with lit windows
  const rng = (() => { let a = 1234567; return () => ((a = (a * 1664525 + 1013904223) >>> 0) / 4294967296); })();
  for (let i = 0; i < 26; i++) {
    const x = -70 + i * 5.6 + rng() * 2, h = 12 + rng() * 38, d = 5 + rng() * 5, z = -34 - rng() * 30;
    s.mesh(`bld${i}`, 'BOX', [4.2 + rng() * 2, h, d], [x, h / 2 - 3, z], [0.12 + rng() * 0.05, 0.1 + rng() * 0.05, 0.14 + rng() * 0.05], { rough: 0.9, shadow: false });
    for (let k = 0; k < 4; k++) if (rng() > 0.35) s.mesh(`win${i}_${k}`, 'BOX', [3.4, 0.5, 0.1], [x, 1 + rng() * (h - 6), z + d / 2 + 0.06], [1, 0.8, 0.5], { rough: 1, emissive: [1.0, 0.72, 0.35], shadow: false });
  }
  // string lights across the roof
  for (let i = 0; i < 9; i++) s.mesh(`bulb${i}`, 'SPHERE', [0.22, 0.22, 0.22], [-8 + i * 2, 4.6 - 0.35 * Math.cos((i / 8) * Math.PI * 2 - Math.PI), -3.0], [1, 0.9, 0.7], { emissive: [3.2, 2.4, 1.4], rough: 0.4, shadow: false });
  s.omni('Warm1', [-6, 4.4, -2.5], [1, 0.62, 0.32], 9, 16);
  s.omni('Warm2', [6, 4.4, -2.5], [1, 0.62, 0.32], 9, 16);
  s.omni('Rim', [0, 3.2, -6], [0.45, 0.6, 1], 11, 20);
  s.flashes();
  s.write('stage1.cwscene');
}

// ── stage 2: a neon alley at night ─────────────────────────────────────
{
  const s = new Scene('Neon Alley');
  s.node('Sun', 'DirectionalLight3D', { color: col(lin([0.45, 0.55, 1])), energy: '0.7', rotation: v3([-0.9, 0.6, 0]), shadow: 'true', shadow_size: '2048', shadow_extent: '14', shadow_far: '60',
    shadow_bias: '-0.0003', shadow_normal_bias: '0.03', shadow_cascades: '2', shadow_distance: '40' });
  s.node('Env', 'WorldEnvironment', { background: col(lin([0.02, 0.025, 0.06])), ambient_color: col(lin([0.35, 0.4, 0.8])), ambient_energy: '0.1',
    sky_color: col(lin([0.12, 0.16, 0.4])), ground_color: col(lin([0.05, 0.04, 0.08])), hemi_energy: '0.4' });
  s.node('Fog', 'HeightFog', { density: '0.02', height_falloff: '0.18', base_height: '0', color: col(lin([0.1, 0.07, 0.2])), sun_scatter: '0.0', max_opacity: '0.7', start_distance: '8', sky_distance: '200' });
  s.node('Post', 'PostProcess', { tonemap: 'AGX', exposure: '1.1', bloom: 'true', bloom_intensity: '0.22', bloom_threshold: '0.8', bloom_knee: '0.6', ssao: 'true', ssao_radius: '0.8',
    ssao_intensity: '1.1', ssr: 'true', ssr_intensity: '0.6', aa: 'SMAA', vignette: 'true', vignette_intensity: '0.38', vignette_smoothness: '0.55', grade: 'true', lut: '"grades/neon.cube"', grade_intensity: '0.85',
    chromatic_aberration: 'true', ca_intensity: '0.15' });
  s.node('Camera', 'Camera3D', { position: v3([0, 1.6, 9]), fov: '34', near: '0.5', far: '300', interpolate: 'false' });
  s.mesh('tex_metal_floor', 'BOX', [20, 0.5, 9], [0, -0.25, -0.5], [0.45, 0.47, 0.5], { rough: 0.45, metal: 0.4 });
  s.mesh('tex_brick_back', 'BOX', [24, 9, 0.8], [0, 4.5, -5], [0.55, 0.42, 0.38]);
  s.mesh('tex_brick_wallL', 'BOX', [0.8, 9, 10], [-11, 4.5, -0.5], [0.5, 0.4, 0.36]);
  s.mesh('tex_brick_wallR', 'BOX', [0.8, 9, 10], [11, 4.5, -0.5], [0.5, 0.4, 0.36]);
  // pipes, dumpsters, crates
  for (let i = 0; i < 4; i++) s.mesh(`tex_metal_pipe${i}`, 'CYLINDER', [0.22, 8, 0.22], [-9 + i * 5.5, 4, -4.5], [0.3, 0.3, 0.32], { metal: 0.85, rough: 0.4 });
  s.mesh('tex_metal_dump1', 'BOX', [2.6, 1.4, 1.3], [-8.4, 0.7, -3.4], [0.15, 0.35, 0.22], { metal: 0.5, rough: 0.55 });
  s.mesh('tex_metal_dump2', 'BOX', [2.6, 1.4, 1.3], [8.6, 0.7, -3.2], [0.3, 0.17, 0.15], { metal: 0.5, rough: 0.55 });
  s.mesh('tex_wood_crate1', 'BOX', [0.9, 0.9, 0.9], [-6.2, 0.45, -3.6], [0.5, 0.4, 0.3]);
  s.mesh('tex_wood_crate2', 'BOX', [0.8, 0.8, 0.8], [6.4, 0.4, -3.8], [0.5, 0.4, 0.3]);
  // neon signs: emissive slabs with matching lights
  const signs = [[-6.5, 5.2, [1, 0.1, 0.55], 4.4, 1.0], [-1.5, 6.2, [0.1, 0.9, 1], 5.5, 0.8], [4.0, 4.6, [0.7, 0.25, 1], 3.8, 0.9], [8.2, 6.4, [1, 0.8, 0.1], 3.0, 0.7]];
  signs.forEach(([x, y, c, w, h], i) => {
    s.mesh(`sign${i}`, 'BOX', [w, h, 0.15], [x, y, -4.5], c.map((a) => a * 0.2), { rough: 0.4, emissive: c.map((a) => a * 4.0), shadow: false });
    s.omni(`NeonL${i}`, [x, y - 0.4, -2.6], c, 12, 14);
  });
  s.mesh('neonstrip', 'BOX', [20, 0.06, 0.06], [0, 0.04, 3.2], [0.1, 0.1, 0.1], { emissive: [0.2, 1.8, 2.4], shadow: false });
  s.omni('RimCool', [0, 3, -5], [0.3, 0.45, 1], 10, 18);
  s.flashes();
  s.write('stage2.cwscene');
}

// ── stage 3: a dojo courtyard by day ───────────────────────────────────
{
  const s = new Scene('Dojo Courtyard');
  s.node('Sky', 'ProceduralSky', { sun: '"../Sun"', time_of_day: '14.2', latitude: '35', declination: '20', north: '30', sun_energy: '9', sky_energy: '1.3', mie: '1.0',
    sun_disk_intensity: '1.0', ground_color: col(lin([0.4, 0.35, 0.28])), env_update_angle: '2' });
  s.node('Sun', 'DirectionalLight3D', { shadow: 'true', shadow_size: '2048', shadow_extent: '14', shadow_far: '60', shadow_bias: '-0.0003', shadow_normal_bias: '0.03',
    shadow_cascades: '3', shadow_distance: '40' });
  s.node('Env', 'WorldEnvironment', { background: col(lin([0.35, 0.55, 0.9])), ambient_color: col(lin([0.7, 0.8, 1])), ambient_energy: '0.1', sky_color: col(lin([0.55, 0.7, 1])),
    ground_color: col(lin([0.4, 0.35, 0.28])), hemi_energy: '0.5' });
  s.node('Fog', 'HeightFog', { density: '0.004', height_falloff: '0.05', base_height: '0', use_sky_color: 'true', sun_scatter: '0.3', sun_g: '0.7', max_opacity: '0.4', start_distance: '20', sky_distance: '300' });
  s.node('Post', 'PostProcess', { tonemap: 'AGX', exposure: '0.95', bloom: 'true', bloom_intensity: '0.07', bloom_threshold: '1.1', bloom_knee: '0.6', ssao: 'true', ssao_radius: '0.7',
    ssao_intensity: '0.95', aa: 'SMAA', vignette: 'true', vignette_intensity: '0.22', vignette_smoothness: '0.6', grade: 'true', lut: '"grades/day.cube"', grade_intensity: '0.7' });
  s.node('Camera', 'Camera3D', { position: v3([0, 1.6, 9]), fov: '34', near: '0.5', far: '400', interpolate: 'false' });
  s.mesh('tex_wood_floor', 'BOX', [20, 0.5, 9], [0, -0.25, -0.5], [0.8, 0.62, 0.45], { rough: 0.7 });
  s.mesh('tex_concrete_step', 'BOX', [20.6, 0.35, 1.2], [0, -0.1, 4.6], [0.5, 0.48, 0.45]);
  // the hall behind: red pillars, a roof beam, paper screens
  for (let i = 0; i < 7; i++) s.mesh(`pillar${i}`, 'CYLINDER', [0.55, 6, 0.55], [-9 + i * 3, 3, -4.4], [0.6, 0.1, 0.08], { rough: 0.45, clearcoat: 0.5 });
  s.mesh('beam', 'BOX', [20, 0.5, 0.7], [0, 6.1, -4.4], [0.3, 0.07, 0.05], { rough: 0.5, clearcoat: 0.3 });
  s.mesh('roof', 'BOX', [22, 0.4, 5], [0, 6.9, -4.8], [0.12, 0.12, 0.13], { rough: 0.5, metal: 0.2 });
  for (let i = 0; i < 6; i++) s.mesh(`screen${i}`, 'BOX', [2.4, 4.6, 0.12], [-7.5 + i * 3, 2.6, -4.2], [0.95, 0.9, 0.78], { rough: 0.9, emissive: [0.5, 0.42, 0.28], shadow: false });
  // stone lanterns and a tree
  for (const x of [-8.6, 8.6]) {
    s.mesh(`tex_concrete_lantern_b${x}`, 'BOX', [0.7, 0.4, 0.7], [x, 0.2, 2.0], [0.5, 0.5, 0.48]);
    s.mesh(`tex_concrete_lantern_c${x}`, 'CYLINDER', [0.3, 1.2, 0.3], [x, 1.0, 2.0], [0.5, 0.5, 0.48]);
    s.mesh(`lantern_l${x}`, 'BOX', [0.6, 0.5, 0.6], [x, 1.85, 2.0], [0.9, 0.7, 0.4], { emissive: [2.0, 1.3, 0.6], rough: 0.6 });
    s.mesh(`tex_concrete_lantern_t${x}`, 'BOX', [0.95, 0.2, 0.95], [x, 2.2, 2.0], [0.5, 0.5, 0.48]);
    s.omni(`LanternL${x > 0 ? 1 : 0}`, [x, 1.85, 2.0], [1, 0.7, 0.35], 4, 8);
  }
  s.mesh('trunk', 'CYLINDER', [0.7, 5, 0.7], [-11, 2.5, -2.2], [0.25, 0.17, 0.12], { rough: 0.95 });
  s.mesh('crown1', 'SPHERE', [5.5, 3.4, 5.5], [-10.5, 6.2, -2.2], [0.95, 0.55, 0.65], { rough: 0.9, sheen: 0.4 });
  s.mesh('crown2', 'SPHERE', [4.0, 2.6, 4.0], [-8.2, 5.4, -3.0], [0.95, 0.6, 0.7], { rough: 0.9, sheen: 0.4 });
  s.flashes();
  s.write('stage3.cwscene');
}
console.log('wrote 3 stages');
