#include <stdlib.h>

#include <uv.h>

#include "nvim/ui.h"
#include "nvim/vim.h"
#include "nvim/ascii.h"
#include "nvim/macros.h"
#include "nvim/memory.h"
#include "nvim/charset.h"
#include "nvim/syntax.h"
#include "nvim/globals.h"
#include "nvim/screen.h"

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "ui.c.generated.h"
#endif

#define MAX_UI_COUNT 8

static UI *uis[MAX_UI_COUNT];
static size_t ui_count = 0;
static int row, col;
static struct {
  int top, bot, left, right;
} sr;
static int current_highlight_mask = 0;
static bool cursor_enabled = true;
typedef UI *(*ui_create_fn)(void);

// See http://stackoverflow.com/a/11172679 for an explanation of this macro
// black magic.
#define UI_CALL(...)                                              \
  do {                                                            \
    for (size_t i = 0; i < ui_count; i++) {                       \
      UI *ui = uis[i];                                            \
      UI_CALL_HELPER(CNT(__VA_ARGS__), __VA_ARGS__);              \
    }                                                             \
  } while (0)
#define CNT(...) SELECT_6TH(__VA_ARGS__, MORE, MORE, MORE, MORE, ZERO, ignore)
#define SELECT_6TH(a1, a2, a3, a4, a5, a6, ...) a6
#define UI_CALL_HELPER(c, ...) UI_CALL_HELPER2(c, __VA_ARGS__)
#define UI_CALL_HELPER2(c, ...) UI_CALL_##c(__VA_ARGS__)
#define UI_CALL_MORE(method, ...) ui->method(__VA_ARGS__)
#define UI_CALL_ZERO(method) ui->method()

void ui_resize(int width, int height)
{
  UI_CALL(resize, width, height);
}

void ui_clear(void)
{
  UI_CALL(clear);
}

void ui_eol_clear(void)
{
  UI_CALL(eol_clear);
}

void ui_cursor_goto(int new_row, int new_col)
{
  row = new_row;
  col = new_col;
  UI_CALL(cursor_goto, row, col);
}

void ui_cursor_on(void)
{
  if (!cursor_enabled) {
    UI_CALL(ui_cursor_on);
    cursor_enabled = true;
  }
}

void ui_cursor_off(void)
{
  if (full_screen) {
    if (cursor_enabled) {
      UI_CALL(ui_cursor_off);
    }
    cursor_enabled = false;
  }
}

void ui_mouse_on(void)
{
  UI_CALL(mouse_on);
}

void ui_mouse_off(void)
{
  UI_CALL(mouse_off);
}

// Notify that the current mode has changed. Can be used to change cursor
// shape, for example.
void ui_change_mode(void)
{
  static int showing_insert_mode = MAYBE;

  if (!full_screen)
    return;

  if (State & INSERT) {
    if (showing_insert_mode != TRUE) {
      UI_CALL(insert_mode);
    }
    showing_insert_mode = TRUE;
  } else {
    if (showing_insert_mode != FALSE) {
      UI_CALL(normal_mode);
    }
    showing_insert_mode = FALSE;
  }
  conceal_check_cursur_line();
}

// Set scrolling region for window 'wp'. The region starts 'off' lines from the
// start of the window.  Also set the vertical scroll region for a vertically
// split window.  Always the full width of the window, excluding the vertical
// separator.
void ui_set_scroll_region(win_T *wp, int off)
{
  sr.top = wp->w_winrow + off;
  sr.bot = wp->w_winrow + wp->w_height - 1;
  sr.left = wp->w_wincol;
  sr.right = wp->w_wincol + wp->w_width - 1;
  UI_CALL(set_scroll_region, sr.top, sr.bot, sr.left, sr.right);
  screen_start();
}

// Reset scrolling region to the whole screen.
void ui_unset_scroll_region(void)
{
  sr.top = 0;
  sr.bot = (int)Rows - 1;
  sr.left = 0;
  sr.right = (int)Columns - 1;
  UI_CALL(set_scroll_region, sr.top, sr.bot, sr.left, sr.right);
  screen_start();
}

void ui_scroll_down(int count)
{
  UI_CALL(scroll_down, count);
}

void ui_scroll_up(int count)
{
  UI_CALL(scroll_up, count);
}

void ui_highlight_start(int mask)
{
  if (mask > HL_ALL) {
    current_highlight_mask = mask;
  } else {
    current_highlight_mask |= mask;
  }

  if (!ui_count) {
    return;
  }

  HlAttrs attrs;
  set_highlight_args(current_highlight_mask, &attrs);
  UI_CALL(highlight_set, attrs);
}

void ui_highlight_stop(int mask)
{
  HlAttrs attrs = {
    false, false, false, false, false, false, -1, -1
  };
  UI_CALL(highlight_set, attrs);
}

void ui_printn(uint8_t *str, size_t len)
{
  for (uint8_t c = *str; len;) {
    if (c < 0x20) {
      str = control_str(str, &len);
    } else {
      str = text_str(str, &len);
    }
  }
}

void ui_print(uint8_t *str)
{
  ui_printn(str, strlen((char *)str));
}

void ui_print_char(uint8_t c)
{
  uint8_t str[] = {c};
  ui_printn(str, 1);
}

void ui_bell(void)
{
  bell();
}

void ui_visual_bell(void)
{
  UI_CALL(visual_bell);
}

void ui_flush(void)
{
  UI_CALL(flush);
}

void ui_suspend(void)
{
  UI_CALL(suspend);
}

void ui_set_title(char *title)
{
  UI_CALL(set_title, title);
}

void ui_set_icon(char *icon)
{
  UI_CALL(set_icon, icon);
}

void ui_register(UI *ui)
{
  if (ui_count == MAX_UI_COUNT) {
    abort();
  }

  uis[ui_count++] = ui;
}

void ui_load(const char *name)
{
  uv_lib_t *lib = xmalloc(sizeof(uv_lib_t));

  if (uv_dlopen(name, lib)) {
    fprintf(stderr, "dlopen error: %s\n", uv_dlerror(lib));
  }

  ui_create_fn fn;
  if (uv_dlsym(lib, "ui_create", (void **)&fn)) {
    fprintf(stderr, "dlsym error: %s\n", uv_dlerror(lib));
  } else {
    UI *ui = fn();
    ui_register(ui);
    if (!ui->setup()) {
      fprintf(stderr, "Failed to start UI\n");
    }
  }
}

void ui_setup(void)
{
}

void ui_teardown(void)
{
  UI_CALL(teardown);
  ui_count = 0;
}

static void set_highlight_args(int mask, HlAttrs *attrs)
{
  attrentry_T *aep = NULL;
  attrs->foreground = -1;
  attrs->background = -1;

  if (mask > HL_ALL) {
    aep = syn_cterm_attr2entry(mask);
    mask = aep ? aep->ae_attr : 0;
  }

  attrs->bold = mask & HL_BOLD;
  attrs->standout = mask & HL_STANDOUT;
  attrs->underline = mask & HL_UNDERLINE;
  attrs->undercurl = mask & HL_UNDERCURL;
  attrs->italic = mask & HL_ITALIC;
  attrs->reverse = mask & HL_INVERSE;

  if (aep && aep->ae_u.cterm.fg_color
      && (cterm_normal_fg_color != aep->ae_u.cterm.fg_color)) {
    attrs->foreground = aep->ae_u.cterm.fg_color - 1;
  }

  if (aep && aep->ae_u.cterm.bg_color
      && (cterm_normal_bg_color != aep->ae_u.cterm.bg_color)) {
    attrs->background = aep->ae_u.cterm.bg_color - 1;
  }
}

static uint8_t *control_str(uint8_t *str, size_t *len)
{
  if (*str == '\n') {
    linefeed();
  } else if (*str == '\r') {
    carriage_return();
  } else if (*str == '\b') {
    cursor_left();
  } else if (*str == Ctrl_L) {
    cursor_right();
  } else if (*str == Ctrl_G) {
    bell();
  } else {
    // Should not receive other control character
    abort();
  }
  (*len)--;
  return str + 1;
}

static uint8_t *text_str(uint8_t *str, size_t *len)
{
  while (*str >= 0x20 && *len) {
    size_t clen = (size_t)mb_ptr2len(str);
    UI_CALL(put, str, (size_t)clen);
    col++;
    if (mb_ptr2cells(str) > 1) {
      // double-width character, blank the next column
      UI_CALL(put, NULL, 0);
      col++;
    }
    str += clen;
    (*len) -= clen;
  }

  return str;
}

static void linefeed(void)
{
  col = 0;
  if (row == sr.bot) {
    row++;
  } else {
    UI_CALL(scroll_down, 1);
  }
  UI_CALL(cursor_goto, row, col);
}

static void carriage_return(void)
{
  col = 0;
  UI_CALL(cursor_goto, row, col);
}

static void cursor_left(void)
{
  if (col) {
    col--;
    UI_CALL(cursor_goto, row, col);
  }
}

static void cursor_right(void)
{
  col++;
  UI_CALL(cursor_goto, row, col);
}

static void bell(void)
{
  UI_CALL(bell);
}

