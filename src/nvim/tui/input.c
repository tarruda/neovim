
#include "nvim/tui/input.h"
#include "nvim/vim.h"
#include "nvim/api/vim.h"
#include "nvim/api/private/helpers.h"
#include "nvim/ascii.h"
#include "nvim/misc2.h"
#include "nvim/os/os.h"
#include "nvim/os/input.h"
#include "nvim/event/rstream.h"

#define PASTETOGGLE_KEY "<f37>"

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "tui/input.c.generated.h"
#endif

void term_input_init(TermInput *input, Loop *loop)
{
  input->loop = loop;
  input->paste_enabled = false;
  input->in_fd = 0;

  const char *term = os_getenv("TERM");
  if (!term) {
    term = "";  // termkey_new_abstract assumes non-null (#2745)
  }
  input->tk = termkey_new_abstract(term, 0);
  int curflags = termkey_get_canonflags(input->tk);
  termkey_set_canonflags(input->tk, curflags | TERMKEY_CANON_DELBS);
  // setup input handle
  rstream_init_fd(loop, &input->read_stream, input->in_fd, 0xfff, input);
  // initialize a timer handle for handling ESC with libtermkey
  time_watcher_init(loop, &input->timer_handle, input);
  // Set the pastetoggle option to a special key that will be sent when
  // \e[20{0,1}~/ are received
  Error err = ERROR_INIT;
  vim_set_option(cstr_as_string("pastetoggle"),
      STRING_OBJ(cstr_as_string(PASTETOGGLE_KEY)), &err);
}

void term_input_destroy(TermInput *input)
{
  time_watcher_close(&input->timer_handle, NULL);
  stream_close(&input->read_stream, NULL);
  termkey_destroy(input->tk);
}

void term_input_start(TermInput *input)
{
  rstream_start(&input->read_stream, read_cb);
}

void term_input_stop(TermInput *input)
{
  rstream_stop(&input->read_stream);
  time_watcher_stop(&input->timer_handle);
}

void term_input_set_encoding(TermInput *input, char* enc)
{
  int enc_flag = strcmp(enc, "utf-8") == 0 ? TERMKEY_FLAG_UTF8
                                           : TERMKEY_FLAG_RAW;
  termkey_set_flags(input->tk, enc_flag);
}

static void input_enqueue_event(void **argv)
{
  char *buf = argv[0];
  input_enqueue(cstr_as_string(buf));
  xfree(buf);
}

static void input_done_event(void **argv)
{
  input_done();
}

static void enqueue_input(char *buf, size_t size)
{
  loop_schedule(&loop, event_create(1, input_enqueue_event, 1,
        xstrndup(buf, size)));
}

static void forward_simple_utf8(TermKeyKey *key)
{
  size_t len = 0;
  char buf[64];
  char *ptr = key->utf8;

  while (*ptr) {
    if (*ptr == '<') {
      len += (size_t)snprintf(buf + len, sizeof(buf) - len, "<lt>");
    } else {
      buf[len++] = *ptr;
    }
    ptr++;
  }

  buf[len] = 0;
  enqueue_input(buf, len);
}

static void forward_modified_utf8(TermKey *tk, TermKeyKey *key)
{
  size_t len;
  char buf[64];

  if (key->type == TERMKEY_TYPE_KEYSYM
      && key->code.sym == TERMKEY_SYM_ESCAPE) {
    len = (size_t)snprintf(buf, sizeof(buf), "<Esc>");
  } else {
    len = termkey_strfkey(tk, buf, sizeof(buf), key, TERMKEY_FORMAT_VIM);
  }

  enqueue_input(buf, len);
}

static void forward_mouse_event(TermKey *tk, TermKeyKey *key)
{
  char buf[64];
  size_t len = 0;
  int button, row, col;
  TermKeyMouseEvent ev;
  termkey_interpret_mouse(tk, key, &ev, &button, &row, &col);

  if (ev != TERMKEY_MOUSE_PRESS && ev != TERMKEY_MOUSE_DRAG) {
    return;
  }

  row--; col--;  // Termkey uses 1-based coordinates
  buf[len++] = '<';

  if (key->modifiers & TERMKEY_KEYMOD_SHIFT) {
    len += (size_t)snprintf(buf + len, sizeof(buf) - len, "S-");
  }

  if (key->modifiers & TERMKEY_KEYMOD_CTRL) {
    len += (size_t)snprintf(buf + len, sizeof(buf) - len, "C-");
  }

  if (key->modifiers & TERMKEY_KEYMOD_ALT) {
    len += (size_t)snprintf(buf + len, sizeof(buf) - len, "A-");
  }

  if (button == 1) {
    len += (size_t)snprintf(buf + len, sizeof(buf) - len, "Left");
  } else if (button == 2) {
    len += (size_t)snprintf(buf + len, sizeof(buf) - len, "Middle");
  } else if (button == 3) {
    len += (size_t)snprintf(buf + len, sizeof(buf) - len, "Right");
  }

  if (ev == TERMKEY_MOUSE_PRESS) {
    if (button == 4) {
      len += (size_t)snprintf(buf + len, sizeof(buf) - len, "ScrollWheelUp");
    } else if (button == 5) {
      len += (size_t)snprintf(buf + len, sizeof(buf) - len, "ScrollWheelDown");
    } else {
      len += (size_t)snprintf(buf + len, sizeof(buf) - len, "Mouse");
    }
  } else if (ev == TERMKEY_MOUSE_DRAG) {
    len += (size_t)snprintf(buf + len, sizeof(buf) - len, "Drag");
  }

  len += (size_t)snprintf(buf + len, sizeof(buf) - len, "><%d,%d>", col, row);
  enqueue_input(buf, len);
}

static TermKeyResult tk_getkey(TermKey *tk, TermKeyKey *key, bool force)
{
  return force ? termkey_getkey_force(tk, key) : termkey_getkey(tk, key);
}

static void timer_cb(TimeWatcher *watcher, void *data);

static int get_key_code_timeout(void)
{
  Integer ms = -1;
  // Check 'ttimeout' to determine if we should send ESC after 'ttimeoutlen'.
  // See :help 'ttimeout' for more information
  Error err = ERROR_INIT;
  if (vim_get_option(cstr_as_string("ttimeout"), &err).data.boolean) {
    ms = vim_get_option(cstr_as_string("ttimeoutlen"), &err).data.integer;
  }

  return (int)ms;
}

static void tk_getkeys(TermInput *input, bool force)
{
  TermKeyKey key;
  TermKeyResult result;

  while ((result = tk_getkey(input->tk, &key, force)) == TERMKEY_RES_KEY) {
    if (key.type == TERMKEY_TYPE_UNICODE && !key.modifiers) {
      forward_simple_utf8(&key);
    } else if (key.type == TERMKEY_TYPE_UNICODE ||
               key.type == TERMKEY_TYPE_FUNCTION ||
               key.type == TERMKEY_TYPE_KEYSYM) {
      forward_modified_utf8(input->tk, &key);
    } else if (key.type == TERMKEY_TYPE_MOUSE) {
      forward_mouse_event(input->tk, &key);
    }
  }

  if (result != TERMKEY_RES_AGAIN) {
    return;
  }

  int ms  = get_key_code_timeout();

  if (ms > 0) {
    // Stop the current timer if already running
    time_watcher_stop(&input->timer_handle);
    time_watcher_start(&input->timer_handle, timer_cb, (uint32_t)ms, 0);
  } else {
    tk_getkeys(input, true);
  }
}

static void timer_cb(TimeWatcher *watcher, void *data)
{
  tk_getkeys(data, true);
}

static bool handle_bracketed_paste(TermInput *input)
{
  if (rbuffer_size(input->read_stream.buffer) > 5 &&
      (!rbuffer_cmp(input->read_stream.buffer, "\x1b[200~", 6) ||
       !rbuffer_cmp(input->read_stream.buffer, "\x1b[201~", 6))) {
    bool enable = *rbuffer_get(input->read_stream.buffer, 4) == '0';
    // Advance past the sequence
    rbuffer_consumed(input->read_stream.buffer, 6);
    if (input->paste_enabled == enable) {
      return true;
    }
    if (enable) {
      // Get the current mode
      int state = get_real_state();
      if (state & NORMAL) {
        // Enter insert mode
        enqueue_input("i", 1);
      } else if (state & VISUAL) {
        // Remove the selected text and enter insert mode
        enqueue_input("c", 1);
      } else if (!(state & INSERT)) {
        // Don't mess with the paste option
        return true;
      }
    }
    enqueue_input(PASTETOGGLE_KEY, sizeof(PASTETOGGLE_KEY) - 1);
    input->paste_enabled = enable;
    return true;
  }
  return false;
}

static bool handle_forced_escape(TermInput *input)
{
  if (rbuffer_size(input->read_stream.buffer) > 1
      && !rbuffer_cmp(input->read_stream.buffer, "\x1b\x00", 2)) {
    // skip the ESC and NUL and push one <esc> to the input buffer
    size_t rcnt;
    termkey_push_bytes(input->tk, rbuffer_read_ptr(input->read_stream.buffer,
          &rcnt), 1);
    rbuffer_consumed(input->read_stream.buffer, 2);
    tk_getkeys(input, true);
    return true;
  }
  return false;
}

static void restart_reading(void **argv);

static void read_cb(Stream *stream, RBuffer *buf, size_t c, void *data,
    bool eof)
{
  TermInput *input = data;

  if (eof) {
    if (input->in_fd == 0 && !os_isatty(0) && os_isatty(2)) {
      // Started reading from stdin which is not a pty but failed. Switch to
      // stderr since it is a pty.
      //
      // This is how we support commands like:
      //
      // echo q | nvim -es
      //
      // and
      //
      // ls *.md | xargs nvim
      input->in_fd = 2;
      stream_close(&input->read_stream, NULL);
      queue_put(input->loop->fast_events, restart_reading, 1, input);
    } else {
      loop_schedule(&loop, event_create(1, input_done_event, 0));
    }
    return;
  }

  do {
    if (handle_bracketed_paste(input) || handle_forced_escape(input)) {
      continue;
    }

    // Find the next 'esc' and push everything up to it(excluding). This is done
    // so the `handle_bracketed_paste`/`handle_forced_escape` calls above work
    // as expected.
    size_t count = 0;
    RBUFFER_EACH(input->read_stream.buffer, c, i) {
      count = i + 1;
      if (c == '\x1b' && count > 1) {
        count--;
        break;
      }
    }

    RBUFFER_UNTIL_EMPTY(input->read_stream.buffer, ptr, len) {
      size_t consumed = termkey_push_bytes(input->tk, ptr, MIN(count, len));
      // termkey_push_bytes can return (size_t)-1, so it is possible that
      // `consumed > input->read_stream.buffer->size`, but since tk_getkeys is
      // called soon, it shouldn't happen
      assert(consumed <= input->read_stream.buffer->size);
      rbuffer_consumed(input->read_stream.buffer, consumed);
      // Need to process the keys now since there's no guarantee "count" will
      // fit into libtermkey's input buffer.
      tk_getkeys(input, false);
      if (!(count -= consumed)) {
        break;
      }
    }
  } while (rbuffer_size(input->read_stream.buffer));

  // Make sure the next input escape sequence fits into the ring buffer
  // without wrap around, otherwise it could be misinterpreted.
  rbuffer_reset(input->read_stream.buffer);
}

static void restart_reading(void **argv)
{
  TermInput *input = argv[0];
  rstream_init_fd(input->loop, &input->read_stream, input->in_fd, 0xfff, input);
  rstream_start(&input->read_stream, read_cb);
}
