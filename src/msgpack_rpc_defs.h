#ifndef NEOVIM_MSGPACK_RPC_DEFS_H
#define NEOVIM_MSGPACK_RPC_DEFS_H

typedef enum {
  kMsgpackRpcRequest = 0,
  kMsgpackRpcResponse = 1,
  kMsgpackRpcNotification = 2
} MsgpackRpcType;

#endif // NEOVIM_MSGPACK_RPC_DEFS_H
