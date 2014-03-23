#ifndef NEOVIM_OS_IO_H
#define NEOVIM_OS_IO_H

#include <stdbool.h>

#include "types.h"

int mch_inchar(char_u *, int, long, int);
bool mch_char_avail(void);
void mch_breakcheck(void);
uint32_t io_read(char *buf, uint32_t count);

#endif

