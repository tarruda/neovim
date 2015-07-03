#include <assert.h>
#include <stdbool.h>

#include <uv.h>

#include "nvim/ascii.h"
#include "nvim/vim.h"
#include "nvim/globals.h"
#include "nvim/memline.h"
#include "nvim/eval.h"
#include "nvim/memory.h"
#include "nvim/misc1.h"
#include "nvim/misc2.h"
#include "nvim/os/signal.h"
#include "nvim/os/event.h"

static SignalWatcher spipe, shup, squit, sterm;
#ifdef SIGPWR
static SignalWatcher spwr;
#endif

static bool rejecting_deadly;

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/signal.c.generated.h"
#endif

void signal_init(void)
{
  signal_watcher_init(&spipe, NULL);
  signal_watcher_init(&shup, NULL);
  signal_watcher_init(&squit, NULL);
  signal_watcher_init(&sterm, NULL);
  signal_watcher_start(&spipe, signal_cb, SIGPIPE);
  signal_watcher_start(&shup, signal_cb, SIGHUP);
  signal_watcher_start(&squit, signal_cb, SIGQUIT);
  signal_watcher_start(&sterm, signal_cb, SIGTERM);
#ifdef SIGPWR
  signal_watcher_init(&spwr, NULL);
  signal_watcher_start(&spwr, signal_cb, SIGPWR);
#endif
}

void signal_teardown(void)
{
  signal_stop();
  event_close_handle((uv_handle_t *)&spipe.uv, NULL);
  event_close_handle((uv_handle_t *)&shup.uv, NULL);
  event_close_handle((uv_handle_t *)&squit.uv, NULL);
  event_close_handle((uv_handle_t *)&sterm.uv, NULL);
#ifdef SIGPWR
  event_close_handle((uv_handle_t *)&spwr.uv, NULL);
#endif
}

void signal_stop(void)
{
  signal_watcher_stop(&spipe);
  signal_watcher_stop(&shup);
  signal_watcher_stop(&squit);
  signal_watcher_stop(&sterm);
#ifdef SIGPWR
  signal_watcher_stop(&spwr);
#endif
}

void signal_reject_deadly(void)
{
  rejecting_deadly = true;
}

void signal_accept_deadly(void)
{
  rejecting_deadly = false;
}

void signal_watcher_init(SignalWatcher *watcher, void *data)
{
  event_call_async(signal_watcher_init_async, 2, watcher, data);
}

void signal_watcher_start(SignalWatcher *watcher, signal_event_handler cb,
    int signum)
{
  watcher->signum = signum;
  watcher->cb = cb;
  event_call_async(signal_watcher_start_async, 1, watcher);
}

void signal_watcher_stop(SignalWatcher *watcher)
{
  event_call_async(signal_watcher_stop_async, 1, watcher);
}

void signal_watcher_close(SignalWatcher *watcher, event_handler cb)
{
  event_close_handle((uv_handle_t *)&watcher->uv, cb);
}

static void signal_watcher_init_async(void **argv)
{
  SignalWatcher *watcher = argv[0];
  watcher->data = argv[1];
  watcher->cb = NULL;
  uv_signal_init(uv_default_loop(), &watcher->uv);
}

static void signal_watcher_cb(uv_signal_t *handle, int signum)
{
  SignalWatcher *watcher = handle->data;
  watcher->cb(watcher, signum, watcher->data);
}

static void signal_watcher_start_async(void **argv)
{
  SignalWatcher *watcher = argv[0];
  watcher->uv.data = watcher;
  uv_signal_start(&watcher->uv, signal_watcher_cb, watcher->signum);
}

static void signal_watcher_stop_async(void **argv)
{
  SignalWatcher *watcher = argv[0];
  uv_signal_stop(&watcher->uv);
}


static char * signal_name(int signum)
{
  switch (signum) {
#ifdef SIGPWR
    case SIGPWR:
      return "SIGPWR";
#endif
    case SIGPIPE:
      return "SIGPIPE";
    case SIGTERM:
      return "SIGTERM";
    case SIGQUIT:
      return "SIGQUIT";
    case SIGHUP:
      return "SIGHUP";
    default:
      return "Unknown";
  }
}

// This function handles deadly signals.
// It tries to preserve any swap files and exit properly.
// (partly from Elvis).
// NOTE: Avoid unsafe functions, such as allocating memory, they can result in
// a deadlock.
static void deadly_signal(int signum)
{
  // Set the v:dying variable.
  set_vim_var_nr(VV_DYING, 1);

  snprintf((char *)IObuff, sizeof(IObuff), "Vim: Caught deadly signal '%s'\n",
      signal_name(signum));

  // Preserve files and exit.
  preserve_exit();
}

static void signal_cb(SignalWatcher *watcher, int signum, void *data)
{
  assert(signum >= 0);

  switch (signum) {
#ifdef SIGPWR
    case SIGPWR:
      // Could be a power failure(eg batteries low), flush the swap files to be
      // safe
      ml_sync_all(false, false);
      break;
#endif
    case SIGPIPE:
      // Ignore
      break;
    case SIGTERM:
    case SIGQUIT:
    case SIGHUP:
      if (!rejecting_deadly) {
        deadly_signal(signum);
      }
      break;
    default:
      fprintf(stderr, "Invalid signal %d", signum);
      break;
  }
}
