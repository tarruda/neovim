#include <string.h>
#include <stdbool.h>

#include <uv.h>

#include "os/shell.h"
#include "os/signal.h"
#include "types.h"
#include "vim.h"
#include "ascii.h"
#include "term.h"
#include "misc2.h"
#include "memline.h"
#include "option_defs.h"
#include "charset.h"

typedef struct {
  uint32_t lnum;
  uv_stream_t *shell_stdin;
} ShellWriteData;

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

static void write_line_to_child(uv_write_t *req);

static int proc_cleanup_exit(int r, int mode, uv_process_options_t *opts);

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
  int status, old_state, tmode = cur_tmode;
  bool exited = false;
  ShellWriteData write_data = {
    .lnum = 0,
    .shell_stdin = (uv_stream_t *)&proc_stdin
  };

  out_flush();
  if (opts & kShellOptCooked) {
    // set to normal mode
    settmode(TMODE_COOK);
  }

  proc.data = &exited;
  // While the child is running, ignore terminating signals
  // TODO

  // Create argv for `uv_spawn`
  proc_opts.args = shell_build_argv(cmd, extra_shell_arg);
  proc_opts.file = proc_opts.args[0];
  proc_opts.exit_cb = exit_cb;
  // Initialize libuv structures 
  proc_opts.stdio = proc_stdio;
  proc_opts.stdio_count = 3;

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
    old_state = State;
    State = EXTERNCMD;

    if (opts & kShellOptWrite) {
      // Write from the current buffer into the process stdin
      uv_pipe_init(uv_default_loop(), &proc_stdin, 0);
      // Save state necessary for the write-related functions
      write_req.data = &write_data;
      proc_stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
      proc_stdio[0].data.stream = (uv_stream_t *)&proc_stdin;
    }

    if (opts & kShellOptRead) {
      uv_pipe_init(uv_default_loop(), &proc_stdout, 0);
      // Read from the process stdout into the current buffer
      proc_stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
      proc_stdio[1].data.stream = (uv_stream_t *)&proc_stdout;
    }
  }

  status = uv_spawn(uv_default_loop(), &proc, &proc_opts);

  if (status) {
    // TODO Log error
    return proc_cleanup_exit(status, tmode, &proc_opts);
  }

  if (opts & kShellOptWrite) {
    write_line_to_child(&write_req);
  }

  if (opts & kShellOptRead) {
    uv_read_start((uv_stream_t *)&proc_stdout, alloc_cb, read_cb);
  }

  while (!exited) {
    uv_run(uv_default_loop(), UV_RUN_ONCE);
  }

  return proc_cleanup_exit(status, tmode, &proc_opts);
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

static void write_line_to_child(uv_write_t *req)
{
  char_u *line_ptr;
  size_t line_len;
  uv_buf_t bufs[] = {
    {.base = NULL, .len = 0},
    {.base = "\n", .len = 1}
  };
  uint32_t nbufs = 1;
  ShellWriteData *data = (ShellWriteData *)req->data;

  if (!data->lnum) {
    // Get the first line number that must be written to the child
    data->lnum = curbuf->b_op_start.lnum;
  }
 
  // pointer to the line
  line_ptr = ml_get(data->lnum);
  // line length
  line_len = strlen((char *)line_ptr);
  // prepare the buffer
  bufs[0].base = (char *)line_ptr;
  bufs[0].len = line_len;
  // Append a NL if this line should have one
  if (data->lnum != curbuf->b_op_end.lnum
      || !curbuf->b_p_bin
      || (data->lnum != curbuf->b_no_eol_lnum
        && (data->lnum !=
          curbuf->b_ml.ml_line_count
          || curbuf->b_p_eol))) {
    nbufs++;
  }

  uv_write(req, data->shell_stdin, bufs, nbufs, write_cb);
}

static void alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
}

static void read_cb(uv_stream_t *stream, ssize_t cnt, const uv_buf_t *buf)
{

}

static void write_cb(uv_write_t *req, int status)
{
  ShellWriteData *data = (ShellWriteData *)req->data;
  data->lnum++;

  if (data->lnum > curbuf->b_op_end.lnum) {
    // finished all the lines, close the stream
    uv_close((uv_handle_t *)req, NULL);
    uv_close((uv_handle_t *)data->shell_stdin, NULL);
    return;
  }

  // Write next line
  write_line_to_child(req);
}

static int proc_cleanup_exit(int r, int mode, uv_process_options_t *opts)
{
  shell_free_argv(opts->args);

  if (mode == TMODE_RAW) {
    // restore mode
    settmode(TMODE_RAW);
  }

  return r;
}

static void exit_cb(uv_process_t *proc, int64_t status, int term_signal)
{
  *(bool *)proc->data = true;

}
