/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Streambuffer Type Definitions & Structures
 * - This header describes the base streambuffer-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
//#define __TRACE

#define __need_minmax
#include <ds/streambuffer.h>
#include <ds/ds.h>
#include <limits.h>
#include <os/futex.h>
#include <string.h>

#define STREAMBUFFER_CAN_OVERWRITE(stream) ((stream)->options & STREAMBUFFER_OVERWRITE_ENABLED)

#define STREAMBUFFER_PARTIAL_OP(options)  ((options) & STREAMBUFFER_ALLOW_PARTIAL)
#define STREAMBUFFER_PARTIAL_OP_SAFE(options, bytes_available)  (STREAMBUFFER_PARTIAL_OP(options) && (bytes_available) != 0)

#define STREAMBUFFER_CAN_READ(options, bytes_available, length)           ((bytes_available) == (length) || STREAMBUFFER_PARTIAL_OP_SAFE(options, bytes_available))
#define STREAMBUFFER_CAN_WRITE(options, bytes_available, length)          ((bytes_available) == (length) || STREAMBUFFER_PARTIAL_OP(options))
#define STREAMBUFFER_CAN_STREAM(stream, options, bytes_available, length) ((bytes_available) == (length) || STREAMBUFFER_CAN_OVERWRITE(stream) || STREAMBUFFER_PARTIAL_OP_SAFE(options, bytes_available))

#define STREAMBUFFER_CAN_BLOCK(options)           (((options) & STREAMBUFFER_NO_BLOCK) == 0)
#define STREAMBUFFER_HAS_MULTIPLE_READERS(stream) ((stream)->options & STREAMBUFFER_MULTIPLE_READERS)
#define STREAMBUFFER_HAS_MULTIPLE_WRITERS(stream) ((stream)->options & STREAMBUFFER_MULTIPLE_WRITERS)
#define STREAMBUFFER_WAIT_FLAGS(stream)           (((stream)->options & STREAMBUFFER_GLOBAL) ? FUTEX_FLAG_WAIT : (FUTEX_FLAG_WAIT | FUTEX_FLAG_PRIVATE))
#define STREAMBUFFER_WAKE_FLAGS(stream)           (((stream)->options & STREAMBUFFER_GLOBAL) ? FUTEX_FLAG_WAKE : (FUTEX_FLAG_WAKE | FUTEX_FLAG_PRIVATE))

typedef struct sb_packethdr {
    size_t packet_len;
} sb_packethdr_t;

static void
streambuffer_dump(
    _In_ streambuffer_t* stream)
{
    dstrace("[dump] capacity 0x%" PRIxIN ", options 0x%x", stream->capacity, stream->options);
    dstrace("[dump] buffer at 0x%" PRIxIN, &stream->buffer[0]);
    dstrace("[dump] producer_index %u, producer_comitted_index %u",
        atomic_load(&stream->producer_index), atomic_load(&stream->producer_comitted_index));
    dstrace("[dump] consumer_index %u, consumer_comitted_index %u",
        atomic_load(&stream->consumer_index), atomic_load(&stream->consumer_comitted_index));
    dstrace("[dump] producer_count %u, consumer_count %u",
        atomic_load(&stream->producer_count), atomic_load(&stream->consumer_count));
}

void
streambuffer_construct(
    _In_ streambuffer_t* stream,
    _In_ size_t          capacity,
    _In_ unsigned int    options)
{
    memset(stream, 0, sizeof(streambuffer_t));
    stream->capacity = capacity;
    stream->options  = options;
}

oserr_t
streambuffer_create(
    _In_  size_t           capacity,
    _In_  unsigned int     options,
    _Out_ streambuffer_t** stream_out)
{
    // When calculating the number of bytes we want to actual structure size
    // without the buffer[1] and then capacity
    streambuffer_t* stream = (streambuffer_t*)dsalloc(sizeof(streambuffer_t) + capacity);
    if (!stream) {
        return OS_EOOM;
    }
    
    streambuffer_construct(stream, capacity, options);
    *stream_out = stream;
    return OS_EOK;
}

void
streambuffer_set_option(
    _In_ streambuffer_t* stream,
    _In_ unsigned int    option)
{
    stream->options |= option;
}

int
streambuffer_has_option(
    _In_ streambuffer_t* stream,
    _In_ unsigned int    option)
{
    return (stream->options & option) != 0;
}

void
streambuffer_clear_option(
    _In_ streambuffer_t* stream,
    _In_ unsigned int    option)
{
    stream->options &= ~(option);
}

static inline size_t
bytes_writable(
    _In_ size_t capacity,
    _In_ size_t read_index,
    _In_ size_t write_index)
{
    if (read_index > write_index) {
        // If we somehow ended up in a state where read_index got ahead of write_index
        // fix this by treating us with an empty capacity
        if ((UINT_MAX - read_index) - write_index > capacity) {
            return capacity;
        }
        return capacity - (write_index - (read_index - UINT_MAX));
    }
    return capacity - (write_index - read_index);
}

static inline size_t
bytes_readable(
    _In_ size_t capacity,
    _In_ size_t read_index,
    _In_ size_t write_index)
{
    // Handle wrap-around
    if (read_index > write_index) {
        if (read_index >= (UINT_MAX - capacity)) {
            // (capacity - (read_index & (capacity- 1))) + (write_index & (capacity - 1)) - 1;
            return (capacity - (read_index % capacity)) + (write_index % capacity) - 1;
        }
        else {
            return 0; // Overcommitted
        }
    }
    return write_index - read_index;
}

static void
streambuffer_try_truncate(
    _In_ streambuffer_t* stream,
    _In_ unsigned int    options,
    _In_ size_t          length)
{
    // when we check, we must check how many bytes are actually allocated, not currently comitted
    // as we have to take into account current readers. The write-index however
    // we have to only take into account how many bytes are actually comitted
    unsigned int write_index     = atomic_load(&stream->producer_comitted_index);
    unsigned int read_index      = atomic_load(&stream->consumer_index);
    size_t       bytes_available = MIN(
        bytes_readable(stream->capacity, read_index, write_index), 
        length);
    size_t       bytes_comitted  = bytes_available;
    if (!STREAMBUFFER_CAN_READ(options, bytes_available, length)) {
        // should not happen but abort if this occurs
        return;
    }

    // Perform the actual allocation, if this fails someone else is truncating
    // or reading, abort
    if (!atomic_compare_exchange_strong(&stream->consumer_index, 
            &read_index, read_index + bytes_available)) {
        return;
    }
    
    // Synchronize with other consumers, we must wait for our turn to increament
    // the comitted index, otherwise we could end up telling writers that the wrong
    // index is writable. This can be skipped for single reader
    if (STREAMBUFFER_HAS_MULTIPLE_READERS(stream)) {
        unsigned int current_commit = atomic_load(&stream->consumer_comitted_index);
        while (current_commit < (read_index - bytes_comitted)) {
            current_commit = atomic_load(&stream->consumer_comitted_index);
        }
    }
    atomic_fetch_add(&stream->consumer_comitted_index, bytes_comitted);
}

void
streambuffer_get_bytes_available_in(
    _In_  streambuffer_t* stream,
    _Out_ size_t*         bytesAvailableOut)
{
    if (!stream || !bytesAvailableOut) {
        return;
    }

    unsigned int write_index     = atomic_load(&stream->producer_comitted_index);
    unsigned int read_index      = atomic_load(&stream->consumer_index);
    size_t       bytes_available = bytes_readable(stream->capacity, read_index, write_index);
    *bytesAvailableOut = bytes_available;
}

void
streambuffer_get_bytes_available_out(
    _In_ streambuffer_t* stream,
    _Out_ size_t*        bytesAvailableOut)
{
    unsigned int write_index     = atomic_load(&stream->producer_index);
    unsigned int read_index      = atomic_load(&stream->consumer_comitted_index);
    size_t       bytes_available = bytes_writable(stream->capacity, read_index, write_index);
    *bytesAvailableOut = bytes_available;
}

size_t
streambuffer_stream_out(
        _In_ streambuffer_t*            stream,
        _In_ void*                      buffer,
        _In_ size_t                     length,
        _In_ streambuffer_rw_options_t* options)
{
    const uint8_t*      casted_ptr    = (const uint8_t*)buffer;
    size_t              bytes_written = 0;
    OSFutexParameters_t parameters;
    dstrace("[streambuffer_stream_out] 0x%" PRIxIN ", length %" PRIuIN ", options 0x%x",
        buffer, length, options);
    //streambuffer_dump(stream);
    
    // Has the streambuffer been disabled?
    if (stream->options & STREAMBUFFER_DISABLED) {
        return 0;
    }
    
    // Guard against bad lengths
    if (!length) {
        return 0;
    }

    // Make sure we write all the bytes
    while (bytes_written < length) {
        // when we check, we must check how many bytes are actually allocated, not committed
        // as we have to take into account current writers. The read index however
        // we have to only take into account how many bytes are actually read
        unsigned int write_index     = atomic_load(&stream->producer_index);
        unsigned int read_index      = atomic_load(&stream->consumer_comitted_index);
        size_t       bytes_available = MIN(
            bytes_writable(stream->capacity, read_index, write_index),
            length - bytes_written);
        size_t       bytes_comitted  = bytes_available;
        if (!STREAMBUFFER_CAN_STREAM(stream, options->flags, bytes_available, (length - bytes_written))) {
            if (!STREAMBUFFER_CAN_BLOCK(options->flags)) {
                break;
            }
            
            parameters.Futex0    = (atomic_int*)&stream->consumer_comitted_index;
            parameters.Expected0 = (int)read_index;
            parameters.Deadline  = options->deadline;
            parameters.Flags     = STREAMBUFFER_WAIT_FLAGS(stream);
            atomic_fetch_add(&stream->producer_count, 1);
            dswait(&parameters, options->async_context);
            continue; // Start over
        }
        
        // Handle overwrite, empty the queue by the needed amount of bytes
        if (bytes_available < (length - bytes_written)) {
            streambuffer_try_truncate(stream, options->flags, (length - bytes_written));
            continue;
        }
        
        // Perform the actual allocation
        if (!atomic_compare_exchange_strong(&stream->producer_index, 
                &write_index, write_index + bytes_available)) {
            continue;
        }

        // Write the data to the internal buffer
        while (bytes_available--) {
            // (write_index++ & (stream->capacity - 1))
            stream->buffer[(write_index++ % stream->capacity)] = casted_ptr[bytes_written++];
        }
        
        // Synchronize with other producers, we must wait for our turn to increament
        // the comitted index, otherwise we could end up telling readers that the wrong
        // index is readable. This can be skipped for single writer
        if (STREAMBUFFER_HAS_MULTIPLE_WRITERS(stream)) {
            unsigned int current_commit = atomic_load(&stream->producer_comitted_index);
            while (current_commit < (write_index - bytes_comitted)) {
                current_commit = atomic_load(&stream->producer_comitted_index);
            }
        }

        atomic_fetch_add(&stream->producer_comitted_index, bytes_comitted);
        parameters.Expected0 = atomic_exchange(&stream->consumer_count, 0);
        if (parameters.Expected0 != 0) {
            parameters.Futex0 = (atomic_int*)&stream->producer_comitted_index;
            parameters.Flags  = STREAMBUFFER_WAKE_FLAGS(stream);
            dswake(&parameters);
        }
    }
    return bytes_written;
}

size_t
streambuffer_write_packet_start(
        _In_ streambuffer_t*            stream,
        _In_ size_t                     length,
        _In_ streambuffer_rw_options_t* options,
        _In_ streambuffer_packet_ctx_t* packetCtx)
{
    size_t              bytes_allocated = 0;
    OSFutexParameters_t parameters;
    size_t              adjusted_length;
    sb_packethdr_t      header = { .packet_len = length };
    
    // Has the streambuffer been disabled?
    if (stream->options & STREAMBUFFER_DISABLED) {
        return 0;
    }

    // Guard against bad lengths
    if (!length || length > (stream->capacity - sizeof(sb_packethdr_t))) {
        return 0;
    }

    adjusted_length = length + sizeof(sb_packethdr_t);
    
    // Make sure we write all the bytes in one go
    while (!bytes_allocated) {
        // when we check, we must check how many bytes are actually allocated, not committed
        // as we have to take into account current writers. The read index however
        // we have to only take into account how many bytes are actually read
        unsigned int write_index     = atomic_load(&stream->producer_index);
        unsigned int read_index      = atomic_load(&stream->consumer_comitted_index);
        size_t       bytes_available = MIN(
            bytes_writable(stream->capacity, read_index, write_index),
            adjusted_length);
        
        if (bytes_available < adjusted_length) {
            if (!STREAMBUFFER_CAN_BLOCK(options->flags)) {
                break;
            }
            
            parameters.Futex0    = (atomic_int*)&stream->consumer_comitted_index;
            parameters.Expected0 = (int)read_index;
            parameters.Deadline  = options->deadline;
            parameters.Flags     = STREAMBUFFER_WAIT_FLAGS(stream);
            atomic_fetch_add(&stream->producer_count, 1);
            dswait(&parameters, options->async_context);
            continue; // Start over
        }
        
        // Perform the actual allocation
        if (!atomic_compare_exchange_strong(&stream->producer_index, 
                &write_index, write_index + bytes_available)) {
            continue;
        }
        
        // Store base before writing the packet header
        packetCtx->_stream = stream;
        packetCtx->_base   = write_index;
        packetCtx->_state  = write_index;
        packetCtx->_length = header.packet_len;
        streambuffer_write_packet_data(
                &header,
                sizeof(sb_packethdr_t),
                packetCtx
        );
        bytes_allocated = header.packet_len;
    }
    return bytes_allocated;
}

void
streambuffer_write_packet_data(
        _In_ void*                      buffer,
        _In_ size_t                     length,
        _In_ streambuffer_packet_ctx_t* packetCtx)
{
    streambuffer_t* stream        = packetCtx->_stream;
    uint8_t*        casted_ptr    = (uint8_t*)buffer;
    unsigned int    write_index   = (packetCtx->_state % stream->capacity);
    size_t          bytes_written = 0;
    
    while (length--) {
        // write_index++ & (stream->capacity - 1)
        stream->buffer[write_index++] = casted_ptr[bytes_written++];
        if (write_index == stream->capacity) {
            write_index = 0;
        }
    }

    packetCtx->_state = write_index;
}

void
streambuffer_write_packet_end(
        _In_ streambuffer_packet_ctx_t* packetCtx)
{
    streambuffer_t*     stream = packetCtx->_stream;
    OSFutexParameters_t parameters;
    size_t              adjusted_length;
    
    // Synchronize with other producers, we must wait for our turn to increament
    // the comitted index, otherwise we could end up telling readers that the wrong
    // index is readable. This can be skipped for single writer
    if (STREAMBUFFER_HAS_MULTIPLE_WRITERS(stream)) {
        unsigned int write_index    = packetCtx->_base;
        unsigned int current_commit = atomic_load(&stream->producer_comitted_index);
        while (current_commit < write_index) {
            current_commit = atomic_load(&stream->producer_comitted_index);
        }
    }

    adjusted_length = packetCtx->_length + sizeof(sb_packethdr_t);
    atomic_fetch_add(&stream->producer_comitted_index, adjusted_length);
    parameters.Expected0 = atomic_exchange(&stream->consumer_count, 0);
    if (parameters.Expected0 != 0) {
        parameters.Futex0 = (atomic_int*)&stream->producer_comitted_index;
        parameters.Flags  = STREAMBUFFER_WAKE_FLAGS(stream);
        dswake(&parameters);
    }
}

size_t
streambuffer_stream_in(
        _In_ streambuffer_t*            stream,
        _In_ void*                      buffer,
        _In_ size_t                     length,
        _In_ streambuffer_rw_options_t* options)
{
    uint8_t*          casted_ptr = (uint8_t*)buffer;
    size_t              bytes_read = 0;
    OSFutexParameters_t parameters;
    dstrace("[streambuffer_stream_in] 0x%" PRIxIN ", length %" PRIuIN ", options 0x%x",
        buffer, length, options);
    //streambuffer_dump(stream);
    
    // Has the streambuffer been disabled?
    if (stream->options & STREAMBUFFER_DISABLED) {
        return 0;
    }

    // Make sure there are bytes to read
    while (bytes_read < length) {
        // when we check, we must check how many bytes are actually allocated, not currently comitted
        // as we have to take into account current readers. The write-index however
        // we have to only take into account how many bytes are actually comitted
        unsigned int write_index     = atomic_load(&stream->producer_comitted_index);
        unsigned int read_index      = atomic_load(&stream->consumer_index);
        size_t       bytes_available = MIN(
            bytes_readable(stream->capacity, read_index, write_index), 
            length - bytes_read);
        size_t       bytes_comitted  = bytes_available;
        if (!STREAMBUFFER_CAN_READ(options->flags, bytes_available, (length - bytes_read))) {
            if (!STREAMBUFFER_CAN_BLOCK(options->flags)) {
                break;
            }
            
            parameters.Futex0    = (atomic_int*)&stream->producer_comitted_index;
            parameters.Expected0 = (int)write_index;
            parameters.Deadline  = options->deadline;
            parameters.Flags     = STREAMBUFFER_WAIT_FLAGS(stream);
            atomic_fetch_add(&stream->consumer_count, 1);
            dswait(&parameters, options->async_context);
            continue; // Start over
        }

        // Perform the actual allocation
        if (!atomic_compare_exchange_strong(&stream->consumer_index, 
                &read_index, read_index + bytes_available)) {
            continue;
        }
        
        // Write the data to the provided buffer
        while (bytes_available--) {
            // read_index++ & (stream->capacity - 1)
            casted_ptr[bytes_read++] = stream->buffer[(read_index++ % stream->capacity)];
        }
        
        // Synchronize with other consumers, we must wait for our turn to increament
        // the comitted index, otherwise we could end up telling writers that the wrong
        // index is writable. This can be skipped for single reader
        if (STREAMBUFFER_HAS_MULTIPLE_READERS(stream)) {
            unsigned int current_commit = atomic_load(&stream->consumer_comitted_index);
            while (current_commit < (read_index - bytes_comitted)) {
                current_commit = atomic_load(&stream->consumer_comitted_index);
            }
        }

        atomic_fetch_add(&stream->consumer_comitted_index, bytes_comitted);
        parameters.Expected0 = atomic_exchange(&stream->producer_count, 0);
        if (parameters.Expected0 != 0) {
            parameters.Futex0 = (atomic_int*)&stream->consumer_comitted_index;
            parameters.Flags  = STREAMBUFFER_WAKE_FLAGS(stream);
            dswake(&parameters);
        }
        break;
    }
    return bytes_read;
}

size_t
streambuffer_read_packet_start(
        _In_ streambuffer_t*            stream,
        _In_ streambuffer_rw_options_t* options,
        _In_ streambuffer_packet_ctx_t* packetCtx)
{
    size_t              bytes_read = 0;
    sb_packethdr_t      header;
    OSFutexParameters_t parameters;
    //streambuffer_dump(stream);
    
    // Has the streambuffer been disabled?
    if (stream->options & STREAMBUFFER_DISABLED) {
        return 0;
    }

    // Make sure there are bytes to read
    while (!bytes_read) {
        // when we check, we must check how many bytes are actually allocated, not currently comitted
        // as we have to take into account current readers. The write-index however
        // we have to only take into account how many bytes are actually comitted
        unsigned int write_index     = atomic_load(&stream->producer_comitted_index);
        unsigned int read_index      = atomic_load(&stream->consumer_index);
        size_t       bytes_available = bytes_readable(stream->capacity, read_index, write_index);
        size_t       length          = sizeof(sb_packethdr_t);
        
        // Validate that it is indeed a header we are looking at, and then readjust
        // the number of bytes available, since we want to block as long as the entire packet
        // is not written into the pipe
        if (bytes_available) {
            streambuffer_read_packet_data(
                    &header,
                    sizeof(sb_packethdr_t),
                    &(streambuffer_packet_ctx_t) {
                        ._stream = stream,
                        ._state = read_index,
                    }
            );
            length = header.packet_len + sizeof(sb_packethdr_t);
        }
        bytes_available = MIN(bytes_available, length);
        
        if (bytes_available < length) {
            if (!STREAMBUFFER_CAN_BLOCK(options->flags)) {
                break;
            }
            
            parameters.Futex0    = (atomic_int*)&stream->producer_comitted_index;
            parameters.Expected0 = (int)write_index;
            parameters.Deadline  = options->deadline;
            parameters.Flags     = STREAMBUFFER_WAIT_FLAGS(stream);
            atomic_fetch_add(&stream->consumer_count, 1);
            dswait(&parameters, options->async_context);
            continue; // Start over
        }
        
        // Perform the actual allocation if PEEK was not specified.
        if (!(options->flags & STREAMBUFFER_PEEK)) {
            if (!atomic_compare_exchange_strong(&stream->consumer_index, 
                    &read_index, read_index + bytes_available)) {
                continue;
            }
        }

        // Set the base at the actual base, but adjust the state_index so the user
        // of this do not read the sb_packethdr_t instance
        bytes_read = bytes_available - sizeof(sb_packethdr_t);
        packetCtx->_stream = stream;
        packetCtx->_base   = read_index;
        packetCtx->_state  = read_index + sizeof(sb_packethdr_t);
        packetCtx->_length = bytes_read;
    }
    return bytes_read;
}

void
streambuffer_read_packet_data(
        _In_ void*                      buffer,
        _In_ size_t                     length,
        _In_ streambuffer_packet_ctx_t* packetCtx)
{
    streambuffer_t* stream = packetCtx->_stream;
    uint8_t*        casted_ptr = (uint8_t*)buffer;
    unsigned int    read_index = (packetCtx->_state % stream->capacity);
    size_t          bytes_read = 0;
    
    // Write the data to the provided buffer
    while (length--) {
        casted_ptr[bytes_read++] = stream->buffer[read_index++];
        if (read_index == stream->capacity) {
            read_index = 0;
        }
    }

    packetCtx->_state = read_index;
}

void
streambuffer_read_packet_end(
        _In_ streambuffer_packet_ctx_t* packetCtx)
{
    OSFutexParameters_t parameters;
    streambuffer_t*     stream = packetCtx->_stream;

    // Synchronize with other consumers, we must wait for our turn to increament
    // the comitted index, otherwise we could end up telling writers that the wrong
    // index is writable. This can be skipped for single reader
    if (STREAMBUFFER_HAS_MULTIPLE_READERS(stream)) {
        unsigned int read_index     = packetCtx->_base;
        unsigned int current_commit = atomic_load(&stream->consumer_comitted_index);
        while (current_commit < read_index) {
            current_commit = atomic_load(&stream->consumer_comitted_index);
        }
    }

    // Take into account an invisible instance of sb_packethdr_t
    packetCtx->_length += sizeof(sb_packethdr_t);
    
    atomic_fetch_add(&stream->consumer_comitted_index, packetCtx->_length);
    parameters.Expected0 = atomic_exchange(&stream->producer_count, 0);
    if (parameters.Expected0 != 0) {
        parameters.Futex0 = (atomic_int*)&stream->consumer_comitted_index;
        parameters.Flags  = STREAMBUFFER_WAKE_FLAGS(stream);
        dswake(&parameters);
    }
}

#if 0
int main()
{
    cout << "read: 0, write: 15 == " << bytes_readable(128, 0, 15) << endl; // == 15
    cout << "read: 120, write: 130 == " << bytes_readable(128, 120, 130) << endl; // == 10
    cout << "read: 130, write: 200 == " << bytes_readable(128, 130, 200) << endl; // == 70
    cout << "read: 20, write: 15 == " << bytes_readable(128, 20, 15) << endl; // == 0

    // test rollover
    unsigned int almost_at_max = UINT_MAX - 15;
    cout << "read: " << almost_at_max << ", write: 15 == " << bytes_readable(128, almost_at_max, 15) << endl << endl; // == 30
    
    cout << "write: 0, read: 15 == " << bytes_writable(128, 15, 0) << endl; // == 128
    cout << "write: 120, read: 130 == " << bytes_writable(128, 130, 120) << endl; // == 128
    cout << "write: 200, read: 130 == " << bytes_writable(128, 130, 200) << endl; // == 58
    cout << "write: 20, read: 15 == " << bytes_writable(128, 15, 20) << endl; // == 123
    
    // test rollover
    almost_at_max = UINT_MAX - 15;
    cout << "read: " << almost_at_max << ", write: 15 == " << bytes_writable(128, almost_at_max, 15) << endl; // == 98
    return 0;
}
#endif
