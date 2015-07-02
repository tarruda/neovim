#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <uv.h>

#include "nvim/os/uv_helpers.h"
#include "nvim/os/wstream.h"
#include "nvim/os/wstream_defs.h"
#include "nvim/os/event.h"
#include "nvim/vim.h"
#include "nvim/memory.h"

#define DEFAULT_MAXMEM 1024 * 1024 * 10

struct wstream {
  uv_stream_t *stream;
  // Memory currently used by pending buffers
  size_t curmem;
  // Maximum memory used by this instance
  size_t maxmem;
  // Number of pending requests
  size_t pending_reqs;
  bool freed, free_handle;
  uv_file fd;
  // (optional) Write callback and data
  wstream_cb cb;
  void *data;
};

struct wbuffer {
  size_t size, refcount;
  char *data;
  wbuffer_data_finalizer cb;
};

typedef struct {
  WStream *wstream;
  WBuffer *buffer;
  uv_write_t uv_req;
  int status;
} WRequest;


#ifdef INCLUDE_GENERATED_DECLARATIONS
# include "os/wstream.c.generated.h"
#endif

/// Creates a new WStream instance. A WStream encapsulates all the boilerplate
/// necessary for writing to a libuv stream.
///
/// @param maxmem Maximum amount memory used by this `WStream` instance. If 0,
///        a default value of 10mb will be used.
/// @return The newly-allocated `WStream` instance
WStream * wstream_new(size_t maxmem)
{
  if (!maxmem) {
    maxmem = DEFAULT_MAXMEM;
  }

  WStream *rv = xmalloc(sizeof(WStream));
  rv->maxmem = maxmem;
  rv->stream = NULL;
  rv->curmem = 0;
  rv->pending_reqs = 0;
  rv->freed = false;
  rv->free_handle = false;
  rv->cb = NULL;

  return rv;
}

/// Frees all memory allocated for a WStream instance
///
/// @param wstream The `WStream` instance
void wstream_free(WStream *wstream)
{
  event_call_async(wstream_free_async, 1, wstream);
}

/// Sets the underlying `uv_stream_t` instance
///
/// @param wstream The `WStream` instance
/// @param stream The new `uv_stream_t` instance
void wstream_set_stream(WStream *wstream, uv_stream_t *stream)
{
  event_call_async(wstream_set_stream_async, 2, wstream, stream);
}

/// Sets the underlying file descriptor that will be written to. Only pipes
/// are supported for now.
///
/// @param wstream The `WStream` instance
/// @param file The file descriptor
void wstream_set_file(WStream *wstream, uv_file file)
{
  event_call_async(wstream_set_file_async, 2, wstream, (void *)(uintptr_t)file);
}

/// Sets a callback that will be called on completion of a write request,
/// indicating failure/success.
///
/// This affects all requests currently in-flight as well. Overwrites any
/// possible earlier callback.
///
/// @note This callback will not fire if the write request couldn't even be
///       queued properly (i.e.: when `wstream_write() returns an error`).
///
/// @param wstream The `WStream` instance
/// @param cb The callback
/// @param data User-provided data that will be passed to `cb`
void wstream_set_write_cb(WStream *wstream, wstream_cb cb, void *data)
  FUNC_ATTR_NONNULL_ARG(1)
{
  wstream->cb = cb;
  wstream->data = data;
}

/// Queues data for writing to the backing file descriptor of a `WStream`
/// instance. This will fail if the write would cause the WStream use more
/// memory than specified by `maxmem`.
///
/// @param wstream The `WStream` instance
/// @param buffer The buffer which contains data to be written
/// @return false if the write failed
bool wstream_write(WStream *wstream, WBuffer *buffer)
{
  bool rv;
  event_call_async(wstream_write_async, 3, wstream, buffer, &rv);
  return rv;
}

/// Creates a WBuffer object for holding output data. Instances of this
/// object can be reused across WStream instances, and the memory is freed
/// automatically when no longer needed(it tracks the number of references
/// internally)
///
/// @param data Data stored by the WBuffer
/// @param size The size of the data array
/// @param refcount The number of references for the WBuffer. This will be used
///        by WStream instances to decide when a WBuffer should be freed.
/// @param cb Pointer to function that will be responsible for freeing
///        the buffer data(passing 'free' will work as expected).
/// @return The allocated WBuffer instance
WBuffer *wstream_new_buffer(char *data,
                            size_t size,
                            size_t refcount,
                            wbuffer_data_finalizer cb)
{
  WBuffer *rv = xmalloc(sizeof(WBuffer));
  rv->size = size;
  rv->refcount = refcount;
  rv->cb = cb;
  rv->data = data;

  return rv;
}

static void write_cb_sync(void *req)
{
  WRequest *data = req;

  data->wstream->curmem -= data->buffer->size;

  wstream_release_wbuffer(data->buffer);

  if (data->wstream->cb) {
    data->wstream->cb(data->wstream,
                      data->wstream->data,
                      data->status);
  }

  data->wstream->pending_reqs--;

  if (data->wstream->freed && data->wstream->pending_reqs == 0) {
    // Last pending write, free the wstream;
    if (data->wstream->free_handle) {
      uv_close((uv_handle_t *)data->wstream->stream, close_cb);
    } else {
      xfree(data->wstream);
    }
  }

  xfree(data);
}

static void write_cb(uv_write_t *req, int status)
{
  WRequest *data = req->data;
  data->status = status;
  event_push(write_cb_sync, req->data);
}

void wstream_release_wbuffer(WBuffer *buffer)
{
  if (!--buffer->refcount) {
    if (buffer->cb) {
      buffer->cb(buffer->data);
    }

    xfree(buffer);
  }
}

static void wstream_free_async(void **argv)
{
  WStream *wstream = argv[0];
  if (!wstream->pending_reqs) {
    if (wstream->free_handle) {
      uv_close((uv_handle_t *)wstream->stream, close_cb);
    } else {
      handle_set_wstream((uv_handle_t *)wstream->stream, NULL);
      xfree(wstream);
    }
  } else {
    wstream->freed = true;
  }
}

static void wstream_set_stream_async(void **argv)
{
  WStream *wstream = argv[0];
  uv_stream_t *stream = argv[1];
  handle_set_wstream((uv_handle_t *)stream, wstream);
  wstream->stream = stream;
}

static void wstream_set_file_async(void **argv)
{
  WStream *wstream = argv[0];
  uv_file file = (uv_file)(uintptr_t)argv[1];
  assert(uv_guess_handle(file) == UV_NAMED_PIPE ||
         uv_guess_handle(file) == UV_TTY);
  wstream->stream = xmalloc(sizeof(uv_pipe_t));
  uv_pipe_init(uv_default_loop(), (uv_pipe_t *)wstream->stream, 0);
  uv_pipe_open((uv_pipe_t *)wstream->stream, file);
  wstream->stream->data = NULL;
  handle_set_wstream((uv_handle_t *)wstream->stream, wstream);
  wstream->free_handle = true;
}

static void wstream_write_async(void **argv)
{
  WStream *wstream = argv[0];
  WBuffer *buffer = argv[1];
  bool *rv = argv[2];

  // This should not be called after a wstream was freed
  assert(!wstream->freed);

  if (wstream->curmem > wstream->maxmem) {
    goto err;
  }

  wstream->curmem += buffer->size;

  WRequest *data = xmalloc(sizeof(WRequest));
  data->wstream = wstream;
  data->buffer = buffer;
  data->uv_req.data = data;

  uv_buf_t uvbuf;
  uvbuf.base = buffer->data;
  uvbuf.len = buffer->size;

  if (uv_write(&data->uv_req, wstream->stream, &uvbuf, 1, write_cb)) {
    xfree(data);
    goto err;
  }

  wstream->pending_reqs++;
  *rv = true;
  return;

err:
  wstream_release_wbuffer(buffer);
  *rv = false;
}

static void close_cb(uv_handle_t *handle)
{
  xfree(handle_get_wstream(handle));
  xfree(handle->data);
  xfree(handle);
}

