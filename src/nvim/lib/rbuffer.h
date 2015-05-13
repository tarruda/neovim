// Ring buffer implementation. This is basically an array that wraps read/write
// pointers around the memory region, and should be more efficient than the old
// RBuffer which required memmove() calls to relocate read/write positions.
//
// The main purpose of using this structure is simplify memory management when
// reading from uv_stream_t instances:
//
// - The event loop writes data to a RBuffer, advancing the write pointer
// - The main loop reads data, advancing the read pointer
// - If the buffer becomes full(size == capacity) the rstream is temporarily
//   stopped(automatic backpressure handling)
//
// Reference: http://en.wikipedia.org/wiki/Circular_buffer
#ifndef NVIM_LIB_RBUFFER_H
#define NVIM_LIB_RBUFFER_H

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "nvim/memory.h"
#include "nvim/vim.h"

typedef struct rbuffer RBuffer;
/// Type of function invoked during certain events:
///   - When the RBuffer switches to the full state
///   - When the RBuffer switches to the non-full state
typedef void(*rbuffer_callback)(RBuffer *buf, void *data);

struct rbuffer {
  rbuffer_callback full_cb, nonfull_cb;
  void *data;
  size_t size;
  char *end_ptr, *read_ptr, *write_ptr;
  char start_ptr[];
};

/// Creates a new `RBuffer` instance.
static inline RBuffer *rbuffer_new(size_t capacity)
{
  if (!capacity) {
    capacity = 0xffff;
  }

  RBuffer *rv = xmalloc(sizeof(RBuffer) + capacity);
  rv->full_cb = rv->nonfull_cb = NULL;
  rv->data = NULL;
  rv->size = 0;
  rv->write_ptr = rv->read_ptr = rv->start_ptr;
  rv->end_ptr = rv->start_ptr + capacity;
  return rv;
}

static inline void rbuffer_free(RBuffer *buf)
{
  xfree(buf);
}

static inline size_t rbuffer_capacity(RBuffer *buf) FUNC_ATTR_NONNULL_ALL
{
  return (size_t)(buf->end_ptr - buf->start_ptr);
}

static inline size_t rbuffer_space(RBuffer *buf) FUNC_ATTR_NONNULL_ALL
{
  return rbuffer_capacity(buf) - buf->size;
}

/// Return a pointer to a raw buffer containing the first empty slot available
/// for writing. The second argument is a pointer to the maximum number of
/// bytes that should be written.
/// 
/// It is necessary to call this function twice to ensure all empty space was
/// used. See RBUFFER_WHILE_NOT_FULL for a macro that simplifies this task.
static inline char *rbuffer_write_ptr(RBuffer *buf, size_t *write_count)
  FUNC_ATTR_NONNULL_ALL
{
  if (buf->write_ptr >= buf->read_ptr) {
    *write_count = (size_t)(buf->end_ptr - buf->write_ptr);
  } else {
    *write_count = (size_t)(buf->read_ptr - buf->write_ptr);
  }

  if (!*write_count) {
    return NULL;
  }

  return buf->write_ptr;
}

/// Adjust `rbuffer` write pointer to reflect produced data. This is called
/// automatically by `rbuffer_write`, but when using `rbuffer_write_ptr`
/// directly, this needs to called after the data was written.
static inline void rbuffer_produced(RBuffer *buf, size_t count)
  FUNC_ATTR_NONNULL_ALL
{
  assert(buf->write_ptr >= buf->read_ptr ?
      buf->write_ptr + count <= buf->end_ptr :
      buf->write_ptr + count <= buf->read_ptr);

  buf->write_ptr += count;
  if (buf->write_ptr == buf->end_ptr) {
    // wrap
    buf->write_ptr = buf->start_ptr;
  }
  buf->size += count;
  if (buf->full_cb && rbuffer_space(buf)) {
    buf->full_cb(buf, buf->data);
  }
}

/// Return a pointer to a raw buffer containing the first byte available
/// for reading. The second argument is a pointer to the maximum number of
/// bytes that should be read.
/// 
/// It is necessary to call this function twice to ensure all available bytes
/// were read. See RBUFFER_WHILE_NOT_EMPTY for a macro that simplifies this task.
static inline char *rbuffer_read_ptr(RBuffer *buf, size_t *read_count)
  FUNC_ATTR_NONNULL_ALL
{
  if (!buf->size) {
    return NULL;
  }

  if (buf->read_ptr < buf->write_ptr) {
    *read_count = (size_t)(buf->write_ptr - buf->read_ptr);
  } else {
    *read_count = (size_t)(buf->end_ptr - buf->read_ptr);
  }

  return buf->read_ptr;
}

/// Adjust `rbuffer` read pointer to reflect consumed data. This is called
/// automatically by `rbuffer_read`, but when using `rbuffer_read_ptr`
/// directly, this needs to called after the data was read.
static inline void rbuffer_consumed(RBuffer *buf, size_t count)
  FUNC_ATTR_NONNULL_ALL
{
  assert(buf->read_ptr < buf->write_ptr ?
      buf->read_ptr + count <= buf->write_ptr : 
      buf->read_ptr + count <= buf->end_ptr);

  bool was_full = rbuffer_space(buf);
  buf->read_ptr += count;
  if (buf->read_ptr == buf->end_ptr) {
    // wrap
    buf->read_ptr = buf->start_ptr;
  }
  buf->size -= count;
  if (buf->nonfull_cb && was_full) {
    buf->nonfull_cb(buf, buf->data);
  }
}

// Macros that simplify working with the read/write pointers directly by hiding
// ring buffer wrap logic. Some examples:
//
// - Pass the write pointer to a function(`write_data`) that incrementally
//   produces data, returning the number of bytes actually written:
// 
//       RBUFFER_WHILE_NOT_FULL(rbuf, wptr, wcnt)
//         rbuffer_produced(rbuf, write_data(state, wptr, wcnt));
//
// - Pass the read pointer to a function(`read_data`) that incrementally
//   consumes data, return the number of bytes actually read:
//
//       RBUFFER_WHILE_NOT_EMPTY(rbuf, rptr, rcnt)
//         rbuffer_consumed(rbuf, read_data(state, rptr, rcnt));
//
#define RBUFFER_WHILE_NOT_FULL(buf, wptr, wcnt)             \
  for (size_t wcnt = 0, d = 0; !d; )                        \
    for (char *wptr = rbuffer_write_ptr(buf, &wcnt);        \
         wptr != NULL && d++ == 0;                          \
         wptr = rbuffer_write_ptr(buf, &wcnt))


#define RBUFFER_WHILE_NOT_EMPTY(buf, rptr, rcnt)            \
  for (size_t rcnt = 0, d = 0; !d; )                        \
    for (char *rptr = rbuffer_read_ptr(buf, &rcnt);         \
         rptr != NULL && d++ == 0;                          \
         rptr = rbuffer_read_ptr(buf, &rcnt))


// Higher level functions for copying from/to RBuffer instances and data
// pointers
static inline size_t rbuffer_write(RBuffer *buf, char *src, size_t src_size)
  FUNC_ATTR_NONNULL_ALL
{
  size_t size = src_size;

  RBUFFER_WHILE_NOT_FULL(buf, wptr, wcnt) {
    size_t copy_count = MIN(src_size, wcnt);
    memcpy(wptr, src, copy_count);
    rbuffer_produced(buf, copy_count);
    src += copy_count;
    src_size -= copy_count;
  }

  return size - src_size;
}

static inline size_t rbuffer_read(RBuffer *buf, char *dst, size_t dst_size)
  FUNC_ATTR_NONNULL_ALL
{
  size_t size = dst_size;

  RBUFFER_WHILE_NOT_EMPTY(buf, rptr, rcnt) {
    size_t copy_count = MIN(dst_size, rcnt);
    memcpy(dst, rptr, copy_count);
    rbuffer_consumed(buf, copy_count);
    dst += copy_count;
    dst_size -= copy_count;
  }

  return size - dst_size;
}

#endif  // NVIM_LIB_RBUFFER_H
