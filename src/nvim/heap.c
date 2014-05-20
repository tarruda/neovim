
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


void heap_init()
{
}
