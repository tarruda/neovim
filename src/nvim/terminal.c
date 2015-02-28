// Terminals attached to nvim buffers, inspired by
// vimshell(http://www.wana.at/vimshell/) and
// Conque(https://code.google.com/p/conque/).
// Libvterm usage instructions (plus some extra code) were taken from
// pangoterm(http://www.leonerd.org.uk/code/pangoterm/)
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <vterm.h>

#include "nvim/vim.h"
#include "nvim/terminal.h"
#include "nvim/message.h"
#include "nvim/memory.h"
#include "nvim/option.h"
#include "nvim/macros.h"
#include "nvim/mbyte.h"
#include "nvim/buffer.h"
#include "nvim/ascii.h"
#include "nvim/getchar.h"
#include "nvim/ui.h"
#include "nvim/syntax.h"
#include "nvim/screen.h"
#include "nvim/keymap.h"
#include "nvim/memline.h"
#include "nvim/mark.h"
#include "nvim/map.h"
#include "nvim/misc1.h"
#include "nvim/move.h"
#include "nvim/ex_docmd.h"
#include "nvim/ex_cmds.h"
#include "nvim/window.h"
#include "nvim/os/event.h"
#include "nvim/api/private/helpers.h"
#include "nvim/fileio.h"

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "terminal.c.generated.h"
#endif

#define MAX_KEY_LENGTH 256

typedef struct {
  size_t cols;
  VTermScreenCell cells[];
} ScrollbackLine;

struct terminal {
  char textbuf[0x1fff];
  // TODO(tarruda): a circular buffer would be more efficient for this
  ScrollbackLine **sb_buffer;
  size_t sb_current, sb_size;
  buf_T *buf;
  win_T *curwin;
  VTerm *vt;
  VTermScreen *vts;
  terminal_write_cb write_cb;
  void *write_data;
  terminal_resize_cb resize_cb;
  void *resize_data;
  // program exited
  bool exited;
  // input focused
  bool focused;
  // some vterm properties
  bool mouse_enabled, altscreen;
  // invalid rows
  int invalid_start, invalid_end;
  // cursor highlight information
  int cursor_hl_group_id;
  int cursor_hl_pos_id;
  uint16_t new_width, new_height;
  struct {
    int row, col;
    bool visible;
  } cursor;
};

static VTermScreenCallbacks vterm_screen_callbacks = {
  .damage      = term_damage,
  .moverect    = term_moverect,
  .movecursor  = term_movecursor,
  .settermprop = term_settermprop,
  .bell        = term_bell,
  .sb_pushline = term_sb_push,
  .sb_popline  = term_sb_pop,
};

static PMap(ptr_t) *damaged_terminals;
static Map(int, int) *color_indexes;
static int default_vt_fg, default_vt_bg;

void terminal_init(void)
{
  damaged_terminals = pmap_new(ptr_t)();
  initialize_color_indexes();
}

Terminal *terminal_open(uint16_t width, uint16_t height, bool force)
{
  int ecmd_flags = ECMD_HIDE | (force ? ECMD_FORCEIT : 0);
  (void)do_ecmd(0, NULL, NULL, NULL, ECMD_ONE, ecmd_flags, NULL);
  set_option_value((uint8_t *)"buftype", 0, (uint8_t *)"nofile", OPT_LOCAL);
  set_option_value((uint8_t *)"swapfile", false, NULL, OPT_LOCAL);
  set_option_value((uint8_t *)"bufhidden", 0, (uint8_t *)"hide", OPT_LOCAL);
  set_option_value((uint8_t *)"undolevels", 0, NULL, OPT_LOCAL);
  set_option_value((uint8_t *)"foldenable", false, NULL, OPT_LOCAL);
  set_option_value((uint8_t *)"wrap", false, NULL, OPT_LOCAL);
  set_option_value((uint8_t *)"number", false, NULL, OPT_LOCAL);
  set_option_value((uint8_t *)"relativenumber", false, NULL, OPT_LOCAL);
  set_option_value((uint8_t *)"textwidth", 0, NULL, OPT_LOCAL);
  curbuf->b_p_ma = false; curbuf->b_p_ro = true;
  RESET_BINDING(curwin);
  invalidate_botline();
  redraw_later(NOT_VALID);
  // Create a new terminal instance and configure it
  Terminal *rv = xcalloc(1, sizeof(Terminal));
  rv->sb_size = 1000;
  rv->sb_current = 0;
  rv->sb_buffer = xmalloc(sizeof(ScrollbackLine *) * rv->sb_size);
  rv->cursor_hl_group_id = syn_name2id((uint8_t *)"TerminalCursor");
  rv->cursor.row = 0;
  rv->cursor.col = 0;
  rv->cursor.visible = true;
  // Associate the terminal instance with the new buffer
  rv->buf= curbuf;
  curbuf->terminal = rv;
  // Create VTerm
  rv->vt = vterm_new(height, width);
  vterm_set_utf8(rv->vt, 1);
  // Setup state
  VTermState *state = vterm_obtain_state(rv->vt);
  vterm_state_set_bold_highbright(state, true);
  // Set up screen
  rv->vts = vterm_obtain_screen(rv->vt);
  vterm_screen_enable_altscreen(rv->vts, true);
    // delete empty lines at the end of the buffer
  vterm_screen_set_callbacks(rv->vts, &vterm_screen_callbacks, rv);
  vterm_screen_set_damage_merge(rv->vts, VTERM_DAMAGE_SCROLL);
  vterm_screen_reset(rv->vts, 1);
  return rv;
}

void terminal_set_title(Terminal *term, char *title)
{
  (void)setfname(term->buf, (char_u *)title, NULL, true);
}

void terminal_resize(Terminal *term, uint16_t width, uint16_t height)
{
  if (term->exited) {
    return;
  }

  int curwidth, curheight;
  vterm_get_size(term->vt, &curheight, &curwidth);

  if (!width) {
    width = (uint16_t)curwidth;
  }

  if (!height) {
    height = (uint16_t)curheight;
  }

  if (curheight == height && curwidth == width) {
    return;
  }

  term->new_width = (uint16_t)width;
  term->new_height = (uint16_t)height;
  event_push((Event) { .data = term, .handler = on_resize }, false);
}

void terminal_enter(Terminal *term, bool process_deferred)
{
  // hide nvim cursor and show terminal's
  ui_cursor_off();
  ui_lock_cursor_state();
  // disable ctrl+c
  bool save_mapped_ctrl_c = mapped_ctrl_c;
  mapped_ctrl_c = true;
  term->focused = true;
  // save the focused window while in this mode
  term->curwin = curwin;
  // go to the bottom
  term->curwin->w_cursor.lnum = term->buf->b_ml.ml_line_count;
  flush_updates();
  bool close = false;
  int c;

  for (;;) {
    if (process_deferred) {
      event_enable_deferred();
    }

    no_mapping++;
    allow_keys++;
    c = safe_vgetc();
    no_mapping--;
    allow_keys--;

    if (process_deferred) {
      event_disable_deferred();
    }

    if (c == K_EVENT) {
      event_process();
    } else if (c == Ctrl_W) {
      break;
    } else if (!term->exited) {
      terminal_send_key(term, c);
    } else {
      if (c == CAR) {
        do_cmdline_cmd((uint8_t *)"bwipeout!");
        close = true;
      }
      break;
    }

    flush_updates();
  }

  term->focused = false;
  ui_unlock_cursor_state();
  ui_cursor_on();
  term->curwin = NULL;
  mapped_ctrl_c = save_mapped_ctrl_c;
  if (close) {
    for (size_t i = 0 ; i < term->sb_current; i++) {
      free(term->sb_buffer[i]);
    }
    free(term->sb_buffer);
    vterm_free(term->vt);
    free(term);
  }
}

void terminal_set_write_cb(Terminal *term, terminal_write_cb cb, void *data)
{
  term->write_cb = cb;
  term->write_data = data;
}

void terminal_set_resize_cb(Terminal *term, terminal_resize_cb cb, void *data)
{
  term->resize_cb = cb;
  term->resize_data = data;
}

void terminal_send(Terminal *term, char *data, size_t size)
{
  term->write_cb(term->write_data, data, size);
}

void terminal_send_key(Terminal *term, int c)
{
  VTermModifier mod = VTERM_MOD_NONE;
  VTermKey key = convert_key(c, &mod);

  if (key) {
    vterm_keyboard_key(term->vt, key, mod);
  } else {
    vterm_keyboard_unichar(term->vt, (uint32_t)c, mod);
  }

  size_t len = vterm_output_read(term->vt, term->textbuf,
      sizeof(term->textbuf));
  terminal_send(term, term->textbuf, (size_t)len);
}

void terminal_receive(Terminal *term, char *data, size_t len)
{
  vterm_input_write(term->vt, data, len);
  vterm_screen_flush_damage(term->vts);
}

void terminal_exit(Terminal *term)
{
  char *msg = _("\r\n[Process completed, press RETURN to close]");
  terminal_receive(term, msg, strlen(msg));
  term->exited = true;
}

void terminal_get_line_attributes(Terminal *term, int line, int *term_attrs)
{
  int height, width;
  vterm_get_size(term->vt, &height, &width);
  int row = line - (int)term->sb_current - 1;
  
  for (int col = 0; col < width; col++) {
    VTermScreenCell cell;
    if (row >= 0) {
      vterm_screen_get_cell(term->vts, (VTermPos){.row = row, .col = col},
          &cell);
    } else {
      // fetch the cell from the scrollback buffer
      ScrollbackLine *sbline = term->sb_buffer[-row - 1];
      cell = sbline->cells[col];
    }

    int vt_fg = cell.fg.red << 16 | cell.fg.green << 8 | cell.fg.blue;
    vt_fg = vt_fg != default_vt_fg ? vt_fg : - 1;
    int vt_bg = cell.bg.red << 16 | cell.bg.green << 8 | cell.bg.blue;
    vt_bg = vt_bg != default_vt_bg ? vt_bg : - 1;

    int hl_attrs = (cell.attrs.bold ? HL_BOLD : 0)
                 | (cell.attrs.italic ? HL_ITALIC : 0)
                 | (cell.attrs.reverse ? HL_INVERSE : 0)
                 | (cell.attrs.underline ? HL_UNDERLINE : 0);

    int attr_id = 0;

    if (hl_attrs || vt_fg != -1 || vt_bg != -1) {
      attr_id = get_attr_entry(&(attrentry_T) {
        .cterm_ae_attr = (short)hl_attrs,
        .cterm_fg_color = vt_fg != default_vt_fg ?
                          map_get(int, int)(color_indexes, vt_fg) :
                          0,
        .cterm_bg_color = vt_bg != default_vt_bg ?
                          map_get(int, int)(color_indexes, vt_bg) :
                          0,
        .rgb_ae_attr = (short)hl_attrs,
        // TODO(tarruda): let the user customize the rgb color palette. An
        // option is to read buffer variables with global fallback
        .rgb_fg_color = vt_fg != default_vt_fg ? vt_fg : -1,
        .rgb_bg_color = vt_bg != default_vt_bg ? vt_bg : -1,
      });
    }

    if (term->cursor.visible && term->cursor.row == row
        && term->cursor.col == col) {
      attr_id = hl_combine_attr(attr_id, get_attr_entry(&(attrentry_T) {
        .rgb_ae_attr = 0, .rgb_fg_color = -1, .rgb_bg_color = -1,
        .cterm_ae_attr = 0, .cterm_fg_color = 0, .cterm_bg_color = 11,
      }));
    } 

    term_attrs[col] = attr_id;
  }
}

static int term_damage(VTermRect rect, void *data)
{
  bool is_empty = true;
  void *stub; (void)(stub);
  map_foreach_value(damaged_terminals, stub, { is_empty = false; });

  Terminal *term = data;
  term->invalid_start = MIN(term->invalid_start, rect.start_row);
  term->invalid_end = MAX(term->invalid_end, rect.end_row);
  pmap_put(ptr_t)(damaged_terminals, data, NULL);

  if (is_empty) {
    event_push((Event) { .handler = on_damage }, false);
  }

  return 1;
}

static int term_moverect(VTermRect dest, VTermRect src, void *data)
{
  VTermRect rect_union = {
    .start_row = MIN(dest.start_row, src.start_row),
    .end_row = MAX(dest.end_row, src.end_row),
  };
  return term_damage(rect_union, data);
}

static int term_movecursor(VTermPos new, VTermPos old, int visible,
    void *data)
{
  Terminal *term = data;
  term->cursor.row = new.row;
  term->cursor.col = new.col;

  VTermRect old_rect = { .start_row = old.row, .end_row = old.row + 1 };
  VTermRect new_rect = { .start_row = new.row, .end_row = new.row + 1 };
  term_damage(old_rect, data);
  term_damage(new_rect, data);
  return 1;
}

static int term_settermprop(VTermProp prop, VTermValue *val, void *data)
{
  Terminal *term = data;

  switch(prop) {
    case VTERM_PROP_ALTSCREEN:
      term->altscreen = val->boolean;
      break;

    case VTERM_PROP_CURSORVISIBLE:
      term->cursor.visible = val->boolean;
      break;

    case VTERM_PROP_TITLE:
      terminal_set_title(term, val->string);
      redraw_buf_later(term->buf, CLEAR);
      break;

    case VTERM_PROP_MOUSE:
      term->mouse_enabled = (bool)val->number;
      break;

    default:
      return 0;
  }

  return 1;
}

static int term_bell(void *data)
{
  ui_putc('\x07');
  return 1;
}

// the scrollback push/pop handlers were copied almost verbatim from pangoterm
static int term_sb_push(int cols, const VTermScreenCell *cells, void *data)
{
  Terminal *term = data;
  term->buf->b_p_ma = true; term->buf->b_p_ro = false;
  // save our buffer
  buf_T *save_curbuf = NULL;
  win_T *save_curwin = NULL;
  tabpage_T *save_curtab = NULL;
  switch_to_win_for_buf(term->buf, &save_curwin, &save_curtab, &save_curbuf);
  // shift up the first visible line, it will be hidden and vterm will redraw
  // over it
  linenr_T src_linenr = (linenr_T)term->sb_current + 1;
  linenr_T tgt_linenr = (linenr_T)term->sb_current;
  char *line = xstrdup((char *)ml_get(src_linenr));
  ml_append(tgt_linenr, (uint8_t *)line, 0, false);
  changed_lines(tgt_linenr, 1, src_linenr, 1);
  // switch back
  restore_win_for_buf(save_curwin, save_curtab, save_curbuf);
  term->buf->b_p_ma = false; term->buf->b_p_ro = true;

  // copy vterm cells into sb_buffer
  size_t c = (size_t)cols;
  ScrollbackLine *lbuf = NULL;
  if (term->sb_current == term->sb_size) {
    if (term->sb_buffer[term->sb_current - 1]->cols == c) {
      // Recycle old row if it's the right size
      lbuf = term->sb_buffer[term->sb_current - 1];
    } else {
      free(term->sb_buffer[term->sb_current - 1]);
    }

    memmove(term->sb_buffer + 1, term->sb_buffer,
        sizeof(term->sb_buffer[0]) * (term->sb_current - 1));

  } else if (term->sb_current > 0) {
    memmove(term->sb_buffer + 1, term->sb_buffer,
        sizeof(term->sb_buffer[0]) * term->sb_current);
  }

  if (!lbuf) {
    lbuf = xmalloc(sizeof(ScrollbackLine) + c * sizeof(lbuf->cells[0]));
    lbuf->cols = c;
  }

  term->sb_buffer[0] = lbuf;
  if (term->sb_current < term->sb_size) {
    term->sb_current++;
  }
  memcpy(lbuf->cells, cells, sizeof(cells[0]) * (size_t)cols);

  return 1;
}

static int term_sb_pop(int cols, VTermScreenCell *cells, void *data)
{
  Terminal *term = data;

  if (!term->sb_current)
    return 0;

  term->buf->b_p_ma = true; term->buf->b_p_ro = false;
  // restore our buffer
  buf_T *save_curbuf = NULL;
  win_T *save_curwin = NULL;
  tabpage_T *save_curtab = NULL;
  switch_to_win_for_buf(term->buf, &save_curwin, &save_curtab, &save_curbuf);
  // delete the first line that is not visible above the screen, it will be
  // redrawn by vterm
  linenr_T tgt_linenr = (linenr_T)term->sb_current;
  ml_delete(tgt_linenr, false);
  changed_lines(tgt_linenr, 1, tgt_linenr + 1, 1);
  // switch back
  restore_win_for_buf(save_curwin, save_curtab, save_curbuf);
  term->buf->b_p_ma = false; term->buf->b_p_ro = true;

  // restore vterm state
  size_t c = (size_t)cols;
  ScrollbackLine *lbuf = term->sb_buffer[0];
  term->sb_current--;
  memmove(term->sb_buffer, term->sb_buffer + 1,
      sizeof(term->sb_buffer[0]) * (term->sb_current));

  size_t cols_to_copy = c;
  if (cols_to_copy > lbuf->cols) {
    cols_to_copy = lbuf->cols;
  }

  // copy to vterm state
  memcpy(cells, lbuf->cells, sizeof(cells[0]) * cols_to_copy);
  for(size_t col = cols_to_copy; col < c; col++) {
    cells[col].chars[0] = 0;
    cells[col].width = 1;
  }
  free(lbuf);

  return 1;
}

static void on_damage(Event event)
{
  Terminal *term;
  void *stub; (void)(stub);
  block_autocmds();
  map_foreach(damaged_terminals, term, stub, {
    refresh(term);
  });
  pmap_clear(ptr_t)(damaged_terminals);
  unblock_autocmds();
  flush_updates();
}

static void on_resize(Event event)
{
  Terminal *term = event.data;

  int width, height;
  vterm_get_size(term->vt, &height, &width);

  if (term->new_height == height && term->new_width == width) {
    return;
  }

  if (term->new_height < height) {
    term->buf->b_p_ma = true; term->buf->b_p_ro = false;
    buf_T *save_curbuf = NULL;
    win_T *save_curwin = NULL;
    tabpage_T *save_curtab = NULL;
    switch_to_win_for_buf(term->buf, &save_curwin, &save_curtab,
        &save_curbuf);
    // delete empty lines at the end of the buffer
    int count = 0;
    term->buf->b_p_ma = true;
    while (ml_get(term->buf->b_ml.ml_line_count)[0] == NUL
        && term->buf->b_ml.ml_line_count > 1) {
      ml_delete(term->buf->b_ml.ml_line_count, false);
      count++;
    }
    deleted_lines(term->buf->b_ml.ml_line_count, count);
    term->buf->b_p_ma = false;
    // switch back
    restore_win_for_buf(save_curwin, save_curtab, save_curbuf);
    FOR_ALL_WINDOWS_IN_TAB(wp, curtab) {
      if (wp->w_buffer == term->buf) {
        wp->w_cursor.lnum = term->buf->b_ml.ml_line_count;
        redraw_win_later(wp, CLEAR);
      }
    }
    term->buf->b_p_ma = false; term->buf->b_p_ro = true;
  }

  vterm_set_size(term->vt, term->new_height, term->new_width);
  vterm_screen_flush_damage(term->vts);
  term->resize_cb(term->resize_data,
      (uint16_t)term->new_width, (uint16_t)term->new_height);
}

static void refresh(Terminal *term)
{
  term->buf->b_p_ma = true; term->buf->b_p_ro = false;
  buf_T *save_curbuf = NULL;
  win_T *save_curwin = NULL;
  tabpage_T *save_curtab = NULL;
  switch_to_win_for_buf(term->buf, &save_curwin, &save_curtab, &save_curbuf);
  VTermPos p;
  int added = 0;
  int height, width;
  vterm_get_size(term->vt, &height, &width);

  for (p.row = term->invalid_start; p.row < term->invalid_end; p.row++) {
    VTermScreenCell cell;
    char *ptr = term->textbuf;
    size_t line_size = 0;

    for (p.col = 0; p.col < width; p.col++) {
      vterm_screen_get_cell(term->vts, p, &cell);
      if (cell.chars[0] == 0) {
        *ptr++ = ' ';
      } else {
        char buf[64];
        size_t l = 0, i = 0;
        do {
          l += (size_t)utf_char2bytes((int)cell.chars[i], (uint8_t *)buf + l);
        } while(cell.chars[++i] != 0);
        memcpy(ptr, buf, l);
        ptr += l;
        if (buf[0] != ' ') {
          // we dont care about trailing whitespace
          line_size = (size_t)(ptr - term->textbuf);
        }
      }
    }
    *ptr++ = 0;

    char *line = xmemdupz(term->textbuf, line_size);
    int linenr = p.row + (int)term->sb_current + 1;

    if (linenr <= term->buf->b_ml.ml_line_count) {
      ml_replace(linenr, (uint8_t *)line, false);
    } else {
      ml_append(linenr - 1, (uint8_t *)line, 0, false);
      added++;
    }
  }

  if (curwin == term->curwin) {
    term->curwin->w_cursor.lnum = term->buf->b_ml.ml_line_count;
  }
  int line_start = term->invalid_start + (int)term->sb_current + 1;
  int line_end = term->invalid_end + (int)term->sb_current + 1;
  changed_lines(line_start, 0, line_end, added);
  restore_win_for_buf(save_curwin, save_curtab, save_curbuf);
  term->buf->b_p_ma = false; term->buf->b_p_ro = true;
  term->invalid_start = INT_MAX;
  term->invalid_end = -1;
}


static VTermKey convert_key(int key, VTermModifier *statep)
{
  if (mod_mask & MOD_MASK_SHIFT) { *statep |= VTERM_MOD_SHIFT; }
  if (mod_mask & MOD_MASK_CTRL)  { *statep |= VTERM_MOD_CTRL; }
  if (mod_mask & MOD_MASK_ALT)   { *statep |= VTERM_MOD_ALT; }

  switch(key) {
    case K_BS:        return VTERM_KEY_BACKSPACE;
    case TAB:         return VTERM_KEY_TAB;
    case Ctrl_M:      return VTERM_KEY_ENTER;
    case ESC:         return VTERM_KEY_ESCAPE;

    case K_UP:        return VTERM_KEY_UP;
    case K_DOWN:      return VTERM_KEY_DOWN;
    case K_LEFT:      return VTERM_KEY_LEFT;
    case K_RIGHT:     return VTERM_KEY_RIGHT;

    case K_INS:       return VTERM_KEY_INS;
    case K_DEL:       return VTERM_KEY_DEL;
    case K_HOME:      return VTERM_KEY_HOME;
    case K_END:       return VTERM_KEY_END;
    case K_PAGEUP:    return VTERM_KEY_PAGEUP;
    case K_PAGEDOWN:  return VTERM_KEY_PAGEDOWN;

    case K_K0:
    case K_KINS:      return VTERM_KEY_KP_0;
    case K_K1:
    case K_KEND:      return VTERM_KEY_KP_1;
    case K_K2:        return VTERM_KEY_KP_2;
    case K_K3:
    case K_KPAGEDOWN: return VTERM_KEY_KP_3;
    case K_K4:        return VTERM_KEY_KP_4;
    case K_K5:        return VTERM_KEY_KP_5;
    case K_K6:        return VTERM_KEY_KP_6;
    case K_K7:
    case K_KHOME:     return VTERM_KEY_KP_7;
    case K_K8:        return VTERM_KEY_KP_8;
    case K_K9:
    case K_KPAGEUP:   return VTERM_KEY_KP_9;
    case K_KDEL:
    case K_KPOINT:    return VTERM_KEY_KP_PERIOD;
    case K_KENTER:    return VTERM_KEY_KP_ENTER;
    case K_KPLUS:     return VTERM_KEY_KP_PLUS;
    case K_KMINUS:    return VTERM_KEY_KP_MINUS;
    case K_KMULTIPLY: return VTERM_KEY_KP_MULT;
    case K_KDIVIDE:   return VTERM_KEY_KP_DIVIDE;

    default:          return VTERM_KEY_NONE;
  }
}

static void initialize_color_indexes(void)
{
  color_indexes = map_new(int, int)();
  VTerm *vt = vterm_new(24, 80);
  VTermState *state = vterm_obtain_state(vt);

  for (int color_index = 0; color_index < 256; color_index++) {
    VTermColor color;
    vterm_state_get_palette_color(state, color_index, &color);
    map_put(int, int)(color_indexes,
        (color.red << 16 | color.green << 8 | color.blue), color_index + 1);
  }

  VTermColor fg, bg;
  vterm_state_get_default_colors(state, &fg, &bg);
  default_vt_fg = fg.red << 16 | fg.green << 8 | fg.blue;
  default_vt_bg = bg.red << 16 | bg.green << 8 | bg.blue;
  vterm_free(vt);
}

static void flush_updates(void)
{
  block_autocmds();
  update_topline();
  validate_cursor();

  if (VIsual_active) {
    update_curbuf(INVERTED);
  } else if (must_redraw) {
    update_screen(0);
  } else if (redraw_cmdline || clear_cmdline) {
    showmode();
  }
  redraw_statuslines();
  if (need_maketitle) {
    maketitle();
  }
  showruler(false);
  setcursor();
  ui_cursor_on();
  ui_flush();
  unblock_autocmds();
}
