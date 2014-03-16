#include <stdbool.h>
#include <pthread.h>

#include <uv.h>

#include "./os.h"
#include "../vim.h"
#include "../term.h"
#include "../os_unix.h"
#include "../memline.h"
#include "../misc2.h"
#include "../ui.h"
#include "../fileio.h"
#include "../getchar.h"
#include "../message.h"
#include "../syntax.h"
#include "../screen.h"

#define UNUSED(x) (void)(x)
#define BUF_SIZE 4096

typedef struct {
  /* 
   * Input buffer structure. This contains a contiguous memory chunk and three
   * pointers(allocated, written and read positions) that are used to implement
   * a simple form of memory management for incoming data without requiring
   * calls to the system memory allocator.
   *
   * Probably the 'apos' (allocated pos) pointer isn't needed, but omitting it
   * would mean we are assuming details about libuv implementation which
   * wouldn't be very robust.
   */ 
  unsigned int apos, wpos, rpos;
  char_u data[BUF_SIZE];
} input_buffer_T;

static int pending_signal = 0;
static uv_thread_t io_thread;
static uv_mutex_t io_mutex;
static uv_cond_t io_cond;
static uv_mutex_t delay_mutex;
static uv_cond_t delay_cond;
static uv_async_t stop_loop_async;
static input_buffer_T in_buffer;
/* Actual conditions behind the io_cond */
static bool signal_consumed = false, activity = false,
            data_consumed = false, running = false, eof = false;

static void io_start(void *);
static void loop_running(uv_idle_t *, int);
static void stop_loop(uv_async_t *, int);
static void alloc_buffer_cb(uv_handle_t *, size_t, uv_buf_t *);
static void read_cb(uv_stream_t *, ssize_t, const uv_buf_t *);
static void signal_cb(uv_signal_t *, int signum);
static void exit_scroll(void);
static int special_key(char_u *, char_u);
static int cursorhold_key(char_u *);
static int signal_key(char_u *);
static void io_lock();
static void io_unlock();
static void io_timedwait(long ms, bool *condition);
static void io_wait(bool *condition);
static void io_signal(bool *condition);

void io_init() {
  sigset_t set;

  /* uv_disable_stdio_inheritance(); */
  uv_mutex_init(&io_mutex);
  uv_cond_init(&io_cond);
  uv_mutex_init(&delay_mutex);
  uv_cond_init(&delay_cond);
  io_lock();
  /* The event loop runs in a background thread */
  uv_thread_create(&io_thread, io_start, NULL);
  /* Wait for the loop thread to be ready */
  io_wait(&running);
  /* Block signals in the main thread 
   * TODO Search for a pure-libuv way of doing this */
  sigfillset(&set);
  pthread_sigmask(SIG_SETMASK, &set, NULL);
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

#ifdef EXITFREE
  free_all_mem();
#endif

  exit(r);
}

int next_signal() {
  /* FIXME pending_signal must be a queue of signals, right now the event loop
   * is blocking until this function is called. */
  int rv;

  io_lock();
  io_signal(&signal_consumed);
  rv = pending_signal;
  pending_signal = 0;
  io_unlock();

  return rv;
}

/*
 * This is ugly, but necessary at least until we start messing with vget*
 * functions a long time from now
 */
int mch_inchar(char_u *buf, int maxlen, long wtime, int tb_change_cnt) {
  int rv;

  io_lock();

  if (pending_signal) {
    io_unlock();
    return signal_key(buf);
  }

  if (in_buffer.rpos < in_buffer.wpos) {
    /* Take whatever data is available */
    rv = read_from_input_buf(buf, (long)maxlen);
    /* If the loop is paused due to full buffer, notify it that
     * we consumed some data and it may read more */
    io_signal(&data_consumed);
    io_unlock();
    return rv;
  }

  if (wtime >= 0) {
    /* Wait up to 'wtime' milliseconds */
    io_timedwait(wtime, &activity);

    if (pending_signal) {
      io_unlock();
      return signal_key(buf);
    }

    if (in_buffer.wpos == in_buffer.rpos) {
      io_unlock();
      /* Didn't read anything */
      if (eof) {
        /* Exit when stdin is closed */
        read_error_exit();
      }
      return 0;
    }

  } else {
    if (trigger_cursorhold() && maxlen >= 3) {
      /* When doing a blocking read, first block for 'updatetime' if a
       * cursorhold event can be triggered */
      io_timedwait(p_ut, &activity);

      if (pending_signal) {
        io_unlock();
        return signal_key(buf);
      }

      if (in_buffer.wpos == in_buffer.rpos) {
        io_unlock();
        return cursorhold_key(buf);
      }
    }

    /* Before blocking check for EOF first or we may end up in a deadlock */
    if (eof) {
      io_unlock();
      read_error_exit();
      return 0;
    }

    before_blocking();
    io_wait(&activity);

    if (pending_signal) {
      io_unlock();
      return signal_key(buf);
    }
  }

  /* This was adapted from the original mch_inchar code.  Not sure why it's
   * here, but I guess it has something to do with netbeans support which was
   * removed.  Leave it alone for now */
  if (typebuf_changed(tb_change_cnt)) {
    io_unlock();
    return 0;
  }

  if (in_buffer.wpos == in_buffer.rpos && eof) {
    io_unlock();
    read_error_exit();
    return 0;
  }

  rv = read_from_input_buf(buf, (long)maxlen);
  io_unlock();

  return rv;
}

/* FIXME This is a temporary function, used to satisfy the way vim currently
 * reads characters. Soon it will be removed */
ssize_t mch_inchar_read(char *buf, size_t count) {
  size_t rv = 0;

  /* Copy at most 'count' to the buffer argument */
  while (in_buffer.rpos < in_buffer.wpos && rv < count)
    buf[rv++] = in_buffer.data[in_buffer.rpos++];

  return rv;
}

int mch_char_avail() {
  return in_buffer.rpos < in_buffer.wpos;
}

void mch_delay(long msec, int ignoreinput) {
  int old_tmode;

  uv_mutex_lock(&delay_mutex);

  if (ignoreinput) {
    /* Go to cooked mode without echo, to allow SIGINT interrupting us
     * here.  But we don't want QUIT to kill us (CTRL-\ used in a
     * shell may produce SIGQUIT). */
    in_mch_delay = TRUE;
    old_tmode = curr_tmode;

    if (curr_tmode == TMODE_RAW)
      settmode(TMODE_SLEEP);

    (void)uv_cond_timedwait(&delay_cond, &delay_mutex, msec * 1000000);

    settmode(old_tmode);
    in_mch_delay = FALSE;
  } else {
    (void)uv_cond_timedwait(&delay_cond, &delay_mutex, msec * 1000000);
  }

  uv_mutex_unlock(&delay_mutex);
}

/*
 * Check for CTRL-C typed by reading all available characters.
 * In cooked mode we should get SIGINT, no need to check.
 */
void mch_breakcheck() {
  /*
   * Apparently this has no effect on the tests, so leave it commented for now.
   * Soon we'll handle SIGINTs and user input in the UI, so this won't matter
   * anyway
   */
  
  if (curr_tmode == TMODE_RAW && mch_char_avail())
    fill_input_buf(FALSE);
}

static void io_start(void *arg) {
  uv_loop_t *loop;
  uv_idle_t idler; 
  uv_signal_t sint, shup, squit, sabrt, sterm, swinch;
  uv_stream_t stdin_stream;

  memset(&in_buffer, 0, sizeof(in_buffer));

  UNUSED(arg);
  loop = uv_loop_new();
  /* Idler for signaling the main thread when the loop is running */
  uv_idle_init(loop, &idler);
  idler.data = &stdin_stream;
  uv_idle_start(&idler, loop_running);
  /* Async watcher used by the main thread to stop the loop */
  uv_async_init(loop, &stop_loop_async, stop_loop);
  /* stdin */
  /* FIXME setting fd to non-blocking is only needed on unix */
  fcntl(read_cmd_fd, F_SETFL, fcntl(read_cmd_fd, F_GETFL, 0) | O_NONBLOCK);
  uv_pipe_init(loop, (uv_pipe_t *)&stdin_stream, 0);
  uv_pipe_open((uv_pipe_t *)&stdin_stream, read_cmd_fd);
  /* signals */
  uv_signal_init(loop, &sint);
  uv_signal_start(&sint, signal_cb, SIGINT);
  uv_signal_init(loop, &shup);
  uv_signal_start(&shup, signal_cb, SIGHUP);
  uv_signal_init(loop, &squit);
  uv_signal_start(&squit, signal_cb, SIGQUIT);
  uv_signal_init(loop, &sabrt);
  uv_signal_start(&sabrt, signal_cb, SIGABRT);
  uv_signal_init(loop, &sterm);
  uv_signal_start(&sterm, signal_cb, SIGTERM);
  uv_signal_init(loop, &swinch);
  uv_signal_start(&swinch, signal_cb, SIGWINCH);
  /* start processing events */
  uv_run(loop, UV_RUN_DEFAULT);
  /* free the event loop */
  uv_loop_delete(loop);
}

/* Signal the main thread that the loop started running */
static void loop_running(uv_idle_t *handle, int status) {
  uv_idle_stop(handle);
  io_lock();
  uv_read_start((uv_stream_t *)handle->data, alloc_buffer_cb, read_cb);
  io_signal(&running);
  io_unlock();
}

static void stop_loop(uv_async_t *handle, int status) {
  UNUSED(status);
  uv_stop(handle->loop);
}

/* Called by libuv to allocate memory for reading. This uses a fixed buffer
 * through the entire stream lifetime */
static void alloc_buffer_cb(uv_handle_t *handle, size_t ssize, uv_buf_t *rv)
{
  int move_count;
  size_t available;

  UNUSED(handle);

  io_lock();

  available = BUF_SIZE - in_buffer.apos;

  if (!available) {
    if (in_buffer.rpos == 0) {
      /* Pause the stream until the main thread consumes some data. The io
       * mutex should only be unlocked when the stream is stopped in the
       * read_cb */
      rv->len = 0;
      io_unlock();
      return;
    }
    /* 
     * Out of space in internal buffer, move data to the 'left' as much as
     * possible.
     */
    move_count = in_buffer.apos - in_buffer.rpos;
    memmove(in_buffer.data, in_buffer.data + in_buffer.rpos, move_count);
    in_buffer.wpos -= in_buffer.rpos;
    in_buffer.apos -= in_buffer.rpos;
    in_buffer.rpos = 0;
    available = BUF_SIZE - in_buffer.apos;
  }

  rv->base = (char *)(in_buffer.data + in_buffer.apos);
  rv->len = available;
  in_buffer.apos += available;
  io_unlock();
}

/*
 * The actual reading was already performed by libuv, this callback will do one
 * of the following:
 *    - If EOF was reached, it will set appropriate flags and signal the main
 *      thread to continue
 *    - If the alloc_buffer_cb didnt allocate anything, then it will pause the
 *      input stream.
 *    - If 'cnt' > 0, it will update the buffer write position(wpos) to reflect
 *      what was actually written.
 */
static void read_cb(uv_stream_t *stream, ssize_t cnt, const uv_buf_t *buf) {
  UNUSED(buf); /* Data is already on the buffer */

  if (cnt < 0) {

    if (cnt == UV_EOF) {
      /* EOF, stop the event loop and signal the main thread. This will cause
       * vim to exit */
      eof = true;
      io_lock();
      io_signal(&activity);
      uv_stop(stream->loop);
      io_unlock();
    } else if (cnt == UV_ENOBUFS) {
      io_lock();
      io_wait(&data_consumed);
      /* Resume reading */
      io_unlock();
    } else {
      fprintf(stderr, "Unexpected error %ld\n", cnt);
    }
    return;
  }

  io_lock();
  /* Data was already written, so all we need is to update 'wpos' to reflect
   * that */
  in_buffer.wpos += cnt;
  /* It's very likely that most of the "allocated" space wasn't used, so adjust
   * `apos` to to `wpos` */
  in_buffer.apos = in_buffer.wpos;
  if (cnt > 0) {
    io_signal(&activity);
  } else {
    /* cnt == 0 means that libuv requested allocation of a buffer it didn't
     * use, "deallocate" it now. */
    in_buffer.apos -= buf->len;
  }
  
  /* After setting the read_cmd_fd to O_NONBLOCK, the bytes entered by the user
   * always have a trailing 0. Without the following 'else', typing
   * interactively will display 'garbage'(Need to figure out why) */ 
  io_unlock();
}

static void signal_cb(uv_signal_t *handle, int signum) {
  io_lock();
  pending_signal = signum;
  io_signal(&activity); /* unblock */
  io_wait(&signal_consumed);
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

/* Helpers for returning special keys from mch_inchar */

static int special_key(char_u *buf, char_u k) {
  /* If nothing was typed, trigger the event */
  buf[0] = K_SPECIAL;
  buf[1] = KS_EXTRA;
  buf[2] = k;
  return 3;
}

static int cursorhold_key(char_u *buf) {
  return special_key(buf, KE_CURSORHOLD);
}

static int signal_key(char_u *buf) {
  return special_key(buf, KE_SIGNAL);
}

/* Helpers for dealing with io synchronization */
static void io_lock() {
  uv_mutex_lock(&io_mutex);
}

static void io_unlock() {
  uv_mutex_unlock(&io_mutex);
}

static void io_wait(bool *condition) {
  while (!(*condition)) uv_cond_wait(&io_cond, &io_mutex);
  *condition = false;
}

static void io_timedwait(long ms, bool *condition) {
  while (!(*condition)) {
    /* FIXME To deal with spurious wakeups correctly we must subtract the time
     * slept on each iteration. This needs support from the libuv-side,
     * probably by changing this function to receive an extra 'out parameter'
     * that will contain the time ellapsed */
    if (uv_cond_timedwait(&io_cond, &io_mutex, ms * 1000000) == UV_ETIMEDOUT)
      break;
  }
  *condition = false;
}

static void io_signal(bool *condition) {
  *condition = true;
  uv_cond_signal(&io_cond);
}

