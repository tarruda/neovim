#ifndef NVIM_UI_H
#define NVIM_UI_H

#include <stdbool.h>
#include <stdlib.h>

#include "nvim/vim.h"
#include "nvim/buffer_defs.h"
#include "nvim/api/private/defs.h"

typedef struct {
  bool bold, standout, underline, undercurl, italic, reverse;
  int foreground, background;
} HlAttrs;

typedef struct ui_t UI;

struct ui_t {
  void (*resize)(int rows, int columns);
  void (*clear)(void);
  void (*eol_clear)(void);
  void (*cursor_goto)(int row, int col);
  void (*ui_cursor_on)(void);
  void (*ui_cursor_off)(void);
  void (*mouse_on)(void);
  void (*mouse_off)(void);
  void (*insert_mode)(void);
  void (*normal_mode)(void);
  void (*set_scroll_region)(int top, int bot, int left, int right);
  void (*scroll_down)(int count);
  void (*scroll_up)(int count);
  void (*highlight_set)(HlAttrs attrs);
  void (*put)(uint8_t *cell, size_t len);
  void (*bell)(void);
  void (*visual_bell)(void);
  void (*flush)(void);
  void (*suspend)(void);
  void (*set_title)(char *title);
  void (*set_icon)(char *icon);
  bool (*setup)(void);
  void (*teardown)(void);
};

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "ui.h.generated.h"
#endif
#endif  // NVIM_UI_H
