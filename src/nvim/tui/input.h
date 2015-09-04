#ifndef NVIM_TUI_INPUT_H
#define NVIM_TUI_INPUT_H

#include <stdbool.h>

#include <termkey.h>
#include "nvim/event/stream.h"
#include "nvim/event/time.h"

typedef struct term_input {
  int in_fd;
  int timeout;  // time in milliseconds to wait for esc sequences
  bool paste_enabled;
  TermKey *tk;
  TimeWatcher timer_handle;
  Loop *loop;
  Stream read_stream;
} TermInput;

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "tui/input.h.generated.h"
#endif

#endif  // NVIM_TUI_INPUT_H
