/* The water thrown about: the rooster tail behind each craft, spray off a carve, a mist that hangs in the low sun, and
 * bursts where something lands. Wakes and foam go into the water's ripple layer. */
#include <math.h>

#include "cartwheel/environment.h"
#include "cartwheel/input.h"
#include "cartwheel/scene_file.h"
#include "cartwheel/particles.h"
#include "game.h"
#define BUOY_AT(i) (BUOYS[i])

static cw_node rain_n;
static cw_node tail[RACERS], mist[RACERS], side_spray, bursts, burst_mist, sheet[2], sheet_fine[2];

static cw_node emitter(cw_node parent, const char *name, int amount, float life, float size, const char *ramp, const char *curve, int seed, bool stretched) {
  cw_node n = cw_node_new("GPUParticles3D", name);
  cw_node_set(n, "amount", cw_val_int(amount));
  cw_node_set(n, "lifetime", cw_val_float(life));
  cw_node_set(n, "lifetime_random", cw_val_float(0.35f));
  cw_node_set(n, "size", cw_val_float(size));
  cw_node_set(n, "size_random", cw_val_float(0.55f));
  cw_node_set(n, "color_ramp", cw_val_string(ramp));
  if (curve) cw_node_set(n, "size_curve", cw_val_string(curve));
  cw_node_set(n, "seed", cw_val_int(seed));
  cw_node_set(n, "emitting", cw_val_bool(false));
  cw_node_set(n, "blend", cw_val_enum("ALPHA"));
  cw_node_set(n, "gravity", cw_val_vec3(0, -9.8f, 0));
  if (stretched) {
    cw_node_set(n, "draw_mode", cw_val_enum("STRETCHED"));
    cw_node_set(n, "stretch", cw_val_float(0.006f));
  }
  cw_node_add_child(parent, n);
  return n;
}

void fx_init(cw_node parent) {
  for (int i = 0; i < RACERS; i++) {
    int amount = i == 0 ? 1100 : 300;
    tail[i] = emitter(parent, i ? "TailAI" : "Tail", amount, 1.1f, 0.013f, "0 1 1 1 0.95, 0.5 0.95 0.97 1 0.7, 1 0.9 0.95 1 0", NULL, 100 + i, true);
    cw_node_set(tail[i], "energy", cw_val_float(1.7f));
    cw_node_set(tail[i], "spread", cw_val_float(16));
    cw_node_set(tail[i], "camera_fade", cw_val_float(6));
    cw_node_set(tail[i], "emission_shape", cw_val_enum("SPHERE"));
    cw_node_set(tail[i], "emission_radius", cw_val_float(0.14f));
    mist[i] = emitter(parent, i ? "MistAI" : "Mist", i == 0 ? 90 : 26, 1.5f, 0.9f, "0 1 1 1 0, 0.12 1 1 1 0.16, 1 1 1 1 0", "0 0.35, 0.5 1, 1 1.8", 200 + i, false);
    cw_node_set(mist[i], "texture", cw_val_string("effects/mist.png"));
    cw_node_set(mist[i], "gravity", cw_val_vec3(0, -0.7f, 0));
    cw_node_set(mist[i], "damping", cw_val_float(1.5f));
    cw_node_set(mist[i], "sort", cw_val_bool(i == 0));
    cw_node_set(mist[i], "soft_distance", cw_val_float(0.6f));
    cw_node_set(mist[i], "camera_fade", cw_val_float(5.5f));
    cw_node_set(mist[i], "spread", cw_val_float(40));
    cw_node_set(mist[i], "emission_shape", cw_val_enum("SPHERE"));
    cw_node_set(mist[i], "emission_radius", cw_val_float(0.35f));
  }
  /* the sheets a planing hull throws to each side: soft broad curtains with fine droplets flying out of their tops */
  for (int s = 0; s < 2; s++) {
    sheet[s] = emitter(parent, s ? "SheetR" : "SheetL", 120, 0.6f, 0.7f, "0 1 1 1 0, 0.15 1 1 1 0.09, 0.6 1 1 1 0.05, 1 1 1 1 0", "0 0.3, 0.4 1, 1 1.5", 600 + s, false);
    cw_node_set(sheet[s], "texture", cw_val_string("effects/mist.png"));
    cw_node_set(sheet[s], "camera_fade", cw_val_float(2.2f));
    cw_node_set(sheet[s], "soft_distance", cw_val_float(0.3f));
    cw_node_set(sheet[s], "energy", cw_val_float(1.15f));
    cw_node_set(sheet[s], "spread", cw_val_float(14));
    sheet_fine[s] = emitter(parent, s ? "SheetFineR" : "SheetFineL", 520, 0.7f, 0.017f, "0 1 1 1 0.9, 0.6 1 1 1 0.6, 1 1 1 1 0", NULL, 620 + s, true);
    cw_node_set(sheet_fine[s], "energy", cw_val_float(1.6f));
    cw_node_set(sheet_fine[s], "spread", cw_val_float(18));
    cw_node_set(sheet_fine[s], "camera_fade", cw_val_float(5.5f));
  }
  rain_n = emitter(parent, "Rain", 3000, 0.62f, 0.02f, "0 0.8 0.86 0.95 0, 0.1 0.82 0.88 0.96 0.75, 1 0.82 0.88 0.96 0.5", NULL, 900, true);
  cw_node_set(rain_n, "stretch", cw_val_float(0.022f));
  cw_node_set(rain_n, "gravity", cw_val_vec3(5, -46, -3));
  cw_node_set(rain_n, "emission_shape", cw_val_enum("SPHERE"));
  cw_node_set(rain_n, "emission_radius", cw_val_float(24));
  cw_node_set(rain_n, "energy", cw_val_float(1));
  cw_node_set(rain_n, "spread", cw_val_float(180));
  side_spray = emitter(parent, "Carve", 1000, 0.7f, 0.012f, "0 1 1 1 0.9, 0.6 0.95 0.97 1 0.6, 1 0.9 0.95 1 0", NULL, 300, true);
  cw_node_set(side_spray, "energy", cw_val_float(1.6f));
  cw_node_set(side_spray, "spread", cw_val_float(24));
  cw_node_set(side_spray, "camera_fade", cw_val_float(6.5f));
  bursts = emitter(parent, "Bursts", 2400, 1.2f, 0.016f, "0 1 1 1 1, 0.6 0.95 0.97 1 0.7, 1 0.9 0.95 1 0", NULL, 400, true);
  cw_node_set(bursts, "bursts", cw_val_int(8));
  cw_node_set(bursts, "explosiveness", cw_val_float(1));
  cw_node_set(bursts, "one_shot", cw_val_bool(true));
  cw_node_set(bursts, "direction", cw_val_vec3(0, 1, 0));
  cw_node_set(bursts, "spread", cw_val_float(50));
  cw_node_set(bursts, "speed_min", cw_val_float(3));
  cw_node_set(bursts, "speed_max", cw_val_float(9));
  cw_node_set(bursts, "energy", cw_val_float(1.7f));
  cw_node_set(bursts, "camera_fade", cw_val_float(3));
  burst_mist = emitter(parent, "BurstMist", 128, 2.0f, 1.5f, "0 1 1 1 0, 0.1 1 1 1 0.26, 1 1 1 1 0", "0 0.4, 0.5 1, 1 1.9", 500, false);
  cw_node_set(burst_mist, "texture", cw_val_string("effects/mist.png"));
  cw_node_set(burst_mist, "bursts", cw_val_int(8));
  cw_node_set(burst_mist, "explosiveness", cw_val_float(1));
  cw_node_set(burst_mist, "one_shot", cw_val_bool(true));
  cw_node_set(burst_mist, "gravity", cw_val_vec3(0, -0.8f, 0));
  cw_node_set(burst_mist, "damping", cw_val_float(1.3f));
  cw_node_set(burst_mist, "direction", cw_val_vec3(0, 1, 0));
  cw_node_set(burst_mist, "spread", cw_val_float(70));
  cw_node_set(burst_mist, "speed_min", cw_val_float(1));
  cw_node_set(burst_mist, "speed_max", cw_val_float(4));
  cw_node_set(burst_mist, "soft_distance", cw_val_float(0.8f));
  cw_node_set(burst_mist, "camera_fade", cw_val_float(6));
  cw_node_set(burst_mist, "sort", cw_val_bool(true));
}

/* a squall's rain: a cloud of streaks that stays round the camera */
static void rain_tick(void) {
  if (!cw_node_valid(rain_n)) return;
  static bool was;
  bool on = DEF->rain > 0;
  if (on != was) { cw_node_set(rain_n, "emitting", cw_val_bool(on)); was = on; }
  if (on) cw_node3d_set_position(rain_n, (t3_vec3){ G.look[0] + (G.look[0] - G.cam[0]) * 1.2f - 4, G.cam[1] + 16, G.look[2] + (G.look[2] - G.cam[2]) * 1.2f + 3 });
}

/* Spray lit by the sun. Particles draw unlit, so each frame the spray is tinted from where the sun stands against the
 * view: looking into a low sun the droplets glow in its colour and bloom (energy over 1), with it behind they are plain
 * white water, and at night they take the dim sky's colour. One colour and one energy a system, set only when they move */
float g_spray_glow;     /* how back-lit the spray is now, 0..1, for the look gate */
int g_spray_lit = 1;    /* sw spray_lit 0: the old flat white spray, the control */
static void spray_light(void) {
  cw_sky_state sk;
  cw_sky_get_state(cw_scene_app_tree(), &sk);
  float vx = G.look[0] - G.cam[0], vy = G.look[1] - G.cam[1], vz = G.look[2] - G.cam[2], vl = sqrtf(vx * vx + vy * vy + vz * vz);
  float back = 0, day = 1;
  t3_color sun = { 1, 1, 1 };
  if (sk.active && vl > 1e-3f) {
    float f = (vx * sk.sun_direction.x + vy * sk.sun_direction.y + vz * sk.sun_direction.z) / vl;
    back = fminf(1.0f, fmaxf(0.0f, (f - 0.15f) / 0.75f));
    back *= back * (3.0f - 2.0f * back);
    /* a low sun lights the spray through more than a high one */
    float low = 1.0f - fminf(1.0f, fmaxf(0.0f, (sk.sun_elevation - 8.0f) / 50.0f));
    back *= 0.45f + 0.55f * low;
    day = fminf(1.0f, fmaxf(0.0f, (sk.sun_elevation + 4.0f) / 14.0f)) * fminf(1.0f, sk.sun_energy / 2.0f);
    float m = fmaxf(sk.sun_color.r, fmaxf(sk.sun_color.g, sk.sun_color.b));
    if (m > 1e-3f) sun = (t3_color){ sk.sun_color.r / m, sk.sun_color.g / m, sk.sun_color.b / m };
  }
  if (!g_spray_lit) back = 0, day = 1;
  g_spray_glow = back * day;
  /* front-lit or overcast: white water a shade under the sky; back-lit: the sun's colour, brighter */
  float k = g_spray_glow, base = 0.55f + 0.45f * day;
  float r = base * (1 - k) + sun.r * k, g = base * (1 - k) + sun.g * k, b = base * (1 - k) * 1.03f + sun.b * k;
  static float last[4] = { -1, -1, -1, -1 };
  if (fabsf(r - last[0]) + fabsf(g - last[1]) + fabsf(b - last[2]) + fabsf(k - last[3]) < 0.01f) return;
  last[0] = r, last[1] = g, last[2] = b, last[3] = k;
  cw_value col = cw_val_color(r, g, b, 1);
  for (int i = 0; i < RACERS; i++) {
    cw_node_set(tail[i], "color", col), cw_node_set(tail[i], "energy", cw_val_float(1.7f + 2.2f * k));
    cw_node_set(mist[i], "color", col), cw_node_set(mist[i], "energy", cw_val_float(1.0f + 2.6f * k));
  }
  for (int s = 0; s < 2; s++) {
    cw_node_set(sheet[s], "color", col), cw_node_set(sheet[s], "energy", cw_val_float(1.15f + 2.0f * k));
    cw_node_set(sheet_fine[s], "color", col), cw_node_set(sheet_fine[s], "energy", cw_val_float(1.6f + 2.2f * k));
  }
  cw_node_set(side_spray, "color", col), cw_node_set(side_spray, "energy", cw_val_float(1.6f + 2.2f * k));
  cw_node_set(bursts, "color", col), cw_node_set(bursts, "energy", cw_val_float(1.7f + 2.0f * k));
  cw_node_set(burst_mist, "color", col), cw_node_set(burst_mist, "energy", cw_val_float(1.0f + 2.4f * k));
}

/* Things standing in the sea sit in it: a collar of foam where each ramp leg and each buoy meets the moving surface, thicker
 * as the swell works up and down it. Stamped into the ripple layer each tick near the player (sw waterline 0: none, the control) */
int g_waterline = 1, g_waterline_stamps;
static void waterline_foam(void) {
  g_waterline_stamps = 0;
  if (!g_waterline || G.state == RACE_DONE) return;
  const craft *p = &G.c[0];
  static const float PZ[5] = { -8, 0, 8, 14, 20 }, PX[3] = { -9.75f, 0, 9.75f };
  for (int r = 0; r < RAMPS; r++) {
    float dx = G.ramp_x[r] - p->x, dz = G.ramp_z[r] - p->z;
    if (dx * dx + dz * dz > 140.0f * 140.0f) continue;
    float sy, cy, s = DEF->ramps[r][5] > 0 ? RAMP_BIG_XZ : 1.0f, sv = DEF->ramps[r][5] > 0 ? RAMP_BIG_Y : 1.0f;
    cw_sincos(G.ramp_yaw[r], &sy, &cy);
    for (int a = 0; a < 5; a++) for (int b = 0; b < 3; b++) {
      float xl = PX[b] * s, zl = PZ[a] * s, wx = G.ramp_x[r] + cy * xl + sy * zl, wz = G.ramp_z[r] - sy * xl + cy * zl;
      cw_water_sample w;
      cw_water_sample_at(G.water, wx, wz, G.wt, &w);
      float top = ramp_top_at(r) + ((PZ[a] - RAMP_HALF_LEN_N) * RAMP_SLOPE - 0.5f) * sv;
      if (top < w.height) continue; /* this leg is under its part of the deck below the water */
      cw_water_splash(G.water, wx, wz, 0.95f * s, 0.0f, 0.1f + 0.12f * fminf(1.0f, fabsf(w.velocity.y)));
      g_waterline_stamps++;
    }
  }
  for (int b = 0; b < BUOY_N; b++) {
    float dx = BUOYS[b][0] - p->x, dz = BUOYS[b][1] - p->z;
    if (dx * dx + dz * dz > 140.0f * 140.0f || !buoy_on(b)) continue;
    cw_water_sample w;
    cw_water_sample_at(G.water, BUOYS[b][0], BUOYS[b][1], G.wt, &w);
    cw_water_splash(G.water, BUOYS[b][0], BUOYS[b][1], 1.1f, 0.0f, 0.08f + 0.12f * fminf(1.0f, fabsf(w.velocity.y)));
    g_waterline_stamps++;
  }
}

void fx_tick(float dt) {
  rain_tick();
  spray_light();
  (void)dt;
  for (int i = 0; i < RACERS; i++) {
    craft *c = &G.c[i];
    float sy, cy;
    cw_sincos(c->yaw, &sy, &cy);
    float v = c->speed;
    float k = v / 28.0f * (1.0f + 0.05f * (float)c->meter); /* the spray grows with the meter */
    bool wet = !c->air && c->wet > 0.15f;
    bool on = wet && v > 3 && c->in.throttle > 0.1f;
    /* the jet's rooster tail: up and back, higher with speed */
    t3_vec3 sp = { c->x - sy * 1.65f, c->surf + 0.1f, c->z - cy * 1.65f };
    cw_node3d_set_position(tail[i], sp);
    cw_node_set(tail[i], "emitting", cw_val_bool(on));
    cw_node_set(tail[i], "direction", cw_val_vec3(-sy * 0.45f + c->vx * 0.006f, 0.85f, -cy * 0.45f + c->vz * 0.006f));
    cw_node_set(tail[i], "speed_min", cw_val_float(3.0f + 3.5f * k));
    cw_node_set(tail[i], "speed_max", cw_val_float(5 + 4.5f * k));
    cw_node3d_set_position(mist[i], (t3_vec3){ sp.x - sy * 1.5f, sp.y + 0.5f, sp.z - cy * 1.5f });
    cw_node_set(mist[i], "emitting", cw_val_bool(on && v > 7));
    cw_node_set(mist[i], "direction", cw_val_vec3(c->vx * 0.05f, 0.6f, c->vz * 0.05f));
    cw_node_set(mist[i], "speed_min", cw_val_float(0.5f + 2 * k));
    cw_node_set(mist[i], "speed_max", cw_val_float(1 + 4 * k));
    if (i == 0) {
      /* both flanks, all the time at speed: the outside one higher in a turn */
      for (int fl = 0; fl < 2; fl++) {
        float sd = fl ? -1.0f : 1.0f; /* +1: the hull's left, (cos, -sin) */
        float out = 1.0f + 1.4f * (c->steer * sd > 0 ? fabsf(c->steer) : 0.0f) - 0.5f * (c->steer * sd < 0 ? fabsf(c->steer) : 0.0f);
        t3_vec3 at = { c->x + cy * sd * 0.56f - sy * 0.15f, c->surf + 0.02f, c->z - sy * sd * 0.56f - cy * 0.15f };
        t3_vec3 dir = { cy * sd * 0.62f - sy * 0.12f + c->vx * 0.018f, 0.5f, -sy * sd * 0.62f - cy * 0.12f + c->vz * 0.018f };
        bool go = wet && v > 7;
        cw_node3d_set_position(sheet[fl], at);
        cw_node_set(sheet[fl], "emitting", cw_val_bool(go));
        cw_node_set(sheet[fl], "direction", cw_val_vec3(dir.x, dir.y, dir.z));
        cw_node_set(sheet[fl], "speed_min", cw_val_float((0.8f + 2.0f * k) * out));
        cw_node_set(sheet[fl], "speed_max", cw_val_float((1.6f + 3.4f * k) * out));
        cw_node3d_set_position(sheet_fine[fl], at);
        cw_node_set(sheet_fine[fl], "emitting", cw_val_bool(go));
        cw_node_set(sheet_fine[fl], "direction", cw_val_vec3(dir.x, dir.y * 1.25f, dir.z));
        cw_node_set(sheet_fine[fl], "speed_min", cw_val_float((1.6f + 3 * k) * out));
        cw_node_set(sheet_fine[fl], "speed_max", cw_val_float((3 + 6 * k) * out));
      }
      /* a carve throws a sheet of water off the outside chine */
      float s = c->steer;
      bool carve = wet && fabsf(s) > 0.2f && v > 8;
      float side = s > 0 ? 1.0f : -1.0f;
      /* (the hull's left is (cos, -sin): a right turn throws water out to the left) */
      t3_vec3 cp = { c->x + cy * side * 0.55f + sy * 0.1f, c->surf + 0.05f, c->z - sy * side * 0.55f + cy * 0.1f };
      cw_node3d_set_position(side_spray, cp);
      cw_node_set(side_spray, "emitting", cw_val_bool(carve));
      cw_node_set(side_spray, "direction", cw_val_vec3(cy * side * 0.8f - sy * 0.3f, 0.65f, -sy * side * 0.8f - cy * 0.3f));
      cw_node_set(side_spray, "speed_min", cw_val_float(2.5f + 7 * k * fabsf(s)));
      cw_node_set(side_spray, "speed_max", cw_val_float(4 + 11 * k * fabsf(s)));
    }
    /* wake: a churned trough from the jet (little dents all over it, foam thick at the stern), foam fanning off each side as
     * the hull passes */
    if (wet && v > 1.5f) {
      {
        uint32_t h = G.tick * 2654435761u + (uint32_t)i * 40503u;
        for (int t = 0; t < 3; t++) {
          h = h * 747796405u + 2891336453u;
          float back = 1.2f + (float)(h >> 24) / 255.0f * 5.0f, lat = ((float)((h >> 16) & 255) / 255.0f - 0.5f) * (0.6f + back * 0.24f);
          cw_water_splash(G.water, c->x - sy * back + cy * lat, c->z - cy * back - sy * lat, 0.42f, 0.05f * k, 0.18f + 0.26f * k);
        }
      }
      float rx = cy, rz = -sy;
      cw_water_wake(G.water, c->x - sy * 1.3f, c->z - cy * 1.3f, c->vx, c->vz, 0.5f, 0.045f + 0.03f * k, 0.35f + 0.4f * k);
      cw_water_wake(G.water, c->x + rx * 0.6f + sy * 0.3f, c->z + rz * 0.6f + cy * 0.3f, c->vx, c->vz, 0.34f, 0.035f * k, 0.6f * k);
      cw_water_wake(G.water, c->x - rx * 0.6f + sy * 0.3f, c->z - rz * 0.6f + cy * 0.3f, c->vx, c->vz, 0.34f, 0.035f * k, 0.6f * k);
    }
    /* a ring on the water at each buoy passed cleanly: the water keeps the score */
    if (c->ok_tick > 0 && c->human) {
      const float *b = BUOY_AT(c->ok_tick - 1);
      cw_water_splash(G.water, b[0], b[1], 2.4f + 0.3f * (float)c->meter, 0.16f, 0.5f);
    }
    if (c->big_slide || (c->spill > 1.85f)) {
      t3_vec3 at = { c->x, c->surf + 0.1f, c->z };
      cw_particles_burst(bursts, at, (t3_vec3){ 0, 1, 0 });
      cw_water_splash(G.water, c->x, c->z, 2.0f, 0.3f, 1.0f);
      if (i == 0) cw_input_rumble(0, 0.8f, 0.6f, 260);
      c->big_slide = false;
    }
    if (c->big_splash) {
      /* the crown grows with the blow: one burst for a touch, three for a big landing */
      t3_vec3 at = { c->x, c->surf + 0.1f, c->z };
      int crowns = c->kick > 0.66f ? 3 : c->kick > 0.3f ? 2 : 1;
      for (int b = 0; b < crowns; b++) cw_particles_burst(bursts, (t3_vec3){ at.x + (float)b * 0.5f - 0.5f, at.y, at.z }, (t3_vec3){ 0, 1, 0 });
      if (c->kick > 0.3f) cw_particles_burst(burst_mist, at, (t3_vec3){ 0, 1, 0 });
      cw_water_splash(G.water, c->x, c->z, 1.6f + 2.4f * c->kick, 0.2f + 0.4f * c->kick, 1.0f);
      if (i == 0) cw_input_rumble(0, 0.35f + 0.6f * c->kick, 0.2f + 0.4f * c->kick, 180 + (int)(240 * c->kick));
    }
    if (c->bump && i == 0 && g_ev_on) cw_input_rumble(0, 0.8f, 0.5f, 120);
  }
  /* the event list: a rumble and a burst for what the human just did */
  { static unsigned read;
    if (g_ev_n - read > EV_RING) read = g_ev_n - EV_RING;
    for (; read != g_ev_n; read++) {
      const game_event *e = &g_ev[read % EV_RING];
      static const float rumble[EV_KINDS][3] = { [EV_PASS] = { 0.15f, 0.25f, 70 }, [EV_TOP] = { 0.5f, 0.7f, 220 }, [EV_TRICK] = { 0.3f, 0.5f, 140 }, [EV_MISS] = { 0.5f, 0.1f, 200 }, [EV_GAIN] = { 0.2f, 0.35f, 90 }, [EV_LOSE] = { 0.2f, 0.1f, 90 }, [EV_LAP] = { 0.25f, 0.3f, 120 }, [EV_FINAL] = { 0.4f, 0.5f, 240 }, [EV_WALL] = { 0.6f, 0.3f, 120 }, [EV_CRASH] = { 1, 0.8f, 420 }, [EV_CHARGE] = { 0.2f, 0.5f, 120 }, [EV_TURBO] = { 0.8f, 0.9f, 500 }, [EV_ROCKET] = { 0.7f, 0.9f, 350 } };
      if (!g_ev_on) continue;
      if (rumble[e->kind][2] > 0) { cw_input_rumble(0, rumble[e->kind][0], rumble[e->kind][1], (int)rumble[e->kind][2]); g_fb[e->kind][2]++; }
      if (e->kind == EV_PASS || e->kind == EV_TOP || e->kind == EV_CRASH || e->kind == EV_WALL || e->kind == EV_TURBO || e->kind == EV_ROCKET) {
        float h = cw_water_height_at(G.water, e->x, e->z, G.wt);
        cw_particles_burst(bursts, (t3_vec3){ e->x, h + 0.2f, e->z }, (t3_vec3){ 0, 1, 0 });
        g_fb[e->kind][3]++;
      }
    } }
  waterline_foam();
}
