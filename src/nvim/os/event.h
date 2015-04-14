#ifndef NVIM_OS_EVENT_H
#define NVIM_OS_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <uv.h>

#include "nvim/os/job_defs.h"
#include "nvim/os/time.h"
#include "nvim/os/time.h"

typedef struct event_filter EventFilter;

typedef void (*event_handler)(void *event_data);
typedef void (*signal_event_handler)(int signum, void *event_data);
typedef bool (*event_filter_cb)(void *event_data, void *filter_data);
typedef void (*async_callback)(void **argv);

struct event_filter {
  event_filter_cb predicate;
  void *data;
};

typedef struct timer {
  uv_timer_t uv;
  void *data;
  event_handler cb;
} Timer;

typedef struct signal {
  uv_signal_t uv;
  void *data;
  signal_event_handler cb;
} Signal;


// Poll for events until a condition or timeout
#define event_poll_until(timeout, condition, filter)                         \
  do {                                                                       \
    int remaining = timeout;                                                 \
    uint64_t before = (remaining > 0) ? os_hrtime() : 0;                     \
    void *node = NULL;                                                       \
    while (!(condition)) {                                                   \
      node = event_poll(node, remaining, filter);                            \
      if (remaining == 0) {                                                  \
        break;                                                               \
      } else if (remaining > 0) {                                            \
        uint64_t now = os_hrtime();                                          \
        remaining -= (int) ((now - before) / 1000000);                       \
        before = now;                                                        \
        if (remaining <= 0) {                                                \
          break;                                                             \
        }                                                                    \
      }                                                                      \
    }                                                                        \
  } while (0)

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/event.h.generated.h"
#endif

#endif  // NVIM_OS_EVENT_H
