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
#include "option_defs.h"
#include "charset.h"

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
  int status;
  int tmode = cur_tmode;

  out_flush();
  if (opts & kShellOptCooked) {
    // set to normal mode
    settmode(TMODE_COOK);
  }

  // While the child is running, ignore terminating signals
  signal_reject_deadly();

  // Create argv for `uv_spawn`
  proc_opts.args = shell_build_argv(cmd, extra_shell_arg);

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

    if (opts & kShellOptWrite) {
      uv_pipe_init(uv_default_loop(), &proc_stdin, 0);
      // Write from the current buffer into the process stdin
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

  if (!status) {
    // run the event loop until the shell exits

  } else {
    // TODO Log error
  }

  if (tmode == TMODE_RAW) {
    // restore mode
    settmode(TMODE_RAW);
  }

  // Release acquired memory
  shell_free_argv(proc_opts.args);

  return status;
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
