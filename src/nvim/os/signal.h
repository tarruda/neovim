#ifndef NVIM_OS_SIGNAL_H
#define NVIM_OS_SIGNAL_H

#include <uv.h>

#include "nvim/os/event.h"

typedef struct signal_watcher SignalWatcher;

typedef void (*signal_event_handler)(SignalWatcher *watcher, int signum,
    void *data);

struct signal_watcher {
  uv_signal_t uv;
  void *data;
  int signum;
  signal_event_handler cb;
};


#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/signal.h.generated.h"
#endif
#endif  // NVIM_OS_SIGNAL_H
