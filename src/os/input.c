#include <stdbool.h>


#include "os/input.h"
#include "os/io.h"
#include "vim.h"
#include "getchar.h"
#include "types.h"
#include "fileio.h"
#include "ui.h"


static int cursorhold_key(char_u *buf);
static int signal_key(char_u *buf);


int mch_inchar(char_u *buf, int maxlen, long ms, int tb_change_cnt) {
  poll_result_t result;

  if (ms >= 0) {
    result = io_poll(ms);
  } else {
    result = io_poll(p_ut);
    if (result == POLL_NONE) {
      if (trigger_cursorhold() && maxlen >= 3 &&
        !typebuf_changed(tb_change_cnt)) {
        return cursorhold_key(buf);
      }
    }
  }

  if (result == POLL_EOF) {
    read_error_exit();
    /* Never return */
  }

  if (result == POLL_SIGNAL) {
    return signal_key(buf);
  }

  if (result == POLL_INPUT) {
    return read_from_input_buf(buf, (long)maxlen);
  }

  before_blocking();
  result = io_poll(-1);

  /* If input was put directly in typeahead buffer bail out here. */
  if (typebuf_changed(tb_change_cnt))
    return 0;

  if (result == POLL_SIGNAL) {
    return signal_key(buf);
  }

  return read_from_input_buf(buf, (long)maxlen);
}

int mch_char_avail() {
  return io_poll(0) == POLL_INPUT;
}

/*
 * Check for CTRL-C typed by reading all available characters.
 * In cooked mode we should get SIGINT, no need to check.
 */
void mch_breakcheck() {
  if (curr_tmode == TMODE_RAW && mch_char_avail())
    fill_input_buf(FALSE);
}


static int cursorhold_key(char_u *buf) {
  buf[0] = K_SPECIAL;
  buf[1] = KS_EXTRA;
  buf[2] = KE_CURSORHOLD;
  return 3;
}


static int signal_key(char_u *buf) {
  buf[0] = K_SPECIAL;
  buf[1] = KS_EXTRA;
  buf[2] = KE_SIGNAL;
  return 3;
}
