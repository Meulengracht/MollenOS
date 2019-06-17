/* MollenOS
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Ringbuffer Type Definitions & Structures
 * - This header describes the base ringbuffer-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/ringbuffer.h>
#include <limits.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>
#include <os/futex.h>
#include <stdlib.h>
#include <string.h>

#define RINGBUFFER_CAN_OVERWRITE(ring) (ring->options & RINGBUFFER_OVERWRITE_ENABLED)

#define RINGBUFFER_CAN_READ_PARTIAL(ring, bytes_available)  ((ring->options & RINGBUFFER_ALLOW_PARTIAL_READS) && bytes_available != 0)
#define RINGBUFFER_CAN_WRITE_PARTIAL(ring, bytes_available) ((ring->options & RINGBUFFER_ALLOW_PARTIAL_WRITES) && bytes_available != 0)

#define RINGBUFFER_CAN_READ(ring, bytes_available, length)  (bytes_available == length || RINGBUFFER_CAN_READ_PARTIAL(ring, bytes_available))
#define RINGBUFFER_CAN_WRITE(ring, bytes_available, length) (bytes_available == length || RINGBUFFER_CAN_OVERWRITE(ring) || RINGBUFFER_CAN_WRITE_PARTIAL(ring, bytes_available))

#define RINGBUFFER_CAN_BLOCK_READER(ring)     ((ring->options & RINGBUFFER_NO_READ_BLOCK) == 0)
#define RINGBUFFER_CAN_BLOCK_WRITER(ring)     ((ring->options & RINGBUFFER_NO_WRITE_BLOCK) == 0)
#define RINGBUFFER_HAS_MULTIPLE_READERS(ring) (ring->options & RINGBUFFER_MULTIPLE_READERS)
#define RINGBUFFER_HAS_MULTIPLE_WRITERS(ring) (ring->options & RINGBUFFER_MULTIPLE_WRITERS)
#define RINGBUFFER_WAIT_FLAGS(ring)           ((ring->options & RINGBUFFER_GLOBAL) ? 0 : FUTEX_WAIT_PRIVATE)
#define RINGBUFFER_WAKE_FLAGS(ring)           ((ring->options & RINGBUFFER_GLOBAL) ? 0 : FUTEX_WAKE_PRIVATE)

void
ringbuffer_construct(
    _In_ ringbuffer_t* ring,
    _In_ size_t        capacity,
    _In_ uint8_t*      buffer,
    _In_ unsigned int  options)
{
    memset(ring, 0, sizeof(ringbuffer_t));
    ring->capacity = capacity;
    ring->buffer   = buffer;
    ring->options  = options;
}

OsStatus_t
ringbuffer_create(
    _In_  size_t         capacity,
    _In_  unsigned int   options,
    _Out_ ringbuffer_t** ring_out)
{
    ringbuffer_t* ring = (ringbuffer_t*)malloc(sizeof(ringbuffer_t));
    uint8_t*      buffer;
    
    if (!ring) {
        return OsOutOfMemory;
    }
    
    buffer = (uint8_t*)malloc(capacity);
    if (!buffer) {
        free((void*)ring);
        return OsOutOfMemory;
    }
    
    ringbuffer_construct(ring, capacity, buffer, options);
    return OsSuccess;
}

void
ringbuffer_set_option(
    _In_ ringbuffer_t* ring,
    _In_ unsigned int  option)
{
    ring->options |= option;
}

void
ringbuffer_clear_option(
    _In_ ringbuffer_t* ring,
    _In_ unsigned int  option)
{
    ring->options &= ~(option);
}

static inline size_t
bytes_writable(
    _In_ size_t capacity,
    _In_ size_t read_index,
    _In_ size_t write_index)
{
    // Handle wrap-around
    if (read_index > write_index) {
        if (read_index >= (UINT_MAX - capacity)) {
            return (read_index & (capacity - 1)) - (write_index & (capacity - 1));
        }
        else {
            return 0; // Overcommitted
        }
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
            return (capacity - (read_index & (capacity- 1))) + 
                (write_index & (capacity - 1)) - 1;
        }
        else {
            return 0; // Overcommitted
        }
    }
    return write_index - read_index;
}

static void
ringbuffer_try_truncate(
    _In_ ringbuffer_t* ring,
    _In_ size_t        length)
{
    // when we check, we must check how many bytes are actually allocated, not currently comitted
    // as we have to take into account current readers. The write index however
    // we have to only take into account how many bytes are actually comitted
    unsigned int write_index     = atomic_load(&ring->producer_comitted_index);
    unsigned int read_index      = atomic_load(&ring->consumer_index);
    size_t       bytes_available = MIN(
        bytes_readable(ring->capacity, read_index, write_index), 
        length);
    size_t       bytes_comitted  = bytes_available;
    if (!RINGBUFFER_CAN_READ(ring, bytes_available, length)) {
        // should not happen but abort if this occurs
        return;
    }

    // Perform the actual allocation, if this fails someone else is truncating
    // or reading, abort
    if (!atomic_compare_exchange_strong(&ring->consumer_index, 
            &read_index, read_index + bytes_available)) {
        return;
    }
    
    // Synchronize with other consumers, we must wait for our turn to increament
    // the comitted index, otherwise we could end up telling writers that the wrong
    // index is writable. This can be skipped for single reader
    if (RINGBUFFER_HAS_MULTIPLE_READERS(ring)) {
        unsigned int current_commit = atomic_load(&ring->consumer_comitted_index);
        while (current_commit < (read_index - bytes_comitted)) {
            current_commit = atomic_load(&ring->consumer_comitted_index);
        }
    }
    atomic_fetch_add(&ring->consumer_comitted_index, bytes_comitted);
}

size_t
ringbuffer_write(
    _In_ ringbuffer_t* ring,
    _In_ const void*   buffer,
    _In_ size_t        length)
{
    const uint8_t*    casted_ptr    = (const uint8_t*)buffer;
    size_t            bytes_written = 0;
    FutexParameters_t parameters;

    // Make sure we write all the bytes
    while (bytes_written < length) {
        // when we check, we must check how many bytes are actually allocated, not committed
        // as we have to take into account current writers. The read index however
        // we have to only take into account how many bytes are actually read
        unsigned int write_index     = atomic_load(&ring->producer_index);
        unsigned int read_index      = atomic_load(&ring->consumer_comitted_index);
        size_t       bytes_available = MIN(
            bytes_writable(ring->capacity, read_index, write_index),
            length - bytes_written);
        size_t       bytes_comitted  = bytes_available;
        if (!RINGBUFFER_CAN_WRITE(ring, bytes_available, (length - bytes_written))) {
            if (!RINGBUFFER_CAN_BLOCK_WRITER(ring)) {
                break;
            }
            
            parameters._futex0  = (atomic_int*)&ring->consumer_comitted_index;
            parameters._val0    = (int)read_index;
            parameters._timeout = 0;
            parameters._flags   = RINGBUFFER_WAIT_FLAGS(ring);
            atomic_fetch_add(&ring->producer_count, 1);
            Syscall_FutexWait(&parameters);
            continue; // Start over
        }
        
        // Handle overwrite, empty the queue by an the needed amount of bytes
        if (bytes_available < (length - bytes_written)) {
            ringbuffer_try_truncate(ring, (length - bytes_written));
            continue;
        }
        
        // Perform the actual allocation
        if (atomic_compare_exchange_strong(&ring->producer_index, 
                &write_index, write_index + bytes_available)) {
            break;
        }

        // Write the data to the internal buffer
        while (bytes_available--) {
            ring->buffer[(write_index++ & (ring->capacity - 1))] = casted_ptr[bytes_written++];
        }
        
        // Synchronize with other producers, we must wait for our turn to increament
        // the comitted index, otherwise we could end up telling readers that the wrong
        // index is readable. This can be skipped for single writer
        if (RINGBUFFER_HAS_MULTIPLE_WRITERS(ring)) {
            unsigned int current_commit = atomic_load(&ring->producer_comitted_index);
            while (current_commit < (write_index - bytes_comitted)) {
                current_commit = atomic_load(&ring->producer_comitted_index);
            }
        }

        atomic_fetch_add(&ring->producer_comitted_index, bytes_comitted);
        parameters._val0 = atomic_exchange(&ring->consumer_count, 0);
        if (parameters._val0 != 0) {
            parameters._futex0 = (atomic_int*)&ring->producer_comitted_index;
            parameters._flags  = RINGBUFFER_WAKE_FLAGS(ring);
            Syscall_FutexWake(&parameters);
        }
    }
    return bytes_written;
}

size_t
ringbuffer_read(
    _In_ ringbuffer_t* ring,
    _In_ void*         buffer,
    _In_ size_t        length)
{
    uint8_t*          casted_ptr = (uint8_t*)buffer;
    size_t            bytes_read = 0;
    FutexParameters_t parameters;
    
    // Make sure there are bytes to read
    while (bytes_read < length) {
        // when we check, we must check how many bytes are actually allocated, not currently comitted
        // as we have to take into account current readers. The write index however
        // we have to only take into account how many bytes are actually comitted
        unsigned int write_index     = atomic_load(&ring->producer_comitted_index);
        unsigned int read_index      = atomic_load(&ring->consumer_index);
        size_t       bytes_available = MIN(
            bytes_readable(ring->capacity, read_index, write_index), 
            length - bytes_read);
        size_t       bytes_comitted  = bytes_available;
        if (!RINGBUFFER_CAN_READ(ring, bytes_available, (length - bytes_read))) {
            if (!RINGBUFFER_CAN_BLOCK_READER(ring)) {
                break;
            }
            
            parameters._futex0  = (atomic_int*)&ring->producer_comitted_index;
            parameters._val0    = (int)write_index;
            parameters._timeout = 0;
            parameters._flags   = RINGBUFFER_WAIT_FLAGS(ring);
            atomic_fetch_add(&ring->consumer_count, 1);
            Syscall_FutexWait(&parameters);
            continue; // Start over
        }

        // Perform the actual allocation
        if (!atomic_compare_exchange_strong(&ring->consumer_index, 
                &read_index, read_index + bytes_available)) {
            continue;
        }
        
        // Write the data to the provided buffer
        while (bytes_available--) {
            casted_ptr[bytes_read++] = ring->buffer[(read_index++ & (ring->capacity - 1))];
        }
        
        // Synchronize with other consumers, we must wait for our turn to increament
        // the comitted index, otherwise we could end up telling writers that the wrong
        // index is writable. This can be skipped for single reader
        if (RINGBUFFER_HAS_MULTIPLE_READERS(ring)) {
            unsigned int current_commit = atomic_load(&ring->consumer_comitted_index);
            while (current_commit < (read_index - bytes_comitted)) {
                current_commit = atomic_load(&ring->consumer_comitted_index);
            }
        }

        atomic_fetch_add(&ring->consumer_comitted_index, bytes_comitted);
        parameters._val0 = atomic_exchange(&ring->producer_count, 0);
        if (parameters._val0 != 0) {
            parameters._futex0 = (atomic_int*)&ring->consumer_comitted_index;
            parameters._flags  = RINGBUFFER_WAKE_FLAGS(ring);
            Syscall_FutexWake(&parameters);
        }
        break;
    }
    return bytes_read;
}
