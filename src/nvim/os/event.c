#include <assert.h>
#include <setjmp.h>
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
#include "nvim/os/provider.h"
#include "nvim/os/signal.h"
#include "nvim/os/rstream.h"
#include "nvim/os/job.h"
#include "nvim/vim.h"
#include "nvim/memory.h"
#include "nvim/misc2.h"

#include "nvim/lib/klist.h"


#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/event.c.generated.h"
#endif

// event will be cleaned up after it gets processed
#define _destroy_event(x)  // do nothing
KLIST_INIT(Event, Event, _destroy_event)
static klist_t(Event) *pending_events;
// dummy variable to block the compiler from optimizing out the 'gap' array
// from `prepare_event_loop`
void *stack_pin;
// main/loop threads(stack context)
static jmp_buf main_context, loop_context;

void event_init(void)
{
  // Initialize the event queue
  pending_events = kl_init(Event);
  // early msgpack-rpc initialization
  msgpack_rpc_init_method_table();
  msgpack_rpc_helpers_init();
  // Initialize input events
  input_init();
  input_start();
  // Signals
  signal_init();
  // Jobs
  job_init();
  // finish mspgack-rpc initialization
  channel_init();
  server_init();
  // Providers
  provider_init();
  if (!setjmp(main_context)) {
    // Setup stack space for libuv event loop
    prepare_event_loop();
  }
}

void event_teardown(void)
{
  channel_teardown();
  job_teardown();
  server_teardown();
}

// Wait for some event
void event_poll(int ms)
{
  uv_timer_t timer;
  uv_idle_t idle;

  if (ms > 0) {
    uv_timer_init(uv_default_loop(), &timer);
    uv_timer_start(&timer, timer_cb, (uint64_t)ms, 0);
  } else if (ms == 0) {
    uv_idle_init(uv_default_loop(), &idle);
    uv_idle_start(&idle, idle_cb);
  }

  jump(main_context, loop_context);

  if (ms > 0) {
    uv_timer_stop(&timer);
    uv_close((uv_handle_t *)&timer, NULL);
    jump(main_context, loop_context);
  } else if (ms == 0) {
    uv_idle_stop(&idle);
    uv_close((uv_handle_t *)&idle, NULL);
    jump(main_context, loop_context);
  }
}

bool event_has_deferred(void)
{
  return !kl_empty(pending_events);
}

// Queue an event
void event_push(Event event)
{
  *kl_pushp(Event, pending_events) = event;
}


void event_process(void)
{
  Event event;

  while (kl_shift(Event, pending_events, &event) == 0) {
    event.handler(event);
  }
}

static void check_cb(uv_check_t *handle)
{
  uv_prepare_t *prepare = handle->data;
  uv_prepare_init(uv_default_loop(), prepare);
  uv_prepare_start(prepare, prepare_cb);
  uv_check_stop(handle);
  uv_close((uv_handle_t *)handle, NULL);
}

static void prepare_cb(uv_prepare_t *handle)
{
  jump(loop_context, main_context);
}

static void prepare_event_loop(void)
{
  char gap[0xfffff];
  stack_pin = gap;
  jump(loop_context, main_context);
  loop();
  jump(loop_context, main_context);
  abort();  // Should never get here
}

static void jump(jmp_buf from, jmp_buf to)
{
  if (!setjmp(from)) {
    longjmp(to, 1);
  }
}

static void loop(void)
{
  uv_prepare_t prepare;
  uv_check_t check;
  uv_check_init(uv_default_loop(), &check);
  uv_check_start(&check, check_cb);
  check.data = &prepare;

  DLOG("Enter event loop");
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  DLOG("Exit event loop");
}

// Dummy callbacks(can't pass NULL as callback of libuv handles)
static void timer_cb(uv_timer_t *handle) { }
static void idle_cb(uv_idle_t *handle) { }
