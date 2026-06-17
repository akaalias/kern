/* md_render.c — Markdown-aware text rendering */

#include <string.h>
#include "md_render.h"
#include "renderer.h"

int md_list_indent(Line *l) {
  if (l->len >= 2 && l->text[0] == '-' && l->text[1] == ' ') {
    return r_get_text_width("    ", 4);
  }
  int i = 0;
  while (i < l->len && l->text[i] >= '0' && l->text[i] <= '9') i++;
  if (i > 0 && i + 1 < l->len && l->text[i] == '.' && l->text[i+1] == ' ') {
    return r_get_text_width("    ", 4);
  }
  return 0;
}

int md_is_heading(Line *l) {
  if (l->len < 2) return 0;
  int i = 0;
  while (i < l->len && l->text[i] == '#') i++;
  return (i > 0 && i < l->len && l->text[i] == ' ');
}

float md_draw_text(const char *text, int start, int end,
                   float x, float y, mu_Color base_color, int heading,
                   int track_cursor_col, int *out_cursor_x) {
  int saved_style = r_get_font_style();
  int i = start;

  #define TRACK_CURSOR_SPAN(from, to, span_x) do { \
    if (track_cursor_col >= (from) && track_cursor_col < (to)) { \
      *out_cursor_x = (int)(span_x) + r_get_text_width(text + (from), track_cursor_col - (from)); \
    } \
  } while(0)

  if (heading) r_set_font_style(FONT_BOLD);

  while (i < end) {
    if (track_cursor_col >= 0 && i == track_cursor_col) *out_cursor_x = (int)x;
    /* check for **bold** */
    if (i + 1 < end && text[i] == '*' && text[i+1] == '*') {
      int close = -1;
      for (int j = i + 2; j + 1 < end; j++) {
        if (text[j] == '*' && text[j+1] == '*') { close = j; break; }
      }
      if (close > 0) {
        char mk[3] = "**";
        TRACK_CURSOR_SPAN(i, i + 2, x);
        r_draw_text(mk, mu_vec2((int)x, (int)y), mu_color(80, 80, 80, 255));
        x += r_get_text_width(mk, 2);
        r_set_font_style(FONT_BOLD);
        char buf[512];
        int blen = close - (i + 2);
        if (blen > (int)sizeof(buf) - 1) blen = (int)sizeof(buf) - 1;
        memcpy(buf, text + i + 2, blen); buf[blen] = '\0';
        TRACK_CURSOR_SPAN(i + 2, close, x);
        r_draw_text(buf, mu_vec2((int)x, (int)y), base_color);
        x += r_get_text_width(buf, blen);
        if (heading) r_set_font_style(FONT_BOLD); else r_set_font_style(FONT_REGULAR);
        TRACK_CURSOR_SPAN(close, close + 2, x);
        r_draw_text(mk, mu_vec2((int)x, (int)y), mu_color(80, 80, 80, 255));
        x += r_get_text_width(mk, 2);
        i = close + 2;
        continue;
      }
    }
    /* check for _italic_ */
    if (text[i] == '_' && i + 1 < end && text[i+1] != ' ') {
      int close = -1;
      for (int j = i + 1; j < end; j++) {
        if (text[j] == '_') { close = j; break; }
      }
      if (close > i + 1) {
        TRACK_CURSOR_SPAN(i, i + 1, x);
        r_draw_text("_", mu_vec2((int)x, (int)y), mu_color(80, 80, 80, 255));
        x += r_get_text_width("_", 1);
        r_set_font_style(FONT_ITALIC);
        char buf[512];
        int blen = close - (i + 1);
        if (blen > (int)sizeof(buf) - 1) blen = (int)sizeof(buf) - 1;
        memcpy(buf, text + i + 1, blen); buf[blen] = '\0';
        TRACK_CURSOR_SPAN(i + 1, close, x);
        r_draw_text(buf, mu_vec2((int)x, (int)y), base_color);
        x += r_get_text_width(buf, blen);
        if (heading) r_set_font_style(FONT_BOLD); else r_set_font_style(FONT_REGULAR);
        TRACK_CURSOR_SPAN(close, close + 1, x);
        r_draw_text("_", mu_vec2((int)x, (int)y), mu_color(80, 80, 80, 255));
        x += r_get_text_width("_", 1);
        i = close + 1;
        continue;
      }
    }
    /* check for `inline code` */
    if (text[i] == '`') {
      int close = -1;
      for (int j = i + 1; j < end; j++) {
        if (text[j] == '`') { close = j; break; }
      }
      if (close > i) {
        TRACK_CURSOR_SPAN(i, i + 1, x);
        r_draw_text("`", mu_vec2((int)x, (int)y), mu_color(80, 80, 80, 255));
        x += r_get_text_width("`", 1);
        r_set_font_style(FONT_MONO);
        char buf[512];
        int blen = close - (i + 1);
        if (blen > (int)sizeof(buf) - 1) blen = (int)sizeof(buf) - 1;
        memcpy(buf, text + i + 1, blen); buf[blen] = '\0';
        TRACK_CURSOR_SPAN(i + 1, close, x);
        r_draw_text(buf, mu_vec2((int)x, (int)y), mu_color(180, 140, 100, 255));
        x += r_get_text_width(buf, blen);
        if (heading) r_set_font_style(FONT_BOLD); else r_set_font_style(FONT_REGULAR);
        TRACK_CURSOR_SPAN(close, close + 1, x);
        r_draw_text("`", mu_vec2((int)x, (int)y), mu_color(80, 80, 80, 255));
        x += r_get_text_width("`", 1);
        i = close + 1;
        continue;
      }
    }
    /* check for [text](url) */
    if (text[i] == '[') {
      int bracket_close = -1;
      for (int j = i + 1; j < end; j++) {
        if (text[j] == ']') { bracket_close = j; break; }
      }
      if (bracket_close > 0 && bracket_close + 1 < end && text[bracket_close + 1] == '(') {
        int paren_close = -1;
        for (int j = bracket_close + 2; j < end; j++) {
          if (text[j] == ')') { paren_close = j; break; }
        }
        if (paren_close > 0) {
          char buf[512];
          int blen = paren_close + 1 - i;
          if (blen > (int)sizeof(buf) - 1) blen = (int)sizeof(buf) - 1;
          memcpy(buf, text + i, blen); buf[blen] = '\0';
          int link_w = r_get_text_width(buf, blen);
          int font_h = r_get_text_height();
          TRACK_CURSOR_SPAN(i, paren_close + 1, x);
          r_draw_rect(mu_rect((int)x, (int)y, link_w, font_h), mu_color(80, 50, 120, 255));
          r_draw_text(buf, mu_vec2((int)x, (int)y), mu_color(180, 160, 220, 255));
          x += link_w;
          i = paren_close + 1;
          continue;
        }
      }
      /* check for [[wikilink]] */
      if (i + 1 < end && text[i+1] == '[') {
        int close = -1;
        for (int j = i + 2; j + 1 < end; j++) {
          if (text[j] == ']' && text[j+1] == ']') { close = j; break; }
        }
        if (close > 0) {
          char buf[512];
          int blen = close + 2 - i;
          if (blen > (int)sizeof(buf) - 1) blen = (int)sizeof(buf) - 1;
          memcpy(buf, text + i, blen); buf[blen] = '\0';
          int link_w = r_get_text_width(buf, blen);
          int font_h = r_get_text_height();
          TRACK_CURSOR_SPAN(i, close + 2, x);
          r_draw_rect(mu_rect((int)x, (int)y, link_w, font_h), mu_color(80, 50, 120, 255));
          r_draw_text(buf, mu_vec2((int)x, (int)y), mu_color(180, 160, 220, 255));
          x += link_w;
          i = close + 2;
          continue;
        }
      }
    }
    /* default: draw one character at a time */
    {
      char ch[2] = { text[i], '\0' };
      r_draw_text(ch, mu_vec2((int)x, (int)y), base_color);
      x += r_get_text_width(ch, 1);
      i++;
    }
  }

  if (track_cursor_col >= 0 && track_cursor_col >= end) *out_cursor_x = (int)x;

  #undef TRACK_CURSOR_SPAN
  r_set_font_style(saved_style);
  return x;
}
