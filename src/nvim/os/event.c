#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <uv.h>

#include "nvim/os/event.h"
#include "nvim/os/input.h"
#include "nvim/msgpack_rpc/defs.h"
#include "nvim/msgpack_rpc/channel.h"
#include "nvim/msgpack_rpc/server.h"
#include "nvim/msgpack_rpc/helpers.h"
#include "nvim/os/signal.h"
#include "nvim/os/rstream.h"
#include "nvim/os/wstream.h"
#include "nvim/os/job.h"
#include "nvim/vim.h"
#include "nvim/memory.h"
#include "nvim/misc2.h"
#include "nvim/ui.h"
#include "nvim/screen.h"
#include "nvim/terminal.h"

#include "nvim/lib/klist.h"

typedef struct event {
  void *data;
  event_handler handler;
} Event;

typedef struct async_call {
  void *argv[4];
  async_callback handler;
} AsyncCall;

#define _destroy_event(x)  // do nothing
KLIST_INIT(Event, Event, _destroy_event)

typedef struct queue {
  uv_mutex_t mutex;
  uv_cond_t cond;
  klist_t(Event) *list;
} Queue;

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/event.c.generated.h"
#endif

static uv_thread_t event_thread;
static uv_async_t async_handle;
static uv_prepare_t prepare_handle;
static uv_loop_t *loop = NULL;
static Queue events;
static uv_mutex_t async_mutex;
static uv_cond_t async_cond;
static AsyncCall async_call;

void event_init(void)
{
  loop = uv_default_loop();
  queue_init(&events);
  uv_mutex_init(&async_mutex);
  uv_cond_init(&async_cond);
  int io_thread_status = uv_thread_create(&event_thread, run, loop);
  if (io_thread_status < 0) {
    abort();
  }
  // wait for the event loop to become ready
  event_poll(NULL, -1, NULL);
  // early msgpack-rpc initialization
  msgpack_rpc_init_method_table();
  msgpack_rpc_helpers_init();
  // Initialize input events
  input_init();
  // Timer to wake the event loop if a timeout argument is passed to
  // `event_poll`
  // Signals
  signal_init();
  // Jobs
  job_init();
  // finish mspgack-rpc initialization
  channel_init();
  server_init();
  terminal_init();
}

void event_teardown(void)
{
  input_stop();
  channel_teardown();
  job_teardown();
  server_teardown();
  signal_teardown();
  terminal_teardown();
  process_all_events();
  event_call_async(stop_async, 0);
  uv_thread_join(&event_thread);
}

/// Push an event to the queue.
void event_push(event_handler handler, void *data)
{
  Event event = {
    .data = data,
    .handler = handler
  };
  uv_mutex_lock(&events.mutex);
  kl_push(Event, events.list, event);
  uv_cond_signal(&events.cond);
  uv_mutex_unlock(&events.mutex);
}

/// Process a single event that satisfies `filter`(or any event if `filter` is
/// NULL). If no events are available, block until one is delivered by the event
/// thread. If `timeout` is >= 0 block for at most `timeout` milliseconds.
///
/// Return the next event node, which can be used as the `start` parameter
/// in future calls to skip traversing through previous events that are known
/// to not match the filter.
void *event_poll(void *start, int timeout, EventFilter *filter)
  FUNC_ATTR_NONNULL_ARG(3)
{
  static int recursive = 0;
  if (recursive++) {
    abort();
  }

  int64_t remaining = timeout;
  uint64_t before;

  if (remaining > 0) {
    before = os_hrtime();
    remaining = timeout * 1000000;  // convert to nanoseconds
  }

  uv_mutex_lock(&events.mutex);
  kliter_t(Event) *node = start;

  for (;;) {
    if ((node = find_event_node(node, filter))) {
      // found an event matching the filter
      break;
    }

    if (timeout < 0) {
      // block until signaled
      uv_cond_wait(&events.cond, &events.mutex);
    } else if (remaining > 0) {
      // block until signaled or timeout
      uv_cond_timedwait(&events.cond, &events.mutex, (uint64_t)remaining);
      // adjust the remaining time. remaining <= 0 means a timeout
      uint64_t now = os_hrtime();
      remaining -= (int64_t)(now - before);
      if (remaining <= 0) {
        break;
      }
      before = now;
    } else {
      break;
    }
  }

  Event event = {.handler = NULL};

  if (node) {
    event = kl_shift_at(Event, events.list, &node);
  }

  uv_mutex_unlock(&events.mutex);

  if (event.handler) {
    // Process the event
    event.handler(event.data);
  }

  recursive--;
  // Return the next event node or the last node(which is always empty).  This
  // can be used by the caller to search for events starting at that specific
  // position.
  return node ? node : kl_end(events.list);
}

/// Schedule a function to be called in the event loop thread.
void event_call_async(async_callback handler, int argc, ...)
{
  async_call.handler = handler;
  va_list args;
  va_start(args, argc);
  for (int i = 0; i < argc; i++) {
    async_call.argv[i] = va_arg(args, void *);
  };
  va_end(args);
  uv_mutex_lock(&async_mutex);
  uv_async_send(&async_handle);
  uv_cond_wait(&async_cond, &async_mutex);
  uv_mutex_unlock(&async_mutex);
}

bool event_has_pending(void)
{
  bool rv;
  uv_mutex_lock(&events.mutex);
  rv = events.list->size != 0;
  uv_mutex_unlock(&events.mutex);
  return rv;
}

void event_timer_init(Timer *timer)
{
  event_call_async(event_timer_init_async, 1, timer);
}

void event_timer_start(Timer *timer, event_handler cb, uint64_t timeout,
    uint64_t repeat, void *data)
{
  timer->data = data;
  timer->cb = cb;
  event_call_async(event_timer_start_async, 3, timer, &timeout, &repeat);
}

void event_timer_stop(Timer *timer)
{
  event_call_async(event_timer_stop_async, 1, timer);
}

static void event_timer_init_async(void **argv)
{
  Timer *timer = argv[0];
  uv_timer_init(loop, &timer->uv);
  timer->cb = NULL;
  timer->data = NULL;
  timer->uv.data = timer;
}

static void timer_cb(uv_timer_t *handle)
{
  Timer *timer = handle->data;
  timer->cb(timer->data);
}

static void event_timer_start_async(void **argv)
{
  Timer *timer = argv[0];
  uint64_t *timeout = argv[1];
  uint64_t *repeat = argv[1];
  uv_timer_start(&timer->uv, timer_cb, *timeout, *repeat);
}

static void event_timer_stop_async(void **argv)
{
  Timer *timer = argv[0];
  uv_timer_stop(&timer->uv);

}

void event_signal_init(Signal *signal)
{
  event_call_async(event_signal_init_async, 1, signal);
}

void event_signal_start(Signal *signal, signal_event_handler cb, int signum)
{
  signal->data = &signum;
  signal->cb = cb;
  event_call_async(event_signal_start_async, 2, signal, &signum);
}

void event_signal_stop(Signal *signal)
{
  event_call_async(event_signal_stop_async, 1, signal);
}

static void event_signal_init_async(void **argv)
{
  Signal *signal = argv[0];
  uv_signal_init(loop, &signal->uv);
  signal->cb = NULL;
  signal->data = NULL;
  signal->uv.data = signal;
}

static void signal_cb(uv_signal_t *handle, int signum)
{
  Signal *signal = handle->data;
  signal->cb(signum, signal->data);
}

static void event_signal_start_async(void **argv)
{
  Signal *signal = argv[0];
  int *signum = argv[1];
  uv_signal_start(&signal->uv, signal_cb, *signum);
}

static void event_signal_stop_async(void **argv)
{
  Signal *signal = argv[0];
  uv_signal_stop(&signal->uv);
  uv_close((uv_handle_t *)&signal->uv, NULL);
}

static void process_all_events(void)
{
  while (!kl_empty(events.list)) {
    event_poll(NULL, 0, NULL);
  }
}

static kliter_t(Event) *find_event_node(kliter_t(Event) *start,
    EventFilter *filter)
{
  kliter_t(Event) *rv = NULL;

  kl_iter_at(Event, events.list, node, start) {
    if (!filter || filter->predicate(kl_val(node).data, filter->data)) {
      rv = node;
      break;
    }
  }

  return rv;
}

static void async_cb(uv_async_t *handle)
{
  uv_mutex_lock(&async_mutex);
  async_call.handler(async_call.argv);
  uv_cond_signal(&async_cond);
  uv_mutex_unlock(&async_mutex);
}

static void queue_init(Queue *queue)
{
  uv_mutex_init(&queue->mutex);
  uv_cond_init(&queue->cond);
  queue->list = kl_init(Event);
}

static void prepare_cb(uv_prepare_t *handle)
{
  event_push(NULL, NULL);
  uv_prepare_stop(handle);
  uv_close((uv_handle_t *)handle, NULL);
}

static void run(void *loop)
{
  uv_async_init(loop, &async_handle, async_cb);
  uv_prepare_init(loop, &prepare_handle);
  uv_prepare_start(&prepare_handle, prepare_cb);
  do {
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  } while (uv_loop_close(uv_default_loop()));
}

static void stop_async(void **argv)
{
  uv_close((uv_handle_t *)&async_handle, NULL);
}
