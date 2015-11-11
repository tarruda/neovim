#ifndef NVIM_UGRID_H
#define NVIM_UGRID_H

#include <stddef.h>
#include <stdbool.h>

typedef struct ucell UCell;
typedef struct ugrid UGrid;

typedef struct {
  bool bold, underline, undercurl, italic, reverse;
  int foreground, background;
} HlAttrs;

#define MAX_MCO        6                 // maximum value for 'maxcombine'

struct ucell {
  char data[6 * MAX_MCO + 1];
  HlAttrs attrs;
};

typedef void* (*ugrid_alloc_fn)(size_t size);
typedef void (*ugrid_free_fn)(void *ptr);

struct ugrid {
  ugrid_alloc_fn alloc_fn;
  ugrid_free_fn free_fn;
  int top, bot, left, right;
  int row, col;
  int bg, fg;
  int width, height;
  HlAttrs attrs;
  UCell **cells;
};

#define EMPTY_ATTRS ((HlAttrs){false, false, false, false, false, -1, -1})

#define UGRID_FOREACH_CELL(grid, top, bot, left, right, code)           \
  do {                                                                  \
    for (int row = top; row <= bot; ++row) {                            \
      UCell *row_cells = (grid)->cells[row];                            \
      for (int col = left; col <= right; ++col) {                       \
        UCell *cell = row_cells + col;                                  \
        (void)(cell);                                                   \
        code;                                                           \
      }                                                                 \
    }                                                                   \
  } while (0)

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "ugrid.h.generated.h"
#endif
#endif  // NVIM_UGRID_H
