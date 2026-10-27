// Writes assets/scenes/stage1..3.cwscene: the light design of the three stages (sky, sun, fill, accent and rim lights,
// fog, post and grade). The geometry is in models/stageN.glb (tools/make-world.mjs), loaded by the game.
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

// Rim0 and Rim1 are moved by the game to stand behind each fighter (a back light that outlines them against the stage).
const rims = (s, c, e) => { for (let i = 0; i < 2; i++) s.omni(`Rim${i}`, [0, 2.4, -2.6], c, e, 7, { fog_energy: '0' }); };

// ── stage 1: rooftop at dusk ───────────────────────────────────────────
{
  const s = new Scene('Rooftop Dusk');
  s.node('Sky', 'ProceduralSky', { sun: '"../Sun"', time_of_day: '18.55', latitude: '35', declination: '15', north: '195', sun_energy: '7', sky_energy: '1.25',
    mie: '2.8', sun_disk_intensity: '1.4', ground_color: col(lin([0.3, 0.2, 0.18])), env_update_angle: '2' });
  s.node('Sun', 'DirectionalLight3D', { shadow: 'true', shadow_size: '2048', shadow_extent: '14', shadow_far: '60', shadow_bias: '-0.0003', shadow_normal_bias: '0.03',
    shadow_cascades: '3', shadow_distance: '40' });
  s.node('Env', 'WorldEnvironment', { background: col(lin([0.1, 0.12, 0.25])), ambient_color: col(lin([0.6, 0.55, 0.75])), ambient_energy: '0.06',
    sky_color: col(lin([0.55, 0.5, 0.75])), ground_color: col(lin([0.35, 0.22, 0.18])), hemi_energy: '0.45' });
  s.node('Fog', 'HeightFog', { density: '0.0045', height_falloff: '0.02', base_height: '-34', use_sky_color: 'true', sun_scatter: '0.6', sun_g: '0.8', max_opacity: '0.85', start_distance: '14', sky_distance: '400' });
  s.node('Post', 'PostProcess', { tonemap: 'AGX', exposure: '1.05', bloom: 'true', bloom_intensity: '0.12', bloom_threshold: '1.0', bloom_knee: '0.6', ssao: 'true', ssao_radius: '0.7',
    ssao_intensity: '1.1', aa: 'SMAA', vignette: 'true', vignette_intensity: '0.32', vignette_smoothness: '0.6', grade: 'true', lut: '"grades/dusk.cube"', grade_intensity: '0.85' });
  s.node('Camera', 'Camera3D', { position: v3([0, 1.6, 9]), fov: '34', near: '0.3', far: '700', interpolate: 'false' });
  // warm string lights over the back of the roof, the door lamp, a cool fill from the city
  s.omni('Warm1', [-5, 3.6, -5.6], [1, 0.62, 0.32], 7, 12);
  s.omni('Warm2', [4, 3.3, -5.4], [1, 0.62, 0.32], 7, 12);
  s.omni('Door', [9.6, 2.1, -5.2], [1, 0.75, 0.45], 5, 7);
  s.omni('CityFill', [0, 4, 8], [0.45, 0.55, 1], 4, 22);
  rims(s, [1, 0.7, 0.45], 9);
  s.flashes();
  s.write('stage1.cwscene');
}

// ── stage 2: a neon alley at night ─────────────────────────────────────
{
  const s = new Scene('Neon Alley');
  s.node('Sun', 'DirectionalLight3D', { color: col(lin([0.45, 0.55, 1])), energy: '0.55', rotation: v3([-0.95, 0.5, 0]), shadow: 'true', shadow_size: '2048', shadow_extent: '14', shadow_far: '60',
    shadow_bias: '-0.0003', shadow_normal_bias: '0.03', shadow_cascades: '2', shadow_distance: '40' });
  s.node('Env', 'WorldEnvironment', { background: col(lin([0.02, 0.025, 0.06])), ambient_color: col(lin([0.35, 0.4, 0.8])), ambient_energy: '0.3',
    sky_color: col(lin([0.12, 0.16, 0.4])), ground_color: col(lin([0.05, 0.04, 0.08])), hemi_energy: '0.8' });
  s.node('Fog', 'HeightFog', { density: '0.02', height_falloff: '0.16', base_height: '0', color: col(lin([0.12, 0.08, 0.22])), sun_scatter: '0.0', max_opacity: '0.75', start_distance: '7', sky_distance: '200' });
  s.node('Post', 'PostProcess', { tonemap: 'AGX', exposure: '1.5', bloom: 'true', bloom_intensity: '0.24', bloom_threshold: '0.85', bloom_knee: '0.6', ssao: 'true', ssao_radius: '0.8',
    ssao_intensity: '1.1', aa: 'SMAA', vignette: 'true', vignette_intensity: '0.38', vignette_smoothness: '0.55', grade: 'true', lut: '"grades/neon.cube"', grade_intensity: '0.85',
    chromatic_aberration: 'false' });
  s.node('Camera', 'Camera3D', { position: v3([0, 1.6, 9]), fov: '34', near: '0.3', far: '300', interpolate: 'false' });
  // each neon sign lights the wet ground and the fighters in its colour; the lanterns glow red; the passage glows violet
  const signs = [[-6.5, 5.2, [1, 0.1, 0.55]], [6.0, 6.2, [0.1, 0.85, 1]], [12.6, 4.4, [1, 0.7, 0.1]], [0, 9.0, [0.6, 0.25, 1]]];
  signs.forEach(([x, y, c], i) => s.omni(`NeonL${i}`, [x, y - 0.6, -4.6], c, 11, 13));
  s.omni('Lanterns', [0, 6.2, -2], [1, 0.25, 0.12], 7, 12);
  s.omni('Passage', [0, 3, -12], [0.55, 0.3, 1], 14, 16);
  s.omni('Vending', [2, 1.2, -4.8], [0.8, 0.9, 1], 3, 5);
  rims(s, [0.4, 0.65, 1], 26);
  s.omni('Fill', [0, 3.2, 6], [0.75, 0.55, 1], 6, 16);
  s.flashes();
  s.write('stage2.cwscene');
}

// ── stage 3: a dojo courtyard in the afternoon ─────────────────────────
{
  const s = new Scene('Dojo Courtyard');
  s.node('Sky', 'ProceduralSky', { sun: '"../Sun"', time_of_day: '15.6', latitude: '35', declination: '20', north: '150', sun_energy: '9', sky_energy: '1.3', mie: '1.4',
    sun_disk_intensity: '1.0', ground_color: col(lin([0.4, 0.35, 0.28])), env_update_angle: '2' });
  s.node('Sun', 'DirectionalLight3D', { shadow: 'true', shadow_size: '2048', shadow_extent: '14', shadow_far: '60', shadow_bias: '-0.0003', shadow_normal_bias: '0.03',
    shadow_cascades: '3', shadow_distance: '40' });
  s.node('Env', 'WorldEnvironment', { background: col(lin([0.35, 0.55, 0.9])), ambient_color: col(lin([0.7, 0.8, 1])), ambient_energy: '0.1', sky_color: col(lin([0.55, 0.7, 1])),
    ground_color: col(lin([0.45, 0.38, 0.3])), hemi_energy: '0.55' });
  s.node('Fog', 'HeightFog', { density: '0.0035', height_falloff: '0.012', base_height: '-20', use_sky_color: 'true', sun_scatter: '0.35', sun_g: '0.7', max_opacity: '0.8', start_distance: '25', sky_distance: '500' });
  s.node('Post', 'PostProcess', { tonemap: 'AGX', exposure: '0.95', bloom: 'true', bloom_intensity: '0.08', bloom_threshold: '1.1', bloom_knee: '0.6', ssao: 'true', ssao_radius: '0.7',
    ssao_intensity: '1.0', aa: 'SMAA', vignette: 'true', vignette_intensity: '0.22', vignette_smoothness: '0.6', grade: 'true', lut: '"grades/day.cube"', grade_intensity: '0.75' });
  s.node('Camera', 'Camera3D', { position: v3([0, 1.6, 9]), fov: '34', near: '0.3', far: '900', interpolate: 'false' });
  for (const x of [-7.6, 7.6]) s.omni(`LanternL${x > 0 ? 1 : 0}`, [x, 1.55, -4.4], [1, 0.7, 0.35], 3, 6);
  s.omni('Hall', [0, 2.5, -6.2], [1, 0.75, 0.45], 6, 10);
  rims(s, [1, 0.92, 0.8], 7);
  s.flashes();
  s.write('stage3.cwscene');
}
console.log('wrote 3 stages');
