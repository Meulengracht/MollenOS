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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Streambuffer Type Definitions & Structures
 * - This header describes the base streambuffer-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __STREAMBUFFER_H__
#define __STREAMBUFFER_H__

#include <ds/dsdefs.h>

// Configuration flags for the stream
#define STREAMBUFFER_MULTIPLE_READERS     0x1
#define STREAMBUFFER_MULTIPLE_WRITERS     0x2
#define STREAMBUFFER_GLOBAL               0x4
#define STREAMBUFFER_OVERWRITE_ENABLED    0x8
#define STREAMBUFFER_DISABLED             0x10

// Options for reads and writes
#define STREAMBUFFER_NO_BLOCK      0x1
#define STREAMBUFFER_ALLOW_PARTIAL 0x2
#define STREAMBUFFER_PRIORITY      0x4
#define STREAMBUFFER_PEEK          0x8

typedef struct streambuffer {
    size_t       capacity;
    unsigned int options;
    
    _Atomic(int)          consumer_count;
    _Atomic(unsigned int) consumer_index;
    _Atomic(unsigned int) consumer_comitted_index;
    _Atomic(int)          producer_count;
    _Atomic(unsigned int) producer_index;
    _Atomic(unsigned int) producer_comitted_index;
    
    uint8_t buffer[];
} streambuffer_t;

DSDECL(void,
streambuffer_construct(
    _In_ streambuffer_t* stream,
    _In_ size_t          capacity,
    _In_ unsigned int    options));

DSDECL(OsStatus_t,
streambuffer_create(
    _In_  size_t           capacity,
    _In_  unsigned int     options,
    _Out_ streambuffer_t** stream_out));

DSDECL(void,
streambuffer_set_option(
    _In_ streambuffer_t* stream,
    _In_ unsigned int    option));

DSDECL(int,
streambuffer_has_option(
    _In_ streambuffer_t* stream,
    _In_ unsigned int    option));

DSDECL(void,
streambuffer_clear_option(
    _In_ streambuffer_t* stream,
    _In_ unsigned int    option));

DSDECL(void,
streambuffer_get_bytes_available_in(
    _In_  streambuffer_t* stream,
    _Out_ size_t*         bytesAvailableOut));

DSDECL(void,
streambuffer_get_bytes_available_out(
    _In_ streambuffer_t* stream,
    _Out_ size_t*        bytesAvailableOut));

DSDECL(size_t,
streambuffer_stream_out(
    _In_ streambuffer_t* stream,
    _In_ void*           buffer,
    _In_ size_t          length,
    _In_ unsigned int    options));

DSDECL(size_t,
streambuffer_write_packet_start(
    _In_  streambuffer_t* stream,
    _In_  size_t          length,
    _In_  unsigned int    options,
    _Out_ unsigned int*   base_out,
    _Out_ unsigned int*   state_out));

DSDECL(void,
streambuffer_write_packet_data(
    _In_    streambuffer_t* stream,
    _In_    void*           buffer,
    _In_    size_t          length,
    _InOut_ unsigned int*   state));

DSDECL(void,
streambuffer_write_packet_end(
    _In_ streambuffer_t* stream,
    _In_ unsigned int    base,
    _In_ size_t          length));

DSDECL(size_t,
streambuffer_stream_in(
    _In_ streambuffer_t* stream,
    _In_ void*           buffer,
    _In_ size_t          length,
    _In_ unsigned int    options));

DSDECL(size_t,
streambuffer_read_packet_start(
    _In_  streambuffer_t* stream,
    _In_  unsigned int    options,
    _Out_ unsigned int*   base_out,
    _Out_ unsigned int*   state_out));

DSDECL(void,
streambuffer_read_packet_data(
    _In_    streambuffer_t* stream,
    _In_    void*           buffer,
    _In_    size_t          length,
    _InOut_ unsigned int*   state));

DSDECL(void,
streambuffer_read_packet_end(
    _In_ streambuffer_t* stream,
    _In_ unsigned int    base,
    _In_ size_t          length));

#endif //!__RINGBUFFER_H__
