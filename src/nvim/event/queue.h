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

#define VA_EVENT_INIT(event, cb, argc)                  \
  do {                                                  \
    assert(argc <= EVENT_HANDLER_MAX_ARGC);             \
    (event)->handler = cb;                                 \
    if (argc) {                                         \
      va_list args;                                     \
      va_start(args, argc);                             \
      for (int i = 0; i < argc; i++) {                  \
        (event)->argv[i] = va_arg(args, void *);           \
      }                                                 \
      va_end(args);                                     \
    }                                                   \
  } while (0)

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "event/queue.h.generated.h"
#endif
#endif  // NVIM_EVENT_QUEUE_H
