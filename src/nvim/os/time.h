#ifndef NVIM_OS_TIME_H
#define NVIM_OS_TIME_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <uv.h>

#include "nvim/os/event.h"

typedef struct time_watcher {
  uv_timer_t uv;
  void *data;
  event_handler cb;
} TimeWatcher;

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/time.h.generated.h"
#endif
#endif  // NVIM_OS_TIME_H
