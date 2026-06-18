/* We provide our own entry point (Swift @main calls editor_main), so tell SDL
   not to redefine main / expect SDL_main. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "renderer.h"
#include "microui.h"
#include "macos_style.h"
#include "editor_types.h"
#include "buffer.h"
#include "navigation.h"
#include "editing.h"
#include "md_render.h"
#include "undo.h"

/* ---- two struct instances hold all mutable state ---- */
static EditorState g_ed = {0};
static ViewState   g_vs = {0};

/* shims: map old global names to struct fields */
#define lines           g_ed.lines
#define line_count      g_ed.line_count
#define line_cap        g_ed.line_cap
#define cursor_line     g_ed.cursor_line
#define cursor_col      g_ed.cursor_col
#define cursor_target_col g_ed.cursor_target_col
#define mark_active     g_ed.mark_active
#define mark_line       g_ed.mark_line
#define mark_col        g_ed.mark_col
#define kill_buf        g_ed.kill_buf
#define kill_len        g_ed.kill_len
#define kill_cap        g_ed.kill_cap
#define last_kill_was_k g_ed.last_kill_was_k
#define font_size       g_vs.font_size
#define scroll_y        g_vs.scroll_y
#define scrollbar_dragging g_vs.scrollbar_dragging
#define drag_offset     g_vs.drag_offset
#define g_content_y     g_vs.content_y
#define g_content_h     g_vs.content_h
#define g_vis_rows      g_vs.vis_rows
#define g_vis_row_count g_vs.vis_row_count
#define g_cursor_x      g_vs.cursor_x
#define ctrl_x_prefix   g_vs.ctrl_x_prefix
#define esc_prefix      g_vs.esc_prefix
#define suppress_next_text g_vs.suppress_next_text
#define search_active   g_vs.search_active
#define search_direction g_vs.search_direction
#define search_buf      g_vs.search_buf
#define search_len      g_vs.search_len
#define search_match_line g_vs.search_match_line
#define search_match_col g_vs.search_match_col
#define minibuf_active  g_vs.minibuf_active
#define minibuf_prompt  g_vs.minibuf_prompt
#define minibuf_text    g_vs.minibuf_text
#define minibuf_len     g_vs.minibuf_len
#define status_msg      g_vs.status_msg
#define status_time     g_vs.status_time
#define g_ctx           g_vs.ctx
#define minibuf_callback g_vs.minibuf_callback
#define g_filename      g_ed.filename
#define g_filepath      g_ed.filepath

/* function shims: old names → new buf_* API */
#define ensure_lines_cap(n) buf_ensure_lines_cap(&g_ed, (n))
#define insert_line_at(i,s,l) buf_insert_line_at(&g_ed, (i), (s), (l))
#define delete_line_at(i) buf_delete_line_at(&g_ed, (i))
#define invalidate_all_wraps() buf_invalidate_all_wraps(&g_ed)
#define kill_set(t,l) buf_kill_set(&g_ed, (t), (l))
#define kill_append(t,l) buf_kill_append(&g_ed, (t), (l))
#define mark_set() buf_mark_set(&g_ed)
#define mark_clear() buf_mark_clear(&g_ed)
#define region_ordered(sl,sc,el,ec) buf_region_ordered(&g_ed, (sl),(sc),(el),(ec))

/* function shims: old names → new nav_* API */
#define win_w()           nav_win_w()
#define win_h()           nav_win_h()
#define page_w()          nav_page_w()
#define page_margin()     nav_page_margin()
#define line_height()     nav_line_height()
#define count_wraps(l)    nav_count_wraps(l)
#define get_wrap_breaks(l,s,m) nav_get_wrap_breaks((l),(s),(m))
#define total_visual_lines() nav_total_visual_lines(&g_ed)
#define visual_to_logical(v,o) nav_visual_to_logical(&g_ed,(v),(o))
#define logical_to_visual(l) nav_logical_to_visual(&g_ed,(l))
#define cursor_to_visual(cl,cc) nav_cursor_to_visual(&g_ed,(cl),(cc))
#define cursor_clamp()    nav_cursor_clamp(&g_ed)
#define ensure_cursor_visible() nav_ensure_cursor_visible(&g_ed, &g_vs)
#define click_to_cursor(mx,my) nav_click_to_cursor(&g_ed, &g_vs, (mx), (my))
#define search_find_next(fl,fc) nav_search_find_next(&g_ed, &g_vs, (fl), (fc))
#define search_find_prev(fl,fc) nav_search_find_prev(&g_ed, &g_vs, (fl), (fc))
#define search_find_first() nav_search_find_first(&g_ed, &g_vs)
#define search_find_current_dir() nav_search_find_current_dir(&g_ed, &g_vs)
#define status_set(msg)   nav_status_set(&g_vs, (msg))
#define status_get()      nav_status_get(&g_ed, &g_vs)

/* function shims: old names → new ed_* API */
#define editor_insert_char(t) ed_insert_char(&g_ed, (t))
#define editor_backspace() ed_backspace(&g_ed)
#define editor_delete()   ed_delete(&g_ed)
#define editor_enter()    ed_enter(&g_ed)
#define emacs_kill_line() ed_emacs_kill_line(&g_ed)
#define emacs_yank()      ed_emacs_yank(&g_ed)
#define emacs_kill_region() ed_emacs_kill_region(&g_ed)
#define emacs_forward_word() ed_emacs_forward_word(&g_ed)
#define emacs_backward_word() ed_emacs_backward_word(&g_ed)

/* function shims: old names → new md_* API */
#define list_indent(l)    md_list_indent(l)
#define is_heading(l)     md_is_heading(l)
#define draw_md_text(text,start,end,x,y,col,head,track) \
  md_draw_text((text),(start),(end),(x),(y),(col),(head),(track),&g_cursor_x)

/* ---- minibuffer filename completion ---- */

static int  minibuf_completing = 0;   /* show ghost completion for this prompt */
static char minibuf_suggest[1024];    /* full completed name; begins with minibuf_text */

/* Recompute the ghost suggestion from the current minibuffer text. */
static void minibuf_refresh_completion(void) {
  minibuf_suggest[0] = '\0';
  if (minibuf_completing && minibuf_len > 0) {
    char full[1024];
    if (buf_complete_filename(minibuf_text, full, sizeof(full)))
      snprintf(minibuf_suggest, sizeof(minibuf_suggest), "%s", full);
  }
}

/* ---- file operations (glue — uses status_set and r_set_title) ---- */

static void open_or_create_file(const char *path) {
  /* resolve the typed name to a path inside the sandbox documents dir */
  buf_resolve_path(path, g_filepath, sizeof(g_filepath));
  const char *slash = strrchr(g_filepath, '/');
  g_filename = slash ? slash + 1 : g_filepath;

  buf_free_all_lines(&g_ed);
  int existed = (buf_load_file(&g_ed, g_filepath) == 0);
  if (!existed) buf_init_empty(&g_ed);

  cursor_line = 0;
  cursor_col = 0;
  cursor_target_col = 0;
  scroll_y = 0;
  invalidate_all_wraps();
  r_set_title(g_filename);
  char msg[256];
  snprintf(msg, sizeof(msg), existed ? "Opened %s" : "New file %s", g_filename);
  status_set(msg);
}

static void save_to_path(const char *path) {
  buf_resolve_path(path, g_filepath, sizeof(g_filepath));
  const char *slash = strrchr(g_filepath, '/');
  g_filename = slash ? slash + 1 : g_filepath;
  char msg[256];
  if (buf_save(&g_ed, g_filepath) == 0) {
    snprintf(msg, sizeof(msg), "Wrote %s", g_filename);
  } else {
    snprintf(msg, sizeof(msg), "FAILED to write %s", g_filename);
  }
  status_set(msg);
  r_set_title(g_filename);
}

/* word wrapping, cursor navigation, editing, and markdown rendering
   now in navigation.c, editing.c, md_render.c (accessed via shims above) */

/* ---- undo is now in undo.c (operation-based) ---- */

/* ---- frame ---- */
static void process_frame(mu_Context *ctx) {
  mu_begin(ctx);
  g_vis_row_count = 0;

  if (mu_begin_window_ex(ctx, "Writer",
        mu_rect(0, 0, win_w(), win_h()),
        MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOCLOSE | MU_OPT_NOSCROLL)) {
    mu_Container *win = mu_get_current_container(ctx);
    win->rect = mu_rect(0, 0, win_w(), win_h());

    int lh = line_height();
    g_content_y = TOP_PADDING;
    int status_bar_h = r_get_text_height() + 16;
    g_content_h = win_h() - TOP_PADDING - status_bar_h;

    /* content area */
    int total_vis = total_visual_lines();
    float max_scroll = (total_vis * lh) - g_content_h;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;

    int first_vis = (int)(scroll_y / lh);
    float y_offset = scroll_y - (first_vis * lh);

    /* how many visual lines fit on screen */
    int vis_on_screen = g_content_h / lh + 4;

    /* clip to content area */
    mu_push_clip_rect(ctx, mu_rect(0, g_content_y, win_w(), g_content_h));

    /* render visible visual lines */
    for (int vi = 0; vi < vis_on_screen; vi++) {
      int vis_idx = first_vis + vi;
      if (vis_idx >= total_vis) break;

      int py = g_content_y + (vi * lh) - (int)y_offset;
      if (py + lh < g_content_y || py > g_content_y + g_content_h) continue;

      /* map visual line to logical line + wrap row */
      int wrap_off;
      int ln = visual_to_logical(vis_idx, &wrap_off);
      Line *l = &lines[ln];

      int starts[256];
      int nrows = get_wrap_breaks(l, starts, 256);
      int row_start = starts[wrap_off];
      int row_end = (wrap_off + 1 < nrows) ? starts[wrap_off + 1] : l->len;

      /* draw mark region highlight */
      if (mark_active && row_end > row_start) {
        int sl, sc, el, ec;
        region_ordered(&sl, &sc, &el, &ec);
        if (ln >= sl && ln <= el) {
          int hl_start = (ln == sl) ? sc : 0;
          int hl_end   = (ln == el) ? ec : lines[ln].len;
          /* clamp to this visual row */
          if (hl_start < row_end && hl_end > row_start) {
            int hs = hl_start < row_start ? row_start : hl_start;
            int he = hl_end > row_end ? row_end : hl_end;
            if (he > hs) {
              int hx = page_margin() + r_get_text_width(l->text + row_start, hs - row_start);
              int hw = r_get_text_width(l->text + hs, he - hs);
              int font_h = r_get_text_height();
              mu_draw_rect(ctx, mu_rect(hx, py, hw, font_h),
                           mu_color(60, 100, 160, 180));
            }
          }
        }
      }

      /* store row info for post-render markdown drawing */
      if (g_vis_row_count < MAX_VIS_ROWS) {
        g_vis_rows[g_vis_row_count].ln = ln;
        g_vis_rows[g_vis_row_count].row_start = row_start;
        g_vis_rows[g_vis_row_count].row_end = row_end;
        g_vis_rows[g_vis_row_count].py = py;
        g_vis_rows[g_vis_row_count].heading = is_heading(l);
        g_vis_row_count++;
      }

      /* draw search highlights on this visual row */
      if (search_active && search_len > 0 && row_end > row_start && (row_end - row_start) >= search_len) {
        for (int sc = row_start; sc <= row_end - search_len; sc++) {
          if (strncasecmp(l->text + sc, search_buf, search_len) == 0) {
            int hx = page_margin() + r_get_text_width(l->text + row_start, sc - row_start);
            int hw = r_get_text_width(l->text + sc, search_len);
            int font_h = r_get_text_height();
            /* current match gets brighter highlight */
            if (ln == search_match_line && sc == search_match_col) {
              mu_draw_rect(ctx, mu_rect(hx, py, hw, font_h),
                           mu_color(200, 150, 0, 180));
            } else {
              mu_draw_rect(ctx, mu_rect(hx, py, hw, font_h),
                           mu_color(120, 90, 0, 120));
            }
          }
        }
      }

      /* cursor drawing moved to post-render pass (uses g_cursor_x) */
    }

    mu_pop_clip_rect(ctx);

    /* scrollbar */
    if (max_scroll > 0) {
      int sb_x = win_w() - 8;
      int sb_w = 6;
      int sb_h = g_content_h;
      float thumb_ratio = (float)g_content_h / (total_vis * lh);
      int thumb_h = (int)(sb_h * thumb_ratio);
      if (thumb_h < 20) thumb_h = 20;
      int thumb_y = g_content_y + (int)((scroll_y / max_scroll) * (sb_h - thumb_h));

      int mx = ctx->mouse_pos.x, my = ctx->mouse_pos.y;
      int mouse_in_track = (mx >= sb_x - 4 && mx < sb_x + sb_w + 4 && my >= g_content_y && my < g_content_y + sb_h);

      if (scrollbar_dragging) {
        if (ctx->mouse_down & MU_MOUSE_LEFT) {
          float ratio = (my - drag_offset - g_content_y) / (float)(sb_h - thumb_h);
          if (ratio < 0) ratio = 0;
          if (ratio > 1) ratio = 1;
          scroll_y = ratio * max_scroll;
        } else {
          scrollbar_dragging = 0;
        }
      } else if (mouse_in_track && (ctx->mouse_pressed & MU_MOUSE_LEFT)) {
        if (my >= thumb_y && my < thumb_y + thumb_h) {
          scrollbar_dragging = 1;
          drag_offset = my - thumb_y;
        } else {
          float ratio = (my - g_content_y - thumb_h / 2.0f) / (float)(sb_h - thumb_h);
          if (ratio < 0) ratio = 0;
          if (ratio > 1) ratio = 1;
          scroll_y = ratio * max_scroll;
          scrollbar_dragging = 1;
          drag_offset = thumb_h / 2.0f;
        }
      }

      mu_Color thumb_color = scrollbar_dragging ? mu_color(140, 140, 140, 255) :
                             mouse_in_track     ? mu_color(120, 120, 120, 255) :
                                                  mu_color(80, 80, 80, 255);
      mu_draw_rect(ctx, mu_rect(sb_x, thumb_y, sb_w, thumb_h), thumb_color);
    }

    mu_end_window(ctx);
  }

  mu_end(ctx);
}


/* ---- key binding table and command functions ---- */

typedef struct {
  int mod;     /* required modifier: KMOD_CTRL, KMOD_ALT, KMOD_GUI, 0 for none */
  int sym;     /* SDL key symbol */
  void (*action)(void);  /* command function */
} KeyBinding;

static void cmd_beginning_of_line(void) {
  cursor_col = 0; cursor_target_col = 0; ensure_cursor_visible();
}
static void cmd_end_of_line(void) {
  cursor_col = lines[cursor_line].len; cursor_target_col = cursor_col; ensure_cursor_visible();
}
static void cmd_forward_char(void) {
  if (cursor_col < lines[cursor_line].len) { cursor_col++; }
  else if (cursor_line < line_count - 1) { cursor_line++; cursor_col = 0; }
  cursor_target_col = cursor_col; ensure_cursor_visible();
}
static void cmd_backward_char(void) {
  if (cursor_col > 0) { cursor_col--; }
  else if (cursor_line > 0) { cursor_line--; cursor_col = lines[cursor_line].len; }
  cursor_target_col = cursor_col; ensure_cursor_visible();
}
static void cmd_next_line(void) {
  if (cursor_line < line_count - 1) { cursor_line++; cursor_col = cursor_target_col; cursor_clamp(); }
  ensure_cursor_visible();
}
static void cmd_previous_line(void) {
  if (cursor_line > 0) { cursor_line--; cursor_col = cursor_target_col; cursor_clamp(); }
  ensure_cursor_visible();
}
static void cmd_kill_line(void) {
  mark_clear(); emacs_kill_line(); ensure_cursor_visible();
}
static void cmd_yank(void) {
  mark_clear(); emacs_yank(); status_set("Yanked"); ensure_cursor_visible();
}
static void cmd_kill_region(void) {
  emacs_kill_region(); status_set("Region killed"); ensure_cursor_visible();
}
static void cmd_undo(void) {
  mark_clear(); undo_perform(&g_ed); status_set("Undo"); ensure_cursor_visible();
}
static void cmd_set_mark(void) {
  mark_set(); status_set("Mark set");
}
static void cmd_keyboard_quit(void) {
  mark_clear(); status_set("Quit");
}
static void cmd_forward_word_alt(void) {
  emacs_forward_word(); ensure_cursor_visible();
}
static void cmd_backward_word_alt(void) {
  emacs_backward_word(); ensure_cursor_visible();
}
static void cmd_end_of_buffer_alt(void) {
  cursor_line = line_count - 1; cursor_col = lines[cursor_line].len;
  cursor_target_col = cursor_col; ensure_cursor_visible(); suppress_next_text = 1;
}
static void cmd_beginning_of_buffer_alt(void) {
  cursor_line = 0; cursor_col = 0; cursor_target_col = 0;
  ensure_cursor_visible(); suppress_next_text = 1;
}
static void cmd_font_increase(void) {
  font_size += 2.0f; if (font_size > 72.0f) font_size = 72.0f;
  r_set_font_size(font_size); invalidate_all_wraps(); ensure_cursor_visible();
}
static void cmd_font_decrease(void) {
  font_size -= 2.0f; if (font_size < 8.0f) font_size = 8.0f;
  r_set_font_size(font_size); invalidate_all_wraps(); ensure_cursor_visible();
}
static void cmd_backspace(void) {
  mark_clear(); editor_backspace(); ensure_cursor_visible();
}
static void cmd_delete(void) {
  mark_clear(); editor_delete();
}
static void cmd_enter(void) {
  mark_clear(); editor_enter(); ensure_cursor_visible();
}

/* Alt+Shift+. and Alt+Shift+, need KMOD_ALT but the sym check uses SDLK_PERIOD/SDLK_COMMA
   plus shift — handled specially via check_binding which checks extra shift for those entries */

static const KeyBinding normal_bindings[] = {
  { KMOD_CTRL, SDLK_a,      cmd_beginning_of_line },
  { KMOD_CTRL, SDLK_e,      cmd_end_of_line },
  { KMOD_CTRL, SDLK_f,      cmd_forward_char },
  { KMOD_CTRL, SDLK_b,      cmd_backward_char },
  { KMOD_CTRL, SDLK_n,      cmd_next_line },
  { KMOD_CTRL, SDLK_p,      cmd_previous_line },
  { KMOD_CTRL, SDLK_k,      cmd_kill_line },
  { KMOD_CTRL, SDLK_y,      cmd_yank },
  { KMOD_CTRL, SDLK_w,      cmd_kill_region },
  { KMOD_CTRL, SDLK_SLASH,  cmd_undo },
  { KMOD_CTRL, SDLK_SPACE,  cmd_set_mark },
  { KMOD_CTRL, SDLK_g,      cmd_keyboard_quit },
  { KMOD_ALT,  SDLK_f,      cmd_forward_word_alt },
  { KMOD_ALT,  SDLK_b,      cmd_backward_word_alt },
  { KMOD_GUI,  SDLK_EQUALS, cmd_font_increase },
  { KMOD_GUI,  SDLK_MINUS,  cmd_font_decrease },
  { 0, SDLK_BACKSPACE,      cmd_backspace },
  { 0, SDLK_DELETE,         cmd_delete },
  { 0, SDLK_RETURN,         cmd_enter },
  { 0, 0, NULL }  /* sentinel */
};

static int check_binding(const KeyBinding *b, int kmod, int sym) {
  if (b->sym != sym) return 0;
  if (b->mod == 0) {
    /* no modifier required — but reject if ctrl/alt/gui are held */
    if (kmod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) return 0;
    return 1;
  }
  /* check that the required modifier is active */
  if (!(kmod & b->mod)) return 0;
  return 1;
}

/* ---- modal key handlers ---- */

static int handle_minibuf_key(int sym, int ctrl) {
  if (sym == SDLK_RETURN) {
    minibuf_active = 0;
    minibuf_completing = 0;
    minibuf_suggest[0] = '\0';
    if (minibuf_callback) minibuf_callback(minibuf_text);
    minibuf_callback = NULL;
    return 1;
  }
  if (sym == SDLK_ESCAPE || (ctrl && sym == SDLK_g)) {
    minibuf_active = 0;
    minibuf_completing = 0;
    minibuf_suggest[0] = '\0';
    minibuf_callback = NULL;
    status_set("Quit");
    return 1;
  }
  if (sym == SDLK_TAB) {
    /* accept the ghost completion */
    if (minibuf_completing && minibuf_suggest[0] &&
        (int)strlen(minibuf_suggest) > minibuf_len) {
      snprintf(minibuf_text, sizeof(minibuf_text), "%s", minibuf_suggest);
      minibuf_len = (int)strlen(minibuf_text);
      minibuf_refresh_completion();
    }
    suppress_next_text = 1;   /* swallow any tab character event */
    return 1;
  }
  if (sym == SDLK_BACKSPACE) {
    if (minibuf_len > 0) {
      minibuf_len--;
      minibuf_text[minibuf_len] = '\0';
    }
    minibuf_refresh_completion();
    return 1;
  }
  return 0;
}

static int handle_search_key(int sym, int ctrl) {
  if (sym == SDLK_ESCAPE || sym == SDLK_RETURN) {
    search_active = 0;
    return 1;
  }
  if (ctrl && sym == SDLK_s) {
    search_direction = 1;
    if (search_match_line >= 0) search_find_next(search_match_line, search_match_col);
    else search_find_first();
    return 1;
  }
  if (ctrl && (sym == SDLK_r || sym == SDLK_b)) {
    search_direction = -1;
    if (search_match_line >= 0) search_find_prev(search_match_line, search_match_col);
    else search_find_first();
    return 1;
  }
  if (ctrl && sym == SDLK_f) {
    search_direction = 1;
    if (search_match_line >= 0) search_find_next(search_match_line, search_match_col);
    else search_find_first();
    return 1;
  }
  if (ctrl && sym == SDLK_g) {
    search_active = 0;
    search_match_line = -1;
    return 1;
  }
  if (sym == SDLK_BACKSPACE) {
    if (search_len > 0) {
      search_len--;
      search_buf[search_len] = '\0';
      if (search_len > 0) search_find_current_dir();
      else search_match_line = -1;
    }
    return 1;
  }
  return 0;
}

static int handle_esc_prefix_key(int sym, int shift) {
  esc_prefix = 0;
  SDL_StartTextInput();
  if (shift && sym == SDLK_PERIOD) {
    /* M-> : end of buffer, set mark first */
    mark_set();
    cursor_line = line_count - 1;
    cursor_col = lines[cursor_line].len;
    cursor_target_col = cursor_col;
    ensure_cursor_visible();
    status_set("Mark set");
    suppress_next_text = 1;
    return 1;
  }
  if (shift && sym == SDLK_COMMA) {
    /* M-< : beginning of buffer, set mark first */
    mark_set();
    cursor_line = 0; cursor_col = 0; cursor_target_col = 0;
    ensure_cursor_visible();
    status_set("Mark set");
    suppress_next_text = 1;
    return 1;
  }
  if (sym == SDLK_f) {
    emacs_forward_word(); ensure_cursor_visible(); return 1;
  }
  if (sym == SDLK_b) {
    emacs_backward_word(); ensure_cursor_visible(); return 1;
  }
  return 1; /* consume even if unrecognized — prefix is cleared */
}

static int handle_cx_prefix_key(int sym, int ctrl) {
  ctrl_x_prefix = 0;
  if (ctrl && sym == SDLK_s) {
    /* C-x C-s: save (honest status, sandbox-resolved path) */
    if (g_filepath[0]) {
      save_to_path(g_filepath);
    } else {
      minibuf_active = 1;
      minibuf_completing = 0;
      minibuf_suggest[0] = '\0';
      snprintf(minibuf_prompt, sizeof(minibuf_prompt), "Write file (Documents): ");
      minibuf_text[0] = '\0';
      minibuf_len = 0;
      minibuf_callback = save_to_path;
    }
    return 1;
  }
  if (ctrl && sym == SDLK_c) {
    exit(EXIT_SUCCESS);
    return 1;
  }
  if (ctrl && sym == SDLK_f) {
    minibuf_active = 1;
    minibuf_completing = 1;       /* enable ghost filename completion */
    minibuf_suggest[0] = '\0';
    snprintf(minibuf_prompt, sizeof(minibuf_prompt), "Find file (Documents): ");
    minibuf_text[0] = '\0';
    minibuf_len = 0;
    minibuf_callback = open_or_create_file;
    return 1;
  }
  return 1; /* consume even if unrecognized — prefix is cleared */
}

/* ---- SDL boilerplate ---- */

static const char button_map[256] = {
  [ SDL_BUTTON_LEFT   & 0xff ] =  MU_MOUSE_LEFT,
  [ SDL_BUTTON_RIGHT  & 0xff ] =  MU_MOUSE_RIGHT,
  [ SDL_BUTTON_MIDDLE & 0xff ] =  MU_MOUSE_MIDDLE,
};

static const char key_map[256] = {
  [ SDLK_LSHIFT       & 0xff ] = MU_KEY_SHIFT,
  [ SDLK_RSHIFT       & 0xff ] = MU_KEY_SHIFT,
  [ SDLK_LCTRL        & 0xff ] = MU_KEY_CTRL,
  [ SDLK_RCTRL        & 0xff ] = MU_KEY_CTRL,
  [ SDLK_LALT         & 0xff ] = MU_KEY_ALT,
  [ SDLK_RALT         & 0xff ] = MU_KEY_ALT,
  [ SDLK_RETURN       & 0xff ] = MU_KEY_RETURN,
  [ SDLK_BACKSPACE    & 0xff ] = MU_KEY_BACKSPACE,
};

static int text_width(mu_Font font, const char *text, int len) {
  if (len == -1) { len = strlen(text); }
  return r_get_text_width(text, len);
}

static int text_height(mu_Font font) {
  return r_get_text_height();
}


/* ---- render pass (callable from main loop and resize watcher) ---- */

static void do_render(void) {
  mu_Context *ctx = g_ctx;
  if (!ctx) return;

  process_frame(ctx);

  r_clear(mu_color(30, 30, 32, 255));
  mu_Command *cmd = NULL;
  while (mu_next_command(ctx, &cmd)) {
    switch (cmd->type) {
      case MU_COMMAND_TEXT: r_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color); break;
      case MU_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
      case MU_COMMAND_ICON: r_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color); break;
      case MU_COMMAND_CLIP: r_set_clip_rect(cmd->clip.rect); break;
    }
  }

  /* draw markdown-formatted text (direct rendering, not through microui) */
  r_set_font_size(font_size);
  r_set_font_style(FONT_REGULAR);
  r_set_clip_rect(mu_rect(0, g_content_y, win_w(), g_content_h));
  mu_Color text_color = g_ctx->style->colors[MU_COLOR_TEXT];
  g_cursor_x = -1;
  for (int i = 0; i < g_vis_row_count; i++) {
    VisRow *vr = &g_vis_rows[i];
    int indent = list_indent(&lines[vr->ln]);
    /* track cursor if it's on this row */
    int track = -1;
    if (vr->ln == cursor_line && cursor_col >= vr->row_start &&
        (cursor_col < vr->row_end || (i + 1 >= g_vis_row_count || g_vis_rows[i+1].ln != vr->ln))) {
      track = cursor_col;
    }
    draw_md_text(lines[vr->ln].text, vr->row_start, vr->row_end,
                 page_margin() + indent, vr->py, text_color, vr->heading, track);
    r_set_font_style(FONT_REGULAR);
  }

  /* draw cursor (post-render, uses markdown-aware x position) */
  if (g_cursor_x >= 0) {
    int font_h = r_get_text_height();
    /* find the py for the cursor row */
    for (int i = 0; i < g_vis_row_count; i++) {
      VisRow *vr = &g_vis_rows[i];
      if (vr->ln == cursor_line && cursor_col >= vr->row_start &&
          (cursor_col < vr->row_end || (i + 1 >= g_vis_row_count || g_vis_rows[i+1].ln != vr->ln))) {
        r_draw_rect(mu_rect(g_cursor_x, vr->py, 3, font_h),
                    mu_color(90, 200, 250, 255));
        break;
      }
    }
  }

  /* emacs-style status bar (monospace) */
  r_set_font_size(font_size);
  r_set_font_style(FONT_MONO);
  int bar_h = r_get_text_height() + 16;
  int bar_y = win_h() - bar_h;
  r_set_clip_rect(mu_rect(0, 0, win_w(), win_h()));

  /* background */
  r_draw_rect(mu_rect(0, bar_y, win_w(), bar_h), mu_color(40, 40, 42, 255));
  /* top border */
  r_draw_rect(mu_rect(0, bar_y, win_w(), 1), mu_color(55, 55, 57, 255));

  /* left: minibuffer input, isearch, or status message */
  if (minibuf_active) {
    r_draw_text(minibuf_prompt, mu_vec2(10, bar_y + 5), mu_color(170, 170, 170, 255));
    int lw = r_get_text_width(minibuf_prompt, strlen(minibuf_prompt));
    r_draw_text(minibuf_text, mu_vec2(10 + lw, bar_y + 5), mu_color(204, 200, 195, 255));
    int cx = 10 + lw + r_get_text_width(minibuf_text, minibuf_len);
    int fh = r_get_text_height();
    /* ghost completion of an existing filename (Tab to accept) */
    if (minibuf_suggest[0] && (int)strlen(minibuf_suggest) > minibuf_len) {
      const char *ghost = minibuf_suggest + minibuf_len;
      r_draw_text(ghost, mu_vec2(cx, bar_y + 5), mu_color(110, 110, 112, 255));
    }
    r_draw_rect(mu_rect(cx, bar_y + 4, 2, fh), mu_color(90, 200, 250, 255));
  } else if (search_active) {
    const char *label = (search_direction == 1) ? "I-search: " : "I-search backward: ";
    r_draw_text(label, mu_vec2(10, bar_y + 5), mu_color(170, 170, 170, 255));
    int lw = r_get_text_width(label, strlen(label));
    r_draw_text(search_buf, mu_vec2(10 + lw, bar_y + 5), mu_color(204, 200, 195, 255));
    int cx = 10 + lw + r_get_text_width(search_buf, search_len);
    int fh = r_get_text_height();
    r_draw_rect(mu_rect(cx, bar_y + 4, 2, fh), mu_color(90, 200, 250, 255));
  } else {
    const char *status = status_get();
    if (status[0]) {
      r_draw_text(status, mu_vec2(10, bar_y + 5), mu_color(170, 170, 170, 255));
    }
  }

  /* right: line, col, font size */
  char info[64];
  snprintf(info, sizeof(info), "(%d,%d)  %.0fpt", cursor_line + 1, cursor_col + 1, font_size);
  int info_w = r_get_text_width(info, strlen(info));
  r_draw_text(info, mu_vec2(win_w() - info_w - 10, bar_y + 5), mu_color(120, 120, 120, 255));
  r_set_font_size(font_size);

  r_present();
}

static int resize_event_watcher(void *data, SDL_Event *event) {
  (void)data;
  if (event->type == SDL_WINDOWEVENT &&
      (event->window.event == SDL_WINDOWEVENT_RESIZED ||
       event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
    /* just update GL viewport so the clear color fills correctly, but don't reflow */
    r_handle_resize();
    r_clear(mu_color(30, 30, 32, 255));
    r_present();
  }
  return 0;
}

/* Called from Swift (via the bridging header) before editor_main to point
   file I/O at the app's sandbox-container Documents directory. */
void editor_set_documents_dir(const char *path) {
  buf_set_documents_dir(path);
}

int editor_main(int argc, char **argv) {
  if (argc >= 2 && buf_load_file(&g_ed, argv[1]) == 0) {
    snprintf(g_filepath, sizeof(g_filepath), "%s", argv[1]);
    g_filename = argv[1];
    const char *slash = strrchr(argv[1], '/');
    if (slash) g_filename = slash + 1;
    printf("loaded %d lines\n", line_count);
  } else {
    buf_init_empty(&g_ed);
  }

  SDL_Init(SDL_INIT_EVERYTHING);
  font_size = 26.0f;
  search_direction = 1;
  search_match_line = -1;
  search_match_col = -1;
  g_cursor_x = -1;
  if (!g_filename) g_filename = "*scratch*";
  r_init();
  r_set_font_size(font_size);
  r_set_title(g_filename);
  macos_style_window(r_get_window());

  mu_Context *ctx = malloc(sizeof(mu_Context));
  mu_init(ctx);
  ctx->text_width = text_width;
  ctx->text_height = text_height;
  ctx->style->colors[MU_COLOR_TEXT] = mu_color(204, 200, 195, 255);

  /* store ctx globally so the resize watcher can trigger a re-render */
  g_ctx = ctx;
  SDL_AddEventWatch(resize_event_watcher, NULL);

  for (;;) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: exit(EXIT_SUCCESS); break;
        case SDL_WINDOWEVENT:
          if (e.window.event == SDL_WINDOWEVENT_RESIZED ||
              e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            r_handle_resize();
            invalidate_all_wraps();
          }
          break;
        case SDL_MOUSEMOTION: mu_input_mousemove(ctx, e.motion.x, e.motion.y); break;
        case SDL_MOUSEWHEEL: scroll_y -= e.wheel.y * line_height() * 3; break;

        case SDL_TEXTINPUT:
          if (suppress_next_text) { suppress_next_text = 0; break; }
          if (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI | KMOD_ALT)) break;
          if (minibuf_active) {
            int tlen = strlen(e.text.text);
            if (minibuf_len + tlen < (int)sizeof(minibuf_text) - 1) {
              memcpy(minibuf_text + minibuf_len, e.text.text, tlen);
              minibuf_len += tlen;
              minibuf_text[minibuf_len] = '\0';
            }
            minibuf_refresh_completion();
          } else if (search_active) {
            int tlen = strlen(e.text.text);
            if (search_len + tlen < (int)sizeof(search_buf) - 1) {
              memcpy(search_buf + search_len, e.text.text, tlen);
              search_len += tlen;
              search_buf[search_len] = '\0';
              search_find_current_dir();
            }
          } else {
            editor_insert_char(e.text.text);
            ensure_cursor_visible();
          }
          break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
          int b = button_map[e.button.button & 0xff];
          if (b && e.type == SDL_MOUSEBUTTONDOWN) {
            mu_input_mousedown(ctx, e.button.x, e.button.y, b);
            if (b == MU_MOUSE_LEFT && e.button.y > TOP_PADDING && e.button.x < win_w() - 12) {
              click_to_cursor(e.button.x, e.button.y);
            }
          }
          if (b && e.type == SDL_MOUSEBUTTONUP) { mu_input_mouseup(ctx, e.button.x, e.button.y, b); }
          break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          /* suppress macOS system beep for ctrl combos */
          if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & KMOD_CTRL)) {
            SDL_StopTextInput();
          }
          if (e.type == SDL_KEYUP && !(SDL_GetModState() & KMOD_CTRL)) {
            SDL_StartTextInput();
          }

          int c = key_map[e.key.keysym.sym & 0xff];
          if (c && e.type == SDL_KEYDOWN) { mu_input_keydown(ctx, c); }
          if (c && e.type ==   SDL_KEYUP) { mu_input_keyup(ctx, c);   }

          if (e.type == SDL_KEYDOWN) {
            int ctrl = !!(e.key.keysym.mod & KMOD_CTRL);
            int sym = e.key.keysym.sym;

            /* 1. Modal handlers (consume and break) */
            if (minibuf_active && handle_minibuf_key(sym, ctrl)) break;
            if (search_active && handle_search_key(sym, ctrl)) break;
            if (ctrl_x_prefix && handle_cx_prefix_key(sym, ctrl)) break;
            if (esc_prefix && handle_esc_prefix_key(sym, !!(e.key.keysym.mod & KMOD_SHIFT))) break;

            /* 2. Prefix starters */
            if (ctrl && sym == SDLK_x) { ctrl_x_prefix = 1; break; }
            if (sym == SDLK_ESCAPE && !search_active) {
              if (mark_active) { mark_clear(); status_set("Quit"); }
              else { esc_prefix = 1; SDL_StopTextInput(); }
              break;
            }

            /* 3. C-s / C-r isearch start/continue */
            if (ctrl && sym == SDLK_s) {
              search_direction = 1;
              if (!search_active) {
                search_active = 1;
                search_buf[0] = '\0';
                search_len = 0;
                search_match_line = -1;
              } else {
                search_find_next(search_match_line >= 0 ? search_match_line : cursor_line,
                                 search_match_col >= 0 ? search_match_col : cursor_col - 1);
              }
              break;
            }
            if (ctrl && sym == SDLK_r) {
              search_direction = -1;
              if (!search_active) {
                search_active = 1;
                search_buf[0] = '\0';
                search_len = 0;
                search_match_line = -1;
              } else {
                search_find_prev(search_match_line >= 0 ? search_match_line : cursor_line,
                                 search_match_col >= 0 ? search_match_col : cursor_col + 1);
              }
              break;
            }

            /* any non-C-k key clears last_kill_was_k */
            if (!(ctrl && sym == SDLK_k)) last_kill_was_k = 0;

            /* 4. Command table lookup */
            {
              int matched = 0;
              for (int i = 0; normal_bindings[i].action; i++) {
                if (check_binding(&normal_bindings[i], e.key.keysym.mod, sym)) {
                  normal_bindings[i].action();
                  matched = 1;
                  break;
                }
              }
              if (matched) break;
            }

            /* 5. Alt+Shift+. / Alt+Shift+, (need shift check, not in table) */
            {
              int alt = !!(e.key.keysym.mod & KMOD_ALT);
              int shift = !!(e.key.keysym.mod & KMOD_SHIFT);
              if (alt && shift && sym == SDLK_PERIOD) {
                cmd_end_of_buffer_alt();
                break;
              }
              if (alt && shift && sym == SDLK_COMMA) {
                cmd_beginning_of_buffer_alt();
                break;
              }
            }

            /* 6. Arrow keys (need shift detection for selection) */
            if (sym == SDLK_LEFT) {
              int shift = !!(e.key.keysym.mod & KMOD_SHIFT);
              int alt = !!(e.key.keysym.mod & KMOD_ALT);
              if (shift && !mark_active) mark_set();
              if (ctrl || alt) {
                emacs_backward_word();
              } else {
                if (cursor_col > 0) cursor_col--;
                else if (cursor_line > 0) { cursor_line--; cursor_col = lines[cursor_line].len; }
                cursor_target_col = cursor_col;
              }
              ensure_cursor_visible();
              break;
            }
            if (sym == SDLK_RIGHT) {
              int shift = !!(e.key.keysym.mod & KMOD_SHIFT);
              int alt = !!(e.key.keysym.mod & KMOD_ALT);
              if (shift && !mark_active) mark_set();
              if (ctrl || alt) {
                emacs_forward_word();
              } else {
                if (cursor_col < lines[cursor_line].len) cursor_col++;
                else if (cursor_line < line_count - 1) { cursor_line++; cursor_col = 0; }
                cursor_target_col = cursor_col;
              }
              ensure_cursor_visible();
              break;
            }
            if (sym == SDLK_UP) {
              int shift = !!(e.key.keysym.mod & KMOD_SHIFT);
              if (shift && !mark_active) mark_set();
              if (cursor_line > 0) { cursor_line--; cursor_col = cursor_target_col; cursor_clamp(); }
              ensure_cursor_visible();
              break;
            }
            if (sym == SDLK_DOWN) {
              int shift = !!(e.key.keysym.mod & KMOD_SHIFT);
              if (shift && !mark_active) mark_set();
              if (cursor_line < line_count - 1) { cursor_line++; cursor_col = cursor_target_col; cursor_clamp(); }
              ensure_cursor_visible();
              break;
            }
          }
          break;
        }
      }
    }

    do_render();
  }

  return 0;
}
