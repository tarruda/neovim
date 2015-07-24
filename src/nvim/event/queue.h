#ifndef NVIM_EVENT_QUEUE_H
#define NVIM_EVENT_QUEUE_H

#include <uv.h>

#include "nvim/lib/queue.h"

#define EVENT_HANDLER_MAX_ARGC 4

typedef void (*argv_callback)(void **argv);
typedef struct message {
  argv_callback handler;
  void *argv[EVENT_HANDLER_MAX_ARGC];
} Event;

typedef struct queue Queue;
typedef void (*put_callback)(Queue *queue, void *data);

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "event/queue.h.generated.h"
#endif
#endif  // NVIM_EVENT_QUEUE_H
