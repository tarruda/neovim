#include <stdint.h>

#include <uv.h>

#include "nvim/event/loop.h"
#include "nvim/event/process.h"

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "event/loop.c.generated.h"
#endif


void loop_init(Loop *loop, void *data)
{
  uv_loop_init(&loop->uv);
  loop->uv.data = loop;
  loop->children = kl_init(WatcherPtr);
  loop->children_stop_requests = 0;
  uv_signal_init(&loop->uv, &loop->children_watcher);
  uv_timer_init(&loop->uv, &loop->children_kill_timer);
}

void loop_run(Loop *loop)
{
  uv_run(&loop->uv, UV_RUN_DEFAULT);
}

void loop_run_once(Loop *loop)
{
  uv_run(&loop->uv, UV_RUN_ONCE);
}

void loop_run_nowait(Loop *loop)
{
  uv_run(&loop->uv, UV_RUN_NOWAIT);
}

void loop_stop(Loop *loop)
{
  uv_stop(&loop->uv);
}

void loop_close(Loop *loop)
{
  uv_close((uv_handle_t *)&loop->children_watcher, NULL);
  uv_close((uv_handle_t *)&loop->children_kill_timer, NULL);
  do {
    uv_run(&loop->uv, UV_RUN_DEFAULT);
  } while (uv_loop_close(&loop->uv));
}
