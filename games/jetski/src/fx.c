/* The water thrown about: the rooster tail behind each craft, spray off a carve, a mist that hangs in the low sun, and
 * bursts where something lands. Wakes and foam go into the water's ripple layer. */
#include <math.h>

#include "cartwheel/input.h"
#include "cartwheel/particles.h"
#include "game.h"

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
    int amount = i == 0 ? 1800 : 300;
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
    sheet[s] = emitter(parent, s ? "SheetR" : "SheetL", 200, 0.5f, 0.26f, "0 1 1 1 0, 0.12 1 1 1 0.3, 0.6 1 1 1 0.14, 1 1 1 1 0", "0 0.3, 0.4 1, 1 1.5", 600 + s, false);
    cw_node_set(sheet[s], "texture", cw_val_string("effects/mist.png"));
    cw_node_set(sheet[s], "camera_fade", cw_val_float(2.2f));
    cw_node_set(sheet[s], "soft_distance", cw_val_float(0.3f));
    cw_node_set(sheet[s], "energy", cw_val_float(1.15f));
    cw_node_set(sheet[s], "spread", cw_val_float(14));
    sheet_fine[s] = emitter(parent, s ? "SheetFineR" : "SheetFineL", 500, 0.75f, 0.011f, "0 1 1 1 0.9, 0.6 1 1 1 0.6, 1 1 1 1 0", NULL, 620 + s, true);
    cw_node_set(sheet_fine[s], "energy", cw_val_float(1.6f));
    cw_node_set(sheet_fine[s], "spread", cw_val_float(18));
    cw_node_set(sheet_fine[s], "camera_fade", cw_val_float(3));
  }
  side_spray = emitter(parent, "Carve", 1000, 0.7f, 0.012f, "0 1 1 1 0.9, 0.6 0.95 0.97 1 0.6, 1 0.9 0.95 1 0", NULL, 300, true);
  cw_node_set(side_spray, "energy", cw_val_float(1.6f));
  cw_node_set(side_spray, "spread", cw_val_float(24));
  cw_node_set(side_spray, "camera_fade", cw_val_float(5));
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

void fx_tick(float dt) {
  (void)dt;
  for (int i = 0; i < RACERS; i++) {
    craft *c = &G.c[i];
    float sy, cy;
    cw_sincos(c->yaw, &sy, &cy);
    float v = c->speed;
    float k = v / 28.0f;
    bool wet = !c->air && c->wet > 0.15f;
    bool on = wet && v > 3 && c->in.throttle > 0.1f;
    /* the jet's rooster tail: up and back, higher with speed */
    t3_vec3 sp = { c->x - sy * 1.65f, c->surf + 0.1f, c->z - cy * 1.65f };
    cw_node3d_set_position(tail[i], sp);
    cw_node_set(tail[i], "emitting", cw_val_bool(on));
    cw_node_set(tail[i], "direction", cw_val_vec3(-sy * 0.22f + c->vx * 0.022f, 0.9f, -cy * 0.22f + c->vz * 0.022f));
    cw_node_set(tail[i], "speed_min", cw_val_float(3.0f + 3.5f * k));
    cw_node_set(tail[i], "speed_max", cw_val_float(5 + 7.5f * k));
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
          cw_water_splash(G.water, c->x - sy * back + cy * lat, c->z - cy * back - sy * lat, 0.42f, 0.05f * k, 0.28f + 0.3f * k);
        }
      }
      float rx = cy, rz = -sy;
      cw_water_wake(G.water, c->x - sy * 1.3f, c->z - cy * 1.3f, c->vx, c->vz, 0.42f, 0.045f + 0.03f * k, 0.3f + 0.25f * k);
      cw_water_wake(G.water, c->x + rx * 0.6f + sy * 0.3f, c->z + rz * 0.6f + cy * 0.3f, c->vx, c->vz, 0.28f, 0.03f * k, 0.4f * k);
      cw_water_wake(G.water, c->x - rx * 0.6f + sy * 0.3f, c->z - rz * 0.6f + cy * 0.3f, c->vx, c->vz, 0.28f, 0.03f * k, 0.4f * k);
    }
    if (c->big_splash) {
      t3_vec3 at = { c->x, c->surf + 0.1f, c->z };
      cw_particles_burst(bursts, at, (t3_vec3){ 0, 1, 0 });
      cw_particles_burst(burst_mist, at, (t3_vec3){ 0, 1, 0 });
      cw_water_splash(G.water, c->x, c->z, 2.2f, 0.25f + 0.3f * c->kick, 1.0f);
      if (i == 0) cw_input_rumble(0, 0.35f + 0.6f * c->kick, 0.2f + 0.4f * c->kick, 180 + (int)(240 * c->kick));
    }
    if (c->bump && i == 0) cw_input_rumble(0, 0.8f, 0.5f, 120);
  }
}
