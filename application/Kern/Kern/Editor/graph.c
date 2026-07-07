/* graph.c — note-relationship graph model + layout for the C-x g graph view.
   Pure C, renderer-independent; headless-tested in tests/unit_graph.c.

   Layout is a damped force-directed embedder (Fruchterman-Reingold flavored):
   pairwise repulsion K²/d, springs d²/K along edges (wikilinks pull harder
   than the softer day/opened relations), and a linear pull to the window
   center so disconnected notes can't drift off-screen. Everything is
   deterministic — the seed placement is a golden-angle spiral, ties are
   broken by node index — so the same vault always lays out the same way. */
#include "graph.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static GraphNode g_nodes[GRAPH_MAX_NODES];
static int       g_node_count;
static GraphEdge g_edges[GRAPH_MAX_EDGES];
static int       g_edge_count;

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
    if (g_node_count >= GRAPH_MAX_NODES) return -1;
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
  if (g_edge_count >= GRAPH_MAX_EDGES) return -1;
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

void graph_scan_links(int from, const char *text) {
  if (from < 0 || from >= g_node_count || !text) return;
  /* one backlink per distinct target per scan — a note that writes [[X]]
     twice still references X once */
  static unsigned char credited[GRAPH_MAX_NODES];
  memset(credited, 0, sizeof credited);
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
        int to = graph_add_node(name);
        if (to >= 0 && to != from) {
          graph_add_edge(from, to, GRAPH_EDGE_LINK);
          if (!credited[to]) {
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

/* ---- layout ---------------------------------------------------------- */

/* Ideal edge length for the current node count in a w×h window. */
static float layout_k(float w, float h) {
  float k = sqrtf(w * h / (float)(g_node_count + 1)) * 0.6f;
  if (k < 60.0f)  k = 60.0f;
  if (k > 170.0f) k = 170.0f;
  return k;
}

void graph_layout_init(float w, float h) {
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
}

float graph_layout_step(float w, float h) {
  int n = g_node_count;
  if (n == 0) return 0.0f;
  const float DT = 0.02f, DAMP = 0.6f, GRAV = 0.12f;
  float k = layout_k(w, h);
  float cx = w * 0.5f, cy = h * 0.5f;
  static float fx[GRAPH_MAX_NODES], fy[GRAPH_MAX_NODES];
  memset(fx, 0, sizeof(float) * (size_t)n);
  memset(fy, 0, sizeof(float) * (size_t)n);

  /* pairwise repulsion k²/d */
  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      float dx = g_nodes[i].x - g_nodes[j].x;
      float dy = g_nodes[i].y - g_nodes[j].y;
      float d2 = dx * dx + dy * dy;
      if (d2 < 0.01f) {                    /* coincident: split by index, not rand */
        dx = 0.31f * (float)(i - j);
        dy = 0.17f * (float)(i + j + 1);
        d2 = dx * dx + dy * dy;
      }
      float d = sqrtf(d2);
      /* inverse-square repulsion (k³/d²): unlike F-R's k²/d it decays fast
         enough that a big vault can't collectively out-push the center
         gravity and drift the whole ring off-screen. Premultiplied for dx. */
      float f = k * k * k / (d2 * d);
      fx[i] += dx * f; fy[i] += dy * f;
      fx[j] -= dx * f; fy[j] -= dy * f;
    }
  }
  /* springs along edges (d²/k), wikilinks stiffer than day/opened relations */
  for (int i = 0; i < g_edge_count; i++) {
    GraphEdge *e = &g_edges[i];
    float weight = (e->kinds & GRAPH_EDGE_LINK)   ? 1.0f
                 : (e->kinds & GRAPH_EDGE_OPENED) ? 0.5f
                                                  : 0.3f;
    float dx = g_nodes[e->a].x - g_nodes[e->b].x;
    float dy = g_nodes[e->a].y - g_nodes[e->b].y;
    if (dx * dx + dy * dy < 0.01f) continue;
    float f = (1.0f / k) * 0.6f * weight;  /* (d²/k) / d = d/k, premultiplied */
    fx[e->a] -= dx * f; fy[e->a] -= dy * f;
    fx[e->b] += dx * f; fy[e->b] += dy * f;
  }
  /* center gravity + damped integration */
  float max_move = 0.0f;
  float vcap = k * 0.35f;
  for (int i = 0; i < n; i++) {
    fx[i] += (cx - g_nodes[i].x) * GRAV;
    fy[i] += (cy - g_nodes[i].y) * GRAV;
    float vx = (g_nodes[i].vx + fx[i] * DT * 60.0f) * DAMP;
    float vy = (g_nodes[i].vy + fy[i] * DT * 60.0f) * DAMP;
    float v = sqrtf(vx * vx + vy * vy);
    if (v > vcap) { vx *= vcap / v; vy *= vcap / v; v = vcap; }
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
