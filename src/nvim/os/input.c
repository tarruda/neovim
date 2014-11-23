#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <uv.h>

#include "nvim/api/private/defs.h"
#include "nvim/os/input.h"
#include "nvim/os/event.h"
#include "nvim/os/rstream_defs.h"
#include "nvim/os/rstream.h"
#include "nvim/ascii.h"
#include "nvim/vim.h"
#include "nvim/ui.h"
#include "nvim/memory.h"
#include "nvim/keymap.h"
#include "nvim/mbyte.h"
#include "nvim/fileio.h"
#include "nvim/ex_cmds2.h"
#include "nvim/getchar.h"
#include "nvim/main.h"
#include "nvim/misc1.h"

#define INPUT_BUFFER_SIZE (0xfff * 4)

typedef enum {
  kInputNone,
  kInputAvail,
  kInputEof
} InbufPollResult;

static RBuffer *input_buffer;
static bool eof = false;

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/input.c.generated.h"
#endif
// Helper function used to push bytes from the 'event' key sequence partially
// between calls to os_inchar when maxlen < 3

void input_init(void)
{
  input_buffer = rbuffer_new(INPUT_BUFFER_SIZE + MAX_KEY_CODE_LEN);
}

// Low level input function.
int os_inchar(uint8_t *buf, int maxlen, int ms, int tb_change_cnt)
{
  if (rbuffer_pending(input_buffer)) {
    return (int)rbuffer_read(input_buffer, (char *)buf, (size_t)maxlen);
  }

  InbufPollResult result;
  if (ms >= 0) {
    if ((result = inbuf_poll(ms)) == kInputNone) {
      return 0;
    }
  } else {
    if ((result = inbuf_poll((int)p_ut)) == kInputNone) {
      if (trigger_cursorhold() && maxlen >= 3
          && !typebuf_changed(tb_change_cnt)) {
        buf[0] = K_SPECIAL;
        buf[1] = KS_EXTRA;
        buf[2] = KE_CURSORHOLD;
        return 3;
      }

      before_blocking();
      result = inbuf_poll(-1);
    }
  }

  // If input was put directly in typeahead buffer bail out here.
  if (typebuf_changed(tb_change_cnt)) {
    return 0;
  }

  if (rbuffer_pending(input_buffer)) {
    // Safe to convert rbuffer_read to int, it will never overflow since we use
    // relatively small buffers.
    return (int)rbuffer_read(input_buffer, (char *)buf, (size_t)maxlen);
  }

  // If there are deferred events, return the keys directly
  if (event_has_deferred()) {
    return push_event_key(buf, maxlen);
  }

  if (result == kInputEof) {
    read_error_exit();
  }

  return 0;
}

// Check if a character is available for reading
bool os_char_avail(void)
{
  return inbuf_poll(0) == kInputAvail;
}

// Check for CTRL-C typed by reading all available characters.
// In cooked mode we should get SIGINT, no need to check.
void os_breakcheck(void)
{
  input_poll(0);
}

/// Return the contents of the input buffer and make it empty. The returned
/// pointer must be passed to `input_buffer_restore()` later.
String input_buffer_save(void)
{
  size_t inbuf_size = rbuffer_pending(input_buffer);
  String rv = {
    .data = xmemdup(rbuffer_read_ptr(input_buffer), inbuf_size),
    .size = inbuf_size
  };
  rbuffer_consumed(input_buffer, inbuf_size);
  return rv;
}

/// Restore the contents of the input buffer and free `str`
void input_buffer_restore(String str)
{
  rbuffer_consumed(input_buffer, rbuffer_pending(input_buffer));
  rbuffer_write(input_buffer, str.data, str.size);
  free(str.data);
}

size_t input_enqueue(String keys)
{
  size_t rv;
  char *ptr = keys.data, *end = ptr + keys.size;

  while (rbuffer_available(input_buffer) >= 6 && ptr < end) {
    int new_size = trans_special((char_u **)&ptr,
                                 (char_u *)rbuffer_write_ptr(input_buffer),
                                 false);
    if (!new_size) {
      // copy the character unmodified
      *rbuffer_write_ptr(input_buffer) = *ptr++;
      new_size = 1;
    }
    // TODO(tarruda): Don't produce past unclosed '<' characters, except if
    // there's a lot of characters after the '<'
    rbuffer_produced(input_buffer, (size_t)new_size);
  }

  rv = (size_t)(ptr - keys.data);
  process_interrupts();
  return rv;
}

void input_done(void)
{
  eof = true;
}

static bool input_poll(int ms)
{
  if (do_profiling == PROF_YES && ms) {
    prof_inchar_enter();
  }

  event_poll_until(ms, input_ready());

  if (do_profiling == PROF_YES && ms) {
    prof_inchar_exit();
  }

  return input_ready();
}

// This is a replacement for the old `WaitForChar` function in os_unix.c
static InbufPollResult inbuf_poll(int ms)
{
  if (input_ready() || input_poll(ms)) {
    return kInputAvail;
  }

  return eof ? kInputEof : kInputNone;
}

static void process_interrupts(void)
{
  if (mapped_ctrl_c) {
    return;
  }

  char *inbuf = rbuffer_read_ptr(input_buffer);
  size_t count = rbuffer_pending(input_buffer), consume_count = 0;

  for (int i = (int)count - 1; i >= 0; i--) {
    if (inbuf[i] == 3) {
      got_int = true;
      consume_count = (size_t)i;
      break;
    }
  }

  if (got_int) {
    // Remove everything typed before the CTRL-C
    rbuffer_consumed(input_buffer, consume_count);
  }
}

static int push_event_key(uint8_t *buf, int maxlen)
{
  static const uint8_t key[3] = { K_SPECIAL, KS_EXTRA, KE_EVENT };
  static int key_idx = 0;
  int buf_idx = 0;

  do {
    buf[buf_idx++] = key[key_idx++];
    key_idx %= 3;
  } while (key_idx > 0 && buf_idx < maxlen);

  return buf_idx;
}

// Check if there's pending input
static bool input_ready(void)
{
  return typebuf_was_filled ||                 // API call filled typeahead
         rbuffer_pending(input_buffer) > 0 ||  // Stdin input
         event_has_deferred();                 // Events must be processed
}

// Exit because of an input read error.
static void read_error_exit(void)
{
  if (silent_mode)      /* Normal way to exit for "ex -s" */
    getout(0);
  STRCPY(IObuff, _("Vim: Error reading input, exiting...\n"));
  preserve_exit();
}
