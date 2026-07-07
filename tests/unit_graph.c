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

/* ---------------------------------------------------------------- layout */

static float dist(int a, int b) {
  float dx = graph_node(a)->x - graph_node(b)->x;
  float dy = graph_node(a)->y - graph_node(b)->y;
  return sqrtf(dx * dx + dy * dy);
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
  RUN(test_graph_layout_deterministic);
  RUN(test_graph_layout_attracts_linked);
  RUN(test_graph_layout_repels_and_settles);
  RUN(test_graph_layout_stays_near_window);
  RUN(test_graph_node_at);
  RUN(test_graph_bounds);
  RUN(test_graph_backlinks_from_scans);
  RUN(test_graph_backlinks_mutual);
  RUN(test_graph_add_backlink);
  RUN(test_graph_radius_grows_with_backlinks);
}
