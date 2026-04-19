
#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER ring_buffer_logic

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "ring_buffer_tp.hpp"

#if !defined(RING_BUFFER_TP_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define RING_BUFFER_TP_H

#include <lttng/tracepoint.h>

// Define the Write event
// clang-format off
TRACEPOINT_EVENT(
    ring_buffer_logic,
    write_attempt,
    TP_ARGS(
        size_t, index_arg,
        int, first_data_arg,
        int, thread_id_arg,
        const char*, status_arg
    ),
    TP_FIELDS(
        ctf_integer(size_t, index, index_arg)
        ctf_integer(int, first_data, first_data_arg)
        ctf_integer(int, thread_id, thread_id_arg)
        ctf_string(status, status_arg)
    )
)

// Define the Read event
TRACEPOINT_EVENT(
    ring_buffer_logic,
    read_attempt,
    TP_ARGS(
        size_t, index_arg,
        int, first_data_arg,
        int, thread_id_arg,
        const char*, status_arg
    ),
    TP_FIELDS(
        ctf_integer(size_t, index, index_arg)
        ctf_integer(int, first_data, first_data_arg)
        ctf_integer(int, thread_id, thread_id_arg)
        ctf_string(status, status_arg)
    )
)
// clang-format on

#endif /* RING_BUFFER_HPP */
#include <lttng/tracepoint-event.h>
