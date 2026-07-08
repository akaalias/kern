/* unit_graph.c — the note-relationship graph model + layout (Editor/graph.c):
 * node dedup (extension-insensitive), typed edges as a kind bitmask,
 * [[wikilink]] scanning, and the deterministic force-directed layout. */
#include <math.h>
#include <string.h>

#include "graph.h"
#include "test.h"

/* ---------------------------------------------------------------- nodes */

static void test_graph_node_dedup_strips_extension(void) {
  graph_clear();
  int a = graph_add_node("Foo.md");
  int b = graph_add_node("Foo");          /* same note, bare wikilink form */
  CHECK(a >= 0);
  CHECK_IEQ(a, b);
  CHECK_IEQ(graph_node_count(), 1);
  CHECK_SEQ(graph_node(a)->name, "Foo");        /* display name loses the ext */
  CHECK_SEQ(graph_open_target(a), "Foo.md");    /* but opens the real file */
}

static void test_graph_node_file_learned_late(void) {
  graph_clear();
  int a = graph_add_node("Bar");          /* first seen as a bare link target */
  CHECK_SEQ(graph_open_target(a), "Bar"); /* no file yet → open the bare name */
  int b = graph_add_node("Bar.md");       /* now the real file shows up */
  CHECK_IEQ(a, b);
  CHECK_SEQ(graph_open_target(a), "Bar.md");
  CHECK_IEQ(graph_node_count(), 1);
}

static void test_graph_find(void) {
  graph_clear();
  graph_add_node("Alpha.md");
  graph_add_node("Beta");
  CHECK_IEQ(graph_find("Alpha"), 0);
  CHECK_IEQ(graph_find("Alpha.md"), 0);   /* find is extension-insensitive too */
  CHECK_IEQ(graph_find("Beta"), 1);
  CHECK_IEQ(graph_find("Gamma"), -1);
}

/* ---------------------------------------------------------------- edges */

static void test_graph_edge_dedup_merges_kinds(void) {
  graph_clear();
  int a = graph_add_node("A");
  int b = graph_add_node("B");
  int e1 = graph_add_edge(a, b, GRAPH_EDGE_LINK);
  int e2 = graph_add_edge(b, a, GRAPH_EDGE_DAY);    /* reversed pair → same edge */
  int e3 = graph_add_edge(a, b, GRAPH_EDGE_LINK);   /* repeat is a no-op */
  CHECK(e1 >= 0);
  CHECK_IEQ(e1, e2);
  CHECK_IEQ(e1, e3);
  CHECK_IEQ(graph_edge_count(), 1);
  CHECK_IEQ((int)graph_edge_kinds_between(a, b),
            GRAPH_EDGE_LINK | GRAPH_EDGE_DAY);
  CHECK_IEQ((int)graph_edge_kinds_between(b, a),
            GRAPH_EDGE_LINK | GRAPH_EDGE_DAY);
  /* degree counts each incident edge once, not once per kind */
  CHECK_IEQ(graph_node(a)->degree, 1);
  CHECK_IEQ(graph_node(b)->degree, 1);
}

static void test_graph_edge_rejects_self_and_bogus(void) {
  graph_clear();
  int a = graph_add_node("A");
  CHECK_IEQ(graph_add_edge(a, a, GRAPH_EDGE_LINK), -1);   /* self-loop */
  CHECK_IEQ(graph_add_edge(a, 99, GRAPH_EDGE_LINK), -1);  /* no such node */
  CHECK_IEQ(graph_add_edge(-1, a, GRAPH_EDGE_LINK), -1);
  CHECK_IEQ(graph_edge_count(), 0);
}

/* ---------------------------------------------------------------- wikilink scan */

static void test_graph_scan_links(void) {
  graph_clear();
  int src = graph_add_node("Src.md");
  graph_scan_links(src,
    "see [[Alpha]] and [[Beta.md]] here\n"
    "not [single] brackets, not [[unclosed\n"
    "self link [[Src]] is ignored\n"
    "[[Alpha]] again is deduped\n");
  CHECK_IEQ(graph_node_count(), 3);            /* Src, Alpha, Beta */
  int ia = graph_find("Alpha"), ib = graph_find("Beta");
  CHECK(ia >= 0);
  CHECK(ib >= 0);
  CHECK_IEQ((int)graph_edge_kinds_between(src, ia), GRAPH_EDGE_LINK);
  CHECK_IEQ((int)graph_edge_kinds_between(src, ib), GRAPH_EDGE_LINK);
  CHECK_SEQ(graph_open_target(ib), "Beta.md"); /* ext form recorded as the file */
  CHECK_IEQ(graph_edge_count(), 2);            /* the self link added nothing */
}

static void test_graph_scan_links_ignores_multiline_and_nested(void) {
  graph_clear();
  int src = graph_add_node("Src");
  graph_scan_links(src, "bad [[spans\nlines]] and [[a[[b]] nests");
  /* "spans\nlines" is not a link; "a[[b" restarts at the inner "[[" → "b" */
  CHECK_IEQ(graph_find("b") >= 0, 1);
  CHECK_IEQ(graph_node_count(), 2);
}

/* No node cap: a real vault has thousands of notes and every one becomes a
   node (the arrays grow on demand). */
static void test_graph_no_node_cap(void) {
  graph_clear();
  char name[32];
  for (int i = 0; i < 1000; i++) {
    snprintf(name, sizeof name, "N%d.md", i);
    CHECK(graph_add_node(name) == i);
  }
  CHECK_IEQ(graph_node_count(), 1000);
  CHECK_IEQ(graph_find("N999"), 999);
  CHECK_SEQ(graph_open_target(700), "N700.md");
}

/* No edge cap either: an 80-node complete graph is 3160 edges. */
static void test_graph_no_edge_cap(void) {
  graph_clear();
  char name[32];
  for (int i = 0; i < 80; i++) { snprintf(name, sizeof name, "E%d", i); graph_add_node(name); }
  for (int i = 0; i < 80; i++)
    for (int j = i + 1; j < 80; j++)
      CHECK(graph_add_edge(i, j, GRAPH_EDGE_LINK) >= 0);
  CHECK_IEQ(graph_edge_count(), 80 * 79 / 2);
}

/* Same-day groups: small ones stay a full clique (every companion connected,
   mutual backlinks — the Context section's semantics), but a big group (a
   bulk-imported day with thousands of notes) becomes a chain, not the
   quadratic clique that would swamp the map. */
static void test_graph_day_group(void) {
  graph_clear();
  char name[32];
  int small[3];
  for (int i = 0; i < 3; i++) { snprintf(name, sizeof name, "S%d", i); small[i] = graph_add_node(name); }
  graph_add_day_group(small, 3);
  CHECK_IEQ(graph_edge_count(), 3);                      /* clique: 3 pairs */
  CHECK((graph_edge_kinds_between(small[0], small[2]) & GRAPH_EDGE_DAY) != 0);
  CHECK_IEQ(graph_node(small[1])->backlinks, 2);         /* mutual credits */

  graph_clear();
  int big[20];
  for (int i = 0; i < 20; i++) { snprintf(name, sizeof name, "B%d", i); big[i] = graph_add_node(name); }
  graph_add_day_group(big, 20);
  CHECK_IEQ(graph_edge_count(), 19);                     /* chain, not 190 */
  for (int i = 0; i < graph_edge_count(); i++)
    CHECK((graph_edge(i)->kinds & GRAPH_EDGE_DAY) != 0);
}

/* ---------------------------------------------------------------- layout */

static float dist(int a, int b) {
  float dx = graph_node(a)->x - graph_node(b)->x;
  float dy = graph_node(a)->y - graph_node(b)->y;
  return sqrtf(dx * dx + dy * dy);
}

/* A large graph (grid-approximated repulsion path) still lays out sanely:
   deterministic, no NaN, linked nodes closer than unlinked, everything at
   finite coordinates. */
static void test_graph_layout_large_graph(void) {
  float first[3][2];
  for (int run = 0; run < 2; run++) {
    graph_clear();
    char name[32];
    for (int i = 0; i < 1200; i++) { snprintf(name, sizeof name, "L%d", i); graph_add_node(name); }
    graph_add_edge(0, 1, GRAPH_EDGE_LINK);       /* one linked pair */
    graph_layout_init(800, 600);
    for (int i = 0; i < 60; i++) graph_layout_step(800, 600);
    int bad = 0;
    for (int i = 0; i < 1200; i++) {
      if (graph_node(i)->x != graph_node(i)->x) bad++;          /* NaN */
      if (fabsf(graph_node(i)->x) > 100000.0f) bad++;
      if (fabsf(graph_node(i)->y) > 100000.0f) bad++;
    }
    CHECK_IEQ(bad, 0);
    CHECK(dist(0, 1) < dist(0, 2));
    for (int i = 0; i < 3; i++) {
      if (run == 0) { first[i][0] = graph_node(i)->x; first[i][1] = graph_node(i)->y; }
      else {
        CHECK(graph_node(i)->x == first[i][0]);      /* bit-identical rerun */
        CHECK(graph_node(i)->y == first[i][1]);
      }
    }
  }
}

/* Big-gas convergence: the grid-approximated repulsion has a noise floor
   (cells re-centroid every step), which used to keep thousands of nodes
   churning at the velocity cap forever. The cooling schedule shrinks the
   per-step displacement cap over time, so the sim provably sleeps; a reheat
   (what a drag calls) restores the cap so the layout responds again. */
static void test_graph_large_layout_cools_to_sleep(void) {
  graph_clear();
  char name[32];
  for (int i = 0; i < 1200; i++) { snprintf(name, sizeof name, "C%d", i); graph_add_node(name); }
  for (int i = 0; i < 1200; i += 13) graph_add_edge(i, (i * 7 + 1) % 1200, GRAPH_EDGE_LINK);
  graph_layout_init(800, 600);
  float move = 1e9f;
  for (int i = 0; i < 3000 && move >= 0.02f; i++) move = graph_layout_step(800, 600);
  CHECK(move < 0.02f);                     /* actually goes to sleep */

  /* cold, a displaced node stays frozen; a reheat wakes the sim up */
  graph_node(0)->x += 500.0f;
  float cold = graph_layout_step(800, 600);
  CHECK(cold < 0.02f);
  graph_layout_reheat();
  float warm = 0.0f;
  for (int i = 0; i < 5; i++) { float m = graph_layout_step(800, 600); if (m > warm) warm = m; }
  CHECK(warm > 0.75f);                     /* responding again */
}

/* Same input → bit-identical layout (no randomness anywhere). */
static void test_graph_layout_deterministic(void) {
  float first[4][2];
  for (int run = 0; run < 2; run++) {
    graph_clear();
    int a = graph_add_node("A"), b = graph_add_node("B");
    int c = graph_add_node("C"), d = graph_add_node("D");
    graph_add_edge(a, b, GRAPH_EDGE_LINK);
    graph_add_edge(b, c, GRAPH_EDGE_LINK);
    (void)d;
    graph_layout_init(800, 600);
    for (int i = 0; i < 200; i++) graph_layout_step(800, 600);
    for (int i = 0; i < 4; i++) {
      if (run == 0) {
        first[i][0] = graph_node(i)->x;
        first[i][1] = graph_node(i)->y;
      } else {
        CHECK(graph_node(i)->x == first[i][0]);
        CHECK(graph_node(i)->y == first[i][1]);
      }
    }
  }
}

/* Linked nodes end up closer together than unlinked ones. */
static void test_graph_layout_attracts_linked(void) {
  graph_clear();
  int a = graph_add_node("A"), b = graph_add_node("B"), c = graph_add_node("C");
  graph_add_edge(a, b, GRAPH_EDGE_LINK);
  graph_layout_init(800, 600);
  for (int i = 0; i < 400; i++) graph_layout_step(800, 600);
  CHECK(dist(a, b) < dist(a, c));
  CHECK(dist(a, b) < dist(b, c));
}

/* Unlinked nodes seeded tight repel to a readable spacing, and the layout
   settles (steps report shrinking movement) without blowing up to NaN. */
static void test_graph_layout_repels_and_settles(void) {
  graph_clear();
  graph_add_node("A");
  graph_add_node("B");
  graph_layout_init(800, 600);
  float start = dist(0, 1);
  float move = 1e9f;
  for (int i = 0; i < 600; i++) move = graph_layout_step(800, 600);
  CHECK(dist(0, 1) > start);
  CHECK(dist(0, 1) > 40.0f);         /* readable separation */
  CHECK(move < 0.5f);                /* settled */
  for (int i = 0; i < 2; i++) {
    CHECK(graph_node(i)->x == graph_node(i)->x);   /* not NaN */
    CHECK(graph_node(i)->y == graph_node(i)->y);
  }
}

/* Everything stays in the neighborhood of the window (center gravity). */
static void test_graph_layout_stays_near_window(void) {
  graph_clear();
  char name[16];
  for (int i = 0; i < 30; i++) {
    snprintf(name, sizeof name, "N%d", i);
    graph_add_node(name);
  }
  for (int i = 1; i < 30; i++) graph_add_edge(0, i, GRAPH_EDGE_LINK);
  graph_layout_init(800, 600);
  for (int i = 0; i < 400; i++) graph_layout_step(800, 600);
  for (int i = 0; i < 30; i++) {
    CHECK(graph_node(i)->x > -400.0f && graph_node(i)->x < 1200.0f);
    CHECK(graph_node(i)->y > -300.0f && graph_node(i)->y < 900.0f);
  }
}

/* A degree-1 node must settle beside its only neighbor — clearly closer than
   the graph's own internal spacing — instead of stranded across the layout
   with an edge as long as the whole map. Exercised for every relation kind
   (the weak DAY/OPENED springs used to lose to global repulsion). */
static void test_graph_leaf_settles_beside_neighbor(void) {
  graph_clear();
  char name[16];
  /* a 16-node connected core: ring + chords, all LINK */
  for (int i = 0; i < 16; i++) { snprintf(name, sizeof name, "C%d", i); graph_add_node(name); }
  for (int i = 0; i < 16; i++) graph_add_edge(i, (i + 1) % 16, GRAPH_EDGE_LINK);
  for (int i = 0; i < 16; i += 4) graph_add_edge(i, (i + 8) % 16, GRAPH_EDGE_LINK);
  int l_link = graph_add_node("L1"); graph_add_edge(l_link, 2,  GRAPH_EDGE_LINK);
  int l_open = graph_add_node("L2"); graph_add_edge(l_open, 7,  GRAPH_EDGE_OPENED);
  int l_day  = graph_add_node("L3"); graph_add_edge(l_day, 12, GRAPH_EDGE_DAY);

  graph_layout_init(800, 600);
  for (int i = 0; i < 800; i++) graph_layout_step(800, 600);

  float core = 0.0f;
  for (int i = 0; i < 16; i++) core += dist(i, (i + 1) % 16);
  core /= 16.0f;
  CHECK(dist(l_link, 2)  < core * 0.75f);
  CHECK(dist(l_open, 7)  < core * 0.75f);
  CHECK(dist(l_day, 12) < core * 0.75f);
}

/* Internal forces must never torque the map as a whole: the degree-normalized
   springs are asymmetric (each endpoint feels a different magnitude), and on
   a vault with a big same-day clique the injected torque used to spin the
   settled layout slowly forever. The rigid rotation/translation modes are
   removed from the force field, so the layout both stops rotating and truly
   sleeps. */
static void test_graph_layout_does_not_rotate(void) {
  graph_clear();
  char name[16];
  int h1 = graph_add_node("H1"), h2 = graph_add_node("H2"), h3 = graph_add_node("H3");
  graph_add_edge(h1, h2, GRAPH_EDGE_LINK);
  graph_add_edge(h2, h3, GRAPH_EDGE_OPENED);
  for (int i = 0; i < 10; i++) { snprintf(name, sizeof name, "A%d", i); graph_add_edge(h1, graph_add_node(name), GRAPH_EDGE_LINK); }
  for (int i = 0; i < 5;  i++) { snprintf(name, sizeof name, "B%d", i); graph_add_edge(h2, graph_add_node(name), GRAPH_EDGE_DAY); }
  for (int i = 0; i < 3;  i++) { snprintf(name, sizeof name, "C%d", i); graph_add_edge(h3, graph_add_node(name), GRAPH_EDGE_OPENED); }
  int day[20];
  for (int i = 0; i < 20; i++) { snprintf(name, sizeof name, "D%d", i); day[i] = graph_add_node(name); }
  for (int i = 0; i < 20; i++)
    for (int j = i + 1; j < 20; j++)
      graph_add_edge(day[i], day[j], GRAPH_EDGE_DAY);
  graph_add_edge(day[0], h1, GRAPH_EDGE_LINK);
  graph_add_edge(day[3], h2, GRAPH_EDGE_LINK);

  graph_layout_init(800, 600);
  for (int i = 0; i < 2000; i++) graph_layout_step(800, 600);

  float cx = 0, cy = 0;
  int n = graph_node_count();
  for (int i = 0; i < n; i++) { cx += graph_node(i)->x; cy += graph_node(i)->y; }
  cx /= (float)n; cy /= (float)n;
  float a0 = atan2f(graph_node(h1)->y - cy, graph_node(h1)->x - cx);

  float move = 1e9f;
  for (int i = 0; i < 1000; i++) move = graph_layout_step(800, 600);

  cx = cy = 0;
  for (int i = 0; i < n; i++) { cx += graph_node(i)->x; cy += graph_node(i)->y; }
  cx /= (float)n; cy /= (float)n;
  float a1 = atan2f(graph_node(h1)->y - cy, graph_node(h1)->x - cx);

  CHECK(fabsf(a1 - a0) < 0.05f);     /* no rigid spin */
  CHECK(move < 0.02f);               /* and it really goes to sleep */
}

/* A hub with many satellites must actually go to sleep: the summed spring
   stiffness on the hub used to flip it in a period-2 oscillation at the
   velocity cap, so the sim never settled (and burned 60fps forever). */
static void test_graph_hub_star_settles(void) {
  graph_clear();
  char name[16];
  int hub = graph_add_node("Hub");
  for (int i = 1; i < 30; i++) {
    snprintf(name, sizeof name, "G%d", i);
    graph_add_edge(hub, graph_add_node(name), GRAPH_EDGE_LINK);
  }
  graph_layout_init(800, 600);
  float move = 1e9f;
  for (int i = 0; i < 2000; i++) move = graph_layout_step(800, 600);
  CHECK(move < 0.02f);               /* below the overlay's sleep threshold */
  for (int i = 1; i < 30; i++)
    CHECK(dist(hub, i) < 400.0f);    /* satellites orbit the hub, not the map */
}

/* ---------------------------------------------------------------- hit test */

static void test_graph_node_at(void) {
  graph_clear();
  int a = graph_add_node("A"), b = graph_add_node("B");
  graph_add_edge(a, b, GRAPH_EDGE_LINK);
  graph_layout_init(800, 600);
  for (int i = 0; i < 100; i++) graph_layout_step(800, 600);
  CHECK(graph_node_radius(a) > 0.0f);
  CHECK_IEQ(graph_node_at(graph_node(a)->x, graph_node(a)->y, 4.0f), a);
  CHECK_IEQ(graph_node_at(graph_node(b)->x, graph_node(b)->y, 4.0f), b);
  CHECK_IEQ(graph_node_at(-5000.0f, -5000.0f, 4.0f), -1);
}

/* Bounding box of the node centers (what the overlay fits to the window). */
static void test_graph_bounds(void) {
  graph_clear();
  float minx = 1, miny = 2, maxx = 3, maxy = 4;
  CHECK_IEQ(graph_bounds(&minx, &miny, &maxx, &maxy), 0);   /* empty graph */
  graph_add_node("A");
  graph_add_node("B");
  graph_add_node("C");
  graph_node(0)->x = -50.0f; graph_node(0)->y = 300.0f;
  graph_node(1)->x = 900.0f; graph_node(1)->y = -20.0f;
  graph_node(2)->x = 400.0f; graph_node(2)->y = 750.0f;
  CHECK_IEQ(graph_bounds(&minx, &miny, &maxx, &maxy), 1);
  CHECK(minx == -50.0f);
  CHECK(miny == -20.0f);
  CHECK(maxx == 900.0f);
  CHECK(maxy == 750.0f);
}

/* Backlinks count references TO a note, direction-aware: scanning a file's
   text credits its targets (deduped within the file), not the file itself. */
static void test_graph_backlinks_from_scans(void) {
  graph_clear();
  int a = graph_add_node("A"), b = graph_add_node("B"), c = graph_add_node("C");
  graph_scan_links(a, "see [[Hub]] and [[Hub]] again");   /* dupes count once */
  graph_scan_links(b, "also [[Hub]]");
  graph_scan_links(c, "[[Hub]] and [[A]]");
  int hub = graph_find("Hub");
  CHECK(hub >= 0);
  CHECK_IEQ(graph_node(hub)->backlinks, 3);
  CHECK_IEQ(graph_node(a)->backlinks, 1);   /* linked once, by C */
  CHECK_IEQ(graph_node(b)->backlinks, 0);   /* links out, nothing links in */
}

/* Mutual links credit both sides once each. */
static void test_graph_backlinks_mutual(void) {
  graph_clear();
  int a = graph_add_node("A"), b = graph_add_node("B");
  graph_scan_links(a, "[[B]]");
  graph_scan_links(b, "[[A]]");
  CHECK_IEQ(graph_node(a)->backlinks, 1);
  CHECK_IEQ(graph_node(b)->backlinks, 1);
}

/* The context relations feed the count through graph_add_backlink. */
static void test_graph_add_backlink(void) {
  graph_clear();
  int a = graph_add_node("A");
  graph_add_backlink(a);
  graph_add_backlink(a);
  graph_add_backlink(-1);            /* out of range: ignored */
  graph_add_backlink(99);
  CHECK_IEQ(graph_node(a)->backlinks, 2);
  graph_clear();
  CHECK_IEQ(graph_add_node("A"), 0);
  CHECK_IEQ(graph_node(0)->backlinks, 0);   /* clear resets the count */
}

/* Backlink count (not raw degree) grows the drawn radius: a note many others
   reference reads bigger than one that merely links out a lot. */
static void test_graph_radius_grows_with_backlinks(void) {
  graph_clear();
  int hub = graph_add_node("Hub"), pump = graph_add_node("Pump");
  char name[16], text[16];
  for (int i = 0; i < 8; i++) {           /* eight notes link TO Hub */
    snprintf(name, sizeof name, "S%d", i);
    int s = graph_add_node(name);
    graph_scan_links(s, "[[Hub]]");
    (void)s;
  }
  snprintf(text, sizeof text, "[[S0]]");   /* Pump links OUT (high degree via edges) */
  for (int i = 0; i < 8; i++) {
    snprintf(text, sizeof text, "[[S%d]]", i);
    graph_scan_links(pump, text);
  }
  CHECK_IEQ(graph_node(hub)->backlinks, 8);
  CHECK_IEQ(graph_node(pump)->backlinks, 0);
  CHECK(graph_node_radius(hub) > graph_node_radius(pump));
  /* every S was linked once by Pump, so they sit between */
  CHECK(graph_node_radius(graph_find("S0")) > graph_node_radius(pump));
  CHECK(graph_node_radius(hub) > graph_node_radius(graph_find("S0")));
}

void suite_graph(void) {
  RUN(test_graph_node_dedup_strips_extension);
  RUN(test_graph_node_file_learned_late);
  RUN(test_graph_find);
  RUN(test_graph_edge_dedup_merges_kinds);
  RUN(test_graph_edge_rejects_self_and_bogus);
  RUN(test_graph_scan_links);
  RUN(test_graph_scan_links_ignores_multiline_and_nested);
  RUN(test_graph_no_node_cap);
  RUN(test_graph_no_edge_cap);
  RUN(test_graph_day_group);
  RUN(test_graph_layout_large_graph);
  RUN(test_graph_large_layout_cools_to_sleep);
  RUN(test_graph_layout_deterministic);
  RUN(test_graph_layout_attracts_linked);
  RUN(test_graph_layout_repels_and_settles);
  RUN(test_graph_layout_stays_near_window);
  RUN(test_graph_leaf_settles_beside_neighbor);
  RUN(test_graph_hub_star_settles);
  RUN(test_graph_layout_does_not_rotate);
  RUN(test_graph_node_at);
  RUN(test_graph_bounds);
  RUN(test_graph_backlinks_from_scans);
  RUN(test_graph_backlinks_mutual);
  RUN(test_graph_add_backlink);
  RUN(test_graph_radius_grows_with_backlinks);
}
