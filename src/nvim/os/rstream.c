#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <uv.h>

#include "nvim/os/uv_helpers.h"
#include "nvim/os/rstream_defs.h"
#include "nvim/os/rstream.h"
#include "nvim/ascii.h"
#include "nvim/vim.h"
#include "nvim/memory.h"
#include "nvim/log.h"
#include "nvim/misc1.h"

struct rstream {
  void *data;
  uv_buf_t uvbuf;
  size_t fpos;
  RBuffer *buffer;
  uv_stream_t *stream;
  uv_idle_t *fread_idle;
  uv_handle_type file_type;
  uv_file fd;
  rstream_cb cb;
  bool free_handle;
  bool async;
  uv_mutex_t buffer_mutex;
};

#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/rstream.c.generated.h"
#endif

/// Creates a new RStream instance. A RStream encapsulates all the boilerplate
/// necessary for reading from a libuv stream.
///
/// @param cb A function that will be called whenever some data is available
///        for reading with `rstream_read`
/// @param buffer RBuffer instance to associate with the RStream
/// @param async If true, the RStream instance callbacks will be invoked
///        by the IO thread.
/// @param data Some state to associate with the `RStream` instance
/// @return The newly-allocated `RStream` instance
RStream * rstream_new(rstream_cb cb, RBuffer *buffer, bool async, void *data)
{
  RStream *rv = xmalloc(sizeof(RStream));
  buffer->data = rv;
  buffer->full_cb = on_rbuffer_full;
  buffer->nonfull_cb = on_rbuffer_nonfull;
  rv->buffer = buffer;
  rv->fpos = 0;
  rv->data = data;
  rv->cb = cb;
  rv->stream = NULL;
  rv->fread_idle = NULL;
  rv->free_handle = false;
  rv->file_type = UV_UNKNOWN_HANDLE;
  rv->async = async;
  if (!async) {
    uv_mutex_init(&rv->buffer_mutex);
  }

  return rv;
}

static void on_rbuffer_full(RBuffer *buf, void *data)
{
  rstream_stop(data);
}

static void on_rbuffer_nonfull(RBuffer *buf, void *data)
{
  rstream_start(data);
}

/// Frees all memory allocated for a RStream instance
///
/// @param rstream The `RStream` instance
void rstream_free(RStream *rstream)
{
  event_call_async(rstream_free_async, 1, rstream);
}

/// Sets the underlying `uv_stream_t` instance
///
/// @param rstream The `RStream` instance
/// @param stream The new `uv_stream_t` instance
void rstream_set_stream(RStream *rstream, uv_stream_t *stream)
{
  event_call_async(rstream_set_stream_async, 2, rstream, stream);
}

/// Sets the underlying file descriptor that will be read from. Only pipes
/// and regular files are supported for now.
///
/// @param rstream The `RStream` instance
/// @param file The file descriptor
void rstream_set_file(RStream *rstream, uv_file file)
{
  event_call_async(rstream_set_file_async, 2, rstream, (void *)(uintptr_t)file);
}

/// Starts watching for events from a `RStream` instance.
///
/// @param rstream The `RStream` instance
void rstream_start(RStream *rstream)
{
  event_call_async(rstream_start_async, 1, rstream);
}

/// Stops watching for events from a `RStream` instance.
///
/// @param rstream The `RStream` instance
void rstream_stop(RStream *rstream)
{
  event_call_async(rstream_stop_async, 1, rstream);
}

// Callbacks used by libuv

// Called by libuv to allocate memory for reading.
static void alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf)
{
  RStream *rstream = handle_get_rstream(handle);
  lock_if_sync(rstream);
  buf->base = rbuffer_write_ptr(rstream->buffer, &buf->len);
}

// Callback invoked by libuv after it copies the data into the buffer provided
// by `alloc_cb`. This is also called on EOF or when `alloc_cb` returns a
// 0-length buffer.
static void read_cb(uv_stream_t *stream, ssize_t cnt, const uv_buf_t *buf)
{
  RStream *rstream = handle_get_rstream((uv_handle_t *)stream);

  if (cnt <= 0) {
    if (cnt != UV_ENOBUFS
        // cnt == 0 means libuv asked for a buffer and decided it wasn't needed:
        // http://docs.libuv.org/en/latest/stream.html#c.uv_read_start.
        //
        // We don't need to do anything with the RBuffer because the next call
        // to `alloc_cb` will return the same unused pointer(`rbuffer_produced`
        // won't be called)
        && cnt != 0) {
      DLOG("Closing RStream(%p) because of %s(%zd)", rstream,
           uv_strerror((int)cnt), cnt);
      // Read error or EOF, either way stop the stream and invoke the callback
      // with eof == true
      uv_read_stop(stream);
      invoke_callback(rstream, true);
    }
    unlock_if_sync(rstream);
    return;
  }

  // at this point we're sure that cnt is positive, no error occurred
  size_t nread = (size_t)cnt;
  // Data was already written, so all we need is to update 'wpos' to reflect
  // the space actually used in the buffer.
  rbuffer_produced(rstream->buffer, nread);
  unlock_if_sync(rstream);
  invoke_callback(rstream, false);
  rstream->cb(rstream, rstream->buffer, rstream->data, false);
}

// Called by the by the 'idle' handle to emulate a reading event
static void fread_idle_cb(uv_idle_t *handle)
{
  uv_fs_t req;
  RStream *rstream = handle_get_rstream((uv_handle_t *)handle);
  lock_if_sync(rstream);
  rstream->uvbuf.base = rbuffer_write_ptr(rstream->buffer, &rstream->uvbuf.len);

  // the offset argument to uv_fs_read is int64_t, could someone really try
  // to read more than 9 quintillion (9e18) bytes?
  // upcast is meant to avoid tautological condition warning on 32 bits
  uintmax_t fpos_intmax = rstream->fpos;
  if (fpos_intmax > INT64_MAX) {
    ELOG("stream offset overflow");
    preserve_exit();
  }

  // Synchronous read
  uv_fs_read(
      uv_default_loop(),
      &req,
      rstream->fd,
      &rstream->uvbuf,
      1,
      (int64_t) rstream->fpos,
      NULL);

  uv_fs_req_cleanup(&req);

  if (req.result <= 0) {
    uv_idle_stop(rstream->fread_idle);
    rstream->cb(rstream, rstream->buffer, rstream->data, true);
    unlock_if_sync(rstream);
    return;
  }

  // no errors (req.result (ssize_t) is positive), it's safe to cast.
  size_t nread = (size_t) req.result;
  rbuffer_produced(rstream->buffer, nread);
  unlock_if_sync(rstream);
  rstream->fpos += nread;
}

static void rstream_free_async(void **argv)
{
  RStream *rstream = argv[0];
  if (rstream->free_handle) {
    if (rstream->fread_idle != NULL) {
      uv_close((uv_handle_t *)rstream->fread_idle, close_cb);
    } else {
      uv_close((uv_handle_t *)rstream->stream, close_cb);
    }
  }

  rbuffer_free(rstream->buffer);
  xfree(rstream);
}

static void rstream_set_stream_async(void **argv)
{
  RStream *rstream = argv[0];
  uv_stream_t *stream = argv[1];
  handle_set_rstream((uv_handle_t *)stream, rstream);
  rstream->stream = stream;
}

static void rstream_set_file_async(void **argv)
{
  RStream *rstream = argv[0];
  uv_file file = (uv_file)(uintptr_t)argv[1];
  rstream->file_type = uv_guess_handle(file);

  if (rstream->free_handle) {
    // If this is the second time we're calling this function, free the
    // previously allocated memory
    if (rstream->fread_idle != NULL) {
      uv_close((uv_handle_t *)rstream->fread_idle, close_cb);
      rstream->fread_idle = NULL;
    } else {
      uv_close((uv_handle_t *)rstream->stream, close_cb);
      rstream->stream = NULL;
    }
  }

  if (rstream->file_type == UV_FILE) {
    // Non-blocking file reads are simulated with an idle handle that reads
    // in chunks of rstream->buffer_size, giving time for other events to
    // be processed between reads.
    rstream->fread_idle = xmalloc(sizeof(uv_idle_t));
    uv_idle_init(uv_default_loop(), rstream->fread_idle);
    rstream->fread_idle->data = NULL;
    handle_set_rstream((uv_handle_t *)rstream->fread_idle, rstream);
  } else {
    // Only pipes are supported for now
    assert(rstream->file_type == UV_NAMED_PIPE
        || rstream->file_type == UV_TTY);
    rstream->stream = xmalloc(sizeof(uv_pipe_t));
    uv_pipe_init(uv_default_loop(), (uv_pipe_t *)rstream->stream, 0);
    uv_pipe_open((uv_pipe_t *)rstream->stream, file);
    rstream->stream->data = NULL;
    handle_set_rstream((uv_handle_t *)rstream->stream, rstream);
  }

  rstream->fd = file;
  rstream->free_handle = true;
}

static void rstream_start_async(void **argv)
{
  RStream *rstream = argv[0];
  if (rstream->file_type == UV_FILE) {
    uv_idle_start(rstream->fread_idle, fread_idle_cb);
  } else {
    uv_read_start(rstream->stream, alloc_cb, read_cb);
  }
}

static void rstream_stop_async(void **argv)
{
  RStream *rstream = argv[0];
  if (rstream->file_type == UV_FILE) {
    uv_idle_stop(rstream->fread_idle);
  } else {
    uv_read_stop(rstream->stream);
  }
}

static void read_sync(void *data)
{
  RStream *rstream = data;
  lock_if_sync(rstream);
  rstream->cb(rstream, rstream->buffer, rstream->data, false);
  unlock_if_sync(rstream);
}

static void eof_sync(void *data)
{
  RStream *rstream = data;
  lock_if_sync(rstream);
  rstream->cb(rstream, rstream->buffer, rstream->data, true);
  unlock_if_sync(rstream);
}

static void invoke_callback(RStream *rstream, bool eof)
{
  if (rstream->async) {
    rstream->cb(rstream, rstream->buffer, rstream->data, eof);
  } else {
    if (eof) {
      event_push(read_sync, rstream);
    } else {
      event_push(eof_sync, rstream);
    }
  }
}

static void close_cb(uv_handle_t *handle)
{
  xfree(handle->data);
  xfree(handle);
}

static void lock_if_sync(RStream *rstream)
{
  if (!rstream->async) {
    uv_mutex_lock(&rstream->buffer_mutex);
  }
}

static void unlock_if_sync(RStream *rstream)
{
  if (!rstream->async) {
    uv_mutex_unlock(&rstream->buffer_mutex);
  }
}
