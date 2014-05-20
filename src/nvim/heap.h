#ifndef NVIM_OBJECT_H

#define NVIM_OBJECT_H

#include "nvim/vim.h"
#include "nvim/buffer_defs.h"

#define HEAP_DECLS(type, name)                                                \
  type *heap_get_##name(uint64_t key);                                        \
  void heap_register_##name(type *name);                                      \
  void heap_unregister_##name(type *name);

void heap_init(void);
HEAP_DECLS(buf_T, buffer)

#endif  // NVIM_OBJECT_H

