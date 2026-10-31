/* The race: craft on the water. Everything here runs on the fixed tick with cw_ arithmetic and cw_water_sample_at: the
 * same bits on every host, and the height a craft rides is the height the surface is drawn at.
 *
 * The hull is four probes on the water's surface. A spring and damper hold the craft to it, so a crest throws it up and
 * the surface falling away faster than gravity leaves it in the air: there is no jump code. Faces shove it downhill.
 * Lean turns it; a thrust and a drag set its speed, and buoys raise the speed the drag allows. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/input.h"
#include "cartwheel/platform.h"
#include "course_data.h"
#include "game.h"

#define G_ACC 9.81f
#define SPRING 46.0f     /* per second squared, per metre under */
#define DAMP 9.5f
#define BASE_TOP 21.5f   /* m/s at an empty meter */
#define METER_TOP 1.55f  /* m/s per arrow */
#define THRUST 11.0f

game G;

static cw_value cw_val_vec4(float x, float y, float z, float w) { cw_value v; memset(&v, 0, sizeof v); v.type = CW_T_VEC4; v.v[0] = x, v.v[1] = y, v.v[2] = z, v.v[3] = w; return v; }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float wrap_pi(float a) {
  while (a > 3.14159265f) a -= TAU;
  while (a < -3.14159265f) a += TAU;
  return a;
}

float ground_at(float x, float z) { return G.land ? cw_terrain_height_at(G.land, x, z) : -40.0f; }

void track_at(float s, float *x, float *z, float *dx, float *dz) {
  float f = s / COURSE_LEN;
  f -= floorf(f);
  float q = f * (float)TRACK_N;
  int i = (int)q, j = (i + 1) % TRACK_N;
  float t = q - (float)i;
  *x = TRACK[i][0] + (TRACK[j][0] - TRACK[i][0]) * t;
  *z = TRACK[i][1] + (TRACK[j][1] - TRACK[i][1]) * t;
  if (dx) *dx = TRACK[i][2] + (TRACK[j][2] - TRACK[i][2]) * t;
  if (dz) *dz = TRACK[i][3] + (TRACK[j][3] - TRACK[i][3]) * t;
}

static void track_update(craft *c) {
  int best = c->track_i;
  float bd = 1e12f;
  for (int k = -3; k <= 24; k++) {
    int i = (c->track_i + k + TRACK_N) % TRACK_N;
    float dx = c->x - TRACK[i][0], dz = c->z - TRACK[i][1], d = dx * dx + dz * dz;
    if (d < bd) bd = d, best = i;
  }
  if (best < c->track_i && c->track_i - best > TRACK_N / 2) {
    if (c->lap < LAPS) c->lap++;
    if (c->lap >= LAPS && c->finish <= 0 && G.state == RACE_ON) c->finish = G.clock;
  }
  c->track_i = best;
  c->dist = (float)c->lap * COURSE_LEN + (float)best / (float)TRACK_N * COURSE_LEN;
}

/* easy rides every second buoy, normal three in four, hard every one */
bool buoy_on(int b) { return g_difficulty == 2 || (g_difficulty == 1 ? (b & 3) != 3 : (b & 1) == 0); }
bool racer_on(int i) { return i == 0 || g_mode != MODE_TIME_TRIAL || G.state == RACE_ATTRACT; }
static int buoy_next(int b) {
  do b = (b + 1) % BUOY_N; while (!buoy_on(b));
  return b;
}

static void buoys(craft *c) {
  const float *b = BUOYS[c->next_buoy];
  float s_x, s_z, tx, tz;
  track_at(b[3], &s_x, &s_z, &tx, &tz);
  float rx = c->x - b[0], rz = c->z - b[1];
  float along = rx * tx + rz * tz;
  if (along < 0 || along > 60) return;
  /* (side +1: the buoy stands right of the line: pass with it on your right, on the line's side of it) */
  float lat = (rx * -tz + rz * tx) * -b[2];
  bool ok = lat > -1.0f && lat < 34.0f;
  if (ok) {
    if (c->meter < 5) c->meter++;
    c->passes++;
    c->flash_ok = 1;
    c->ok_tick = c->next_buoy + 1;
  } else {
    c->meter = 0;
    c->misses++;
    c->flash_miss = 1;
    if (c->misses >= 5 && c->human && !c->dq) c->dq = 1;
  }
  c->next_buoy = buoy_next(c->next_buoy);
}

static void computer(craft *c, int idx) {
  const float *b = BUOYS[c->next_buoy];
  float tx, tz, sx, sz;
  track_at(b[3], &sx, &sz, &tx, &tz);
  /* aim for the racing side of the next buoy, then along the line after it */
  float gap = 4.0f + 3.0f * (float)(idx % 3);
  float ax = sx + (b[0] - sx) * (1.0f - gap / 8.5f), az = sz + (b[1] - sz) * (1.0f - gap / 8.5f);
  float rx = ax - c->x, rz = az - c->z, d2 = rx * rx + rz * rz;
  if (d2 < 22.0f * 22.0f) {
    float lx, lz;
    track_at(c->dist + 45.0f, &lx, &lz, NULL, NULL);
    float m = 1.0f - d2 / (22.0f * 22.0f);
    ax += (lx - ax) * m * 0.35f, az += (lz - az) * m * 0.35f;
  }
  float want = cw_atan2(ax - c->x, az - c->z);
  float err = wrap_pi(want - c->yaw);
  c->in.steer = clampf(-err * 2.6f, -1, 1);
  c->in.lean = fabsf(err) > 0.55f ? 1.0f : 0.0f;
  c->in.throttle = 1;
  c->in.trick = false;
}

static void craft_tick(craft *c, int idx, float dt) {
  c->px = c->x, c->py = c->y, c->pz = c->z, c->pyaw = c->yaw, c->ppitch = c->pitch, c->proll = c->roll;
  if (c->spill > 0) {
    /* thrown off: the hull coasts and rights itself while the rider climbs back on. Under three seconds, and the meter only
     * loses two arrows, so a big try is worth making */
    c->spill -= dt;
    c->in.steer = 0, c->in.lean = 0, c->in.throttle = 0, c->in.trick = false;
    c->trick_kind = 0;
    if (c->spill <= 0) { c->roll = 0, c->rr = 0; c->meter = c->meter > 2 ? c->meter - 2 : 0; }
  }
  c->steer += (c->in.steer - c->steer) * clampf(dt * 9, 0, 1);
  c->lean += (c->in.lean - c->lean) * clampf(dt * 6, 0, 1);
  float sy, cy;
  cw_sincos(c->yaw, &sy, &cy);
  float fx = sy, fz = cy, rx = -cy, rz = sy; /* right of the hull: y up, forward +z at yaw 0, so right is -x */
  cw_water_sample s;
  cw_water_sample_at(G.water, c->x, c->z, G.wt, &s);
  float hb = cw_water_height_at(G.water, c->x + fx * 1.15f, c->z + fz * 1.15f, G.wt);
  float hs = cw_water_height_at(G.water, c->x - fx * 1.15f, c->z - fz * 1.15f, G.wt);
  float hp = cw_water_height_at(G.water, c->x - rx * 0.55f, c->z - rz * 0.55f, G.wt);
  float hq = cw_water_height_at(G.water, c->x + rx * 0.55f, c->z + rz * 0.55f, G.wt);
  float hm = (s.height * 2 + hb + hs + hp + hq) / 6;
  c->surf = s.height;
  float under = hm - c->y;
  bool was_air = c->air;
  c->wet = clampf((under + 0.08f) / 0.26f, 0, 1);
  /* on the sand the hull is not in the air: it rests on the ground, and the rider can still drive it off */
  float land_y = ground_at(c->x, c->z);
  bool land = land_y > c->y - 0.32f && land_y > s.height - 0.1f && c->vy < 2.5f;
  c->air = c->wet <= 0 && !land;
  /* the rise of the surface itself, seen from the moving craft */
  float rel_vy = c->vy - s.velocity.y;
  float ay = -G_ACC;
  if (!c->air) ay += (SPRING * clampf(under, -0.1f, 1.1f)) - DAMP * rel_vy * c->wet;
  /* horizontal: thrust along the hull when the pump is under, drag against the water */
  float wvx = s.velocity.x * 0.6f, wvz = s.velocity.z * 0.6f;
  float ux = c->vx - wvx, uz = c->vz - wvz;
  float vf = ux * fx + uz * fz, vl = ux * rx + uz * rz;
  c->top = BASE_TOP + METER_TOP * (float)c->meter + c->boost * 3.5f;
  float top = c->top;
  static const float DIFF_TOP[3] = { 0.93f, 1.0f, 1.05f };
  if (!c->human) top *= DIFF_TOP[g_difficulty] * c->skill * (1.0f + 0.03f * clampf((G.c[0].dist - c->dist) / 120.0f, -1, 1)); /* a small, bounded pull toward the human */
  float drag = THRUST / (top * top);
  float af = 0;
  if (!c->air) {
    af += THRUST * c->in.throttle * (land ? 0.8f : c->wet > 0.2f ? 1.0f : 0.3f);
    af -= drag * vf * fabsf(vf) * (0.35f + 0.65f * c->wet) * (1.0f + 0.5f * c->lean * fabsf(c->steer));
    if (c->in.throttle < 0.1f) af -= 0.8f * vf * 0.25f;
    /* lateral grip: the hull carves */
    /* pulled back the stern digs in and the hull holds its line through the turn; let go at full lock it skates */
    float grip = land ? 4.0f : (2.0f + 2.2f * c->wet + 4.5f * clampf(c->lean, 0, 1)) * c->wet;
    c->slip = vl;
    /* the price of too much stick: a slide scrubs speed, and held long at speed it throws the rider */
    if (fabsf(vl) > 3.5f) af -= (fabsf(vl) - 3.5f) * 1.6f;
    if (fabsf(vl) > 8.5f && !land) { c->slide_t += dt; if (c->slide_t > 0.9f && c->spill <= 0) { c->spill = 1.9f, c->spills++, c->big_slide = true; c->slide_t = 0; } }
    else c->slide_t = 0;
    vl -= vl * clampf(grip * dt, 0, 1);
  } else {
    c->air_time += dt;
    if (c->y > c->air_rise) c->air_rise = c->y;
  }
  /* put back together with the water's motion, the faces' shove included in the air-free case */
  float nvf = vf + af * dt;
  float sx = 0, sz = 0;
  if (!c->air) sx = s.normal.x * G_ACC * 1.15f * c->wet * dt, sz = s.normal.z * G_ACC * 1.15f * c->wet * dt;
  c->vx = wvx + fx * nvf + rx * vl + sx;
  c->vz = wvz + fz * nvf + rz * vl + sz;
  c->vy += ay * dt;
  /* the pump: leaning back up a face and throwing forward at the top adds lift */
  if (!c->air && c->in.lean < -0.5f && c->load > 0.5f && c->lean > -0.3f) {
    c->vy += 2.8f;
    c->load = 0;
  }
  if (c->in.lean > 0.5f && !c->air) c->load = clampf(c->load + dt * 2.5f, 0, 1);
  else if (c->in.lean >= -0.5f) c->load = clampf(c->load - dt * 1.2f, 0, 1);
  /* turning: the hull yaws with the stick; less at speed, more when leaned back */
  float sp = sqrtf(c->vx * c->vx + c->vz * c->vz);
  c->speed = sp;
  float rate = 1.45f - 0.03f * sp + 0.85f * clampf(c->lean, 0, 1);
  /* a hull on sand pivots only as it moves: steering turns its path, it does not spin on the spot */
  if (land) rate = 0.25f + clampf(sp * 0.45f, 0, 1.1f);
  if (c->air) rate *= 0.35f;
  c->yaw = wrap_pi(c->yaw - c->steer * rate * dt);
  /* cutting across a face turns the hull down it, so the line through the waves is a choice */
  if (!c->air && !land) {
    float down_side = s.normal.x * rx + s.normal.z * rz; /* + : the face falls away to the right */
    c->yaw = wrap_pi(c->yaw - down_side * 0.6f * c->wet * dt);
  }
  /* the path's turn radius, for the handling gate */
  {
    float hd = cw_atan2(c->vx, c->vz), dh = fabsf(wrap_pi(hd - c->heading)) / dt;
    c->heading = hd;
    if (dh > 0.02f && sp > 3) { float rr = sp / dh; c->radius += (rr - c->radius) * 0.03f; }
  }
  /* attitude */
  float pitch_t = cw_atan2(hb - hs, 2.3f) + 0.045f + 0.06f * clampf(c->lean, 0, 1) + 0.004f * sp;
  if (land) {
    /* lie on the ground's own slope */
    float gb = ground_at(c->x + fx * 1.15f, c->z + fz * 1.15f), gs = ground_at(c->x - fx * 1.15f, c->z - fz * 1.15f);
    pitch_t = cw_atan2(gb - gs, 2.3f);
  }
  float roll_t = cw_atan2(hp - hq, 1.1f) + c->steer * (0.2f + 0.009f * sp) * (1.0f + 0.25f * clampf(c->lean, 0, 1));
  if (!c->air) {
    c->pr += ((pitch_t - c->pitch) * 70.0f - c->pr * 11.0f) * dt;
    c->rr += ((roll_t - c->roll) * 60.0f - c->rr * 11.0f) * dt;
  } else {
    /* in the air: nose falls slowly, the stick trims */
    /* left alone the hull settles level for its landing; the stick trims it */
    if (!c->trick_kind) c->pr += ((0.1f - c->pitch) * 2.2f - c->pr * 1.8f + c->in.lean * 2.6f) * dt;
    c->rr += (-c->roll * 2.4f + c->steer * 1.5f - c->rr * 1.6f) * dt;
    if (c->trick_kind == 0 && c->in.trick && c->air_time > 0.08f) {
      c->trick_kind = c->in.lean > 0.4f ? 2 : 1;
      c->trick_t = 0;
      c->trick_dir = c->steer < 0 ? -1.0f : 1.0f;
    }
    if (c->trick_kind) {
      c->trick_t += dt;
      float speed = TAU / 0.72f;
      if (c->trick_kind == 1) c->roll += c->trick_dir * speed * dt;
      else c->pitch += speed * dt;
      if (c->trick_t >= 0.72f) {
        if (c->trick_kind == 1) c->roll = wrap_pi(c->roll);
        else c->pitch = wrap_pi(c->pitch);
        c->trick_kind = 0;
        c->tricks++;
        if (c->meter < 5) c->meter++;
        c->flash_ok = 1;
      }
    }
  }
  c->pitch += c->pr * dt;
  c->roll += c->rr * dt;
  c->x += c->vx * dt, c->y += c->vy * dt, c->z += c->vz * dt;
  /* landing */
  c->big_splash = false;
  if (was_air && !c->air) {
    float err = fabsf(wrap_pi(c->pitch - pitch_t)) + fabsf(wrap_pi(c->roll - roll_t));
    float hit = -rel_vy;
    if (c->air_time > 0.2f) {
      c->big_splash = true;
      c->kick = clampf(hit * 0.16f, 0, 1);
      /* flat keeps the speed, nose down plunges and loses it; weight back on the way in saves some of it. A trick not finished
       * or a landing far out of line throws the rider. A clean long one pays into the meter */
      float nose = wrap_pi(pitch_t - c->pitch); /* + : nose below the water's line */
      float e2 = err + (nose > 0 ? nose * 0.9f : 0) - 0.35f * clampf(c->in.lean, 0, 1) * (nose > 0 ? 1.0f : 0.0f);
      e2 = e2 < 0 ? 0 : e2;
      float keep = e2 < 0.12f ? 1.0f : 1.0f - clampf((e2 - 0.12f) * 0.42f, 0, 0.65f);
      if (nose > 0.3f) c->vy -= nose * 3.0f, c->y -= nose * 0.25f;
      if (c->trick_kind || e2 > 2.1f) keep = 0.35f, c->trick_kind = 0, c->spill = 1.9f, c->spills++;
      c->vx *= keep, c->vz *= keep;
      c->land_keep = keep;
      if (e2 < 0.25f && c->air_time > 0.6f) { c->boost = 1; if (c->meter < 5) c->meter++; c->flash_ok = 1; }
      c->pitch = wrap_pi(c->pitch), c->roll = wrap_pi(c->roll);
      c->pr *= 0.25f, c->rr *= 0.25f;
    }
    c->air_time = 0;
    c->air_rise = c->y;
  }
  /* a rival's wake: crossing the fresh water behind another craft bumps and shoves this one */
  c->wake_hit = 0;
  if (!c->air) {
    for (int j = 0; j < RACERS; j++) {
      const craft *o = &G.c[j];
      if (o == c || o->air || o->speed < 8) continue;
      float osy, ocy;
      cw_sincos(o->yaw, &osy, &ocy);
      float dx = c->x - o->x, dz = c->z - o->z, back = -(dx * osy + dz * ocy);
      if (back < 3 || back > 24) continue;
      float lat = dx * ocy - dz * osy, width = 1.6f + back * 0.16f, off = fabsf(fabsf(lat) - width);
      if (off > 0.9f) continue;
      float k = (1.0f - off / 0.9f) * (1.0f - back / 24.0f) * (o->speed / 28.0f);
      float sgn = lat > 0 ? 1.0f : -1.0f;
      /* outward along the rival's left-right, a lift as the hull rides over */
      c->vx += ocy * sgn * k * 5.0f * dt;
      c->vz += -osy * sgn * k * 5.0f * dt;
      c->vy += k * 7.0f * dt;
      c->wake_hit = 1;
    }
  }
  /* the ground: sand drags, a rising bed pushes out */
  float gy = ground_at(c->x, c->z);
  c->bump = false;
  c->grounded = 0;
  if (gy > c->y - 0.22f) {
    t3_vec3 n = cw_terrain_normal_at(G.land, c->x, c->z);
    float over = gy - (c->y - 0.22f);
    c->y += clampf(over, 0, 0.5f) * 0.7f;
    if (land && c->wet <= 0) c->y = gy + 0.22f;
    if (c->vy < 0) c->vy *= 0.2f;
    c->grounded = clampf(over * 3, 0, 1);
    float k = clampf(1.0f - dt * (1.2f + 1.6f * c->grounded), 0, 1);
    c->vx *= k, c->vz *= k;
    float hspeed = c->vx * n.x + c->vz * n.z;
    if (over > 0.35f) {
      if (hspeed < 0) c->vx -= n.x * hspeed * 1.4f, c->vz -= n.z * hspeed * 1.4f;
      c->vx += n.x * 22.0f * dt, c->vz += n.z * 22.0f * dt;
      if (sp > 6) c->bump = true;
    }
  }
  /* beached: the sand's slope and a helping shove take the craft back toward the water beside the course, gently without
   * throttle, firmly with it */
  if (land) {
    c->beached++;
    float tx = TRACK[c->track_i][0] - c->x, tz = TRACK[c->track_i][1] - c->z, tl = sqrtf(tx * tx + tz * tz);
    if (tl > 1) {
      float shove = (c->beached > 40 ? 2.2f : 0.8f) + (c->beached > 40 ? 3.0f * c->in.throttle : 0);
      c->vx += tx / tl * shove * dt, c->vz += tz / tl * shove * dt;
    }
  } else if (c->beached > 0) {
    if (c->beached > 30 && sp > 2) c->escapes++;
    c->beached = 0;
  }
  /* never lost in the world: a soft wall past the course's sea */
  float r2 = c->x * c->x + c->z * c->z;
  if (r2 > 860.0f * 860.0f) {
    float r = sqrtf(r2);
    c->vx -= c->x / r * 8.0f * dt, c->vz -= c->z / r * 8.0f * dt;
  }
  c->boost = clampf(c->boost - dt * 0.6f, 0, 1);
  c->flash_ok = clampf(c->flash_ok - dt * 1.4f, 0, 1);
  c->flash_miss = clampf(c->flash_miss - dt * 1.1f, 0, 1);
  c->kick = clampf(c->kick - dt * 3.0f, 0, 1);
  (void)idx;
}

void sim_reset(int state) {
  uint32_t tick = G.tick;
  int level = G.level;
  double wt_extra = G.wt_extra;
  float keep_cam[3] = { G.cam[0], G.cam[1], G.cam[2] };
  cw_node water = G.water, camera = G.camera, sun = G.sun, sky = G.sky, clouds = G.clouds, sun_node = G.sun_node;
  cw_terrain *land = G.land;
  memset(&G, 0, sizeof G);
  G.tick = tick, G.water = water, G.camera = camera, G.sun = sun, G.sky = sky, G.clouds = clouds, G.sun_node = sun_node, G.land = land;
  G.state = state;
  G.level = level;
  G.wt_extra = wt_extra;
  memcpy(G.cam, keep_cam, sizeof keep_cam);
  G.clock = state == RACE_GRID ? -3.6f : 0;
  float yaw = cw_atan2(START_DX, START_DZ);
  for (int i = 0; i < RACERS; i++) {
    craft *c = &G.c[i];
    int row = i / 2, col = i % 2;
    float back = 6.0f + 9.0f * (float)row + (col ? 3.0f : 0), side = col ? 4.5f : -4.5f;
    c->x = START_X - START_DX * back + START_DZ * side;
    c->z = START_Z - START_DZ * back - START_DX * side;
    c->y = 0.1f;
    c->yaw = yaw;
    c->px = c->x, c->pz = c->z, c->py = c->y, c->pyaw = c->yaw;
    c->human = i == 0;
    static const float skills[RACERS] = { 1.0f, 0.955f, 0.93f, 0.97f, 0.91f, 0.94f };
    c->skill = skills[i];
    c->track_i = TRACK_N - 3;
    c->dist = -back;
    c->lap = -1; /* behind the line: its first crossing begins lap one */
    c->place = i + 1;
    G.order[i] = i;
    /* a time trial: the others stay home, out of the world */
    if (!racer_on(i)) { c->x = c->px = 3000.0f + 40.0f * (float)i, c->z = c->pz = 3000.0f; c->y = c->py = -60.0f; c->dist = -1e6f; }
  }
  if (state == RACE_ATTRACT && cw_node_valid(G.water)) sim_place(COURSE_LEN * 0.2f);
}

/* the sea builds lap by lap (the waves are properties: the surface and every height query read them at once) */
static void weather(void) {
  float lead = 0;
  for (int i = 0; i < RACERS; i++) if (racer_on(i) && G.c[i].dist > lead) lead = G.c[i].dist;
  float t = G.state == RACE_ATTRACT ? 0.75f : clampf(lead / (COURSE_LEN * (float)LAPS), 0, 1);
  float target = 0.62f + 0.38f * t;
  G.swell += (target - G.swell) * 0.004f;
  if (G.swell < 0.62f) G.swell = target;
#ifdef JETSKI_AUTOPILOT
  { const char *e = getenv("JETSKI_SWELL"); if (e) G.swell = (float)atof(e); }
#endif
  /* difficulty: the sea itself is bigger */
  static const float DIFF_SEA[3] = { 0.8f, 1.0f, 1.18f };
  float a = G.swell * DIFF_SEA[g_difficulty];
  cw_node_set(G.water, "wave0", cw_val_vec4(200, 46, 1.5f * a, 0.7f));
  cw_node_set(G.water, "wave1", cw_val_vec4(216, 37, 1.05f * a, 0.7f));
  cw_node_set(G.water, "wave2", cw_val_vec4(150, 14, 0.14f + 0.22f * a, 0.65f));
  cw_node_set(G.water, "wave3", cw_val_vec4(252, 7, 0.05f + 0.05f * a, 0.5f));
  /* the tide comes in over the race */
  G.tide = 0.5f * t;
  cw_node3d_set_position(G.water, (t3_vec3){ 0, G.tide, 0 });
}

/* every racer to a place on the course, at speed */
void sim_place(float s) {
  for (int i = 0; i < RACERS; i++) {
    craft *c = &G.c[i];
    float x, z, dx, dz;
    track_at(s - (float)(i * 11), &x, &z, &dx, &dz);
    c->x = c->px = x + dz * (i % 2 ? 3.0f : -3.0f);
    c->z = c->pz = z - dx * (i % 2 ? 3.0f : -3.0f);
    c->y = c->py = cw_water_height_at(G.water, c->x, c->z, cw_water_time(G.water));
    c->yaw = c->pyaw = cw_atan2(dx, dz);
    c->vx = dx * 22, c->vz = dz * 22, c->vy = 0;
    c->track_i = (int)(s / COURSE_LEN * (float)TRACK_N) % TRACK_N;
    if (c->track_i < 0) c->track_i += TRACK_N;
    for (int b = 0; b < BUOY_N; b++) if (buoy_on(b) && BUOYS[b][3] > s - (float)(i * 11)) { c->next_buoy = b; break; }
    c->lap = 0;
  }
  G.cam[0] = 0, G.look[0] = 0;
}

/* the course's area: far from the line for five seconds and the race is over, with a warning all the way */
static void out_of_bounds(craft *c, float dt) {
  float tx = c->x - TRACK[c->track_i][0], tz = c->z - TRACK[c->track_i][1];
  if (tx * tx + tz * tz > 110.0f * 110.0f) { c->out_t += dt; if (c->out_t > 5.0f && c->human && !c->dq) c->dq = 2; }
  else c->out_t = 0;
}

void sim_tick(float dt) {
  G.tick++;
  G.wt = cw_water_time(G.water) + G.wt_extra;
  weather();
  G.clock += dt;
  bool go = G.state != RACE_GRID || G.clock >= 0;
  if (G.state == RACE_GRID && G.clock >= 0) G.state = RACE_ON;
  for (int i = 0; i < RACERS; i++) {
    craft *c = &G.c[i];
    if (!racer_on(i)) continue;
    bool ai = !c->human || G.state == RACE_ATTRACT;
#ifdef JETSKI_AUTOPILOT
    ai = true;
#endif
    if (c->finish > 0) ai = true;
    if (ai) computer(c, i);
    if (!go) c->in.throttle = 0;
    craft_tick(c, i, dt);
    c->ok_tick = 0;
    if (go && G.state != RACE_DONE) {
      track_update(c);
      buoys(c);
      if (G.state == RACE_ON) out_of_bounds(c, dt);
    }
  }
  /* craft shoulder each other aside, they do not pass through */
  for (int i = 0; i < RACERS; i++)
    for (int j = i + 1; j < RACERS; j++) {
      craft *a = &G.c[i], *b = &G.c[j];
      float dx = b->x - a->x, dz = b->z - a->z, d2 = dx * dx + dz * dz;
      if (d2 > 2.2f * 2.2f || d2 < 1e-4f) continue;
      float d = sqrtf(d2), push = (2.2f - d) * 0.5f, nx = dx / d, nz = dz / d;
      a->x -= nx * push, a->z -= nz * push, b->x += nx * push, b->z += nz * push;
      float rel = (b->vx - a->vx) * nx + (b->vz - a->vz) * nz;
      if (rel < 0) a->vx += nx * rel * 0.5f, a->vz += nz * rel * 0.5f, b->vx -= nx * rel * 0.5f, b->vz -= nz * rel * 0.5f;
      a->kick = clampf(a->kick + 0.2f, 0, 1), b->kick = clampf(b->kick + 0.2f, 0, 1);
    }
  /* places */
  for (int i = 0; i < RACERS; i++) G.order[i] = i;
  for (int a = 1; a < RACERS; a++) {
    int v = G.order[a], b = a;
    float dv = G.c[v].finish > 0 ? 1e7f - G.c[v].finish : G.c[v].dist;
    while (b > 0) {
      int u = G.order[b - 1];
      float du = G.c[u].finish > 0 ? 1e7f - G.c[u].finish : G.c[u].dist;
      if (du >= dv) break;
      G.order[b] = u;
      b--;
    }
    G.order[b] = v;
  }
  for (int i = 0; i < RACERS; i++) G.c[G.order[i]].place = i + 1;
  if (G.state == RACE_ON && (G.c[0].finish > 0 || G.c[0].dq)) G.state = RACE_DONE;
  /* the attract loop races on and on, and stays out where the swell is */
  if (G.state == RACE_ATTRACT && G.c[G.order[0]].lap >= 1) for (int i = 0; i < RACERS; i++) G.c[i].lap = -1;
  uint32_t h = G.tick;
  for (int i = 0; i < RACERS; i++) {
    h = cw_hash32(&G.c[i].x, sizeof(float) * 6, h);
    h = cw_hash32(&G.c[i].yaw, sizeof(float) * 3, h);
  }
  G.sim_hash = h;
}
