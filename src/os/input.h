#ifndef NEOVIM_OS_INPUT_H
#define NEOVIM_OS_INPUT_H

#include "types.h"

int mch_inchar(char_u *, int, long, int);
int mch_char_avail(void);
void mch_breakcheck(void);

#endif

