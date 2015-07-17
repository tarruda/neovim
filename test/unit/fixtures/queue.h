#include "nvim/queue.h"

Queue *ut_queue_new(Queue *parent);
void ut_queue_free(Queue *queue);
void ut_queue_push(Queue *queue, const char *str);
const char *ut_queue_remove(Queue *queue);
