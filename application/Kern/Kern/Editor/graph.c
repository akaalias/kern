/* graph.c — note-relationship graph model + layout for the C-x g graph view.
   Pure C, renderer-independent; headless-tested in tests/unit_graph.c.

   Layout is a damped force-directed embedder: softened inverse-square
   repulsion between all pairs, linear springs along edges (wikilinks pull
   harder than the softer day/opened relations; degree-1 satellites pull
   extra and repel less, so a note with a single connection settles beside
   its one neighbor instead of stranded across the map), and a linear pull
   to the window center so disconnected notes can't drift off-screen.
   Everything is deterministic — the seed placement is a golden-angle spiral
   (degree-1 nodes seeded beside their only neighbor), ties are broken by
   node index — so the same vault always lays out the same way. */
#include "graph.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Storage grows on demand — a vault of thousands of notes is one graph, no
   caps. Buffers are kept across graph_clear so a rebuild doesn't churn. */
static GraphNode *g_nodes;
static int        g_node_count, g_node_cap;
static GraphEdge *g_edges;
static int        g_edge_count, g_edge_cap;

static int ensure_node_cap(int need) {
  if (need <= g_node_cap) return 1;
  int cap = g_node_cap ? g_node_cap : 256;
  while (cap < need) cap *= 2;
  GraphNode *p = realloc(g_nodes, (size_t)cap * sizeof *p);
  if (!p) return 0;
  g_nodes = p;
  g_node_cap = cap;
  return 1;
}

static int ensure_edge_cap(int need) {
  if (need <= g_edge_cap) return 1;
  int cap = g_edge_cap ? g_edge_cap : 1024;
  while (cap < need) cap *= 2;
  GraphEdge *p = realloc(g_edges, (size_t)cap * sizeof *p);
  if (!p) return 0;
  g_edges = p;
  g_edge_cap = cap;
  return 1;
}

void graph_clear(void) {
  g_node_count = 0;
  g_edge_count = 0;
}

/* Length of a trailing note extension (.md/.markdown/.txt), else 0. */
static int note_ext_len(const char *name) {
  const char *dot = strrchr(name, '.');
  if (!dot) return 0;
  if (strcmp(dot, ".md") == 0 || strcmp(dot, ".markdown") == 0 ||
      strcmp(dot, ".txt") == 0)
    return (int)strlen(dot);
  return 0;
}

/* `name` minus any note extension — the node identity ("Foo.md" ≡ "Foo"). */
static void node_key(const char *name, char *out, int outsz) {
  int n = (int)strlen(name) - note_ext_len(name);
  if (n > outsz - 1) n = outsz - 1;
  if (n < 0) n = 0;
  memcpy(out, name, (size_t)n);
  out[n] = '\0';
}

int graph_find(const char *name) {
  if (!name) return -1;
  char key[256];
  node_key(name, key, sizeof key);
  for (int i = 0; i < g_node_count; i++)
    if (strcmp(g_nodes[i].name, key) == 0) return i;
  return -1;
}

int graph_add_node(const char *name) {
  if (!name || !name[0]) return -1;
  char key[256];
  node_key(name, key, sizeof key);
  if (!key[0]) return -1;
  int i = graph_find(name);
  if (i < 0) {
    if (!ensure_node_cap(g_node_count + 1)) return -1;
    i = g_node_count++;
    memset(&g_nodes[i], 0, sizeof g_nodes[i]);
    snprintf(g_nodes[i].name, sizeof g_nodes[i].name, "%s", key);
  }
  /* a name carrying the extension is the on-disk form — remember it so a node
     first seen as a bare [[link]] learns its real file when the scan hits it */
  if (note_ext_len(name) && !g_nodes[i].file[0])
    snprintf(g_nodes[i].file, sizeof g_nodes[i].file, "%s", name);
  return i;
}

const char *graph_open_target(int idx) {
  if (idx < 0 || idx >= g_node_count) return "";
  return g_nodes[idx].file[0] ? g_nodes[idx].file : g_nodes[idx].name;
}

int graph_add_edge(int a, int b, unsigned kind) {
  if (a < 0 || b < 0 || a >= g_node_count || b >= g_node_count || a == b)
    return -1;
  if (a > b) { int t = a; a = b; b = t; }
  for (int i = 0; i < g_edge_count; i++)
    if (g_edges[i].a == a && g_edges[i].b == b) {
      g_edges[i].kinds |= kind;
      return i;
    }
  if (!ensure_edge_cap(g_edge_count + 1)) return -1;
  g_edges[g_edge_count].a = a;
  g_edges[g_edge_count].b = b;
  g_edges[g_edge_count].kinds = kind;
  g_nodes[a].degree++;
  g_nodes[b].degree++;
  return g_edge_count++;
}

int        graph_node_count(void) { return g_node_count; }
int        graph_edge_count(void) { return g_edge_count; }
GraphNode *graph_node(int i) { return &g_nodes[i]; }
GraphEdge *graph_edge(int i) { return &g_edges[i]; }

unsigned graph_edge_kinds_between(int a, int b) {
  if (a > b) { int t = a; a = b; b = t; }
  for (int i = 0; i < g_edge_count; i++)
    if (g_edges[i].a == a && g_edges[i].b == b) return g_edges[i].kinds;
  return 0;
}

/* Does a [[target]] name a note? Bare names ([[My Note]]) and note
   extensions ([[My Note.md]]) do; media/attachment links ([[photo.jpg]],
   [[doc.pdf]]) don't — they stay off the map. A file ending must contain a
   letter, so "[[Release 1.3.5]]" (a version number) still counts as a note;
   spaces or other punctuation after the last dot also mean it's just a name
   ("[[Mrs. Smith]]"). */
static int wikilink_target_is_note(const char *name) {
  const char *dot = strrchr(name, '.');
  if (!dot || dot == name) return 1;
  const char *ext = dot + 1;
  size_t len = strlen(ext);
  if (len == 0 || len > 9) return 1;       /* trailing dot / long tail: a name */
  int has_alpha = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)ext[i];
    if (!isalnum(c)) return 1;             /* not extension-shaped */
    if (isalpha(c)) has_alpha = 1;
  }
  if (!has_alpha) return 1;                /* ".5" — version, not an ending */
  return strcasecmp(ext, "md") == 0 || strcasecmp(ext, "markdown") == 0 ||
         strcasecmp(ext, "txt") == 0;
}

void graph_scan_links(int from, const char *text) {
  if (from < 0 || from >= g_node_count || !text) return;
  /* one backlink per distinct target per scan — a note that writes [[X]]
     twice still references X once. The buffer tracks nodes created mid-scan
     (ghost targets) too, so it's sized past the current count. */
  static unsigned char *credited;
  static int            credited_cap;
  if (g_node_count + 256 > credited_cap) {
    int cap = credited_cap ? credited_cap : 1024;
    while (cap < g_node_count + 256) cap *= 2;
    unsigned char *p = realloc(credited, (size_t)cap);
    if (!p) return;
    credited = p;
    credited_cap = cap;
  }
  memset(credited, 0, (size_t)credited_cap);
  const char *p = text;
  while ((p = strstr(p, "[[")) != NULL) {
    const char *s = p + 2;
    /* walk to the closing "]]"; a newline kills the candidate, an inner '['
       restarts the scan there (so "[[a[[b]]" links b, like the editor's own
       clean-query rule) */
    const char *q = s;
    while (*q && *q != '\n' && *q != '[' && !(q[0] == ']' && q[1] == ']')) q++;
    if (q[0] == ']' && q[1] == ']') {
      int len = (int)(q - s);
      if (len > 0 && len < 256) {
        char name[256];
        memcpy(name, s, (size_t)len);
        name[len] = '\0';
        int to = wikilink_target_is_note(name) ? graph_add_node(name) : -1;
        if (to >= 0 && to != from) {
          graph_add_edge(from, to, GRAPH_EDGE_LINK);
          if (to < credited_cap && !credited[to]) {
            credited[to] = 1;
            g_nodes[to].backlinks++;
          }
        }
      }
      p = q + 2;
    } else if (*q == '[') {
      p = q;          /* re-scan from the inner bracket */
    } else {
      p = s;          /* newline / end of text: give up on this "[[" */
    }
  }
}

void graph_add_backlink(int idx) {
  if (idx < 0 || idx >= g_node_count) return;
  g_nodes[idx].backlinks++;
}

void graph_add_day_group(const int *nodes, int count) {
  if (!nodes || count < 2) return;
  if (count <= 8) {
    /* the Context section's semantics: every companion connected, mutual
       backlink credits */
    for (int i = 0; i < count; i++)
      for (int j = i + 1; j < count; j++) {
        graph_add_edge(nodes[i], nodes[j], GRAPH_EDGE_DAY);
        graph_add_backlink(nodes[i]);
        graph_add_backlink(nodes[j]);
      }
    return;
  }
  /* a bulk-imported day with hundreds of notes: chain them — the full clique
     is quadratic and would swamp the map (5000 notes → 12.5M edges) */
  for (int i = 1; i < count; i++) {
    graph_add_edge(nodes[i - 1], nodes[i], GRAPH_EDGE_DAY);
    graph_add_backlink(nodes[i - 1]);
    graph_add_backlink(nodes[i]);
  }
}

/* ---- layout ---------------------------------------------------------- */

/* Ideal edge length for the current node count in a w×h window. */
static float layout_k(float w, float h) {
  float k = sqrtf(w * h / (float)(g_node_count + 1)) * 0.6f;
  if (k < 60.0f)  k = 60.0f;
  if (k > 170.0f) k = 170.0f;
  return k;
}

/* Cooling temperature: scales the per-step displacement cap. Decays each
   step so the sim always converges; reheated by init and by user contact. */
static float g_temp = 1.0f;

void graph_layout_reheat(void) { g_temp = 1.0f; }

void graph_layout_init(float w, float h) {
  g_temp = 1.0f;
  float cx = w * 0.5f, cy = h * 0.5f;
  float step = (w < h ? w : h) * 0.045f + 8.0f;
  for (int i = 0; i < g_node_count; i++) {
    float ang = (float)i * 2.399963f;              /* golden angle */
    float rad = step * sqrtf((float)i + 0.5f);
    g_nodes[i].x = cx + cosf(ang) * rad;
    g_nodes[i].y = cy + sinf(ang) * rad;
    g_nodes[i].vx = 0.0f;
    g_nodes[i].vy = 0.0f;
  }
  /* second pass: seed each degree-1 node beside its only neighbor, offset
     radially outward (plus an index-based fan so several satellites of one
     hub spread out) — starting in the right basin is what keeps a leaf from
     ending up stranded on the far side of the layout */
  for (int i = 0; i < g_node_count; i++) {
    if (g_nodes[i].degree != 1) continue;
    for (int e = 0; e < g_edge_count; e++) {
      int other = g_edges[e].a == i ? g_edges[e].b
                : g_edges[e].b == i ? g_edges[e].a : -1;
      if (other < 0) continue;
      float ox = g_nodes[other].x - cx, oy = g_nodes[other].y - cy;
      if (ox * ox + oy * oy < 1.0f) { ox = 1.0f; oy = 0.0f; }
      float ang = atan2f(oy, ox) + 0.35f * (float)((i % 5) - 2);
      g_nodes[i].x = g_nodes[other].x + cosf(ang) * step * 1.5f;
      g_nodes[i].y = g_nodes[other].y + sinf(ang) * step * 1.5f;
      break;
    }
  }
}

/* Above this node count the O(n²) exact repulsion walk is replaced by the
   grid approximation (a 5000-note vault is ~12.5M pairs per step — seconds,
   not frames). Below it, the exact path keeps small layouts bit-stable. */
#define GRAPH_EXACT_MAX 512
#define GRID_DIM        16

/* Grid-approximated repulsion: bin nodes into a GRID_DIM² grid over their
   bounding box (counting sort, index order — deterministic), then for each
   node take exact pairwise forces from the 3×3 cell neighborhood and one
   aggregate force per far cell, applied at the cell's centroid weighted by
   its effective mass (degree-1 sources count half, mirroring the exact
   path's satellite discount). */
static void repulsion_grid(int n, float k, float soft, float *fx, float *fy) {
  enum { CELLS = GRID_DIM * GRID_DIM };
  static int   *order;         /* node indexes sorted by cell */
  static int   *cell_of;
  static int    grid_cap;
  if (n > grid_cap) {
    int cap = grid_cap ? grid_cap : 1024;
    while (cap < n) cap *= 2;
    int *po = realloc(order, (size_t)cap * sizeof *po);
    if (!po) return;
    order = po;
    int *pc = realloc(cell_of, (size_t)cap * sizeof *pc);
    if (!pc) return;
    cell_of = pc;
    grid_cap = cap;
  }
  float x0 = g_nodes[0].x, y0 = g_nodes[0].y, x1 = x0, y1 = y0;
  for (int i = 1; i < n; i++) {
    if (g_nodes[i].x < x0) x0 = g_nodes[i].x;
    if (g_nodes[i].x > x1) x1 = g_nodes[i].x;
    if (g_nodes[i].y < y0) y0 = g_nodes[i].y;
    if (g_nodes[i].y > y1) y1 = g_nodes[i].y;
  }
  float cw = (x1 - x0) / (float)GRID_DIM, ch = (y1 - y0) / (float)GRID_DIM;
  if (cw < 1.0f) cw = 1.0f;
  if (ch < 1.0f) ch = 1.0f;

  int   cnt[CELLS], start[CELLS + 1];
  float sx[CELLS], sy[CELLS], eff[CELLS];
  memset(cnt, 0, sizeof cnt);
  memset(sx, 0, sizeof sx);
  memset(sy, 0, sizeof sy);
  memset(eff, 0, sizeof eff);
  for (int i = 0; i < n; i++) {
    int gx = (int)((g_nodes[i].x - x0) / cw);
    int gy = (int)((g_nodes[i].y - y0) / ch);
    if (gx < 0) gx = 0;
    if (gx >= GRID_DIM) gx = GRID_DIM - 1;
    if (gy < 0) gy = 0;
    if (gy >= GRID_DIM) gy = GRID_DIM - 1;
    int c = gy * GRID_DIM + gx;
    cell_of[i] = c;
    cnt[c]++;
    sx[c] += g_nodes[i].x;
    sy[c] += g_nodes[i].y;
    eff[c] += g_nodes[i].degree == 1 ? 0.5f : 1.0f;
  }
  start[0] = 0;
  for (int c = 0; c < CELLS; c++) start[c + 1] = start[c] + cnt[c];
  {
    int fill[CELLS];
    memcpy(fill, start, sizeof fill);
    for (int i = 0; i < n; i++) order[fill[cell_of[i]]++] = i;
  }

  for (int i = 0; i < n; i++) {
    int ci = cell_of[i];
    int cix = ci % GRID_DIM, ciy = ci / GRID_DIM;
    float rfx = 0.0f, rfy = 0.0f;
    for (int c = 0; c < CELLS; c++) {
      if (!cnt[c]) continue;
      int dxc = c % GRID_DIM - cix, dyc = c / GRID_DIM - ciy;
      if (dxc >= -1 && dxc <= 1 && dyc >= -1 && dyc <= 1) {
        /* near: exact pairwise, source satellite discount applied per node */
        for (int oi = start[c]; oi < start[c + 1]; oi++) {
          int j = order[oi];
          if (j == i) continue;
          float dx = g_nodes[i].x - g_nodes[j].x;
          float dy = g_nodes[i].y - g_nodes[j].y;
          float d2 = dx * dx + dy * dy;
          if (d2 < 0.01f) {                /* coincident: split by index */
            dx = 0.31f * (float)(i - j);
            dy = 0.17f * (float)(i + j + 1);
            d2 = dx * dx + dy * dy;
          }
          float d = sqrtf(d2);
          float f = k * k * k / ((d2 + soft * soft) * d);
          if (g_nodes[j].degree == 1) f *= 0.5f;
          rfx += dx * f;
          rfy += dy * f;
        }
      } else {
        /* far: the whole cell as one mass at its centroid */
        float mx = sx[c] / (float)cnt[c], my = sy[c] / (float)cnt[c];
        float dx = g_nodes[i].x - mx, dy = g_nodes[i].y - my;
        float d2 = dx * dx + dy * dy;
        if (d2 < 1.0f) d2 = 1.0f;
        float d = sqrtf(d2);
        float f = eff[c] * k * k * k / ((d2 + soft * soft) * d);
        rfx += dx * f;
        rfy += dy * f;
      }
    }
    if (g_nodes[i].degree == 1) { rfx *= 0.5f; rfy *= 0.5f; }
    fx[i] += rfx;
    fy[i] += rfy;
  }
}

float graph_layout_step(float w, float h) {
  int n = g_node_count;
  if (n == 0) return 0.0f;
  const float DT = 0.02f, DAMP = 0.6f, GRAV = 0.12f;
  const float SOFT = 50.0f;   /* repulsion pole softening radius (px) */
  float k = layout_k(w, h);
  float cx = w * 0.5f, cy = h * 0.5f;
  static float *fx, *fy;
  static int    force_cap;
  if (n > force_cap) {
    int cap = force_cap ? force_cap : 512;
    while (cap < n) cap *= 2;
    float *px = realloc(fx, (size_t)cap * sizeof *px);
    if (!px) return 0.0f;
    fx = px;
    float *py = realloc(fy, (size_t)cap * sizeof *py);
    if (!py) return 0.0f;    /* cap not bumped — retried next call */
    fy = py;
    force_cap = cap;
  }
  memset(fx, 0, sizeof(float) * (size_t)n);
  memset(fy, 0, sizeof(float) * (size_t)n);

  /* Repulsion: inverse-square (k³/d²) — unlike F-R's k²/d it decays fast
     enough that a big vault can't collectively out-push the center gravity
     and drift the whole ring off-screen. The pole is softened (SOFT) so
     crowded satellites settle instead of vcap-jittering, and degree-1 nodes
     claim half the personal space — satellites may nestle close to their
     hub. Small graphs get the exact all-pairs sum; past GRAPH_EXACT_MAX the
     O(n²) walk is replaced by a grid: exact forces from the 3×3 neighborhood
     of cells, one centroid-aggregated force per far cell. The approximation
     isn't exactly momentum-preserving, but the rigid-mode removal below
     absorbs that. */
  if (n <= GRAPH_EXACT_MAX) {
    for (int i = 0; i < n; i++) {
      for (int j = i + 1; j < n; j++) {
        float dx = g_nodes[i].x - g_nodes[j].x;
        float dy = g_nodes[i].y - g_nodes[j].y;
        float d2 = dx * dx + dy * dy;
        if (d2 < 0.01f) {                  /* coincident: split by index, not rand */
          dx = 0.31f * (float)(i - j);
          dy = 0.17f * (float)(i + j + 1);
          d2 = dx * dx + dy * dy;
        }
        float d = sqrtf(d2);
        float f = k * k * k / ((d2 + SOFT * SOFT) * d);
        if (g_nodes[i].degree == 1) f *= 0.5f;
        if (g_nodes[j].degree == 1) f *= 0.5f;
        fx[i] += dx * f; fy[i] += dy * f;
        fx[j] -= dx * f; fy[j] -= dy * f;
      }
    }
  } else {
    repulsion_grid(n, k, SOFT, fx, fy);
  }
  /* linear springs along edges, wikilinks stiffer than day/opened relations;
     a degree-1 endpoint doubles the pull (its single edge is all that holds
     it), and each endpoint's force is normalized by its own degree so a hub
     with dozens of edges has bounded total stiffness (an unnormalized hub
     flips in a period-2 oscillation at the velocity cap and never sleeps) */
  for (int i = 0; i < g_edge_count; i++) {
    GraphEdge *e = &g_edges[i];
    float weight = (e->kinds & GRAPH_EDGE_LINK)   ? 1.0f
                 : (e->kinds & GRAPH_EDGE_OPENED) ? 0.5f
                                                  : 0.3f;
    int da = g_nodes[e->a].degree, db = g_nodes[e->b].degree;
    if (da < 1) da = 1;
    if (db < 1) db = 1;
    weight *= 1.0f + 1.0f / (float)(da < db ? da : db);
    float dx = g_nodes[e->a].x - g_nodes[e->b].x;
    float dy = g_nodes[e->a].y - g_nodes[e->b].y;
    if (dx * dx + dy * dy < 0.01f) continue;
    float f = 0.2f * weight;               /* per-dx factor: force = 0.2·w·d */
    float fa = f / (float)da, fb = f / (float)db;
    fx[e->a] -= dx * fa; fy[e->a] -= dy * fa;
    fx[e->b] += dx * fb; fy[e->b] += dy * fb;
  }
  /* The degree-normalized springs are asymmetric — the two endpoints of an
     edge feel different magnitudes — so the internal forces aren't equal-and-
     opposite and inject a little net momentum and torque every step; damping
     balances the injection into a slow permanent drift/spin of the whole map.
     Remove the rigid translation and rotation modes from the internal force
     field (the gravity below still recenters legitimately). */
  if (n > 1) {
    float mfx = 0.0f, mfy = 0.0f, mx = 0.0f, my = 0.0f;
    for (int i = 0; i < n; i++) {
      mfx += fx[i]; mfy += fy[i];
      mx += g_nodes[i].x; my += g_nodes[i].y;
    }
    mfx /= (float)n; mfy /= (float)n;
    mx /= (float)n;  my /= (float)n;
    float tau = 0.0f, inertia = 0.0f;
    for (int i = 0; i < n; i++) {
      float rx = g_nodes[i].x - mx, ry = g_nodes[i].y - my;
      tau += rx * (fy[i] - mfy) - ry * (fx[i] - mfx);
      inertia += rx * rx + ry * ry;
    }
    float omega = inertia > 1.0f ? tau / inertia : 0.0f;
    for (int i = 0; i < n; i++) {
      float rx = g_nodes[i].x - mx, ry = g_nodes[i].y - my;
      fx[i] -= mfx - ry * omega;
      fy[i] -= mfy + rx * omega;
    }
  }

  /* center gravity + damped integration. The velocity cap cools over time
     (floored below the sleep snap, so a fully-cooled gas freezes solid) —
     without it the grid approximation's per-step force noise keeps a big
     vault churning at the cap forever. */
  float max_move = 0.0f;
  float temp = g_temp < 0.02f ? 0.02f : g_temp;
  float vcap = k * 0.35f * temp;
  g_temp *= 0.9965f;
  /* big vaults get softened gravity: the full pull compresses thousands of
     notes into one dense ball; a gentler center lets the field spread and
     the cluster structure show */
  float grav = n > GRAPH_EXACT_MAX ? GRAV * 0.375f : GRAV;
  for (int i = 0; i < n; i++) {
    fx[i] += (cx - g_nodes[i].x) * grav;
    fy[i] += (cy - g_nodes[i].y) * grav;
    float vx = (g_nodes[i].vx + fx[i] * DT * 60.0f) * DAMP;
    float vy = (g_nodes[i].vy + fy[i] * DT * 60.0f) * DAMP;
    float v = sqrtf(vx * vx + vy * vy);
    if (v > vcap) { vx *= vcap / v; vy *= vcap / v; v = vcap; }
    /* sleep snap: sub-pixel motion is invisible, but a marginal micro-limit-
       cycle would keep the sim awake at 60fps forever — freeze it dead */
    if (v < 0.75f) { vx = 0.0f; vy = 0.0f; v = 0.0f; }
    g_nodes[i].vx = vx;
    g_nodes[i].vy = vy;
    g_nodes[i].x += vx;
    g_nodes[i].y += vy;
    if (v > max_move) max_move = v;
  }
  return max_move;
}

float graph_node_radius(int i) {
  if (i < 0 || i >= g_node_count) return 0.0f;
  float r = 5.0f + 2.2f * sqrtf((float)g_nodes[i].backlinks);
  return r > 18.0f ? 18.0f : r;
}

int graph_bounds(float *minx, float *miny, float *maxx, float *maxy) {
  if (g_node_count == 0) return 0;
  float x0 = g_nodes[0].x, y0 = g_nodes[0].y, x1 = x0, y1 = y0;
  for (int i = 1; i < g_node_count; i++) {
    if (g_nodes[i].x < x0) x0 = g_nodes[i].x;
    if (g_nodes[i].x > x1) x1 = g_nodes[i].x;
    if (g_nodes[i].y < y0) y0 = g_nodes[i].y;
    if (g_nodes[i].y > y1) y1 = g_nodes[i].y;
  }
  *minx = x0; *miny = y0; *maxx = x1; *maxy = y1;
  return 1;
}

int graph_node_at(float x, float y, float slop) {
  int best = -1;
  float best_d2 = 0.0f;
  for (int i = 0; i < g_node_count; i++) {
    float dx = x - g_nodes[i].x, dy = y - g_nodes[i].y;
    float d2 = dx * dx + dy * dy;
    float reach = graph_node_radius(i) + slop;
    if (d2 <= reach * reach && (best < 0 || d2 < best_d2)) {
      best = i;
      best_d2 = d2;
    }
  }
  return best;
}
