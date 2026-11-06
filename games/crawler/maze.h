/* crawler floors: the .cwmaze level format, parsed for the rules (sim.c), the view (view.c, mazebuild.c), the
 * bake tool (tools/levelview) and the soak build. Text, one record a line, '#' starts a comment line:
 *
 *   crawler-maze 1                 the format and its version (first line)
 *   floor <n>                      the floor's number (1..)
 *   theme <id>                     crypt sewer foundry grotto cathedral abyss
 *   size <w> <h>                   tiles (2 m each), at most MZ_MAX x MZ_MAX
 *   seed <n>                       what made it (the generator's seed, or 0 for a floor drawn by hand)
 *   grid                           then h lines of w characters, then "end":
 *                                    '#' wall   '.' floor   'D' door   'B' breakable wall   'T' trap wall (a wall
 *                                    that vanishes when its trap plate is stepped on)   ' ' nothing (outside)
 *   start <x> <y>                  where the players appear (up to 4 lines; fewer: the rest stand round the first)
 *   exit <x> <y> <skip>            a stair down: to the floor skip ahead (1 the next)
 *   spawner <x> <y> <kind> <str>   a monster spawner: the monster's name (sim_kind_by_name), strength 1..3
 *   monster <x> <y> <kind> <str>   a monster standing there at the start
 *   item <x> <y> <what>            key, potion, gold, cup, chest, chest_trap, food_roast, food_bread, food_flagon,
 *                                  food_plate, elixir_<armor|shot|speedshot|magic|melee|speed>, charm_<n> (0..15)
 *   teleport <x> <y>               a teleporter pad
 *   trap <x> <y> <group>           a trap plate: the 'T' walls of its group vanish when it is stepped on
 *   trapwall <x> <y> <group>       which group a 'T' wall belongs to
 *   ambush <x> <y> <kind> <str> <n>  a room that springs: the first player to step within a tile of (x, y) brings n
 *                                  monsters of that kind out of the floor round the room
 *   prop <x> <y> <kind> <frame> <side>  scenery: kind decor (a floor prop of the tileset's kit, its frame) or torch
 *                                  (mounted on the wall on <side> of floor tile x y: n e s w); decor side is its
 *                                  turn in quarter turns (0..3)
 *   light <x> <y> <height> <r> <g> <b> <energy> <range>   a light in the bake only (torches, glows): its tile
 *                                  centre and height in metres, colour 0..1, energy and range as OmniLight3D
 *
 * Tile (x, y) is column x, row y; row 0 is the far (north, top of the screen) side. Its centre in the world is
 * x = (x - w / 2 + 0.5) * 2, z = (y - h / 2 + 0.5) * 2 metres. */
#ifndef CRAWLER_MAZE_H
#define CRAWLER_MAZE_H

#include <stdbool.h>
#include <stdint.h>

#define MZ_MAX 48      /* tiles a side, at most */
#define MZ_TILE 2.0f   /* metres a tile */
enum { MZ_MAXSPAWN = 64, MZ_MAXITEM = 320, MZ_MAXPROP = 640, MZ_MAXLIGHT = 128, MZ_MAXMON = 256, MZ_MAXEXIT = 4, MZ_MAXTELE = 16,
       MZ_MAXTRAP = 16, MZ_MAXAMBUSH = 8 };
enum { MZ_THEME_CRYPT, MZ_THEME_SEWER, MZ_THEME_FOUNDRY, MZ_THEME_GROTTO, MZ_THEME_CATHEDRAL, MZ_THEME_ABYSS, MZ_THEMES };
extern const char *const MZ_THEME_ID[MZ_THEMES];

/* tile kinds in the grid */
enum { T_FLOOR, T_WALL, T_DOOR, T_BREAK, T_TRAPWALL, T_VOID };

/* item kinds */
enum { IT_KEY, IT_POTION, IT_GOLD, IT_CUP, IT_CHEST, IT_CHEST_TRAP, IT_ROAST, IT_BREAD, IT_FLAGON, IT_PLATE, IT_ELIXIR, IT_CHARM, IT_KINDS };
enum { EL_ARMOR, EL_SHOT, EL_SHOTSPEED, EL_MAGIC, EL_MELEE, EL_SPEED, EL_KINDS };

typedef struct mz_spawn { uint8_t x, y, kind, str; } mz_spawn;
typedef struct mz_item { uint8_t x, y, kind, arg; } mz_item; /* arg: an elixir's or a charm's kind */
typedef struct mz_prop { uint8_t x, y, kind, frame, side; } mz_prop; /* kind 0 decor, 1 torch */
typedef struct mz_light { uint8_t x, y; float h, r, g, b, energy, range; } mz_light;

typedef struct maze {
  int floor, theme, w, h;
  uint32_t seed;
  uint8_t t[MZ_MAX][MZ_MAX];     /* [y][x] tile kind */
  uint8_t tgroup[MZ_MAX][MZ_MAX]; /* a trap wall's group */
  int nstart, nexit, nspawn, nmon, nitem, ntele, ntrap, nprop, nlight, nambush;
  uint8_t ambush[MZ_MAXAMBUSH][5]; /* x, y, kind, strength, count */
  uint8_t start[4][2];
  uint8_t exit[MZ_MAXEXIT][3]; /* x, y, skip */
  mz_spawn spawn[MZ_MAXSPAWN];
  mz_spawn mon[MZ_MAXMON];
  mz_item item[MZ_MAXITEM];
  uint8_t tele[MZ_MAXTELE][2];
  uint8_t trap[MZ_MAXTRAP][3]; /* x, y, group */
  mz_prop prop[MZ_MAXPROP];
  mz_light light[MZ_MAXLIGHT];
} maze;

/* Parse a .cwmaze from its text; false with a message in err when it is not a valid floor. kind_by_name names the
 * monsters (sim_kind_by_name); NULL accepts any name as kind 0. */
bool maze_parse(const char *text, maze *m, int (*kind_by_name)(const char *), char *err, int errlen);
/* Read assets/levels/<n>.cwmaze (an asset path, cw_vfs) and parse it. */
bool maze_load(int floor, maze *m, int (*kind_by_name)(const char *));
/* the asset path of a floor's level file and of its bake */
void maze_path(int floor, char *out, int len);
void maze_bake_path(int floor, char *out, int len);
/* the floors there are (assets/levels/count.txt's number; 0 when it cannot be read) */
int maze_count(void);

static inline float mz_wx(const maze *m, float tx) { return (tx - (float)m->w * 0.5f + 0.5f) * MZ_TILE; }
static inline float mz_wz(const maze *m, float ty) { return (ty - (float)m->h * 0.5f + 0.5f) * MZ_TILE; }
static inline int mz_tx(const maze *m, float x) { return (int)((x / MZ_TILE) + (float)m->w * 0.5f + 1000.0f) - 1000; }
static inline int mz_ty(const maze *m, float z) { return (int)((z / MZ_TILE) + (float)m->h * 0.5f + 1000.0f) - 1000; }
static inline bool mz_in(const maze *m, int x, int y) { return x >= 0 && y >= 0 && x < m->w && y < m->h; }
/* a tile a walker cannot enter (closed doors and standing walls; the sim keeps its own copy as doors open) */
static inline bool mz_solid_kind(int t) { return t != T_FLOOR; }
/* the hash of what a floor's bake depends on: FNV-1a 64 over its text's theme, size, grid, prop and light lines
 * (each trimmed, with a newline), in file order. The bake tool writes it beside the .cwlm (lightmaps/<n>.hash, 16
 * hex digits); tools/check-levels.mjs computes the same from the text and compares. */
uint64_t maze_bake_hash(const char *text);

#endif
