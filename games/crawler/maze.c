/* crawler floors: the .cwmaze parser (format in maze.h). */
#include "maze.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartwheel/core.h"
#include "cartwheel/vfs.h"

const char *const MZ_THEME_ID[MZ_THEMES] = { "crypt", "sewer", "foundry", "grotto", "cathedral", "abyss" };

static const char *const ITEM_NAME[IT_KINDS] = { "key", "potion", "gold", "cup", "chest", "chest_trap", "food_roast", "food_bread", "food_flagon", "food_plate", "elixir", "charm" };
static const char *const ELIXIR_NAME[EL_KINDS] = { "armor", "shot", "speedshot", "magic", "melee", "speed" };

/* one line of the text into out (no newline), trimmed of trailing spaces and a CR; returns the next line */
static const char *line(const char *p, char *out, int len) {
  int n = 0;
  while (*p && *p != '\n') {
    if (n < len - 1) out[n++] = *p;
    p++;
  }
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\r' || out[n - 1] == '\t')) n--;
  out[n] = 0;
  return *p ? p + 1 : p;
}

static int item_by_name(const char *s, int *arg) {
  *arg = 0;
  if (!strncmp(s, "elixir_", 7)) {
    for (int k = 0; k < EL_KINDS; k++)
      if (!strcmp(s + 7, ELIXIR_NAME[k])) return *arg = k, IT_ELIXIR;
    return -1;
  }
  if (!strncmp(s, "charm_", 6)) {
    int k = atoi(s + 6);
    if (k < 0 || k > 15) return -1;
    return *arg = k, IT_CHARM;
  }
  for (int k = 0; k < IT_KINDS; k++)
    if (k != IT_ELIXIR && k != IT_CHARM && !strcmp(s, ITEM_NAME[k])) return k;
  return -1;
}

#define FAIL(...) do { if (err) snprintf(err, (size_t)errlen, __VA_ARGS__); return false; } while (0)

bool maze_parse(const char *text, maze *m, int (*kind_by_name)(const char *), char *err, int errlen) {
  memset(m, 0, sizeof *m);
  for (int y = 0; y < MZ_MAX; y++)
    for (int x = 0; x < MZ_MAX; x++) m->t[y][x] = T_VOID;
  char ln[256];
  const char *p = text;
  int ln_no = 0;
  bool header = false, sized = false, gridded = false;
  m->theme = -1;
  while (*p) {
    p = line(p, ln, sizeof ln);
    ln_no++;
    if (!ln[0] || ln[0] == '#') continue;
    char w0[32] = "", w1[64] = "", w2[64] = "", w3[64] = "";
    int a, b, c, d;
    sscanf(ln, "%31s", w0);
    if (!header) {
      if (strcmp(w0, "crawler-maze")) FAIL("line %d: not a crawler floor (no crawler-maze header)", ln_no);
      header = true;
      continue;
    }
    if (!strcmp(w0, "floor")) m->floor = atoi(ln + 6);
    else if (!strcmp(w0, "seed")) m->seed = (uint32_t)strtoul(ln + 5, NULL, 10);
    else if (!strcmp(w0, "theme")) {
      sscanf(ln, "%*s %63s", w1);
      for (int k = 0; k < MZ_THEMES; k++)
        if (!strcmp(w1, MZ_THEME_ID[k])) m->theme = k;
      if (m->theme < 0) FAIL("line %d: unknown theme %s", ln_no, w1);
    } else if (!strcmp(w0, "size")) {
      if (sscanf(ln, "%*s %d %d", &a, &b) != 2 || a < 4 || b < 4 || a > MZ_MAX || b > MZ_MAX) FAIL("line %d: bad size", ln_no);
      m->w = a, m->h = b, sized = true;
    } else if (!strcmp(w0, "grid")) {
      if (!sized) FAIL("line %d: grid before size", ln_no);
      for (int y = 0; y < m->h; y++) {
        if (!*p) FAIL("the grid ends at row %d of %d", y, m->h);
        p = line(p, ln, sizeof ln);
        ln_no++;
        if ((int)strlen(ln) > m->w) FAIL("line %d: grid row %d is %d wide, not %d", ln_no, y, (int)strlen(ln), m->w);
        for (int x = 0; x < m->w; x++) {
          char ch = x < (int)strlen(ln) ? ln[x] : ' ';
          int t = ch == '#' ? T_WALL : ch == '.' ? T_FLOOR : ch == 'D' ? T_DOOR : ch == 'B' ? T_BREAK : ch == 'T' ? T_TRAPWALL : ch == ' ' ? T_VOID : -1;
          if (t < 0) FAIL("line %d: unknown tile '%c'", ln_no, ch);
          m->t[y][x] = (uint8_t)t;
        }
      }
      p = line(p, ln, sizeof ln);
      ln_no++;
      if (strcmp(ln, "end")) FAIL("line %d: the grid must close with end", ln_no);
      gridded = true;
    } else if (!strcmp(w0, "start")) {
      if (sscanf(ln, "%*s %d %d", &a, &b) != 2 || m->nstart >= 4) FAIL("line %d: bad start", ln_no);
      m->start[m->nstart][0] = (uint8_t)a, m->start[m->nstart][1] = (uint8_t)b, m->nstart++;
    } else if (!strcmp(w0, "exit")) {
      if (sscanf(ln, "%*s %d %d %d", &a, &b, &c) != 3 || m->nexit >= MZ_MAXEXIT || c < 1) FAIL("line %d: bad exit", ln_no);
      m->exit[m->nexit][0] = (uint8_t)a, m->exit[m->nexit][1] = (uint8_t)b, m->exit[m->nexit][2] = (uint8_t)c, m->nexit++;
    } else if (!strcmp(w0, "spawner") || !strcmp(w0, "monster")) {
      bool sp = w0[0] == 's';
      if (sscanf(ln, "%*s %d %d %63s %d", &a, &b, w1, &c) != 4 || c < 1 || c > 3) FAIL("line %d: bad %s", ln_no, w0);
      int k = kind_by_name ? kind_by_name(w1) : 0;
      if (k < 0) FAIL("line %d: unknown monster %s", ln_no, w1);
      if (sp ? m->nspawn >= MZ_MAXSPAWN : m->nmon >= MZ_MAXMON) FAIL("line %d: too many %ss", ln_no, w0);
      mz_spawn s = { (uint8_t)a, (uint8_t)b, (uint8_t)k, (uint8_t)c };
      if (sp) m->spawn[m->nspawn++] = s;
      else m->mon[m->nmon++] = s;
    } else if (!strcmp(w0, "item")) {
      if (sscanf(ln, "%*s %d %d %63s", &a, &b, w1) != 3 || m->nitem >= MZ_MAXITEM) FAIL("line %d: bad item", ln_no);
      int arg, k = item_by_name(w1, &arg);
      if (k < 0) FAIL("line %d: unknown item %s", ln_no, w1);
      m->item[m->nitem++] = (mz_item){ (uint8_t)a, (uint8_t)b, (uint8_t)k, (uint8_t)arg };
    } else if (!strcmp(w0, "teleport")) {
      if (sscanf(ln, "%*s %d %d", &a, &b) != 2 || m->ntele >= MZ_MAXTELE) FAIL("line %d: bad teleport", ln_no);
      m->tele[m->ntele][0] = (uint8_t)a, m->tele[m->ntele][1] = (uint8_t)b, m->ntele++;
    } else if (!strcmp(w0, "trap")) {
      if (sscanf(ln, "%*s %d %d %d", &a, &b, &c) != 3 || m->ntrap >= MZ_MAXTRAP) FAIL("line %d: bad trap", ln_no);
      m->trap[m->ntrap][0] = (uint8_t)a, m->trap[m->ntrap][1] = (uint8_t)b, m->trap[m->ntrap][2] = (uint8_t)c, m->ntrap++;
    } else if (!strcmp(w0, "ambush")) {
      int e2;
      if (sscanf(ln, "%*s %d %d %63s %d %d", &a, &b, w1, &c, &e2) != 5 || m->nambush >= MZ_MAXAMBUSH || c < 1 || c > 3 || e2 < 1 || e2 > 60) FAIL("line %d: bad ambush", ln_no);
      int k = kind_by_name ? kind_by_name(w1) : 0;
      if (k < 0) FAIL("line %d: unknown monster %s", ln_no, w1);
      m->ambush[m->nambush][0] = (uint8_t)a, m->ambush[m->nambush][1] = (uint8_t)b, m->ambush[m->nambush][2] = (uint8_t)k, m->ambush[m->nambush][3] = (uint8_t)c, m->ambush[m->nambush][4] = (uint8_t)e2, m->nambush++;
    } else if (!strcmp(w0, "trapwall")) {
      if (sscanf(ln, "%*s %d %d %d", &a, &b, &c) != 3 || !gridded || !mz_in(m, a, b)) FAIL("line %d: bad trapwall", ln_no);
      m->tgroup[b][a] = (uint8_t)c;
    } else if (!strcmp(w0, "prop")) {
      if (sscanf(ln, "%*s %d %d %63s %d %63s", &a, &b, w1, &c, w2) != 5 || m->nprop >= MZ_MAXPROP) FAIL("line %d: bad prop", ln_no);
      int kind = !strcmp(w1, "decor") ? 0 : !strcmp(w1, "torch") ? 1 : -1;
      if (kind < 0) FAIL("line %d: unknown prop %s", ln_no, w1);
      int side = w2[0] == 'n' ? 0 : w2[0] == 'e' ? 1 : w2[0] == 's' ? 2 : w2[0] == 'w' ? 3 : atoi(w2) & 3;
      m->prop[m->nprop++] = (mz_prop){ (uint8_t)a, (uint8_t)b, (uint8_t)kind, (uint8_t)c, (uint8_t)side };
    } else if (!strcmp(w0, "light")) {
      mz_light l;
      if (sscanf(ln, "%*s %d %d %f %f %f %f %f %f", &a, &b, &l.h, &l.r, &l.g, &l.b, &l.energy, &l.range) != 8 || m->nlight >= MZ_MAXLIGHT) FAIL("line %d: bad light", ln_no);
      l.x = (uint8_t)a, l.y = (uint8_t)b;
      m->light[m->nlight++] = l;
    } else if (!strcmp(w0, "name")) {
      /* a hand-drawn floor's working name: not shown */
    } else
      FAIL("line %d: unknown record %s", ln_no, w0);
    (void)d, (void)w3;
  }
  if (!header) FAIL("empty file");
  if (m->theme < 0) FAIL("no theme");
  if (!gridded) FAIL("no grid");
  if (!m->nstart) FAIL("no start");
  if (!m->nexit) FAIL("no exit");
  /* every position must be on the grid */
#define ONGRID(X, Y, WHAT) if (!mz_in(m, (X), (Y))) FAIL("%s at %d %d is off the grid", WHAT, (int)(X), (int)(Y))
  for (int i = 0; i < m->nstart; i++) ONGRID(m->start[i][0], m->start[i][1], "a start");
  for (int i = 0; i < m->nexit; i++) ONGRID(m->exit[i][0], m->exit[i][1], "an exit");
  for (int i = 0; i < m->nspawn; i++) ONGRID(m->spawn[i].x, m->spawn[i].y, "a spawner");
  for (int i = 0; i < m->nmon; i++) ONGRID(m->mon[i].x, m->mon[i].y, "a monster");
  for (int i = 0; i < m->nitem; i++) ONGRID(m->item[i].x, m->item[i].y, "an item");
  for (int i = 0; i < m->ntele; i++) ONGRID(m->tele[i][0], m->tele[i][1], "a teleporter");
  for (int i = 0; i < m->ntrap; i++) ONGRID(m->trap[i][0], m->trap[i][1], "a trap");
  for (int i = 0; i < m->nprop; i++) ONGRID(m->prop[i].x, m->prop[i].y, "a prop");
  for (int i = 0; i < m->nlight; i++) ONGRID(m->light[i].x, m->light[i].y, "a light");
  for (int i = 0; i < m->nambush; i++) ONGRID(m->ambush[i][0], m->ambush[i][1], "an ambush");
  return true;
}

void maze_path(int floor, char *out, int len) { snprintf(out, (size_t)len, "levels/%03d.cwmaze", floor); }
void maze_bake_path(int floor, char *out, int len) { snprintf(out, (size_t)len, "lightmaps/%03d.cwlm", floor); }

bool maze_load(int floor, maze *m, int (*kind_by_name)(const char *)) {
  char path[64], err[160];
  maze_path(floor, path, sizeof path);
  size_t len = 0;
  char *buf = (char *)cw_vfs_read(path, &len);
  if (!buf) {
    cw_error("crawler: cannot read %s", path);
    return false;
  }
  char *text = malloc(len + 1);
  memcpy(text, buf, len);
  text[len] = 0;
  free(buf);
  bool ok = maze_parse(text, m, kind_by_name, err, sizeof err);
  if (!ok) cw_error("crawler: %s: %s", path, err);
  free(text);
  return ok;
}

int maze_count(void) {
  size_t len = 0;
  char *buf = (char *)cw_vfs_read("levels/count.txt", &len);
  if (!buf) return 0;
  char tmp[16] = { 0 };
  memcpy(tmp, buf, len < 15 ? len : 15);
  free(buf);
  return atoi(tmp);
}

uint64_t maze_bake_hash(const char *text) {
  uint64_t h = 0xcbf29ce484222325ull;
  char ln[256];
  const char *p = text;
  int grid = 0, rows = 0, hgt = 0;
  while (*p) {
    p = line(p, ln, sizeof ln);
    const char *s = ln;
    while (*s == ' ' || *s == '\t') s++;
    bool take = false;
    if (grid) {
      if (!strcmp(s, "end")) grid = 0;
      else take = rows++ < hgt;
      if (take) { /* grid rows keep their leading spaces (void tiles) */
        for (const char *c = ln; *c; c++) h = (h ^ (uint8_t)*c) * 0x100000001b3ull;
        h = (h ^ (uint8_t)'\n') * 0x100000001b3ull;
      }
      continue;
    }
    if (!strncmp(s, "grid", 4) && (s[4] == 0)) { grid = 1; continue; }
    if (!strncmp(s, "size ", 5)) { int w; sscanf(s + 5, "%d %d", &w, &hgt); take = true; }
    if (!strncmp(s, "theme ", 6) || !strncmp(s, "prop ", 5) || !strncmp(s, "light ", 6)) take = true;
    if (!take) continue;
    for (const char *c = s; *c; c++) h = (h ^ (uint8_t)*c) * 0x100000001b3ull;
    h = (h ^ (uint8_t)'\n') * 0x100000001b3ull;
  }
  return h;
}
