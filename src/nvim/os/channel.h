#ifndef NVIM_OS_CHANNEL_H
#define NVIM_OS_CHANNEL_H

#include <uv.h>

#include "nvim/api/private/defs.h"
#include "nvim/vim.h"

#define EVENT_MAXLEN 512

/// Initializes the module
void channel_init(void);

/// Teardown the module
void channel_teardown(void);

/// Creates an API channel from a libuv stream representing a tcp or
/// pipe/socket client connection
///
/// @param stream The established connection
void channel_from_stream(uv_stream_t *stream);

/// Creates an API channel by starting a job and connecting to its
/// stdin/stdout. stderr is forwarded to the editor error stream.
///
/// @param argv The argument vector for the process
void channel_from_job(char **argv);

/// Sends event/data to channel
///
/// @param id The channel id. If 0, the event will be sent to all
///        channels that have subscribed to the event type
/// @param type The event type, an arbitrary string
/// @param obj The event data
/// @return True if the data was sent successfully, false otherwise.
bool channel_send_event(uint64_t id, char *type, Object data);

/// Subscribes to event broadcasts
///
/// @param id The channel id
/// @param event The event type string
void channel_subscribe(uint64_t id, char *event);

/// Unsubscribes to event broadcasts
///
/// @param id The channel id
/// @param event The event type string
void channel_unsubscribe(uint64_t id, char *event);

#endif  // NVIM_OS_CHANNEL_H

