#include <string.h>
#include "nvim/queue.h"
#include "queue.h"


Queue *ut_queue_new(Queue *queue)
{
  Queue *rv = malloc(sizeof(Queue));
  *rv = queue_init(queue);
  return rv;
}

void ut_queue_free(Queue *queue)
{
  queue_destroy(queue);
  free(queue);
}

void ut_queue_push(Queue *queue, const char *str)
{
  queue_push_callback(queue, NULL, 1, strdup(str));
}

const char *ut_queue_remove(Queue *queue)
{
  static char buf[1024];
  QueueItem item = queue_poll(queue, 0);
  strcpy(buf, item.data.callback.argv[0]);
  free(item.data.callback.argv[0]);
  return buf;
}
