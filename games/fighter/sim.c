#include "sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const FT_MOVE_ID[FT_NMOVES] = { "5L", "5M", "5H", "2L", "2M", "2H", "JL", "JM", "JH", "THROW", "SP1", "SP2", "SP3", "SUPER" };
const char *const FT_STATE_NAME[S_COUNT] = { "intro", "idle", "walk_f", "walk_b", "crouch", "prejump", "air", "attack", "block", "hit", "air_hit",
                                             "down", "getup", "thrown", "dash_f", "dash_b", "ko", "win", "land" };

/* ── data file ───────────────────────────────────────────────────────── */

static char errbuf[160];
static int slot_of(const char *s) {
  for (int i = 0; i < FT_NMOVES; i++)
    if (!strcmp(s, FT_MOVE_ID[i])) return i;
  return -1;
}
static bool parse_box(const char *s, ft_box *b) {
  int a[4];
  if (sscanf(s, "%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3]) != 4) return false;
  *b = (ft_box){ (int16_t)a[0], (int16_t)a[1], (int16_t)a[2], (int16_t)a[3] };
  return true;
}
static bool set_move_key(ft_move *m, const char *k, const char *v) {
  int n = atoi(v);
#define U8(name, field) if (!strcmp(k, name)) { m->field = (uint8_t)n; return true; }
#define I16(name, field) if (!strcmp(k, name)) { m->field = (int16_t)n; return true; }
  U8("su", su) U8("ac", ac) U8("rc", rc) U8("hs", hs) U8("bs", bs) U8("hstop", hstop) U8("kd", kd) U8("hits", hits) U8("rehit", rehit)
  U8("inv", inv) U8("mvfrom", mvfrom) U8("mvto", mvto)
  I16("dmg", dmg) I16("pushh", pushh) I16("pushb", pushb) I16("launch", launch) I16("mv", mv) I16("mg", mg)
  if (!strcmp(k, "lv")) { m->lv = v[0] == 'h' ? LV_HIGH : v[0] == 'l' ? LV_LOW : LV_MID; return true; }
  if (!strcmp(k, "hit")) return parse_box(v, &m->hit);
  if (!strcmp(k, "ext")) return parse_box(v, &m->ext);
  if (!strcmp(k, "proj")) {
    int a, b, c;
    if (sscanf(v, "%d,%d,%d", &a, &b, &c) != 3) return false;
    m->pspeed = (int16_t)a; m->plife = (int16_t)b; m->psize = (int16_t)c;
    return true;
  }
  if (!strcmp(k, "flags")) {
    uint8_t f = 0;
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s", v);
    for (char *t = strtok(tmp, ","); t; t = strtok(NULL, ",")) {
      if (!strcmp(t, "air")) f |= FM_AIR;
      else if (!strcmp(t, "crouch")) f |= FM_CROUCH;
      else if (!strcmp(t, "throw")) f |= FM_THROW;
      else if (!strcmp(t, "special")) f |= FM_SPECIAL;
      else if (!strcmp(t, "super")) f |= FM_SUPER;
      else if (!strcmp(t, "proj")) f |= FM_PROJ;
      else return false;
    }
    m->flags = f;
    return true;
  }
  return false;
}
static bool set_def_key(ft_def *d, const char *k, const char *v) {
  int n = atoi(v);
  if (!strcmp(k, "name")) { snprintf(d->name, sizeof d->name, "%s", v); return true; }
  if (!strcmp(k, "hp")) { d->hp = (int16_t)n; return true; }
  if (!strcmp(k, "walk")) { d->walk = (int16_t)n; return true; }
  if (!strcmp(k, "back")) { d->back = (int16_t)n; return true; }
  if (!strcmp(k, "jump")) { d->jump_v = (int16_t)n; return true; }
  if (!strcmp(k, "jumpfx")) { d->jump_fx = (int16_t)n; return true; }
  if (!strcmp(k, "grav")) { d->grav = (int16_t)n; return true; }
  if (!strcmp(k, "dash")) { d->dash = (int16_t)n; return true; }
  if (!strcmp(k, "dashback")) { d->dash_back = (int16_t)n; return true; }
  if (!strcmp(k, "sp1")) { snprintf(d->motion[0], 12, "%s", v); return true; }
  if (!strcmp(k, "sp2")) { snprintf(d->motion[1], 12, "%s", v); return true; }
  if (!strcmp(k, "sp3")) { snprintf(d->motion[2], 12, "%s", v); return true; }
  if (!strcmp(k, "super")) { snprintf(d->motion[3], 12, "%s", v); return true; }
  return false;
}

int ft_roster_load(ft_roster *r, const char *text, const char **err) {
  memset(r, 0, sizeof *r);
  ft_def tpl;
  memset(&tpl, 0, sizeof tpl);
  tpl.hp = FT_HP; tpl.w_half = 320; tpl.h_stand = 1750; tpl.h_crouch = 1050;
  for (int i = 0; i < FT_NMOVES; i++) tpl.mv[i].hits = 1;
  char line[512];
  int ln = 0;
  *err = NULL;
#define FAIL(...) do { snprintf(errbuf, sizeof errbuf, __VA_ARGS__); *err = errbuf; return -1; } while (0)
  while (*text) {
    size_t n = strcspn(text, "\n");
    snprintf(line, sizeof line, "%.*s", (int)(n < sizeof line - 1 ? n : sizeof line - 1), text);
    text += n + (text[n] ? 1 : 0);
    ln++;
    char *p = line;
    while (*p == ' ') p++;
    if (!*p || *p == '#' || *p == '\r') continue;
    char *tok[24];
    int nt = 0;
    for (char *t = strtok(p, " \t\r"); t && nt < 24; t = strtok(NULL, " \t\r")) tok[nt++] = t;
    if (nt < 2) FAIL("moves line %d: too short", ln);
    if (!strcmp(tok[0], "fighter")) {
      if (r->n >= FT_MAX_ROSTER) FAIL("moves line %d: more than %d fighters", ln, FT_MAX_ROSTER);
      ft_def *d = &r->d[r->n++];
      *d = tpl;
      snprintf(d->id, sizeof d->id, "%s", tok[1]);
      snprintf(d->name, sizeof d->name, "%s", tok[1]);
      for (int i = 2; i < nt; i++) {
        char *eq = strchr(tok[i], '=');
        if (!eq) FAIL("moves line %d: '%s' is not key=value", ln, tok[i]);
        *eq = 0;
        if (!set_def_key(d, tok[i], eq + 1)) FAIL("moves line %d: unknown fighter key '%s'", ln, tok[i]);
      }
    } else if (!strcmp(tok[0], "move")) {
      if (nt < 3) FAIL("moves line %d: move needs a fighter and a slot", ln);
      int s = slot_of(tok[2]);
      if (s < 0) FAIL("moves line %d: unknown move slot '%s'", ln, tok[2]);
      ft_def *d = NULL;
      if (strcmp(tok[1], "*")) {
        for (int i = 0; i < r->n; i++) if (!strcmp(r->d[i].id, tok[1])) d = &r->d[i];
        if (!d) FAIL("moves line %d: fighter '%s' is not declared yet", ln, tok[1]);
      }
      ft_move *m = d ? &d->mv[s] : &tpl.mv[s];
      for (int i = 3; i < nt; i++) {
        char *eq = strchr(tok[i], '=');
        if (!eq) FAIL("moves line %d: '%s' is not key=value", ln, tok[i]);
        *eq = 0;
        if (!set_move_key(m, tok[i], eq + 1)) FAIL("moves line %d: bad move key/value '%s'", ln, tok[i]);
      }
    } else FAIL("moves line %d: unknown line '%s'", ln, tok[0]);
  }
  for (int i = 0; i < r->n; i++) {
    ft_def *d = &r->d[i];
    for (int k = 0; k < 4; k++) {
      if (!cw_motion_parse(d->motion[k], &d->mo[k])) FAIL("fighter %s: bad motion '%s'", d->id, d->motion[k]);
      if (d->mo[k].charge) d->mo[k].charge_frames = 30;
    }
    for (int s = 0; s < FT_NMOVES; s++) {
      ft_move *m = &d->mv[s];
      if (!m->su || (!m->ac) ) FAIL("fighter %s move %s: needs su and ac", d->id, FT_MOVE_ID[s]);
      if (!m->hits) m->hits = 1;
      if (m->mv && !m->mvto) FAIL("fighter %s move %s: mv without mvto", d->id, FT_MOVE_ID[s]);
    }
  }
  if (!r->n) FAIL("moves: no fighters");
  return r->n;
}

int ft_advantage(const ft_move *m, bool blocked) {
  int stun = blocked ? m->bs : m->hs;
  return stun - (m->ac - 1 + m->rc);
}

/* ── helpers ─────────────────────────────────────────────────────────── */

static const ft_def *DEF(const ft_game *g, int i) { return &g->roster->d[g->f[i].def]; }
const ft_move *ft_cur_move(const ft_game *g, int who) {
  const ft_fighter *f = &g->f[who];
  return f->state == S_ATTACK ? &DEF(g, who)->mv[f->mv] : NULL;
}
bool ft_airborne(const ft_fighter *f) { return f->state == S_AIR || f->state == S_AIRHIT || (f->state == S_ATTACK && f->y > 0); }
bool ft_actionable(const ft_fighter *f) {
  return f->state == S_IDLE || f->state == S_WALKF || f->state == S_WALKB || f->state == S_CROUCH;
}
static bool grounded_free(const ft_fighter *f) { return ft_actionable(f) || f->state == S_BLOCK; }

static void emit(ft_game *g, int type, int who, int power, int x, int y) {
  ft_event *e = &g->ev[g->ev_seq & 63];
  *e = (ft_event){ (uint8_t)type, (uint8_t)who, (uint8_t)power, 0, x, y, g->tick };
  g->ev_seq++;
}
static ft_box world_box(const ft_fighter *f, ft_box b) {
  ft_box o;
  if (f->facing > 0) o = (ft_box){ (int16_t)(f->x + b.x0), (int16_t)(f->y + b.y0), (int16_t)(f->x + b.x1), (int16_t)(f->y + b.y1) };
  else o = (ft_box){ (int16_t)(f->x - b.x1), (int16_t)(f->y + b.y0), (int16_t)(f->x - b.x0), (int16_t)(f->y + b.y1) };
  return o;
}
/* boxes are int16 in world space: x within +-4400 plus reach fits */
static bool overlap(ft_box a, ft_box b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
static int iabs(int v) { return v < 0 ? -v : v; }

int ft_hurtboxes(const ft_game *g, int who, ft_box out[2]) {
  const ft_fighter *f = &g->f[who];
  const ft_def *d = DEF(g, who);
  int h = d->h_stand, w = d->w_half;
  bool crouch = f->state == S_CROUCH || (f->state == S_ATTACK && (d->mv[f->mv].flags & FM_CROUCH)) || (f->state == S_BLOCK && f->crouch_block);
  if (crouch) h = d->h_crouch;
  if (f->state == S_KD) return 0;
  ft_box body = { (int16_t)(f->x - w), (int16_t)f->y, (int16_t)(f->x + w), (int16_t)(f->y + h) };
  if (f->state == S_AIRHIT) body = (ft_box){ (int16_t)(f->x - 400), (int16_t)(f->y + 200), (int16_t)(f->x + 400), (int16_t)(f->y + 1500) };
  out[0] = body;
  int n = 1;
  if (f->state == S_ATTACK) {
    const ft_move *m = &d->mv[f->mv];
    if (f->t > m->su && f->t <= m->su + m->ac && m->ext.x1 > m->ext.x0) out[n++] = world_box(f, m->ext);
  }
  return n;
}
bool ft_hitbox_active(const ft_game *g, int who, ft_box *out) {
  const ft_fighter *f = &g->f[who];
  if (f->state != S_ATTACK) return false;
  const ft_move *m = &DEF(g, who)->mv[f->mv];
  if (f->t <= m->su || f->t > m->su + m->ac || m->hit.x1 <= m->hit.x0) return false;
  *out = world_box(f, m->hit);
  return true;
}

static int dir_of(const ft_fighter *f) { return cw_cmd_dir_now(&f->cb); }

static void set_state(ft_fighter *f, int s) { f->state = (uint8_t)s; f->t = 0; }

/* ── starting things ─────────────────────────────────────────────────── */

void ft_round_start(ft_game *g) {
  for (int i = 0; i < 2; i++) {
    ft_fighter *f = &g->f[i];
    const ft_def *d = DEF(g, i);
    uint8_t wins = f->wins;
    uint8_t def = f->def;
    int16_t meter = f->meter;
    memset(f, 0, sizeof *f);
    f->def = def;
    f->wins = wins;
    f->meter = g->training ? FT_METER_MAX : meter;
    f->hp = d->hp;
    f->x = i == 0 ? -1500 : 1500;
    f->facing = i == 0 ? 1 : -1;
    f->state = S_INTRO;
    f->tech_age = 200;
    cw_cmd_clear(&f->cb);
  }
  memset(g->proj, 0, sizeof g->proj);
  g->phase = g->training ? P_FIGHT : P_INTRO;
  g->phase_t = 0;
  g->timer = FT_ROUND_TIME * 60;
  g->hitstop = 0;
  g->round_winner = -1;
  if (g->training) for (int i = 0; i < 2; i++) g->f[i].state = S_IDLE;
}

void ft_game_start(ft_game *g, const ft_roster *r, int def0, int def1) {
  bool training = g->training;
  uint8_t dummy = g->dummy;
  bool cpu[2] = { g->cpu[0], g->cpu[1] };
  uint8_t lvl[2] = { g->cpu_level[0], g->cpu_level[1] };
  memset(g, 0, sizeof *g);
  g->roster = r;
  g->training = training;
  g->dummy = dummy;
  g->cpu[0] = cpu[0]; g->cpu[1] = cpu[1];
  g->cpu_level[0] = lvl[0]; g->cpu_level[1] = lvl[1];
  g->f[0].def = (uint8_t)def0;
  g->f[1].def = (uint8_t)def1;
  g->rng = 12345;
  g->round_no = 1;
  g->match_winner = -1;
  ft_round_start(g);
}

static void spawn_proj(ft_game *g, int who, const ft_move *m, int mvslot) {
  ft_fighter *f = &g->f[who];
  for (int i = 0; i < 8; i++) {
    if (g->proj[i].live) continue;
    g->proj[i] = (ft_proj){ true, (int8_t)who, f->x + f->facing * 700, 1050, f->facing * (m->pspeed + 20 * f->strength), m->plife, m->psize, (uint8_t)mvslot, 0, { 0 } };
    return;
  }
}

static void start_move(ft_game *g, int i, int slot, int strength) {
  ft_fighter *f = &g->f[i];
  const ft_move *m = &DEF(g, i)->mv[slot];
  set_state(f, S_ATTACK);
  f->mv = (uint8_t)slot;
  f->connected = 0;
  f->hit_count = 0;
  f->rehit_t = 0;
  f->strength = (int16_t)strength;
  f->invuln = m->inv;
  if (m->flags & FM_SUPER) {
    f->meter -= FT_METER_MAX;
    g->hitstop = 24;
    emit(g, EV_SUPER, i, 0, f->x, 1000);
  }
  if (!(m->flags & FM_AIR)) f->vx = 0;
  if (m->flags & FM_AIR) f->air_used = 1;
  g->last_move = (uint8_t)slot; g->last_who = (uint8_t)i;
  g->last_su = m->su; g->last_ac = m->ac; g->last_rc = m->rc;
  /* the training readout starts watching for the frame advantage on contact */
  cw_cmd_consume(&f->cb);
}

/* the strength a special was cast with: the attack button that went down last */
static int cast_strength(const ft_fighter *f) {
  int best = -1, age = 99;
  for (int b = 0; b < 3; b++) {
    int a = cw_cmd_pressed_ago(&f->cb, (uint16_t)(1 << b), 6);
    if (a >= 0 && a < age) { age = a; best = b; }
  }
  return best < 0 ? 0 : best;
}

/* choose what the input asks for. cancel_level: -1 free, else only moves that cancel from `cur` */
static bool choose_move(ft_game *g, int i, int cur, bool *is_special, int *slot, int *strength) {
  ft_fighter *f = &g->f[i];
  const ft_def *d = DEF(g, i);
  bool air = ft_airborne(f) || f->state == S_PREJUMP;
  bool cancel = cur >= 0;
  bool cur_special = cancel && (d->mv[cur].flags & (FM_SPECIAL | FM_SUPER));
  bool cur_super = cancel && (d->mv[cur].flags & FM_SUPER);
  *is_special = false;
  if (!air && !cur_super) {
    /* the super, then the specials, each the motion plus an attack button */
    if (f->meter >= FT_METER_MAX && cw_special_match(&f->cb, &d->mo[3], B_ATTACKS, d->mo[3].charge ? 50 : 18, 5) >= 0) {
      *slot = M_SUPER; *strength = cast_strength(f); *is_special = true; return true;
    }
    if (!cur_special) {
      for (int s = 0; s < 3; s++)
        if (cw_special_match(&f->cb, &d->mo[s], B_ATTACKS, d->mo[s].charge ? 50 : 16, 5) >= 0) {
          *slot = M_SP1 + s; *strength = cast_strength(f); *is_special = true; return true;
        }
    }
  }
  if (cur_special) return false;
  /* throw: the throw button, or L and M together */
  if (!air && (cancel == false)) {
    int tp = cw_cmd_pressed_ago(&f->cb, B_T, 3);
    int a1 = cw_cmd_pressed_ago(&f->cb, B_L, 2), a2 = cw_cmd_pressed_ago(&f->cb, B_M, 2);
    bool both = a1 >= 0 && a2 >= 0 && (cw_cmd_held_now(&f->cb) & (B_L | B_M)) == (B_L | B_M);
    if (tp >= 0 || both) { *slot = M_THROW; *strength = 0; return true; }
  }
  int btn = -1;
  for (int b = 2; b >= 0; b--) if (cw_cmd_pressed_ago(&f->cb, (uint16_t)(1 << b), 4) >= 0) { btn = b; break; }
  if (btn < 0) return false;
  int s;
  int dir = dir_of(f);
  bool crouch = dir == 1 || dir == 2 || dir == 3 || f->state == S_CROUCH;
  if (air) s = M_JL + btn;
  else if (crouch) s = M_2L + btn;
  else s = M_5L + btn;
  if (cancel) {
    /* chains: a normal cancels into a stronger one */
    int curbtn = cur % 3;
    if (!(cur <= M_JH) || btn <= curbtn) return false;
  }
  *slot = s; *strength = 0;
  return true;
}

/* ── per-fighter control ─────────────────────────────────────────────── */

static void land(ft_game *g, int i) {
  ft_fighter *f = &g->f[i];
  f->y = 0; f->vy = 0; f->vx = 0; f->air_used = 0;
  emit(g, EV_DUST, i, 1, f->x, 0);
}

static void control(ft_game *g, int i) {
  ft_fighter *f = &g->f[i];
  const ft_def *d = DEF(g, i);
  int fw = f->facing;
  int dir = dir_of(f);
  bool fwd = dir == 6 || dir == 9 || dir == 3, back = dir == 4 || dir == 7 || dir == 1;
  bool up = dir >= 7, down = dir <= 3;
  f->t++;
  if (f->invuln) f->invuln--;
  if (f->rehit_t) f->rehit_t--;
  if (cw_cmd_pressed_ago(&f->cb, B_T, 0) >= 0) f->tech_age = 0; else if (f->tech_age < 200) f->tech_age++;
  switch (f->state) {
  case S_IDLE: case S_WALKF: case S_WALKB: case S_CROUCH: {
    int slot, str;
    bool sp;
    if (choose_move(g, i, -1, &sp, &slot, &str)) { start_move(g, i, slot, str); break; }
    if (up) {
      set_state(f, S_PREJUMP);
      f->jump_dir = (uint8_t)(fwd ? 2 : back ? 0 : 1);
      f->vx = 0;
      break;
    }
    if (cw_motion_match(&f->cb, &(cw_motion){ { 6, 6 }, 2, 0, 3, 30 }, 12) == 0) {
      /* the double tap must be the forward direction relative to facing: the buffer stores it so */
      set_state(f, S_DASHF); cw_cmd_consume(&f->cb); emit(g, EV_DUST, i, 2, f->x, 0); break;
    }
    if (cw_motion_match(&f->cb, &(cw_motion){ { 4, 4 }, 2, 0, 3, 30 }, 12) == 0) {
      set_state(f, S_DASHB); f->invuln = 5; cw_cmd_consume(&f->cb); emit(g, EV_DUST, i, 2, f->x, 0); break;
    }
    if (down) { if (f->state != S_CROUCH) set_state(f, S_CROUCH); f->vx = 0; break; }
    if (fwd) { if (f->state != S_WALKF) set_state(f, S_WALKF); f->vx = fw * d->walk; }
    else if (back) { if (f->state != S_WALKB) set_state(f, S_WALKB); f->vx = -fw * d->back; }
    else { if (f->state != S_IDLE) set_state(f, S_IDLE); f->vx = 0; }
    break;
  }
  case S_PREJUMP:
    if (f->t >= 4) {
      set_state(f, S_AIR);
      f->vy = d->jump_v;
      f->vx = fw * (f->jump_dir - 1) * d->jump_fx;
      emit(g, EV_DUST, i, 1, f->x, 0);
    }
    break;
  case S_AIR: {
    int slot, str;
    bool sp;
    if (!f->air_used && choose_move(g, i, -1, &sp, &slot, &str) && slot >= M_JL && slot <= M_JH) start_move(g, i, slot, 0);
    break;
  }
  case S_ATTACK: {
    const ft_move *m = &d->mv[f->mv];
    if (f->t >= ft_move_total(m)) {
      if (m->flags & FM_AIR) { set_state(f, S_AIR); f->air_used = 1; }
      else set_state(f, down ? S_CROUCH : S_IDLE);
      break;
    }
    if (m->flags & FM_PROJ && f->t == m->su + 1) {
      spawn_proj(g, i, m, f->mv);
      emit(g, EV_FIRE, i, f->mv == M_SUPER ? 2 : 0, f->x + fw * 700, 1050);
    }
    if (!(m->flags & FM_AIR)) {
      if (m->mv && f->t >= m->mvfrom && f->t <= m->mvto) f->vx = fw * m->mv; else f->vx = 0;
    }
    if (f->connected && f->t > m->su) {
      int slot, str;
      bool sp;
      if (choose_move(g, i, f->mv, &sp, &slot, &str) && ((m->flags & FM_AIR) == (d->mv[slot].flags & FM_AIR))) {
        start_move(g, i, slot, str);
      }
    }
    break;
  }
  case S_BLOCK: case S_HIT:
    if (f->stun) f->stun--;
    if (!f->stun) { f->combo_hits = 0; f->combo_dmg = 0; set_state(f, down ? S_CROUCH : S_IDLE); }
    break;
  case S_AIRHIT: break; /* until it lands */
  case S_KD:
    if (f->t >= 40 && f->hp > 0) { set_state(f, S_GETUP); f->invuln = 24; f->combo_hits = 0; f->combo_dmg = 0; }
    break;
  case S_GETUP:
    if (f->t >= 20) set_state(f, S_IDLE);
    break;
  case S_THROWN:
    if (f->stun) f->stun--;
    if (!f->stun) { set_state(f, S_AIRHIT); f->vy = 70; f->vx = -fw * 25; }
    break;
  case S_DASHF:
    f->vx = f->t <= 12 ? fw * d->dash / 8 : 0;
    if (f->t >= 18) set_state(f, S_IDLE);
    break;
  case S_DASHB:
    f->vx = f->t <= 12 ? -fw * d->dash_back / 8 : 0;
    if (f->t >= 22) set_state(f, S_IDLE);
    break;
  case S_LAND:
    if (f->t >= f->stun) set_state(f, S_IDLE);
    break;
  default: break;
  }
}

/* ── movement ────────────────────────────────────────────────────────── */

static void move_fighter(ft_game *g, int i) {
  ft_fighter *f = &g->f[i];
  const ft_def *d = DEF(g, i);
  f->x += f->vx + f->pvx;
  if (f->pvx > 0) { f->pvx -= 5; if (f->pvx < 0) f->pvx = 0; }
  else if (f->pvx < 0) { f->pvx += 5; if (f->pvx > 0) f->pvx = 0; }
  bool air = f->y > 0 || f->vy > 0;
  if (f->state == S_AIR || f->state == S_AIRHIT || (f->state == S_ATTACK && air)) {
    f->y += f->vy;
    f->vy -= d->grav;
    if (f->y <= 0 && f->vy < 0) {
      f->y = 0;
      if (f->state == S_AIRHIT) {
        if (f->hp <= 0 || f->t > 0) { set_state(f, S_KD); f->vx = 0; emit(g, EV_DUST, i, 2, f->x, 0); emit(g, EV_WALL, i, 0, f->x, 0); }
      } else if (f->state == S_ATTACK) {
        const ft_move *m = &d->mv[f->mv];
        land(g, i);
        if (m->flags & FM_AIR) { set_state(f, S_LAND); f->stun = m->rc; }
        else set_state(f, S_IDLE);
      } else { land(g, i); set_state(f, S_LAND); f->stun = 3; }
    }
  }
  if (f->state == S_KD) f->vx = 0;
  if (f->x < -FT_WALL) f->x = -FT_WALL;
  if (f->x > FT_WALL) f->x = FT_WALL;
}

/* ── hits ────────────────────────────────────────────────────────────── */

static const int8_t SCALE[10] = { 100, 100, 90, 80, 70, 60, 50, 40, 30, 30 };

static void finish_ko(ft_game *g, int victim) {
  if (g->training) { g->f[victim].hp = DEF(g, victim)->hp; return; }
  g->phase = P_KO;
  g->phase_t = 0;
  g->round_winner = (int8_t)(1 - victim);
  g->f[victim].state = S_AIRHIT;
  g->f[victim].vy = 100;
  g->f[victim].vx = g->f[1 - victim].facing * 30;
  if (g->f[victim].y < 1) g->f[victim].y = 1;
  g->hitstop += 20;
  emit(g, EV_KO, victim, 0, g->f[victim].x, 1000);
}

/* returns true when the hit connected (hit or blocked) */
static void resolve(ft_game *g, int a, int dfn, const ft_move *m, bool proj, int strength, int hx, int hy) {
  ft_fighter *A = &g->f[a], *D = &g->f[dfn];
  int power = strength;
  /* special strengths scale damage */
  int dmg = m->dmg;
  if (m->flags & (FM_SPECIAL)) dmg = dmg * (4 + strength) / 4;
  bool unblockable = m->flags & FM_THROW;
  bool block = false;
  if (!unblockable && grounded_free(D) && g->phase != P_KO) {
    int dir = dir_of(D);
    if (m->lv == LV_LOW) block = dir == 1;
    else if (m->lv == LV_HIGH) block = dir == 4;
    else block = dir == 4 || dir == 1;
  }
  if (g->training && g->dummy == T_BLOCK && grounded_free(D) && !unblockable) block = true;
  if (block) {
    D->crouch_block = (dir_of(D) == 1) || (g->training && g->dummy == T_BLOCK && D->state == S_CROUCH);
    set_state(D, S_BLOCK);
    D->stun = m->bs + (strength > 0 ? 0 : 0);
    int chip = (m->flags & (FM_SPECIAL | FM_SUPER)) ? dmg / 8 : 0;
    D->hp -= chip;
    if (D->hp < 1) D->hp = 1;
    A->meter += m->mg / 2; if (A->meter > FT_METER_MAX) A->meter = FT_METER_MAX;
    D->meter += m->mg / 4; if (D->meter > FT_METER_MAX) D->meter = FT_METER_MAX;
    g->hitstop = (uint8_t)(m->hstop > 3 ? m->hstop - 2 : m->hstop);
    if (D->x <= -FT_WALL + 50 || D->x >= FT_WALL - 50) { if (!proj) A->pvx = -A->facing * m->pushb; }
    else D->pvx = A->facing * m->pushb;
    if (proj) D->pvx = A->facing * m->pushb;
    emit(g, EV_BLOCK, dfn, power, hx, hy);
    if (!proj) A->connected = 1;
    g->adv_valid = 0;
    g->watch_adv = !proj; g->atk_free_tick = -1; g->def_free_tick = -1; g->last_dmg = (int16_t)chip;
    return;
  }
  if (m->flags & FM_THROW) {
    set_state(D, S_THROWN);
    D->stun = 14;
    D->x = A->x + A->facing * 520;
    emit(g, EV_THROW, dfn, 0, D->x, 900);
  }
  /* damage and combo scaling */
  D->combo_hits++;
  int sc = SCALE[(D->combo_hits - 1) > 9 ? 9 : (D->combo_hits - 1)];
  int dd = dmg * sc / 100;
  if (dd < 1) dd = 1;
  D->hp -= dd;
  D->combo_dmg += dd;
  g->last_dmg = (int16_t)dd; g->last_combo = D->combo_hits;
  A->meter += m->mg; if (A->meter > FT_METER_MAX) A->meter = FT_METER_MAX;
  D->meter += m->mg / 2; if (D->meter > FT_METER_MAX) D->meter = FT_METER_MAX;
  g->hitstop = m->hstop;
  bool was_air = ft_airborne(D) && D->state != S_THROWN;
  if (!(m->flags & FM_THROW)) {
    if (m->launch || m->kd || was_air) {
      bool juggle = was_air;
      set_state(D, S_AIRHIT);
      D->vy = m->launch ? m->launch : (juggle ? 70 : 60);
      D->vx = A->facing * (m->launch ? 14 : 26);
      D->y = D->y < 1 ? 1 : D->y;
    } else {
      set_state(D, S_HIT);
      D->stun = m->hs;
      D->crouch_block = 0;
      if (D->x <= -FT_WALL + 50 || D->x >= FT_WALL - 50) { if (!proj) A->pvx = -A->facing * m->pushh; }
      else D->pvx = A->facing * m->pushh;
    }
  }
  D->invuln = 0;
  emit(g, EV_HIT, dfn, power + (m->kd || m->launch ? 3 : 0), hx, hy);
  if (!proj) A->connected = 1;
  g->adv_valid = 0;
  g->watch_adv = !proj && !(m->kd || m->launch || was_air); g->atk_free_tick = -1; g->def_free_tick = -1;
  if (D->hp <= 0) { D->hp = 0; finish_ko(g, dfn); }
}

static bool can_be_hit(const ft_game *g, int d) {
  const ft_fighter *D = &g->f[d];
  if (D->invuln || D->state == S_KD || D->state == S_GETUP || D->state == S_KO || D->state == S_INTRO || D->state == S_WIN) return false;
  if (D->state == S_THROWN) return false;
  if (g->phase == P_KO || g->phase == P_ROUND_END) return false;
  return true;
}

static void detect_hits(ft_game *g) {
  struct hit { bool yes; int hx, hy; } hits[2] = { { 0 }, { 0 } };
  /* trades: both are checked against the state before either hit lands */
  for (int a = 0; a < 2; a++) {
    ft_fighter *A = &g->f[a];
    int d = 1 - a;
    ft_box hb;
    if (!ft_hitbox_active(g, a, &hb)) continue;
    const ft_move *m = &DEF(g, a)->mv[A->mv];
    if (m->flags & FM_PROJ) continue; /* projectile moves hit through the projectile */
    if (A->hit_count >= m->hits || A->rehit_t) continue;
    if (!can_be_hit(g, d)) continue;
    if (m->flags & FM_THROW) {
      const ft_fighter *D = &g->f[d];
      if (!(grounded_free(D) || D->state == S_DASHF || D->state == S_DASHB || D->state == S_LAND)) continue;
      if (D->state == S_BLOCK && D->stun > 0) { /* a blocking defender can still be thrown after blockstun only */ continue; }
      if (D->invuln) continue;
    }
    ft_box hurts[2];
    int nh = ft_hurtboxes(g, d, hurts);
    for (int k = 0; k < nh; k++)
      if (overlap(hb, hurts[k])) {
        hits[a].yes = true;
        hits[a].hx = (hb.x0 + hb.x1) / 2;
        hits[a].hy = (hb.y0 + hb.y1) / 2;
        break;
      }
  }
  /* throw tech: both throw at once, or the defender pressed throw within 7 frames */
  for (int a = 0; a < 2; a++) {
    if (!hits[a].yes) continue;
    const ft_move *m = &DEF(g, a)->mv[g->f[a].mv];
    if (m->flags & FM_THROW) {
      ft_fighter *D = &g->f[1 - a], *A = &g->f[a];
      if (D->tech_age <= 7 || (g->f[1 - a].state == S_ATTACK && (DEF(g, 1 - a)->mv[D->mv].flags & FM_THROW))) {
        hits[a].yes = false;
        set_state(A, S_HIT); A->stun = 12; A->pvx = -A->facing * 50;
        set_state(D, S_HIT); D->stun = 12; D->pvx = -D->facing * 50;
        D->tech_age = 200;
        emit(g, EV_TECH, a, 0, (A->x + D->x) / 2, 1000);
        g->hitstop = 8;
      }
    }
  }
  for (int a = 0; a < 2; a++) {
    if (!hits[a].yes) continue;
    ft_fighter *A = &g->f[a];
    const ft_move *m = &DEF(g, a)->mv[A->mv];
    if (g->f[1 - a].state == S_THROWN && g->f[1 - a].t == 0) continue;
    A->hit_count++;
    A->rehit_t = m->rehit;
    uint8_t before = g->hitstop;
    resolve(g, a, 1 - a, m, false, A->strength, hits[a].hx, hits[a].hy);
    (void)before;
    if (m->flags & FM_THROW && g->f[1 - a].state == S_THROWN) { /* stays thrown */ }
  }
  /* projectiles */
  for (int p = 0; p < 8; p++) {
    ft_proj *P = &g->proj[p];
    if (!P->live) continue;
    int a = P->owner, d = 1 - a;
    ft_box pb = { (int16_t)(P->x - P->size), (int16_t)(P->y - P->size), (int16_t)(P->x + P->size), (int16_t)(P->y + P->size) };
    /* projectiles cancel each other */
    for (int q = p + 1; q < 8; q++) {
      ft_proj *Q = &g->proj[q];
      if (Q->live && Q->owner != P->owner) {
        ft_box qb = { (int16_t)(Q->x - Q->size), (int16_t)(Q->y - Q->size), (int16_t)(Q->x + Q->size), (int16_t)(Q->y + Q->size) };
        if (overlap(pb, qb)) {
          emit(g, EV_CLASH, a, 0, (P->x + Q->x) / 2, P->y);
          bool pbig = g->roster->d[g->f[a].def].mv[P->mv].flags & FM_SUPER, qbig = g->roster->d[g->f[Q->owner].def].mv[Q->mv].flags & FM_SUPER;
          if (!pbig) P->live = false;
          if (!qbig) Q->live = false;
          break;
        }
      }
    }
    if (!P->live || !can_be_hit(g, d)) continue;
    ft_box hurts[2];
    int nh = ft_hurtboxes(g, d, hurts);
    for (int k = 0; k < nh; k++)
      if (overlap(pb, hurts[k])) {
        const ft_move *m = &DEF(g, a)->mv[P->mv];
        resolve(g, a, d, m, true, g->f[a].strength, P->x, P->y);
        if (!(m->flags & FM_SUPER)) P->live = false;
        else P->hit++;
        break;
      }
  }
}

static void update_proj(ft_game *g) {
  for (int p = 0; p < 8; p++) {
    ft_proj *P = &g->proj[p];
    if (!P->live) continue;
    P->x += P->vx;
    if (--P->life <= 0 || P->x < -FT_WALL - 800 || P->x > FT_WALL + 800) P->live = false;
    if (P->hit >= 1 && (g->roster->d[g->f[P->owner].def].mv[P->mv].flags & FM_SUPER) && P->hit > 0) P->live = P->life > 0;
  }
}

/* ── the tick ────────────────────────────────────────────────────────── */

static ft_input dummy_input(ft_game *g) {
  ft_input in = { 0 };
  const ft_fighter *f = &g->f[1];
  int away = f->facing > 0 ? 0 : 1; /* raw: facing right -> back is left */
  switch (g->dummy) {
  case T_CROUCH: in.down = 1; break;
  case T_JUMP: if ((g->tick / 90) % 2 == 0 && f->state != S_AIR) in.up = (g->tick % 90) < 3; break;
  case T_BLOCK: if (away == 0) in.left = 1; else in.right = 1; break;
  default: break;
  }
  return in;
}

void ft_step(ft_game *g, const ft_input in_[2]) {
  g->tick++;
  ft_input in[2] = { in_[0], in_[1] };
  if (g->cpu[0]) in[0] = ft_ai(g, 0);
  if (g->training) {
    if (g->dummy == T_CPU) in[1] = ft_ai(g, 1); else in[1] = dummy_input(g);
  } else if (g->cpu[1]) in[1] = ft_ai(g, 1);
  for (int i = 0; i < 2; i++) {
    ft_fighter *f = &g->f[i];
    f->in = in[i];
    /* during hitstop the buffer holds still: only a change of input is recorded, so a tap made
     * while frozen is still a fresh press when the fight resumes */
    int nd = cw_cmd_dir(in[i].left, in[i].right, in[i].up, in[i].down, f->facing);
    if (!g->hitstop || nd != cw_cmd_dir_now(&f->cb) || (in[i].btn & ~cw_cmd_held_now(&f->cb)))
      cw_cmd_push(&f->cb, in[i].left, in[i].right, in[i].up, in[i].down, f->facing, in[i].btn);
  }
  if (g->hitstop) { g->hitstop--; return; }

  g->phase_t++;
  if (g->phase == P_INTRO) {
    if (g->phase_t >= 100) { g->phase = P_FIGHT; g->phase_t = 0; for (int i = 0; i < 2; i++) set_state(&g->f[i], S_IDLE); emit(g, EV_ROUND, 0, g->round_no, 0, 0); }
    return;
  }
  if (g->phase == P_MATCH_END) {
    for (int i = 0; i < 2; i++) if (g->f[i].state == S_IDLE) g->f[i].t++;
    return;
  }
  if (g->phase == P_FIGHT) {
    if (!g->training && --g->timer <= 0) {
      g->timer = 0;
      int w = g->f[0].hp == g->f[1].hp ? -1 : (g->f[0].hp > g->f[1].hp ? 0 : 1);
      g->round_winner = (int8_t)w;
      g->phase = P_ROUND_END;
      g->phase_t = 0;
    }
  }
  for (int i = 0; i < 2; i++) {
    if (g->phase == P_KO || g->phase == P_ROUND_END) {
      ft_fighter *f = &g->f[i];
      if (g->round_winner == i && ft_actionable(f)) { if (f->state != S_WIN) set_state(f, S_WIN); }
      if (f->state == S_WIN) { f->t++; continue; }
      if (f->state == S_AIRHIT || f->state == S_KD || f->state == S_HIT) {
        if (f->state == S_HIT) { f->stun = 0; }
      }
    }
    if (g->f[i].state != S_WIN) control(g, i);
  }
  for (int i = 0; i < 2; i++) move_fighter(g, i);
  /* push boxes keep grounded fighters apart; fighters in the air pass over */
  {
    ft_fighter *a = &g->f[0], *b = &g->f[1];
    bool ga = a->y <= 0 && a->state != S_KD, gb = b->y <= 0 && b->state != S_KD;
    int min = 560;
    if (ga && gb && iabs(a->x - b->x) < min) {
      int dx = (min - iabs(a->x - b->x));
      int s = a->x <= b->x ? 1 : -1; /* a is left of b */
      if (a->x == b->x) s = a->facing > 0 ? 1 : -1;
      int half = dx / 2 + 1;
      a->x -= s * half; b->x += s * half;
      if (a->x < -FT_WALL) { b->x += (-FT_WALL - a->x); a->x = -FT_WALL; }
      if (b->x > FT_WALL) { a->x -= (b->x - FT_WALL); b->x = FT_WALL; }
      if (a->x > FT_WALL) a->x = FT_WALL;
      if (b->x < -FT_WALL) b->x = -FT_WALL;
    }
  }
  if (g->phase == P_FIGHT || g->phase == P_KO) detect_hits(g);
  update_proj(g);
  /* face the other: only while standing free, so a cross-up reads right after landing */
  for (int i = 0; i < 2; i++) {
    ft_fighter *f = &g->f[i];
    if (ft_actionable(f) || f->state == S_BLOCK || f->state == S_HIT || f->state == S_IDLE || f->state == S_INTRO || f->state == S_WIN) {
      int dx = g->f[1 - i].x - f->x;
      if (dx > 0) f->facing = 1; else if (dx < 0) f->facing = -1;
    }
  }
  /* frame advantage readout (training and tests) */
  if (g->watch_adv) {
    int a = g->last_who, d = 1 - a;
    if (g->atk_free_tick < 0 && ft_actionable(&g->f[a])) g->atk_free_tick = (int32_t)g->tick;
    if (g->def_free_tick < 0 && ft_actionable(&g->f[d])) g->def_free_tick = (int32_t)g->tick;
    if (g->atk_free_tick >= 0 && g->def_free_tick >= 0) { g->adv = (int16_t)(g->def_free_tick - g->atk_free_tick); g->adv_valid = 1; g->watch_adv = false; }
  }
  /* phases */
  if (g->phase == P_KO || g->phase == P_ROUND_END) {
    if (g->phase == P_KO && g->phase_t > 90) { g->phase = P_ROUND_END; g->phase_t = 0; }
    else if (g->phase == P_ROUND_END && g->phase_t > 110) {
      if (g->round_winner >= 0) g->f[g->round_winner].wins++;
      if (g->round_winner >= 0 && g->f[g->round_winner].wins >= 2) { g->phase = P_MATCH_END; g->match_winner = g->round_winner; g->phase_t = 0; }
      else { if (g->round_winner >= 0) g->round_no++; else g->round_no = g->round_no; ft_round_start(g); }
    }
  }
  if (g->training) {
    /* refill once the exchange is over */
    bool idle = true;
    for (int i = 0; i < 2; i++) if (!ft_actionable(&g->f[i]) || g->f[i].combo_hits) idle = false;
    if (idle) {
      for (int i = 0; i < 2; i++) { g->f[i].hp = DEF(g, i)->hp; g->f[i].meter = FT_METER_MAX; }
    }
  }
}

uint32_t ft_hash(const ft_game *g) {
  uint32_t h = 2166136261u;
#define MIX(v) do { uint32_t _v = (uint32_t)(v); h = (h ^ (_v & 255)) * 16777619u; h = (h ^ ((_v >> 8) & 255)) * 16777619u; h = (h ^ ((_v >> 16) & 255)) * 16777619u; h = (h ^ (_v >> 24)) * 16777619u; } while (0)
  for (int i = 0; i < 2; i++) {
    const ft_fighter *f = &g->f[i];
    MIX(f->x); MIX(f->y); MIX(f->vx); MIX(f->vy); MIX(f->pvx); MIX(f->hp); MIX(f->meter); MIX(f->state); MIX(f->t); MIX(f->mv);
    MIX(f->stun); MIX(f->facing); MIX(f->combo_hits); MIX(f->wins); MIX(f->hit_count);
  }
  for (int p = 0; p < 8; p++) if (g->proj[p].live) { MIX(g->proj[p].x); MIX(g->proj[p].life); }
  MIX(g->phase); MIX(g->timer); MIX(g->tick); MIX(g->hitstop); MIX(g->rng);
  return h;
}
