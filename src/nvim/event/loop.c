#include <stdint.h>

#include <uv.h>

#include "nvim/event/loop.h"

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "event/loop.c.generated.h"
#endif


void loop_init(Loop *loop, void *data)
{
  uv_loop_init(&loop->uv);
  loop->uv.data = loop;
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
  do {
    uv_run(&loop->uv, UV_RUN_DEFAULT);
  } while (uv_loop_close(&loop->uv));
}
