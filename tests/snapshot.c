/* snapshot.c — golden render snapshots.
 *
 * Renders representative documents through the same text path the app uses
 * (wrap each logical line, then md_draw_text per visual row) into the capture
 * stub, serializes the ordered draw-op stream, and compares it to a committed
 * golden under tests/snapshots/. This is the parity proof for future refactors
 * of md_render/navigation/microui: the serialized model must stay identical.
 *
 * Regenerate goldens after an intentional change:
 *     KERN_UPDATE_SNAPSHOTS=1 make test
 *
 * Note: this mirrors the in-flow text drawing (inline spans, wrapping, list
 * indent, heading bold). It does not replicate textview.c-only chrome such as
 * hanging heading markers or selection highlights. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "test.h"
#include "buffer.h"
#include "navigation.h"
#include "md_render.h"
#include "stub_renderer.h"

#define SNAP_TEXT mu_color(204, 200, 195, 255)   /* the app's MU_COLOR_TEXT */

/* ---- serialization buffer ---- */

static char sb[1 << 16];
static int  sblen;

static void sb_reset(void) { sblen = 0; sb[0] = '\0'; }

static void sb_addf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int avail = (int)sizeof sb - sblen;
  int w = vsnprintf(sb + sblen, avail, fmt, ap);
  va_end(ap);
  if (w > 0 && w < avail) sblen += w;
}

static const char *style_name(int s) {
  switch (s) {
    case FONT_BOLD:   return "BOLD";
    case FONT_ITALIC: return "ITAL";
    case FONT_MONO:   return "MONO";
    default:          return "REG ";
  }
}

/* ---- render one document the way do_render's text pass does ---- */

static void render_document(const char *const *lines, int n) {
  stub_reset();
  int vrow = 0;
  int lh = nav_line_height();
  int margin = nav_page_margin();

  for (int li = 0; li < n; li++) {
    Line L;
    line_init(&L, lines[li], (int)strlen(lines[li]));
    int indent  = md_list_indent(&L);
    int heading = md_is_heading(&L);
    int marker  = md_list_marker_width(&L);

    int starts[256];
    int rows = nav_get_wrap_breaks(&L, starts, 256);
    for (int r = 0; r < rows; r++) {
      int rs = starts[r];
      int re = (r + 1 < rows) ? starts[r + 1] : L.len;
      int x0 = margin + indent + (r > 0 ? marker : 0);
      int y  = vrow * lh;
      int out = -1;
      md_draw_text(&L, rs, re, x0, y, SNAP_TEXT, heading, -1, &out, 1);
      vrow++;
    }
    free(L.text);
    free(L.md_spans);
  }
}

static void serialize(void) {
  sb_reset();
  for (int i = 0; i < stub_op_count; i++) {
    StubOp *o = &stub_ops[i];
    if (o->kind == STUB_OP_TEXT)
      sb_addf("T %4d %3d %s %3d,%3d,%3d,%3d %s\n",
              o->rect.x, o->rect.y, style_name(o->style),
              o->color.r, o->color.g, o->color.b, o->color.a, o->ch);
    else
      sb_addf("R %4d %3d %3d %3d %3d,%3d,%3d,%3d\n",
              o->rect.x, o->rect.y, o->rect.w, o->rect.h,
              o->color.r, o->color.g, o->color.b, o->color.a);
  }
}

static long read_file(const char *path, char *out, size_t outsz) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  size_t k = fread(out, 1, outsz - 1, f);
  fclose(f);
  out[k] = '\0';
  return (long)k;
}

/* Render `lines`, then compare to (or, in update mode, write) the golden. */
static void check_snapshot(const char *name, const char *const *lines, int n) {
  render_document(lines, n);
  serialize();

  char path[256];
  snprintf(path, sizeof path, "snapshots/%s.snap", name);

  char golden[1 << 16];
  long glen = read_file(path, golden, sizeof golden);
  int update = getenv("KERN_UPDATE_SNAPSHOTS") != NULL;

  if (glen < 0 || update) {
    mkdir("snapshots", 0755);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(sb, 1, sblen, f); fclose(f); }
    fprintf(stdout, "  snapshot %-12s wrote golden (%d bytes)\n", name, sblen);
    return;
  }

  kt_checks++;
  if (strcmp(sb, golden) != 0) {
    kt_cur_failed++;
    kt_failed_checks++;
    fprintf(stderr,
            "  FAIL snapshot %s differs from golden "
            "(re-run with KERN_UPDATE_SNAPSHOTS=1 to inspect/update)\n", name);
  }
}

/* ---- scenarios ---- */

static void snap_plain(void) {
  const char *doc[] = {
    "A short first line.",
    "",
    ("This second paragraph is deliberately long enough that it must wrap "
     "across more than one visual row at the test page width of seventy."),
  };
  check_snapshot("plain", doc, 3);
}

static void snap_inline(void) {
  const char *doc[] = {
    "Some **bold** and _italic_ and `code` words.",
    "A [[Wikilink]] and an ==important highlight== here.",
  };
  check_snapshot("inline", doc, 2);
}

static void snap_inline_wrap(void) {
  /* a bold span that spans the 70-glyph row boundary */
  const char *doc[] = {
    ("**this entire sentence is bold and is long enough to wrap onto a "
     "second visual row while staying bold**"),
  };
  check_snapshot("inline_wrap", doc, 1);
}

static void snap_lists(void) {
  const char *doc[] = {
    "- top level bullet",
    "  - nested bullet",
    "1. first numbered",
    "2. second numbered",
  };
  check_snapshot("lists", doc, 4);
}

static void snap_headings(void) {
  const char *doc[] = {
    "# Heading one",
    "## Heading two",
    "### Heading three",
    "Body text under the headings.",
  };
  check_snapshot("headings", doc, 4);
}

static void snap_links(void) {
  const char *doc[] = {
    "See the [docs](https://example.com/page) for details.",
    "Mixed: **bold**, a [link](u), and `code` together.",
  };
  check_snapshot("links", doc, 2);
}

/* Characterizes current behavior for unterminated/empty/“nested” delimiters —
 * the parser does not nest spans, so these must stay stable across refactors. */
static void snap_edges(void) {
  const char *doc[] = {
    "An unterminated **bold start, a lone _, and a stray ` here.",
    "Empty spans ****, __, ==, and an asterisk * by itself.",
    "No nesting: **bold with _underscore_ kept bold** then normal.",
  };
  check_snapshot("edges", doc, 3);
}

void suite_snapshot(void) {
  RUN(snap_plain);
  RUN(snap_inline);
  RUN(snap_inline_wrap);
  RUN(snap_lists);
  RUN(snap_headings);
  RUN(snap_links);
  RUN(snap_edges);
}
