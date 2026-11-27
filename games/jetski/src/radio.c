/* The race radio: the crew chief of the rider you picked speaks only on news (the big waves coming, the last lap, a rival
 * closing on you, taking the lead, the day's short way open), and the other riders call out a pass, theirs or yours.
 * Lines come from story/radio.story (compiled to radio.cwstory) and are voiced ahead of time (radio.cwspeech, Kokoro-82M),
 * each also shown as a subtitle, so turning the radio off (Audio: Race radio 0) keeps the words. It never nags: each
 * pool has its own cooldown, no line follows itself, and the radio as a whole waits RADIO_GAP seconds between lines.
 * sw radio_gap 0: no gap of its own (the radio gate's control). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cartwheel/speech.h"
#include "cartwheel/story.h"
#include "game.h"

#define RADIO_GAP 8.0f
static cw_story *st;
static cw_speech *sp;
static unsigned ev_read;
static float since = 1e9f, closing_t, race_t;
static bool route_said;
static uint32_t last_key;
int g_radio_vol = 8;
float g_radio_gap = RADIO_GAP;
float g_radio[5] = { 0, 1e9f, 0, 0, 0 }; /* lines said, the least gap between two (s), the same line twice running, lines shown, rival calls */
static const char *const WHO[RACERS] = { "cruz", "marlin", "kestrel", "tide", "juno", "reef" };

void radio_init(void) {
  sp = cw_speech_load("radio.cwspeech");
  st = cw_story_new(&(cw_story_desc){ .path = "radio.cwstory", .speech = sp });
  if (!st) cw_log(CW_LOG_WARN, "jetski: no race radio (radio.cwstory)");
}
void radio_volume(float v) { cw_bus_set_volume(cw_speech_bus(), v); }
float radio_duck(void) { return sp ? cw_speech_duck(sp) : 1.0f; }
void radio_race_reset(void) { ev_read = g_ev_n, since = 1e9f, closing_t = 0, race_t = 0, route_said = false; }

static bool say(const char *pool) {
  if (!st || since < g_radio_gap || !cw_story_pool_ready(st, pool)) return false;
  const cw_story_line *l = cw_story_bark(st, pool);
  if (!l) return false;
  uint32_t key = l->key;
  if (g_radio[0] > 0) g_radio[1] = fminf(g_radio[1], since);
  if (key == last_key) g_radio[2] += 1;
  last_key = key, since = 0, g_radio[0] += 1;
  if (cw_story_aside(st)) g_radio[3] += 1;
  return true;
}
static void crew(const char *what) { char p[48]; snprintf(p, sizeof p, "%s_crew.%s", WHO[rider_of(0)], what); say(p); }
static void rival(int seat, const char *what) { if (seat <= 0 || seat >= RACERS) return; char p[48]; snprintf(p, sizeof p, "%s.%s", WHO[rider_of(seat)], what); if (say(p)) g_radio[4] += 1; }

/* the rider just behind you (+1) or just ahead (-1) in the race's order; at either end, the one on the other side */
static int neighbour(int dir) {
  int k = 0;
  for (int i = 0; i < RACERS; i++) if (G.order[i] == 0) k = i;
  int j = k + dir;
  if (j < 0 || j >= RACERS) j = k - dir;
  return j >= 0 && j < RACERS ? G.order[j] : -1;
}
void radio_tick(float dt) {
  if (!st) return;
  cw_story_tick(st, dt);
  if (sp) cw_speech_tick(sp, dt);
  since += dt;
  if (G.state != RACE_ON || MODE_ALONE || PLAYERS == 2 || G.c[0].finish > 0) { /* (two players: no radio, it speaks to one) */ ev_read = g_ev_n; return; }
  race_t += dt;
  const craft *h = &G.c[0];
  if (g_ev_n - ev_read > EV_RING) ev_read = g_ev_n - EV_RING;
  for (; ev_read != g_ev_n; ev_read++) {
    const game_event *e = &g_ev[ev_read % EV_RING];
    switch (e->kind) {
      case EV_WAVES: crew("waves"); break;
      case EV_FINAL: crew("lastlap"); break;
      /* a held place changed: up means you passed the rider now behind you, down that the rider now ahead passed you */
      case EV_GAIN: if (e->a == 1) crew("lead"); else rival(neighbour(1), "passed"); break;
      case EV_LOSE: rival(neighbour(-1), "passes"); break;
      default: break;
    }
  }
  /* a rival within 12 m behind for two seconds, while you lead or chase: the crew warns once */
  bool close = false;
  for (int i = 1; i < RACERS; i++) if (racer_on(i)) { float d = h->dist - G.c[i].dist; if (d > 0 && d < 12) close = true; }
  closing_t = close ? closing_t + dt : 0;
  if (closing_t > 2.0f) { crew("closing"); closing_t = -20; }
  /* the day's short way open: said once, early in the race */
  if (!route_said && race_t > 4 && wx_route_open()) { route_said = true; crew("route"); }
}
/* the line being said, as a subtitle (the story draws it: the speaker's name in their colour) */
void radio_draw(cw_font f) { if (st) cw_story_draw(st, f, (cw_rect){ 0 }); }
