#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include <uv.h>

#include "os/shell.h"
#include "os/signal.h"
#include "types.h"
#include "vim.h"
#include "message.h"
#include "ascii.h"
#include "term.h"
#include "misc2.h"
#include "screen.h"
#include "memline.h"
#include "option_defs.h"
#include "charset.h"

#define READ_BUFFER_LENGTH 100

typedef struct {
  int old_state;
  int old_mode;
  int exit_status;
  int exited;
} ProcessData;

typedef struct {
  uint32_t lnum;
  uv_stream_t *shell_stdin;
  ProcessData *proc_data;
  char *buffer;
} ShellWriteData;

typedef struct {
  garray_T ga;
  char readbuf[READ_BUFFER_LENGTH];
  bool reading;
  ProcessData *proc_data;
} ShellReadData;

/// Parses a command string into a sequence of words, taking quotes into
/// consideration.
///
/// @param str The command string to be parsed
/// @param argv The vector that will be filled with copies of the parsed
///        words. It can be NULL if the caller only needs to count words.
/// @return The number of words parsed.
static int tokenize(char_u *str, char **argv);

/// Calculates the length of a shell word.
///
/// @param str A pointer to the first character of the word
/// @return The offset from `str` at which the word ends.
static int word_length(char_u *command);

static void write_selection(uv_write_t *req);

static int proc_cleanup_exit(ProcessData *data,
                             uv_process_options_t *opts,
                             int shellopts);
// Callbacks for libuv
static void alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf);
static void read_cb(uv_stream_t *stream, ssize_t cnt, const uv_buf_t *buf);
static void write_cb(uv_write_t *req, int status);
static void exit_cb(uv_process_t *proc, int64_t status, int term_signal);

char ** shell_build_argv(char_u *cmd, char_u *extra_shell_opt)
{
  int i;
  char **rv;
  int argc = tokenize(p_sh, NULL) + tokenize(p_shcf, NULL);

  rv = (char **)xmalloc((unsigned)((argc + 4) * sizeof(char *)));

  // Split 'shell'
  i = tokenize(p_sh, rv);

  if (extra_shell_opt != NULL) {
    // Push a copy of `extra_shell_opt`
    rv[i++] = strdup((char *)extra_shell_opt);
  }

  if (cmd != NULL) {
    // Split 'shellcmdflag'
    i += tokenize(p_shcf, rv + i);
    rv[i++] = strdup((char *)cmd);
  }

  rv[i] = NULL;

  return rv;
}

void shell_free_argv(char **argv)
{
  char **p = argv;

  if (p == NULL) {
    // Nothing was allocated, return
    return;
  }

  while (*p != NULL) {
    // Free each argument 
    free(*p);
    p++;
  }

  free(argv);
}

int os_call_shell(char_u *cmd, ShellOpts opts, char_u *extra_shell_arg)
{
  uv_stdio_container_t proc_stdio[3];
  uv_process_options_t proc_opts;
  uv_process_t proc;
  uv_pipe_t proc_stdin, proc_stdout;
  uv_write_t write_req;
  int expected_exits = 1;
  ProcessData proc_data = {
    .exited = 0,
    .old_mode = cur_tmode,
    .old_state = State
  };
  ShellWriteData write_data = {
    .lnum = 0,
    .shell_stdin = (uv_stream_t *)&proc_stdin,
    .proc_data = &proc_data
  };
  ShellReadData read_data = {
    .reading = false,
    .proc_data = &proc_data
  };

  out_flush();
  if (opts & kShellOptCooked) {
    // set to normal mode
    settmode(TMODE_COOK);
  }

  // While the child is running, ignore terminating signals
  signal_reject_deadly();

  // Create argv for `uv_spawn`
  proc_opts.args = shell_build_argv(cmd, extra_shell_arg);
  proc_opts.file = proc_opts.args[0];
  proc_opts.exit_cb = exit_cb;
  // Initialize libuv structures 
  proc_opts.stdio = proc_stdio;
  proc_opts.stdio_count = 3;
  // Hide window on Windows :)
  proc_opts.flags = UV_PROCESS_WINDOWS_HIDE;
  proc_opts.cwd = NULL;
  proc_opts.env = NULL;

  // The default is to inherit all standard file descriptors
  proc_stdio[0].flags = UV_INHERIT_FD;
  proc_stdio[0].data.fd = 0;
  proc_stdio[1].flags = UV_INHERIT_FD;
  proc_stdio[1].data.fd = 1;
  proc_stdio[2].flags = UV_INHERIT_FD;
  proc_stdio[2].data.fd = 2;

  if (opts & (kShellOptHideMess | kShellOptExpand)) {
    // Ignore the shell stdio
    proc_stdio[0].flags = UV_IGNORE;
    proc_stdio[1].flags = UV_IGNORE;
    proc_stdio[2].flags = UV_IGNORE;
  } else {
    State = EXTERNCMD;

    if (opts & kShellOptWrite) {
      // Write from the current buffer into the process stdin
      uv_pipe_init(uv_default_loop(), &proc_stdin, 0);
      write_req.data = &write_data;
      proc_stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
      proc_stdio[0].data.stream = (uv_stream_t *)&proc_stdin;
    }

    if (opts & kShellOptRead) {
      // Read from the process stdout into the current buffer
      uv_pipe_init(uv_default_loop(), &proc_stdout, 0);
      proc_stdout.data = &read_data;
      proc_stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
      proc_stdio[1].data.stream = (uv_stream_t *)&proc_stdout;
      ga_init(&read_data.ga, 1, READ_BUFFER_LENGTH);
    }
  }

  if (uv_spawn(uv_default_loop(), &proc, &proc_opts)) {
    if (!emsg_silent) {
      MSG_PUTS(_("\nCannot execute shell "));
      msg_outtrans(p_sh);
      msg_putchar('\n');
    }

    return proc_cleanup_exit(&proc_data, &proc_opts, opts);
  }

  // Assign the flag address after `proc` is initialized by `uv_spawn`
  proc.data = &proc_data;

  if (opts & kShellOptWrite) {
    write_selection(&write_req);
    expected_exits++;
  }

  if (opts & kShellOptRead) {
    uv_read_start((uv_stream_t *)&proc_stdout, alloc_cb, read_cb);
    expected_exits++;
  }

  while (proc_data.exited < expected_exits) {
    uv_run(uv_default_loop(), UV_RUN_ONCE);

    if (got_int) {
      uv_process_kill(&proc, SIGINT);
      got_int = FALSE;
    }
  }

  if (opts & kShellOptRead) {
    if (read_data.ga.ga_len > 0) {
      append_ga_line(&read_data.ga);
      /* remember that the NL was missing */
      curbuf->b_no_eol_lnum = curwin->w_cursor.lnum;
    } else
      curbuf->b_no_eol_lnum = 0;
    ga_clear(&read_data.ga);
  }

  free(write_data.buffer);

  return proc_cleanup_exit(&proc_data, &proc_opts, opts);
}

static int tokenize(char_u *str, char **argv)
{
  int argc = 0, len;
  char_u *p = str;

  while (*p != NUL) {
    len = word_length(p);

    if (argv != NULL) {
      // Fill the slot
      argv[argc] = xmalloc(len + 1);
      memcpy(argv[argc], p, len);
      argv[argc][len] = NUL;
    }

    argc++;
    p += len;
    p = skipwhite(p);
  }

  return argc;
}

static int word_length(char_u *str)
{
  char_u *p = str;
  bool inquote = false;
  int length = 0;

  // Move `p` to the end of shell word by advancing the pointer while it's
  // inside a quote or it's a non-whitespace character
  while (*p && (inquote || (*p != ' ' && *p != TAB))) {
    if (*p == '"') {
      // Found a quote character, switch the `inquote` flag
      inquote = !inquote;
    }

    p++;
    length++;
  }

  return length;
}

static void write_selection(uv_write_t *req)
{
  ShellWriteData *data = (ShellWriteData *)req->data;
  // TODO use a static buffer for up to a limit(eg 4096 bytes) and only
  // after that allocate memory
  int buflen = 4096;
  data->buffer = (char *)xmalloc(buflen);
  uv_buf_t uvbuf;
  linenr_T lnum = curbuf->b_op_start.lnum;
  int off = 0;
  int written = 0;
  char_u      *lp = ml_get(lnum);
  int l;
  int len;

  for (;; ) {
    l = STRLEN(lp + written);
    if (l == 0) {
      len = 0;
    } else if (lp[written] == NL) {
      /* NL -> NUL translation */
      len = 1;
      if (off + len >= buflen) {
        buflen *= 2;
        data->buffer = xrealloc(data->buffer, buflen);
      }
      data->buffer[off++] = NUL;
    } else {
      char_u  *s = vim_strchr(lp + written, NL);
      len = s == NULL ? l : s - (lp + written);
      while (off + len >= buflen) {
        buflen *= 2;
        data->buffer = xrealloc(data->buffer, buflen);
      }
      memcpy(data->buffer + off, lp + written, len);
      off += len;
    }
    if (len == l) {
      /* Finished a line, add a NL, unless this line
       * should not have one. */
      if (lnum != curbuf->b_op_end.lnum
          || !curbuf->b_p_bin
          || (lnum != curbuf->b_no_eol_lnum
            && (lnum !=
              curbuf->b_ml.ml_line_count
              || curbuf->b_p_eol))) {
        if (off + 1 >= buflen) {
          buflen *= 2;
          data->buffer = xrealloc(data->buffer, buflen);
        }
        data->buffer[off++] = NL;
      }
      ++lnum;
      if (lnum > curbuf->b_op_end.lnum) {
        break;
      }
      lp = ml_get(lnum);
      written = 0;
    } else if (len > 0)
      written += len;
  }

  uvbuf.base = data->buffer;
  uvbuf.len = off;

  uv_write(req, data->shell_stdin, &uvbuf, 1, write_cb);
}

static void alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
  ShellReadData *data = (ShellReadData *)handle->data;

  if (data->reading) {
    buf->len = 0;
    return;
  }

  buf->base = data->readbuf;
  buf->len = READ_BUFFER_LENGTH;
  data->reading = true;
}

static void read_cb(uv_stream_t *stream, ssize_t cnt, const uv_buf_t *buf)
{
  int i;
  ShellReadData *data = (ShellReadData *)stream->data;

  if (cnt <= 0) {
    if (cnt != UV_ENOBUFS) {
      uv_read_stop(stream);
      uv_close((uv_handle_t *)stream, NULL);
      data->proc_data->exited++;
    }
    return;
  }

  for (i = 0; i < cnt; ++i) {
    if (data->readbuf[i] == NL) {
      // Insert the line
      append_ga_line(&data->ga);
    } else if (data->readbuf[i] == NUL) {
      // Translate NUL to NL
      ga_append(&data->ga, NL);
    } else {
      // buffer data into the grow array
      ga_append(&data->ga, data->readbuf[i]);
    }
  }

  windgoto(msg_row, msg_col);
  cursor_on();
  out_flush();

  data->reading = false;
}

static void write_cb(uv_write_t *req, int status)
{
  ShellWriteData *data = (ShellWriteData *)req->data;
  uv_close((uv_handle_t *)data->shell_stdin, NULL);
  data->proc_data->exited++;
}

static int proc_cleanup_exit(ProcessData *proc_data,
                             uv_process_options_t *proc_opts,
                             int shellopts)
{

  if (proc_data->exited) {
    if (!emsg_silent && proc_data->exit_status != 0 &&
        !(shellopts & kShellOptSilent)) {
      MSG_PUTS(_("\nshell returned "));
      msg_outnum((long)proc_data->exit_status);
      msg_putchar('\n');
    }
  }

  State = proc_data->old_state;

  if (proc_data->old_mode == TMODE_RAW) {
    // restore mode
    settmode(TMODE_RAW);
  }

  signal_accept_deadly();

  // Release argv memory
  shell_free_argv(proc_opts->args);

  return proc_data->exit_status;
}

static void exit_cb(uv_process_t *proc, int64_t status, int term_signal)
{
  ProcessData *data = (ProcessData *)proc->data;
  data->exited++;
  data->exit_status = status;
}
