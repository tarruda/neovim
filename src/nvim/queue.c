#include <assert.h>
#include <stdarg.h>
#include <stdint.h>

#include <uv.h>

#include "nvim/queue.h"
#include "nvim/os/time.h"

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "queue.c.generated.h"
#endif


QueueItem queue_poll(Queue *queue, int timeout)
{
  int64_t remaining = timeout;
  uint64_t before;

  if (remaining > 0) {
    before = os_hrtime();
    remaining = (int64_t)timeout * 1000000;  // convert to nanoseconds
  }

  uv_mutex_t *mutex = queue_mutex(queue);
  uv_cond_t *cond = queue_cond(queue);
  uv_mutex_lock(mutex);

  for (;;) {
    if (!kl_empty(queue->items)) {
      // not empty, exit the wait loop
      break;
    }

    if (timeout < 0) {
      // block until signaled
      uv_cond_wait(cond, mutex);
    } else if (remaining > 0) {
      // block until signaled or timeout
      uv_cond_timedwait(cond, mutex, (uint64_t)remaining);
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

  if (kl_empty(queue->items)) {
    return (QueueItem) {.nil = true};
  }

  QueueItem item = queue_remove(queue);
  uv_mutex_unlock(mutex);
  return item;
}

void queue_push_callback(Queue *queue, argv_callback cb, int argc, ...)
{
  assert(queue->parent);  // don't push directly to the parent queue
  assert(argc <= CROSS_THREAD_CB_MAX_ARGC);
  QueueItem item;
  item.link = false;
  item.nil = false;
  item.data.callback.cb = cb;
  if (argc) {
    va_list args;
    va_start(args, argc);
    for (int i = 0; i < argc; i++) {
      item.data.callback.argv[i] = va_arg(args, void *);
    }
    va_end(args);
  }
  uv_mutex_t *mutex = queue_mutex(queue);
  uv_cond_t *cond = queue_cond(queue);
  uv_mutex_lock(mutex);
  queue_push(queue, item);
  uv_cond_signal(cond);
  uv_mutex_unlock(mutex);
}

static QueueItem queue_remove(Queue *queue)
{
  assert(!kl_empty(queue->items));
  QueueItem item = kl_shift(QueueItem, queue->items);

  if (queue->parent) {
    assert(!kl_empty(queue->parent->items));
    // also remove the corresponding link node in the parent queue
    kliter_t(QueueItem) **parent_ptr = item.parent_ptr;
    kl_shift_at(QueueItem, queue->parent->items, parent_ptr);
    // queue_fix_next_linked_parent_ptr(queue->parent);
  } else {
    // remove the next node in the linked queue
    Queue *linked = item.data.queue;
    assert(!kl_empty(linked->items));
    assert(item.link);
    item = kl_shift(QueueItem, linked->items);
    queue_fix_next_linked_parent_ptr(queue);
  }

  return item;
}

static void queue_push(Queue *queue, QueueItem item)
{
  assert(queue->parent);  // only allow pushing to child queues
  // save a pointer to the next link node in the parent
  item.parent_ptr = queue->parent->next;
  kl_push(QueueItem, queue->items, item);
  queue->parent->next = &(*queue->parent->next)->next;
  // push a link node to the parent
  QueueItem parent_item;
  parent_item.link = true;
  parent_item.nil = false;
  parent_item.data.queue = queue;
  kl_push(QueueItem, queue->parent->items, parent_item);
}

static void queue_fix_next_linked_parent_ptr(Queue *queue)
{
  if (kl_empty(queue->items)) {
    return;
  }
  Queue *linked = queue->items->head->data.data.queue;
  assert(queue->items->head->data.link);
  assert(!kl_empty(linked->items));
  kl_begin(linked->items)->data.parent_ptr = &queue->items->head;
}

// no need to use different mutexes for children, always synchronize using the
// parent queue
static uv_mutex_t *queue_mutex(Queue *queue)
{
  if (queue->parent) {
    return queue_mutex(queue->parent);
  }
  return &queue->mutex;
}

static uv_cond_t *queue_cond(Queue *queue)
{
  if (queue->parent) {
    return queue_cond(queue->parent);
  }
  return &queue->cond;
}

