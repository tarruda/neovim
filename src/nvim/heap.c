
#include "nvim/vim.h"
#include "nvim/buffer_defs.h"
#include "map.h"

#define HEAP_IMPL(type, name)                                                 \
  type *heap_get_##name(uint64_t key)                                         \
  {                                                                           \
    return map_get(uint64_t)(registered_##name##s, key);                      \
  }                                                                           \
                                                                              \
  void heap_register_##name(type *name)                                       \
  {                                                                           \
    assert(!name->uid);                                                       \
    name->uid = uid++;                                                        \
    map_put(uint64_t)(registered_##name##s, name->uid, name);                 \
  }                                                                           \
                                                                              \
  void heap_unregister_##name(type *name)                                     \
  {                                                                           \
    map_del(uint64_t)(registered_##name##s, name->uid);                       \
  }


static uint64_t uid = 1;
static Map(uint64_t) *registered_buffers = NULL,
                     *registered_windows = NULL,
                     *registered_tabpages = NULL;

void heap_init()
{
  registered_buffers = map_new(uint64_t)();
  registered_windows = map_new(uint64_t)();
  registered_tabpages = map_new(uint64_t)();
}

HEAP_IMPL(buf_T, buffer)
HEAP_IMPL(win_T, window)
HEAP_IMPL(tabpage_T, tabpage)
