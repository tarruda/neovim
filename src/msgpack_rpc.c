#include <msgpack.h>

#include "msgpack_rpc.h"
#include "vim.h"
#include "memory.h"


bool msgpack_rpc_call(msgpack_object *req, msgpack_packer *res)
{
  // Validate the basic structure of the msgpack-rpc payload
  if (req->type != MSGPACK_OBJECT_ARRAY
      // Must be an array of size 4
      || req->via.array.size != 4
      // First item is the message type, it must be 0 which represents
      // a request
      || req->via.array.ptr[0].type != MSGPACK_OBJECT_POSITIVE_INTEGER
      || req->via.array.ptr[0].via.u64 != 0
      // Second item is the request id, it must be a positive integer
      || req->via.array.ptr[1].type != MSGPACK_OBJECT_POSITIVE_INTEGER
      // Third item is the API function id, also a positive integer
      || req->via.array.ptr[2].type != MSGPACK_OBJECT_POSITIVE_INTEGER
      // Last item is the function parameters, it must be an array
      || req->via.array.ptr[3].type != MSGPACK_OBJECT_ARRAY) {
    return msgpack_rpc_error(req, res, "Invalid msgpack-rpc request");
  }

  // dispatch the message
  return msgpack_rpc_dispatch(req, res);
}

void msgpack_rpc_response(msgpack_object *req, msgpack_packer *res)
{
  // Array of size 4
  msgpack_pack_array(res, 4);
  // Response type is 1
  msgpack_pack_int(res, 1);
  // Msgid is the same as the request
  msgpack_pack_int(res, req->via.array.ptr[1].via.u64);
}

void msgpack_rpc_success(msgpack_object *req, msgpack_packer *res)
{
  msgpack_rpc_response(req, res);
  // Nil error
  msgpack_pack_nil(res);
}

bool msgpack_rpc_error(msgpack_object *req, msgpack_packer *res, char *msg)
{
  size_t len = strlen(msg);

  msgpack_rpc_response(req, res);
  msgpack_pack_raw(res, len);
  msgpack_pack_raw_body(res, msg, len);
  // Nil result
  msgpack_pack_nil(res);

  return false;
}

char ** msgpack_rpc_array_argument(msgpack_object *obj)
{
  uint32_t i;
  char **rv = xmalloc(obj->via.array.size);

  for (i = 0; i < obj->via.array.size; i++) {
    rv[i] = msgpack_rpc_raw_argument(obj->via.array.ptr + i);
  }

  rv[i] = NULL;

  return rv;
}

char * msgpack_rpc_raw_argument(msgpack_object *obj)
{
  char *rv = xmalloc(obj->via.raw.size + 1);
  memcpy(rv, obj->via.raw.ptr, obj->via.raw.size);
  rv[obj->via.raw.size] = NUL;

  return rv;
}

uint32_t msgpack_rpc_integer_argument(msgpack_object *obj)
{
  return obj->via.u64;
}

bool msgpack_rpc_array_result(char **result,
                             msgpack_object *req,
                             msgpack_packer *res)
{
  uint32_t array_size = 0;
  char **ptr;

  // Count number of items in the array
  for (ptr = result; *ptr != NULL; ptr++) {
    array_size++;
  }

  msgpack_rpc_success(req, res);
  msgpack_pack_array(res, array_size);

  // push each string to the awway
  for (uint32_t i = 0; i < array_size; i++) {
    uint32_t raw_size = strlen(*ptr);
    msgpack_pack_raw(res, raw_size);
    msgpack_pack_raw_body(res, *ptr, raw_size);
  }

  return true;
}

bool msgpack_rpc_raw_result(char *result,
                             msgpack_object *req,
                             msgpack_packer *res)
{
  uint32_t raw_size = strlen(result);
  msgpack_rpc_success(req, res);
  msgpack_pack_raw(res, raw_size);
  msgpack_pack_raw_body(res, result, raw_size);
  return true;
}

bool msgpack_rpc_integer_result(uint32_t result,
                             msgpack_object *req,
                             msgpack_packer *res)
{
  msgpack_rpc_success(req, res);
  msgpack_pack_int(res, result);
  return true;
}

bool msgpack_rpc_void_result(msgpack_object *req, msgpack_packer *res)
{
  msgpack_rpc_success(req, res);
  msgpack_pack_nil(res);
  return true;
}
