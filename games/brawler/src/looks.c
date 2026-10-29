/* brawler looks: every regular enemy that walks on gets its own skin tone, hair and clothes, so a crowd of one kind is
 * not a crowd of clones. The riot guard keeps its uniform (only its skin tone varies); bosses and fighters keep theirs.
 *
 * How: a glTF instance shares its file's materials, so each rig clones the materials it will recolour (once, when the
 * rig is made) and every spawn sets their base colours. The textures are untouched: cloth, denim and leather textures
 * are near neutral and the material's colour is the dye; the face texture has the model's skin painted in, so the face
 * takes the ratio of the new skin to the model's (darker or lighter by channel, lips and brows with it). A clone does
 * not inherit the engine's skin shading values (cartwheel/skin_shading.h keeps them per material), so a skin clone gets
 * them again, deepened for darker skin as tools/make-characters.mjs does.
 *
 * The choice is the simulation's (look_pick: from the spawn count and the kind, never cw_rng, so the rules and the
 * gates' hashes do not move) and it never repeats the skin and the top of another enemy of the same kind on the street.
 * Rendering only reads it. */
#include <string.h>

#include "cartwheel/math.h"
#include "cartwheel/skin_shading.h"
#include "game.h"

enum { SL_NONE, SL_SKIN, SL_FACE, SL_NAIL, SL_STUBBLE, SL_HAIR, SL_LASH, SL_SHIRT, SL_PANTS, SL_OUTER, SL_HAT, SL_SHOE, SL_MASK, SL_COUNT };
/* which slots each enemy kind varies (the rest of its look is its own) */
static uint32_t slots_of(int def) {
  const uint32_t SKIN = 1u << SL_SKIN | 1u << SL_FACE | 1u << SL_NAIL | 1u << SL_STUBBLE, HAIR = 1u << SL_HAIR | 1u << SL_LASH | 1u << SL_STUBBLE;
  switch (def) {
  case D_THUG: return SKIN | HAIR | 1u << SL_SHIRT | 1u << SL_PANTS | 1u << SL_HAT;
  case D_PUNK: return SKIN | HAIR | 1u << SL_PANTS | 1u << SL_OUTER;
  case D_RUNNER: return SKIN | HAIR | 1u << SL_SHIRT | 1u << SL_PANTS | 1u << SL_OUTER | 1u << SL_HAT | 1u << SL_SHOE;
  case D_KNIFER: return SKIN | HAIR | 1u << SL_PANTS | 1u << SL_OUTER | 1u << SL_MASK;
  case D_BRUISER: return SKIN | 1u << SL_SHIRT | 1u << SL_PANTS;
  case D_THROWER: return SKIN | HAIR | 1u << SL_SHIRT | 1u << SL_PANTS | 1u << SL_OUTER | 1u << SL_HAT;
  case D_GUARD: return SKIN; /* the uniform stays */
  case D_TRUCKER: return SKIN | HAIR | 1u << SL_SHIRT | 1u << SL_PANTS; /* the red cap stays */
  case D_CREEP: return 1u << SL_SHIRT | 1u << SL_PANTS | 1u << SL_OUTER | 1u << SL_HAT; /* always pale: his coat and hat vary */
  default: return 0;
  }
}
static int slot_of(const char *name) {
  const char *s = name ? strchr(name, '_') : NULL;
  if (!s) return SL_NONE;
  s++;
  static const struct { const char *n; int sl; } T[] = { { "skin", SL_SKIN }, { "face", SL_FACE }, { "nail", SL_NAIL }, { "stubble", SL_STUBBLE },
    { "hair", SL_HAIR }, { "hairCard", SL_HAIR }, { "lash", SL_LASH }, { "shirt", SL_SHIRT }, { "pants", SL_PANTS }, { "outer", SL_OUTER },
    { "hat", SL_HAT }, { "shoe", SL_SHOE }, { "mask", SL_MASK } };
  for (size_t i = 0; i < sizeof T / sizeof *T; i++) if (!strcmp(s, T[i].n)) return T[i].sl;
  return SL_NONE;
}

/* ── palettes (sRGB) ───────────────────────────────────────────────────── */
/* skin: from very fair to deep brown, the range a city street has */
static const uint32_t SKIN[] = { 0xf2d3bf, 0xe6bb98, 0xd6a47c, 0xc48e64, 0xae7850, 0x966343, 0x7e5036, 0x673f2a, 0x523121, 0x3f261a }; /* light to dark, evenly spaced: neighbours in the list are already easy to tell apart, two apart clearly */
static const uint32_t HAIR[] = { 0x16110d, 0x241a12, 0x352517, 0x4e3420, 0x6a4a2c, 0x8c6a42, 0x6f6a64, 0x7c3418, 0x0e0c0b };
static const uint32_t DYED[] = { 0x30d0a0, 0xd0307c, 0x3a70e0, 0xd8c830, 0x9040d0, 0xe04a20, 0xe6e6ea }; /* the punk's crest */
/* matte cloth dyes: tees, hoodies, tanks, vests, jackets */
static const uint32_t CLOTH[] = { 0x5a2626, 0x23304a, 0x2f4a2e, 0x303236, 0x9a7428, 0x9a4a20, 0x6a6a6e, 0x3a5a80, 0x18181a, 0x50502e, 0x245a5a, 0x4a1418, 0xc9c0aa, 0x3e2a52, 0x7a2a2a, 0xb8b2a2 };
static const uint32_t PANTS[] = { 0x34384a, 0x2c4a80, 0x1e2a44, 0x4a4430, 0x262626, 0x6e5a44, 0x62728a, 0x55303a, 0x8a8478 };
static const uint32_t LEATHER[] = { 0x18181c, 0x3a2418, 0x4a2a1c, 0x2a2e36, 0x4a1616, 0x1c2a3a, 0x3a3a30 };
static const uint32_t HAT[] = { 0x282828, 0x5a1a1a, 0x1c2a44, 0x2e4a2e, 0xa04020, 0x6a6a6a, 0xc0962c, 0x2a5aa8 };
static const uint32_t SHOE[] = { 0xd04030, 0xe2e2e2, 0x2a4aa0, 0x141414, 0xd8b830, 0x2a8a50 };
static const uint32_t COAT[] = { 0x8a7652, 0x6a6a5c, 0x5a4a34, 0x4a4a3e, 0x7a6a4a, 0x3a3a34 }; /* the creep's trench coats: tan, grey, brown, olive, worn camel, charcoal */
static const uint32_t MASK[] = { 0x6a1a1a, 0x18181a, 0x4a4a4e, 0x1c2a44, 0x3e4a2c };
/* the trucker's tee is the dirt in its texture over a light dye: white, grey, a faded yellow, a pale blue, off white, sand */
static const uint32_t TEE[] = { 0xf4f0e6, 0xd0d0cc, 0xece0b0, 0xc4d2e0, 0xe6e2d4, 0xd8d0b8 };
#define N(a) (int)(sizeof a / sizeof *a)

static float lin(float c) { return c <= 0.04045f ? c / 12.92f : cw_pow((c + 0.055f) / 1.055f, 2.4f); }
static t3_color rgb(uint32_t h) { return (t3_color){ lin((float)((h >> 16) & 255) / 255), lin((float)((h >> 8) & 255) / 255), lin((float)(h & 255) / 255) }; }
static t3_color skin_lin(uint32_t h) { /* the models' skin is their sRGB colour desaturated 14 percent (make-characters.mjs) */
  float c[3] = { (float)((h >> 16) & 255) / 255, (float)((h >> 8) & 255) / 255, (float)(h & 255) / 255 }, l = 0.3f * c[0] + 0.55f * c[1] + 0.15f * c[2];
  for (int i = 0; i < 3; i++) c[i] = lin(c[i] + (l - c[i]) * 0.14f);
  return (t3_color){ c[0], c[1], c[2] };
}

/* a look: palette indices packed in a uint32 (4 bits each: skin, hair, shirt, pants, outer, hat, shoe, mask) */
enum { LK_SKIN, LK_HAIR, LK_SHIRT, LK_PANTS, LK_OUTER, LK_HAT, LK_SHOE, LK_MASK };
static int lk(uint32_t look, int k) { return (int)((look >> (k * 4)) & 15); }
static uint32_t mix32(uint32_t h) { h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16; return h; }
static uint32_t look_make(int def, uint32_t seed) {
  static const int sizes[8] = { N(SKIN), N(HAIR), N(CLOTH), N(PANTS), N(LEATHER), N(HAT), N(SHOE), N(MASK) };
  uint32_t look = 0;
  for (int k = 0; k < 8; k++) {
    int n = sizes[k];
    if (k == LK_HAIR && def == D_PUNK) n = N(DYED);
    if (k == LK_SHIRT && def == D_TRUCKER) n = N(TEE);
    if (k == LK_OUTER && (def == D_RUNNER || def == D_THROWER)) n = N(CLOTH);
    if (k == LK_OUTER && def == D_CREEP) n = N(COAT);
    if (n > 16) n = 16;
    look |= (mix32(seed * 0x9e3779b9u + (uint32_t)k * 0x85ebca6bu + (uint32_t)def * 0xc2b2ae35u) % (uint32_t)n) << (k * 4);
  }
  return look | 0x80000000u; /* the top bit: a look was picked (0 keeps the model's own colours) */
}
/* would read as the same person at game distance: the same skin tone, or fewer than two of skin, top, trousers and hair
   or hat differ (the punk's dyed crest always differs; it is the first thing anyone sees) */
/* would read as the same person at game distance. Two of a kind on the street need skin tones at least two steps apart
   (the palette runs light to dark), different trousers and top, and at least three of skin, top, trousers, jacket, hair
   or hat, and facial hair different;
   the punk's dyed crest always differs (it is the first thing anyone sees) */
static int iabs(int x) { return x < 0 ? -x : x; }
static bool look_same(int def, uint32_t a, uint32_t b) {
  uint32_t s = slots_of(def);
  if (def == D_PUNK && lk(a, LK_HAIR) == lk(b, LK_HAIR)) return true;
  if ((s & 1u << SL_SKIN) && iabs(lk(a, LK_SKIN) - lk(b, LK_SKIN)) < 2) return true;
  if (def == D_GUARD) return false; /* the uniform: skin only */
  if ((s & 1u << SL_PANTS) && lk(a, LK_PANTS) == lk(b, LK_PANTS)) return true; /* the trousers always differ, */
  if ((s & 1u << SL_SHIRT) && lk(a, LK_SHIRT) == lk(b, LK_SHIRT)) return true; /* and so does the top */
  int diff = (s & 1u << SL_SKIN) ? 1 : 0; /* the skin tone */
  if (s & 1u << SL_SHIRT) diff += lk(a, LK_SHIRT) != lk(b, LK_SHIRT);
  if (s & 1u << SL_OUTER) diff += lk(a, LK_OUTER) != lk(b, LK_OUTER);
  if (s & 1u << SL_PANTS) diff += lk(a, LK_PANTS) != lk(b, LK_PANTS);
  if (s & 1u << SL_HAT) diff += lk(a, LK_HAT) != lk(b, LK_HAT);
  else if (s & 1u << SL_HAIR) diff += lk(a, LK_HAIR) != lk(b, LK_HAIR);
  if (s & 1u << SL_STUBBLE) diff += (lk(a, LK_MASK) & 1) != (lk(b, LK_MASK) & 1); /* shaved or not */
  return diff < 3;
}
uint32_t look_pick(int def, int self) {
  if (!slots_of(def)) return 0;
  uint32_t serial = (uint32_t)++W.spawns;
  for (int tries = 0; tries < 48; tries++) {
    uint32_t c = look_make(def, serial * 31u + (uint32_t)tries * 7919u), clash = 0;
    for (int i = 0; i < MAXE; i++) {
      const ent *o = &W.e[i];
      if (i == self || !o->used || o->def != def || !o->look) continue;
      if (look_same(def, c, o->look)) clash = 1;
    }
    if (!clash) return c;
  }
  return look_make(def, serial * 31u);
}

/* ── per rig: the cloned materials ─────────────────────────────────────── */
enum { LMAX = 16, RIGMAX = 96 };
typedef struct look_mat { t3_material *m; int slot; t3_color base; bool skin; cw_skin_shading_desc sd; } look_mat;
static struct { int def, n; look_mat e[LMAX]; t3_color model_skin; uint32_t shown; } RL[RIGMAX];

typedef struct { int rig; uint32_t want; t3_material *orig[LMAX]; } bind_ctx;
static void bind_visit(t3_object *o, void *u) {
  bind_ctx *c = u;
  if (o->type != T3_MESH && o->type != T3_SKINNED_MESH) return;
  t3_mesh *mesh = (t3_mesh *)o;
  for (int i = 0; i < mesh->material_count; i++) {
    t3_material *m = mesh->materials[i];
    int sl = m ? slot_of(m->name) : SL_NONE;
    if (!sl || !(c->want & 1u << sl)) continue;
    int k = 0;
    while (k < RL[c->rig].n && c->orig[k] != m) k++;
    if (k == RL[c->rig].n) { /* first use of this material in the rig: its clone */
      if (k >= LMAX) continue;
      look_mat *e = &RL[c->rig].e[k];
      c->orig[k] = m;
      e->m = t3_material_clone(m), e->slot = sl, e->base = m->color;
      e->skin = cw_skin_shading_get(m, &e->sd);
      if (e->skin) cw_skin_shading_apply(e->m, &e->sd);
      if (sl == SL_SKIN) RL[c->rig].model_skin = m->color;
      RL[c->rig].n++;
    } else t3_retain(RL[c->rig].e[k].m);
    mesh->materials[i] = RL[c->rig].e[k].m;
    t3_release(m); /* the mesh held the shared one */
  }
}
void looks_bind(int rig, int def, t3_object *inst) {
  if (rig < 0 || rig >= RIGMAX) return;
  memset(&RL[rig], 0, sizeof RL[rig]);
  RL[rig].def = def;
  uint32_t want = slots_of(def);
  if (!want || !inst) return;
  bind_ctx c = { rig, want, { 0 } };
  t3_object_traverse(inst, bind_visit, &c);
}
/* the rigs went with the scene: the skin shading records still hold the clones */
void looks_reset(void) {
  for (int r = 0; r < RIGMAX; r++) {
    for (int k = 0; k < RL[r].n; k++) if (RL[r].e[k].skin) cw_skin_shading_remove(RL[r].e[k].m);
    RL[r].n = 0;
  }
}

static t3_color mulc(t3_color a, float k) { return (t3_color){ a.r * k, a.g * k, a.b * k }; }
static t3_color lerpc(t3_color a, t3_color b, float t) { return (t3_color){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
static float ratio(float a, float b) { float r = b > 1e-4f ? a / b : 1; return r < 0.12f ? 0.12f : r > 1.35f ? 1.35f : r; }
void looks_apply(int rig, uint32_t look) {
  if (rig < 0 || rig >= RIGMAX || !RL[rig].n || RL[rig].shown == look) return;
  RL[rig].shown = look;
  int def = RL[rig].def;
  t3_color ms = RL[rig].model_skin;
  t3_color sk = look ? skin_lin(SKIN[lk(look, LK_SKIN) % N(SKIN)]) : ms;
  t3_color rt = { ratio(sk.r, ms.r), ratio(sk.g, ms.g), ratio(sk.b, ms.b) };
  t3_color hair = look ? rgb(def == D_PUNK ? DYED[lk(look, LK_HAIR) % N(DYED)] : HAIR[lk(look, LK_HAIR) % N(HAIR)]) : (t3_color){ -1, 0, 0 };
  float dark = 0;
  { uint32_t h = SKIN[lk(look, LK_SKIN) % N(SKIN)]; dark = 1 - ((float)((h >> 16) & 255) + (float)((h >> 8) & 255) + (float)(h & 255)) / 765.0f; }
  for (int k = 0; k < RL[rig].n; k++) {
    look_mat *e = &RL[rig].e[k];
    t3_color c = e->base;
    if (look) switch (e->slot) {
      case SL_SKIN: c = sk; break;
      case SL_FACE: case SL_NAIL: c = (t3_color){ e->base.r * rt.r, e->base.g * rt.g, e->base.b * rt.b }; break;
      case SL_STUBBLE: /* hair over skin, as the model mixes it; or shaved (half of them): the skin itself */
        c = (lk(look, LK_MASK) & 1) ? sk : lerpc(mulc(hair.r >= 0 ? hair : e->base, 0.6f), sk, 0.4f);
        break;
      case SL_HAIR: if (hair.r >= 0) c = hair; break;
      case SL_LASH: if (hair.r >= 0) c = (t3_color){ hair.r * 0.35f + 0.004f, hair.g * 0.35f + 0.004f, hair.b * 0.35f + 0.004f }; break;
      case SL_SHIRT: c = rgb(def == D_TRUCKER ? TEE[lk(look, LK_SHIRT) % N(TEE)] : CLOTH[lk(look, LK_SHIRT) % N(CLOTH)]); break;
      case SL_PANTS: c = rgb(PANTS[lk(look, LK_PANTS) % N(PANTS)]); break;
      case SL_OUTER: c = rgb(def == D_CREEP ? COAT[lk(look, LK_OUTER) % N(COAT)] : def == D_RUNNER || def == D_THROWER ? CLOTH[lk(look, LK_OUTER) % N(CLOTH)] : LEATHER[lk(look, LK_OUTER) % N(LEATHER)]); break;
      case SL_HAT: c = rgb(def == D_CREEP ? COAT[(lk(look, LK_HAT) + 2) % N(COAT)] : HAT[lk(look, LK_HAT) % N(HAT)]); break; /* the creep's fedora: browns and greys */
      case SL_SHOE: c = rgb(SHOE[lk(look, LK_SHOE) % N(SHOE)]); break;
      case SL_MASK: c = rgb(MASK[lk(look, LK_MASK) % N(MASK)]); break;
      default: break;
    }
    e->m->color = c; /* a uniform: no new program */
    if (e->skin) { /* darker skin scatters a deeper red and lets less light through (make-characters.mjs shading()) */
      cw_skin_shading_desc d = e->sd;
      if (look) {
        d.scatter = (t3_color){ 1.0f + (0.9f - 1.0f) * dark, 0.36f + (0.3f - 0.36f) * dark, 0.24f + (0.2f - 0.24f) * dark };
        d.translucency = 0.38f + (0.22f - 0.38f) * dark;
      }
      cw_skin_shading_apply(e->m, &d);
    }
  }
}
int looks_clones(void) {
  int n = 0;
  for (int i = 0; i < MAXE; i++) for (int j = i + 1; j < MAXE; j++) {
    const ent *a = &W.e[i], *b = &W.e[j];
    if (!a->used || !b->used || a->def != b->def || !a->look || !b->look || a->st == S_DEAD || b->st == S_DEAD) continue;
    if (look_same(a->def, a->look, b->look)) n++;
  }
  return n;
}
#undef N
