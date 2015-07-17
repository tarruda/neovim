// Main data structure for communication between threads. This is like most
// thread-safe blocking queues implementations, but it also allows a queue to
// have an associated parent queue. The following properties apply regarding
// this parent/child relationship:
//
// - pushing a node to a child queue will push a corresponding link node to the
//   parent queue
// - removing a link node from a parent queue will remove the next node
//   in the linked child queue
// - removing a node from a child queue will remove the corresponding link node
//   in the parent queue
//
// These properties allow neovim to organize and process events from different
// sources with a certain degree of control. Here's how the queue is used:
//
//                         +----------------+
//                         |   Main loop    |
//                         +----------------+
//                                  ^
//                                  |
//                         +----------------+
//         +-------------->|   Event loop   |<------------+
//         |               +--+-------------+             |
//         |                  ^           ^               |
//         |                  |           |               |
//    +-----------+   +-----------+    +---------+    +---------+
//    | Channel 1 |   | Channel 2 |    |  Job 1  |    |  Job 2  |
//    +-----------+   +-----------+    +---------+    +---------+
//
//
// In the above diagram, the lower boxes represents event emitters, each with
// it's own private queue that has the event loop queue as the parent.
//
// When idle, the main loop polls the event loop, which is running in another
// thread and receives events from many sources(channels, jobs, user...). Each
// event emitter pushes events to its own private queue which is propagated to
// the event loop queue. When the main loop consumes events from the event loop,
// the corresponding events are removed from the respective emitters.
//
// The reason we have this queue hierarchy is to allow focusing on a single
// event emitter while blocking the main loop. For example, if the `jobwait`
// vimscript function is called on job1, the main loop will temporarily stop
// polling the event loop queue and poll job1 queue instead. Same with channels,
// when calling `rpcrequest`, we want to temporarily stop processing events from
// other sources and focus on a specific channel.


#ifndef NVIM_QUEUE_H
#define NVIM_QUEUE_H

#include <stdbool.h>

#include <uv.h>

#include "nvim/lib/klist.h"

#define CROSS_THREAD_CB_MAX_ARGC 4

typedef void (*argv_callback)(void **argv);

typedef struct cross_thread_callback {
  argv_callback cb;
  void *argv[CROSS_THREAD_CB_MAX_ARGC];
} CrossThreadCallback;

typedef struct queue Queue;
typedef struct queue_item {
  union {
    Queue *queue;
    CrossThreadCallback callback;
  } data;
  void *parent_ptr;
  bool link, nil;  // this is just a link to a node in a child queue
} QueueItem;

#define _noop(x)
KLIST_INIT(QueueItem, QueueItem, _noop)

struct queue {
  uv_mutex_t mutex;
  uv_cond_t cond;
  klist_t(QueueItem) *items;
  Queue *parent;
  kliter_t(QueueItem) **next;
};

static inline Queue queue_init(Queue *parent)
{
  Queue rv;
  uv_mutex_init(&rv.mutex);
  uv_cond_init(&rv.cond);
  rv.items = kl_init(QueueItem);
  rv.parent = parent;
  rv.next = &rv.items->head;
  return rv;
}

static inline void queue_destroy(Queue *queue)
{
  kl_destroy(QueueItem, queue->items);
  uv_cond_destroy(&queue->cond);
  uv_mutex_destroy(&queue->mutex);
}


#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "queue.h.generated.h"
#endif
#endif  // NVIM_QUEUE_H
