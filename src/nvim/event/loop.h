#ifndef NVIM_EVENT_LOOP_H
#define NVIM_EVENT_LOOP_H

#include <uv.h>

#include "nvim/lib/klist.h"

#define _noop(x)
typedef void * WatcherPtr;
KLIST_INIT(WatcherPtr, WatcherPtr, _noop)

typedef struct loop {
  uv_loop_t uv;
  klist_t(WatcherPtr) *children;
  uv_signal_t children_watcher;
  uv_timer_t children_kill_timer;
  size_t children_stop_requests;
} Loop;

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "event/loop.h.generated.h"
#endif

#endif  // NVIM_EVENT_LOOP_H
