#ifndef NVIM_TERMINAL_H
#define NVIM_TERMINAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct terminal Terminal;
typedef void (*terminal_write_cb)(void *data, char *buffer, size_t size);
typedef void (*terminal_resize_cb)(void *data, uint16_t width,
    uint16_t height);

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "terminal.h.generated.h"
#endif
#endif  // NVIM_TERMINAL_H
