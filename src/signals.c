#include <stdbool.h>
#include <uv.h>

#include "vim.h"
#include "eval.h"
#include "main.h"
#include "misc1.h"
#include "term.h"
#include "os/io.h"


static void handle_deadly(int signum);


char * signal_name(int signum) {
  switch (signum) {
    case SIGINT:
      return "SIGINT";
    case SIGWINCH:
      return "SIGWINCH";
    case SIGTERM:
      return "SIGTERM";
    case SIGABRT:
      return "SIGABRT";
    case SIGQUIT:
      return "SIGQUIT";
    case SIGHUP:
      return "SIGHUP";
    default:
      return "Unknown";
  }
}

void handle_signal() {
  int sig = io_consume_signal();

  switch (sig) {
    case SIGINT:
      got_int = TRUE;
      break;
    case SIGWINCH:
      shell_resized();
      break;
    case SIGTERM:
    case SIGABRT:
    case SIGQUIT:
    case SIGHUP:
      handle_deadly(sig);
      break;
    default:
      fprintf(stderr, "Invalid signal %d", sig);
      break;
  }
}

/*
 * This function handles deadly signals.
 * It tries to preserve any swap files and exit properly.
 * (partly from Elvis).
 * NOTE: Avoid unsafe functions, such as allocating memory, they can result in
 * a deadlock.
 */
static void handle_deadly(int sig) {
  /* Set the v:dying variable. */
  set_vim_var_nr(VV_DYING, 1);

  sprintf((char *)IObuff, "Vim: Caught deadly signal '%s'\n",
      signal_name(sig));

  /* Preserve files and exit.  This sets the really_exiting flag to prevent
   * calling free(). */
  preserve_exit();
}

