#ifndef NVIM_EVENT_PROCESS_H
#define NVIM_EVENT_PROCESS_H

#include "nvim/event/loop.h"
#include "nvim/event/rstream.h"
#include "nvim/event/wstream.h"

typedef enum {
  kProcessTypeUv,
  kProcessTypePty
} ProcessType;

typedef struct process Process;
typedef void (*process_exit_cb)(Process *proc, int status, void *data);
typedef void (*internal_process_cb)(Process *proc);

struct process {
  ProcessType type;
  Loop *loop;
  void *data;
  int pid, status, refcount;
  // set to the hrtime of when process_stop was called for the process.
  uint64_t stopped_time;
  char **argv;
  Stream *in, *out, *err;
  process_exit_cb cb;
  internal_process_cb internal_exit_cb, internal_close_cb;
  bool closed, term_sent;
  Queue *events;
  // Timeout after the job exits to close the out/err streams. This is used as a
  // simple heuristic that ensures we won't close the streams before receiving
  // all data(Data can still be in the OS buffer after the child exits).
  uint64_t stream_eof_timeout;
  uv_timer_t eof_timer;
};

static inline Process process_init(Loop *loop, ProcessType type, void *data)
{
  return (Process) {
    .type = type,
    .data = data,
    .loop = loop,
    .events = loop->fast_events,
    .pid = 0,
    .status = 0,
    .refcount = 0,
    .stopped_time = 0,
    .argv = NULL,
    .in = NULL,
    .out = NULL,
    .err = NULL,
    .cb = NULL,
    .closed = false,
    .term_sent = false,
    .internal_close_cb = NULL,
    .internal_exit_cb = NULL,
    .stream_eof_timeout = 50
  };
}

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "event/process.h.generated.h"
#endif
#endif  // NVIM_EVENT_PROCESS_H
