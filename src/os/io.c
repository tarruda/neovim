#include <uv.h>

#include "./os.h"
#include "../vim.h"
#include "../term.h"
#include "../os_unix.h"
#include "../memline.h"
#include "../ui.h"
#include "../fileio.h"
#include "../getchar.h"
#include "../message.h"
#include "../syntax.h"
#include "../screen.h"

#define UNUSED(x) (void)(x)
#define BUF_SIZE 1

typedef struct {
  unsigned int wpos, rpos;
  char_u data[BUF_SIZE];
} input_buffer_T;

static uv_thread_t io_thread;
static uv_mutex_t io_mutex;
static uv_cond_t io_cond;
static uv_async_t read_wake_async, stop_loop_async;
static uv_pipe_t stdin_pipe;
static input_buffer_T in_buffer;
static bool reading = false, eof = false;

static void io_start(void *);
static void loop_running(uv_idle_t *, int);
static void read_wake(uv_async_t *, int);
static void stop_loop(uv_async_t *, int);
static void alloc_buffer_cb(uv_handle_t *, size_t, uv_buf_t *);
static void read_cb(uv_stream_t *, ssize_t, const uv_buf_t *);
static void exit_scroll(void);
static void io_lock();
static void io_unlock();
static void io_wait();
static void io_timedwait(long ms);
static void io_signal();

/* Called at startup to setup the background thread that will handle all
 * events and translate to keys. */
void io_init() {
  /* uv_disable_stdio_inheritance(); */
  uv_mutex_init(&io_mutex);
  uv_cond_init(&io_cond);
  io_lock();
  /* The event loop runs in a background thread */
  uv_thread_create(&io_thread, io_start, NULL);
  /* Wait for the loop thread to be ready */
  io_wait();
  io_unlock();
}

void mch_exit(int r) {
  exiting = TRUE;
  /* stop libuv loop */
  uv_async_send(&stop_loop_async);
  /* wait for the event loop thread */
  uv_thread_join(&io_thread);


  {
    settmode(TMODE_COOK);
    mch_restore_title(3);       /* restore xterm title and icon name */
    /*
     * When t_ti is not empty but it doesn't cause swapping terminal
     * pages, need to output a newline when msg_didout is set.  But when
     * t_ti does swap pages it should not go to the shell page.  Do this
     * before stoptermcap().
     */
    if (swapping_screen() && !newline_on_exit)
      exit_scroll();

    /* Stop termcap: May need to check for T_CRV response, which
     * requires RAW mode. */
    stoptermcap();

    /*
     * A newline is only required after a message in the alternate screen.
     * This is set to TRUE by wait_return().
     */
    if (!swapping_screen() || newline_on_exit)
      exit_scroll();

    /* Cursor may have been switched off without calling starttermcap()
     * when doing "vim -u vimrc" and vimrc contains ":q". */
    if (full_screen)
      cursor_on();
  }
  out_flush();
  ml_close_all(TRUE);           /* remove all memfiles */
  // may_core_dump();

#ifdef EXITFREE
  free_all_mem();
#endif

  exit(r);
}

/*
 * This is very ugly, but necessary at least until we start messing with
 * vget* functions
 */
int mch_inchar(char_u *buf, int maxlen, long wtime, int tb_change_cnt) {
  int rv;

  UNUSED(tb_change_cnt);
  io_lock();

  if (eof) {
    io_unlock();
    read_error_exit();
    return 0;
  }

  if (!reading) {
    uv_async_send(&read_wake_async);
    reading = true;
  }

  if (in_buffer.rpos == in_buffer.wpos) {

    if (wtime >= 0 || eof) {
      io_timedwait(wtime);
      io_unlock();
      return 0;
    }

    io_timedwait(p_ut);

    if (eof) {
      io_unlock();
      read_error_exit();
      return 0;
    }

    /* TODO refactor cursorhold events out of here */
    if (trigger_cursorhold() && maxlen >= 3
        && !typebuf_changed(tb_change_cnt)) {
      buf[0] = K_SPECIAL;
      buf[1] = KS_EXTRA;
      buf[2] = (int)KE_CURSORHOLD;
      io_unlock();
      return 3;
    }
    before_blocking();

    io_wait();

    if (eof) {
      io_unlock();
      read_error_exit();
      return 0;
    }
  }

  /* TODO Get rid of the typeahead buffer */
  if (typebuf_changed(tb_change_cnt)) {
    io_unlock();
    return 0;
  }
  /* Copy at most 'maxlen' to the buffer argument */
  rv = 0;

  while (in_buffer.rpos < in_buffer.wpos && rv < maxlen)
    buf[rv++] = in_buffer.data[in_buffer.rpos++];

  io_unlock();

  return rv;
}

int mch_char_avail() {
  return in_buffer.rpos < in_buffer.wpos;
}


void mch_delay(long msec, int ignoreinput) {
  int old_tmode;

  io_lock();

  if (ignoreinput) {
    /* Go to cooked mode without echo, to allow SIGINT interrupting us
     * here.  But we don't want QUIT to kill us (CTRL-\ used in a
     * shell may produce SIGQUIT). */
    in_mch_delay = TRUE;
    old_tmode = curr_tmode;

    if (curr_tmode == TMODE_RAW)
      settmode(TMODE_SLEEP);

    io_timedwait(msec);

    settmode(old_tmode);
    in_mch_delay = FALSE;
  } else
    io_timedwait(msec);

  io_unlock();
}

static void io_start(void *arg) {
  uv_idle_t idler;

  memset(&in_buffer, 0, sizeof(in_buffer));

  UNUSED(arg);
  /* use default loop */
  uv_loop_t *loop = uv_default_loop();
  /* Idler for signaling the main thread when the loop is running */
  uv_idle_init(loop, &idler);
  uv_idle_start(&idler, loop_running);
  /* Async watcher used by the main thread to resume reading */
  uv_async_init(loop, &read_wake_async, read_wake);
  uv_async_init(loop, &stop_loop_async, stop_loop);
  /* stdin */
  uv_pipe_init(loop, &stdin_pipe, 0);
  uv_pipe_open(&stdin_pipe, read_cmd_fd);
  /* start processing events */
  uv_run(loop, UV_RUN_DEFAULT);
}

/* Signal the main thread that the loop started running */
static void loop_running(uv_idle_t *handle, int status) {
  uv_idle_stop(handle);
  io_lock();
  io_signal();
  io_unlock();
}


/* Signal the loop to continue reading stdin */
static void read_wake(uv_async_t *handle, int status) {
  UNUSED(handle);
  UNUSED(status);
  uv_read_start((uv_stream_t *)&stdin_pipe, alloc_buffer_cb, read_cb);
}

static void stop_loop(uv_async_t *handle, int status) {
  UNUSED(handle);
  UNUSED(status);
  uv_stop(uv_default_loop());
}

/* Called by libuv to allocate memory for reading. This uses a static buffer */
static void alloc_buffer_cb(uv_handle_t *handle, size_t ssize, uv_buf_t *rv) {
  int wpos;
  UNUSED(handle);
  io_lock();
  /*
   * Write data at the current write position
   */
  wpos = in_buffer.wpos;
  io_unlock();
  if (wpos == BUF_SIZE) {
    /* No more space in buffer */
    rv->len = 0;
    return;
  }
  if (BUF_SIZE < (wpos + ssize))
    ssize = BUF_SIZE - wpos;
  rv->base = (char *)(in_buffer.data + wpos);
  rv->len = ssize;
}

/* This is only used to check how many bytes were read or if an error
 * occurred. If the static buffer is full(wpos == BUF_SIZE) try to move
 * the data to free space, or stop reading. */
static void read_cb(uv_stream_t *s, ssize_t cnt, const uv_buf_t *buf) {
  int move_count;
  UNUSED(s);
  UNUSED(buf); /* Data is already on the static buffer */
  if (cnt < 0) {
    if (cnt == UV_EOF) {
      io_lock();
      eof = true;
      uv_stop(uv_default_loop());
      io_signal();
      io_unlock();
      return;
    } else if (cnt == UV_ENOBUFS) {
      /* Out of space in internal buffer, move data to the 'left' as much
       * as possible. If we cant move anything, stop reading for now. */
      io_lock();
      if (in_buffer.rpos == 0)
      {
        reading = false;
        io_unlock();
        uv_read_stop((uv_stream_t *)&stdin_pipe);
      }
      move_count = BUF_SIZE - in_buffer.rpos;
      memmove(in_buffer.data, in_buffer.data + in_buffer.rpos, move_count);
      in_buffer.wpos -= in_buffer.rpos;
      in_buffer.rpos = 0;
      io_unlock();
    }
    else {
      fprintf(stderr, "Unexpected error %s\n", uv_strerror(cnt));
    }
    return;
  }
  io_lock();
  in_buffer.wpos += cnt;
  io_signal();
  io_unlock();
}

/*
 * Output a newline when exiting.
 * Make sure the newline goes to the same stream as the text.
 */
static void exit_scroll() {
  if (silent_mode)
    return;
  if (newline_on_exit || msg_didout) {
    if (msg_use_printf()) {
      if (info_message)
        mch_msg("\n");
      else
        mch_errmsg("\r\n");
    } else
      out_char('\n');
  } else   {
    restore_cterm_colors();             /* get original colors back */
    msg_clr_eos_force();                /* clear the rest of the display */
    windgoto((int)Rows - 1, 0);         /* may have moved the cursor */
  }
}


/* Helpers for dealing with io synchronization */
static void io_lock() {
  uv_mutex_lock(&io_mutex);
}


static void io_unlock() {
  uv_mutex_unlock(&io_mutex);
}


static void io_wait() {
  uv_cond_wait(&io_cond, &io_mutex);
}


static void io_timedwait(long ms) {
  (void)uv_cond_timedwait(&io_cond, &io_mutex, ms * 1000000);
}


static void io_signal() {
  uv_cond_signal(&io_cond);
}
