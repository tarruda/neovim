#ifndef NVIM_MEMORY_H
#define NVIM_MEMORY_H

#include <stdint.h>
#include <stddef.h>  // for size_t

#define MEMORY_ERROR(msg)                                      \
  do {                                                         \
    ui_print((uint8_t *)msg);                                  \
    ui_print((uint8_t *)"\n");                                 \
    preserve_exit();                                           \
  } while(0)

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "memory.h.generated.h"
#endif
#endif  // NVIM_MEMORY_H
