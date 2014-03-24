#include <uv.h>

#include "os/io.h"
#include "vim.h"
#include "misc2.h"
#include "fileio.h"
#include "getchar.h"
#include "globals.h"
#include "ui.h"

#define INPUT_BUFFER_LENGTH 4096

typedef enum {
  kPollNone,
  kPollInput,
  kPollEof
} PollResult;

typedef struct {
  uv_buf_t uvbuf;
  uint32_t rpos, wpos, apos, fpos;
  char_u data[INPUT_BUFFER_LENGTH];
} InputBuffer;

static uv_stream_t *read_stream;
static uv_fs_t read_req;
static uv_timer_t timer_req;
static uv_handle_type read_stream_type;
static InputBuffer in_buffer;
static bool initialized = false, eof = false;
static PollResult io_poll(int32_t ms);
static PollResult inbuf_poll(int32_t ms);
static void initialize_event_loop(void);
static void alloc_cb(uv_handle_t *, size_t, uv_buf_t *);
static void read_cb(uv_stream_t *, ssize_t, const uv_buf_t *);
static void fread_cb(uv_fs_t *);
static void timer_cb(uv_timer_t *handle, int);
static void relocate(void);

int mch_inchar(char_u *buf, int maxlen, long ms, int tb_change_cnt)
{
  PollResult result;

  if (ms >= 0) {
    if ((result = inbuf_poll(ms)) != kPollInput) {
      return 0;
    }
  } else {
    if ((result = inbuf_poll(p_ut)) != kPollInput) {
      if (trigger_cursorhold() && maxlen >= 3 &&
          !typebuf_changed(tb_change_cnt)) {
        buf[0] = K_SPECIAL;
        buf[1] = KS_EXTRA;
        buf[2] = KE_CURSORHOLD;
        return 3;
      }

      before_blocking();
      result = inbuf_poll(-1);
    }
  }

  /* If input was put directly in typeahead buffer bail out here. */
  if (typebuf_changed(tb_change_cnt))
    return 0;

  if (result == kPollEof) {
    read_error_exit();
    return 0;
  }

  return read_from_input_buf(buf, (long)maxlen);
}

bool mch_char_avail()
{
  return inbuf_poll(0) == kPollInput;
}

/*
 * Check for CTRL-C typed by reading all available characters.
 * In cooked mode we should get SIGINT, no need to check.
 */
void mch_breakcheck()
{
  if (curr_tmode == TMODE_RAW && in_buffer.rpos < in_buffer.wpos)
    fill_input_buf(FALSE);
}

/* Copies what was read from `read_cmd_fd` */
uint32_t io_read(char *buf, uint32_t count)
{
  uint32_t rv = 0;

  /* Copy at most 'count' to the buffer argument */
  while (in_buffer.rpos < in_buffer.wpos && rv < count)
    buf[rv++] = in_buffer.data[in_buffer.rpos++];

  return rv;
}

/* This is a replacement for the old `WaitForChar` function in os_unix.c */
static PollResult inbuf_poll(int32_t ms)
{
  if (input_available())
    return kPollInput;

  return io_poll(ms);
}

/* Poll the system for user input */
PollResult io_poll(int32_t ms)
{
  bool timeout;

  if (in_buffer.rpos < in_buffer.wpos) {
    /* If there's data buffered from a previous event loop iteration, 
     * let vim read it */
    return kPollInput;
  }

  if (eof) {
    return kPollEof;
  }

  if (ms == 0) {
    return kPollNone;
  }

  if (!initialized) {
    /* Only initialize the event loop once */
    initialize_event_loop();
    initialized = true;
  }

  /* Pin the buffer used by libuv */
  in_buffer.uvbuf.len = INPUT_BUFFER_LENGTH - in_buffer.apos;
  in_buffer.uvbuf.base = (char *)(in_buffer.data + in_buffer.apos);
  in_buffer.apos = INPUT_BUFFER_LENGTH;

  if (read_stream_type == UV_FILE) {
    uv_fs_read(
        uv_default_loop(),
        &read_req,
        read_cmd_fd,
        &in_buffer.uvbuf,
        1,
        in_buffer.fpos,
        fread_cb);
  } else {
    uv_read_start(read_stream, alloc_cb, read_cb);
  }

  timeout = false;

  if (ms > 0) {
    timer_req.data = &timeout;
    uv_timer_start(&timer_req, timer_cb, ms, 0);
  }

  while (true) {
    uv_run(uv_default_loop(), UV_RUN_ONCE);

    if (in_buffer.rpos < in_buffer.wpos || eof || timeout) {
      break;
    }
  }

  relocate();

  if (read_stream_type != UV_FILE) {
    uv_read_stop(read_stream);
  }

  if (timeout && ms > 0) {
    uv_timer_stop(&timer_req);
  }

  if (in_buffer.rpos < in_buffer.wpos) {
    return kPollInput;
  }

  if (eof) {
    return kPollEof;
  }

  /* timeout */
  return kPollNone;
}

static void initialize_event_loop()
{
  in_buffer.wpos = in_buffer.rpos = in_buffer.apos = in_buffer.fpos = 0;
#ifdef DEBUG
  memset(&in_buffer.data, 0, INPUT_BUFFER_LENGTH);
#endif

  if ((read_stream_type = uv_guess_handle(read_cmd_fd)) != UV_FILE) {
    read_stream = (uv_stream_t *)malloc(sizeof(uv_pipe_t));
    uv_pipe_init(uv_default_loop(), (uv_pipe_t *)read_stream, 0);
    uv_pipe_open((uv_pipe_t *)read_stream, read_cmd_fd);
  }

  uv_timer_init(uv_default_loop(), &timer_req);
}

/* Called by libuv to allocate memory for reading. */
static void alloc_cb(uv_handle_t *handle, size_t ssize, uv_buf_t *rv)
{
  rv->base = in_buffer.uvbuf.base;
  rv->len = in_buffer.uvbuf.len;
}

/*
 * Callback invoked by libuv after it copies the data into the buffer provided
 * by `alloc_cb`. This is also called on EOF or when `alloc_cb` returns a
 * 0-length buffer.
 */
static void read_cb(uv_stream_t *stream, ssize_t cnt, const uv_buf_t *buf)
{
  if (cnt < 0) {
    if (cnt == UV_EOF) {
      /* Set the EOF flag */
      eof = true;
    } else if (cnt != UV_ENOBUFS) {
      fprintf(stderr, "Unexpected error %s\n", uv_strerror(cnt));
    }
    return;
  }

  /* Data was already written, so all we need is to update 'wpos' to reflect
   * the space actually used in the buffer. */
  in_buffer.wpos += cnt;
}

static void fread_cb(uv_fs_t *req)
{
  uv_fs_req_cleanup(req);

  if (req->result <= 0) {
    if (req->result == 0) {
      /* Set the EOF flag */
      eof = true;
    } else {
      fprintf(stderr, "Unexpected error %s\n", uv_strerror(req->result));
    }
    return;
  }

  in_buffer.wpos += req->result;
  in_buffer.fpos += req->result;
}

static void timer_cb(uv_timer_t *handle, int status) {
  *((bool *)handle->data) = true;
}

static void relocate()
{
  uint32_t move_count;

  if (in_buffer.apos > in_buffer.wpos) {
    /* Restore `apos` to `wpos` to reflect what was actually used by libuv */
    in_buffer.apos = in_buffer.wpos;
    return;
  }

  /* Relocate buffer by moving data to the left */ 
  move_count = in_buffer.apos - in_buffer.rpos;
  memmove(in_buffer.data, in_buffer.data + in_buffer.rpos, move_count);
  in_buffer.apos -= in_buffer.rpos;
  in_buffer.wpos -= in_buffer.rpos;
  in_buffer.rpos = 0;
}
