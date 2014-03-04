#ifndef NEOVIM_OS_H
#define NEOVIM_OS_H

#include "../vim.h"

void io_init();
void mch_exit(int);
int mch_inchar(char_u *, int, long, int);
int mch_char_avail(void);
void mch_delay(long, int);
void mch_breakcheck(void);
long_u mch_total_mem(int);
int mch_chdir(char *);
long_u mch_total_mem(int special);
int mch_chdir(char *path);
int mch_dirname(char_u *buf, int len);
int mch_FullName (char_u *fname, char_u *buf, int len, int force);
int mch_isFullName (char_u *fname);

#endif
