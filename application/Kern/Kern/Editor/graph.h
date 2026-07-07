#ifndef GRAPH_H
#define GRAPH_H

/* Note-relationship graph for the C-x g graph view (Obsidian-style).
   Pure C, renderer-independent: the model (nodes = notes, typed edges), the
   [[wikilink]] scanner, and a deterministic force-directed layout. textview.c
   builds it from the documents dir + the opened-after table and draws it; the
   headless suite (tests/unit_graph.c) exercises everything here directly. */

#define GRAPH_MAX_NODES 256
#define GRAPH_MAX_EDGES 2048

/* Edge kinds — one edge per node pair, kinds OR together as a bitmask. */
enum {
  GRAPH_EDGE_LINK   = 1,   /* a [[wikilink]] between the two notes */
  GRAPH_EDGE_DAY    = 2,   /* created the same calendar day */
  GRAPH_EDGE_OPENED = 4,   /* one was open right before the other */
};

typedef struct {
  char  name[256];   /* display name, note extension stripped — the identity */
  char  file[256];   /* on-disk filename to open; "" until a real file is seen */
  float x, y;        /* layout position (screen-space units) */
  float vx, vy;      /* layout velocity */
  int   degree;      /* incident edges (drives the layout) */
  int   backlinks;   /* references TO this note — incoming wikilinks plus the
                        context relations (same-day companions, appearances in
                        other notes' opened-after lists). Drives the radius. */
} GraphNode;

typedef struct {
  int      a, b;     /* node indices, a < b */
  unsigned kinds;    /* GRAPH_EDGE_* bitmask */
} GraphEdge;

void graph_clear(void);

/* Add (or find) the node for `name`. Identity is extension-insensitive:
   "Foo.md" and "Foo" are the same node (display name "Foo"); a name carrying
   a note extension (.md/.markdown/.txt) also records it as the node's file.
   Returns the node index, or -1 when the table is full / name unusable. */
int  graph_add_node(const char *name);
int  graph_find(const char *name);            /* index or -1 */

/* The name open_or_create_file should receive for this node: the on-disk
   filename when one was seen, else the bare name (same semantics as following
   a [[wikilink]] to a note that doesn't exist yet). */
const char *graph_open_target(int idx);

/* Add `kind` to the a<->b edge (created if new). Self-edges and out-of-range
   indices are ignored. Returns the edge index, or -1. */
int  graph_add_edge(int a, int b, unsigned kind);

int        graph_node_count(void);
int        graph_edge_count(void);
GraphNode *graph_node(int i);
GraphEdge *graph_edge(int i);

/* The kinds bitmask of the edge between a and b (0 = no edge). */
unsigned graph_edge_kinds_between(int a, int b);

/* Scan `text` for [[wikilinks]] and add a LINK edge from node `from` to each
   target (target nodes are created as needed — a link to a note that doesn't
   exist on disk still shows as a node, like Obsidian's ghost nodes). Each
   distinct target also gains one backlink (deduped within the scan, so a note
   linking [[X]] twice still counts once). */
void graph_scan_links(int from, const char *text);

/* Count one reference to node `idx` from elsewhere (a same-day companion, an
   appearance in another note's opened-after list). Grows the drawn radius. */
void graph_add_backlink(int idx);

/* Deterministic initial placement (golden-angle spiral around the w×h
   center) — no randomness, so layouts reproduce exactly. */
void graph_layout_init(float w, float h);

/* One force-directed relaxation step (repulsion between all pairs, springs
   along edges — LINK stronger than DAY/OPENED — and center gravity). Returns
   the largest node displacement this step, so the caller knows when the
   layout has settled. */
float graph_layout_step(float w, float h);

/* Drawn radius for node i (grows with degree). */
float graph_node_radius(int i);

/* Topmost node whose disc (+ `slop` px) covers (x,y); -1 = none. */
int  graph_node_at(float x, float y, float slop);

/* Bounding box of all node centers; 0 (outputs untouched) when empty. The
   overlay uses it to fit the settled layout into the window. */
int  graph_bounds(float *minx, float *miny, float *maxx, float *maxy);

#endif /* GRAPH_H */
