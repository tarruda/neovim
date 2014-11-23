#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include <unistd.h>

#include <termkey.h>
#include <tickit.h>

#include "nvim/vim.h"
#include "nvim/ui.h"
#include "nvim/api/vim.h"
#include "nvim/os/event.h"
#include "nvim/os/input.h"

// Need to remember screen state in order to handle scroll commands
typedef struct {
  char data[8];
  HlAttrs attrs;
} Cell;
static Cell **screen = NULL;

static int in_fd, out_fd;
static UI self;
static uv_poll_t input_watcher;
static uv_signal_t winch_watcher;
static TermKey *tk;
static TickitTerm *tt;
static TickitPen *pen;
static TickitRect scrollrect;
static int width, height, row, col;
static HlAttrs attrs;

static void clear_cells(Cell *ptr, int count)
{
  while (count--) {
    ptr->data[0] = ' ';
    ptr->data[1] = 0;
    ptr->attrs = (HlAttrs) {false, false, false, false, false, false, -1, -1};
    ptr++;
  }
}

static void print_cell(Cell *ptr)
{
  if (*ptr->data == 0) {
    return;
  }

  tickit_pen_set_colour_attr(pen, TICKIT_PEN_FG,
      ptr->attrs.foreground != -1 ? ptr->attrs.foreground : -1);
  // tickit_pen_set_colour_attr(pen, TICKIT_PEN_BG,
  //     ptr->attrs.background != -1 ? ptr->attrs.background : -1);
  tickit_pen_set_bool_attr(pen, TICKIT_PEN_BOLD, ptr->attrs.bold);
  tickit_pen_set_bool_attr(pen, TICKIT_PEN_ITALIC, ptr->attrs.italic);
  tickit_pen_set_bool_attr(pen, TICKIT_PEN_UNDER,
      ptr->attrs.undercurl || ptr->attrs.underline);
  tickit_pen_set_bool_attr(pen, TICKIT_PEN_REVERSE,
      ptr->attrs.standout || ptr->attrs.reverse);
  tickit_term_setpen(tt, pen);
  tickit_term_print(tt, ptr->data);
}

static void print_cells(Cell *ptr, int count)
{
  while (count--) {
    print_cell(ptr);
    col++;
    ptr++;
  }
}

static void clear_block(int top, int lines, int left, int cols)
{
  int bot = top + lines;
  for (int i = top; i < bot; i++) {
    tickit_term_goto(tt, i, left);
    clear_cells(screen[i] + left, cols);
    print_cells(screen[i] + left, cols);
  }
}

static void clear_scroll_region(void)
{
  clear_block(scrollrect.top, scrollrect.lines, scrollrect.left,
      scrollrect.cols);
}

static void redraw_scroll_region(void)
{
  int top = scrollrect.top;
  int bot = tickit_rect_bottom(&scrollrect) - 1;

  for (int i = top; i <= bot; i++) {
    tickit_term_goto(tt, i, scrollrect.left);
    print_cells(screen[i] + scrollrect.left, scrollrect.cols);
  }
}

static void shift_scroll_region(int count) {
  int top = scrollrect.top;
  int bot = tickit_rect_bottom(&scrollrect) - 1;
  int left = scrollrect.left;
  int start, stop, step;

  if (count > 0) {
    start = top;
    stop = bot - count + 1;
    step = 1;
  } else {
    start = bot;
    stop = top - count - 1;
    step = -1;
  }

  int i;
  // Shift row sections
  for (i = start; i != stop; i += step) {
    Cell *target_row = screen[i] + left;
    Cell *source_row = screen[i + count] + left;
    memcpy(target_row, source_row, sizeof(Cell) * (size_t)scrollrect.cols);
  }

  // Clear invalid row sections
  for (stop += count; i != stop; i += step) {
    clear_cells(screen[i] + left, scrollrect.cols);
  }
}

static void scroll(int count)
{
  // Update internal screen
  shift_scroll_region(count);

  // Update the terminal, first try with tickit_term_scrollrect which will
  // try to do it in the most efficient way for the terminal
  if (!tickit_term_scrollrect(tt, scrollrect.top, scrollrect.left,
        scrollrect.lines, scrollrect.cols, count, 0)) {
    // Failing that, redraw the whole scrolled region
    redraw_scroll_region();
  }

  // Restore the cursor position
  tickit_term_goto(tt, row, col);
}

static void forward_simple_utf8(TermKeyKey *key)
{
  vim_input((String) {.data = key->utf8, .size = strlen(key->utf8)});
}

static void forward_modified_utf8(TermKeyKey *key)
{
  size_t size;
  char name[128];
#define SET(str) size = sizeof(str); memcpy(name, str, size)

  switch (key->code.sym) {
    // Handle names that differ from vim's internal representation
    case TERMKEY_SYM_BACKSPACE:
      SET("<Bs>");
      break;
    case TERMKEY_SYM_TAB:
      SET("<Tab>");
      break;
    case TERMKEY_SYM_ENTER:
      SET("<Cr>");
      break;
    case TERMKEY_SYM_ESCAPE:
      SET("<Esc>");
      break;
    case TERMKEY_SYM_SPACE:
      SET("<Space>");
      break;
    case TERMKEY_SYM_DEL:
      SET("<Del>");
      break;
    default:
      size = termkey_strfkey(tk, name, sizeof(name), key,
          TERMKEY_FORMAT_VIM) + 1;
      break;
  }

  vim_input((String){.data = name, .size = size - 1});
}

static void forward_mouse_event(TermKeyKey *key)
{
  int button, line, column;
  TermKeyMouseEvent ev;
  termkey_interpret_mouse(tk, key, &ev, &button, &line, &column);
  // TODO
}

static void poll_cb(uv_poll_t *handle, int status, int events)
{
  if (status < 0) {
    input_done();
    return;
  }

  termkey_advisereadable(tk);
  TermKeyKey key;
  while (termkey_getkey_force(tk, &key) == TERMKEY_RES_KEY) {
    if (key.type == TERMKEY_TYPE_MOUSE) {
      forward_mouse_event(&key);
    } else if (key.type == TERMKEY_TYPE_UNICODE && !key.modifiers) {
      forward_simple_utf8(&key);
    } else if (key.type == TERMKEY_TYPE_UNICODE ||
               key.type == TERMKEY_TYPE_FUNCTION ||
               key.type == TERMKEY_TYPE_KEYSYM) {
      forward_modified_utf8(&key);
    }
  }
}

static void terminal_resized(Event ev)
{
  int new_width, new_height;
  tickit_term_refresh_size(tt);
  tickit_term_get_size(tt, &new_height, &new_width);
  vim_resize(new_width, new_height);
}

static void sigwinch_cb(uv_signal_t *handle, int signum)
{
  // Queue the event because resizing can result in recursive event_poll calls
  event_push((Event) { .handler = terminal_resized }, false);
}

static void tui_resize(int new_width, int new_height)
{
  if (screen) {
    for (int i = 0; i < height; i++) {
      free(screen[i]);
    }
    free(screen);
  }

  screen = malloc((size_t)new_height * sizeof(Cell *));
  for (int i = 0; i < new_height; i++) {
    // utf-8 characters can occupy up to 6 bytes, so allocate for the worst
    // possible scenario
    screen[i] = calloc((size_t)new_width, sizeof(Cell));
    clear_cells(screen[i], new_width);
  }

  tickit_term_set_size(tt, new_height, new_width);
  tickit_rect_init_sized(&scrollrect, 0, 0, new_height, new_width);
  row = col = 0;
  // TODO If the new dimensions are bigger than the terminal, setup some
  // kind of scrolling. If smaller, fill the extra area with dots(like tmux)
  height = new_height;
  width = new_width;
}

static void tui_clear(void)
{
  // Update internal screen
  for (int i = 0; i < height; i++) {
    clear_cells(screen[i], width);
  }

  // Update terminal
  tickit_term_clear(tt);
}

static void tui_eol_clear(void)
{
  clear_block(row, 1, col, width - col);
}

static void tui_cursor_goto(int new_row, int new_col)
{
  row = new_row;
  col = new_col;
  tickit_term_goto(tt, row, col);
}

static void tui_cursor_on(void)
{
  tickit_term_setctl_int(tt, TICKIT_TERMCTL_CURSORVIS, true);
}

static void tui_cursor_off(void)
{
  tickit_term_setctl_int(tt, TICKIT_TERMCTL_CURSORVIS, false);
}

static void tui_mouse_on(void)
{
  tickit_term_setctl_int(tt, TICKIT_TERMCTL_MOUSE, TICKIT_TERM_MOUSEMODE_CLICK);
}

static void tui_mouse_off(void)
{
  tickit_term_setctl_int(tt, TICKIT_TERMCTL_MOUSE, TICKIT_TERM_MOUSEMODE_OFF);
}

static void tui_insert_mode(void)
{
  if (!tickit_term_setctl_int(tt, TICKIT_TERMCTL_CURSORSHAPE,
      TICKIT_TERM_CURSORSHAPE_LEFT_BAR)) {
    tickit_term_setctl_int(tt, TICKIT_TERMCTL_CURSORSHAPE,
        TICKIT_TERM_CURSORSHAPE_UNDER);
  }
}

static void tui_normal_mode(void)
{
  tickit_term_setctl_int(tt, TICKIT_TERMCTL_CURSORSHAPE,
      TICKIT_TERM_CURSORSHAPE_BLOCK);
}

static void tui_set_scroll_region(int top, int bot, int left, int right)
{
  scrollrect.top = top;
  scrollrect.lines = bot - top + 1;
  scrollrect.left = left;
  scrollrect.cols = right - left + 1;
}

static void tui_scroll_down(int count)
{
  if (count > scrollrect.lines) {
    // Scrolled out of the region, clear it
    clear_scroll_region();
  } else {
    scroll(count);
  }
}

static void tui_scroll_up(int count)
{
  if (count > scrollrect.lines) {
    // Scrolled out of the region, clear it
    clear_scroll_region();
  } else {
    scroll(-count);
  }
}

static void tui_highlight_set(HlAttrs new_attrs)
{
  attrs = new_attrs;
}

static void tui_put(uint8_t *str, size_t len)
{
  Cell *cell = screen[row] + col;
  cell->data[len] = 0;
  cell->attrs = attrs;
  if (str) {
    memcpy(cell->data, str, len);
  }
  print_cells(cell, 1);
}

static void tui_bell(void)
{
}

static void tui_visual_bell(void)
{
}

static void tui_flush(void)
{
  tickit_term_flush(tt);
}

static void tui_suspend(void)
{
}

static void tui_set_title(char *title)
{
}

static void tui_set_icon(char *icon)
{
}

static bool tui_setup(void)
{
  // read input from stderr if stdin is not a tty
  in_fd = isatty(0) ? 0 : (isatty(2) ? 2 : -1);
  // write output to stderr if stdout is not a tty
  out_fd = isatty(1) ? 1 : (isatty(2) ? 2 : -1);

  if (in_fd == -1 || out_fd == -1) {
    fprintf(stderr, "Stdio is not connected to a tty\n");
    return false;
  }

  // libtickit/libtermkey setup
  tk = termkey_new(in_fd, TERMKEY_FLAG_CTRLC);
  tt = tickit_term_new();
  pen = tickit_pen_new();
  tickit_term_setpen(tt, pen);

  if (!tk || !tt) {
    fprintf(stderr, "Failed to initialize tty libraries: %s\n", strerror(errno));
    return false;
  }

  // initial setup of libtickit
  tickit_term_set_output_fd(tt, out_fd);
  tickit_term_await_started(tt, &(const struct timeval){
      .tv_sec = 0, .tv_usec = 50000 });
  tickit_term_setctl_int(tt, TICKIT_TERMCTL_ALTSCREEN, 1);
  Event dummy;
  terminal_resized(dummy);
  tickit_term_clear(tt);

  // listen input fd
  uv_poll_init(uv_default_loop(), &input_watcher, in_fd);
  uv_poll_start(&input_watcher, UV_READABLE, poll_cb);

  // listen for SIGWINCH
  uv_signal_init(uv_default_loop(), &winch_watcher);
  uv_signal_start(&winch_watcher, sigwinch_cb, SIGWINCH);
  return true;
}

static void tui_teardown(void)
{
  uv_poll_stop(&input_watcher);
  uv_close((uv_handle_t *)&input_watcher, NULL);
  uv_signal_stop(&winch_watcher);
  uv_close((uv_handle_t *)&winch_watcher, NULL);
  tickit_term_destroy(tt);
  termkey_destroy(tk);
}

UI *ui_create(void)
{
  self.resize = tui_resize;
  self.clear = tui_clear;
  self.eol_clear = tui_eol_clear;
  self.cursor_goto = tui_cursor_goto;
  self.ui_cursor_on = tui_cursor_on;
  self.ui_cursor_off = tui_cursor_off;
  self.mouse_on = tui_mouse_on;
  self.mouse_off = tui_mouse_off;
  self.insert_mode = tui_insert_mode;
  self.normal_mode = tui_normal_mode;
  self.set_scroll_region = tui_set_scroll_region;
  self.scroll_down = tui_scroll_down;
  self.scroll_up = tui_scroll_up;
  self.highlight_set = tui_highlight_set;
  self.put = tui_put;
  self.bell = tui_bell;
  self.visual_bell = tui_visual_bell;
  self.flush = tui_flush;
  self.suspend = tui_suspend;
  self.set_title = tui_set_title;
  self.set_icon = tui_set_icon;
  self.setup = tui_setup;
  self.teardown = tui_teardown;
  return &self;
}

